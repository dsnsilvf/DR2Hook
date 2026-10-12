// Painéis do editor (Dear ImGui, ramo docking): menu, barra de ferramentas com ícones, painéis encaixáveis
// (Cena, Histórico e Inspector) em volta do 3D, barra de status, gizmo de eixos e as janelas de confirmação.
// O 3D fica no nó central do encaixe; o layout dos painéis e as opções ficam no imgui.ini da pasta de
// preferências do usuário. A pista e a câmera ficam fora; os painéis só chamam a API do TrackView.
#pragma once

#include "app/launch.hpp"
#include "app/project.hpp"
#include "app/track_info.hpp"
#include "app/track_view.hpp"
#include "core/car_catalog.hpp"
#include "core/car_tuning.hpp"
#include "render/camera.hpp"

#include <glm/glm.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
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

class Pads;

// Idioma da interface: 0 = o do sistema (português se o sistema está em português, inglês em qualquer outro),
// 1 = português, 2 = inglês. Aplica na hora e passa aos scripts filhos em DR2_LANG.
void apply_language(int pref);
// O --lang fixa o idioma: o Idioma= do imgui.ini fica guardado, mas não manda nesta execução.
void lock_language(int pref);

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
    void render(TrackView* track, render::OrbitCamera& cam, const glm::mat4& view_proj, const Rect& vp);

    // Gizmo (ImGuizmo, desenhado e arrastado no render): com o mouse sobre ele, o ImGui fica com o clique e o app
    // não começa outro arraste. `viewport_drag`: o app já está arrastando (orbitar, pan, objeto), e um clique nesse
    // estado não vira arraste do gizmo. X troca os eixos entre os do mundo e os do objeto.
    bool viewport_drag = false;
    void toggle_gizmo_axes() { gizmo_local_ = !gizmo_local_; }
    // Retângulo da seleção por arraste (coordenadas da janela), desenhado por cima do 3D.
    void set_marquee(bool on, float x0 = 0, float y0 = 0, float x1 = 0, float y1 = 0) {
        marquee_ = on;
        marquee_box_[0] = x0, marquee_box_[1] = y0, marquee_box_[2] = x1, marquee_box_[3] = y1;
    }
    // Esquema de atalhos (Preferências): true = estilo Unity (Q/W/E ferramentas, botão direito orbita, WASD com o
    // botão direito apertado, arrastar no vazio seleciona por retângulo); false = o clássico do editor.
    bool unity_keys() const { return unity_keys_; }
    // Vista (Z troca, Shift+G grade): ficam no imgui.ini e o quadro passa ao TrackView.
    void cycle_view_mode() { view_mode_ = (view_mode_ + 1) % 3; }
    void toggle_grid() { show_grid_ = !show_grid_; }
    // Ctrl+P: lista de comandos com busca; Ctrl+vírgula: Preferências.
    void open_commands() { cmd_.request = true; }
    void open_prefs() { show_prefs_ = true; }
    // Mouse sobre o cubo de vista (canto do 3D): o clique é dele, o app não orbita nem seleciona.
    bool over_view_cube(float x, float y) const {
        return show_cube_ && x >= cube_[0] && y >= cube_[1] && x < cube_[0] + cube_[2] && y < cube_[1] + cube_[2];
    }
    // Navegação dos painéis pelo controle ou volante (ImGui: setas do D-pad, A confirma, B volta).
    bool gamepad_nav() const { return gamepad_nav_; }
    // Câmeras do editor e do jogo juntas (Testar no jogo > Avançado, ou menu Exibir); o TrackView segue isto.
    bool camera_sync() const { return launch_.sync; }
    // Controle ou volante (o main abre e lê; nulo = sem): o Back troca entre mexer a câmera e navegar nos painéis.
    // Nos painéis, os volantes (joystick sem mapa) entram como D-pad, A e B do ImGui.
    Pads* pads = nullptr;
    bool pad_panels() const { return pad_panels_; }
    void toggle_pad_panels();

    // Pedidos do usuário que o main executa (abrir outra pista, sair).
    std::string open_request;  // pasta da pista a abrir
    bool quit_request = false;  // confirmou sair (gravando ou não)
    // Pede confirmação de saída se houver edições não gravadas; true = pode sair já.
    bool ask_quit(TrackView* track);
    // Pede para abrir outra pista (com confirmação se houver edições não gravadas).
    void ask_open(TrackView* track, const std::string& dir);
    void show_message(std::string msg) {
        if (!msg.empty()) {
            log_message(msg);
            toast(msg);
        }
        message_ = std::move(msg);
        message_time_ = std::chrono::steady_clock::now();
    }
    bool panels = true;  // barra de ferramentas e painéis (F10; o menu e o status ficam)
    void toggle_help() { show_help_ = !show_help_; }
    void open_about() { about_request_ = true; }
    // Arquivo > Informações da pista: o package.json do pacote .dr2track (nome, país, alfinete do mapa, foto do card).
    void open_track_info() { info_.open = true; }

    // Jogo (grupo do meio da barra de ferramentas e menu Jogo).
    // Testar no jogo (F5): abre a janela de opções ou, com um teste rodando, a do progresso.
    void open_launch(TrackView* track);
    // Pausar ou continuar a especial aberta (F6), pelo canal de comandos do jogo.
    void toggle_pause(TrackView* track);
    // Parar (Shift+F5): cancela o teste em andamento e fecha o jogo.
    void stop_game(TrackView* track);
    // Título da janela: "pista · arquivo * — DR2Hook Editor de Pistas" (o * enquanto houver edições não gravadas).
    static std::string window_title(const TrackView* track);
    // Capturas (--ui): abre a janela "exibicao", "atalhos", "sobre" ou "info" no primeiro quadro. false se o nome não existe.
    bool open_on_start(const std::string& what);
    // Capturas (--launch-tab): a aba do Testar no jogo que abre (geral, veiculo, acerto, condicoes ou jogo). false se não existe.
    bool launch_tab_on_start(const std::string& what);
    // Capturas (--launch-car ID[:nome=valor,...]): o carro e, com ":", o acerto próprio dele. false se o acerto não é nome=número.
    bool launch_car_on_start(const std::string& what);
    // <viewer3d>/cars (modelos e catalog.json); `explicit_dir`: o --car escolheu o modelo do 3D
    void set_cars_root(const std::string& root, bool explicit_dir) { cars_.root = root, cars_.explicit_dir = explicit_dir; }
    // Aviso no canto do 3D por uns segundos (erros ficam mais e em vermelho).
    void toast(const std::string& text);

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
    void track_info_window(TrackView* track, render::OrbitCamera& cam);
    void world_map(TrackInfo& info);
    void card_guide(const TrackView* track) const;
    void poll_launch();
    void launch_modal(TrackView* track);
    void launch_general();
    void launch_vehicle();
    void launch_tuning();
    void launch_conditions(const std::string& overlay);
    void launch_game();
    std::string launch_summary() const;
    void start_launch(TrackView& track);
    void poll_control();
    void run_control(TrackView* track, std::vector<std::string> args, std::string what);
    std::string sel_hint(const TrackView& track) const;
    void view_overlay(TrackView& track, const render::OrbitCamera& cam, const Rect& vp);
    void console_panel(TrackView* track);
    void palette_panel(TrackView& track, const render::OrbitCamera& cam);
    void models_panel(TrackView& track, render::OrbitCamera& cam);
    void track_panel(TrackView& track, render::OrbitCamera& cam);
    // Projeto da pista: Arquivos do projeto (atalhos e pastas), Sons e Configurações da pista
    void project_sync(TrackView* track);
    void project_panel(TrackView& track, render::OrbitCamera& cam);
    void project_tree(const FileNode& node, const std::string& rel, bool overlay);
    void sounds_panel(TrackView& track);
    void config_panel(TrackView& track);
    void start_project_job(const char* what);
    void play_sound(const std::string& path);
    void prefs_window();
    void command_palette(TrackView* track, render::OrbitCamera& cam);
    void draw_toasts(const Rect& vp);
    void view_cube(render::OrbitCamera& cam, const Rect& vp);
    void log_message(const std::string& text);
    void drop_from_palette(TrackView& track, const render::OrbitCamera& cam, const Rect& vp);
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

    bool marquee_ = false;
    float marquee_box_[4] = {};
    bool unity_keys_ = true;
    int lang_pref_ = 0;  // apply_language
    int view_mode_ = 0;  // TrackView::ViewMode
    bool show_grid_ = false, show_stats_ = true, show_cube_ = true, gamepad_nav_ = true;
    float cube_[3] = {};  // x, y, lado do cubo de vista no último quadro
    bool show_prefs_ = false;
    bool show_console_ = false, show_palette_ = false, show_track_ = false;
    // Modelos 3D: lista de todos os tipos com cópia na rota e a vista grande que gira
    bool show_models_ = false;
    char models_filter_[64] = {};
    int models_kind_ = 0;  // 0 todos, 1 objetos e:, 2 ornamentos o:, 3 árvores, 4 terreno distante
    int models_sel_ = -1;
    std::size_t models_next_ = 0;  // próxima cópia do "Ver na pista"
    float models_yaw_ = 0.8f, models_pitch_ = 0.45f, models_zoom_ = 1.0f;
    int models_mode_ = 0;  // TrackView::ViewMode
    bool models_spin_ = true;
    std::vector<std::uint16_t> models_cache_;
    const TrackView* models_of_ = nullptr;
    double models_at_ = -1.0;
    // Projeto da pista (project.hpp): os dados são da pista `track_id` (relidos quando ela muda)
    bool show_project_ = false, show_sounds_ = false, show_config_ = false;
    struct Project {
        std::string repo, track_id, overlay;
        FileNode roots[3];  // pasta do editor, pista na overlay, saída do Testar no jogo
        double scanned_at = -1.0;
        SoundIndex sounds;
        bool sounds_tried = false;
        std::string sounds_error;
        int bank = 0;
        std::uint32_t sound_sel = 0;
        char filter[32] = {};
        SoundPlayer player;
        // extrair os sons ou conferir o inventário: um script por vez
        ChildProcess job;
        Progress job_progress;
        std::string job_what;  // "sons" ou "inventario"
        Inventory inv;
        bool inv_tried = false;
        std::string inv_error;
    } proj_;
    char palette_filter_[64] = {};
    std::vector<std::uint16_t> palette_cache_;  // TrackView::palette_types, refeita a cada meio segundo
    const TrackView* palette_of_ = nullptr;
    double palette_at_ = -1.0;
    bool pad_panels_ = false;
    const char* focus_next_ = nullptr;  // --ui paleta/pista: traz a aba para a frente no primeiro quadro
    float thumb_size_ = 72.0f;
    // Comandos (Ctrl+P): o que aparece é montado a cada quadro em que a janela está aberta.
    struct Command {
        std::string label;
        const char* keys = nullptr;
        std::function<void()> run;
        bool enabled = true;
    };
    struct CommandPalette {
        bool request = false;
        char filter[96] = {};
        int sel = 0;
    } cmd_;
    std::vector<Command> commands(TrackView* track, render::OrbitCamera& cam);
    struct Toast {
        std::string text;
        bool bad = false;
        std::chrono::steady_clock::time_point at;
    };
    std::vector<Toast> toasts_;
    // Mensagens do editor (painéis e pista) para o Console, com a hora.
    std::vector<std::pair<std::string, std::string>> messages_;
    std::string last_status_;
    bool console_follow_ = true;

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
    bool project_to_console_ = false;  // layout 2 -> 3: pôr os Arquivos do projeto como aba do Console
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
        bool windowed = false;   // avançado: o jogo abre em janela (fullscreen="0" no hardware_settings_config.xml)
        bool keep_focus = false; // avançado: o jogo não pausa ao perder o foco (dr2hook_live.ini)
        bool sync = false;       // avançado: câmeras do editor e do jogo juntas (só no editor)
        int tab = 0;             // aba à mostra: 0 Geral, 1 Veículo, 2 Acerto, 3 Condições, 4 Jogo
        int tab_request = -1;    // trocar de aba no próximo quadro (Ctrl+Tab, --launch-tab)
        std::string car = "fr5"; // id do carro (o .nefs em cars/ do jogo)
        std::string livery = "00";
        int time = 1;            // hora: índice em kTimeArg (1 meio-dia)
        int surface = 0;         // piso: 0 seco, 1 molhado
        bool cycle_cam = false;  // câmeras externas alternando (cycle_camera do benchmark)
        bool tuning_on = false;  // acerto próprio (o de `tuning[car]`) em vez do padrão do jogo
        std::map<std::string, TuningValues> tuning;  // por carro: só o que difere do padrão
        char car_filter[64] = {};
        bool scroll_car = false;  // a lista rola até o carro escolhido (ao abrir a aba)
        bool started = false;  // já houve um teste (a janela mostra o progresso em vez das opções)
        bool follow_log = true;
        ChildProcess child;
        Progress progress;
        double finished_at = -1.0;  // segundos do filho quando acabou (para o "levou X s")
    } launch_;

    // Carros do jogo (core/car_catalog.hpp): <viewer3d>/cars/catalog.json, gerado pelo car_catalog.py na primeira
    // vez que a aba Veículo abre. O carro escolhido vira o modelo do carro ao vivo no 3D quando já foi extraído.
    struct Cars {
        std::string root;       // <viewer3d>/cars
        bool explicit_dir = false;  // --car na linha de comando: o modelo não troca com a escolha
        CarCatalog catalog;
        bool loaded = false;    // tentou ler (com ou sem sucesso)
        std::string error;
        ChildProcess child;     // car_catalog.py, quando falta o catalog.json
        Progress progress;
        std::string shown;      // carro já posto no 3D (ou tentado) na pista `shown_track`
        const TrackView* shown_track = nullptr;
        // faixas do acerto (<root>/tuning/<carro>.json, da dxgi via ring_deploy.py); relidas quando o arquivo muda
        CarTuning tuning;
        std::string tuning_car, tuning_error;
        std::filesystem::file_time_type tuning_time{};
        bool tuning_ok = false;
    } cars_;
    const CarTuning* car_tuning(const std::string& car);  // nullptr: o carro ainda não rodou um teste
    int tuning_changes() const;  // quantos valores do acerto próprio do carro escolhido vão para o jogo
    void poll_cars(TrackView* track);
    void load_cars(const std::string& repo);
    bool car_model_ready(const std::string& id) const;

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

    // Informações da pista (track_info.hpp): o que está na janela e o que está no disco, da pista `dir`.
    struct InfoWindow {
        bool open = false;     // pedido do menu (abre e foca no próximo quadro)
        bool visible = false;  // janela aberta (a guia de recorte do card aparece no 3D)
        bool guide = true;     // mostrar a guia de recorte do card no 3D
        std::string dir, error;
        TrackInfo info, saved;
        bool exists = false;
        // mapa-múndi do jogo (build/re/world_map.webp; o world_map_image.py extrai)
        unsigned map_tex = 0;
        int map_w = 0, map_h = 0;
        bool map_tried = false;
        ChildProcess extract;
        Progress extract_progress;
    } info_;
};

}  // namespace dr2::app
