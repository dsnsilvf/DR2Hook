// A pista aberta com --track: carrega a rota 0, desenha terreno, objetos e linhas, e trata as
// teclas próprias da pista. A câmera e o laço ficam no main.
#pragma once

#include "core/track.hpp"
#include "core/dr2i.hpp"
#include "render/camera.hpp"
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
    explicit TrackView(const std::string& dir);

    // Enquadra a rota como tvFrameRoute: centro da caixa da IA e dos portões, pitch 0,7.
    void frame_route(render::OrbitCamera& cam) const;
    // Trata uma tecla; true se consumiu.
    bool key(SDL_Keycode key, render::OrbitCamera& cam);
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
    bool show_terrain_ = true, show_gates_ = true, show_ai_ = true;
};

}  // namespace dr2::app
