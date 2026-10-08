#include "core/track.hpp"

#include "core/io.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace dr2 {

namespace {

Vec3 vec3(const json::Value& v) {
    if (v.size() < 3) throw std::runtime_error("track.json: ponto com menos de 3 coordenadas");
    return {static_cast<float>(v[0].as_number()), static_cast<float>(v[1].as_number()), static_cast<float>(v[2].as_number())};
}

std::string str(const json::Value& v, std::string_view key) {
    const json::Value* x = v.find(key);
    return x && x->is_string() ? x->as_string() : std::string();
}

std::vector<Vec3> points(const json::Value& v, std::string_view key) {
    std::vector<Vec3> out;
    if (const json::Value* a = v.find(key))
        for (const json::Value& p : a->as_array()) out.push_back(vec3(p));
    return out;
}

Replay read_replay(const json::Value& rep) {
    Replay out;
    if (const json::Value* cams = rep.find("cameras")) {
        for (const json::Value& c : cams->as_array()) {
            ReplayCamera cam;
            cam.name = str(c, "name");
            cam.kind = str(c, "kind");
            cam.role = str(c, "role");
            cam.s = c.number_or("s", -1.0);
            cam.pos = vec3(c["pos"]);
            cam.aim = c.find("aim") ? vec3(c["aim"]) : cam.pos;
            cam.path = points(c, "path");
            cam.target = points(c, "target");
            cam.duration = c.number_or("duration", 0.0);
            out.cameras.push_back(std::move(cam));
        }
    }
    if (const json::Value* zones = rep.find("zones")) {
        for (const json::Value& z : zones->as_array()) {
            ReplayZone zone;
            zone.name = str(z, "name");
            zone.s = z.number_or("s", 0.0);
            zone.l = vec3(z["l"]);
            zone.r = vec3(z["r"]);
            zone.lap = static_cast<int>(z.number_or("lap", 0.0));
            if (const json::Value* sw = z.find("switch"))
                for (const json::Value& w : sw->as_array()) zone.sw.push_back({str(w, "camera"), w.number_or("p", 0.0)});
            out.zones.push_back(std::move(zone));
        }
    }
    if (const json::Value* bounds = rep.find("bounds")) {
        for (const json::Value& b : bounds->as_array()) {
            ReplayBound bound;
            bound.name = str(b, "name");
            const json::Value& cs = b["corners"];
            if (cs.size() != 4) throw std::runtime_error("track.json: prisma do replay sem 4 cantos");
            for (std::size_t k = 0; k < 4; ++k)
                bound.corners[k] = {static_cast<float>(cs[k][0].as_number()), static_cast<float>(cs[k][1].as_number())};
            bound.y0 = static_cast<float>(b.number_or("y0", 0.0));
            bound.y1 = static_cast<float>(b.number_or("y1", 0.0));
            out.bounds.push_back(std::move(bound));
        }
    }
    return out;
}

GridSlot read_slot(const json::Value& v) {
    GridSlot slot;
    slot.name = str(v, "name");
    slot.pos = vec3(v["pos"]);
    if (v.find("fwd")) slot.fwd = vec3(v["fwd"]);
    slot.s = v.number_or("s", -1.0);
    slot.lat = v.number_or("lat", 0.0);
    if (const json::Value* sz = v.find("size"); sz && sz->size() >= 2) {
        slot.width = static_cast<float>((*sz)[0].as_number());
        slot.length = static_cast<float>((*sz)[1].as_number());
    }
    return slot;
}

std::vector<Grid> read_grids(const json::Value& grids) {
    std::vector<Grid> out;
    for (const json::Value& g : grids.as_array()) {
        Grid grid;
        grid.name = str(g, "name");
        grid.role = str(g, "role");
        grid.pos = vec3(g["pos"]);
        if (g.find("fwd")) grid.fwd = vec3(g["fwd"]);
        if (const json::Value* sl = g.find("slots"))
            for (const json::Value& v : sl->as_array()) grid.slots.push_back(read_slot(v));
        if (const json::Value* mk = g.find("markers"))
            for (const json::Value& v : mk->as_array()) grid.markers.push_back(read_slot(v));
        out.push_back(std::move(grid));
    }
    return out;
}

std::size_t count(const json::Value& v, std::string_view key) {
    const double d = v.number_or(key, 0.0);
    return d > 0 ? static_cast<std::size_t>(d) : 0;
}

}  // namespace

Track read_track(const std::string& dir) {
    Track t;
    t.dir = dir;
    t.raw = json::parse_file(join_path(dir, "track.json"));
    const json::Value& doc = t.raw;
    t.id = doc["id"].as_string();
    if (const json::Value* src = doc.find("src"); src && src->is_string()) t.src = src->as_string();

    for (const json::Value& r : doc["routes"].as_array()) {
        Route route;
        route.name = r["name"].as_string();
        if (const json::Value* ter = r.find("terrain")) {
            route.terrain_file = (*ter)["file"].as_string();
            route.terrain_meshes = count(*ter, "meshes");
            route.terrain_verts = count(*ter, "verts");
        }
        route.instances = count(r, "instances");
        if (const json::Value* prog = r.find("progress"); prog && prog->find("gates")) {
            for (const json::Value& g : (*prog)["gates"].as_array())
                route.gates.push_back({g.number_or("d", 0.0), vec3(g["l"]), vec3(g["r"])});
        }
        if (const json::Value* ai = r.find("ai")) {
            for (const json::Value& line : ai->as_array()) {
                AiLine a;
                if (const json::Value* n = line.find("name"); n && n->is_string()) a.name = n->as_string();
                for (const json::Value& p : line["pts"].as_array()) a.pts.push_back(vec3(p));
                route.ai.push_back(std::move(a));
            }
        }
        if (const json::Value* ids = r.find("ens_ids")) {
            for (const json::Value& s : ids->as_array()) route.ens_ids.push_back(s.is_string() ? s.as_string() : std::string());
        }
        if (const json::Value* rep = r.find("replay")) route.replay = read_replay(*rep);
        if (const json::Value* grids = r.find("grids")) route.grids = read_grids(*grids);
        t.routes.push_back(std::move(route));
    }
    if (t.routes.empty()) throw std::runtime_error("track.json: nenhuma rota");

    if (const json::Value* order = doc.find("type_order")) {
        const json::Value& types = doc["types"];
        for (const json::Value& name : order->as_array()) {
            const json::Value& info = types[name.as_string()];
            t.types.push_back({name.as_string(), count(info, "first"), count(info, "count")});
        }
    }
    if (const json::Value* mats = doc.find("materials")) {
        for (const auto& [key, file] : mats->as_object())
            if (file.is_string()) t.materials.emplace(key, file.as_string());
    }
    return t;
}

bool route_bounds(const Track& track, Vec3& lo, Vec3& hi) {
    constexpr float inf = std::numeric_limits<float>::infinity();
    lo = {inf, inf, inf};
    hi = {-inf, -inf, -inf};
    bool any = false;
    auto add = [&](const Vec3& p) {
        any = true;
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], p[k]);
            hi[k] = std::max(hi[k], p[k]);
        }
    };
    for (const Route& r : track.routes) {
        for (const AiLine& a : r.ai)
            for (const Vec3& p : a.pts) add(p);
        for (const Gate& g : r.gates) {
            add(g.l);
            add(g.r);
        }
    }
    return any;
}

}  // namespace dr2
