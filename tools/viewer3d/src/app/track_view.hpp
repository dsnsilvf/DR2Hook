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

#include <memory>
#include <string>

namespace dr2::app {

class TrackView {
public:
    enum class Tool { Navigate, Move, Rotate };

    // `out`: caminho do edits.json (vazio = build/uiview/saves/<id>.edits.json).
    TrackView(const std::string& dir, std::string out);

    // Enquadra a rota como tvFrameRoute: centro da caixa da IA e dos portões, pitch 0,7.
    void frame_route(render::OrbitCamera& cam) const;
    // Trata uma tecla; true se consumiu.
    bool key(SDL_Keycode key, SDL_Keymod mod, render::OrbitCamera& cam);

    // Mouse (coordenadas da janela, w×h o tamanho dela). Botão esquerdo pressionado com Mover ou
    // Girar sobre um objeto começa a editar (true); o main então manda os movimentos para edit_drag.
    bool begin_edit(float x, float y, float w, float h, const render::OrbitCamera& cam);
    void edit_drag(float x, float y, float w, float h, const render::OrbitCamera& cam, bool shift);
    void end_edit();
    // Clique sem arraste: seleciona o objeto sob o mouse ou desseleciona.
    void click(float x, float y, float w, float h, const render::OrbitCamera& cam);
    bool has_selection() const { return sel_ >= 0; }
    void deselect() { sel_ = -1; }
    // Grava o edits.json; devolve o número de edições.
    std::size_t save();
    const Instances& instances() const { return inst_; }
    // Corta pela câmera atual e desenha.
    void draw(const glm::mat4& view_proj, const render::OrbitCamera& cam);
    std::string title() const;

    const Track& track() const { return track_; }

private:
    Track track_;
    const Route* route_ = nullptr;
    render::TrackShader shader_;
    std::unique_ptr<render::Terrain> terrain_;
    std::unique_ptr<render::RouteLines> lines_;
    render::TextureCache textures_;
    Instances inst_;
    std::unique_ptr<render::InstanceRenderer> objects_;
    render::Layers layers_;
    float draw_dist_ = 700.0f;
    unsigned edit_rev_ = 0;
    std::string out_;
    Tool tool_ = Tool::Navigate;
    int sel_ = -1;
    edit::History hist_;
    struct EditDrag {
        bool active = false;
        std::uint32_t i = 0;
        std::vector<edit::Snap> before;
        float base[kInstFloats] = {};
        bool has_start = false;
        glm::vec3 start{};
        float sy = 0, ex = 0;
    } drag_;
    void frame_selected(render::OrbitCamera& cam) const;
    void commit(const char* label, std::vector<edit::Snap> before);
    void delete_selected();
    bool show_terrain_ = true, show_gates_ = true, show_ai_ = true;
};

}  // namespace dr2::app
