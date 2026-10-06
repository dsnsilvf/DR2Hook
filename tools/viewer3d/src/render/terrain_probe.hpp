// Sonda de raio contra o terreno, na GPU: renderiza só as malhas que o raio atravessa, num alvo de
// 5×5 pixels com um frustum estreito ao longo do raio, e guarda a MENOR distância do olho (mistura
// GL_MIN em R32F, sem buffer de profundidade, então sem a imprecisão de profundidade de 24 bits a
// quilômetros). Serve para o picking não atravessar morros e para assentar objetos no chão.
// Não guarda cópia do terreno na RAM (importa nas pistas de 10 milhões de vértices).
#pragma once

#include "render/gl.hpp"
#include "render/terrain.hpp"

#include <glm/glm.hpp>

namespace dr2::render {

class TerrainProbe {
public:
    TerrainProbe();
    ~TerrainProbe();
    TerrainProbe(const TerrainProbe&) = delete;
    TerrainProbe& operator=(const TerrainProbe&) = delete;

    // Distância (em metros, ao longo de `d`, unitária) de `o` ao primeiro ponto do terreno em
    // (0, max_dist]; false se o raio não encontra terreno. Restaura o estado de GL que mexe.
    bool cast(const Terrain& terrain, const glm::vec3& o, const glm::vec3& d, float max_dist, float& out);

    // Altura (y) do terreno no ponto (x, z): primeiro tenta de `y_from` para baixo (o que fica acima
    // de `y_from` não conta: ponte, galeria), depois, se não achou, do alto. false se não há terreno ali.
    bool height(const Terrain& terrain, float x, float z, float y_from, float& out_y);

    std::size_t casts() const { return casts_; }

private:
    GLuint fbo_ = 0, tex_ = 0;
    gl::Program program_;
    GLint vp_, eye_;
    std::size_t casts_ = 0;
};

}  // namespace dr2::render
