// A pista aberta com --track: carrega a rota 0, desenha terreno, objetos e linhas, e trata as
// teclas próprias da pista. A câmera e o laço ficam no main.
#pragma once

#include "core/track.hpp"
#include "core/dr2i.hpp"
#include "core/live.hpp"
#include "render/camera.hpp"
#include "render/car.hpp"
#include "edit/edits_json.hpp"
#include "edit/history.hpp"
#include "render/instances.hpp"
#include "render/lines.hpp"
#include "render/pick.hpp"
#include "render/terrain.hpp"
#include "render/terrain_probe.hpp"
#include "render/texture.hpp"
#include "render/track_shader.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <chrono>
#include <map>
#include <memory>
#include <string>

namespace dr2::app {

class TrackView {
public:
    enum class Tool { Navigate, Move, Rotate };

    // `out`: caminho do edits.json (vazio = default_out). Se ele existe e `resume`, as edições dele
    // voltam para a sessão (continuar de onde parou).
    TrackView(const std::string& dir, std::string out, bool resume = true, render::TextureCache::Options tex = {});
    ~TrackView();
    TrackView(const TrackView&) = delete;
    TrackView& operator=(const TrackView&) = delete;
    // <raiz>/saves/<id>.edits.json para uma pista em <raiz>/tracks/<id>; senão build/uiview/saves/.
    static std::string default_out(const std::string& dir, const std::string& id);

    // Enquadra a rota como tvFrameRoute: centro da caixa da IA e dos portões, pitch 0,7.
    void frame_route(render::OrbitCamera& cam) const;
    // Trata uma tecla; true se consumiu. Com `unity_keys`, Q/W/E trocam a ferramenta (navegar, mover, girar);
    // sem, Q/E giram 15°. Vírgula e ponto giram nos dois.
    bool key(SDL_Keycode key, SDL_Keymod mod, render::OrbitCamera& cam);
    bool unity_keys = true;

    // Mouse (coordenadas da janela, w×h o tamanho dela). Botão esquerdo pressionado com Mover ou
    // Girar sobre um objeto começa a editar (true); o main então manda os movimentos para edit_drag.
    // Com Mover, `shift` apertado sobe e desce em vez de arrastar no chão (pode trocar no meio).
    bool begin_edit(float x, float y, float w, float h, const render::OrbitCamera& cam, bool shift);
    void edit_drag(float x, float y, float w, float h, const render::OrbitCamera& cam, bool shift);
    void end_edit();
    // Clique sem arraste: seleciona o que está sob o mouse (câmera do replay ou vaga de largada primeiro, que são
    // desenhadas por cima de tudo; senão um objeto) e tira o resto da seleção; no vazio, tira tudo.
    // Com `add` (Ctrl), o objeto sob o mouse entra na seleção ou sai dela, e o resto fica.
    void click(float x, float y, float w, float h, const render::OrbitCamera& cam, bool add = false);
    bool has_selection() const { return sel_ >= 0; }
    void deselect();
    // Instância principal da seleção (-1 = nenhuma): a do Inspector, do gizmo e o pivô do girar.
    // select aceita apagadas (pela árvore, para Restaurar); mover e girar não agem nelas.
    int selected() const { return sel_; }
    void select(int i);
    // Seleção múltipla: todas as selecionadas, a principal incluída (vazia = nenhuma). As ações (apagar,
    // girar, mover pelo gizmo ou pelo mouse, duplicar, pôr no chão…) valem para todas num passo só.
    const std::vector<std::uint32_t>& selection() const { return multi_; }
    std::size_t selection_count() const { return multi_.size(); }
    bool is_selected(std::uint32_t i) const;
    // Entra na seleção (vira a principal) ou sai dela.
    void toggle_select(int i);
    // Troca a seleção por `idx` (ou soma, com `add`); a última vira a principal.
    void select_many(const std::vector<std::uint32_t>& idx, bool add);
    // Retângulo na tela (pixels da área do 3D, w×h o tamanho dela): seleciona os objetos à mostra com o centro
    // dentro dele (não os travados). Devolve quantos entraram.
    std::size_t select_rect(float x0, float y0, float x1, float y1, float w, float h, const render::OrbitCamera& cam, bool add);
    // Esconder (olho da Cena) e travar, só na vista: não vão ao histórico nem ao edits.json.
    bool veiled(std::uint32_t i) const { return inst_.flag(i, Instances::kVeiled); }
    bool locked(std::uint32_t i) const { return inst_.flag(i, Instances::kLocked); }
    void set_veiled(std::uint32_t i, bool on);
    void set_locked(std::uint32_t i, bool on);
    // Mostra tudo que foi escondido pelo olho e destrava tudo.
    void clear_view_flags();
    std::size_t veiled_count() const;
    // Todas as instâncias à mostra de um tipo (para "selecionar todos deste tipo").
    std::vector<std::uint32_t> of_type(std::uint16_t type) const;
    // Copia uma instância do tipo `type` para o ponto `at` (soltar da paleta), selecionada, num passo de
    // histórico. Só objetos e: (objects.ens). Devolve o índice ou -1.
    int place_type(std::uint16_t type, const glm::vec3& at);
    // Paleta: tipos e: com instância nesta rota (os que place_type aceita), em ordem de nome.
    std::vector<std::uint16_t> palette_types() const;
    // Miniatura do tipo (textura GL kThumbSide², 0 = ainda não feita: fica na fila). thumb_tick faz até `budget`
    // por quadro, fora do 3D (num framebuffer próprio); refaz quando as texturas do tipo chegam depois.
    enum class ViewMode { Textured, Solid, Wire };
    static constexpr int kThumbSide = 128;
    unsigned thumbnail(std::uint16_t type);
    void thumb_tick(int budget = 2);
    // Janela Modelos 3D: todos os tipos com malha e com cópia nesta rota (objetos, ornamentos, árvores e terreno
    // distante), por camada e nome. A vista grande é pedida a cada quadro (preview) e desenhada no thumb_tick,
    // num framebuffer `side`²; devolve a textura do pedido anterior (0 = ainda não há). yaw/pitch em radianos,
    // zoom multiplica a distância que enquadra o modelo, mode = ViewMode.
    std::vector<std::uint16_t> model_types() const;
    unsigned preview(std::uint16_t type, int side, float yaw, float pitch, float zoom, ViewMode mode);
    // Grava o edits.json; false se falhou (a mensagem fica em status() e o programa segue).
    bool save();
    // Há edições diferentes das da última gravação (ou do arquivo aberto)?
    bool unsaved() const;
    // Última mensagem para o usuário (gravou, falhou, não abriu a rota).
    const std::string& status() const { return status_; }
    double status_age() const;  // segundos desde a última mensagem
    // A cada quadro: grava <out>.autosave.json se há edições não gravadas há mais de `period` segundos
    // (um crash ou um kill não perde a sessão). O Ctrl+S apaga o autosave.
    void autosave_tick(double period = 60.0);
    std::string autosave_path() const { return out_ + ".autosave.json"; }
    bool has_autosave() const;
    bool recover_autosave();
    // Volta ao estado descrito por um edits.json em texto (rotas relidas do arquivo exportado, sem
    // histórico); fica "não gravado". false (e a mensagem em status) se não deu.
    bool recover(const std::string& edits_text, const std::string& from);
    // edits.json de todas as rotas abertas, como o Ctrl+S gravaria.
    std::string current_edits() const;
    void set_status(std::string msg);
    Tool tool() const { return tool_; }
    const Instances& instances() const { return inst_; }
    // Troca para a rota seguinte (+1) ou anterior (-1), guardando as edições da atual.
    void switch_route(int step);
    // Abre a rota `index` (guarda as edições da atual); se falhar, fica na atual e avisa em status().
    void open_route(std::size_t index);
    std::size_t route_index() const { return route_index_; }
    const Route& route() const { return *route_; }

    // Ações (as mesmas dos atalhos), para os painéis.
    void set_tool(Tool t) { if (!drag_.active) tool_ = t; }
    bool undo();
    bool redo();
    // Volta ou avança o histórico até a posição `pos` (0 = antes da primeira edição).
    void history_go(std::size_t pos);
    const edit::History& history() const { return hist_; }
    void delete_selected();
    void duplicate_selected();
    void restore_selected();
    // Gira em volta do eixo `axis` da pista (0 = X, 1 = Y, 2 = Z) por `deg` graus; com várias, em volta da principal.
    void turn_selected(float deg, int axis = 1);
    void frame_selected(render::OrbitCamera& cam) const;

    // Mudança contínua (campos numéricos, gizmo): begin guarda o antes, set_matrix aplica sem
    // histórico, end empilha um passo só. A posição fica na matriz (m[9..11]).
    bool begin_change(std::uint32_t i);
    // Aplica em `i`, que tem de ser a do begin_change; valores não finitos ou acima de 1e6 são ignorados.
    // As outras selecionadas acompanham (a mesma mudança, relativa à principal).
    void set_matrix(std::uint32_t i, const float* m);
    // Fecha uma mudança numérica aberta (passo no histórico); seleção nova, clique e arraste chamam.
    void close_change();
    void end_change(const char* label);
    bool changing() const { return drag_.active; }

    // Encaixe: passo do mover (m; 0 = livre) e do girar por arraste (graus; 0 = livre).
    float snap_move = 0.0f, snap_turn = 0.0f;

    // Camadas e raio, para os painéis.
    render::Layers& layers() { return layers_; }
    bool& show_terrain() { return show_terrain_; }
    bool& show_gates() { return show_gates_; }
    bool& show_ai() { return show_ai_; }
    bool& show_replay() { return show_replay_; }
    // Câmera do replay em destaque (-1 = nenhuma) e "ver por ela": a órbita fica com o olho na câmera
    // e o alvo no ponto para onde ela olha (nas que andam, o começo do caminho).
    int replay_selected() const { return replay_sel_; }
    void select_replay(int i) { replay_sel_ = route_ && i >= 0 && i < static_cast<int>(route_->replay.cameras.size()) ? i : -1; }
    bool look_through(int i, render::OrbitCamera& cam);
    // Edição das câmeras do replay (cada uma é um passo de histórico e vai na seção "replay" do edits.json).
    // Nova: câmera de beira de pista (camera_r0_spectator_NNN) com o olho e o alvo da vista atual, e uma zona de troca
    // `lead` metros antes do ponto da pista para onde ela olha, que liga a câmera nova. Devolve o índice ou -1.
    int add_replay_camera(const render::OrbitCamera& cam, double lead = 40.0);
    // Põe a câmera `i` no olho e no alvo da vista atual (uma câmera com caminho leva o caminho junto).
    bool move_replay_camera(int i, const render::OrbitCamera& cam);
    // Apaga a câmera `i` e as trocas para ela; uma zona que fica sem trocas sai também, e as chances das outras
    // trocas da zona voltam a somar 100%.
    bool delete_replay_camera(int i);
    // Muda a zona de troca da câmera `i` para `lead` metros antes do ponto da pista para onde ela olha.
    bool replay_zone_before(int i, double lead = 40.0);
    // Câmeras e zonas mudadas em relação ao track.json (nesta rota).
    bool replay_changed() const;
    // Volta as câmeras e zonas desta rota ao track.json, num passo de histórico.
    bool reset_replay();
    // Vagas de largada (onde o carro nasce): camada (L), vaga em destaque e "ver do carro" (Shift+L: a vista no
    // banco do piloto, olhando para a frente da vaga), ou "enquadrar" (de trás e de cima).
    bool& show_grids() { return show_grids_; }
    SlotRef slot_selected() const { return slot_sel_; }
    void select_slot(SlotRef r) { slot_sel_ = route_ && route_->slot(r) ? r : SlotRef{}; }
    bool look_from_slot(SlotRef r, render::OrbitCamera& cam);
    bool frame_slot(SlotRef r, render::OrbitCamera& cam);
    // Folga da vaga até o terreno: no centro e a menor sob as 4 rodas (m); false se não há terreno embaixo.
    // Abaixo de kSlotLow o carro nasce dentro do chão (no jogo: posição NaN e a carga trava).
    static constexpr float kSlotLow = 0.2f, kSlotHigh = 1.5f;
    bool slot_clearance(const GridSlot& s, float& center, float& wheels);
    float& draw_dist() { return draw_dist_; }
    // Raio do terreno em metros a partir do alvo da câmera, no plano xz (0 = sem limite, o padrão).
    float& terrain_dist() { return terrain_dist_; }
    const render::InstanceRenderer& objects() const { return *objects_; }
    const render::Terrain& terrain() const { return *terrain_; }
    render::TextureCache& textures() { return textures_; }
    const std::string& out_path() const { return out_; }
    // Arquivo de origem do tipo pelo kind: objects.ens (e), ornaments.bin (o), trees.bin (t).
    static const char* source_file(const std::string& type_name);
    // Instância sob o raio (a mesma do clique, com o terreno na frente tapando o que fica atrás); -1 se nenhuma.
    int pick_ray(const render::Ray& ray, const glm::vec3& target);
    // Câmera do replay ou vaga de largada à mostra sob o raio (a mais perto do olho). `pixel`: tamanho de um pixel a
    // 1 m do olho, para que as de longe ainda peguem num raio de alguns pixels. false se nenhuma.
    struct HelperHit {
        int camera = -1;
        SlotRef slot;
    };
    bool pick_helper(const render::Ray& ray, float pixel, HelperHit& hit) const;
    // Distância do olho até o terreno ao longo do raio (a mesma que o picking usa); false sem terreno.
    bool terrain_hit(const render::Ray& ray, float& t);
    // Altura do terreno em (x, z), contando só o que fica abaixo de `y_from` (e, se não achar, de cima).
    bool terrain_height(float x, float z, float y_from, float& y);
    // Põe a instância selecionada no chão (y = altura do terreno em x, z), num passo de histórico.
    void settle_selected();
    // Alinhar ao terreno (Shift+T): a selecionada acompanha o chão sob a base do tipo (edit::fit_to_ground),
    // num passo de histórico. Objetos inclinam junto e descem o que ficaria no ar; árvores, e tudo quando
    // `align_upright` está ligado, ficam em pé e descem até o ponto mais baixo do chão sob a base.
    void align_selected();
    // O mesmo para todas as instâncias à mostra do tipo da selecionada, num passo só.
    void align_type();
    bool align_upright = false;
    // Base usada pelo alinhar (caixa local do tipo; tronco nas árvores) e se o tipo fica em pé. Falso se o tipo não alinha.
    bool fit_footprint(std::uint32_t i, float& x0, float& x1, float& z0, float& z1, bool& upright) const;
    // Mover grudado no terreno: a altura acompanha o chão durante o arraste (guarda a folga que o objeto tinha).
    bool follow_ground = false;
    // Modo de vista: com texturas, sólido (cinza com luz, para ver a forma) ou aramado.
    // Grade no plano da altura do alvo da câmera, com passo que acompanha o zoom (1, 10, 100 m…).
    bool show_grid = false;
    float grid_step() const { return grid_step_; }
    ViewMode view_mode = ViewMode::Textured;
    // Corta pela câmera atual e desenha.
    void draw(const glm::mat4& view_proj, const render::OrbitCamera& cam);
    std::string title() const;

    const Track& track() const { return track_; }

    // Jogo ao vivo (core/live.hpp): o carro do jogador e a câmera da especial, quando o jogo está aberto com
    // o mod. Vista livre (a de sempre), seguir o carro (a órbita gira em volta dele) ou pela câmera do jogo.
    enum class LiveView { Free, FollowCar, GameCamera };
    // Lê o que chegou e, conforme a vista, leva a câmera ao carro ou à câmera do jogo. Uma vez por quadro.
    void live_tick(render::OrbitCamera& cam);
    LiveView live_view() const { return live_view_; }
    void set_live_view(LiveView v);
    bool& show_live() { return show_live_; }
    // Câmeras juntas: com o jogo ao vivo, a câmera livre liga e desliga junto nos dois (F9 no jogo, V aqui) e,
    // com ela ligada, quem mexeu por último leva a outra (LiveSender para o jogo, os passos do pacote para cá).
    bool sync_camera() const { return sync_; }
    void set_sync_camera(bool on);
    // Câmeras juntas com o jogo ao vivo: o 3D fica parecido com o jogo para comparar, sem as ajudas do editor
    // (rotas, câmeras e limites do replay, vagas, grade do chão, destaque e gizmo da seleção, cubo de vistas, mastro do carro).
    bool clean_view() const { return sync_ && live_connected(); }
    // Malha do carro do jogo (pasta do viewer_car.py) no lugar da caixa; vazio = sem modelo. Precisa do GL.
    void set_car(const std::string& dir);
    const render::CarRenderer* car() const { return car_.get(); }
    bool live_connected() const { return live_.age() < 2.0; }
    // O pacote mais novo (vale só com live_connected): carro na especial, pausa, velocidade.
    const LiveSample& live_sample() const { return live_.last(); }
    std::string live_status() const;

private:
    Track track_;
    std::string dir_;
    const Route* route_ = nullptr;
    std::size_t route_index_ = 0;
    std::vector<Replay> replay0_;  // replay de cada rota como veio do track.json
    Replay& replay_mut() { return track_.routes[route_index_].replay; }
    // Passo de histórico do replay (o de agora contra `before`) e linhas refeitas.
    void commit_replay(const char* label, Replay before);
    double gate_at(const Vec3& p) const;           // distância na pista do portão mais perto de `p`
    const Gate* gate_near_d(double d) const;       // portão mais perto da distância `d` (dá a volta)
    std::string terrain_file_;
    render::TrackShader shader_;
    std::unique_ptr<render::Terrain> terrain_;
    std::unique_ptr<render::RouteLines> lines_;
    render::TextureCache textures_;
    std::unique_ptr<render::TerrainProbe> probe_;
    Instances inst_;
    std::unique_ptr<render::InstanceRenderer> objects_;
    render::Layers layers_;
    float draw_dist_ = 700.0f;
    float terrain_dist_ = 0.0f;
    unsigned edit_rev_ = 0;
    std::string out_;
    Tool tool_ = Tool::Navigate;
    int sel_ = -1;
    std::vector<std::uint32_t> multi_;  // seleção inteira, com a principal (sel_)
    void set_sel(int i);
    // Selecionadas que não estão apagadas, a principal primeiro.
    std::vector<std::uint32_t> live_selection() const;
    // Aplica `d` (matriz 4×4 da pista) às instâncias, num passo de histórico.
    void transform(const std::vector<std::uint32_t>& idx, const glm::mat4& d, const char* label);
    // As outras do grupo do arraste seguem a principal (a mesma mudança desde a base).
    void follow_group();
    edit::History hist_;
    std::string status_;
    std::chrono::steady_clock::time_point status_time_{};
    void resume_edits();
    static bool can_write(const std::string& path);
    std::string saved_text_;  // edits.json da última gravação (ou sem edições), para saber se há o que gravar
    bool backed_up_ = false;
    std::chrono::steady_clock::time_point autosave_t_ = std::chrono::steady_clock::now();  // a cópia .bak do arquivo anterior já foi feita nesta sessão
    unsigned saves_ = 0;
    mutable bool unsaved_ = false;
    mutable unsigned unsaved_rev_ = ~0u, unsaved_saves_ = ~0u;
    void after_history();
    bool wrote_autosave_ = false;
    void apply_doc(const json::Value& doc, edit::ApplyReport& rep);
    struct Saved {
        Instances inst;
        edit::History hist;
    };
    std::map<std::size_t, Saved> saved_;  // rotas abertas antes, com edições e histórico
    void load_route(std::size_t index);
    struct EditDrag {
        bool active = false;
        std::uint32_t i = 0;
        std::vector<edit::Snap> before;
        float base[kInstFloats] = {};
        std::vector<std::uint32_t> group;  // as outras selecionadas, que acompanham a principal
        std::vector<float> group_base;     // 12 floats de cada uma, do começo (ou da troca de Shift)
        bool has_start = false;
        glm::vec3 start{};
        float sy = 0, ex = 0;
        bool shift = false;
        bool numeric = false;  // begin_change (painel ou gizmo), não arraste do mouse no chão
        float ground_off = 0;  // y do objeto menos a altura do terreno no ponto de partida
        bool has_ground = false;
    } drag_;
    static constexpr float kTerrainSlack = 0.25f;  // folga (m) entre a caixa do objeto e o terreno no teste de oclusão
    float visible_terrain_t(const render::Ray& ray);
    bool fit_instance(std::uint32_t i, float* out);
    void commit(const char* label, std::vector<edit::Snap> before);
    float snapped(float v, float step) const { return step > 0 ? std::round(v / step) * step : v; }
    bool show_terrain_ = true, show_gates_ = true, show_ai_ = true, show_replay_ = true, show_grids_ = true;
    int replay_sel_ = -1;
    SlotRef slot_sel_;
    LiveReceiver live_;
    GameTransform game_;
    std::string live_error_;
    LiveView live_view_ = LiveView::Free;
    bool show_live_ = true;
    // câmeras juntas
    LiveSender sender_;
    EditorMessage to_game_;          // o que vai ao jogo (mandado a cada quadro enquanto há sinal)
    bool sync_ = false;
    bool sync_seen_ = false;         // já leu o estado do jogo desde que ligou (senão a próxima leitura manda)
    bool game_free_ = false;         // câmera livre do jogo no último pacote
    int asked_ = -1;                 // o último pedido do editor ainda não visto no jogo: 1 ligar, 0 desligar
    std::uint32_t game_moves_ = 0;   // passos da câmera livre do jogo já seguidos
    render::OrbitCamera synced_;     // a câmera como ficou na última troca (diferente = o usuário mexeu aqui)
    void sync_tick(render::OrbitCamera& cam);
    void sync_request(bool want_free);
    void sync_push(const render::OrbitCamera& cam);
    void take_game_pose(render::OrbitCamera& cam);
    render::DynamicLines live_lines_;
    render::DynamicLines grid_lines_;
    struct Thumbs {
        unsigned fbo = 0, depth = 0;
        std::map<std::uint16_t, unsigned> tex;
        std::map<std::uint16_t, int> redo;  // vezes que ainda refaz (texturas a caminho quando foi feita)
        std::vector<std::uint16_t> queue;
        double next_redo = 0.0;
        Thumbs() = default;
        Thumbs(const Thumbs&) = delete;
        Thumbs& operator=(const Thumbs&) = delete;
        ~Thumbs();
    } thumbs_;
    bool render_thumb(std::uint16_t type);
    struct Preview {
        unsigned fbo = 0, depth = 0, tex = 0;
        int side = 0, made = 0;  // pedido e o tamanho da textura atual
        bool want = false;
        std::uint16_t type = 0;
        float yaw = 0.8f, pitch = 0.45f, zoom = 1.0f;
        ViewMode mode = ViewMode::Textured;
        Preview() = default;
        Preview(const Preview&) = delete;
        Preview& operator=(const Preview&) = delete;
        ~Preview();
    } preview_;
    void render_preview();
    // Desenha o tipo (pela primeira cópia à mostra, com a matriz dela) enquadrado no framebuffer já ligado.
    bool draw_type_framed(std::uint16_t type, int side, float yaw, float pitch, float zoom, ViewMode mode);
    float grid_step_ = 10.0f;
    void draw_grid(const render::OrbitCamera& cam);
    std::unique_ptr<render::CarRenderer> car_;  // sem ele, a caixa
    void draw_live();
};

}  // namespace dr2::app
