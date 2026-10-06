// A pista aberta com --track: carrega a rota 0, desenha terreno, objetos e linhas, e trata as
// teclas próprias da pista. A câmera e o laço ficam no main.
#pragma once

#include "core/track.hpp"
#include "core/dr2i.hpp"
#include "render/camera.hpp"
#include "edit/history.hpp"
#include "render/instances.hpp"
#include "render/lines.hpp"
#include "render/terrain.hpp"
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
    TrackView(const std::string& dir, std::string out, bool resume = true);
    // <raiz>/saves/<id>.edits.json para uma pista em <raiz>/tracks/<id>; senão build/uiview/saves/.
    static std::string default_out(const std::string& dir, const std::string& id);

    // Enquadra a rota como tvFrameRoute: centro da caixa da IA e dos portões, pitch 0,7.
    void frame_route(render::OrbitCamera& cam) const;
    // Trata uma tecla; true se consumiu.
    bool key(SDL_Keycode key, SDL_Keymod mod, render::OrbitCamera& cam);

    // Mouse (coordenadas da janela, w×h o tamanho dela). Botão esquerdo pressionado com Mover ou
    // Girar sobre um objeto começa a editar (true); o main então manda os movimentos para edit_drag.
    // Com Mover, `shift` apertado sobe e desce em vez de arrastar no chão (pode trocar no meio).
    bool begin_edit(float x, float y, float w, float h, const render::OrbitCamera& cam, bool shift);
    void edit_drag(float x, float y, float w, float h, const render::OrbitCamera& cam, bool shift);
    void end_edit();
    // Clique sem arraste: seleciona o objeto sob o mouse ou desseleciona.
    void click(float x, float y, float w, float h, const render::OrbitCamera& cam);
    bool has_selection() const { return sel_ >= 0; }
    void deselect() { sel_ = -1; }
    // Instância selecionada (-1 = nenhuma). select aceita apagadas (pela árvore, para Restaurar);
    // mover e girar não agem nelas.
    int selected() const { return sel_; }
    void select(int i);
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
    void turn_selected(float deg);
    void frame_selected(render::OrbitCamera& cam) const;

    // Mudança contínua (campos numéricos, gizmo): begin guarda o antes, set_matrix aplica sem
    // histórico, end empilha um passo só. A posição fica na matriz (m[9..11]).
    bool begin_change(std::uint32_t i);
    // Aplica em `i`, que tem de ser a do begin_change; valores não finitos ou acima de 1e6 são ignorados.
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
    float& draw_dist() { return draw_dist_; }
    // Raio do terreno em metros a partir do olho (0 = sem limite, o padrão).
    float& terrain_dist() { return terrain_dist_; }
    const render::InstanceRenderer& objects() const { return *objects_; }
    const render::Terrain& terrain() const { return *terrain_; }
    render::TextureCache& textures() { return textures_; }
    const std::string& out_path() const { return out_; }
    // Arquivo de origem do tipo pelo kind: objects.ens (e), ornaments.bin (o), trees.bin (t).
    static const char* source_file(const std::string& type_name);
    // Corta pela câmera atual e desenha.
    void draw(const glm::mat4& view_proj, const render::OrbitCamera& cam);
    std::string title() const;

    const Track& track() const { return track_; }

private:
    Track track_;
    std::string dir_;
    const Route* route_ = nullptr;
    std::size_t route_index_ = 0;
    std::string terrain_file_;
    render::TrackShader shader_;
    std::unique_ptr<render::Terrain> terrain_;
    std::unique_ptr<render::RouteLines> lines_;
    render::TextureCache textures_;
    Instances inst_;
    std::unique_ptr<render::InstanceRenderer> objects_;
    render::Layers layers_;
    float draw_dist_ = 700.0f;
    float terrain_dist_ = 0.0f;
    unsigned edit_rev_ = 0;
    std::string out_;
    Tool tool_ = Tool::Navigate;
    int sel_ = -1;
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
    std::string current_edits() const;
    void after_history();
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
        bool has_start = false;
        glm::vec3 start{};
        float sy = 0, ex = 0;
        bool shift = false;
        bool numeric = false;  // begin_change (painel ou gizmo), não arraste do mouse no chão
    } drag_;
    void commit(const char* label, std::vector<edit::Snap> before);
    float snapped(float v, float step) const { return step > 0 ? std::round(v / step) * step : v; }
    bool show_terrain_ = true, show_gates_ = true, show_ai_ = true;
};

}  // namespace dr2::app
