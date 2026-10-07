// Alinhar ao terreno: a matriz de uma instância acompanha o chão sob a base do tipo. Só dados: a altura
// do chão vem de quem chama (no viewer, a sonda de GPU). Mesma conta de tools/synthtrack/meshes.ground_fit.
#pragma once

#include "core/dr2i.hpp"

#include <functional>

namespace dr2::edit {

enum class FitMode {
    Tilt,     // inclina junto com o chão (plano por mínimos quadrados, até max_tilt) e desce o que ficar no ar
    Upright,  // fica em pé (prédio, placa, árvore) e desce até o ponto mais baixo do chão sob a base
};

// Base local do tipo vista de cima: x0..x1 e z0..z1 (sem escala; a escala vem da matriz).
struct Footprint {
    float x0 = -0.5f, x1 = 0.5f, z0 = -0.5f, z1 = 0.5f;
};

// Altura do chão em (x, z); false se não há terreno ali.
using HeightFn = std::function<bool(float x, float z, float& y)>;

// Olha o chão em 3×3 pontos da base (cantos, meios e centro), em volta da posição atual e com o rumo
// (giro em y) e a escala de cada eixo de `m`. Grava o resultado em `out` (pode ser o próprio `m`).
// false (e `out` intacto) se algum ponto não tem terreno.
bool fit_to_ground(const float* m, const Footprint& foot, FitMode mode, const HeightFn& height, float* out,
                   float max_tilt_deg = 25.0f);

}  // namespace dr2::edit
