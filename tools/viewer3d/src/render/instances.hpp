// Objetos da rota: a biblioteca de tipos (objects.bin) num VBO/IBO únicos, um VAO e um buffer de
// instâncias por tipo, corte por distância (tvCull) e desenho instanciado.
#pragma once

#include "core/dr2i.hpp"
#include "core/dr2m.hpp"
#include "core/grid.hpp"
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
        mutable int tex_handle = -2;  // TextureCache::handle, resolvido no primeiro desenho (-2 = ainda não)
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

    // Refaz os grupos por tipo e a grade (outra rota, ou cópias novas) e força o próximo corte.
    void regroup(const Instances& inst);
    // Refaz os buffers só se a chave mudou (alvo em passos de 8 m, raio, camadas, revisão de edição). Só
    // percorre as células da grade perto do alvo; na troca de revisão (desfazer, refazer, abrir edits)
    // confere antes se alguma instância mudou de célula.
    void cull(const Instances& inst, const glm::vec3& target, float cam_dist, float draw_dist, const Layers& layers,
              unsigned edit_rev);
    // A matriz de `i` mudou sem passo de histórico (arraste, campo numérico, gizmo): muda de célula se
    // preciso e, se ela continua visível, reenvia só as 12 floats dela; se entrou ou saiu do corte,
    // o próximo cull refaz os buffers. Vale para o alvo e as camadas do último cull.
    void touch(const Instances& inst, std::uint32_t i);
    // Instância passa no corte atual? (camada ligada, não apagada, dentro do raio salvo `dist`)
    bool passes(const Instances& inst, std::size_t i, const glm::vec3& target, float draw_dist, const Layers& layers) const;
    // Chama fn(i) para cada instância candidata a passar no corte (as das células perto de `target`, mais
    // as do terreno distante); quem chama ainda confere com `passes`. É o que o picking percorre.
    template <class F>
    void for_each_near(const glm::vec3& target, float draw_dist, F&& fn) const {
        grid_.query(target.x, target.z, draw_dist, fn);
        for (std::uint32_t i : grid_.always()) fn(i);
    }

    // Contadores para medir (cortes completos, reenvios parciais, células visitadas no último corte).
    std::size_t culls() const { return culls_; }
    std::size_t partial_updates() const { return partial_; }
    std::size_t last_cells() const { return last_cells_; }
    double cull_seconds() const { return cull_s_; }  // soma dos cortes completos

    void draw(const TrackShader& shader, TextureCache& textures) const;
    // Desenha uma instância só (realce do selecionado), com uHi.
    void draw_one(const TrackShader& shader, TextureCache& textures, const Instances& inst, std::size_t i, float hi) const;

    const std::vector<Type>& types() const { return types_; }
    std::size_t visible() const { return visible_; }

private:
    void draw_parts(const TrackShader& shader, TextureCache& textures, const Type& ty, GLsizei count) const;

    struct CullKey {
        long x = 0, y = 0, z = 0;
        float dist = 0;
        bool obj = false, tree = false, far = false;
        unsigned rev = 0;
        bool operator==(const CullKey&) const = default;
    };

    gl::Buffer vbo_, ibo_;
    std::vector<Type> types_;
    std::vector<gl::Vao> vaos_;       // um por tipo
    std::vector<gl::Buffer> ibufs_;   // instâncias visíveis de cada tipo (12 floats cada)
    gl::Vao single_;                  // uma instância, com as linhas constantes (realce)
    InstanceGrid grid_;
    std::vector<std::uint8_t> in_grid_;               // por tipo: 0 = terreno distante, fora da grade
    std::vector<std::vector<float>> per_type_;        // matrizes visíveis de cada tipo, montadas no corte
    std::vector<std::vector<std::uint32_t>> slots_;   // instância de cada posição do buffer de um tipo
    std::vector<std::int32_t> slot_of_;               // posição de cada instância no buffer (-1 = fora)
    CullKey key_;
    bool have_key_ = false;
    glm::vec3 cut_target_{0.0f};                      // alvo, raio e camadas do último corte (para touch)
    float cut_dist_ = 0;
    Layers cut_layers_;
    std::size_t visible_ = 0, culls_ = 0, partial_ = 0, last_cells_ = 0;
    double cull_s_ = 0.0;
};

}  // namespace dr2::render
