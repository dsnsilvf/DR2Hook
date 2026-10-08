// Linhas da rota: portões (degraus, esquerdo e direito) e linhas da IA, como tvLinesFrom, o replay (câmeras com
// um ícone de câmera em arame e, na em destaque, o cone de visão; caminhos das que andam, zonas de troca ligadas à
// câmera e prismas) e as vagas de largada (caixa do carro com seta para a frente e um mastro).
#pragma once

#include "core/track.hpp"
#include "render/gl.hpp"
#include "render/track_shader.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace dr2::render {

// O que desenhar. `sel_cam`: câmera do replay em destaque (as linhas dela e das zonas que a ligam ficam brancas, e
// só ela mostra o cone de visão); `inside_cam`: a câmera por onde se está vendo (o ícone e o cone dela não são
// desenhados). `sel_slot`/`inside_slot` fazem o mesmo com as vagas de largada, pelo índice corrido
// (Route::slot_index). -1 = nenhuma.
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
        bool frustum = false;   // o desenho da própria câmera (some quando se olha por ela)
        bool sel_only = false;  // só na câmera em destaque (o cone de visão)
    };
    gl::Vao vao_;
    gl::Buffer vbo_;
    std::vector<Strip> strips_;
};

// Linhas refeitas a cada quadro (o carro do jogo ao vivo): pares de pontos (GL_LINES), uma cor por grupo.
class DynamicLines {
public:
    struct Group {
        std::vector<glm::vec3> points;  // pares
        glm::vec3 color{1.0f};
        bool on_top = true;  // sem teste de profundidade (aparece através do terreno)
    };
    void draw(const TrackShader& shader, const std::vector<Group>& groups);

private:
    gl::Vao vao_;
    gl::Buffer vbo_;
};

// Caixa de um carro (pares para GL_LINES): centro `c`, eixos direita/cima/frente, largura × comprimento, de
// `below` abaixo do centro a `above` acima, com seta no teto.
std::vector<glm::vec3> car_box(const glm::vec3& c, const glm::vec3& right, const glm::vec3& up, const glm::vec3& fwd,
                               float width, float length, float below, float above);

}  // namespace dr2::render
