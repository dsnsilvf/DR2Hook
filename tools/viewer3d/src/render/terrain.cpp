#include "render/terrain.hpp"

#include "render/texture.hpp"

#include <algorithm>
#include <array>
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
    // buffers do tamanho final e envio malha por malha: sem uma cópia intercalada da pista inteira na
    // RAM (numa pista de 10 M de vértices eram ~430 MB a mais no pico da carga)
    vao_.bind();
    vbo_.upload(GL_ARRAY_BUFFER, nullptr, nv * sizeof(Vertex));
    ibo_.upload(GL_ELEMENT_ARRAY_BUFFER, nullptr, ni * sizeof(std::uint32_t));
    const glm::vec3 base(0.42f, 0.45f, 0.34f);
    std::vector<Vertex> verts;
    std::vector<std::uint32_t> idx;
    std::size_t v_at = 0, i_at = 0;
    for (const Mesh& m : meshes) {
        const auto offset = static_cast<std::uint32_t>(v_at);
        verts.clear();
        verts.reserve(m.verts);
        for (std::uint32_t i = 0; i < m.verts; ++i) {
            Vertex v{};
            std::memcpy(v.pos, &m.pos[3 * i], sizeof v.pos);
            std::memcpy(v.uv, &m.uv[2 * i], sizeof v.uv);
            if (!m.col.empty()) std::memcpy(v.col, &m.col[4 * i], 4);
            else std::memset(v.col, 255, 4);
            verts.push_back(v);
        }
        Part p;
        p.first_index = i_at;
        p.count = static_cast<GLsizei>(m.idx.size());
        p.material = m.material;
        p.color = material_color(m.material, base);
        p.vertex_color = !m.col.empty();
        if (m.verts) {
            p.lo = p.hi = glm::vec3(m.pos[0], m.pos[1], m.pos[2]);
            for (std::uint32_t i = 1; i < m.verts; ++i) {
                const glm::vec3 v(m.pos[3 * i], m.pos[3 * i + 1], m.pos[3 * i + 2]);
                p.lo = glm::min(p.lo, v);
                p.hi = glm::max(p.hi, v);
            }
        }
        idx.clear();
        idx.reserve(m.idx.size());
        for (std::uint32_t i : m.idx) idx.push_back(i + offset);
        if (!verts.empty())
            glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(v_at * sizeof(Vertex)),
                            static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)), verts.data());
        if (!idx.empty())
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>(i_at * sizeof(std::uint32_t)),
                            static_cast<GLsizeiptr>(idx.size() * sizeof(std::uint32_t)), idx.data());
        v_at += m.verts;
        i_at += m.idx.size();
        if (p.count) parts_.push_back(std::move(p));
    }
    meshes_ = meshes.size();
    vertices_ = nv;
    triangles_ = ni / 3;

    glBindBuffer(GL_ARRAY_BUFFER, vbo_.id());
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

namespace {

// Os 6 planos do frustum (Gribb e Hartmann), em coordenadas de mundo: a·x + b·y + c·z + d >= 0 dentro.
std::array<glm::vec4, 6> frustum_planes(const glm::mat4& m) {
    const glm::vec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]), r1(m[0][1], m[1][1], m[2][1], m[3][1]),
        r2(m[0][2], m[1][2], m[2][2], m[3][2]), r3(m[0][3], m[1][3], m[2][3], m[3][3]);
    return {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2};
}

// A caixa está toda do lado de fora de algum plano? (teste conservador: na dúvida, desenha)
bool outside(const std::array<glm::vec4, 6>& planes, const glm::vec3& lo, const glm::vec3& hi) {
    for (const glm::vec4& p : planes) {
        const glm::vec3 far(p.x >= 0 ? hi.x : lo.x, p.y >= 0 ? hi.y : lo.y, p.z >= 0 ? hi.z : lo.z);
        if (p.x * far.x + p.y * far.y + p.z * far.z + p.w < 0) return true;
    }
    return false;
}

}  // namespace

void Terrain::draw_probe(const glm::mat4& view_proj) const {
    vao_.bind();
    const auto planes = frustum_planes(view_proj);
    probe_counts_.clear();
    probe_offsets_.clear();
    for (const Part& p : parts_) {
        if (outside(planes, p.lo, p.hi)) continue;
        probe_counts_.push_back(p.count);
        probe_offsets_.push_back(reinterpret_cast<const void*>(p.first_index * sizeof(std::uint32_t)));
    }
    if (!probe_counts_.empty())
        glMultiDrawElements(GL_TRIANGLES, probe_counts_.data(), GL_UNSIGNED_INT, probe_offsets_.data(),
                            static_cast<GLsizei>(probe_counts_.size()));
    glBindVertexArray(0);
}

void Terrain::draw(const TrackShader& shader, TextureCache* textures, const glm::mat4& view_proj, const glm::vec3& center,
                   float max_dist) const {
    vao_.bind();
    TrackShader::identity_rows();
    shader.set_highlight(0.0f);
    if (tex_.size() != parts_.size()) {
        tex_.assign(parts_.size(), 0);
        handle_.assign(parts_.size(), -2);
    }
    const auto planes = frustum_planes(view_proj);
    drawn_ = 0;
    GLuint bound = 0;
    // partes visíveis em ordem de material; as do mesmo estado (textura, ou cor e cor por vértice)
    // vão num glMultiDrawElements só
    auto flush = [&]() {
        if (counts_.empty()) return;
        glMultiDrawElements(GL_TRIANGLES, counts_.data(), GL_UNSIGNED_INT, offsets_.data(), static_cast<GLsizei>(counts_.size()));
        counts_.clear();
        offsets_.clear();
    };
    std::size_t prev = parts_.size();
    for (std::size_t k : order_) {
        const Part& p = parts_[k];
        if (outside(planes, p.lo, p.hi)) continue;
        if (max_dist > 0.0f) {
            const float dx = std::max({p.lo.x - center.x, 0.0f, center.x - p.hi.x});
            const float dz = std::max({p.lo.z - center.z, 0.0f, center.z - p.hi.z});
            if (dx * dx + dz * dz > max_dist * max_dist) continue;
        }
        ++drawn_;
        if (textures) {
            if (handle_[k] == -2) handle_[k] = textures->handle(p.material);
            tex_[k] = textures->use(handle_[k]);  // 0 enquanto a textura não chegou: cor fixa
        }
        const GLuint tex = textures ? tex_[k] : 0;
        const bool same = prev < parts_.size() && (tex ? tex == bound && tex_[prev] == tex
                                                       : tex_[prev] == 0 && parts_[prev].material == p.material &&
                                                             parts_[prev].vertex_color == p.vertex_color);
        if (!same) {
            flush();
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
        }
        counts_.push_back(p.count);
        offsets_.push_back(reinterpret_cast<const void*>(p.first_index * sizeof(std::uint32_t)));
        prev = k;
    }
    flush();
    glBindVertexArray(0);
}

}  // namespace dr2::render
