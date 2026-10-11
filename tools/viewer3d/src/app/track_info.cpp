#include "app/track_info.hpp"

#include "core/json.hpp"
#include "edit/edits_json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace dr2::app {

namespace {

// src/trackpkg/builder.cpp (kMapX0, kMapXPerLon, kMapY0, kMapYPerLat) e a caixa do nó image!2 do mapa.
constexpr double kMapX0 = 1.96478, kMapXPerLon = 0.0129152;
constexpr double kMapY0 = -1.74648, kMapYPerLat = 0.0157565;
constexpr double kMapW = 4.4, kMapH = 2.64;

std::string str_or(const json::Value& v, const char* key, const std::string& fallback) {
    const json::Value* f = v.find(key);
    return f && f->is_string() ? f->as_string() : fallback;
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

}  // namespace

const CountryPreset kCountries[] = {
    {"Argentina", -31.4, -64.2},     {"Australia", -35.3, 149.1},      {"Austria", 47.5, 14.5},
    {"Belgium", 50.5, 4.5},          {"Brazil", -15.8, -47.9},         {"Canada", 45.5, -73.6},
    {"Chile", -33.4, -70.6},         {"China", 39.9, 116.4},           {"Croatia", 45.1, 15.2},
    {"Czech Republic", 49.8, 15.5},  {"Estonia", 58.6, 25.0},          {"Finland", 62.2, 25.7},
    {"France", 46.6, 2.4},           {"Germany", 51.2, 10.4},          {"Greece", 38.0, 23.7},
    {"Ireland", 53.4, -8.0},         {"Italy", 42.5, 12.5},            {"Japan", 35.7, 139.7},
    {"Kenya", -1.3, 36.8},           {"Latvia", 56.9, 24.1},           {"Mexico", 21.1, -101.7},
    {"New Zealand", -41.3, 174.8},   {"Norway", 60.5, 8.5},            {"Poland", 52.0, 19.1},
    {"Portugal", 39.6, -8.0},        {"Scotland", 56.5, -4.2},         {"South Africa", -26.2, 28.0},
    {"Spain", 40.4, -3.7},           {"Sweden", 60.1, 15.0},           {"Turkey", 39.9, 32.9},
    {"United Arab Emirates", 24.5, 54.4}, {"United Kingdom", 52.4, -1.5}, {"United States", 39.8, -98.6},
    {"Wales", 52.3, -3.7},
};
const int kCountryCount = static_cast<int>(sizeof kCountries / sizeof kCountries[0]);

void map_uv(double lat, double lon, float* u, float* v) {
    *u = static_cast<float>((kMapX0 + kMapXPerLon * lon) / kMapW);
    *v = static_cast<float>(-(kMapY0 + kMapYPerLat * lat) / kMapH);
}

void map_latlon(float u, float v, double* lat, double* lon) {
    *lon = std::clamp((u * kMapW - kMapX0) / kMapXPerLon, -180.0, 180.0);
    *lat = std::clamp((-v * kMapH - kMapY0) / kMapYPerLat, -90.0, 90.0);
}

std::string TrackInfo::path(const std::string& dir) { return (std::filesystem::path(dir) / "package.json").string(); }

TrackInfo TrackInfo::load(const std::string& dir, bool* exists) {
    TrackInfo t;
    const std::string p = path(dir);
    std::error_code ec;
    const bool there = std::filesystem::exists(p, ec);
    if (exists) *exists = there;
    if (!there) return t;
    const json::Value v = json::parse_file(p);
    t.name = str_or(v, "name", "");
    t.country = str_or(v, "country", "");
    t.description = str_or(v, "description", "");
    t.author = str_or(v, "author", "");
    t.version = str_or(v, "version", "1");
    if (const json::Value* m = v.find("map"); m && m->is_object()) {
        const json::Value *la = m->find("lat"), *lo = m->find("lon");
        if (la && lo && la->is_number() && lo->is_number()) {
            t.has_map = true;
            t.lat = la->as_number();
            t.lon = lo->as_number();
        }
    }
    if (const json::Value* c = v.find("card"); c && c->is_object()) {
        const json::Value* cam = c->find("camera");
        if (cam && cam->is_array() && cam->size() == 6) {
            t.has_card = true;
            for (std::size_t k = 0; k < 6; ++k) t.card[k] = static_cast<float>((*cam)[k].as_number());
            t.card_aspect = static_cast<float>(c->number_or("aspect", 16.0 / 9.0));
        }
    }
    return t;
}

void TrackInfo::save(const std::string& dir) const {
    std::string s = "{\n";
    s += " \"name\": " + json::quote(name) + ",\n";
    s += " \"country\": " + json::quote(country) + ",\n";
    s += " \"description\": " + json::quote(description) + ",\n";
    s += " \"author\": " + json::quote(author) + ",\n";
    s += " \"version\": " + json::quote(version);
    if (has_map) s += ",\n \"map\": {\"lat\": " + num(lat) + ", \"lon\": " + num(lon) + "}";
    if (has_card) {
        s += ",\n \"card\": {\"camera\": [";
        for (int k = 0; k < 6; ++k) s += (k ? ", " : "") + num(card[k]);
        s += "], \"aspect\": " + num(card_aspect) + "}";
    }
    s += "\n}\n";
    edit::write_text(path(dir), s);
}

bool TrackInfo::operator==(const TrackInfo& o) const {
    return name == o.name && country == o.country && description == o.description && author == o.author &&
           version == o.version && has_map == o.has_map && (!has_map || (lat == o.lat && lon == o.lon)) &&
           has_card == o.has_card &&
           (!has_card || (std::equal(card, card + 6, o.card) && card_aspect == o.card_aspect));
}

}  // namespace dr2::app
