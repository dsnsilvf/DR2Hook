#include "edit/edits_json.hpp"

#include "core/json.hpp"
#include "edit/history.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace dr2::edit {

namespace {

std::string floats(const float* v) {
    std::string out = "[";
    char buf[32];
    for (std::size_t k = 0; k < kInstFloats; ++k) {
        std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(v[k]));
        if (k) out += ',';
        out += buf;
    }
    return out + "]";
}

}  // namespace

std::string edits_json(const Track& track, const std::vector<RouteEdits>& routes, std::size_t* count) {
    std::string out = "{\"format\":\"dr2-track-edits\",\"version\":1,\"track\":" + json::quote(track.id) +
                      ",\"src\":" + json::quote(track.src) + ",\"edits\":[";
    std::size_t n = 0;
    for (const RouteEdits& re : routes) {
        const Instances& inst = *re.inst;
        for (std::size_t i = 0; i < inst.n; ++i) {
            const bool added = inst.idnum[i] >= kAdded;
            if (added ? inst.hidden[i] != 0 : !changed(inst, i)) continue;
            const std::string& name = track.types.at(inst.type[i]).name;
            const std::string kind = name.substr(0, 1);
            const std::string type = name.size() > 2 ? name.substr(2) : std::string();
            if (n++) out += ',';
            out += "\n{\"route\":" + json::quote(re.route->name) + ",\"kind\":" + json::quote(kind) + ",\"type\":" + json::quote(type);
            if (added) out += ",\"added\":true,\"src\":" + std::to_string(inst.idnum[i] - kAdded) + ",\"index\":-1,\"deleted\":false";
            else out += ",\"index\":" + std::to_string(inst.idnum[i]) + ",\"deleted\":" + (inst.hidden[i] ? "true" : "false");
            out += ",\"m\":" + floats(inst.matrix(i)) + ",\"m0\":" + floats(&inst.m0[i * kInstFloats]) + "}";
        }
    }
    out += "\n]}\n";
    if (count) *count = n;
    return out;
}

bool inside_game_folder(const std::string& path) {
    std::string p = std::filesystem::absolute(path).lexically_normal().generic_string();
    std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::replace(p.begin(), p.end(), '\\', '/');
    const std::string game = "/steamapps/common/dirt rally 2.0";
    return p.find(game + "/") != std::string::npos || p.ends_with(game);
}

void write_text(const std::string& path, const std::string& text) {
    if (inside_game_folder(path)) throw std::runtime_error("recusado: " + path + " fica dentro da pasta do jogo");
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out || !out.write(text.data(), static_cast<std::streamsize>(text.size())))
        throw std::runtime_error("não gravou " + path);
}

}  // namespace dr2::edit
