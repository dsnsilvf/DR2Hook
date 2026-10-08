// Carro do jogo ao vivo: a malha do highLOD exportada por tools/uiview/car/viewer_car.py (car.bin +
// car.json com as texturas de cor), desenhada numa pose (as quatro linhas do TrackShader).
#pragma once

#include "core/dr2m.hpp"
#include "render/gl.hpp"
#include "render/texture.hpp"
#include "render/track_shader.hpp"

#include <glm/glm.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dr2::render {

class CarRenderer {
public:
    // `dir` tem car.bin e car.json. Lança std::runtime_error se falta algum ou se o DR2M não lê.
    explicit CarRenderer(const std::string& dir);
    CarRenderer(const CarRenderer&) = delete;
    CarRenderer& operator=(const CarRenderer&) = delete;

    // Chame no começo do quadro, como o TextureCache da pista.
    void begin_frame() { textures_->begin_frame(); }
    // rows: direita, cima, frente e posição (12 floats); a origem do modelo é o chão sob o carro.
    void draw(const TrackShader& shader, const float* rows) const;

    const std::string& id() const { return id_; }
    std::size_t meshes() const { return parts_.size(); }
    std::size_t verts() const { return verts_; }

private:
    struct Part {
        std::size_t first_index = 0;
        GLsizei count = 0;
        std::string material;
        glm::vec3 color{};
        mutable int tex_handle = -2;
    };
    std::string id_;
    std::map<std::string, std::string, std::less<>> materials_;  // o TextureCache guarda referência
    std::unique_ptr<TextureCache> textures_;
    gl::Buffer vbo_, ibo_;
    gl::Vao vao_;
    std::vector<Part> parts_;
    std::size_t verts_ = 0;
};

// Pasta do carro: `explicit_dir` se não vazio; senão a primeira de <exe>/cars/ que tem car.bin
// (vazio se não há nenhuma).
std::string find_car_dir(const std::string& explicit_dir, const std::string& exe_dir);

}  // namespace dr2::render
