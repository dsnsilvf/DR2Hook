#include "render/instances.hpp"

#include "render/texture.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace dr2::render {

namespace {

struct Vertex {
    float pos[3];
    float uv[2];
};

}  // namespace

Layer layer_of(const std::string& name) {
    if (!name.empty() && name[0] == 't') return name.find("_dist_") != std::string::npos ? Layer::Dist : Layer::Tree;
    return Layer::Obj;
}

InstanceRenderer::InstanceRenderer(const Track& track, const std::vector<Mesh>& objects, const Instances& inst) {
    // biblioteca: todas as malhas de objects.bin num VBO/IBO, faixa de índices por malha
    std::vector<Vertex> verts;
    std::vector<std::uint32_t> idx;
    std::vector<std::size_t> first(objects.size()), lo_v(objects.size());
    for (std::size_t k = 0; k < objects.size(); ++k) {
        const Mesh& m = objects[k];
        const auto offset = static_cast<std::uint32_t>(verts.size());
        lo_v[k] = verts.size();
        for (std::uint32_t i = 0; i < m.verts; ++i) {
            Vertex v{};
            std::memcpy(v.pos, &m.pos[3 * i], sizeof v.pos);
            std::memcpy(v.uv, &m.uv[2 * i], sizeof v.uv);
            verts.push_back(v);
        }
        first[k] = idx.size();
        for (std::uint32_t i : m.idx) idx.push_back(i + offset);
    }
    vbo_.upload(GL_ARRAY_BUFFER, verts.data(), verts.size() * sizeof(Vertex));

    const glm::vec3 base(0.8f, 0.75f, 0.7f);
    types_.resize(track.types.size());
    for (std::size_t t = 0; t < track.types.size(); ++t) {
        const TypeInfo& info = track.types[t];
        Type& ty = types_[t];
        ty.name = info.name;
        ty.layer = layer_of(info.name);
        constexpr float inf = std::numeric_limits<float>::infinity();
        ty.lo = glm::vec3(inf);
        ty.hi = glm::vec3(-inf);
        for (std::size_t k = info.first; k < info.first + info.count && k < objects.size(); ++k) {
            const Mesh& m = objects[k];
            if (m.idx.empty()) continue;
            ty.parts.push_back({first[k], static_cast<GLsizei>(m.idx.size()), m.material, material_color(m.material, base)});
            for (std::uint32_t i = 0; i < m.verts; ++i) {
                const glm::vec3 p(m.pos[3 * i], m.pos[3 * i + 1], m.pos[3 * i + 2]);
                ty.lo = glm::min(ty.lo, p);
                ty.hi = glm::max(ty.hi, p);
            }
        }
        ty.empty = ty.parts.empty();
        if (ty.empty) ty.lo = ty.hi = glm::vec3(0.0f);
    }
    regroup(inst);

    // um VAO por tipo: malha (por vértice) + instâncias (divisor 1); o IBO é o mesmo
    vaos_.resize(types_.size());
    ibufs_.resize(types_.size());
    for (std::size_t t = 0; t < types_.size(); ++t) {
        vaos_[t].bind();
        glBindBuffer(GL_ARRAY_BUFFER, vbo_.id());
        glEnableVertexAttribArray(kPos);
        glVertexAttribPointer(kPos, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, pos)));
        glEnableVertexAttribArray(kUv);
        glVertexAttribPointer(kUv, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, uv)));
        glBindBuffer(GL_ARRAY_BUFFER, ibufs_[t].id());
        const GLuint rows[4] = {kRowX, kRowY, kRowZ, kRowP};
        for (int k = 0; k < 4; ++k) {
            glEnableVertexAttribArray(rows[k]);
            glVertexAttribPointer(rows[k], 3, GL_FLOAT, GL_FALSE, kInstFloats * sizeof(float),
                                  reinterpret_cast<void*>(k * 3 * sizeof(float)));
            glVertexAttribDivisor(rows[k], 1);
        }
        if (t == 0) ibo_.upload(GL_ELEMENT_ARRAY_BUFFER, idx.data(), idx.size() * sizeof(std::uint32_t));
        else glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_.id());
    }
    single_.bind();
    glBindBuffer(GL_ARRAY_BUFFER, vbo_.id());
    glEnableVertexAttribArray(kPos);
    glVertexAttribPointer(kPos, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, pos)));
    glEnableVertexAttribArray(kUv);
    glVertexAttribPointer(kUv, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, uv)));
    if (types_.empty()) ibo_.upload(GL_ELEMENT_ARRAY_BUFFER, idx.data(), idx.size() * sizeof(std::uint32_t));
    else glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_.id());
    glBindVertexArray(0);
}

void InstanceRenderer::regroup(const Instances& inst) {
    for (Type& ty : types_) ty.group.clear();
    for (std::uint32_t i = 0; i < inst.n; ++i) types_[inst.type[i]].group.push_back(i);
    key_.clear();
}

bool InstanceRenderer::passes(const Instances& inst, std::size_t i, const glm::vec3& target, float draw_dist,
                              const Layers& layers) const {
    const Type& ty = types_[inst.type[i]];
    if (ty.empty || !layers.on(ty.layer) || inst.hidden[i]) return false;
    if (ty.layer == Layer::Dist) return true;
    const float* m = inst.matrix(i);
    const float dx = m[9] - target.x, dz = m[11] - target.z;
    return dx * dx + dz * dz <= draw_dist * draw_dist;
}

void InstanceRenderer::cull(const Instances& inst, const glm::vec3& target, float cam_dist, float draw_dist,
                            const Layers& layers, unsigned edit_rev) {
    char key[160];
    (void)cam_dist;  // o corte é pelo alvo; o zoom não muda o que passa
    std::snprintf(key, sizeof key, "%ld,%ld,%ld,%g,%d%d%d,%u", std::lround(target.x / 8), std::lround(target.y / 8),
                  std::lround(target.z / 8), static_cast<double>(draw_dist), layers.obj, layers.tree, layers.dist, edit_rev);
    if (key_ == key) return;
    key_ = key;
    visible_ = 0;
    for (std::size_t t = 0; t < types_.size(); ++t) {
        Type& ty = types_[t];
        ty.visible = 0;
        if (ty.empty || !layers.on(ty.layer)) continue;
        scratch_.clear();
        for (std::uint32_t i : ty.group) {
            if (!passes(inst, i, target, draw_dist, layers)) continue;
            const float* m = inst.matrix(i);
            scratch_.insert(scratch_.end(), m, m + kInstFloats);
        }
        ty.visible = static_cast<GLsizei>(scratch_.size() / kInstFloats);
        visible_ += static_cast<std::size_t>(ty.visible);
        if (ty.visible) ibufs_[t].upload(GL_ARRAY_BUFFER, scratch_.data(), scratch_.size() * sizeof(float), GL_DYNAMIC_DRAW);
    }
}

void InstanceRenderer::draw_parts(const TrackShader& shader, TextureCache& textures, const Type& ty, GLsizei count) const {
    for (const Part& p : ty.parts) {
        const GLuint tex = textures.for_material(p.material);
        const bool cut = p.material.empty() || p.material[0] != 'g';
        if (tex) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex);
        }
        shader.set_texture(tex != 0, cut);
        shader.set_color(p.color);
        const void* offset = reinterpret_cast<void*>(p.first_index * sizeof(std::uint32_t));
        if (count > 0) glDrawElementsInstanced(GL_TRIANGLES, p.count, GL_UNSIGNED_INT, offset, count);
        else glDrawElements(GL_TRIANGLES, p.count, GL_UNSIGNED_INT, offset);
    }
}

void InstanceRenderer::draw(const TrackShader& shader, TextureCache& textures) const {
    shader.set_highlight(0.0f);
    shader.set_vertex_color(false);
    for (std::size_t t = 0; t < types_.size(); ++t) {
        if (!types_[t].visible) continue;
        vaos_[t].bind();
        draw_parts(shader, textures, types_[t], types_[t].visible);
    }
    glBindVertexArray(0);
}

void InstanceRenderer::draw_one(const TrackShader& shader, TextureCache& textures, const Instances& inst, std::size_t i,
                                float hi) const {
    const Type& ty = types_[inst.type[i]];
    if (ty.empty) return;
    single_.bind();
    TrackShader::constant_rows(inst.matrix(i));
    shader.set_highlight(hi);
    shader.set_vertex_color(false);
    // o realce redesenha o objeto na mesma profundidade: com GL_LESS ele perderia para o próprio objeto
    glDepthFunc(GL_LEQUAL);
    draw_parts(shader, textures, ty, 0);
    glDepthFunc(GL_LESS);
    shader.set_highlight(0.0f);
    TrackShader::identity_rows();
    glBindVertexArray(0);
}

}  // namespace dr2::render
