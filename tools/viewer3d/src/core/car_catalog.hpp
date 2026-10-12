// Lista dos carros do jogo para Testar no jogo > Veículo: o catalog.json que o scripts/research/car_catalog.py
// grava (do base.ctpk e dos textos do jogo) ao lado dos modelos do editor, em <viewer3d>/cars/.
#pragma once

#include "core/json.hpp"

#include <string>
#include <vector>

namespace dr2 {

struct CarClass {
    std::string id;  // "rally_r5"
    std::string pt, en;
    const std::string& name() const;  // no idioma da interface
};

struct CarInfo {
    std::string id;  // "fr5": o cars/<id>.nefs e o vehicle do AutoStage
    std::string pt, en;
    std::string cls;  // CarClass::id
    int year = 0;
    std::vector<std::string> liveries;  // pinturas numeradas ("00", "01"); nunca vazia
    const std::string& name() const;
};

struct CarCatalog {
    std::vector<CarClass> classes;  // na ordem do menu do jogo
    std::vector<CarInfo> cars;      // agrupados por classe, na ordem de `classes`
    const CarInfo* find(const std::string& id) const;
    const CarClass* find_class(const std::string& id) const;
};

// Lança std::runtime_error se o documento não tem o formato (versão 1).
CarCatalog parse_car_catalog(const json::Value& doc);
CarCatalog load_car_catalog(const std::string& path);

}  // namespace dr2
