#include "app/track_view.hpp"

#include "core/dr2m.hpp"
#include "core/io.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace dr2::app {

namespace {

double seconds_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

std::string thousands(std::size_t n) {
    std::string s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), " ");
    return s;
}

}  // namespace

TrackView::TrackView(const std::string& dir) : track_(read_track(dir)), textures_(dir, track_.materials) {
    route_ = &track_.routes.front();
    if (route_->terrain_file.empty()) throw std::runtime_error("track.json: a rota " + route_->name + " não tem terreno");

    auto t0 = std::chrono::steady_clock::now();
    const std::vector<std::uint8_t> bytes = read_file(join_path(dir, route_->terrain_file));
    const std::vector<Mesh> meshes = read_dr2m(bytes);
    const double read_s = seconds_since(t0);
    const MeshTotals tot = totals(meshes);
    if (tot.verts > 5'000'000)
        std::fprintf(stderr, "viewer3d: aviso: %s tem %zu vértices; o MVP não otimiza pistas desse tamanho\n",
                     route_->terrain_file.c_str(), tot.verts);

    t0 = std::chrono::steady_clock::now();
    terrain_ = std::make_unique<render::Terrain>(meshes);
    glFinish();
    const double upload_s = seconds_since(t0);
    lines_ = std::make_unique<render::RouteLines>(*route_);

    t0 = std::chrono::steady_clock::now();
    const std::vector<Mesh> lib = read_dr2m(read_file(join_path(dir, "objects.bin")));
    inst_ = read_dr2i(read_file(join_path(dir, "inst_" + route_->name + ".bin")), track_.types.size());
    objects_ = std::make_unique<render::InstanceRenderer>(track_, lib, inst_);
    glFinish();
    std::size_t with_mesh = 0;
    for (const auto& ty : objects_->types()) with_mesh += !ty.empty;
    std::fprintf(stderr, "viewer3d: objects.bin %zu malhas, %zu tipos (%zu com malha), %u instâncias; %.3f s\n", lib.size(),
                 objects_->types().size(), with_mesh, inst_.n, seconds_since(t0));

    // texturas do terreno já na carga, para medir (as dos objetos vêm sob demanda)
    t0 = std::chrono::steady_clock::now();
    for (const auto& part : terrain_->parts()) textures_.for_material(part.material);
    glFinish();
    const double tex_s = seconds_since(t0);

    std::fprintf(stderr,
                 "viewer3d: %s %s: %zu malhas, %zu vértices, %zu triângulos; leitura %.3f s (%.1f MB), envio à GPU %.3f s\n",
                 track_.id.c_str(), route_->terrain_file.c_str(), tot.meshes, tot.verts, tot.tris, read_s,
                 static_cast<double>(bytes.size()) / 1e6, upload_s);
    std::fprintf(stderr, "viewer3d: texturas do terreno: %zu criadas (%zu falharam), %.1f MB RGBA, %.3f s (decodificação %.3f s)\n",
                 textures_.created(), textures_.failed(), static_cast<double>(textures_.bytes_rgba()) / 1e6, tex_s,
                 textures_.decode_seconds());
}

void TrackView::frame_route(render::OrbitCamera& cam) const {
    Vec3 lo, hi;
    if (!route_bounds(track_, lo, hi)) return;
    cam.target = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
    cam.dist = std::max(60.0f, std::hypot(hi[0] - lo[0], hi[2] - lo[2]) * 0.9f);
    cam.yaw = 0.8f;
    cam.pitch = 0.7f;
}

bool TrackView::key(SDL_Keycode key, render::OrbitCamera& cam) {
    switch (key) {
    case SDLK_F: frame_route(cam); return true;
    case SDLK_F1: show_terrain_ = !show_terrain_; return true;
    case SDLK_F2: layers_.obj = !layers_.obj; return true;
    case SDLK_F3: layers_.tree = !layers_.tree; return true;
    case SDLK_F4: layers_.dist = !layers_.dist; return true;
    case SDLK_LEFTBRACKET: draw_dist_ = std::max(100.0f, draw_dist_ - 100.0f); return true;
    case SDLK_RIGHTBRACKET: draw_dist_ = std::min(4000.0f, draw_dist_ + 100.0f); return true;
    case SDLK_G: show_gates_ = !show_gates_; return true;
    case SDLK_I: show_ai_ = !show_ai_; return true;
    default: return false;
    }
}

void TrackView::draw(const glm::mat4& view_proj, const render::OrbitCamera& cam) {
    objects_->cull(inst_, cam.target, cam.dist, draw_dist_, layers_, edit_rev_);
    shader_.use();
    shader_.set_view_proj(view_proj);
    if (show_terrain_) terrain_->draw(shader_, &textures_);
    objects_->draw(shader_, textures_);
    lines_->draw(shader_, show_gates_, show_ai_);
}

std::string TrackView::title() const {
    return route_->name + " | " + thousands(terrain_->meshes()) + " malhas | " + thousands(terrain_->vertices()) +
           " vértices | " + thousands(terrain_->triangles()) + " tri | inst " + thousands(objects_->visible()) + "/" +
           thousands(inst_.n) + " | " + std::to_string(static_cast<int>(draw_dist_)) + " m";
}

}  // namespace dr2::app
