// Terreno de uma rota: todas as malhas DR2M num VBO e num IBO únicos (índices de 32 bits com o
// deslocamento de vértice já somado); uma faixa de índices por malha.
#pragma once

#include "core/dr2m.hpp"
#include "render/gl.hpp"
#include "render/track_shader.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace dr2::render {

class TextureCache;

class Terrain {
public:
    struct Part {
        std::size_t first_index = 0;  // em índices, não em bytes
        GLsizei count = 0;
        std::string material;
        glm::vec3 color{};
        bool vertex_color = false;
        glm::vec3 lo{0.0f}, hi{0.0f};  // caixa da malha, para o corte pela câmera
    };

    explicit Terrain(const std::vector<Mesh>& meshes);
    // Desenha com o programa já em uso só as malhas cuja caixa toca o frustum de `view_proj`. Com
    // `textures`, cada malha usa a textura do material (agrupado por textura, um glMultiDrawElements
    // por grupo); sem, a cor do material.
    // `max_dist` > 0 também corta as malhas cuja caixa fica a mais que isso (no plano xz) de `eye`.
    void draw(const TrackShader& shader, TextureCache* textures, const glm::mat4& view_proj, const glm::vec3& eye = {},
              float max_dist = 0.0f) const;
    std::size_t drawn() const { return drawn_; }
    // Caixa de todas as malhas; false se não há malha.
    bool bounds(glm::vec3& lo, glm::vec3& hi) const {
        if (parts_.empty()) return false;
        lo = parts_[0].lo;
        hi = parts_[0].hi;
        for (const Part& p : parts_) lo = glm::min(lo, p.lo), hi = glm::max(hi, p.hi);
        return true;
    }  // malhas desenhadas no último quadro

    const std::vector<Part>& parts() const { return parts_; }
    std::size_t meshes() const { return meshes_; }
    std::size_t vertices() const { return vertices_; }
    std::size_t triangles() const { return triangles_; }

private:
    gl::Vao vao_;
    gl::Buffer vbo_, ibo_;
    std::vector<Part> parts_;
    std::vector<std::size_t> order_;  // partes ordenadas por material (textura)
    std::size_t meshes_ = 0, vertices_ = 0, triangles_ = 0;
    mutable std::size_t drawn_ = 0;
    mutable std::vector<GLuint> tex_;  // textura de cada parte, resolvida uma vez (sem busca por nome a cada quadro)
    mutable std::vector<bool> tex_done_;
    mutable std::vector<GLsizei> counts_;
    mutable std::vector<const void*> offsets_;
};

}  // namespace dr2::render
