#include "edit/edits_json.hpp"

#include "core/json.hpp"
#include "edit/history.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
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

namespace {

void check_doc(const json::Value& doc, const Track& track) {
    const json::Value* fmt = doc.find("format");
    if (!fmt || !fmt->is_string() || fmt->as_string() != "dr2-track-edits" || doc.number_or("version", 0) != 1)
        throw std::runtime_error("não é um edits.json (dr2-track-edits v1)");
    const json::Value* id = doc.find("track");
    if (id && id->is_string() && id->as_string() != track.id)
        throw std::runtime_error("o edits.json é da pista " + id->as_string() + ", não de " + track.id);
}

bool read_floats(const json::Value& v, float* out) {
    if (!v.is_array() || v.size() != kInstFloats) return false;
    for (std::size_t k = 0; k < kInstFloats; ++k) {
        if (!v[k].is_number()) return false;
        out[k] = static_cast<float>(v[k].as_number());
    }
    return true;
}

bool near(const float* a, const float* b) {
    for (std::size_t k = 0; k < kInstFloats; ++k)
        if (std::fabs(a[k] - b[k]) > 1e-3f * std::max(1.0f, std::fabs(b[k]))) return false;
    return true;
}

}  // namespace

std::vector<std::string> routes_in_edits(const json::Value& doc) {
    std::vector<std::string> out;
    const json::Value* edits = doc.find("edits");
    if (!edits || !edits->is_array()) return out;
    for (const json::Value& e : edits->as_array()) {
        const json::Value* r = e.find("route");
        if (r && r->is_string() && std::find(out.begin(), out.end(), r->as_string()) == out.end()) out.push_back(r->as_string());
    }
    return out;
}

void apply_edits(const json::Value& doc, const Track& track, const Route& route, Instances& inst, ApplyReport& report) {
    check_doc(doc, track);
    const json::Value& edits = doc["edits"];
    for (std::size_t n = 0; n < edits.size(); ++n) {
        const json::Value& e = edits[n];
        const json::Value* r = e.find("route");
        if (!r || !r->is_string() || r->as_string() != route.name) continue;
        const std::string where = route.name + " #" + std::to_string(n + 1);
        const json::Value* kind = e.find("kind");
        const json::Value* type = e.find("type");
        float m[kInstFloats], m0[kInstFloats];
        if (!kind || !kind->is_string() || !type || !type->is_string() || !read_floats(e["m"], m) || !read_floats(e["m0"], m0)) {
            report.skipped.push_back(where + ": entrada incompleta");
            continue;
        }
        const std::string name = kind->as_string() + ":" + type->as_string();
        const json::Value* added = e.find("added");
        const bool is_added = added && added->is_bool() && added->as_bool();
        const double key = is_added ? e.number_or("src", -1) : e.number_or("index", -1);
        if (key < 0 || key >= static_cast<double>(kAdded)) {
            report.skipped.push_back(where + ": índice inválido");
            continue;
        }
        const auto id = static_cast<std::uint32_t>(key);
        std::size_t found = inst.n;
        for (std::size_t i = 0; i < inst.n && found == inst.n; ++i)
            if (inst.idnum[i] == id && track.types.at(inst.type[i]).name == name) found = i;
        if (found == inst.n) {
            report.skipped.push_back(where + ": " + name + " #" + std::to_string(id) + " não existe nesta exportação");
            continue;
        }
        if (is_added) {
            inst.type.push_back(inst.type[found]);
            inst.idnum.push_back(kAdded + id);
            inst.m.insert(inst.m.end(), m, m + kInstFloats);
            inst.m0.insert(inst.m0.end(), m0, m0 + kInstFloats);
            inst.hidden.push_back(0);
            ++inst.n;
        } else {
            if (!near(m0, &inst.m0[found * kInstFloats])) {
                report.skipped.push_back(where + ": " + name + " #" + std::to_string(id) + " mudou no arquivo desde a edição");
                continue;
            }
            std::copy(m, m + kInstFloats, inst.matrix(found));
            const json::Value* del = e.find("deleted");
            inst.hidden[found] = del && del->is_bool() && del->as_bool() ? 1 : 0;
        }
        ++report.applied;
    }
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
    // grava ao lado e renomeia: uma falha no meio não deixa o arquivo antigo pela metade
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(text.data(), static_cast<std::streamsize>(text.size())) || !out.flush()) {
            out.close();
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            throw std::runtime_error("não gravou " + path);
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        throw std::runtime_error("não gravou " + path + ": " + ec.message());
    }
}

std::string backup_existing(const std::string& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return {};
    for (int k = 1; k < 1000; ++k) {
        const std::string bak = path + "." + std::to_string(k) + ".bak";
        if (std::filesystem::exists(bak, ec)) continue;
        std::filesystem::copy_file(path, bak, ec);
        if (ec) throw std::runtime_error("não copiou " + path + " para " + bak + ": " + ec.message());
        return bak;
    }
    throw std::runtime_error("já há 999 cópias de " + path);
}

}  // namespace dr2::edit
