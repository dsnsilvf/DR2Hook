// Painéis do editor (Dear ImGui, ramo docking): menu, barra de ferramentas com ícones, painéis encaixáveis
// (Cena, Histórico e Inspector) em volta do 3D, barra de status, gizmo de eixos e as janelas de confirmação.
// O 3D fica no nó central do encaixe; o layout dos painéis e as opções ficam no imgui.ini da pasta de
// preferências do usuário. A pista e a câmera ficam fora; os painéis só chamam a API do TrackView.
#pragma once

#include "app/launch.hpp"
#include "app/track_view.hpp"
#include "render/camera.hpp"

#include <glm/glm.hpp>

#include <chrono>
#include <string>
#include <vector>

#include <SDL3/SDL_scancode.h>

struct SDL_Window;
union SDL_Event;
struct ImFont;
typedef unsigned int ImGuiID;

namespace dr2::app {

// Área do viewport 3D dentro da janela, em pontos da janela (as coordenadas dos eventos do mouse).
struct Rect {
    float x = 0, y = 0, w = 1, h = 1;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

class EditorUi {
public:
    // Cria o contexto do ImGui para a janela e o contexto GL atuais. `persist`: lê e grava o layout e as opções
    // no imgui.ini da pasta de preferências (o --frames passa false: capturas sempre com o layout padrão).
    EditorUi(SDL_Window* window, void* gl_context, bool persist);
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

    // Gizmo (ImGuizmo, desenhado e arrastado no render): com o mouse sobre ele, o ImGui fica com o clique e o app
    // não começa outro arraste. `viewport_drag`: o app já está arrastando (orbitar, pan, objeto), e um clique nesse
    // estado não vira arraste do gizmo. X troca os eixos entre os do mundo e os do objeto.
    bool viewport_drag = false;
    void toggle_gizmo_axes() { gizmo_local_ = !gizmo_local_; }

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
    bool panels = true;  // barra de ferramentas e painéis (F10; o menu e o status ficam)
    void toggle_help() { show_help_ = !show_help_; }
    void open_about() { about_request_ = true; }

    // Jogo (grupo do meio da barra de ferramentas e menu Jogo).
    // Testar no jogo (F5): abre a janela de opções ou, com um teste rodando, a do progresso.
    void open_launch(TrackView* track);
    // Pausar ou continuar a especial aberta (F6), pelo canal de comandos do jogo.
    void toggle_pause(TrackView* track);
    // Parar (Shift+F5): cancela o teste em andamento e fecha o jogo.
    void stop_game(TrackView* track);
    // Título da janela: "pista · arquivo * — DR2Hook Editor de Pistas" (o * enquanto houver edições não gravadas).
    static std::string window_title(const TrackView* track);
    // Capturas (--ui): abre a janela "exibicao", "atalhos" ou "sobre" no primeiro quadro. false se o nome não existe.
    bool open_on_start(const std::string& what);

private:
    friend struct UiSettings;  // imgui.ini: painéis à mostra, versão do layout e opções

    void menu_bar(TrackView* track, render::OrbitCamera& cam);
    void toolbar(TrackView& track);
    void play_group(TrackView& track);
    void display_popup(TrackView& track);
    Rect dock_space(bool visible);
    void default_layout(ImGuiID dock, float w, float h);
    void scene_tree(TrackView& track, render::OrbitCamera& cam);
    void history(TrackView& track);
    void inspector(TrackView& track, render::OrbitCamera& cam);
    void replay_tree(TrackView& track, render::OrbitCamera& cam);
    void grids_tree(TrackView& track, render::OrbitCamera& cam);
    void status_bar(TrackView* track, float fps);
    void modals(TrackView* track);
    void help_window();
    void about_window();
    void poll_launch();
    void launch_modal(TrackView* track);
    void start_launch(TrackView& track);
    void poll_control();
    void run_control(TrackView* track, std::vector<std::string> args, std::string what);
    std::string sel_hint(const TrackView& track) const;
    void scan_tracks();
    std::string settings_text() const;  // as linhas da seção do editor no imgui.ini
    void watch_settings();

    // Botão só de ícone da barra (fonte de ícones maior). `on`: ferramenta ou opção ligada (fundo na cor de
    // destaque); `tint`: cor do ícone (0 = a do texto).
    bool tool_button(const char* id, const char* icon, bool on = false, bool enabled = true, unsigned tint = 0);
    // Dica do último item: título em seminegrito, atalho em cinza ao lado e a explicação embaixo.
    void tip(const char* title, const char* keys = nullptr, const char* body = nullptr) const;
    void section(const char* label);  // título de seção (seminegrito com linha)
    // Linha de camada na Cena: ícone e nome (nó da árvore), contagem em cinza e o olho que mostra ou esconde.
    // Devolve se o nó está aberto (com ImGuiTreeNodeFlags_Leaf não há TreePop).
    bool layer_row(const char* id, const char* icon, const char* name, const std::string& count, bool* shown,
                   const char* keys, int flags = 0);

    // Gizmo da seleção: arraste em curso (do ImGuizmo) e a mudança aberta na pista para o histórico.
    void gizmo(TrackView& track, const render::OrbitCamera& cam, const Rect& vp);
    void gizmo_style();
    bool gizmo_local_ = false;  // eixos do objeto em vez dos do mundo (X)
    bool gizmo_drag_ = false;   // o ImGuizmo está arrastando
    bool gizmo_open_ = false;   // begin_change aceito: o arraste vai à pista e ao histórico
    int gizmo_type_ = 0;        // ImGuizmo::MOVETYPE do arraste (o rótulo no histórico)
    bool gizmo_ground_ = false; // grudar no chão: a altura segue o terreno com a folga do começo
    float gizmo_ground_off_ = 0.0f;
    float gizmo_y0_ = 0.0f;     // altura no começo (de onde a busca do terreno parte)

    Rect vp_;
    float scale_ = 1.0f;
    ImFont* font_text_ = nullptr;  // Inter com os ícones
    ImFont* font_head_ = nullptr;  // Inter seminegrito com os ícones (títulos)
    ImFont* font_tool_ = nullptr;  // só ícones, maiores (barra de ferramentas)
    char filter_[64] = {};
    bool show_help_ = false;
    bool about_request_ = false;  // abrir o Sobre no próximo quadro (fora do menu, para o id do popup ser o mesmo)
    unsigned about_tex_ = 0;      // textura da imagem do Sobre (criada na primeira vez que abre)
    int about_w_ = 0, about_h_ = 0;
    // Painéis à mostra (o X na aba fecha; o menu Exibir abre de novo no mesmo lugar).
    bool show_scene_ = true, show_history_ = true, show_inspector_ = true;
    int layout_version_ = 0;     // do imgui.ini; abaixo de kLayoutVersion, refaz o layout padrão
    bool reset_layout_ = false;  // Exibir > Restaurar layout padrão
    std::string ini_path_;
    std::string settings_seen_;  // estado que vai ao imgui.ini, para gravar só quando muda
    // Encaixe e grudar no chão valem para qualquer pista aberta (o quadro passa ao TrackView). O passo de
    // cada um fica guardado com o encaixe desligado.
    bool snap_ = false, follow_ground_ = false;
    float snap_move_step_ = 0.5f, snap_turn_step_ = 15.0f;
    bool display_open_ = false;     // janela da Exibição aberta no quadro anterior (o botão fica aceso)
    bool display_request_ = false;  // abrir a Exibição no próximo quadro (--ui exibicao)
    // O que o Inspector mostra: a última coisa selecionada (objeto, vaga de largada ou câmera do replay), ou a
    // pista quando ela sai da seleção. focus_obj_/slot_/cam_: as seleções do quadro anterior.
    enum class Focus { Track, Object, Slot, Camera } focus_ = Focus::Track;
    int focus_obj_ = -1, focus_slot_ = -1, focus_cam_ = -1;
    void update_focus(const TrackView& track);
    void slot_inspector(TrackView& track, render::OrbitCamera& cam);
    void camera_inspector(TrackView& track, render::OrbitCamera& cam);
    void track_inspector(TrackView& track);
    enum class Pending { None, Quit, Open } pending_ = Pending::None;
    std::string pending_dir_;
    bool scroll_to_sel_ = false;
    int last_sel_ = -1;
    int last_replay_sel_ = -1;
    int last_slot_sel_ = -1, last_slot_open_ = -1;  // vaga de largada em destaque: abre a lista e a grade dela
    std::string message_;
    std::chrono::steady_clock::time_point message_time_{};
    double message_age() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - message_time_).count(); }
    std::size_t last_hist_size_ = 0;
    bool imgui_keys_[SDL_SCANCODE_COUNT] = {};  // teclas cujo apertar foi ao ImGui (a soltura também vai)
    std::vector<std::string> tracks_;  // pastas em build/uiview/tracks e depois em examples/tracks
    bool tracks_scanned_ = false;

    // Testar no jogo: opções (ficam para a próxima vez), o filho e o progresso dele.
    struct Launch {
        bool open = false;     // pedir para abrir a janela no próximo quadro
        bool quick = true;     // pula a foto aérea da tela de carregamento
        int mode = 0;          // 0 bot dirige, 1 eu dirijo (ainda não), 2 câmera livre
        bool started = false;  // já houve um teste (a janela mostra o progresso em vez das opções)
        bool follow_log = true;
        ChildProcess child;
        Progress progress;
        double finished_at = -1.0;  // segundos do filho quando acabou (para o "levou X s")
    } launch_;

    // Pausar e Parar: o ring_deploy.py --cmd/--close num filho à parte (o script sabe onde fica o jogo). Um por
    // vez; o próximo espera na fila (o Parar cancela o que estiver rodando e entra em seguida).
    struct Control {
        ChildProcess child;
        Progress progress;
        std::string what;  // "Pausar", "Continuar", "Parar": começo da mensagem do resultado
        std::vector<std::string> next_args;
        std::string next_what, repo;
    } control_;
    GameWatch game_;
};

}  // namespace dr2::app
