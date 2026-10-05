#include "render/terrain.hpp"

#include "render/texture.hpp"

#include <algorithm>
#include <cstring>
#include <numeric>

namespace dr2::render {

namespace {

struct Vertex {
    float pos[3];
    float uv[2];
    std::uint8_t col[4];
};
static_assert(sizeof(Vertex) == 24);

}  // namespace

Terrain::Terrain(const std::vector<Mesh>& meshes) {
    std::size_t nv = 0, ni = 0;
    for (const Mesh& m : meshes) {
        nv += m.verts;
        ni += m.idx.size();
    }
    std::vector<Vertex> verts;
    verts.reserve(nv);
    std::vector<std::uint32_t> idx;
    idx.reserve(ni);
    const glm::vec3 base(0.42f, 0.45f, 0.34f);
    for (const Mesh& m : meshes) {
        const auto offset = static_cast<std::uint32_t>(verts.size());
        for (std::uint32_t i = 0; i < m.verts; ++i) {
            Vertex v{};
            std::memcpy(v.pos, &m.pos[3 * i], sizeof v.pos);
            std::memcpy(v.uv, &m.uv[2 * i], sizeof v.uv);
            if (!m.col.empty()) std::memcpy(v.col, &m.col[4 * i], 4);
            else std::memset(v.col, 255, 4);
            verts.push_back(v);
        }
        Part p;
        p.first_index = idx.size();
        p.count = static_cast<GLsizei>(m.idx.size());
        p.material = m.material;
        p.color = material_color(m.material, base);
        p.vertex_color = !m.col.empty();
        for (std::uint32_t i : m.idx) idx.push_back(i + offset);
        if (p.count) parts_.push_back(std::move(p));
    }
    meshes_ = meshes.size();
    vertices_ = verts.size();
    triangles_ = idx.size() / 3;

    vao_.bind();
    vbo_.upload(GL_ARRAY_BUFFER, verts.data(), verts.size() * sizeof(Vertex));
    ibo_.upload(GL_ELEMENT_ARRAY_BUFFER, idx.data(), idx.size() * sizeof(std::uint32_t));
    glEnableVertexAttribArray(kPos);
    glVertexAttribPointer(kPos, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, pos)));
    glEnableVertexAttribArray(kUv);
    glVertexAttribPointer(kUv, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(kCol);
    glVertexAttribPointer(kCol, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, col)));
    glBindVertexArray(0);

    order_.resize(parts_.size());
    std::iota(order_.begin(), order_.end(), std::size_t{0});
    std::stable_sort(order_.begin(), order_.end(), [&](std::size_t a, std::size_t b) { return parts_[a].material < parts_[b].material; });
}

void Terrain::draw(const TrackShader& shader, TextureCache* textures) const {
    vao_.bind();
    TrackShader::identity_rows();
    shader.set_highlight(0.0f);
    GLuint bound = 0;
    for (std::size_t k : order_) {
        const Part& p = parts_[k];
        const GLuint tex = textures ? textures->for_material(p.material) : 0;
        if (tex) {
            if (tex != bound) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, tex);
                bound = tex;
            }
            shader.set_texture(true, false);  // terreno (g|) é opaco: sem descarte por alfa
            shader.set_vertex_color(false);
        } else {
            shader.set_texture(false, false);
            shader.set_color(p.color);
            shader.set_vertex_color(p.vertex_color);
        }
        glDrawElements(GL_TRIANGLES, p.count, GL_UNSIGNED_INT, reinterpret_cast<void*>(p.first_index * sizeof(std::uint32_t)));
    }
    glBindVertexArray(0);
}

}  // namespace dr2::render
