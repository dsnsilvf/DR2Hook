// Objetos da rota: a biblioteca de tipos (objects.bin) num VBO/IBO únicos, um VAO e um buffer de
// instâncias por tipo, corte por distância (tvCull) e desenho instanciado.
#pragma once

#include "core/dr2i.hpp"
#include "core/dr2m.hpp"
#include "core/track.hpp"
#include "render/gl.hpp"
#include "render/track_shader.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace dr2::render {

class TextureCache;

enum class Layer { Obj, Tree, Dist };

// Camada de um tipo pelo nome, como tvKind: t: com _dist_ é terreno distante, outro t: é árvore,
// e: e o: são objetos.
Layer layer_of(const std::string& type_name);

struct Layers {
    bool obj = true, tree = true, dist = false;
    bool on(Layer l) const { return l == Layer::Obj ? obj : l == Layer::Tree ? tree : dist; }
};

class InstanceRenderer {
public:
    struct Part {
        std::size_t first_index = 0;
        GLsizei count = 0;
        std::string material;
        glm::vec3 color{};
    };
    struct Type {
        std::string name;
        Layer layer = Layer::Obj;
        std::vector<Part> parts;
        glm::vec3 lo{0.0f}, hi{0.0f};       // caixa local de todas as malhas do tipo
        bool empty = true;                  // sem malha: não desenha nem seleciona
        std::vector<std::uint32_t> group;   // instâncias deste tipo
        GLsizei visible = 0;
    };

    InstanceRenderer(const Track& track, const std::vector<Mesh>& objects, const Instances& inst);
    InstanceRenderer(const InstanceRenderer&) = delete;
    InstanceRenderer& operator=(const InstanceRenderer&) = delete;

    // Refaz os grupos por tipo (outra rota, ou cópias novas) e força o próximo corte.
    void regroup(const Instances& inst);
    // Refaz os buffers só se a chave mudou (alvo e distância em passos de 8 m, raio, camadas, revisão de edição).
    void cull(const Instances& inst, const glm::vec3& target, float cam_dist, float draw_dist, const Layers& layers,
              unsigned edit_rev);
    // Instância passa no corte atual? (camada ligada, não apagada, dentro do raio salvo `dist`)
    bool passes(const Instances& inst, std::size_t i, const glm::vec3& target, float draw_dist, const Layers& layers) const;

    void draw(const TrackShader& shader, TextureCache& textures) const;
    // Desenha uma instância só (realce do selecionado), com uHi.
    void draw_one(const TrackShader& shader, TextureCache& textures, const Instances& inst, std::size_t i, float hi) const;

    const std::vector<Type>& types() const { return types_; }
    std::size_t visible() const { return visible_; }

private:
    void draw_parts(const TrackShader& shader, TextureCache& textures, const Type& ty, GLsizei count) const;

    gl::Buffer vbo_, ibo_;
    std::vector<Type> types_;
    std::vector<gl::Vao> vaos_;       // um por tipo
    std::vector<gl::Buffer> ibufs_;   // instâncias visíveis de cada tipo (12 floats cada)
    gl::Vao single_;                  // uma instância, com as linhas constantes (realce)
    std::vector<float> scratch_;
    std::string key_;
    std::size_t visible_ = 0;
};

}  // namespace dr2::render
