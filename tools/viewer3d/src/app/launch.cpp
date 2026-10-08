#include "app/launch.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string_view>

#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace dr2::app {

#ifdef _WIN32

GameWatch::GameWatch() = default;
GameWatch::~GameWatch() = default;

ChildProcess::~ChildProcess() = default;
bool ChildProcess::start(const std::vector<std::string>&, std::string& error) {
    error = "Testar no jogo só existe no Linux por enquanto";
    return false;
}
void ChildProcess::poll(Progress&) {}
void ChildProcess::terminate() {}

#else

ChildProcess::~ChildProcess() {
    if (fd_ >= 0) ::close(fd_);
}

bool ChildProcess::start(const std::vector<std::string>& argv, std::string& error) {
    if (running() || argv.empty()) {
        error = running() ? "já tem um teste rodando" : "nada para rodar";
        return false;
    }
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0) {
        error = std::string("pipe: ") + std::strerror(errno);
        return false;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 2);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    // grupo próprio (cancelar pega o script e a etapa dele) e sinais no padrão (o SDL mexe em alguns)
    sigset_t def;
    sigemptyset(&def);
    sigaddset(&def, SIGPIPE);
    sigaddset(&def, SIGTERM);
    sigaddset(&def, SIGINT);
    posix_spawnattr_setsigdefault(&attr, &def);
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setpgroup(&attr, 0);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    std::vector<char*> args;
    for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    pid_t pid = -1;
    const int rc = posix_spawnp(&pid, args[0], &fa, &attr, args.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    ::close(fds[1]);
    if (rc != 0) {
        ::close(fds[0]);
        error = argv[0] + ": " + std::strerror(rc);
        return false;
    }
    ::fcntl(fds[0], F_SETFL, ::fcntl(fds[0], F_GETFL) | O_NONBLOCK);
    pid_ = pid;
    fd_ = fds[0];
    partial_.clear();
    start_ = std::chrono::steady_clock::now();
    return true;
}

void ChildProcess::poll(Progress& progress) {
    if (fd_ >= 0) {
        char buf[4096];
        for (;;) {
            const ssize_t n = ::read(fd_, buf, sizeof buf);
            if (n > 0) {
                partial_.append(buf, static_cast<std::size_t>(n));
                std::size_t at;
                while ((at = partial_.find('\n')) != std::string::npos) {
                    progress.feed(partial_.substr(0, at));
                    partial_.erase(0, at + 1);
                }
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            if (n == 0) {  // fim: o filho fechou a saída
                if (!partial_.empty()) progress.feed(partial_);
                partial_.clear();
                ::close(fd_);
                fd_ = -1;
            }
            break;
        }
    }
    if (pid_ > 0) {
        int status = 0;
        const pid_t r = ::waitpid(pid_, &status, WNOHANG);
        if (r == pid_) {
            pid_ = -1;
            if (fd_ < 0) {
                progress.exited(WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
            } else {
                // a saída ainda pode ter linhas: lê no próximo poll; o resultado vem do @done/@fail ou do exited abaixo
                exit_code_pending_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            }
        }
    }
    if (pid_ < 0 && fd_ < 0 && exit_code_pending_ >= 0) {
        progress.exited(exit_code_pending_);
        exit_code_pending_ = -1;
    }
}

void ChildProcess::terminate() {
    if (pid_ > 0) ::kill(-pid_, SIGTERM);
}

namespace {

// Algum processo com o comm "dirtrally2.exe" em /proc.
bool game_process() {
    DIR* proc = ::opendir("/proc");
    if (!proc) return false;
    bool found = false;
    while (const dirent* e = ::readdir(proc)) {
        if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        char path[64], comm[32];
        std::snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
        const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        const ssize_t n = ::read(fd, comm, sizeof comm - 1);
        ::close(fd);
        if (n > 0 && std::string_view(comm, static_cast<std::size_t>(n)) == "dirtrally2.exe\n") {
            found = true;
            break;
        }
    }
    ::closedir(proc);
    return found;
}

}  // namespace

GameWatch::GameWatch() {
    thread_ = std::thread([this] {
        std::unique_lock lock(mutex_);
        while (!stop_) {
            lock.unlock();
            running_.store(game_process(), std::memory_order_relaxed);
            lock.lock();
            wake_.wait_for(lock, std::chrono::seconds(1), [this] { return stop_; });
        }
    });
}

GameWatch::~GameWatch() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

#endif

std::string find_repo(const std::string& track_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path script = fs::path("scripts") / "research" / "ring_deploy.py";
    if (fs::is_regular_file(script, ec)) return fs::current_path(ec).generic_string();
    for (fs::path p = fs::absolute(track_dir, ec).lexically_normal(); !p.empty(); p = p.parent_path()) {
        if (fs::is_regular_file(p / script, ec)) return p.generic_string();
        if (p == p.parent_path()) break;
    }
    return {};
}

}  // namespace dr2::app
