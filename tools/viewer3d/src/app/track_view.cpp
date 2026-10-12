#include "app/track_view.hpp"
#include "core/i18n.hpp"

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
#include <random>
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

using dr2::i18n::tr;
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

// As 12 floats da instância (três linhas e a posição) como matriz 4×4 da pista, e de volta.
glm::mat4 to_mat(const float* m) {
    return glm::mat4(m[0], m[1], m[2], 0.0f, m[3], m[4], m[5], 0.0f, m[6], m[7], m[8], 0.0f, m[9], m[10], m[11], 1.0f);
}

void from_mat(const glm::mat4& a, float* m) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 3; ++r) m[c * 3 + r] = a[c][r];
}

bool finite_matrix(const float* m) {
    for (std::size_t k = 0; k < kInstFloats; ++k)
        if (!std::isfinite(m[k]) || std::fabs(m[k]) > 1e6f) return false;
    return true;
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
    for (const Route& r : track_.routes) replay0_.push_back(r.replay);
    if (edit::inside_game_folder(out_)) throw std::runtime_error("--out " + out_ + tr(" fica dentro da pasta do jogo"));
    if (std::filesystem::is_directory(out_)) throw std::runtime_error("--out " + out_ + tr(" é uma pasta; passe o caminho do arquivo .json"));

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
    try {
        game_ = load_game_transform(dir);
    } catch (const std::exception& e) {
        live_error_ = std::string("game_transform.json: ") + e.what();
    }
    if (std::string err; !live_.open(err) && live_error_.empty()) live_error_ = err;
    probe_ = std::make_unique<render::TerrainProbe>();
    if (!can_write(out_)) set_status(tr("não vai dar para gravar em ") + out_ + tr(" (pasta sem permissão ou caminho impossível); use --out"));
    if (std::filesystem::exists(out_)) {
        if (resume) resume_edits();
        else set_status(out_ + tr(" já existe e não foi lido (--fresh); o primeiro Ctrl+S guarda uma cópia dele em .<n>.bak"));
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
                set_status(tr("ATENÇÃO: uma sessão anterior não gravou edições; Arquivo > Recuperar autosave (") + autosave_path() + ")");
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
            rep.skipped.push_back(name + tr(": rota não existe nesta pista"));
            continue;
        }
        Instances copy = read_dr2i(read_file(join_path(dir_, "inst_" + name + ".bin")), track_.types.size());
        edit::apply_edits(doc, track_, track_.routes[k], copy, rep);
        next[k] = std::move(copy);
    }
    // rota atual fora do doc volta ao arquivo; as outras abertas fora do doc também
    if (!next.count(route_index_))
        next[route_index_] = read_dr2i(read_file(join_path(dir_, "inst_" + route_->name + ".bin")), track_.types.size());
    for (std::size_t k = 0; k < track_.routes.size(); ++k) {
        track_.routes[k].replay = replay0_[k];
        edit::apply_replay(doc, track_.routes[k], track_.routes[k].replay);
    }
    lines_ = std::make_unique<render::RouteLines>(*route_);
    replay_sel_ = -1;
    saved_.clear();
    for (auto& [k, inst] : next) {
        if (k == route_index_) inst_ = std::move(inst);
        else saved_[k] = Saved{std::move(inst), edit::History{}};
    }
    hist_ = edit::History{};
    set_sel(-1);
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
        set_status(tr("recuperadas ") + std::to_string(rep.applied) + tr(" edições de ") + from + tr(" (sem histórico; Ctrl+S grava em ") + out_ + ")" +
                   (rep.skipped.empty() ? "" : "; " + std::to_string(rep.skipped.size()) + tr(" ficaram de fora")));
        return true;
    } catch (const std::exception& e) {
        set_status(std::string(tr("não recuperou ")) + from + ": " + e.what());
        return false;
    }
}

bool TrackView::recover_autosave() {
    try {
        const std::vector<std::uint8_t> bytes = read_file(autosave_path());
        return recover(std::string(bytes.begin(), bytes.end()), autosave_path());
    } catch (const std::exception& e) {
        set_status(std::string(tr("não leu o autosave: ")) + e.what());
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
        set_status(tr("retomadas ") + std::to_string(rep.applied) + tr(" edições de ") + out_ +
                   (rep.skipped.empty() ? "" : "; " + std::to_string(rep.skipped.size()) + tr(" ficaram de fora (veja o terminal)")));
    } catch (const std::exception& e) {
        set_status(tr("não leu ") + out_ + ": " + e.what() + tr("; começando sem edições (o primeiro Ctrl+S guarda o arquivo em .<n>.bak)"));
    }
}

void TrackView::load_route(std::size_t index) {
    // lê tudo antes de mudar o estado: se um arquivo falhar, a rota aberta continua como estava
    const Route& route = track_.routes.at(index);
    if (route.terrain_file.empty()) throw std::runtime_error(tr("track.json: a rota ") + route.name + tr(" não tem terreno"));
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
    set_sel(-1);
    replay_sel_ = -1;
    slot_sel_ = {};
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
        set_status(tr("não abriu ") + track_.routes[next].name + ": " + e.what());
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
        if (unity_keys) set_tool(Tool::Navigate);
        else turn_selected((mod & SDL_KMOD_SHIFT) ? -90.0f : -15.0f);
        return true;
    case SDLK_W:
        if (!unity_keys) return false;  // no clássico, W anda (main)
        set_tool(Tool::Move);
        return true;
    case SDLK_E:
        if (unity_keys) set_tool(Tool::Rotate);
        else turn_selected((mod & SDL_KMOD_SHIFT) ? 90.0f : 15.0f);
        return true;
    case SDLK_COMMA:
    case SDLK_PERIOD: turn_selected(((mod & SDL_KMOD_SHIFT) ? 90.0f : 15.0f) * (key == SDLK_PERIOD ? 1.0f : -1.0f)); return true;
    case SDLK_F1: show_terrain_ = !show_terrain_; return true;
    case SDLK_F2: layers_.obj = !layers_.obj; return true;
    case SDLK_F3: layers_.tree = !layers_.tree; return true;
    case SDLK_F4: layers_.dist = !layers_.dist; return true;
    case SDLK_LEFTBRACKET: draw_dist_ = std::max(100.0f, draw_dist_ - 100.0f); return true;
    case SDLK_RIGHTBRACKET: draw_dist_ = std::min(4000.0f, draw_dist_ + 100.0f); return true;
    case SDLK_G: show_gates_ = !show_gates_; return true;
    case SDLK_I: show_ai_ = !show_ai_; return true;
    case SDLK_C:
        if (mod & SDL_KMOD_SHIFT) {  // próxima câmera do replay, vista por ela
            const int n = static_cast<int>(route_->replay.cameras.size());
            if (n > 0) look_through((replay_sel_ + 1) % n, cam);
        } else {
            show_replay_ = !show_replay_;
        }
        return true;
    case SDLK_V:  // vista: livre → seguir o carro do jogo → pela câmera do jogo
        set_live_view(static_cast<LiveView>((static_cast<int>(live_view_) + 1) % 3));
        return true;
    case SDLK_L:
        if (mod & SDL_KMOD_SHIFT) {  // próxima vaga de largada, vista do banco do piloto
            const int n = route_->slot_count();
            if (n > 0) look_from_slot(route_->slot_at((route_->slot_index(slot_sel_) + 1) % n), cam);
        } else {
            show_grids_ = !show_grids_;
        }
        return true;
    default: return false;
    }
}

void TrackView::draw(const glm::mat4& view_proj, const render::OrbitCamera& cam) {
    if (car_) car_->begin_frame();
    textures_.begin_frame();  // envia o que as threads decodificaram, dentro do orçamento do quadro
    objects_->cull(inst_, cam.target, cam.dist, draw_dist_, layers_, edit_rev_);
    shader_.use();
    shader_.set_view_proj(view_proj);
    shader_.set_solid(view_mode != ViewMode::Textured);
    if (view_mode == ViewMode::Wire) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    if (show_terrain_) terrain_->draw(shader_, &textures_, view_proj, cam.target, terrain_dist_);
    objects_->draw(shader_, textures_);
    if (view_mode == ViewMode::Wire) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    const bool clean = clean_view();  // câmeras juntas: só o que o jogo também mostra
    if (!clean)
        for (std::uint32_t i : multi_)  // a principal mais clara que as outras selecionadas
            if (!inst_.hidden[i]) objects_->draw_one(shader_, textures_, inst_, i, static_cast<int>(i) == sel_ ? 1.0f : 0.6f);
    int inside = -1;  // vendo pela câmera em destaque: o ícone e o cone dela tampariam a vista
    if (replay_sel_ >= 0) {
        const Vec3& p = route_->replay.cameras[static_cast<std::size_t>(replay_sel_)].pos;
        if (glm::distance(cam.eye(), glm::vec3(p[0], p[1], p[2])) < 1.0f) inside = replay_sel_;
    }
    render::LinesShow show;
    show.gates = show_gates_;
    show.ai = show_ai_;
    show.replay = show_replay_;
    show.grids = show_grids_;
    show.sel_cam = replay_sel_;
    show.inside_cam = inside;
    show.sel_slot = route_->slot_index(slot_sel_);
    if (const GridSlot* s = route_->slot(slot_sel_))  // no banco do piloto: a caixa da vaga tamparia a vista
        if (glm::distance(cam.eye(), glm::vec3(s->pos[0], s->pos[1], s->pos[2])) < 3.0f) show.inside_slot = show.sel_slot;
    if (show_grid && !clean) draw_grid(cam);
    if (!clean) lines_->draw(shader_, show);
    draw_live();
    shader_.use();
    shader_.set_solid(false);
}

void TrackView::set_live_view(LiveView v) {
    live_view_ = v;
    if (sync_) sync_request(v == LiveView::Free);  // seguir o carro e a câmera do jogo = câmera livre desligada lá
    const char* const names[] = {tr("vista livre"), tr("seguindo o carro do jogo"), tr("vendo pela câmera do jogo")};
    set_status(names[static_cast<int>(v)] + (live_connected() ? std::string() : std::string(tr(" (jogo sem sinal: abra pelo F5)"))));
}

std::string TrackView::live_status() const {
    if (!live_.is_open()) return tr("jogo: ") + live_error_;
    if (!live_connected()) return live_.received() ? tr("jogo: sem sinal") : tr("jogo: esperando");
    const LiveSample& s = live_.last();
    char buf[96];
    if (s.car) std::snprintf(buf, sizeof buf, tr("jogo ao vivo · %.0f km/h%s"), static_cast<double>(s.speed_kmh), s.paused ? tr(" · pausado") : "");
    else std::snprintf(buf, sizeof buf, tr("jogo ao vivo · %s"), s.view ? tr("sem carro") : tr("fora da especial"));
    return std::string(buf) + (game_.loaded ? "" : tr(" · sem game_transform.json"));
}

namespace {

glm::vec3 slot_forward(const GridSlot& s) {
    glm::vec3 f(s.fwd[0], s.fwd[1], s.fwd[2]);
    return glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0.0f, 0.0f, 1.0f);
}

// Põe a órbita com o olho em `eye` olhando para `aim` (o pitch fica na faixa da órbita).
void aim_orbit(render::OrbitCamera& cam, const glm::vec3& eye, const glm::vec3& aim) {
    const glm::vec3 d = eye - aim;
    cam.dist = std::clamp(glm::length(d), render::OrbitCamera::kDistMin, render::OrbitCamera::kDistMax);
    cam.pitch = std::clamp(std::asin(d.y / glm::length(d)), render::OrbitCamera::kPitchMin, render::OrbitCamera::kPitchMax);
    cam.yaw = std::atan2(d.x, d.z);
    cam.target = aim;
}

}  // namespace

bool TrackView::look_from_slot(SlotRef r, render::OrbitCamera& cam) {
    select_slot(r);
    const GridSlot* s = route_->slot(slot_sel_);
    if (!s) return false;
    const glm::vec3 f = slot_forward(*s), c(s->pos[0], s->pos[1], s->pos[2]);
    // olho do piloto: 0,7 m acima do centro da vaga (1,2 m do asfalto), um pouco atrás do meio
    const glm::vec3 eye = c + glm::vec3(0.0f, 0.7f, 0.0f) - f * 0.3f;
    aim_orbit(cam, eye, eye + f * 25.0f - glm::vec3(0.0f, 1.2f, 0.0f));
    set_status(tr("vendo da vaga ") + route_->grids[static_cast<std::size_t>(slot_sel_.grid)].name + "/" + s->name);
    return true;
}

bool TrackView::frame_slot(SlotRef r, render::OrbitCamera& cam) {
    select_slot(r);
    const GridSlot* s = route_->slot(slot_sel_);
    if (!s) return false;
    const glm::vec3 f = slot_forward(*s), c(s->pos[0], s->pos[1], s->pos[2]);
    aim_orbit(cam, c - f * 12.0f + glm::vec3(0.0f, 6.0f, 0.0f), c + f * 4.0f);
    return true;
}

void TrackView::live_tick(render::OrbitCamera& cam) {
    live_.poll();
    if (!live_connected()) return;
    if (sync_) sync_tick(cam);
    if (live_view_ == LiveView::Free) return;
    const LiveSample& s = live_.last();
    if (live_view_ == LiveView::FollowCar && s.car) {
        const Vec3 p = game_.point_to_track(s.car_pos);
        cam.target = {p[0], p[1], p[2]};
    } else if (live_view_ == LiveView::GameCamera && s.view) {
        const Vec3 e = game_.point_to_track(s.eye), f = game_.dir_to_track(s.forward);
        const glm::vec3 eye(e[0], e[1], e[2]), fwd = glm::normalize(glm::vec3(f[0], f[1], f[2]));
        aim_orbit(cam, eye, eye + fwd * render::OrbitCamera::kDistMin);
    }
}

// ---- câmeras juntas

namespace {

bool same_camera(const render::OrbitCamera& a, const render::OrbitCamera& b) {
    constexpr float e = 1e-4f;
    return std::fabs(a.yaw - b.yaw) < e && std::fabs(a.pitch - b.pitch) < e && std::fabs(a.dist - b.dist) < e &&
           glm::length(a.target - b.target) < e;
}

}  // namespace

void TrackView::set_sync_camera(bool on) {
    if (on == sync_) return;
    sync_ = on;
    sync_seen_ = false;  // o próximo pacote põe o editor no estado do jogo
    asked_ = -1;
    if (to_game_.session == 0) {
        std::random_device rd;
        to_game_.session = rd() | 1u;  // nunca 0
    }
    set_status(on ? tr("câmeras do editor e do jogo juntas") : tr("câmeras do editor e do jogo separadas"));
}

void TrackView::sync_request(bool want_free) {
    if (++to_game_.request == 0) ++to_game_.request;
    to_game_.want_free = want_free;
    asked_ = want_free ? 1 : 0;
}

void TrackView::sync_push(const render::OrbitCamera& cam) {
    if (++to_game_.pose == 0) ++to_game_.pose;
    const glm::vec3 eye = cam.eye(), f = glm::normalize(cam.target - eye);
    to_game_.eye = game_.point_to_game({eye.x, eye.y, eye.z});
    to_game_.forward = game_.dir_to_game({f.x, f.y, f.z});
    synced_ = cam;
}

void TrackView::take_game_pose(render::OrbitCamera& cam) {
    const LiveSample& s = live_.last();
    if (s.view) {
        const Vec3 e = game_.point_to_track(s.eye), f = game_.dir_to_track(s.forward);
        const glm::vec3 eye(e[0], e[1], e[2]), fwd = glm::normalize(glm::vec3(f[0], f[1], f[2]));
        aim_orbit(cam, eye, eye + fwd * render::OrbitCamera::kDistMin);
    }
    synced_ = cam;
}

void TrackView::sync_tick(render::OrbitCamera& cam) {
    const LiveSample& s = live_.last();
    // o jogo ainda não pôs a nossa última pose (pedido no caminho, ou outro jogo aberto): a do editor vale
    const bool pending = to_game_.pose != 0 && s.editor_pose != to_game_.pose;
    if (!sync_seen_ || s.free != game_free_) {
        // a câmera livre mudou no jogo (F9 lá, ou o nosso pedido chegou): o editor vai junto
        const bool ours = asked_ == (s.free ? 1 : 0);
        sync_seen_ = true;
        game_free_ = s.free;
        asked_ = -1;
        game_moves_ = s.free_moves;
        const LiveView want = s.free ? LiveView::Free : LiveView::GameCamera;
        if (!ours && live_view_ != want && !(want == LiveView::GameCamera && live_view_ == LiveView::FollowCar)) {
            live_view_ = want;
            set_status(s.free ? tr("câmera livre ligada no jogo: as duas andam juntas") : tr("câmera livre desligada no jogo: vendo pela câmera do jogo"));
        }
        if (s.free) {
            if (ours || pending) sync_push(cam);  // o pedido foi daqui: o jogo vem para a vista do editor
            else take_game_pose(cam);           // F9 no jogo: o editor vai para onde o jogo está
        }
    } else if (s.free && live_view_ == LiveView::Free) {
        if (!same_camera(cam, synced_)) {
            sync_push(cam);  // mexeram aqui
            game_moves_ = s.free_moves;
        } else if (s.free_moves != game_moves_) {
            game_moves_ = s.free_moves;
            if (!pending) take_game_pose(cam);  // mexeram lá
        }
    }
    ++to_game_.seq;
    sender_.send(to_game_);
}

void TrackView::draw_grid(const render::OrbitCamera& cam) {
    // passo em potências de 10 conforme a distância; a cada 10 linhas uma mais clara
    grid_step_ = std::pow(10.0f, std::max(0.0f, std::floor(std::log10(std::max(cam.dist, 1.0f) / 10.0f))));
    const float step = grid_step_;
    constexpr int kHalf = 30;
    const float y = cam.target.y;
    const float cx = std::round(cam.target.x / step) * step, cz = std::round(cam.target.z / step) * step;
    const float ext = kHalf * step;
    render::DynamicLines::Group minor{{}, {0.32f, 0.34f, 0.38f}, false}, major{{}, {0.55f, 0.58f, 0.64f}, false};
    for (int k = -kHalf; k <= kHalf; ++k) {
        const float x = cx + k * step, z = cz + k * step;
        auto& g = std::lround(x / step) % 10 == 0 ? major : minor;
        g.points.push_back({x, y, cz - ext});
        g.points.push_back({x, y, cz + ext});
        auto& h = std::lround(z / step) % 10 == 0 ? major : minor;
        h.points.push_back({cx - ext, y, z});
        h.points.push_back({cx + ext, y, z});
    }
    shader_.use();
    grid_lines_.draw(shader_, {minor, major});
}

void TrackView::draw_live() {
    if (!show_live_ || !live_connected()) return;
    const LiveSample& s = live_.last();
    std::vector<render::DynamicLines::Group> groups;
    auto v = [](const Vec3& a) { return glm::vec3(a[0], a[1], a[2]); };
    if (s.car) {
        // linhas da rotação do jogo: 0 = direita, 1 = cima, 2 = frente
        const glm::vec3 c = v(game_.point_to_track(s.car_pos));
        const glm::vec3 r = v(game_.dir_to_track({s.car_rot[0], s.car_rot[1], s.car_rot[2]}));
        const glm::vec3 u = v(game_.dir_to_track({s.car_rot[3], s.car_rot[4], s.car_rot[5]}));
        const glm::vec3 f = v(game_.dir_to_track({s.car_rot[6], s.car_rot[7], s.car_rot[8]}));
        if (car_) {
            // a origem do jogo é o chão sob o carro, a mesma do modelo (rodas em y = 0)
            const float rows[12] = {r.x, r.y, r.z, u.x, u.y, u.z, f.x, f.y, f.z, c.x, c.y, c.z};
            shader_.use();
            car_->draw(shader_, rows);
        } else {
            groups.push_back({render::car_box(c, r, u, f, 1.8f, 4.1f, 0.45f, 0.85f), {1.0f, 0.15f, 0.6f}, true});
        }
        // mastro: acha o carro de longe (sai de cima do teto quando há modelo); câmeras juntas: fica só o modelo
        const glm::vec3 m0 = car_ ? c + u * 1.6f : c;
        if (!clean_view()) groups.push_back({{m0, m0 + u * 3.0f}, {1.0f, 0.15f, 0.6f}, true});
    }
    // a moldura da câmera do jogo some quando ela é a própria vista (pela câmera do jogo, ou câmeras juntas)
    if (s.view && live_view_ != LiveView::GameCamera && !(sync_ && s.free && live_view_ == LiveView::Free)) {
        const glm::vec3 e = v(game_.point_to_track(s.eye)), f = v(game_.dir_to_track(s.forward)), u = v(game_.dir_to_track(s.up));
        const glm::vec3 r = glm::normalize(glm::cross(f, u));
        const glm::vec3 c = e + f * 2.0f;
        std::vector<glm::vec3> pts;
        const glm::vec3 k[4] = {c + r * 1.0f + u * 0.6f, c - r * 1.0f + u * 0.6f, c - r * 1.0f - u * 0.6f, c + r * 1.0f - u * 0.6f};
        for (int i = 0; i < 4; ++i) {
            pts.push_back(e);
            pts.push_back(k[i]);
            pts.push_back(k[i]);
            pts.push_back(k[(i + 1) % 4]);
        }
        groups.push_back({pts, {1.0f, 1.0f, 0.3f}, true});
    }
    shader_.use();
    live_lines_.draw(shader_, groups);
}

void TrackView::set_car(const std::string& dir) {
    car_.reset();
    if (dir.empty()) return;
    try {
        car_ = std::make_unique<render::CarRenderer>(dir);
        std::fprintf(stderr, "viewer3d: carro %s de %s (%zu malhas, %zu vértices)\n", car_->id().c_str(), dir.c_str(),
                     car_->meshes(), car_->verts());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "viewer3d: carro em %s não abriu (%s); fica a caixa\n", dir.c_str(), e.what());
    }
}

bool TrackView::slot_clearance(const GridSlot& s, float& center, float& wheels) {
    const glm::vec3 f = slot_forward(s);
    glm::vec3 r = glm::cross(f, glm::vec3(0.0f, 1.0f, 0.0f));
    r = glm::length(r) < 1e-3f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::normalize(r);
    const glm::vec3 c(s.pos[0], s.pos[1], s.pos[2]);
    float y = 0;
    // o que está até 1 m acima do centro conta como chão: um carro afundado também aparece
    if (!terrain_height(c.x, c.z, c.y + 1.0f, y)) return false;
    center = c.y - y;
    wheels = center;
    // rodas: 80 % da meia largura e 70 % do meio comprimento da caixa da vaga
    for (const float u : {-1.0f, 1.0f})
        for (const float v : {-1.0f, 1.0f}) {
            const glm::vec3 p = c + r * (u * s.width * 0.4f) + f * (v * s.length * 0.35f);
            if (terrain_height(p.x, p.z, p.y + 1.0f, y)) wheels = std::min(wheels, p.y - y);
        }
    return true;
}

bool TrackView::look_through(int i, render::OrbitCamera& cam) {
    select_replay(i);
    if (replay_sel_ < 0) return false;
    const ReplayCamera& c = route_->replay.cameras[static_cast<std::size_t>(replay_sel_)];
    const glm::vec3 eye(c.pos[0], c.pos[1], c.pos[2]);
    glm::vec3 aim(c.aim[0], c.aim[1], c.aim[2]);
    if (glm::length(aim - eye) < render::OrbitCamera::kDistMin) aim = eye + glm::vec3(0.0f, 0.0f, render::OrbitCamera::kDistMin);
    const glm::vec3 d = eye - aim;
    cam.dist = std::clamp(glm::length(d), render::OrbitCamera::kDistMin, render::OrbitCamera::kDistMax);
    // o pitch da órbita vai de kPitchMin a kPitchMax: uma câmera que olha muito para cima fica aproximada
    cam.pitch = std::clamp(std::asin(d.y / glm::length(d)), render::OrbitCamera::kPitchMin, render::OrbitCamera::kPitchMax);
    cam.yaw = std::atan2(d.x, d.z);
    cam.target = aim;
    set_status(tr("vendo pela câmera ") + c.name);
    return true;
}

void TrackView::commit_replay(const char* label, Replay before) {
    if (hist_.commit_replay(label, std::move(before), route_->replay)) ++edit_rev_;
    lines_ = std::make_unique<render::RouteLines>(*route_);
}

double TrackView::gate_at(const Vec3& p) const {
    double best = std::numeric_limits<double>::infinity(), d = 0;
    for (const Gate& g : route_->gates) {
        const float dx = (g.l[0] + g.r[0]) * 0.5f - p[0], dz = (g.l[2] + g.r[2]) * 0.5f - p[2];
        const double e = static_cast<double>(dx * dx + dz * dz);
        if (e < best) best = e, d = g.d;
    }
    return d;
}

const Gate* TrackView::gate_near_d(double d) const {
    const auto& gates = route_->gates;
    if (gates.empty()) return nullptr;
    const double lap = gates.back().d;
    if (lap > 0) d = std::fmod(std::fmod(d, lap) + lap, lap);
    const Gate* best = &gates.front();
    for (const Gate& g : gates)
        if (std::fabs(g.d - d) < std::fabs(best->d - d)) best = &g;
    return best;
}

namespace {

std::string free_name(const Replay& rep, const char* prefix, bool zones) {
    for (int n = 1; n < 10000; ++n) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s%03d", prefix, n);
        bool used = false;
        if (zones) {
            for (const ReplayZone& z : rep.zones) used |= z.name == buf;
        } else {
            used = rep.find(buf) >= 0;
        }
        if (!used) return buf;
    }
    return prefix;
}

Vec3 to_vec3(const glm::vec3& v) { return {v.x, v.y, v.z}; }

}  // namespace

int TrackView::add_replay_camera(const render::OrbitCamera& cam, double lead) {
    if (!route_ || drag_.active) return -1;
    Replay before = route_->replay;
    Replay& rep = replay_mut();
    ReplayCamera c;
    c.name = free_name(rep, "camera_r0_spectator_", false);
    c.kind = "trackside";
    c.role = tr("criada no editor");
    c.pos = to_vec3(cam.eye());
    c.aim = to_vec3(cam.target);
    if (!route_->gates.empty()) c.s = gate_at(c.aim);
    rep.cameras.push_back(c);
    if (const Gate* g = route_->gates.empty() ? nullptr : gate_near_d(c.s - lead)) {
        ReplayZone z;
        z.name = free_name(rep, "Box", true);
        z.s = g->d;
        z.l = g->l;
        z.r = g->r;
        z.sw.push_back({c.name, 1.0});
        rep.zones.push_back(std::move(z));
    }
    commit_replay(tr("Nova câmera do replay"), std::move(before));
    replay_sel_ = static_cast<int>(rep.cameras.size()) - 1;
    set_status(tr("câmera ") + c.name + tr(" criada na vista atual") + (route_->gates.empty() ? tr(" (sem portões: sem zona de troca)") : ""));
    return replay_sel_;
}

bool TrackView::move_replay_camera(int i, const render::OrbitCamera& cam) {
    if (!route_ || drag_.active || i < 0 || i >= static_cast<int>(route_->replay.cameras.size())) return false;
    Replay before = route_->replay;
    ReplayCamera& c = replay_mut().cameras[static_cast<std::size_t>(i)];
    const glm::vec3 dp = cam.eye() - glm::vec3(c.pos[0], c.pos[1], c.pos[2]);
    const glm::vec3 da = cam.target - glm::vec3(c.aim[0], c.aim[1], c.aim[2]);
    for (Vec3& p : c.path) p = {p[0] + dp.x, p[1] + dp.y, p[2] + dp.z};
    for (Vec3& p : c.target) p = {p[0] + da.x, p[1] + da.y, p[2] + da.z};
    c.pos = to_vec3(cam.eye());
    c.aim = to_vec3(cam.target);
    if (c.s >= 0 && !route_->gates.empty()) c.s = gate_at(c.aim);
    const std::string name = c.name;
    commit_replay(tr("Mover câmera do replay"), std::move(before));
    set_status(tr("câmera ") + name + tr(" agora na vista atual"));
    return true;
}

bool TrackView::delete_replay_camera(int i) {
    if (!route_ || drag_.active || i < 0 || i >= static_cast<int>(route_->replay.cameras.size())) return false;
    Replay before = route_->replay;
    Replay& rep = replay_mut();
    const std::string name = rep.cameras[static_cast<std::size_t>(i)].name;
    rep.cameras.erase(rep.cameras.begin() + i);
    std::size_t gone = 0;
    for (ReplayZone& z : rep.zones) {
        const double total = [&] { double t = 0; for (const auto& w : z.sw) t += w.p; return t; }();
        if (!std::erase_if(z.sw, [&](const ReplaySwitch& w) { return w.camera == name; })) continue;
        double left = 0;
        for (const auto& w : z.sw) left += w.p;
        if (left > 0)
            for (auto& w : z.sw) w.p *= total / left;
    }
    gone = std::erase_if(rep.zones, [](const ReplayZone& z) { return z.sw.empty(); });
    commit_replay(tr("Apagar câmera do replay"), std::move(before));
    replay_sel_ = -1;
    set_status(tr("câmera ") + name + tr(" apagada") + (gone == 1 ? tr("; a zona de troca dela saiu junto") : gone > 1 ? "; " + std::to_string(gone) + tr(" zonas de troca saíram junto") : ""));
    return true;
}

bool TrackView::replay_zone_before(int i, double lead) {
    if (!route_ || drag_.active || i < 0 || i >= static_cast<int>(route_->replay.cameras.size()) || route_->gates.empty()) return false;
    Replay before = route_->replay;
    Replay& rep = replay_mut();
    const ReplayCamera& c = rep.cameras[static_cast<std::size_t>(i)];
    const Gate* g = gate_near_d(gate_at(c.aim) - lead);
    // a zona que só liga esta câmera muda de lugar; senão nasce uma nova
    ReplayZone* zone = nullptr;
    for (ReplayZone& z : rep.zones)
        if (z.sw.size() == 1 && z.sw.front().camera == c.name && z.lap == 0) zone = &z;
    if (!zone) {
        rep.zones.push_back(ReplayZone{free_name(rep, "Box", true), 0, {}, {}, 0, {{c.name, 1.0}}});
        zone = &rep.zones.back();
    }
    zone->s = g->d;
    zone->l = g->l;
    zone->r = g->r;
    const std::string name = c.name;
    commit_replay(tr("Zona de troca da câmera"), std::move(before));
    set_status(tr("zona de troca de ") + name + tr(" em ") + std::to_string(static_cast<int>(g->d)) + tr(" m da pista"));
    return true;
}

bool TrackView::replay_changed() const {
    return route_ && replay_json(route_->replay) != replay_json(replay0_[route_index_]);
}

bool TrackView::reset_replay() {
    if (!replay_changed() || drag_.active) return false;
    Replay before = route_->replay;
    replay_mut() = replay0_[route_index_];
    commit_replay(tr("Câmeras do replay do arquivo"), std::move(before));
    replay_sel_ = -1;
    set_status(tr("câmeras do replay voltaram às do track.json"));
    return true;
}

void TrackView::frame_selected(render::OrbitCamera& cam) const {
    if (sel_ < 0) return;
    // a caixa dos centros de todas as selecionadas, mais o tamanho da maior
    glm::vec3 lo(std::numeric_limits<float>::infinity()), hi(-std::numeric_limits<float>::infinity());
    float size = 0.0f;
    for (std::uint32_t i : multi_) {
        const float* m = inst_.matrix(i);
        const glm::vec3 p(m[9], m[10], m[11]);
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
        const auto& ty = objects_->types()[inst_.type[i]];
        size = std::max(size, ty.empty ? 4.0f : glm::length(ty.hi - ty.lo));
    }
    cam.target = (lo + hi) * 0.5f;
    cam.dist = std::clamp(std::max(size * 3.0f, glm::length(hi - lo) * 1.2f), 6.0f, render::OrbitCamera::kDistMax);
}

void TrackView::commit(const char* label, std::vector<edit::Snap> before) {
    std::vector<std::uint32_t> idx;
    for (const auto& s : before) idx.push_back(s.index);
    hist_.commit(label, std::move(before), edit::snapshot(inst_, idx));
    ++edit_rev_;
}

bool TrackView::undo() {
    if (drag_.active || !hist_.undo(inst_, &replay_mut())) return false;
    after_history();
    return true;
}

bool TrackView::redo() {
    if (drag_.active || !hist_.redo(inst_, &replay_mut())) return false;
    after_history();
    return true;
}

void TrackView::history_go(std::size_t pos) {
    while (hist_.pos() > pos && undo()) {}
    while (hist_.pos() < pos && redo()) {}
}

void TrackView::close_change() {
    // um campo do Inspector ainda aberto fecha o passo dele antes de a seleção mudar
    if (drag_.active && drag_.numeric) end_change(tr("Editar (campo)"));
}

void TrackView::set_sel(int i) {
    sel_ = i >= 0 && static_cast<std::uint32_t>(i) < inst_.n ? i : -1;
    multi_.clear();
    if (sel_ >= 0) multi_.push_back(static_cast<std::uint32_t>(sel_));
}

void TrackView::select(int i) {
    close_change();
    if (drag_.active) return;
    set_sel(i);
}

void TrackView::deselect() {
    close_change();
    if (drag_.active) return;
    set_sel(-1);
}

bool TrackView::is_selected(std::uint32_t i) const { return std::find(multi_.begin(), multi_.end(), i) != multi_.end(); }

void TrackView::toggle_select(int i) {
    close_change();
    if (drag_.active || i < 0 || static_cast<std::uint32_t>(i) >= inst_.n) return;
    const auto u = static_cast<std::uint32_t>(i);
    if (auto it = std::find(multi_.begin(), multi_.end(), u); it != multi_.end()) {
        multi_.erase(it);
        if (sel_ == i) sel_ = multi_.empty() ? -1 : static_cast<int>(multi_.back());
    } else {
        multi_.push_back(u);
        sel_ = i;
    }
}

void TrackView::select_many(const std::vector<std::uint32_t>& idx, bool add) {
    close_change();
    if (drag_.active) return;
    if (!add) set_sel(-1);
    for (std::uint32_t i : idx) {
        if (i >= inst_.n) continue;
        if (!is_selected(i)) multi_.push_back(i);
        sel_ = static_cast<int>(i);
    }
}

std::vector<std::uint32_t> TrackView::live_selection() const {
    std::vector<std::uint32_t> out;
    if (sel_ >= 0 && !inst_.hidden[static_cast<std::size_t>(sel_)]) out.push_back(static_cast<std::uint32_t>(sel_));
    for (std::uint32_t i : multi_)
        if (static_cast<int>(i) != sel_ && !inst_.hidden[i]) out.push_back(i);
    return out;
}

std::size_t TrackView::select_rect(float x0, float y0, float x1, float y1, float w, float h, const render::OrbitCamera& cam,
                                   bool add) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    const glm::mat4 vp = cam.proj(w / std::max(1.0f, h)) * cam.view();
    std::vector<std::uint32_t> hit;
    objects_->for_each_near(cam.target, draw_dist_, [&](std::uint32_t i) {
        if (!objects_->passes(inst_, i, cam.target, draw_dist_, layers_) || locked(i)) return;
        const float* m = inst_.matrix(i);
        const glm::vec4 c = vp * glm::vec4(m[9], m[10], m[11], 1.0f);
        if (c.w <= 1e-4f) return;  // atrás do olho
        const float sx = (c.x / c.w * 0.5f + 0.5f) * w, sy = (0.5f - c.y / c.w * 0.5f) * h;
        if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1) hit.push_back(i);
    });
    std::sort(hit.begin(), hit.end());
    if (hit.empty() && !add) {
        deselect();
        return 0;
    }
    select_many(hit, add);
    return hit.size();
}

void TrackView::set_veiled(std::uint32_t i, bool on) {
    if (i >= inst_.n) return;
    inst_.set_flag(i, Instances::kVeiled, on);
    if (on && is_selected(i)) toggle_select(static_cast<int>(i));
    ++edit_rev_;  // refaz o corte
}

void TrackView::set_locked(std::uint32_t i, bool on) {
    if (i >= inst_.n) return;
    inst_.set_flag(i, Instances::kLocked, on);
    if (on && is_selected(i)) toggle_select(static_cast<int>(i));
}

void TrackView::clear_view_flags() {
    inst_.flags.clear();
    ++edit_rev_;
}

std::size_t TrackView::veiled_count() const {
    return static_cast<std::size_t>(
        std::count_if(inst_.flags.begin(), inst_.flags.end(), [](std::uint8_t f) { return (f & Instances::kVeiled) != 0; }));
}

std::vector<std::uint32_t> TrackView::of_type(std::uint16_t type) const {
    std::vector<std::uint32_t> out;
    for (std::uint32_t i = 0; i < inst_.n; ++i)
        if (inst_.type[i] == type && !inst_.hidden[i] && !veiled(i) && !locked(i)) out.push_back(i);
    return out;
}

std::vector<std::uint16_t> TrackView::palette_types() const {
    std::vector<bool> has(track_.types.size(), false);
    for (std::uint32_t i = 0; i < inst_.n; ++i)
        if (!inst_.hidden[i]) has[inst_.type[i]] = true;
    std::vector<std::uint16_t> out;
    const auto& types = objects_->types();
    for (std::size_t t = 0; t < track_.types.size(); ++t)
        if (has[t] && track_.types[t].name.starts_with("e:") && !types[t].empty) out.push_back(static_cast<std::uint16_t>(t));
    std::sort(out.begin(), out.end(), [&](std::uint16_t a, std::uint16_t b) { return track_.types[a].name < track_.types[b].name; });
    return out;
}

TrackView::Thumbs::~Thumbs() {
    for (auto& [type, t] : tex) glDeleteTextures(1, &t);
    if (depth) glDeleteRenderbuffers(1, &depth);
    if (fbo) glDeleteFramebuffers(1, &fbo);
}

unsigned TrackView::thumbnail(std::uint16_t type) {
    if (const auto it = thumbs_.tex.find(type); it != thumbs_.tex.end()) return it->second;
    if (std::find(thumbs_.queue.begin(), thumbs_.queue.end(), type) == thumbs_.queue.end()) thumbs_.queue.push_back(type);
    return 0;
}

void TrackView::thumb_tick(int budget) {
    const double now = SDL_GetTicks() / 1000.0;
    if (thumbs_.queue.empty() && !thumbs_.redo.empty() && now >= thumbs_.next_redo) {
        // texturas que estavam a caminho: refaz as miniaturas uma vez a cada 2 s, até 5 vezes
        for (auto it = thumbs_.redo.begin(); it != thumbs_.redo.end();) {
            thumbs_.queue.push_back(it->first);
            it = --it->second <= 0 ? thumbs_.redo.erase(it) : std::next(it);
        }
        thumbs_.next_redo = now + 2.0;
    }
    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    for (int k = 0; k < budget && !thumbs_.queue.empty(); ++k) {
        const std::uint16_t type = thumbs_.queue.front();
        thumbs_.queue.erase(thumbs_.queue.begin());
        if (!render_thumb(type)) break;
    }
    render_preview();
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
}

bool TrackView::render_thumb(std::uint16_t type) {
    std::uint32_t src = inst_.n;
    for (std::uint32_t i = 0; i < inst_.n && src == inst_.n; ++i)
        if (inst_.type[i] == type && !inst_.hidden[i]) src = i;
    const auto& ty = objects_->types()[type];
    if (src == inst_.n || ty.empty) return true;
    constexpr int side = kThumbSide;
    if (!thumbs_.fbo) {
        glGenFramebuffers(1, &thumbs_.fbo);
        glGenRenderbuffers(1, &thumbs_.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, thumbs_.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, side, side);
    }
    unsigned& tex = thumbs_.tex[type];
    if (!tex) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, thumbs_.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, thumbs_.depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    draw_type_framed(type, side, 0.8f, 0.45f, 1.0f, ViewMode::Textured);
    if (textures_.busy() && !thumbs_.redo.contains(type)) thumbs_.redo[type] = 5;
    glClearColor(0.55f, 0.68f, 0.82f, 1.0f);
    return true;
}

bool TrackView::draw_type_framed(std::uint16_t type, int side, float yaw, float pitch, float zoom, ViewMode mode) {
    std::uint32_t src = inst_.n;
    for (std::uint32_t i = 0; i < inst_.n && src == inst_.n; ++i)
        if (inst_.type[i] == type && !inst_.hidden[i]) src = i;
    if (src == inst_.n || type >= objects_->types().size()) return false;
    const auto& ty = objects_->types()[type];
    glViewport(0, 0, side, side);
    glClearColor(0.16f, 0.17f, 0.19f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glClearColor(0.55f, 0.68f, 0.82f, 1.0f);
    if (ty.empty) return false;
    // caixa do tipo no mundo, vista de cima e de lado como a câmera padrão
    const glm::mat4 model = to_mat(inst_.matrix(src));
    const glm::vec3 c = glm::vec3(model * glm::vec4((ty.lo + ty.hi) * 0.5f, 1.0f));
    const float r = std::max(0.25f, glm::length(glm::mat3(model) * ((ty.hi - ty.lo) * 0.5f)));
    render::OrbitCamera view;
    view.target = c;
    view.dist = r / std::sin(render::OrbitCamera::kFovY * 0.5f) * 1.05f * zoom;
    view.yaw = yaw;
    view.pitch = pitch;
    const glm::mat4 vp = glm::perspective(render::OrbitCamera::kFovY, 1.0f, std::max(0.05f, view.dist - r * 2.0f), view.dist + r * 2.0f) * view.view();
    shader_.use();
    shader_.set_view_proj(vp);
    shader_.set_solid(mode != ViewMode::Textured);
    if (mode == ViewMode::Wire) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    objects_->draw_one(shader_, textures_, inst_, src, 0.0f);
    if (mode == ViewMode::Wire) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    shader_.set_solid(view_mode != ViewMode::Textured);
    return true;
}

std::vector<std::uint16_t> TrackView::model_types() const {
    std::vector<bool> has(track_.types.size(), false);
    for (std::uint32_t i = 0; i < inst_.n; ++i)
        if (!inst_.hidden[i]) has[inst_.type[i]] = true;
    const auto& types = objects_->types();
    std::vector<std::uint16_t> out;
    for (std::size_t t = 0; t < track_.types.size() && t < types.size(); ++t)
        if (has[t] && !types[t].empty) out.push_back(static_cast<std::uint16_t>(t));
    std::sort(out.begin(), out.end(), [&](std::uint16_t a, std::uint16_t b) {
        if (types[a].layer != types[b].layer) return types[a].layer < types[b].layer;
        return track_.types[a].name < track_.types[b].name;
    });
    return out;
}

unsigned TrackView::preview(std::uint16_t type, int side, float yaw, float pitch, float zoom, ViewMode mode) {
    preview_.want = true;
    preview_.type = type;
    preview_.side = std::clamp(side, 64, 2048);
    preview_.yaw = yaw, preview_.pitch = pitch, preview_.zoom = zoom, preview_.mode = mode;
    return preview_.tex;
}

TrackView::Preview::~Preview() {
    if (tex) glDeleteTextures(1, &tex);
    if (depth) glDeleteRenderbuffers(1, &depth);
    if (fbo) glDeleteFramebuffers(1, &fbo);
}

void TrackView::render_preview() {
    Preview& p = preview_;
    if (!p.want) return;
    p.want = false;  // a janela pede de novo no próximo quadro enquanto estiver aberta
    if (!p.fbo) glGenFramebuffers(1, &p.fbo);
    if (!p.tex || p.made != p.side) {
        if (!p.tex) glGenTextures(1, &p.tex);
        glBindTexture(GL_TEXTURE_2D, p.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, p.side, p.side, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (!p.depth) glGenRenderbuffers(1, &p.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, p.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, p.side, p.side);
        p.made = p.side;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, p.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p.tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, p.depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return;
    draw_type_framed(p.type, p.side, p.yaw, p.pitch, p.zoom, p.mode);
}

int TrackView::place_type(std::uint16_t type, const glm::vec3& at) {
    close_change();
    if (drag_.active || type >= track_.types.size()) return -1;
    if (!track_.types[type].name.starts_with("e:")) {
        set_status(tr("só objetos e: (objects.ens) entram pela paleta; ornamentos e árvores têm contagem fixa"));
        return -1;
    }
    // modelo: a primeira instância do tipo nesta rota (mesma escala e giro do arquivo)
    std::uint32_t src = inst_.n;
    for (std::uint32_t i = 0; i < inst_.n && src == inst_.n; ++i)
        if (inst_.type[i] == type) src = i;
    if (src == inst_.n) {
        set_status(track_.types[type].name + tr(": nenhuma instância nesta rota para copiar"));
        return -1;
    }
    const float* m = inst_.matrix(src);
    const float off[3] = {at.x - m[9], at.y - m[10], at.z - m[11]};
    const std::uint32_t made = edit::duplicate(inst_, hist_, std::vector<std::uint32_t>{src}, off, tr("Pôr da paleta")).front();
    objects_->regroup(inst_);
    ++edit_rev_;
    set_sel(static_cast<int>(made));
    return static_cast<int>(made);
}

void TrackView::transform(const std::vector<std::uint32_t>& idx, const glm::mat4& d, const char* label) {
    if (idx.empty()) return;
    auto before = edit::snapshot(inst_, idx);
    for (std::uint32_t i : idx) {
        float m[kInstFloats];
        from_mat(d * to_mat(inst_.matrix(i)), m);
        if (!finite_matrix(m)) continue;
        std::copy(m, m + kInstFloats, inst_.matrix(i));
        edit::snap_to_file(inst_, i);
    }
    commit(label, std::move(before));
}

void TrackView::follow_group() {
    if (drag_.group.empty()) return;
    const glm::mat4 d = to_mat(inst_.matrix(drag_.i)) * glm::inverse(to_mat(drag_.base));
    for (std::size_t k = 0; k < drag_.group.size(); ++k) {
        float m[kInstFloats];
        from_mat(d * to_mat(&drag_.group_base[k * kInstFloats]), m);
        if (!finite_matrix(m)) continue;
        std::copy(m, m + kInstFloats, inst_.matrix(drag_.group[k]));
        objects_->touch(inst_, drag_.group[k]);
    }
}

void TrackView::duplicate_selected() {
    // duplicar só vale para objetos de objects.ens (ornamentos e árvores têm contagem fixa)
    if (drag_.active) return;
    const std::vector<std::uint32_t> live = live_selection();
    if (live.empty()) return;
    std::vector<std::uint32_t> src;
    for (std::uint32_t i : live)
        if (track_.types[inst_.type[i]].name.starts_with("e:")) src.push_back(i);
    if (src.empty()) {
        set_status(tr("duplicar só vale para objetos e: (objects.ens); ornamentos e árvores têm contagem fixa"));
        return;
    }
    const float off[3] = {2.0f, 0.0f, 0.0f};
    const std::vector<std::uint32_t> made = edit::duplicate(inst_, hist_, src, off);
    objects_->regroup(inst_);
    ++edit_rev_;
    // as cópias ficam selecionadas; a da principal (a primeira) continua principal
    select_many(made, false);
    sel_ = static_cast<int>(made.front());
    if (src.size() < live.size())
        set_status(std::to_string(src.size()) + tr(" duplicadas; ") + std::to_string(live.size() - src.size()) +
                   tr(" ficaram (ornamentos e árvores têm contagem fixa)"));
}

void TrackView::restore_selected() {
    if (sel_ < 0 || drag_.active) return;
    auto before = edit::snapshot(inst_, multi_);
    for (std::uint32_t i : multi_) {
        std::copy(inst_.m0.begin() + i * kInstFloats, inst_.m0.begin() + (i + 1) * kInstFloats, inst_.matrix(i));
        inst_.hidden[i] = 0;
    }
    commit(tr("Restaurar"), std::move(before));
}

void TrackView::turn_selected(float deg, int axis) {
    if (drag_.active) return;
    const std::vector<std::uint32_t> live = live_selection();
    if (live.empty() || axis < 0 || axis > 2) return;
    // em volta da principal: sozinha, gira no lugar; em grupo, as outras andam em volta dela
    const float* p = inst_.matrix(live.front());
    const glm::vec3 pivot(p[9], p[10], p[11]);
    glm::vec3 ax(0.0f);
    ax[axis] = 1.0f;
    const glm::mat4 d = glm::translate(glm::mat4(1.0f), pivot) * glm::rotate(glm::mat4(1.0f), glm::radians(deg), ax) *
                        glm::translate(glm::mat4(1.0f), -pivot);
    const char* const labels[] = {tr("Girar em X"), tr("Girar"), tr("Girar em Z")};
    transform(live, d, labels[axis]);
}

bool TrackView::begin_change(std::uint32_t i) {
    if (drag_.active || i >= inst_.n || inst_.hidden[i]) return false;
    drag_ = EditDrag{};
    drag_.active = true;
    drag_.numeric = true;
    drag_.i = i;
    std::copy(inst_.matrix(i), inst_.matrix(i) + kInstFloats, drag_.base);
    std::vector<std::uint32_t> all{i};
    if (static_cast<int>(i) == sel_)
        for (std::uint32_t k : live_selection())
            if (k != i) {
                drag_.group.push_back(k);
                drag_.group_base.insert(drag_.group_base.end(), inst_.matrix(k), inst_.matrix(k) + kInstFloats);
                all.push_back(k);
            }
    drag_.before = edit::snapshot(inst_, all);
    return true;
}

void TrackView::set_matrix(std::uint32_t i, const float* m) {
    if (!drag_.active || !drag_.numeric || i != drag_.i) return;  // só na instância do begin_change
    if (!finite_matrix(m)) return;                                 // nada de inf/1e39 no edits.json
    std::copy(m, m + kInstFloats, inst_.matrix(drag_.i));
    objects_->touch(inst_, drag_.i);  // só ela: o resto do corte e dos buffers continua valendo
    follow_group();
}

void TrackView::end_change(const char* label) {
    if (!drag_.active) return;
    drag_.active = false;
    edit::snap_to_file(inst_, drag_.i);
    for (std::uint32_t k : drag_.group) edit::snap_to_file(inst_, k);
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
    if (drag_.active) return;
    const std::vector<std::uint32_t> live = live_selection();
    if (live.empty()) return;
    auto before = edit::snapshot(inst_, live);
    for (std::uint32_t i : live) inst_.hidden[i] = 1;
    commit(tr("Apagar"), std::move(before));
    set_sel(-1);
}

bool TrackView::begin_edit(float x, float y, float w, float h, const render::OrbitCamera& cam, bool shift) {
    close_change();
    if (tool_ == Tool::Navigate || drag_.active) return false;
    const auto ray = render::mouse_ray(cam, x, y, w, h);
    HelperHit helper;
    if (pick_helper(ray, 2.0f * std::tan(render::OrbitCamera::kFovY / 2.0f) / h, helper)) return false;  // o clique seleciona
    const int hit = render::pick(ray, inst_, *objects_, cam.target, draw_dist_, layers_, visible_terrain_t(ray));
    if (hit < 0) return false;
    // pegar uma das selecionadas arrasta o grupo todo (ela vira a principal); outra troca a seleção
    if (is_selected(static_cast<std::uint32_t>(hit))) sel_ = hit;
    else set_sel(hit);
    drag_ = EditDrag{};
    drag_.active = true;
    drag_.i = static_cast<std::uint32_t>(hit);
    std::vector<std::uint32_t> all{drag_.i};
    for (std::uint32_t k : live_selection())
        if (k != drag_.i) {
            drag_.group.push_back(k);
            drag_.group_base.insert(drag_.group_base.end(), inst_.matrix(k), inst_.matrix(k) + kInstFloats);
            all.push_back(k);
        }
    drag_.before = edit::snapshot(inst_, all);
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
        for (std::size_t k = 0; k < drag_.group.size(); ++k)
            std::copy(inst_.matrix(drag_.group[k]), inst_.matrix(drag_.group[k]) + kInstFloats, &drag_.group_base[k * kInstFloats]);
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
    follow_group();
}

void TrackView::end_edit() {
    if (!drag_.active || drag_.numeric) return;
    drag_.active = false;
    edit::snap_to_file(inst_, drag_.i);
    for (std::uint32_t k : drag_.group) edit::snap_to_file(inst_, k);
    commit(tool_ == Tool::Move ? tr("Mover") : tr("Girar"), std::move(drag_.before));
}

void TrackView::click(float x, float y, float w, float h, const render::OrbitCamera& cam, bool add) {
    close_change();
    if (drag_.active) return;
    const auto ray = render::mouse_ray(cam, x, y, w, h);
    HelperHit helper;
    if (!add && pick_helper(ray, 2.0f * std::tan(render::OrbitCamera::kFovY / 2.0f) / h, helper)) {
        set_sel(-1);
        replay_sel_ = helper.camera;
        slot_sel_ = helper.slot;
        return;
    }
    const int hit = render::pick(ray, inst_, *objects_, cam.target, draw_dist_, layers_, visible_terrain_t(ray));
    if (add) {
        toggle_select(hit);  // no vazio, com Ctrl, a seleção fica como está
        return;
    }
    set_sel(hit);
    replay_sel_ = -1;
    slot_sel_ = SlotRef{};
}

bool TrackView::pick_helper(const render::Ray& ray, float pixel, HelperHit& hit) const {
    if (!route_) return false;
    float best = std::numeric_limits<float>::infinity();
    // esfera em volta do desenho, nunca menor que uns 10 pixels na tela; devolve a distância ao longo do raio
    auto test = [&](const glm::vec3& c, float radius) {
        const float t = glm::dot(c - ray.o, ray.d);
        if (t <= 0.0f) return false;
        if (glm::length(ray.o + ray.d * t - c) > std::max(radius, t * pixel * 10.0f) || t >= best) return false;
        best = t;
        return true;
    };
    if (show_replay_ && !clean_view()) {  // escondidas com as câmeras juntas: não se clica no que não aparece
        const auto& cams = route_->replay.cameras;
        for (std::size_t i = 0; i < cams.size(); ++i) {
            const glm::vec3 p(cams[i].pos[0], cams[i].pos[1], cams[i].pos[2]);
            if (glm::distance(ray.o, p) < 1.0f) continue;  // vendo por ela: o desenho dela não está na tela
            glm::vec3 f = glm::vec3(cams[i].aim[0], cams[i].aim[1], cams[i].aim[2]) - p;
            f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0.0f, 0.0f, 1.0f);
            // o ícone vai da lente (em pos) 2,4 m para trás, com os rolos em cima
            if (test(p - f * 1.2f + glm::vec3(0.0f, 0.4f, 0.0f), 1.5f)) hit = HelperHit{static_cast<int>(i), SlotRef{}};
        }
    }
    if (show_grids_ && !clean_view()) {
        const glm::vec3 eye = ray.o;
        for (std::size_t g = 0; g < route_->grids.size(); ++g) {
            const auto& slots = route_->grids[g].slots;
            for (std::size_t k = 0; k < slots.size(); ++k) {
                const GridSlot& s = slots[k];
                const glm::vec3 c(s.pos[0], s.pos[1] + 0.2f, s.pos[2]);
                if (glm::distance(eye, c) < 3.0f) continue;  // no banco do piloto dela
                if (test(c, 0.45f * std::max(s.width, s.length)))
                    hit = HelperHit{-1, SlotRef{static_cast<int>(g), static_cast<int>(k)}};
            }
        }
    }
    return hit.camera >= 0 || static_cast<bool>(hit.slot);
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
    if (drag_.active) return;
    const std::vector<std::uint32_t> live = live_selection();
    if (live.size() > 1) {
        // várias: cada uma desce (ou sobe) até o chão sob a própria origem, num passo só
        std::vector<std::uint32_t> idx;
        std::vector<float> ys;
        for (std::uint32_t i : live) {
            const float* m = inst_.matrix(i);
            float y = 0;
            if (terrain_height(m[9], m[11], m[10] + 1.0f, y) && std::fabs(y - m[10]) >= 1e-4f) {
                idx.push_back(i);
                ys.push_back(y);
            }
        }
        if (idx.empty()) {
            set_status(tr("já estão no chão (ou sem terreno embaixo)"));
            return;
        }
        auto before = edit::snapshot(inst_, idx);
        for (std::size_t k = 0; k < idx.size(); ++k) {
            inst_.matrix(idx[k])[10] = ys[k];
            edit::snap_to_file(inst_, idx[k]);
        }
        commit(tr("Assentar no terreno"), std::move(before));
        return;
    }
    if (live.empty()) return;
    const auto i = live.front();
    float m[kInstFloats];
    std::copy(inst_.matrix(i), inst_.matrix(i) + kInstFloats, m);
    float y = 0;
    // o que está até 1 m acima da origem conta como chão (calombo); acima disso (ponte) fica de fora
    if (!terrain_height(m[9], m[11], m[10] + 1.0f, y)) {
        set_status(tr("sem terreno em x ") + std::to_string(static_cast<int>(m[9])) + ", z " + std::to_string(static_cast<int>(m[11])));
        return;
    }
    if (std::fabs(y - m[10]) < 1e-4f) {
        set_status(tr("já está no chão"));
        return;
    }
    if (!begin_change(i)) return;
    m[10] = y;
    set_matrix(i, m);
    end_change(tr("Assentar no terreno"));
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
    if (drag_.active) return;
    const std::vector<std::uint32_t> live = live_selection();
    if (live.size() > 1) {
        std::vector<std::uint32_t> idx;
        std::vector<float> fitted;
        for (std::uint32_t i : live) {
            float m[kInstFloats];
            if (!fit_instance(i, m)) continue;
            const float* cur = inst_.matrix(i);
            if (std::equal(m, m + kInstFloats, cur, [](float a, float b) { return std::fabs(a - b) < 1e-4f; })) continue;
            idx.push_back(i);
            fitted.insert(fitted.end(), m, m + kInstFloats);
        }
        if (idx.empty()) {
            set_status(tr("já estão alinhadas ao terreno (ou sem terreno sob a base)"));
            return;
        }
        auto before = edit::snapshot(inst_, idx);
        for (std::size_t k = 0; k < idx.size(); ++k) {
            std::copy(&fitted[k * kInstFloats], &fitted[(k + 1) * kInstFloats], inst_.matrix(idx[k]));
            edit::snap_to_file(inst_, idx[k]);
        }
        commit(tr("Alinhar ao terreno"), std::move(before));
        return;
    }
    if (live.empty()) return;
    const auto i = live.front();
    float m[kInstFloats];
    if (!fit_instance(i, m)) {
        set_status(tr("sem terreno sob a base (ou tipo sem malha)"));
        return;
    }
    const float* cur = inst_.matrix(i);
    if (std::equal(m, m + kInstFloats, cur, [](float a, float b) { return std::fabs(a - b) < 1e-4f; })) {
        set_status(tr("já está alinhado ao terreno"));
        return;
    }
    if (!begin_change(i)) return;
    set_matrix(i, m);
    end_change(tr("Alinhar ao terreno"));
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
        set_status(name + tr(": nada a alinhar") + (missing ? " (" + std::to_string(missing) + tr(" sem terreno)") : ""));
        return;
    }
    auto before = edit::snapshot(inst_, idx);
    for (std::size_t k = 0; k < idx.size(); ++k) {
        std::copy(&fitted[k * kInstFloats], &fitted[(k + 1) * kInstFloats], inst_.matrix(idx[k]));
        edit::snap_to_file(inst_, idx[k]);
    }
    commit(tr("Alinhar tipo ao terreno"), std::move(before));
    set_status(name + ": " + std::to_string(idx.size()) + tr(" alinhadas ao terreno") +
               (missing ? "; " + std::to_string(missing) + tr(" sem terreno") : ""));
}

bool TrackView::save() {
    // todas as rotas abertas, como o web (tvEditList percorre o cache de rotas)
    std::vector<edit::RouteEdits> routes;
    for (std::size_t k = 0; k < track_.routes.size(); ++k) {
        if (k == route_index_) routes.push_back({route_, &inst_, &replay0_[k]});
        else if (auto it = saved_.find(k); it != saved_.end()) routes.push_back({&track_.routes[k], &it->second.inst, &replay0_[k]});
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
        set_status(tr("gravado ") + out_ + " (" + std::to_string(count) + tr(" edições)") + (bak.empty() ? "" : tr("; anterior em ") + bak));
        std::printf("gravado %s (%zu edições)%s%s\n", out_.c_str(), count, bak.empty() ? "" : tr("; anterior copiado para "),
                    bak.c_str());
        std::fflush(stdout);
        return true;
    } catch (const std::exception& e) {
        set_status(std::string(tr("NÃO GRAVOU: ")) + e.what());
        return false;
    }
}

std::string TrackView::current_edits() const {
    std::vector<edit::RouteEdits> routes;
    for (std::size_t k = 0; k < track_.routes.size(); ++k) {
        if (k == route_index_) routes.push_back({route_, &inst_, &replay0_[k]});
        else if (auto it = saved_.find(k); it != saved_.end()) routes.push_back({&track_.routes[k], &it->second.inst, &replay0_[k]});
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
    // o passo pode ter sido de câmera do replay: linhas refeitas, destaque válido
    lines_ = std::make_unique<render::RouteLines>(*route_);
    if (replay_sel_ >= static_cast<int>(route_->replay.cameras.size())) replay_sel_ = -1;
    // desfazer uma cópia ou refazer um Apagar esconde o selecionado: sai da seleção
    std::erase_if(multi_, [&](std::uint32_t i) { return i >= inst_.n || inst_.hidden[i]; });
    if (sel_ >= 0 && !is_selected(static_cast<std::uint32_t>(sel_))) sel_ = multi_.empty() ? -1 : static_cast<int>(multi_.back());
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
        const std::string who = id >= kAdded ? tr("cópia de ") + std::to_string(id - kAdded) : std::to_string(id);
        const std::string shown = name.size() > 2 ? name.substr(2) : name;
        const char kind = name.empty() ? '?' : name[0];
        std::snprintf(buf, sizeof buf, " | %s | kind %c | %s | %.2f %.2f %.2f", shown.c_str(), kind, who.c_str(),
                      static_cast<double>(m[9]), static_cast<double>(m[10]), static_cast<double>(m[11]));
        sel = buf;
    }
    return std::string(tools[static_cast<int>(tool_)]) + " | " + route_->name + " | " + thousands(terrain_->meshes()) + " malhas | " + thousands(terrain_->vertices()) +
           " vértices | " + thousands(terrain_->triangles()) + " tri | inst " + thousands(objects_->visible()) + "/" +
           thousands(inst_.n) + " | " + std::to_string(static_cast<int>(draw_dist_)) + " m | hist " +
           std::to_string(hist_.pos()) + "/" + std::to_string(hist_.size()) + (unsaved() ? tr(" | não gravado") : "") + sel;
}

}  // namespace dr2::app
