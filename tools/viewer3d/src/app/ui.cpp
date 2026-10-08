#include "app/ui.hpp"

#include "edit/edits_json.hpp"
#include "edit/history.hpp"
#include "render/pick.hpp"
#include "render/texture.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace dr2::app {

namespace {

constexpr float kPi = 3.14159265358979f;
const ImVec4 kRed(0.95f, 0.45f, 0.40f, 1.0f), kYellow(0.95f, 0.80f, 0.35f, 1.0f), kGrey(0.55f, 0.55f, 0.55f, 1.0f),
    kGreen(0.55f, 0.85f, 0.55f, 1.0f);

// Fonte com acentos: DejaVu ou Noto do sistema; sem nenhuma, a do ImGui (só Latin-1).
void load_font(float scale) {
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2013, 0x2026, 0x2190, 0x2193, 0x2212, 0x2212, 0};
    const char* const candidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",          "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "C:/Windows/Fonts/segoeui.ttf",
    };
    ImGuiIO& io = ImGui::GetIO();
    for (const char* path : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec) && io.Fonts->AddFontFromFileTTF(path, 15.0f * scale, nullptr, ranges))
            return;
    }
    ImFontConfig cfg;
    cfg.SizePixels = 13.0f * scale;
    io.Fonts->AddFontDefault(&cfg);
}

// "e:core_barr~a" -> "core_barr~a" (o kind vai à parte)
std::string short_name(const std::string& name) { return name.size() > 2 && name[1] == ':' ? name.substr(2) : name; }

bool contains_ci(const std::string& text, const char* needle) {
    if (!*needle) return true;
    const auto it = std::search(text.begin(), text.end(), needle, needle + std::strlen(needle),
                                [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    return it != text.end();
}

// Rótulo de uma instância: id de texto do objects.ens se houver, senão o idnum; cópias dizem de quem.
std::string instance_label(const TrackView& track, std::uint32_t i) {
    const Instances& inst = track.instances();
    const std::uint32_t id = inst.idnum[i];
    if (id >= kAdded) return "cópia de #" + std::to_string(id - kAdded);
    const std::string& name = track.track().types[inst.type[i]].name;
    const auto& ens = track.route().ens_ids;
    if (!name.empty() && name[0] == 'e' && id < ens.size() && !ens[id].empty()) return "#" + std::to_string(id) + "  " + ens[id];
    return "#" + std::to_string(id);
}

// Ângulo em Y da matriz (convenção de edit::spin: a linha 0 vai de +x para −z com θ positivo).
float yaw_of(const float* m) { return std::atan2(-m[2], m[0]); }

bool project(const glm::mat4& view_proj, const Rect& vp, const glm::vec3& p, glm::vec2& out) {
    const glm::vec4 c = view_proj * glm::vec4(p, 1.0f);
    if (c.w <= 1e-4f) return false;
    out = {vp.x + (c.x / c.w * 0.5f + 0.5f) * vp.w, vp.y + (0.5f - c.y / c.w * 0.5f) * vp.h};
    return true;
}

float seg_dist(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 ab = b - a;
    const float t = std::clamp(glm::dot(p - a, ab) / std::max(1e-6f, glm::dot(ab, ab)), 0.0f, 1.0f);
    return glm::length(p - (a + t * ab));
}

float snapped(float v, float step) { return step > 0 ? std::round(v / step) * step : v; }

void tooltip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", text);
}

}  // namespace

EditorUi::EditorUi(SDL_Window* window, void* gl_context) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // nada de imgui.ini na pasta de trabalho
    ImGui::StyleColorsDark();
    const float scale = std::max(1.0f, SDL_GetWindowDisplayScale(window));
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().WindowRounding = 0.0f;
    left_w_ *= scale;
    right_w_ *= scale;
    load_font(scale);
    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");
}

EditorUi::~EditorUi() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

bool EditorUi::event(const SDL_Event& e) {
    const ImGuiIO& io = ImGui::GetIO();
    // teclas de atalho (sem campo ativo, sem menu ou janela aberta) não vão ao ImGui: a fila dele
    // trata uma tecla por quadro, e com poucos fps centenas de teclas atrasam o menu por dezenas de
    // segundos. Modificadores vão sempre (Ctrl+clique para digitar), e a soltura de uma tecla que o
    // ImGui recebeu também.
    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const bool ui_keys = io.WantCaptureKeyboard || io.WantTextInput || popup;
    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        const SDL_Scancode sc = e.key.scancode;
        const bool modifier = (e.key.key == SDLK_LCTRL || e.key.key == SDLK_RCTRL || e.key.key == SDLK_LSHIFT ||
                               e.key.key == SDLK_RSHIFT || e.key.key == SDLK_LALT || e.key.key == SDLK_RALT);
        // repetição e soltura de uma tecla cujo apertar foi ao ImGui continuam indo para ele (senão ela fica presa)
        const bool held = sc < SDL_SCANCODE_COUNT && imgui_keys_[sc];
        const bool forward = modifier || held || (e.type == SDL_EVENT_KEY_DOWN && ui_keys);
        if (sc < SDL_SCANCODE_COUNT) {
            if (e.type == SDL_EVENT_KEY_DOWN && forward) imgui_keys_[sc] = true;
            if (e.type == SDL_EVENT_KEY_UP) imgui_keys_[sc] = false;
        }
        if (forward) ImGui_ImplSDL3_ProcessEvent(&e);
        return e.type == SDL_EVENT_KEY_DOWN && ui_keys;
    }
    if (e.type == SDL_EVENT_TEXT_INPUT) {
        if (io.WantTextInput) ImGui_ImplSDL3_ProcessEvent(&e);
        return io.WantCaptureKeyboard;
    }
    ImGui_ImplSDL3_ProcessEvent(&e);
    switch (e.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_WHEEL:
        return io.WantCaptureMouse;
    default:
        return false;
    }
}

bool EditorUi::wants_keyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }

bool EditorUi::ask_quit(TrackView* track) {
    if (!track || !track->unsaved()) return true;
    pending_ = Pending::Quit;
    return false;
}

void EditorUi::ask_open(TrackView* track, const std::string& dir) {
    if (track && track->unsaved()) {
        pending_ = Pending::Open;
        pending_dir_ = dir;
    } else {
        open_request = dir;
    }
}

void EditorUi::scan_tracks() {
    tracks_.clear();
    std::error_code ec;
    for (const char* root : {"build/uiview/tracks", "examples/tracks"}) {  // exportadas primeiro, exemplos no fim
        const std::size_t first = tracks_.size();
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (std::filesystem::is_regular_file(entry.path() / "track.json", ec)) tracks_.push_back(entry.path().generic_string());
        }
        std::sort(tracks_.begin() + static_cast<std::ptrdiff_t>(first), tracks_.end());
    }
    tracks_scanned_ = true;
}

Rect EditorUi::frame(TrackView* track, render::OrbitCamera& cam, float fps) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    poll_launch();
    const ImGuiViewport* main = ImGui::GetMainViewport();
    const float W = main->Size.x, H = main->Size.y;

    menu_bar(track, cam);
    float top = ImGui::GetFrameHeight();
    const float status_h = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y;
    if (track && panels) {
        const float bar_h = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2;
        ImGui::SetNextWindowPos(ImVec2(0, top));
        ImGui::SetNextWindowSize(ImVec2(W, bar_h));
        if (ImGui::Begin("##ferramentas", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus))
            toolbar(*track);
        ImGui::End();
        top += bar_h;
    }
    const float side_h = std::max(50.0f, H - top - status_h);
    Rect left{0, top, 0, side_h}, right{W, top, 0, side_h};
    if (track && panels) {
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus;
        left_w_ = std::clamp(left_w_, std::min(160.0f, W * 0.45f), std::max(160.0f, W * 0.45f));
        right_w_ = std::clamp(right_w_, std::min(200.0f, W * 0.45f), std::max(200.0f, W * 0.45f));
        ImGui::SetNextWindowPos(ImVec2(0, top));
        ImGui::SetNextWindowSize(ImVec2(left_w_, side_h));
        if (ImGui::Begin("Cena", nullptr, flags)) scene_tree(*track, cam, left);
        left_w_ = ImGui::GetWindowWidth();
        ImGui::End();
        ImGui::SetNextWindowPos(ImVec2(W - right_w_, top));
        ImGui::SetNextWindowSize(ImVec2(right_w_, side_h));
        if (ImGui::Begin("Inspector", nullptr, flags)) inspector(*track, cam, right);
        right_w_ = ImGui::GetWindowWidth();
        ImGui::End();
        left.w = left_w_;
        right.x = W - right_w_;
        right.w = right_w_;
    }
    status_bar(track, fps, Rect{0, H - status_h, W, status_h});
    if (show_help_) help_window();
    modals(track);

    vp_ = Rect{left.x + left.w, top, std::max(1.0f, right.x - (left.x + left.w)), side_h};
    return vp_;
}

void EditorUi::menu_bar(TrackView* track, render::OrbitCamera& cam) {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("Arquivo")) {
        if (ImGui::BeginMenu("Abrir pista")) {
            if (!tracks_scanned_) scan_tracks();
            if (tracks_.empty()) ImGui::TextDisabled("nada em build/uiview/tracks nem em examples/tracks");
            for (const std::string& dir : tracks_) {
                std::error_code ec;
                const bool current = track && std::filesystem::equivalent(dir, track->track().dir, ec);
                std::string label = std::filesystem::path(dir).filename().string();
                if (dir.rfind("examples/", 0) == 0) label += " (exemplo)";
                if (ImGui::MenuItem((label + "###" + dir).c_str(), nullptr, current, !current))
                    ask_open(track, dir);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Procurar de novo")) scan_tracks();
            ImGui::EndMenu();
        } else {
            tracks_scanned_ = false;  // relê ao abrir o submenu
        }
        if (ImGui::MenuItem("Gravar edits.json", "Ctrl+S", false, track != nullptr)) track->save();
        if (ImGui::MenuItem("Recuperar autosave", nullptr, false, track && track->has_autosave())) track->recover_autosave();
        if (track) tooltip("Volta às edições do último autosave (a cada 60 s); fica não gravado até o Ctrl+S");
        ImGui::Separator();
        if (ImGui::MenuItem("Sair", "Ctrl+Q") && ask_quit(track)) quit_request = true;
        ImGui::EndMenu();
    }
    if (track && ImGui::BeginMenu("Editar")) {
        const int sel = track->selected();
        const bool alive = sel >= 0 && !track->instances().hidden[static_cast<std::size_t>(sel)];
        if (ImGui::MenuItem("Desfazer", "Ctrl+Z", false, track->history().pos() > 0)) track->undo();
        if (ImGui::MenuItem("Refazer", "Ctrl+Y", false, track->history().pos() < track->history().size())) track->redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicar", "Ctrl+D", false, alive)) track->duplicate_selected();
        if (ImGui::MenuItem("Apagar", "Delete", false, alive)) track->delete_selected();
        if (ImGui::MenuItem("Restaurar do arquivo", "R", false, sel >= 0)) track->restore_selected();
        if (ImGui::MenuItem("Pôr no chão", "T", false, alive)) track->settle_selected();
        if (ImGui::MenuItem("Alinhar ao terreno", "Shift+T", false, alive)) track->align_selected();
        if (ImGui::MenuItem("Alinhar todos deste tipo", nullptr, false, alive)) track->align_type();
        if (ImGui::MenuItem("Girar +15°", "E", false, alive)) track->turn_selected(15.0f);
        if (ImGui::MenuItem("Girar −15°", "Q", false, alive)) track->turn_selected(-15.0f);
        ImGui::Separator();
        if (ImGui::MenuItem("Enquadrar", "F")) {
            if (sel >= 0) track->frame_selected(cam);
            else track->frame_route(cam);
        }
        if (ImGui::MenuItem("Tirar seleção", "Esc", false, sel >= 0)) track->deselect();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Exibir")) {
        if (track) {
            ImGui::MenuItem("Terreno", "F1", &track->show_terrain());
            ImGui::MenuItem("Objetos", "F2", &track->layers().obj);
            ImGui::MenuItem("Árvores", "F3", &track->layers().tree);
            ImGui::MenuItem("Terreno distante", "F4", &track->layers().dist);
            ImGui::MenuItem("Portões", "G", &track->show_gates());
            ImGui::MenuItem("Linha da IA", "I", &track->show_ai());
            ImGui::MenuItem("Câmeras do replay", "C", &track->show_replay());
            if (ImGui::MenuItem("Ver pela próxima câmera", "Shift+C", false, !track->route().replay.cameras.empty())) {
                const int n = static_cast<int>(track->route().replay.cameras.size());
                track->look_through((track->replay_selected() + 1) % n, cam);
            }
            ImGui::MenuItem("Largada (onde o carro nasce)", "L", &track->show_grids());
            if (ImGui::MenuItem("Ver da próxima vaga", "Shift+L", false, track->route().slot_count() > 0)) {
                const Route& r = track->route();
                track->look_from_slot(r.slot_at((r.slot_index(track->slot_selected()) + 1) % r.slot_count()), cam);
            }
            ImGui::Separator();
        }
        ImGui::MenuItem("Painéis", "F10", &panels);
        ImGui::MenuItem("Histórico", nullptr, &show_history_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ajuda")) {
        ImGui::MenuItem("Atalhos", "F11", &show_help_);
        ImGui::EndMenu();
    }
    play_button(track);
    ImGui::EndMainMenuBar();
}

void EditorUi::toolbar(TrackView& track) {
    static const char* const names[] = {"Navegar (1)", "Mover (2)", "Girar (3)"};
    static const char* const tips[] = {"Arrastar orbita a câmera; clique seleciona",
                                       "Arrastar o objeto no chão; Shift sobe e desce; setas do gizmo prendem num eixo",
                                       "Arrastar para os lados gira em Y; o anel do gizmo gira em torno do objeto"};
    for (int k = 0; k < 3; ++k) {
        if (k) ImGui::SameLine();
        const bool on = static_cast<int>(track.tool()) == k;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(names[k])) track.set_tool(static_cast<TrackView::Tool>(k));
        if (on) ImGui::PopStyleColor();
        tooltip(tips[k]);
    }
    ImGui::SameLine(0, 18);
    ImGui::BeginDisabled(track.history().pos() == 0);
    if (ImGui::Button("Desfazer")) track.undo();
    ImGui::EndDisabled();
    tooltip("Ctrl+Z");
    ImGui::SameLine();
    ImGui::BeginDisabled(track.history().pos() >= track.history().size());
    if (ImGui::Button("Refazer")) track.redo();
    ImGui::EndDisabled();
    tooltip("Ctrl+Y ou Ctrl+Shift+Z");
    ImGui::SameLine();
    const bool dirty = track.unsaved();
    if (dirty) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.40f, 0.10f, 1.0f));
    if (ImGui::Button("Gravar")) track.save();
    if (dirty) ImGui::PopStyleColor();
    tooltip(("Ctrl+S: grava " + track.out_path()).c_str());

    ImGui::SameLine(0, 18);
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("route_00000").x + 30);
    if (ImGui::BeginCombo("##rota", track.route().name.c_str())) {
        for (std::size_t k = 0; k < track.track().routes.size(); ++k)
            if (ImGui::Selectable(track.track().routes[k].name.c_str(), k == track.route_index())) track.open_route(k);
        ImGui::EndCombo();
    }
    tooltip("Rota (Tab / Shift+Tab). Cada rota guarda as próprias edições e histórico");

    ImGui::SameLine(0, 18);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Encaixe");
    ImGui::SameLine();
    static const float moves[] = {0.0f, 0.1f, 0.5f, 1.0f};
    static const char* const move_names[] = {"livre", "0,1 m", "0,5 m", "1 m"};
    int mi = 0;
    for (int k = 0; k < 4; ++k)
        if (track.snap_move == moves[k]) mi = k;
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("0,5 m").x + 40);
    if (ImGui::Combo("##mover", &mi, move_names, 4)) track.snap_move = moves[mi];
    tooltip("Passo do mover (arraste no chão, Shift e gizmo)");
    ImGui::SameLine();
    static const float turns[] = {0.0f, 5.0f, 15.0f, 45.0f};
    static const char* const turn_names[] = {"livre", "5°", "15°", "45°"};
    int ti = 0;
    for (int k = 0; k < 4; ++k)
        if (track.snap_turn == turns[k]) ti = k;
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("livre").x + 40);
    if (ImGui::Combo("##giro", &ti, turn_names, 4)) track.snap_turn = turns[ti];
    tooltip("Passo do girar por arraste e pelo anel do gizmo");
    ImGui::SameLine();
    ImGui::Checkbox("Grudar no chão", &track.follow_ground);
    tooltip("Ao arrastar no chão, a altura do objeto acompanha o terreno (mantém a folga que ele tinha)");

    ImGui::SameLine(0, 18);
    ImGui::SetNextItemWidth(160);
    ImGui::SliderFloat("Distância", &track.draw_dist(), 100.0f, 4000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
    tooltip("Raio de desenho dos objetos e árvores ([ e ])");
    ImGui::SameLine(0, 18);
    ImGui::SetNextItemWidth(150);
    float km = track.terrain_dist() / 1000.0f;
    if (ImGui::SliderFloat("Terreno", &km, 0.0f, 20.0f, km <= 0.0f ? "sem limite" : "%.1f km"))
        track.terrain_dist() = km < 0.25f ? 0.0f : km * 1000.0f;
    tooltip("Raio do terreno a partir da câmera. Em pistas grandes, um raio menor desenha menos (o horizonte some)");
}

void EditorUi::scene_tree(TrackView& track, render::OrbitCamera& cam, const Rect&) {
    const Instances& inst = track.instances();
    const auto& types = track.objects().types();
    const int sel = track.selected();
    if (sel != last_sel_) {
        scroll_to_sel_ = sel >= 0;
        last_sel_ = sel;
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filtro", "filtrar tipos (nome)", filter_, sizeof filter_);

    const float hist_h = show_history_ ? std::min(ImGui::GetContentRegionAvail().y * 0.35f, 220.0f) : 0.0f;
    ImGui::BeginChild("##arvore", ImVec2(0, -hist_h), ImGuiChildFlags_None);
    struct LayerRow {
        const char* label;
        render::Layer layer;
        bool* on;
    };
    const LayerRow rows[] = {{"Objetos", render::Layer::Obj, &track.layers().obj},
                             {"Árvores", render::Layer::Tree, &track.layers().tree},
                             {"Terreno distante", render::Layer::Dist, &track.layers().dist}};
    ImGui::Checkbox("##terreno", &track.show_terrain());
    tooltip("Mostrar o terreno (F1)");
    ImGui::SameLine();
    ImGui::TextUnformatted(("Terreno  " + std::to_string(track.terrain().meshes()) + " malhas").c_str());
    grids_tree(track, cam);
    replay_tree(track, cam);
    for (const LayerRow& row : rows) {
        std::size_t n_types = 0, n_inst = 0;
        for (const auto& ty : types)
            if (ty.layer == row.layer && !ty.group.empty()) ++n_types, n_inst += ty.group.size();
        ImGui::PushID(row.label);
        ImGui::Checkbox("##on", row.on);
        tooltip("Mostrar a camada");
        ImGui::SameLine();
        const bool sel_here = sel >= 0 && types[inst.type[static_cast<std::size_t>(sel)]].layer == row.layer;
        if (sel_here && scroll_to_sel_) ImGui::SetNextItemOpen(true);
        char label[96];
        std::snprintf(label, sizeof label, "%s (%zu tipos, %zu)", row.label, n_types, n_inst);
        if (ImGui::TreeNodeEx(label, row.layer == render::Layer::Obj ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            for (std::size_t t = 0; t < types.size(); ++t) {
                const auto& ty = types[t];
                if (ty.layer != row.layer || ty.group.empty() || !contains_ci(ty.name, filter_)) continue;
                const bool sel_type = sel >= 0 && inst.type[static_cast<std::size_t>(sel)] == t;
                if (sel_type && scroll_to_sel_) ImGui::SetNextItemOpen(true);
                // std::string: um nome longo não pode cortar o "###t<n>" (ids iguais abririam juntos)
                const std::string type_label = short_name(ty.name) + "  (" + std::to_string(ty.group.size()) + ")" +
                                               (ty.empty ? "  sem malha" : "") + "###t" + std::to_string(t);
                if (sel_type) ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
                const bool open = ImGui::TreeNodeEx(type_label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
                if (sel_type) ImGui::PopStyleColor();
                if (!open) continue;
                std::size_t sel_pos = ty.group.size();
                if (sel_type && scroll_to_sel_)
                    sel_pos = static_cast<std::size_t>(
                        std::find(ty.group.begin(), ty.group.end(), static_cast<std::uint32_t>(sel)) - ty.group.begin());
                ImGuiListClipper clip;
                clip.Begin(static_cast<int>(ty.group.size()));
                if (sel_pos < ty.group.size()) clip.IncludeItemByIndex(static_cast<int>(sel_pos));
                while (clip.Step()) {
                    for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                        const std::uint32_t i = ty.group[static_cast<std::size_t>(r)];
                        std::string text = instance_label(track, i);
                        const bool hidden = inst.hidden[i] != 0;
                        const bool edited = !hidden && edit::changed(inst, i);
                        const bool added = inst.idnum[i] >= kAdded;
                        if (hidden) text += added ? "  (desfeita)" : "  (apagada)";
                        else if (added) text += "  (nova)";
                        else if (edited) text += "  (editada)";
                        if (hidden) ImGui::PushStyleColor(ImGuiCol_Text, kGrey);
                        else if (edited || added) ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
                        ImGui::PushID(static_cast<int>(i));
                        if (ImGui::Selectable(text.c_str(), static_cast<int>(i) == sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                            track.select(static_cast<int>(i));
                            last_sel_ = track.selected();
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) track.frame_selected(cam);
                        }
                        if (static_cast<std::size_t>(r) == sel_pos && scroll_to_sel_) {
                            ImGui::SetScrollHereY(0.4f);
                            scroll_to_sel_ = false;
                        }
                        ImGui::PopID();
                        if (hidden || edited || added) ImGui::PopStyleColor();
                    }
                }
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (show_history_) {
        const edit::History& h = track.history();
        ImGui::SeparatorText(("Histórico " + std::to_string(h.pos()) + "/" + std::to_string(h.size())).c_str());
        ImGui::BeginChild("##historico");
        // com o histórico cheio, os passos mais antigos saíram: o início da lista não é mais o arquivo
        const std::string first = h.dropped() ? "(início: " + std::to_string(h.dropped()) + " passos mais antigos descartados)"
                                              : "(arquivo aberto)";
        if (ImGui::Selectable(first.c_str(), h.pos() == 0)) track.history_go(0);
        if (h.dropped()) tooltip("Desfazer tudo não volta mais ao arquivo; R restaura um objeto à matriz do arquivo");
        for (std::size_t k = 0; k < h.entries().size(); ++k) {
            const auto& e = h.entries()[k];
            const bool future = k >= h.pos();
            if (future) ImGui::PushStyleColor(ImGuiCol_Text, kGrey);
            char label[96];
            std::snprintf(label, sizeof label, "%zu. %s%s###h%zu", k + 1, e.label.c_str(), e.before.size() > 1 ? " (vários)" : "", k);
            if (ImGui::Selectable(label, k + 1 == h.pos())) track.history_go(k + 1);
            if (future) ImGui::PopStyleColor();
        }
        // rola para o fim só quando entra um passo novo (senão não dá para rolar a lista)
        if (h.size() != last_hist_size_) {
            if (h.pos() == h.size()) ImGui::SetScrollHereY(1.0f);
            last_hist_size_ = h.size();
        }
        ImGui::EndChild();
    }
}

void EditorUi::grids_tree(TrackView& track, render::OrbitCamera& cam) {
    const Route& route = track.route();
    if (route.grids.empty()) return;
    ImGui::PushID("grids");
    ImGui::Checkbox("##on", &track.show_grids());
    tooltip("Mostrar as vagas de largada (L)");
    ImGui::SameLine();
    char label[96];
    std::snprintf(label, sizeof label, "Largada (%d vagas)", route.slot_count());
    const int flat = route.slot_index(track.slot_selected());
    if (flat != last_slot_sel_) {  // Shift+L ou --look abrem a lista na vaga nova
        last_slot_sel_ = flat;
        if (flat >= 0) ImGui::SetNextItemOpen(true);
    }
    if (ImGui::TreeNodeEx(label)) {
        const SlotRef sel = track.slot_selected();
        if (ImGui::Button("Ver do carro") && sel) track.look_from_slot(sel, cam);
        tooltip("Põe a vista no banco do piloto da vaga em destaque (Shift+L: próxima)");
        ImGui::SameLine();
        if (ImGui::Button("Enquadrar##vaga") && sel) track.frame_slot(sel, cam);
        tooltip("Vista de trás e de cima da vaga");
        if (const GridSlot* s = route.slot(sel)) {
            const Grid& g = route.grids[static_cast<std::size_t>(sel.grid)];
            ImGui::TextColored(kGreen, "%s / %s", g.name.c_str(), s->name.c_str());
            ImGui::Text("%s", g.role.c_str());
            ImGui::Text("pos %.1f %.1f %.1f", s->pos[0], s->pos[1], s->pos[2]);
            if (s->s >= 0) ImGui::Text("na pista em %.0f m, %.1f m %s do centro", s->s, std::fabs(s->lat), s->lat >= 0 ? "à esquerda" : "à direita");
            ImGui::Text("caixa %.1f × %.1f m", s->width, s->length);
            float center = 0, wheels = 0;
            if (!track.slot_clearance(*s, center, wheels)) {
                ImGui::TextColored(kYellow, "sem terreno embaixo");
            } else {
                const bool low = wheels < TrackView::kSlotLow, high = center > TrackView::kSlotHigh;
                const ImVec4 color = low ? kRed : high ? kYellow : kGreen;
                ImGui::TextColored(color, "acima do chão: %.2f m (rodas %.2f m)", center, wheels);
                if (low) ImGui::TextWrapped("dentro do chão: no jogo o carro nasce enterrado e a carga trava");
                else if (high) ImGui::TextWrapped("alto demais: o carro cai ao nascer");
            }
        }
        for (std::size_t g = 0; g < route.grids.size(); ++g) {
            const Grid& grid = route.grids[g];
            const bool here = sel && sel.grid == static_cast<int>(g);
            if (here && flat != last_slot_open_) ImGui::SetNextItemOpen(true);
            const std::string head = grid.name + "  " + grid.role + "  (" + std::to_string(grid.slots.size()) + ")###g" + std::to_string(g);
            if (ImGui::TreeNodeEx(head.c_str(), g == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                for (std::size_t k = 0; k < grid.slots.size(); ++k) {
                    const GridSlot& s = grid.slots[k];
                    const SlotRef ref{static_cast<int>(g), static_cast<int>(k)};
                    char text[96];
                    std::snprintf(text, sizeof text, "%s  %.0f m%s###s%zu", s.name.c_str(), s.s, s.lat ? (s.lat > 0 ? "  esq." : "  dir.") : "", k);
                    if (ImGui::Selectable(text, here && sel.slot == static_cast<int>(k), ImGuiSelectableFlags_AllowDoubleClick)) {
                        track.select_slot(ref);
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) track.look_from_slot(ref, cam);
                    }
                }
                if (!grid.markers.empty()) ImGui::TextDisabled("%zu nós de apoio sem carro", grid.markers.size());
                ImGui::TreePop();
            }
        }
        last_slot_open_ = flat;
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void EditorUi::replay_tree(TrackView& track, render::OrbitCamera& cam) {
    const Replay& rep = track.route().replay;
    if (rep.cameras.empty() && rep.zones.empty()) return;
    ImGui::PushID("replay");
    ImGui::Checkbox("##on", &track.show_replay());
    tooltip("Mostrar câmeras, zonas e prismas do replay (C)");
    ImGui::SameLine();
    char label[96];
    std::snprintf(label, sizeof label, "Câmeras do replay (%zu)", rep.cameras.size());
    if (track.replay_selected() != last_replay_sel_) {  // Shift+C ou --look abrem a lista na câmera nova
        last_replay_sel_ = track.replay_selected();
        if (last_replay_sel_ >= 0) ImGui::SetNextItemOpen(true);
    }
    if (ImGui::TreeNodeEx(label)) {
        const int sel = track.replay_selected();
        if (ImGui::Button("Ver por esta câmera") && sel >= 0) track.look_through(sel, cam);
        tooltip("Põe a vista no lugar da câmera em destaque, olhando para onde ela olha (Shift+C: próxima)");
        if (sel >= 0) {
            const ReplayCamera& c = rep.cameras[static_cast<std::size_t>(sel)];
            ImGui::TextColored(kGreen, "%s", c.name.c_str());
            ImGui::Text("%s%s%s", c.kind.c_str(), c.role.empty() ? "" : " · ", c.role.c_str());
            ImGui::Text("pos %.1f %.1f %.1f", c.pos[0], c.pos[1], c.pos[2]);
            if (c.s >= 0) ImGui::Text("na pista em %.0f m", c.s);
            if (!c.path.empty())
                ImGui::Text("caminho: %zu trechos, %.1f s%s", c.path.size() / 4, c.duration, c.target.empty() ? "" : ", com alvo");
            std::string by;
            for (const ReplayZone& z : rep.zones)
                for (const ReplaySwitch& w : z.sw)
                    if (w.camera == c.name) by += (by.empty() ? "" : ", ") + z.name + " (" + std::to_string(static_cast<int>(w.p * 100 + 0.5)) + "%)";
            ImGui::TextWrapped("ligada por: %s", by.empty() ? "nenhuma zona (tomada do jogo)" : by.c_str());
        }
        for (std::size_t i = 0; i < rep.cameras.size(); ++i) {
            const ReplayCamera& c = rep.cameras[i];
            const std::string text = c.name + "  " + (c.role.empty() ? c.kind : c.role) + "###c" + std::to_string(i);
            if (ImGui::Selectable(text.c_str(), static_cast<int>(i) == sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                track.select_replay(static_cast<int>(i));
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) track.look_through(static_cast<int>(i), cam);
            }
        }
        std::snprintf(label, sizeof label, "Zonas de troca (%zu)", rep.zones.size());
        if (ImGui::TreeNodeEx(label)) {
            for (std::size_t i = 0; i < rep.zones.size(); ++i) {
                const ReplayZone& z = rep.zones[i];
                std::string text = z.name + "  " + std::to_string(static_cast<int>(z.s)) + " m";
                if (z.lap > 0) text += "  (volta " + std::to_string(z.lap) + ")";
                const int target = z.sw.empty() ? -1 : rep.find(z.sw.front().camera);
                if (ImGui::Selectable((text + "###z" + std::to_string(i)).c_str(), target >= 0 && target == sel)) track.select_replay(target);
                if (ImGui::IsItemHovered()) {
                    std::string tip;
                    for (const ReplaySwitch& w : z.sw) tip += w.camera + "  " + std::to_string(static_cast<int>(w.p * 100 + 0.5)) + "%\n";
                    ImGui::SetTooltip("%s", tip.c_str());
                }
            }
            ImGui::TreePop();
        }
        ImGui::TextDisabled("%zu prismas em volta das peças altas", rep.bounds.size());
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void EditorUi::inspector(TrackView& track, render::OrbitCamera& cam, const Rect&) {
    const Instances& inst = track.instances();
    const int sel = track.selected();
    if (sel < 0) {
        const Track& t = track.track();
        ImGui::SeparatorText("Pista");
        ImGui::TextWrapped("%s", t.id.c_str());
        ImGui::TextDisabled("%s", t.src.c_str());
        ImGui::TextDisabled("%s", t.dir.c_str());
        ImGui::Spacing();
        if (ImGui::BeginTable("##pista", 2, ImGuiTableFlags_SizingFixedFit)) {
            auto row = [](const char* k, const std::string& v) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", k);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(v.c_str());
            };
            row("Rota", track.route().name + "  (" + std::to_string(track.route_index() + 1) + " de " + std::to_string(t.routes.size()) + ")");
            row("Terreno", track.route().terrain_file);
            row("Malhas", std::to_string(track.terrain().meshes()));
            row("Vértices", std::to_string(track.terrain().vertices()));
            row("Triângulos", std::to_string(track.terrain().triangles()));
            row("Instâncias", std::to_string(track.objects().visible()) + " visíveis de " + std::to_string(inst.n));
            row("Tipos", std::to_string(t.types.size()));
            row("Materiais", std::to_string(t.materials.size()));
            auto& tex = track.textures();
            char buf[160];
            std::snprintf(buf, sizeof buf, "%zu na GPU, %zu na fila, %zu falharam", tex.loaded(), tex.pending(), tex.failed());
            row("Texturas", buf);
            std::snprintf(buf, sizeof buf, "%.0f de %.0f MB%s", static_cast<double>(tex.gpu_bytes()) / 1048576.0,
                          static_cast<double>(tex.budget()) / 1048576.0, tex.over_budget() ? "  (acima do limite!)" : "");
            row("VRAM das texturas", buf);
            if (std::size_t total = 0, free = 0; gl::vram_kb(total, free)) {
                std::snprintf(buf, sizeof buf, total ? "%.0f MB usados de %.0f MB" : "%.0f MB livres", total ? static_cast<double>(total - free) / 1024.0 : static_cast<double>(free) / 1024.0,
                              static_cast<double>(total) / 1024.0);
                row("VRAM da GPU", buf);
            }
            if (tex.evicted() || tex.downscaled()) {
                std::snprintf(buf, sizeof buf, "%zu reduzidas, %zu descartadas por falta de espaço", tex.downscaled(), tex.evicted());
                row("", buf);
            }
            row("Portões", std::to_string(track.route().gates.size()));
            row("Linhas da IA", std::to_string(track.route().ai.size()));
            row("Grava em", track.out_path());
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Clique num objeto (no 3D ou na Cena) para inspecionar.");
        return;
    }

    const auto i = static_cast<std::uint32_t>(sel);
    const std::string& name = track.track().types[inst.type[i]].name;
    const auto& ty = track.objects().types()[inst.type[i]];
    const bool hidden = inst.hidden[i] != 0;
    const bool added = inst.idnum[i] >= kAdded;
    ImGui::SeparatorText("Objeto");
    ImGui::TextWrapped("%s", short_name(name).c_str());
    ImGui::TextDisabled("kind %c  ·  %s", name.empty() ? '?' : name[0], TrackView::source_file(name));
    ImGui::TextUnformatted(instance_label(track, i).c_str());
    if (hidden) ImGui::TextColored(kGrey, added ? "Cópia desfeita" : "Apagado nesta sessão");
    else if (added) ImGui::TextColored(kYellow, "Cópia nova (vai como added no edits.json)");
    else if (edit::changed(inst, i)) ImGui::TextColored(kYellow, "Editado");
    else ImGui::TextDisabled("Como no arquivo");

    // Transformação: posição e giro em Y editáveis, escala só leitura
    ImGui::SeparatorText("Transformação");
    ImGui::BeginDisabled(hidden);
    ImGui::PushID(static_cast<int>(i));  // campos de outra seleção são outros campos (o texto aberto não passa adiante)
    static float base[kInstFloats];
    static float yaw0 = 0.0f;
    const float* m = inst.matrix(i);
    float pos[3] = {m[9], m[10], m[11]};
    ImGui::SetNextItemWidth(-1);
    const bool pos_changed = ImGui::DragFloat3("##pos", pos, 0.05f, 0.0f, 0.0f, "%.3f");
    if (ImGui::IsItemActivated() && track.begin_change(i)) std::copy(m, m + kInstFloats, base);
    if (pos_changed) {
        if (!track.changing() && track.begin_change(i)) std::copy(m, m + kInstFloats, base);
        float next[kInstFloats];
        std::copy(m, m + kInstFloats, next);
        next[9] = pos[0];
        next[10] = pos[1];
        next[11] = pos[2];
        track.set_matrix(i, next);
    }
    if (ImGui::IsItemDeactivated()) track.end_change("Mover (campo)");
    tooltip("Posição X Y Z em metros. Arraste ou Ctrl+clique para digitar");
    float deg = yaw_of(m) * 180.0f / kPi;
    ImGui::SetNextItemWidth(-1);
    const bool yaw_changed = ImGui::DragFloat("##yaw", &deg, 0.5f, -360.0f, 360.0f, "giro Y %.2f°");
    if (ImGui::IsItemActivated() && track.begin_change(i)) {
        std::copy(m, m + kInstFloats, base);
        yaw0 = yaw_of(m);
    }
    if (yaw_changed) {
        if (!track.changing() && track.begin_change(i)) {
            std::copy(m, m + kInstFloats, base);
            yaw0 = yaw_of(m);
        }
        float next[kInstFloats];
        if (std::isfinite(deg)) {
            edit::spin(next, base, deg * kPi / 180.0f - yaw0);
            track.set_matrix(i, next);
        }
    }
    if (ImGui::IsItemDeactivated()) track.end_change("Girar (campo)");
    tooltip("Ângulo em Y em graus. Arraste ou Ctrl+clique para digitar");
    const float sx = std::hypot(m[0], m[1], m[2]), sy = std::hypot(m[3], m[4], m[5]), sz = std::hypot(m[6], m[7], m[8]);
    ImGui::TextDisabled("escala %.3f  %.3f  %.3f", static_cast<double>(sx), static_cast<double>(sy), static_cast<double>(sz));
    ImGui::PopID();
    ImGui::EndDisabled();

    if (!hidden) {
        if (ImGui::Button("−90°")) track.turn_selected(-90.0f);
        ImGui::SameLine();
        if (ImGui::Button("−15°")) track.turn_selected(-15.0f);
        ImGui::SameLine();
        if (ImGui::Button("+15°")) track.turn_selected(15.0f);
        ImGui::SameLine();
        if (ImGui::Button("+90°")) track.turn_selected(90.0f);
    }
    if (ImGui::Button("Enquadrar (F)")) track.frame_selected(cam);
    ImGui::SameLine();
    ImGui::BeginDisabled(!hidden && !edit::changed(inst, i));
    if (ImGui::Button("Restaurar (R)")) track.restore_selected();
    ImGui::EndDisabled();
    tooltip("Volta à matriz do arquivo e mostra de novo");
    if (!hidden) {
        ImGui::BeginDisabled(!name.starts_with("e:"));
        if (ImGui::Button("Duplicar (Ctrl+D)")) track.duplicate_selected();
        ImGui::EndDisabled();
        tooltip("Só objetos e: (objects.ens); ornamentos e árvores têm contagem fixa");
        ImGui::SameLine();
        if (ImGui::Button("Apagar (Del)")) track.delete_selected();
        if (ImGui::Button("Pôr no chão (T)")) track.settle_selected();
        tooltip("Baixa (ou sobe) o objeto até o terreno que está sob ele, só na altura");
        if (ImGui::Button("Alinhar ao terreno (Shift+T)")) track.align_selected();
        tooltip("Inclina o objeto junto com o chão sob a base e desce o que ficaria no ar (até 25°). "
                "Árvores ficam em pé");
        ImGui::SameLine();
        if (ImGui::Button("Todos deste tipo")) track.align_type();
        tooltip("Alinha ao terreno todas as instâncias à mostra deste tipo, num passo só (Ctrl+Z desfaz tudo)");
        ImGui::Checkbox("Manter em pé", &track.align_upright);
        tooltip("Para prédios, tendas e placas: não inclina, só desce até o ponto mais baixo do chão sob a base "
                "(nada fica no ar)");
    }

    if (ImGui::CollapsingHeader("Matriz (atual | arquivo)")) {
        if (ImGui::BeginTable("##m", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame)) {
            static const char* const rows[] = {"linha 0", "linha 1", "linha 2", "posição"};
            for (int r = 0; r < 4; ++r) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", rows[r]);
                for (int c = 0; c < 3; ++c) {
                    ImGui::TableNextColumn();
                    const float a = m[r * 3 + c], b = inst.m0[i * kInstFloats + static_cast<std::size_t>(r * 3 + c)];
                    if (a != b) ImGui::TextColored(kYellow, "%.4g", static_cast<double>(a));
                    else ImGui::Text("%.4g", static_cast<double>(a));
                    if (a != b && ImGui::IsItemHovered()) ImGui::SetTooltip("arquivo: %.6g", static_cast<double>(b));
                }
            }
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Tipo e materiais", ImGuiTreeNodeFlags_DefaultOpen)) {
        const TypeInfo& info = track.track().types[inst.type[i]];
        ImGui::Text("%zu malha(s) em objects.bin, %zu instância(s) nesta rota", info.count, ty.group.size());
        if (ty.empty) {
            ImGui::TextColored(kGrey, "Sem malha: não desenha (marcador)");
        } else {
            const glm::vec3 size = ty.hi - ty.lo;
            ImGui::Text("caixa %.2f × %.2f × %.2f m", static_cast<double>(size.x), static_cast<double>(size.y), static_cast<double>(size.z));
        }
        const float thumb = ImGui::GetTextLineHeight() * 3.0f;
        for (std::size_t k = 0; k < ty.parts.size(); ++k) {
            const auto& part = ty.parts[k];
            ImGui::PushID(static_cast<int>(k));
            const GLuint tex = track.textures().for_material(part.material);
            if (tex) {
                ImGui::Image(static_cast<ImTextureID>(tex), ImVec2(thumb, thumb));
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Image(static_cast<ImTextureID>(tex), ImVec2(256, 256));
                    ImGui::EndTooltip();
                }
            } else {
                ImGui::ColorButton("##cor", ImVec4(part.color.r, part.color.g, part.color.b, 1.0f), ImGuiColorEditFlags_NoTooltip,
                                   ImVec2(thumb, thumb));
            }
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextWrapped("%s", part.material.c_str());
            const auto it = track.track().materials.find(part.material);
            if (it != track.track().materials.end()) ImGui::TextDisabled("%s", it->second.c_str());
            else ImGui::TextDisabled("sem textura (cor fixa)");
            ImGui::TextDisabled("%d triângulos", part.count / 3);
            ImGui::EndGroup();
            ImGui::PopID();
        }
    }
}

void EditorUi::status_bar(TrackView* track, float fps, const Rect& area) {
    ImGui::SetNextWindowPos(ImVec2(area.x, area.y));
    ImGui::SetNextWindowSize(ImVec2(area.w, area.h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 3));
    if (ImGui::Begin("##status", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing)) {
        if (track) {
            static const char* const tools[] = {"Navegar", "Mover", "Girar"};
            ImGui::Text("%s", tools[static_cast<int>(track->tool())]);
            ImGui::SameLine(0, 16);
            if (track->unsaved()) ImGui::TextColored(kYellow, "não gravado");
            else ImGui::TextDisabled("gravado");
            ImGui::SameLine(0, 16);
            // a mensagem mais recente: a dos painéis (abrir pista) ou a da pista (gravar, rota)
            const bool mine = !message_.empty() && message_age() < track->status_age();
            const std::string& msg = mine ? message_ : track->status();
            const bool bad = msg.starts_with("NÃO") || msg.starts_with("não ") || msg.starts_with("ATENÇÃO");
            // a mensagem some depois de um tempo (erros ficam mais) para não contradizer o estado atual
            const double age = mine ? message_age() : track->status_age();
            const bool fresh = !msg.empty() && age < (bad ? 30.0 : 8.0);
            if (!fresh) ImGui::TextDisabled("%s", sel_hint(*track).c_str());
            else if (bad) ImGui::TextColored(kRed, "%s", msg.c_str());
            else ImGui::TextUnformatted(msg.c_str());
            if (fresh && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", msg.c_str());
        } else {
            ImGui::TextUnformatted(message_.empty() ? "Cena de teste: abra uma pista em Arquivo > Abrir pista" : message_.c_str());
        }
        if (const int glerr = gl::warned_errors()) {
            ImGui::SameLine(0, 16);
            ImGui::TextColored(kRed, "%d erro(s) de GL (veja o terminal)", glerr);
        }
        char right[160];
        if (track)
            std::snprintf(right, sizeof right, "inst %zu/%u  ·  %.0f m  ·  %.0f fps", track->objects().visible(), track->instances().n,
                          static_cast<double>(track->draw_dist()), static_cast<double>(fps));
        else std::snprintf(right, sizeof right, "%.0f fps", static_cast<double>(fps));
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 16, area.w - ImGui::CalcTextSize(right).x - 12));
        ImGui::TextDisabled("%s", right);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

std::string EditorUi::sel_hint(const TrackView& track) const {
    switch (track.tool()) {
    case TrackView::Tool::Move: return "Mover: arraste o objeto no chão (Shift sobe e desce) ou uma seta do gizmo";
    case TrackView::Tool::Rotate: return "Girar: arraste para os lados ou pelo anel do gizmo; Q/E giram 15°";
    default: return "Clique num objeto para selecionar; arraste para orbitar, botão direito para pan, roda para zoom";
    }
}

void EditorUi::help_window() {
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Atalhos", &show_help_, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    static const char* const keys[][2] = {
        {"Botão esquerdo", "orbitar (Navegar ou fora de objeto); clique seleciona"},
        {"Direito / meio / Shift+esquerdo", "pan"},
        {"Roda", "zoom"},
        {"W A S D", "andar (Shift = ×3)"},
        {"1 / 2 / 3", "Navegar / Mover / Girar"},
        {"Shift ao mover", "sobe e desce"},
        {"Setas do gizmo", "mover só em X, Y ou Z"},
        {"Anel do gizmo", "girar em Y em torno do objeto"},
        {"Q / E (Shift)", "girar −15° / +15° (±90°)"},
        {"Delete", "apagar"},
        {"Ctrl+D", "duplicar (objetos e:)"},
        {"R", "restaurar do arquivo"},
        {"T", "pôr no chão (altura do terreno sob o objeto)"},
        {"Shift+T", "alinhar ao terreno (inclina com o chão)"},
        {"F", "enquadrar seleção ou rota"},
        {"Esc", "tirar seleção; fechar janela de confirmação"},
        {"Ctrl+Q", "sair (pergunta se há edições não gravadas)"},
        {"Ctrl+Z / Ctrl+Y", "desfazer / refazer"},
        {"Ctrl+S", "gravar edits.json"},
        {"Tab / Shift+Tab", "próxima / anterior rota"},
        {"F1 F2 F3 F4", "terreno, objetos, árvores, terreno distante"},
        {"G / I", "portões / linha da IA"},
        {"C / Shift+C", "câmeras do replay / ver pela próxima"},
        {"L / Shift+L", "largada (vagas do carro) / ver da próxima vaga"},
        {"[ / ]", "distância de desenho"},
        {"F5", "testar no jogo (porta a pista e abre o jogo nela)"},
        {"F10 / F11", "painéis / esta janela"},
    };
    if (ImGui::BeginTable("##atalhos", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        for (const auto& k : keys) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(kGreen, "%s", k[0]);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(k[1]);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void EditorUi::modals(TrackView* track) {
    launch_modal(track);
    if (pending_ != Pending::None && !ImGui::IsPopupOpen("Edições não gravadas")) ImGui::OpenPopup("Edições não gravadas");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Edições não gravadas", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        return;
    const bool quit = pending_ == Pending::Quit;
    ImGui::TextUnformatted(quit ? "Há edições que não estão no edits.json." : "Abrir outra pista descarta as edições desta.");
    if (track) ImGui::TextDisabled("%s", track->out_path().c_str());
    ImGui::Spacing();
    auto done = [&](bool go) {
        if (go) {
            if (quit) quit_request = true;
            else open_request = pending_dir_;
        }
        pending_ = Pending::None;
        ImGui::CloseCurrentPopup();
    };
    if (ImGui::Button(quit ? "Gravar e sair" : "Gravar e abrir")) {
        if (track && track->save()) done(true);
        else done(false);  // a falha fica na barra de status; nada se perde
    }
    ImGui::SameLine();
    if (ImGui::Button(quit ? "Sair sem gravar" : "Abrir sem gravar")) done(true);
    ImGui::SameLine();
    if (ImGui::Button("Cancelar") || ImGui::IsKeyPressed(ImGuiKey_Escape)) done(false);
    ImGui::EndPopup();
}

// ---- Testar no jogo (F5)

namespace {

// Só o Ring tem o porte para o jogo (scripts/research/ring_deploy.py).
constexpr const char* kPortedTrack = "synthetic__dr2hook_ring";
const char* const kModeArg[] = {"bot", "drive", "freecam"};

}  // namespace

void EditorUi::open_launch(TrackView* track) {
    if (!track) {
        show_message("Testar no jogo: abra uma pista antes");
        return;
    }
    // com um teste rodando, mostra o progresso; senão, as opções de um teste novo
    if (!launch_.child.running()) launch_.started = false;
    launch_.open = true;
}

void EditorUi::poll_launch() {
    const bool was = launch_.child.running();
    launch_.child.poll(launch_.progress);
    if (!was || launch_.child.running()) return;
    launch_.finished_at = launch_.child.seconds();
    const Progress& p = launch_.progress;
    show_message(p.state == Progress::State::Done ? "Testar no jogo: " + p.result : "Testar no jogo falhou: " + p.result);
}

void EditorUi::play_button(TrackView* track) {
    const float h = ImGui::GetFrameHeight();
    const float w = std::round(h * 1.8f);
    const float mid = (ImGui::GetWindowWidth() - w) * 0.5f;
    if (ImGui::GetCursorPosX() < mid) ImGui::SetCursorPosX(mid);
    const bool busy = launch_.child.running();
    const ImVec4 base = busy ? ImVec4(0.60f, 0.42f, 0.10f, 1.0f) : ImVec4(0.16f, 0.52f, 0.24f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(base.x * 1.25f, base.y * 1.25f, base.z * 1.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(base.x * 0.8f, base.y * 0.8f, base.z * 0.8f, 1.0f));
    ImGui::BeginDisabled(!track);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    if (ImGui::Button("##testar", ImVec2(w, h))) open_launch(track);
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(p0.x + w * 0.5f, p0.y + h * 0.5f);
    const float r = h * 0.28f;
    const ImU32 white = ImGui::GetColorU32(track ? ImVec4(1, 1, 1, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1));
    if (busy) {  // rodando: a barra enche dentro do botão
        const float f = launch_.progress.fraction;
        dl->AddRectFilled(ImVec2(p0.x + 3, p0.y + h - 5), ImVec2(p0.x + 3 + (w - 6) * f, p0.y + h - 2), white);
        dl->AddRectFilled(ImVec2(c.x - r * 0.75f, c.y - r * 0.85f), ImVec2(c.x - r * 0.2f, c.y + r * 0.55f), white);
        dl->AddRectFilled(ImVec2(c.x + r * 0.2f, c.y - r * 0.85f), ImVec2(c.x + r * 0.75f, c.y + r * 0.55f), white);
    } else {
        dl->AddTriangleFilled(ImVec2(c.x - r * 0.8f, c.y - r), ImVec2(c.x - r * 0.8f, c.y + r), ImVec2(c.x + r, c.y), white);
    }
    if (busy) {
        tooltip(("Testando no jogo: " + launch_.progress.label + " (" +
                 std::to_string(static_cast<int>(launch_.progress.fraction * 100.0f)) + "%). Clique ou F5 para ver")
                    .c_str());
    } else {
        tooltip("Testar no jogo (F5): porta a pista para a overlay e abre o jogo nela");
    }
}

void EditorUi::start_launch(TrackView& track) {
    Launch& L = launch_;
    L.progress = Progress{};
    L.finished_at = -1.0;
    L.started = true;
    L.follow_log = true;
    const std::string repo = find_repo(track.track().dir);
    if (repo.empty()) {
        L.progress.feed("@fail não achei scripts/research/ring_deploy.py (rode o viewer da raiz do repositório)");
        return;
    }
    // as edições do jeito que estão agora, gravadas ou não (o edits.json do usuário não muda)
    const std::string edits = repo + "/build/re/ring_deploy/viewer.edits.json";
    try {
        edit::write_text(edits, track.current_edits());
    } catch (const std::exception& e) {
        L.progress.feed(std::string("@fail não gravei as edições do teste: ") + e.what());
        return;
    }
    std::vector<std::string> argv = {"python3", "-u", repo + "/scripts/research/ring_deploy.py", "--mode", kModeArg[L.mode],
                                     "--edits", edits};
    if (L.quick) argv.push_back("--quick");
    std::string cmd;
    for (std::size_t k = 2; k < argv.size(); ++k) cmd += (k > 2 ? " " : "") + argv[k];
    L.progress.log.push_back("$ " + cmd);
    std::string err;
    if (!L.child.start(argv, err)) L.progress.feed("@fail " + err);
}

void EditorUi::launch_modal(TrackView* track) {
    Launch& L = launch_;
    const char* const title = "Testar no jogo";
    if (L.open) {
        if (!ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
        L.open = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        return;
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    const bool esc = ImGui::IsKeyPressed(ImGuiKey_Escape, false);

    if (!L.started) {
        // ---- opções
        const bool ring = track && track->track().id == kPortedTrack;
        const std::string repo = track ? find_repo(track->track().dir) : std::string();
        ImGui::TextUnformatted("Pista:");
        ImGui::SameLine();
        ImGui::TextColored(kGreen, "%s", track ? track->track().id.c_str() : "(nenhuma)");
        ImGui::SameLine();
        ImGui::TextDisabled("route_0");
        ImGui::Spacing();
        ImGui::Checkbox("Inicialização rápida", &L.quick);
        tooltip("Pula a tela de carregamento: a foto aérea renderizada e o traçado (fica a da última vez). Economiza "
                "uns 20 s. No jogo, tela preta com o log ao vivo e sem som do boot até a largada. As outras etapas só refazem o que mudou");
        ImGui::Spacing();
        ImGui::TextUnformatted("Quem dirige");
        static const char* const modes[] = {"Bot dirige", "Eu dirijo", "Câmera livre"};
        static const char* const tips[] = {
            "O AutoStage abre a pista pelo benchmark do jogo: o carro anda sozinho e a câmera segue o carro",
            "Ainda não: o AutoStage usa o benchmark, que não passa o controle ao jogador. Falta achar como "
            "(engenharia reversa do benchmark)",
            "O bot dirige e a câmera fica solta (o teste manda F9 na largada; no jogo, F9 alterna)"};
        for (int k = 0; k < 3; ++k) {
            ImGui::BeginDisabled(k == 1);
            if (ImGui::RadioButton(modes[k], L.mode == k)) L.mode = k;
            if (k == 1) {
                ImGui::SameLine();
                ImGui::TextDisabled("(ainda não)");
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tips[k]);
        }
        ImGui::Separator();
        if (track && track->unsaved()) ImGui::TextColored(kYellow, "As edições não gravadas entram no teste (o edits.json fica como está).");
        if (track && track->route().name != "route_0")
            ImGui::TextColored(kYellow, "O teste leva só a route_0; as edições da %s ficam de fora.", track->route().name.c_str());
        if (track && !ring) ImGui::TextColored(kRed, "Só o DR2 Hook Ring (%s) tem o porte para o jogo por enquanto.", kPortedTrack);
        if (ring && repo.empty()) ImGui::TextColored(kRed, "Não achei scripts/research/ring_deploy.py: rode o viewer da raiz do repositório.");
        ImGui::TextDisabled("Fecha o jogo se estiver aberto e o abre direto na pista.");
        ImGui::Spacing();
        const bool can = ring && !repo.empty();
        ImGui::BeginDisabled(!can);
        const bool go = ImGui::Button("Iniciar (Enter)") || (can && enter);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancelar (Esc)") || esc) {
            ImGui::CloseCurrentPopup();
        } else if (go && track) {
            start_launch(*track);
        }
        ImGui::TextDisabled("↑↓ quem dirige · R rápida");
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) L.quick = !L.quick;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) L.mode = L.mode == 2 ? 0 : 2;  // pula o "Eu dirijo"
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) L.mode = L.mode == 0 ? 2 : 0;
        ImGui::EndPopup();
        return;
    }

    // ---- progresso
    const Progress& p = L.progress;
    const bool busy = L.child.running();
    const float width = std::max(560.0f, ImGui::GetMainViewport()->Size.x * 0.4f);
    if (p.steps > 0) ImGui::Text("Etapa %d de %d: %s", p.step, p.steps, p.label.c_str());
    else ImGui::TextUnformatted(busy ? "Começando…" : "");
    const double secs = busy ? L.child.seconds() : std::max(0.0, L.finished_at);
    char overlay[64];
    std::snprintf(overlay, sizeof overlay, "%d%%  ·  %.0f s", static_cast<int>(p.fraction * 100.0f + 0.5f), secs);
    const ImVec4 bar = p.state == Progress::State::Failed ? ImVec4(0.70f, 0.25f, 0.22f, 1.0f) : ImVec4(0.20f, 0.60f, 0.28f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar);
    ImGui::ProgressBar(p.fraction, ImVec2(width, 0), overlay);
    ImGui::PopStyleColor();
    const float log_h = ImGui::GetTextLineHeightWithSpacing() * 14;
    if (ImGui::BeginChild("##log", ImVec2(width, log_h), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const std::string& line : p.log) {
            const bool err = line.rfind("erro:", 0) == 0;
            const bool ok = line.rfind("pronto:", 0) == 0;
            const bool head = line.rfind("– ", 0) == 0;
            if (err || ok || head) ImGui::PushStyleColor(ImGuiCol_Text, err ? kRed : ok ? kGreen : kYellow);
            ImGui::TextUnformatted(line.c_str());
            if (err || ok || head) ImGui::PopStyleColor();
        }
        // segue o fim enquanto ninguém rolou para cima
        if (L.follow_log) ImGui::SetScrollHereY(1.0f);
        L.follow_log = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f;
    }
    ImGui::EndChild();
    if (p.state == Progress::State::Done) ImGui::TextColored(kGreen, "Pronto: %s", p.result.c_str());
    else if (p.state == Progress::State::Failed) ImGui::TextColored(kRed, "Falhou: %s", p.result.c_str());
    else ImGui::TextDisabled("O editor segue usável: Esconder deixa o teste rodando (o botão verde mostra o andamento).");
    ImGui::Spacing();
    if (busy) {
        if (ImGui::Button("Esconder (Esc)") || esc) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button("Cancelar o teste")) {
            L.child.terminate();
            L.progress.feed("@fail cancelado (o jogo, se já abriu, continua aberto)");
        }
    } else {
        if (ImGui::Button("Fechar (Enter)") || enter || esc) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button("Testar de novo (F5)") || ImGui::IsKeyPressed(ImGuiKey_F5, false)) L.started = false;
    }
    ImGui::EndPopup();
}

int EditorUi::gizmo_hit(float x, float y) const {
    if (!gizmo_.visible || !vp_.contains(x, y)) return -1;
    const glm::vec2 p(x, y);
    int hit = -1;
    float best = 9.0f;
    if (!gizmo_.ring.empty()) {
        for (std::size_t k = 0; k < gizmo_.ring.size(); ++k) {
            const float d = seg_dist(p, gizmo_.ring[k], gizmo_.ring[(k + 1) % gizmo_.ring.size()]);
            if (d < best) best = d, hit = 3;
        }
        return hit;
    }
    for (int a = 0; a < 3; ++a) {
        if (glm::length(gizmo_.tip[a] - gizmo_.origin) <= 6.0f) continue;  // eixo de ponta para a câmera
        // o primeiro quarto do eixo fica para o arraste livre no chão (clicar no centro do objeto)
        const float d = seg_dist(p, gizmo_.origin + 0.25f * (gizmo_.tip[a] - gizmo_.origin), gizmo_.tip[a]);
        if (d < best) best = d, hit = a;
    }
    return hit;
}

bool EditorUi::gizmo_press(TrackView& track, const render::OrbitCamera& cam, float x, float y) {
    // acerto pela posição do clique (o realce do quadro anterior pode ser de outra posição)
    gizmo_.hover = gizmo_hit(x, y);
    if (gizmo_.hover < 0) return false;
    const int sel = track.selected();
    if (sel < 0 || !track.begin_change(static_cast<std::uint32_t>(sel))) return false;
    gizmo_.axis = gizmo_.hover;
    gizmo_.press = {x, y};
    const float* m = track.instances().matrix(static_cast<std::size_t>(sel));
    std::copy(m, m + kInstFloats, gizmo_.base);
    if (gizmo_.axis == 3) {
        glm::vec3 p;
        const auto ray = render::mouse_ray(cam, x - vp_.x, y - vp_.y, vp_.w, vp_.h);
        gizmo_ang0_ = render::ground(ray, m[10], p) ? std::atan2(-(p.z - m[11]), p.x - m[9]) : 0.0f;
    }
    return true;
}

void EditorUi::gizmo_drag(TrackView& track, const render::OrbitCamera& cam, float x, float y) {
    if (gizmo_.axis < 0) return;
    float next[kInstFloats];
    std::copy(gizmo_.base, gizmo_.base + kInstFloats, next);
    if (gizmo_.axis < 3) {
        const glm::vec2 d = gizmo_.tip[gizmo_.axis] - gizmo_.origin;
        const float len = std::max(4.0f, glm::length(d));
        const float t = glm::dot(glm::vec2(x, y) - gizmo_.press, d / len) / len * gizmo_.world_len;
        next[9 + gizmo_.axis] = snapped(gizmo_.base[9 + gizmo_.axis] + t, track.snap_move);
    } else {
        glm::vec3 p;
        const auto ray = render::mouse_ray(cam, x - vp_.x, y - vp_.y, vp_.w, vp_.h);
        if (!render::ground(ray, gizmo_.base[10], p)) return;
        float th = std::atan2(-(p.z - gizmo_.base[11]), p.x - gizmo_.base[9]) - gizmo_ang0_;
        if (track.snap_turn > 0) th = snapped(th, track.snap_turn * kPi / 180.0f);
        edit::spin(next, gizmo_.base, th);
    }
    track.set_matrix(static_cast<std::uint32_t>(track.selected()), next);
}

void EditorUi::gizmo_release(TrackView& track) {
    if (gizmo_.axis < 0) return;
    static const char* const labels[] = {"Mover X", "Mover Y", "Mover Z", "Girar"};
    track.end_change(labels[gizmo_.axis]);
    gizmo_.axis = -1;
}

void EditorUi::render(TrackView* track, const render::OrbitCamera& cam, const glm::mat4& view_proj, const Rect& vp) {
    gizmo_.visible = false;
    const int sel = track ? track->selected() : -1;
    if (track && sel >= 0) {
        // caixa local do tipo, transformada, em todas as ferramentas: a seleção se vê mesmo de longe
        const auto i = static_cast<std::size_t>(sel);
        const auto& ty = track->objects().types()[track->instances().type[i]];
        const float* m = track->instances().matrix(i);
        const glm::vec3 lo = ty.empty ? glm::vec3(-1.0f) : ty.lo, hi = ty.empty ? glm::vec3(1.0f) : ty.hi;
        glm::vec2 corner[8];
        bool all = true;
        for (int k = 0; k < 8; ++k) {
            const glm::vec3 p((k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z);
            const glm::vec3 w = p.x * glm::vec3(m[0], m[1], m[2]) + p.y * glm::vec3(m[3], m[4], m[5]) + p.z * glm::vec3(m[6], m[7], m[8]) +
                                glm::vec3(m[9], m[10], m[11]);
            all = project(view_proj, vp, w, corner[k]) && all;
        }
        if (all) {
            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            dl->PushClipRect(ImVec2(vp.x, vp.y), ImVec2(vp.x + vp.w, vp.y + vp.h), true);
            const ImU32 col = track->instances().hidden[i] ? IM_COL32(160, 160, 160, 200) : IM_COL32(255, 200, 60, 230);
            static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& e : edges)
                dl->AddLine(ImVec2(corner[e[0]].x, corner[e[0]].y), ImVec2(corner[e[1]].x, corner[e[1]].y), col, 1.5f);
            dl->PopClipRect();
        }
    }
    if (track && sel >= 0 && track->tool() != TrackView::Tool::Navigate && !track->instances().hidden[static_cast<std::size_t>(sel)]) {
        const float* m = track->instances().matrix(static_cast<std::size_t>(sel));
        const glm::vec3 c(m[9], m[10], m[11]);
        // tamanho constante na tela: proporcional à distância do olho
        gizmo_.world_len = std::max(0.5f, glm::length(cam.eye() - c) * 0.14f);
        glm::vec2 o;
        if (project(view_proj, vp, c, o)) {
            gizmo_.visible = true;
            gizmo_.origin = o;
            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            dl->PushClipRect(ImVec2(vp.x, vp.y), ImVec2(vp.x + vp.w, vp.y + vp.h), true);
            const ImVec2 mouse = ImGui::GetMousePos();
            gizmo_.ring.clear();
            if (track->tool() == TrackView::Tool::Move) {
                static const ImU32 cols[] = {IM_COL32(230, 70, 60, 255), IM_COL32(90, 200, 80, 255), IM_COL32(70, 130, 240, 255)};
                for (int a = 0; a < 3; ++a) {
                    glm::vec3 tip = c;
                    tip[a] += gizmo_.world_len;
                    if (!project(view_proj, vp, tip, gizmo_.tip[a])) gizmo_.tip[a] = o;
                }
                if (gizmo_.axis < 0) gizmo_.hover = gizmo_hit(mouse.x, mouse.y);
                for (int a = 0; a < 3; ++a) {
                    const bool hot = gizmo_.axis == a || (gizmo_.axis < 0 && gizmo_.hover == a);
                    const ImU32 col = hot ? IM_COL32(255, 230, 90, 255) : cols[a];
                    const ImVec2 p0(o.x, o.y), p1(gizmo_.tip[a].x, gizmo_.tip[a].y);
                    dl->AddLine(p0, p1, col, hot ? 4.0f : 3.0f);
                    dl->AddCircleFilled(p1, hot ? 7.0f : 5.5f, col);
                    static const char* const names[] = {"X", "Y", "Z"};
                    dl->AddText(ImVec2(p1.x + 7, p1.y - 7), col, names[a]);
                }
            } else {
                // anel no plano horizontal do objeto
                ImVec2 pts[48];
                int n = 0;
                for (int k = 0; k < 48; ++k) {
                    const float a = 2.0f * kPi * static_cast<float>(k) / 48.0f;
                    glm::vec2 s;
                    if (!project(view_proj, vp, c + gizmo_.world_len * glm::vec3(std::cos(a), 0.0f, std::sin(a)), s)) continue;
                    pts[n++] = ImVec2(s.x, s.y);
                    gizmo_.ring.push_back(s);
                }
                if (gizmo_.axis < 0) gizmo_.hover = gizmo_hit(mouse.x, mouse.y);
                const bool hot = gizmo_.axis == 3 || (gizmo_.axis < 0 && gizmo_.hover == 3);
                if (n > 2) dl->AddPolyline(pts, n, hot ? IM_COL32(255, 230, 90, 255) : IM_COL32(90, 200, 80, 255), ImDrawFlags_Closed, hot ? 4.0f : 2.5f);
            }
            dl->AddCircleFilled(ImVec2(o.x, o.y), 4.0f, IM_COL32(255, 255, 255, 230));
            dl->PopClipRect();
        }
    }
    if (!gizmo_.visible) {
        gizmo_.hover = -1;
        gizmo_.ring.clear();
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

}  // namespace dr2::app
