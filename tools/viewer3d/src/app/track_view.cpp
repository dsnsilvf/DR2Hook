#include "app/track_view.hpp"

#include "core/dr2m.hpp"
#include "core/io.hpp"
#include "edit/edits_json.hpp"
#include "render/pick.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>

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

TrackView::TrackView(const std::string& dir, std::string out)
    : track_(read_track(dir)), textures_(dir, track_.materials), out_(std::move(out)) {
    route_ = &track_.routes.front();
    if (out_.empty()) out_ = "build/uiview/saves/" + track_.id + ".edits.json";
    if (edit::inside_game_folder(out_)) throw std::runtime_error("--out " + out_ + " fica dentro da pasta do jogo");
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

bool TrackView::key(SDL_Keycode key, SDL_Keymod mod, render::OrbitCamera& cam) {
    if (mod & SDL_KMOD_CTRL) {
        switch (key) {
        case SDLK_Z:
            if (drag_.active) return true;
            if ((mod & SDL_KMOD_SHIFT) ? hist_.redo(inst_) : hist_.undo(inst_)) ++edit_rev_;
            return true;
        case SDLK_Y:
            if (!drag_.active && hist_.redo(inst_)) ++edit_rev_;
            return true;
        case SDLK_S: save(); return true;
        default: return false;
        }
    }
    switch (key) {
    case SDLK_F:
        if (sel_ >= 0) frame_selected(cam);
        else frame_route(cam);
        return true;
    case SDLK_1: tool_ = Tool::Navigate; return true;
    case SDLK_2: tool_ = Tool::Move; return true;
    case SDLK_3: tool_ = Tool::Rotate; return true;
    case SDLK_DELETE: delete_selected(); return true;
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
    if (sel_ >= 0 && !inst_.hidden[static_cast<std::size_t>(sel_)])
        objects_->draw_one(shader_, textures_, inst_, static_cast<std::size_t>(sel_), 1.0f);
    lines_->draw(shader_, show_gates_, show_ai_);
}

void TrackView::frame_selected(render::OrbitCamera& cam) const {
    const auto i = static_cast<std::size_t>(sel_);
    const float* m = inst_.matrix(i);
    cam.target = {m[9], m[10], m[11]};
    const auto& ty = objects_->types()[inst_.type[i]];
    const float size = ty.empty ? 4.0f : glm::length(ty.hi - ty.lo);
    cam.dist = std::max(6.0f, size * 3.0f);
}

void TrackView::commit(const char* label, std::vector<edit::Snap> before) {
    std::vector<std::uint32_t> idx;
    for (const auto& s : before) idx.push_back(s.index);
    hist_.commit(label, std::move(before), edit::snapshot(inst_, idx));
    ++edit_rev_;
}

void TrackView::delete_selected() {
    if (sel_ < 0 || drag_.active) return;
    const auto i = static_cast<std::uint32_t>(sel_);
    auto before = edit::snapshot(inst_, {i});
    inst_.hidden[i] = 1;
    commit("Apagar", std::move(before));
    sel_ = -1;
}

bool TrackView::begin_edit(float x, float y, float w, float h, const render::OrbitCamera& cam) {
    if (tool_ == Tool::Navigate) return false;
    const auto ray = render::mouse_ray(cam, x, y, w, h);
    const int hit = render::pick(ray, inst_, *objects_, cam.target, draw_dist_, layers_);
    if (hit < 0) return false;
    sel_ = hit;
    drag_ = EditDrag{};
    drag_.active = true;
    drag_.i = static_cast<std::uint32_t>(hit);
    drag_.before = edit::snapshot(inst_, {drag_.i});
    std::copy(inst_.matrix(drag_.i), inst_.matrix(drag_.i) + kInstFloats, drag_.base);
    drag_.has_start = render::ground(ray, drag_.base[10], drag_.start);
    drag_.sy = y;
    drag_.ex = x;
    return true;
}

void TrackView::edit_drag(float x, float y, float w, float h, const render::OrbitCamera& cam, bool shift) {
    if (!drag_.active) return;
    float* m = inst_.matrix(drag_.i);
    if (tool_ == Tool::Move) {
        if (shift) {
            m[10] = drag_.base[10] - (y - drag_.sy) * std::max(0.02f, cam.dist * 0.002f);
        } else if (drag_.has_start) {
            glm::vec3 p;
            if (render::ground(render::mouse_ray(cam, x, y, w, h), drag_.base[10], p)) {
                m[9] = drag_.base[9] + p.x - drag_.start.x;
                m[11] = drag_.base[11] + p.z - drag_.start.z;
                m[10] = drag_.base[10];
            }
        }
    } else {
        edit::spin(m, drag_.base, (x - drag_.ex) * 0.01f);
    }
    ++edit_rev_;
}

void TrackView::end_edit() {
    if (!drag_.active) return;
    drag_.active = false;
    commit(tool_ == Tool::Move ? "Mover" : "Girar", std::move(drag_.before));
}

void TrackView::click(float x, float y, float w, float h, const render::OrbitCamera& cam) {
    sel_ = render::pick(render::mouse_ray(cam, x, y, w, h), inst_, *objects_, cam.target, draw_dist_, layers_);
}

std::size_t TrackView::save() {
    std::size_t count = 0;
    const std::string text = edit::edits_json(track_, *route_, inst_, &count);
    edit::write_text(out_, text);
    std::printf("gravado %s (%zu edições)\n", out_.c_str(), count);
    std::fflush(stdout);
    return count;
}

std::string TrackView::title() const {
    static const char* const tools[] = {"navegar", "mover", "girar"};
    std::string sel;
    if (sel_ >= 0) {
        const auto i = static_cast<std::size_t>(sel_);
        const std::string& name = track_.types[inst_.type[i]].name;
        const float* m = inst_.matrix(i);
        char buf[200];
        std::snprintf(buf, sizeof buf, " | %s | kind %c | %u | %.2f %.2f %.2f", name.substr(2).c_str(), name[0], inst_.idnum[i],
                      static_cast<double>(m[9]), static_cast<double>(m[10]), static_cast<double>(m[11]));
        sel = buf;
    }
    return std::string(tools[static_cast<int>(tool_)]) + " | " + route_->name + " | " + thousands(terrain_->meshes()) + " malhas | " + thousands(terrain_->vertices()) +
           " vértices | " + thousands(terrain_->triangles()) + " tri | inst " + thousands(objects_->visible()) + "/" +
           thousands(inst_.n) + " | " + std::to_string(static_cast<int>(draw_dist_)) + " m | hist " +
           std::to_string(hist_.pos()) + "/" + std::to_string(hist_.size()) + sel;
}

}  // namespace dr2::app
