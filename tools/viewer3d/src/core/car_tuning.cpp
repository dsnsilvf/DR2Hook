#include "core/car_tuning.hpp"

#include "core/i18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace dr2 {

using i18n::tr;

namespace {

bool starts(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

std::string printf_text(const char* format, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, format, v);
    return buf;
}

// prefixo do nome -> grupo; o resto vai para "Outros"
constexpr std::string_view kGroupPrefix[kTuningGroups - 1] = {
    "brakes_", "differential_front_", "differential_centre_", "differential_rear_", "gearbox_",
    "suspension_front_", "suspension_rear_", "damping_front_", "damping_rear_"};

}  // namespace

int TuningParam::index(float v) const {
    if (steps < 2 || !(max > min)) return 0;
    const float k = std::round((v - min) / step());
    return static_cast<int>(std::clamp(k, 0.0f, static_cast<float>(steps - 1)));
}

float TuningParam::at(int k) const {
    if (steps < 2) return min;
    k = std::clamp(k, 0, steps - 1);
    return k == steps - 1 ? max : min + step() * static_cast<float>(k);
}

const TuningParam* CarTuning::find(std::string_view name) const {
    for (const TuningParam& p : params)
        if (p.name == name) return &p;
    return nullptr;
}

CarTuning parse_car_tuning(const json::Value& doc) {
    if (doc.number_or("version", 0) != 1) throw std::runtime_error("tuning: versão desconhecida");
    CarTuning out;
    if (const json::Value* c = doc.find("car"); c && c->is_string()) out.car = c->as_string();
    for (const json::Value& p : doc["params"].as_array()) {
        const json::Value* n = p.find("name");
        if (!n || !n->is_string()) continue;
        out.params.push_back({n->as_string(), static_cast<int>(p.number_or("state", 0)), static_cast<float>(p.number_or("default", 0)),
                              static_cast<float>(p.number_or("min", 0)), static_cast<float>(p.number_or("max", 0)),
                              static_cast<int>(p.number_or("steps", 0))});
    }
    return out;
}

CarTuning load_car_tuning(const std::string& path) { return parse_car_tuning(json::parse_file(path)); }

int tuning_group(std::string_view name) {
    for (int g = 0; g < kTuningGroups - 1; ++g)
        if (starts(name, kGroupPrefix[g])) return g;
    return kTuningGroups - 1;
}

const char* tuning_group_name(int group) {
    switch (group) {
    case 0: return tr("Freios");
    case 1: return tr("Diferencial dianteiro");
    case 2: return tr("Diferencial central");
    case 3: return tr("Diferencial traseiro");
    case 4: return tr("Câmbio");
    case 5: return tr("Suspensão dianteira");
    case 6: return tr("Suspensão traseira");
    case 7: return tr("Amortecedores dianteiros");
    case 8: return tr("Amortecedores traseiros");
    default: return tr("Outros");
    }
}

std::string tuning_label(std::string_view name) {
    const int g = tuning_group(name);
    const std::string_view s = g < kTuningGroups - 1 ? name.substr(kGroupPrefix[g].size()) : name;
    if (g == 0) {
        if (s == "bias") return tr("Distribuição para a frente");
        if (s == "strength") return tr("Força");
        if (s == "rear_saturation") return tr("Limite do freio traseiro");
    } else if (g >= 1 && g <= 3) {
        if (s == "driving_lock") return tr("Trava acelerando");
        if (s == "braking_lock") return tr("Trava freando");
        if (s == "preload") return tr("Pré-carga");
        if (s == "viscosity") return tr("Viscosidade");
        if (s == "torque_distribution") return tr("Divisão do torque");
    } else if (g == 4) {
        if (s == "final_drive") return tr("Relação final");
        if (s.size() == 1 && s[0] >= '1' && s[0] <= '9') return printf_text(tr("%.0fª marcha"), s[0] - '0');
    } else if (g == 5 || g == 6) {
        if (s == "ride_height") return tr("Altura do chão");
        if (s == "spring_rate") return tr("Mola");
        if (s == "tender_spring_rate") return tr("Mola auxiliar");
        if (s == "anti_roll") return tr("Barra estabilizadora");
        if (s == "camber") return tr("Cambagem");
        if (s == "toe") return tr("Convergência");
        if (s == "air_pressure") return tr("Pressão da suspensão");
    } else if (g == 7 || g == 8) {
        if (s == "bump") return tr("Compressão");
        if (s == "fast_bump") return tr("Compressão rápida");
        if (s == "fast_bump_threshold") return tr("Início da compressão rápida");
        if (s == "rebound") return tr("Retorno");
        if (s == "fast_rebound") return tr("Retorno rápido");
        if (s == "fast_rebound_threshold") return tr("Início do retorno rápido");
    } else if (s == "spare_tyre_count") {
        return tr("Estepes");
    }
    return std::string(name);
}

std::string tuning_value_text(std::string_view name, float value) {
    const double v = value;
    auto ends = [&](std::string_view suffix) {
        return name.size() >= suffix.size() && name.substr(name.size() - suffix.size()) == suffix;
    };
    if (name == "brakes_bias") return printf_text(tr("%.0f%% frente"), v * 100.0);
    if (name == "brakes_strength") return printf_text("%.0f", v);
    if (ends("_lock") || ends("torque_distribution")) return printf_text("%.0f%%", v * 100.0);
    if (ends("_preload")) return printf_text("%.0f N·m", v);
    if (starts(name, "gearbox_")) return printf_text("%.3f", v);
    if (ends("ride_height")) return printf_text("%.0f mm", v * 1000.0);
    if (ends("spring_rate") || ends("anti_roll")) return printf_text("%.1f N/mm", v / 1000.0);
    if (ends("camber") || ends("toe")) return printf_text("%.2f°", v);
    if (ends("_threshold")) return printf_text("%.0f mm/s", v * 1000.0);  // velocidade do amortecedor (0..0,2)
    if (starts(name, "damping_")) {
        const double k = std::round(v);
        return k == 0.0 ? std::string("0") : printf_text("%+.0f", k);
    }
    if (name == "spare_tyre_count") return printf_text("%.0f", v);
    return printf_text("%.3g", v);
}

std::string tuning_arg(const TuningValues& values) {
    std::string out;
    for (const auto& [name, v] : values) {
        char buf[48];
        std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(v));  // volta ao mesmo float no strtof da dxgi
        out += (out.empty() ? "" : ",") + name + "=" + buf;
    }
    return out;
}

bool parse_tuning_arg(std::string_view text, TuningValues& out) {
    TuningValues got;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string item(text.substr(0, comma));
        text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
        if (item.empty()) continue;
        const std::size_t eq = item.find('=');
        if (eq == 0 || eq == std::string::npos) return false;
        const std::string num = item.substr(eq + 1);
        char* end = nullptr;
        const float v = std::strtof(num.c_str(), &end);
        if (num.empty() || *end || !std::isfinite(v)) return false;
        got[item.substr(0, eq)] = v;
    }
    out = std::move(got);
    return true;
}

}  // namespace dr2
