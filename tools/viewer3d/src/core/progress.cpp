#include "core/progress.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace dr2 {

namespace {

// "<palavra> <resto>": tira a primeira palavra de `s`.
std::string take_word(std::string& s) {
    const std::size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) {
        s.clear();
        return {};
    }
    const std::size_t b = s.find(' ', a);
    std::string w = s.substr(a, b == std::string::npos ? std::string::npos : b - a);
    s = b == std::string::npos ? std::string() : s.substr(b + 1);
    return w;
}

bool to_int(const std::string& w, int& out) {
    if (w.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(w.c_str(), &end, 10);
    if (*end || v < 0 || v > 100000) return false;
    out = static_cast<int>(v);
    return true;
}

}  // namespace

void Progress::feed(std::string line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    bool command = false;
    if (line.size() > 1 && line[0] == '@') {
        std::string rest = line.substr(1);
        const std::string cmd = take_word(rest);
        command = true;
        if (cmd == "step") {
            int k = 0, n = 0;
            std::string r = rest;
            if (to_int(take_word(r), k) && to_int(take_word(r), n) && k >= 1 && k <= n) {
                step = k;
                steps = n;
                label = r;
                log.push_back("– " + std::to_string(k) + "/" + std::to_string(n) + " " + r);
            } else {
                command = false;
            }
        } else if (cmd == "progress") {
            char* end = nullptr;
            const float f = std::strtof(rest.c_str(), &end);
            if (end != rest.c_str() && std::isfinite(f)) fraction = std::max(fraction, std::clamp(f, 0.0f, 1.0f));
            else command = false;
        } else if (cmd == "done" || cmd == "fail") {
            if (state == State::Running) {
                state = cmd == "done" ? State::Done : State::Failed;
                result = rest;
                if (state == State::Done) fraction = 1.0f;
            }
            log.push_back((cmd == "done" ? "pronto: " : "erro: ") + rest);
        } else {
            command = false;
        }
    }
    if (!command) log.push_back(std::move(line));
    while (log.size() > max_log) log.pop_front();
}

void Progress::exited(int code) {
    if (state != State::Running) return;
    state = State::Failed;
    result = "o processo saiu sem terminar (código " + std::to_string(code) + ")";
    log.push_back("erro: " + result);
}

}  // namespace dr2
