#include "app/track_view.hpp"

#include "core/dr2m.hpp"
#include "core/io.hpp"
#include "core/json.hpp"
#include "edit/edits_json.hpp"
#include "edit/ground_fit.hpp"
#include "render/pick.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>

#ifdef __GLIBC__
#include <malloc.h>
#endif
#ifdef _WIN32
#include <io.h>
#define access _access
#define W_OK 2
#else
#include <unistd.h>
#endif

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

std::string TrackView::default_out(const std::string& dir, const std::string& id) {
    // pista exportada em <raiz>/tracks/<id>: grava em <raiz>/saves, qualquer que seja a pasta atual
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path abs = fs::absolute(fs::path(dir), ec).lexically_normal();
    if (!abs.has_filename()) abs = abs.parent_path();
    if (!ec && abs.parent_path().filename() == "tracks")
        return (abs.parent_path().parent_path() / "saves" / (id + ".edits.json")).generic_string();
    return "build/uiview/saves/" + id + ".edits.json";
}

TrackView::TrackView(const std::string& dir, std::string out, bool resume, render::TextureCache::Options tex)
    : track_(read_track(dir)), dir_(dir), textures_(dir, track_.materials, tex), out_(std::move(out)) {
    if (out_.empty()) out_ = default_out(dir, track_.id);
    if (edit::inside_game_folder(out_)) throw std::runtime_error("--out " + out_ + " fica dentro da pasta do jogo");
    if (std::filesystem::is_directory(out_)) throw std::runtime_error("--out " + out_ + " é uma pasta; passe o caminho do arquivo .json");

    const auto t0 = std::chrono::steady_clock::now();
    std::size_t library_meshes = 0;
    {
        // as malhas dos objetos só servem para o envio à GPU (a caixa de cada tipo fica no InstanceRenderer)
        const std::vector<Mesh> library = read_dr2m(read_file(join_path(dir, "objects.bin")));
        library_meshes = library.size();
        objects_ = std::make_unique<render::InstanceRenderer>(track_, library, Instances{});
    }
    glFinish();
    std::size_t with_mesh = 0;
    for (const auto& ty : objects_->types()) with_mesh += !ty.empty;
    std::fprintf(stderr, "viewer3d: objects.bin %zu malhas, %zu tipos (%zu com malha); %.3f s\n", library_meshes,
                 objects_->types().size(), with_mesh, seconds_since(t0));
    load_route(0);
    saved_text_ = current_edits();  // nada editado ainda
    probe_ = std::make_unique<render::TerrainProbe>();
    if (!can_write(out_)) set_status("não vai dar para gravar em " + out_ + " (pasta sem permissão ou caminho impossível); use --out");
    if (std::filesystem::exists(out_)) {
        if (resume) resume_edits();
        else set_status(out_ + " já existe e não foi lido (--fresh); o primeiro Ctrl+S guarda uma cópia dele em .<n>.bak");
    }
    {
        // uma sessão anterior que caiu deixou um autosave mais novo que o arquivo gravado? (depois do
        // resume, para a mensagem dele não cobrir esta)
        std::error_code ec;
        const auto as = std::filesystem::last_write_time(autosave_path(), ec);
        if (!ec) {
            std::error_code ec2;
            const auto saved = std::filesystem::last_write_time(out_, ec2);
            if (ec2 || as > saved)
                set_status("ATENÇÃO: uma sessão anterior não gravou edições; Arquivo > Recuperar autosave (" + autosave_path() + ")");
        }
    }
}

bool TrackView::can_write(const std::string& path) {
    // o primeiro ancestral que existe precisa ser uma pasta com escrita (o resto o Ctrl+S cria)
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p = fs::absolute(path, ec).parent_path();
    while (!p.empty() && !fs::exists(p, ec)) {
        if (p == p.parent_path()) return false;
        p = p.parent_path();
    }
    return fs::is_directory(p, ec) && ::access(p.c_str(), W_OK) == 0;
}

void TrackView::apply_doc(const json::Value& doc, edit::ApplyReport& rep) {
    // o doc descreve a sessão inteira: cada rota volta ao arquivo exportado e recebe as edições dele.
    // Aplica em cópias e só troca no fim: um erro no meio não deixa metade aplicada.
    std::map<std::size_t, Instances> next;
    for (const std::string& name : edit::routes_in_edits(doc)) {
        std::size_t k = 0;
        while (k < track_.routes.size() && track_.routes[k].name != name) ++k;
        if (k == track_.routes.size()) {
            rep.skipped.push_back(name + ": rota não existe nesta pista");
            continue;
        }
        Instances copy = read_dr2i(read_file(join_path(dir_, "inst_" + name + ".bin")), track_.types.size());
        edit::apply_edits(doc, track_, track_.routes[k], copy, rep);
        next[k] = std::move(copy);
    }
    // rota atual fora do doc volta ao arquivo; as outras abertas fora do doc também
    if (!next.count(route_index_))
        next[route_index_] = read_dr2i(read_file(join_path(dir_, "inst_" + route_->name + ".bin")), track_.types.size());
    saved_.clear();
    for (auto& [k, inst] : next) {
        if (k == route_index_) inst_ = std::move(inst);
        else saved_[k] = Saved{std::move(inst), edit::History{}};
    }
    hist_ = edit::History{};
    sel_ = -1;
    drag_ = EditDrag{};
    objects_->regroup(inst_);
    ++edit_rev_;
}

bool TrackView::recover(const std::string& edits_text, const std::string& from) {
    // volta a um estado de edições guardado (autosave, ou a sessão de antes de abrir outra pista);
    // fica "não gravado" em relação ao arquivo
    try {
        edit::ApplyReport rep;
        apply_doc(json::parse(edits_text), rep);
        for (const std::string& w : rep.skipped) std::fprintf(stderr, "viewer3d: %s: ficou de fora: %s\n", from.c_str(), w.c_str());
        set_status("recuperadas " + std::to_string(rep.applied) + " edições de " + from + " (sem histórico; Ctrl+S grava em " + out_ + ")" +
                   (rep.skipped.empty() ? "" : "; " + std::to_string(rep.skipped.size()) + " ficaram de fora"));
        return true;
    } catch (const std::exception& e) {
        set_status(std::string("não recuperou ") + from + ": " + e.what());
        return false;
    }
}

bool TrackView::recover_autosave() {
    try {
        const std::vector<std::uint8_t> bytes = read_file(autosave_path());
        return recover(std::string(bytes.begin(), bytes.end()), autosave_path());
    } catch (const std::exception& e) {
        set_status(std::string("não leu o autosave: ") + e.what());
        return false;
    }
}

bool TrackView::has_autosave() const {
    std::error_code ec;
    return std::filesystem::is_regular_file(autosave_path(), ec);
}

TrackView::~TrackView() {
    // saída normal: o autosave desta sessão não serve mais (num crash ele fica)
    if (wrote_autosave_) {
        std::error_code ec;
        std::filesystem::remove(autosave_path(), ec);
    }
}

void TrackView::resume_edits() {
    // continua a sessão anterior: as edições gravadas voltam para as rotas, sem histórico
    try {
        const json::Value doc = json::parse_file(out_);
        edit::ApplyReport rep;
        apply_doc(doc, rep);
        saved_text_ = current_edits();  // o que está no arquivo não é "não gravado"
        for (const std::string& w : rep.skipped) std::fprintf(stderr, "viewer3d: edits.json: ficou de fora: %s\n", w.c_str());
        set_status("retomadas " + std::to_string(rep.applied) + " edições de " + out_ +
                   (rep.skipped.empty() ? "" : "; " + std::to_string(rep.skipped.size()) + " ficaram de fora (veja o terminal)"));
    } catch (const std::exception& e) {
        set_status("não leu " + out_ + ": " + e.what() + "; começando sem edições (o primeiro Ctrl+S guarda o arquivo em .<n>.bak)");
    }
}

void TrackView::load_route(std::size_t index) {
    // lê tudo antes de mudar o estado: se um arquivo falhar, a rota aberta continua como estava
    const Route& route = track_.routes.at(index);
    if (route.terrain_file.empty()) throw std::runtime_error("track.json: a rota " + route.name + " não tem terreno");
    Instances inst;
    const bool reopen = saved_.count(index) != 0;
    if (!reopen) inst = read_dr2i(read_file(join_path(dir_, "inst_" + route.name + ".bin")), track_.types.size());

    // rotas com a mesma seleção de terreno dividem o arquivo: só relê se mudou
    std::unique_ptr<render::Terrain> next_terrain;
    struct Loaded {
        double read_s = 0, upload_s = 0;
        std::size_t bytes = 0;
        MeshTotals tot{};
    } loaded;
    if (route.terrain_file != terrain_file_) {
        auto t0 = std::chrono::steady_clock::now();
        std::size_t file_bytes = 0;
        // o arquivo lido sai da memória assim que vira malhas (não fica junto no pico da carga)
        const std::vector<Mesh> meshes = [&] {
            const std::vector<std::uint8_t> bytes = read_file(join_path(dir_, route.terrain_file));
            file_bytes = bytes.size();
            return read_dr2m(bytes);
        }();
        const double read_s = seconds_since(t0);
        const MeshTotals tot = totals(meshes);
        if (tot.verts > 5'000'000)
            std::fprintf(stderr, "viewer3d: aviso: %s tem %zu vértices; o MVP não otimiza pistas desse tamanho\n",
                         route.terrain_file.c_str(), tot.verts);
        t0 = std::chrono::steady_clock::now();
        auto terrain = std::make_unique<render::Terrain>(meshes);
        glFinish();
        const double upload_s = seconds_since(t0);
        next_terrain = std::move(terrain);
        loaded = Loaded{read_s, upload_s, file_bytes, tot};
    }
    // tudo que pode falhar já foi feito: daqui para baixo só troca o estado
    auto lines = std::make_unique<render::RouteLines>(route);
    if (next_terrain) {
        terrain_ = std::move(next_terrain);
#ifdef __GLIBC__
        malloc_trim(0);  // os vetores da leitura (centenas de MB numa pista grande) voltam ao sistema
#endif
        terrain_file_ = route.terrain_file;
        const MeshTotals& tot = loaded.tot;
        // as texturas (terreno e objetos) vêm em segundo plano, na ordem em que o desenho pede
        std::fprintf(stderr,
                     "viewer3d: %s %s: %zu malhas, %zu vértices, %zu triângulos; leitura %.3f s (%.1f MB), envio à GPU %.3f s\n",
                     track_.id.c_str(), route.terrain_file.c_str(), tot.meshes, tot.verts, tot.tris, loaded.read_s,
                     static_cast<double>(loaded.bytes) / 1e6, loaded.upload_s);
    }
    route_index_ = index;
    route_ = &route;
    lines_ = std::move(lines);

    if (auto it = saved_.find(index); it != saved_.end()) {
        inst_ = std::move(it->second.inst);
        hist_ = std::move(it->second.hist);
        saved_.erase(it);
    } else {
        inst_ = std::move(inst);
        hist_ = edit::History{};
        if (inst_.n != route_->instances)
            std::fprintf(stderr, "viewer3d: aviso: inst_%s.bin tem %u instâncias e o track.json diz %zu\n", route_->name.c_str(),
                         inst_.n, route_->instances);
    }
    objects_->regroup(inst_);
    sel_ = -1;
    drag_ = EditDrag{};
    ++edit_rev_;
    std::fprintf(stderr, "viewer3d: rota %s (%zu de %zu), %u instâncias\n", route_->name.c_str(), index + 1, track_.routes.size(),
                 inst_.n);
}

void TrackView::switch_route(int step) {
    const std::size_t n = track_.routes.size();
    if (n < 2) return;
    open_route((route_index_ + n + static_cast<std::size_t>(step % static_cast<int>(n) + static_cast<int>(n))) % n);
}

void TrackView::open_route(std::size_t next) {
    if (drag_.active || next == route_index_ || next >= track_.routes.size()) return;
    saved_[route_index_] = Saved{std::move(inst_), std::move(hist_)};
    try {
        load_route(next);
    } catch (const std::exception& e) {
        // a rota atual volta intacta, com edições e histórico
        if (auto it = saved_.find(route_index_); it != saved_.end()) {
            inst_ = std::move(it->second.inst);
            hist_ = std::move(it->second.hist);
            saved_.erase(it);
        }
        set_status("não abriu " + track_.routes[next].name + ": " + e.what());
    }
}

void TrackView::frame_route(render::OrbitCamera& cam) const {
    Vec3 lo, hi;
    if (!route_bounds(track_, lo, hi)) {
        // rota sem portões nem linha da IA: enquadra o terreno
        glm::vec3 tl, th;
        if (!terrain_ || !terrain_->bounds(tl, th)) return;
        lo = {tl.x, tl.y, tl.z};
        hi = {th.x, th.y, th.z};
    }
    cam.target = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
    cam.dist = std::max(60.0f, std::hypot(hi[0] - lo[0], hi[2] - lo[2]) * 0.9f);
    cam.yaw = 0.8f;
    cam.pitch = 0.7f;
}

bool TrackView::key(SDL_Keycode key, SDL_Keymod mod, render::OrbitCamera& cam) {
    if (mod & SDL_KMOD_CTRL) {
        switch (key) {
        case SDLK_Z:
            if (mod & SDL_KMOD_SHIFT) redo();
            else undo();
            return true;
        case SDLK_Y: redo(); return true;
        case SDLK_S: save(); return true;
        case SDLK_D: duplicate_selected(); return true;
        default: return false;
        }
    }
    switch (key) {
    case SDLK_F:
        if (sel_ >= 0) frame_selected(cam);
        else frame_route(cam);
        return true;
    case SDLK_1: set_tool(Tool::Navigate); return true;
    case SDLK_2: set_tool(Tool::Move); return true;
    case SDLK_3: set_tool(Tool::Rotate); return true;
    case SDLK_DELETE: delete_selected(); return true;
    case SDLK_TAB: switch_route((mod & SDL_KMOD_SHIFT) ? -1 : 1); return true;
    case SDLK_R: restore_selected(); return true;
    case SDLK_T:
        if (mod & SDL_KMOD_SHIFT) align_selected();
        else settle_selected();
        return true;
    case SDLK_Q:
    case SDLK_E: turn_selected(((mod & SDL_KMOD_SHIFT) ? 90.0f : 15.0f) * (key == SDLK_E ? 1.0f : -1.0f)); return true;
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
    textures_.begin_frame();  // envia o que as threads decodificaram, dentro do orçamento do quadro
    objects_->cull(inst_, cam.target, cam.dist, draw_dist_, layers_, edit_rev_);
    shader_.use();
    shader_.set_view_proj(view_proj);
    if (show_terrain_) terrain_->draw(shader_, &textures_, view_proj, cam.target, terrain_dist_);
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

bool TrackView::undo() {
    if (drag_.active || !hist_.undo(inst_)) return false;
    after_history();
    return true;
}

bool TrackView::redo() {
    if (drag_.active || !hist_.redo(inst_)) return false;
    after_history();
    return true;
}

void TrackView::history_go(std::size_t pos) {
    while (hist_.pos() > pos && undo()) {}
    while (hist_.pos() < pos && redo()) {}
}

void TrackView::close_change() {
    // um campo do Inspector ainda aberto fecha o passo dele antes de a seleção mudar
    if (drag_.active && drag_.numeric) end_change("Editar (campo)");
}

void TrackView::select(int i) {
    close_change();
    if (drag_.active) return;
    sel_ = i >= 0 && static_cast<std::uint32_t>(i) < inst_.n ? i : -1;
}

void TrackView::duplicate_selected() {
    // duplicar só vale para objetos de objects.ens (ornamentos e árvores têm contagem fixa)
    if (sel_ < 0 || drag_.active || inst_.hidden[static_cast<std::size_t>(sel_)]) return;
    if (!track_.types[inst_.type[static_cast<std::size_t>(sel_)]].name.starts_with("e:")) {
        set_status("duplicar só vale para objetos e: (objects.ens); ornamentos e árvores têm contagem fixa");
        return;
    }
    sel_ = static_cast<int>(edit::duplicate(inst_, hist_, static_cast<std::uint32_t>(sel_)));
    objects_->regroup(inst_);
    ++edit_rev_;
}

void TrackView::restore_selected() {
    if (sel_ < 0 || drag_.active) return;
    edit::restore(inst_, hist_, static_cast<std::uint32_t>(sel_));
    ++edit_rev_;
}

void TrackView::turn_selected(float deg) {
    if (sel_ < 0 || drag_.active || inst_.hidden[static_cast<std::size_t>(sel_)]) return;
    edit::turn(inst_, hist_, static_cast<std::uint32_t>(sel_), deg);
    ++edit_rev_;
}

bool TrackView::begin_change(std::uint32_t i) {
    if (drag_.active || i >= inst_.n || inst_.hidden[i]) return false;
    drag_ = EditDrag{};
    drag_.active = true;
    drag_.numeric = true;
    drag_.i = i;
    drag_.before = edit::snapshot(inst_, {i});
    return true;
}

void TrackView::set_matrix(std::uint32_t i, const float* m) {
    if (!drag_.active || !drag_.numeric || i != drag_.i) return;  // só na instância do begin_change
    for (std::size_t k = 0; k < kInstFloats; ++k)
        if (!std::isfinite(m[k]) || std::fabs(m[k]) > 1e6f) return;  // nada de inf/1e39 no edits.json
    std::copy(m, m + kInstFloats, inst_.matrix(drag_.i));
    objects_->touch(inst_, drag_.i);  // só ela: o resto do corte e dos buffers continua valendo
}

void TrackView::end_change(const char* label) {
    if (!drag_.active) return;
    drag_.active = false;
    edit::snap_to_file(inst_, drag_.i);
    commit(label, std::move(drag_.before));
}

const char* TrackView::source_file(const std::string& type_name) {
    switch (type_name.empty() ? '?' : type_name[0]) {
    case 'e': return "objects.ens";
    case 'o': return "ornaments.bin";
    case 't': return "trees.bin";
    default: return "?";
    }
}

void TrackView::delete_selected() {
    if (sel_ < 0 || drag_.active || inst_.hidden[static_cast<std::size_t>(sel_)]) return;
    const auto i = static_cast<std::uint32_t>(sel_);
    auto before = edit::snapshot(inst_, {i});
    inst_.hidden[i] = 1;
    commit("Apagar", std::move(before));
    sel_ = -1;
}

bool TrackView::begin_edit(float x, float y, float w, float h, const render::OrbitCamera& cam, bool shift) {
    close_change();
    if (tool_ == Tool::Navigate || drag_.active) return false;
    const auto ray = render::mouse_ray(cam, x, y, w, h);
    const int hit = render::pick(ray, inst_, *objects_, cam.target, draw_dist_, layers_, visible_terrain_t(ray));
    if (hit < 0) return false;
    sel_ = hit;
    drag_ = EditDrag{};
    drag_.active = true;
    drag_.i = static_cast<std::uint32_t>(hit);
    drag_.before = edit::snapshot(inst_, {drag_.i});
    std::copy(inst_.matrix(drag_.i), inst_.matrix(drag_.i) + kInstFloats, drag_.base);
    drag_.has_start = render::ground(ray, drag_.base[10], drag_.start);
    float gy = 0;
    drag_.has_ground = follow_ground && terrain_height(drag_.base[9], drag_.base[11], drag_.base[10] + 1.0f, gy);
    drag_.ground_off = drag_.has_ground ? drag_.base[10] - gy : 0.0f;
    drag_.sy = y;
    drag_.ex = x;
    drag_.shift = shift;
    return true;
}

void TrackView::edit_drag(float x, float y, float w, float h, const render::OrbitCamera& cam, bool shift) {
    if (!drag_.active || drag_.numeric) return;
    float* m = inst_.matrix(drag_.i);
    if (tool_ == Tool::Move && shift != drag_.shift) {
        // trocou entre subir/descer e arrastar no chão: recomeça do ponto atual, sem perder o que já mudou
        std::copy(m, m + kInstFloats, drag_.base);
        drag_.shift = shift;
        drag_.sy = y;
        drag_.has_start = render::ground(render::mouse_ray(cam, x, y, w, h), drag_.base[10], drag_.start);
    }
    if (tool_ == Tool::Move) {
        if (shift) {
            m[10] = snapped(drag_.base[10] - (y - drag_.sy) * std::max(0.02f, cam.dist * 0.002f), snap_move);
        } else if (drag_.has_start) {
            glm::vec3 p;
            if (render::ground(render::mouse_ray(cam, x, y, w, h), drag_.base[10], p)) {
                m[9] = snapped(drag_.base[9] + p.x - drag_.start.x, snap_move);
                m[11] = snapped(drag_.base[11] + p.z - drag_.start.z, snap_move);
                m[10] = drag_.base[10];
                float gy = 0;
                // grudado: a altura segue o terreno, com a mesma folga de quando pegou o objeto
                if (follow_ground && drag_.has_ground && terrain_height(m[9], m[11], drag_.base[10] + 1.0f, gy))
                    m[10] = gy + drag_.ground_off;
            }
        }
    } else {
        float th = (x - drag_.ex) * 0.01f;
        if (snap_turn > 0) th = snapped(th, snap_turn * 3.14159265f / 180.0f);
        edit::spin(m, drag_.base, th);
    }
    objects_->touch(inst_, drag_.i);
}

void TrackView::end_edit() {
    if (!drag_.active || drag_.numeric) return;
    drag_.active = false;
    edit::snap_to_file(inst_, drag_.i);
    commit(tool_ == Tool::Move ? "Mover" : "Girar", std::move(drag_.before));
}

void TrackView::click(float x, float y, float w, float h, const render::OrbitCamera& cam) {
    close_change();
    if (drag_.active) return;
    const auto ray = render::mouse_ray(cam, x, y, w, h);
    sel_ = render::pick(ray, inst_, *objects_, cam.target, draw_dist_, layers_, visible_terrain_t(ray));
}

int TrackView::pick_ray(const render::Ray& ray, const glm::vec3& target) {
    return render::pick(ray, inst_, *objects_, target, draw_dist_, layers_, visible_terrain_t(ray));
}

float TrackView::visible_terrain_t(const render::Ray& ray) {
    // o terreno escondido não esconde nada: sem ele na tela, o raio não para nele
    float t = 0;
    if (show_terrain_ && terrain_hit(ray, t)) return t + kTerrainSlack;
    return std::numeric_limits<float>::infinity();
}

bool TrackView::terrain_hit(const render::Ray& ray, float& t) {
    return probe_ && terrain_ && probe_->cast(*terrain_, ray.o, ray.d, 40000.0f, t);
}

bool TrackView::terrain_height(float x, float z, float y_from, float& y) {
    return probe_ && terrain_ && probe_->height(*terrain_, x, z, y_from, y);
}

void TrackView::settle_selected() {
    if (sel_ < 0 || drag_.active || inst_.hidden[static_cast<std::size_t>(sel_)]) return;
    const auto i = static_cast<std::uint32_t>(sel_);
    float m[kInstFloats];
    std::copy(inst_.matrix(i), inst_.matrix(i) + kInstFloats, m);
    float y = 0;
    // o que está até 1 m acima da origem conta como chão (calombo); acima disso (ponte) fica de fora
    if (!terrain_height(m[9], m[11], m[10] + 1.0f, y)) {
        set_status("sem terreno em x " + std::to_string(static_cast<int>(m[9])) + ", z " + std::to_string(static_cast<int>(m[11])));
        return;
    }
    if (std::fabs(y - m[10]) < 1e-4f) {
        set_status("já está no chão");
        return;
    }
    if (!begin_change(i)) return;
    m[10] = y;
    set_matrix(i, m);
    end_change("Assentar no terreno");
}

bool TrackView::fit_footprint(std::uint32_t i, float& x0, float& x1, float& z0, float& z1, bool& upright) const {
    const auto& ty = objects_->types()[inst_.type[i]];
    if (ty.empty || ty.layer == render::Layer::Dist) return false;
    const bool tree = ty.layer == render::Layer::Tree;
    // árvore: só o tronco conta (a copa larga enterraria a árvore inteira no barranco)
    if (tree) x0 = z0 = -0.4f, x1 = z1 = 0.4f;
    else x0 = ty.lo.x, x1 = ty.hi.x, z0 = ty.lo.z, z1 = ty.hi.z;
    upright = tree || align_upright;
    return true;
}

bool TrackView::fit_instance(std::uint32_t i, float* out) {
    edit::Footprint foot;
    bool upright = false;
    if (!fit_footprint(i, foot.x0, foot.x1, foot.z0, foot.z1, upright)) return false;
    const float* m = inst_.matrix(i);
    const float scale = std::max({std::hypot(m[0], m[1], m[2]), std::hypot(m[6], m[7], m[8]), 1e-3f});
    // procura o chão de um pouco acima da base: o lado de cima de uma rampa de até 45° ainda conta, ponte não
    const float y_from = m[10] + 1.0f + 0.5f * scale * std::hypot(foot.x1 - foot.x0, foot.z1 - foot.z0);
    const auto height = [&](float x, float z, float& y) { return terrain_height(x, z, y_from, y); };
    return edit::fit_to_ground(m, foot, upright ? edit::FitMode::Upright : edit::FitMode::Tilt, height, out);
}

void TrackView::align_selected() {
    if (sel_ < 0 || drag_.active || inst_.hidden[static_cast<std::size_t>(sel_)]) return;
    const auto i = static_cast<std::uint32_t>(sel_);
    float m[kInstFloats];
    if (!fit_instance(i, m)) {
        set_status("sem terreno sob a base (ou tipo sem malha)");
        return;
    }
    const float* cur = inst_.matrix(i);
    if (std::equal(m, m + kInstFloats, cur, [](float a, float b) { return std::fabs(a - b) < 1e-4f; })) {
        set_status("já está alinhado ao terreno");
        return;
    }
    if (!begin_change(i)) return;
    set_matrix(i, m);
    end_change("Alinhar ao terreno");
}

void TrackView::align_type() {
    if (sel_ < 0 || drag_.active) return;
    const std::uint16_t type = inst_.type[static_cast<std::size_t>(sel_)];
    std::vector<std::uint32_t> idx;
    std::vector<float> fitted;
    std::size_t missing = 0;
    for (std::uint32_t i = 0; i < inst_.n; ++i) {
        if (inst_.type[i] != type || inst_.hidden[i]) continue;
        float m[kInstFloats];
        if (!fit_instance(i, m)) {
            ++missing;
            continue;
        }
        const float* cur = inst_.matrix(i);
        if (std::equal(m, m + kInstFloats, cur, [](float a, float b) { return std::fabs(a - b) < 1e-4f; })) continue;
        idx.push_back(i);
        fitted.insert(fitted.end(), m, m + kInstFloats);
    }
    const std::string& name = track_.types[type].name;
    if (idx.empty()) {
        set_status(name + ": nada a alinhar" + (missing ? " (" + std::to_string(missing) + " sem terreno)" : ""));
        return;
    }
    auto before = edit::snapshot(inst_, idx);
    for (std::size_t k = 0; k < idx.size(); ++k) {
        std::copy(&fitted[k * kInstFloats], &fitted[(k + 1) * kInstFloats], inst_.matrix(idx[k]));
        edit::snap_to_file(inst_, idx[k]);
    }
    commit("Alinhar tipo ao terreno", std::move(before));
    set_status(name + ": " + std::to_string(idx.size()) + " alinhadas ao terreno" +
               (missing ? "; " + std::to_string(missing) + " sem terreno" : ""));
}

bool TrackView::save() {
    // todas as rotas abertas, como o web (tvEditList percorre o cache de rotas)
    std::vector<edit::RouteEdits> routes;
    for (std::size_t k = 0; k < track_.routes.size(); ++k) {
        if (k == route_index_) routes.push_back({route_, &inst_});
        else if (auto it = saved_.find(k); it != saved_.end()) routes.push_back({&track_.routes[k], &it->second.inst});
    }
    std::size_t count = 0;
    const std::string text = edit::edits_json(track_, routes, &count);
    try {
        // o primeiro Ctrl+S da sessão guarda o edits.json que já existia (de outra sessão) em .<n>.bak
        std::string bak;
        // um .bak por sessão, mesmo que a gravação depois falhe e o usuário tente de novo
        if (!backed_up_) bak = edit::backup_existing(out_);
        backed_up_ = true;
        edit::write_text(out_, text);
        saved_text_ = text;
        ++saves_;
        std::error_code ec;
        std::filesystem::remove(autosave_path(), ec);  // o arquivo de verdade está em dia
        set_status("gravado " + out_ + " (" + std::to_string(count) + " edições)" + (bak.empty() ? "" : "; anterior em " + bak));
        std::printf("gravado %s (%zu edições)%s%s\n", out_.c_str(), count, bak.empty() ? "" : "; anterior copiado para ",
                    bak.c_str());
        std::fflush(stdout);
        return true;
    } catch (const std::exception& e) {
        set_status(std::string("NÃO GRAVOU: ") + e.what());
        return false;
    }
}

std::string TrackView::current_edits() const {
    std::vector<edit::RouteEdits> routes;
    for (std::size_t k = 0; k < track_.routes.size(); ++k) {
        if (k == route_index_) routes.push_back({route_, &inst_});
        else if (auto it = saved_.find(k); it != saved_.end()) routes.push_back({&track_.routes[k], &it->second.inst});
    }
    return edit::edits_json(track_, routes, nullptr);
}

bool TrackView::unsaved() const {
    // durante um arraste já há mudança; refazer o edits.json a cada movimento custa caro com milhares de edições
    if (drag_.active) return true;
    // os painéis perguntam a cada quadro: só recalcula quando algo mudou
    if (unsaved_rev_ != edit_rev_ || unsaved_saves_ != saves_) {
        try {
            unsaved_ = current_edits() != saved_text_;
        } catch (const std::exception&) {
            unsaved_ = true;  // matriz inválida: o Ctrl+S vai dizer por quê
        }
        unsaved_rev_ = edit_rev_;
        unsaved_saves_ = saves_;
    }
    return unsaved_;
}

void TrackView::set_status(std::string msg) {
    std::fprintf(stderr, "viewer3d: %s\n", msg.c_str());
    status_ = std::move(msg);
    status_time_ = std::chrono::steady_clock::now();
}

double TrackView::status_age() const { return seconds_since(status_time_); }

void TrackView::autosave_tick(double period) {
    if (seconds_since(autosave_t_) < period) return;
    autosave_t_ = std::chrono::steady_clock::now();
    if (drag_.active || !unsaved()) return;
    try {
        edit::write_text(autosave_path(), current_edits());
        wrote_autosave_ = true;
        std::fprintf(stderr, "viewer3d: autosave em %s\n", autosave_path().c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "viewer3d: autosave falhou: %s\n", e.what());
    }
}

void TrackView::after_history() {
    ++edit_rev_;
    // desfazer uma cópia ou refazer um Apagar esconde o selecionado: tira a seleção
    if (sel_ >= 0 && inst_.hidden[static_cast<std::size_t>(sel_)]) sel_ = -1;
}

std::string TrackView::title() const {
    static const char* const tools[] = {"navegar", "mover", "girar"};
    std::string sel;
    if (sel_ >= 0) {
        const auto i = static_cast<std::size_t>(sel_);
        const std::string& name = track_.types[inst_.type[i]].name;
        const float* m = inst_.matrix(i);
        char buf[200];
        const std::uint32_t id = inst_.idnum[i];
        const std::string who = id >= kAdded ? "cópia de " + std::to_string(id - kAdded) : std::to_string(id);
        const std::string shown = name.size() > 2 ? name.substr(2) : name;
        const char kind = name.empty() ? '?' : name[0];
        std::snprintf(buf, sizeof buf, " | %s | kind %c | %s | %.2f %.2f %.2f", shown.c_str(), kind, who.c_str(),
                      static_cast<double>(m[9]), static_cast<double>(m[10]), static_cast<double>(m[11]));
        sel = buf;
    }
    return std::string(tools[static_cast<int>(tool_)]) + " | " + route_->name + " | " + thousands(terrain_->meshes()) + " malhas | " + thousands(terrain_->vertices()) +
           " vértices | " + thousands(terrain_->triangles()) + " tri | inst " + thousands(objects_->visible()) + "/" +
           thousands(inst_.n) + " | " + std::to_string(static_cast<int>(draw_dist_)) + " m | hist " +
           std::to_string(hist_.pos()) + "/" + std::to_string(hist_.size()) + (unsaved() ? " | não gravado" : "") + sel;
}

}  // namespace dr2::app
