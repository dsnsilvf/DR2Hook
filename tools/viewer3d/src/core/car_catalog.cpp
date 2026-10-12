#include "core/car_catalog.hpp"

#include "core/i18n.hpp"

#include <stdexcept>

namespace dr2 {

namespace {

const std::string& pick(const std::string& pt, const std::string& en) {
    return i18n::language() == i18n::Lang::Pt && !pt.empty() ? pt : en.empty() ? pt : en;
}

std::string text(const json::Value& v, const char* key) {
    const json::Value* f = v.find(key);
    return f && f->is_string() ? f->as_string() : std::string();
}

}  // namespace

const std::string& CarClass::name() const { return pick(pt, en); }
const std::string& CarInfo::name() const { return pick(pt, en); }

const CarInfo* CarCatalog::find(const std::string& id) const {
    for (const CarInfo& c : cars)
        if (c.id == id) return &c;
    return nullptr;
}

const CarClass* CarCatalog::find_class(const std::string& id) const {
    for (const CarClass& c : classes)
        if (c.id == id) return &c;
    return nullptr;
}

CarCatalog parse_car_catalog(const json::Value& doc) {
    if (doc.number_or("version", 0) != 1) throw std::runtime_error("catalog.json: versão desconhecida");
    CarCatalog out;
    for (const json::Value& c : doc["classes"].as_array())
        out.classes.push_back({text(c, "id"), text(c, "pt"), text(c, "en")});
    for (const json::Value& c : doc["cars"].as_array()) {
        CarInfo car{text(c, "id"), text(c, "pt"), text(c, "en"), text(c, "class"), static_cast<int>(c.number_or("year", 0)), {}};
        if (car.id.empty()) continue;
        if (const json::Value* l = c.find("liveries"); l && l->is_array())
            for (const json::Value& v : l->as_array())
                if (v.is_string()) car.liveries.push_back(v.as_string());
        if (car.liveries.empty()) car.liveries.push_back("00");
        if (car.pt.empty() && car.en.empty()) car.en = car.id;
        out.cars.push_back(std::move(car));
    }
    return out;
}

CarCatalog load_car_catalog(const std::string& path) { return parse_car_catalog(json::parse_file(path)); }

}  // namespace dr2
