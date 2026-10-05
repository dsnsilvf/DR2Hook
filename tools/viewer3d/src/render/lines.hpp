// Linhas da rota: portões (degraus, esquerdo e direito) e linhas da IA, como tvLinesFrom.
#pragma once

#include "core/track.hpp"
#include "render/gl.hpp"
#include "render/track_shader.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace dr2::render {

class RouteLines {
public:
    explicit RouteLines(const Route& route);
    void draw(const TrackShader& shader, bool gates, bool ai) const;

private:
    struct Strip {
        GLint first;
        GLsizei count;
        GLenum mode;
        glm::vec3 color;
        bool gate;
    };
    gl::Vao vao_;
    gl::Buffer vbo_;
    std::vector<Strip> strips_;
};

}  // namespace dr2::render
