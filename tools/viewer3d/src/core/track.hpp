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

// Câmeras de replay (track.json routes[i].replay, gerado por tools/synthtrack/cameras.py).
struct ReplayCamera {
    std::string name, kind, role;  // kind: trackside, static ou dolly
    double s = -1;                 // distância na pista (-1 = sem)
    Vec3 pos{}, aim{};
    std::vector<Vec3> path, target;  // Bézier cúbicas, grupos de 4 pontos (só dolly)
    double duration = 0;
};

struct ReplaySwitch {
    std::string camera;  // do plano ou do carro (onboard_front, external_front_R...)
    double p = 0;
};

struct ReplayZone {
    std::string name;
    double s = 0;
    Vec3 l{}, r{};
    int lap = 0;  // > 0: só nesta volta
    std::vector<ReplaySwitch> sw;
};

struct ReplayBound {
    std::string name;
    std::array<std::array<float, 2>, 4> corners{};  // x, z
    float y0 = 0, y1 = 0;
};

struct Replay {
    std::vector<ReplayCamera> cameras;
    std::vector<ReplayZone> zones;
    std::vector<ReplayBound> bounds;
    int find(const std::string& name) const {
        for (std::size_t i = 0; i < cameras.size(); ++i)
            if (cameras[i].name == name) return static_cast<int>(i);
        return -1;
    }
};

// Vagas de largada (track.json routes[i].grids, gerado por tools/synthtrack/grids.py): onde o carro nasce.
struct GridSlot {
    std::string name;
    Vec3 pos{}, fwd{0, 0, 1};  // centro da vaga e para onde o carro aponta (unitário)
    double s = -1, lat = 0;    // referencial da pista (-1 = sem)
    float width = 2.8f, length = 5.5f;
};

struct Grid {
    std::string name, role;
    Vec3 pos{}, fwd{0, 0, 1};
    std::vector<GridSlot> slots;
    std::vector<GridSlot> markers;  // nós de apoio sem carro (car_grid_spline_*)
};

// Índice de uma vaga em todas as grades da rota, na ordem do arquivo.
struct SlotRef {
    int grid = -1, slot = -1;
    explicit operator bool() const { return grid >= 0; }
};

struct Route {
    std::string name;
    std::string terrain_file;
    std::size_t terrain_meshes = 0, terrain_verts = 0;
    std::size_t instances = 0;
    std::vector<Gate> gates;
    std::vector<AiLine> ai;
    std::vector<std::string> ens_ids;  // id de texto de cada instância e: por idnum (só rótulo)
    Replay replay;
    std::vector<Grid> grids;
    // "grade/vaga" ou só o nome da vaga (a primeira que achar)
    SlotRef find_slot(const std::string& name) const {
        for (std::size_t g = 0; g < grids.size(); ++g)
            for (std::size_t k = 0; k < grids[g].slots.size(); ++k)
                if (grids[g].slots[k].name == name || grids[g].name + "/" + grids[g].slots[k].name == name)
                    return {static_cast<int>(g), static_cast<int>(k)};
        return {};
    }
    // Índice corrido de uma vaga (grades na ordem do arquivo) e o inverso; -1 / {} fora da faixa.
    int slot_index(SlotRef r) const {
        if (r.grid < 0 || r.grid >= static_cast<int>(grids.size())) return -1;
        if (r.slot < 0 || r.slot >= static_cast<int>(grids[static_cast<std::size_t>(r.grid)].slots.size())) return -1;
        int flat = r.slot;
        for (int g = 0; g < r.grid; ++g) flat += static_cast<int>(grids[static_cast<std::size_t>(g)].slots.size());
        return flat;
    }
    SlotRef slot_at(int flat) const {
        if (flat < 0) return {};
        for (std::size_t g = 0; g < grids.size(); ++g) {
            const int n = static_cast<int>(grids[g].slots.size());
            if (flat < n) return {static_cast<int>(g), flat};
            flat -= n;
        }
        return {};
    }
    int slot_count() const {
        int n = 0;
        for (const Grid& g : grids) n += static_cast<int>(g.slots.size());
        return n;
    }
    const GridSlot* slot(SlotRef r) const {
        return slot_index(r) >= 0 ? &grids[static_cast<std::size_t>(r.grid)].slots[static_cast<std::size_t>(r.slot)] : nullptr;
    }
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
