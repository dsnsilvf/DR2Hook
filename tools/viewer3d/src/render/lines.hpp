// Linhas da rota: portões (degraus, esquerdo e direito) e linhas da IA, como tvLinesFrom, o replay
// (câmeras com o cone de visão, caminhos das que andam, zonas de troca ligadas à câmera e prismas) e as vagas
// de largada (caixa do carro com seta para a frente e um mastro).
#pragma once

#include "core/track.hpp"
#include "render/gl.hpp"
#include "render/track_shader.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace dr2::render {

// O que desenhar. `sel_cam`: câmera do replay em destaque (as linhas dela e das zonas que a ligam ficam brancas);
// `inside_cam`: a câmera por onde se está vendo (a pirâmide dela não é desenhada). `sel_slot`/`inside_slot` fazem o
// mesmo com as vagas de largada, pelo índice corrido (Route::slot_index). -1 = nenhuma.
struct LinesShow {
    bool gates = true, ai = true, replay = false, grids = false;
    int sel_cam = -1, inside_cam = -1, sel_slot = -1, inside_slot = -1;
};

class RouteLines {
public:
    explicit RouteLines(const Route& route);
    void draw(const TrackShader& shader, const LinesShow& show) const;

private:
    enum class Kind : unsigned char { Gate, Ai, Replay, Grid };
    struct Strip {
        GLint first;
        GLsizei count;
        GLenum mode;
        glm::vec3 color;
        Kind kind;
        int cam = -1;  // câmera do replay (ou vaga de largada, em Kind::Grid) a que a linha pertence
        bool frustum = false;  // a pirâmide da própria câmera
    };
    gl::Vao vao_;
    gl::Buffer vbo_;
    std::vector<Strip> strips_;
};

}  // namespace dr2::render
