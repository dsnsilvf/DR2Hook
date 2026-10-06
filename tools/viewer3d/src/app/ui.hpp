// Painéis do editor (Dear ImGui): menu, barra de ferramentas, Cena (árvore), Inspector, histórico,
// barra de status, gizmo de eixos e as janelas de confirmação. A pista e a câmera ficam fora; o
// painel só chama a API do TrackView.
#pragma once

#include "app/track_view.hpp"
#include "render/camera.hpp"

#include <glm/glm.hpp>

#include <chrono>
#include <string>
#include <vector>

#include <SDL3/SDL_scancode.h>

struct SDL_Window;
union SDL_Event;

namespace dr2::app {

// Área do viewport 3D dentro da janela, em pontos da janela (as coordenadas dos eventos do mouse).
struct Rect {
    float x = 0, y = 0, w = 1, h = 1;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

class EditorUi {
public:
    // Cria o contexto do ImGui para a janela e o contexto GL atuais.
    EditorUi(SDL_Window* window, void* gl_context);
    ~EditorUi();
    EditorUi(const EditorUi&) = delete;
    EditorUi& operator=(const EditorUi&) = delete;

    // Passa o evento ao ImGui. true se o ImGui quer o mouse ou o teclado (o app não deve usar o evento).
    bool event(const SDL_Event& e);
    bool wants_keyboard() const;

    // Começa o quadro e desenha os painéis; devolve a área livre para o 3D. `track` pode ser nulo
    // (cena de teste).
    Rect frame(TrackView* track, render::OrbitCamera& cam, float fps);
    // Desenha o gizmo da seleção (depois do 3D, com a view_proj do quadro) e manda tudo ao GL.
    void render(TrackView* track, const render::OrbitCamera& cam, const glm::mat4& view_proj, const Rect& vp);

    // Gizmo: botão esquerdo no viewport. true se pegou um eixo (o app não começa outro arraste).
    bool gizmo_press(TrackView& track, const render::OrbitCamera& cam, float x, float y);
    void gizmo_drag(TrackView& track, const render::OrbitCamera& cam, float x, float y);
    void gizmo_release(TrackView& track);

    // Pedidos do usuário que o main executa (abrir outra pista, sair).
    std::string open_request;  // pasta da pista a abrir
    bool quit_request = false;  // confirmou sair (gravando ou não)
    // Pede confirmação de saída se houver edições não gravadas; true = pode sair já.
    bool ask_quit(TrackView* track);
    // Pede para abrir outra pista (com confirmação se houver edições não gravadas).
    void ask_open(TrackView* track, const std::string& dir);
    void show_message(std::string msg) {
        message_ = std::move(msg);
        message_time_ = std::chrono::steady_clock::now();
    }
    bool panels = true;  // barra de ferramentas, Cena e Inspector (F10; o menu e o status ficam)
    void toggle_help() { show_help_ = !show_help_; }

private:
    void menu_bar(TrackView* track, render::OrbitCamera& cam);
    void toolbar(TrackView& track);
    void scene_tree(TrackView& track, render::OrbitCamera& cam, const Rect& area);
    void inspector(TrackView& track, render::OrbitCamera& cam, const Rect& area);
    void status_bar(TrackView* track, float fps, const Rect& area);
    void modals(TrackView* track);
    void help_window();
    std::string sel_hint(const TrackView& track) const;
    void scan_tracks();

    struct Gizmo {
        int axis = -1;           // 0 x, 1 y, 2 z sendo arrastado; -1 nenhum
        int hover = -1;
        glm::vec2 origin{};      // centro na tela
        glm::vec2 tip[3]{};      // ponta de cada eixo na tela
        float world_len = 1.0f;  // comprimento do eixo no mundo
        bool visible = false;
        glm::vec2 press{};
        float base[kInstFloats] = {};
        std::vector<glm::vec2> ring;  // anel do girar na tela (vazio no mover)
    } gizmo_;
    int gizmo_hit(float x, float y) const;  // eixo (0..2), anel (3) ou -1 sob o ponto

    Rect vp_;
    float left_w_ = 300.0f, right_w_ = 340.0f;
    char filter_[64] = {};
    bool show_help_ = false, show_history_ = true;
    enum class Pending { None, Quit, Open } pending_ = Pending::None;
    std::string pending_dir_;
    float gizmo_ang0_ = 0.0f;
    bool scroll_to_sel_ = false;
    int last_sel_ = -1;
    std::string message_;
    std::chrono::steady_clock::time_point message_time_{};
    double message_age() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - message_time_).count(); }
    std::size_t last_hist_size_ = 0;
    bool imgui_keys_[SDL_SCANCODE_COUNT] = {};  // teclas cujo apertar foi ao ImGui (a soltura também vai)
    std::vector<std::string> tracks_;  // pastas em build/uiview/tracks e depois em examples/tracks
    bool tracks_scanned_ = false;
};

}  // namespace dr2::app
