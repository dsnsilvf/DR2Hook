#include "render/car.hpp"

#include "core/io.hpp"
#include "core/json.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace dr2::render {

namespace {

struct Vertex {
    float pos[3];
    float uv[2];
};

}  // namespace

CarRenderer::CarRenderer(const std::string& dir) {
    const std::vector<Mesh> meshes = read_dr2m(read_file(join_path(dir, "car.bin")));
    const json::Value doc = json::parse_file(join_path(dir, "car.json"));
    if (const json::Value* id = doc.find("id"); id && id->is_string()) id_ = id->as_string();
    if (const json::Value* mats = doc.find("materials"); mats && mats->is_object())
        for (const auto& [key, file] : mats->as_object())
            if (file.is_string()) materials_.emplace(key, file.as_string());
    textures_ = std::make_unique<TextureCache>(dir, materials_);

    std::vector<Vertex> verts;
    std::vector<std::uint32_t> idx;
    const glm::vec3 base(0.75f, 0.75f, 0.78f);
    for (const Mesh& m : meshes) {
        if (m.idx.empty()) continue;
        const auto offset = static_cast<std::uint32_t>(verts.size());
        for (std::uint32_t i = 0; i < m.verts; ++i) {
            Vertex v{};
            std::memcpy(v.pos, &m.pos[3 * i], sizeof v.pos);
            std::memcpy(v.uv, &m.uv[2 * i], sizeof v.uv);
            verts.push_back(v);
        }
        parts_.push_back({idx.size(), static_cast<GLsizei>(m.idx.size()), m.material, material_color(m.material, base), -2});
        for (std::uint32_t i : m.idx) idx.push_back(i + offset);
    }
    verts_ = verts.size();
    vao_.bind();
    vbo_.upload(GL_ARRAY_BUFFER, verts.data(), verts.size() * sizeof(Vertex));
    glEnableVertexAttribArray(kPos);
    glVertexAttribPointer(kPos, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, pos)));
    glEnableVertexAttribArray(kUv);
    glVertexAttribPointer(kUv, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, uv)));
    ibo_.upload(GL_ELEMENT_ARRAY_BUFFER, idx.data(), idx.size() * sizeof(std::uint32_t));
    glBindVertexArray(0);
}

void CarRenderer::draw(const TrackShader& shader, const float* rows) const {
    vao_.bind();
    TrackShader::constant_rows(rows);
    shader.set_highlight(0.0f);
    shader.set_vertex_color(false);
    for (const Part& p : parts_) {
        if (p.tex_handle == -2) p.tex_handle = textures_->handle(p.material);
        const GLuint tex = textures_->use(p.tex_handle);
        if (tex) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex);
        }
        shader.set_texture(tex != 0, true);
        shader.set_color(p.color);
        glDrawElements(GL_TRIANGLES, p.count, GL_UNSIGNED_INT, reinterpret_cast<void*>(p.first_index * sizeof(std::uint32_t)));
    }
    TrackShader::identity_rows();
    glBindVertexArray(0);
}

std::string find_car_dir(const std::string& explicit_dir, const std::string& exe_dir) {
    if (!explicit_dir.empty()) return explicit_dir;
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> found;
    for (const auto& e : fs::directory_iterator(fs::path(exe_dir) / "cars", ec))
        if (fs::exists(e.path() / "car.bin", ec)) found.push_back(e.path());
    if (found.empty()) return {};
    std::sort(found.begin(), found.end());
    return found.front().generic_string();
}

}  // namespace dr2::render
