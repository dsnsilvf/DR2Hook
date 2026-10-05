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
    };

    explicit Terrain(const std::vector<Mesh>& meshes);
    // Desenha com o programa já em uso. Com `textures`, cada malha usa a textura do material
    // (agrupado por textura); sem, a cor do material.
    void draw(const TrackShader& shader, TextureCache* textures) const;

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
};

}  // namespace dr2::render
