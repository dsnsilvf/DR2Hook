// track.json exportado por tools/uiview/track/export.py. Especificação em
// docs/plans/viewer3d/formatos.md#trackjson.
#pragma once

#include "core/json.hpp"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace dr2 {

using Vec3 = std::array<float, 3>;

struct Gate {
    double d = 0;
    Vec3 l{}, r{};
};

struct AiLine {
    std::string name;
    std::vector<Vec3> pts;
};

struct Route {
    std::string name;
    std::string terrain_file;
    std::size_t terrain_meshes = 0, terrain_verts = 0;
    std::size_t instances = 0;
    std::vector<Gate> gates;
    std::vector<AiLine> ai;
    std::vector<std::string> ens_ids;  // id de texto de cada instância e: por idnum (só rótulo)
};

struct TypeInfo {
    std::string name;  // "<k>:<nome>"
    std::size_t first = 0, count = 0;  // faixa de malhas em objects.bin
};

struct Track {
    std::string dir;  // pasta da pista
    std::string id, src;
    std::vector<Route> routes;
    std::vector<TypeInfo> types;  // na ordem de type_order (o tipo do DR2I indexa esta lista)
    std::map<std::string, std::string, std::less<>> materials;  // material -> "tex/arquivo.webp"
    json::Value raw;
};

// Lê <dir>/track.json. Lança std::runtime_error se faltar campo obrigatório.
Track read_track(const std::string& dir);

// Caixa (lo, hi) de todos os pontos da IA e dos portões de todas as rotas; false se não houver ponto.
bool route_bounds(const Track& track, Vec3& lo, Vec3& hi);

}  // namespace dr2
