#include "core/live.hpp"

#include "core/json.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace dr2 {

namespace {

std::uint32_t u32(const unsigned char* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

void floats(const unsigned char* p, float* out, int n) { std::memcpy(out, p, static_cast<std::size_t>(n) * 4); }

double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

bool parse_live(const void* data, std::size_t size, LiveSample& out) {
    const auto* p = static_cast<const unsigned char*>(data);
    if (size != kLivePacketSize || std::memcmp(p, "DR2L", 4) != 0 || u32(p + 4) != 1) return false;
    LiveSample s;
    s.seq = u32(p + 8);
    const std::uint32_t flags = u32(p + 12);
    s.car = flags & 1u;
    s.view = flags & 2u;
    s.paused = flags & 4u;
    floats(p + 16, s.car_pos.data(), 3);
    floats(p + 28, s.car_rot, 9);
    floats(p + 64, s.car_vel.data(), 3);
    floats(p + 76, s.eye.data(), 3);
    floats(p + 88, s.forward.data(), 3);
    floats(p + 100, s.up.data(), 3);
    s.top = u32(p + 112);
    floats(p + 116, &s.speed_kmh, 1);
    out = s;
    return true;
}

Vec3 GameTransform::dir_to_track(const Vec3& d) const {
    // giro_y(v, a): x' = cos a·x − sin a·z, z' = sin a·x + cos a·z; aqui com a = −yaw
    const double a = -static_cast<double>(yaw_deg) * M_PI / 180.0;
    const double c = std::cos(a), s = std::sin(a);
    return {static_cast<float>(c * d[0] - s * d[2]), d[1], static_cast<float>(s * d[0] + c * d[2])};
}

Vec3 GameTransform::point_to_track(const Vec3& p) const {
    return dir_to_track({p[0] - offset[0], p[1] - offset[1], p[2] - offset[2]});
}

GameTransform load_game_transform(const std::string& track_dir) {
    GameTransform t;
    const std::filesystem::path path = std::filesystem::path(track_dir) / "game_transform.json";
    if (!std::filesystem::exists(path)) return t;
    const json::Value v = json::parse_file(path.string());
    t.yaw_deg = static_cast<float>(v["yaw_deg"].as_number());
    const json::Array& o = v["offset"].as_array();
    if (o.size() != 3) throw std::runtime_error("game_transform.json: offset precisa de 3 números");
    for (int k = 0; k < 3; ++k) t.offset[static_cast<std::size_t>(k)] = static_cast<float>(o[static_cast<std::size_t>(k)].as_number());
    t.loaded = true;
    return t;
}

LiveReceiver::~LiveReceiver() {
    if (fd_ >= 0) ::close(fd_);
}

bool LiveReceiver::open(std::string& error) {
    if (fd_ >= 0) return true;
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        error = std::string("socket: ") + std::strerror(errno);
        return false;
    }
    // DR2_LIVE_PORT troca a porta (testes com emissor falso enquanto outro editor usa a de sempre)
    std::uint16_t port = kLivePort;
    if (const char* env = std::getenv("DR2_LIVE_PORT")) {
        const long p = std::strtol(env, nullptr, 10);
        if (p > 0 && p < 65536) port = static_cast<std::uint16_t>(p);
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0) {
        error = "porta " + std::to_string(port) + " ocupada (outro editor aberto?): " + std::strerror(errno);
        ::close(fd);
        return false;
    }
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    fd_ = fd;
    return true;
}

bool LiveReceiver::poll() {
    if (fd_ < 0) return false;
    bool fresh = false;
    unsigned char buf[256];
    for (;;) {
        const ssize_t n = ::recv(fd_, buf, sizeof buf, 0);
        if (n < 0) break;
        LiveSample s;
        if (!parse_live(buf, static_cast<std::size_t>(n), s)) continue;
        last_ = s;
        fresh = true;
        ++received_;
    }
    if (fresh) last_time_ = now_seconds();
    return fresh;
}

double LiveReceiver::age() const {
    return last_time_ < 0 ? std::numeric_limits<double>::infinity() : now_seconds() - last_time_;
}

}  // namespace dr2
