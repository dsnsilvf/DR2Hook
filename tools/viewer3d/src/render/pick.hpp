// Raio do mouse e seleção de instância, porte de tvRay, tvGround, tvInvAffine e tvPickIndex.
#pragma once

#include "core/dr2i.hpp"
#include "render/camera.hpp"
#include "render/instances.hpp"

#include <glm/glm.hpp>

#include <limits>

namespace dr2::render {

struct Ray {
    glm::vec3 o, d;  // origem no olho, direção unitária
};

// x, y em pixels da janela (0,0 no canto de cima à esquerda), w, h o tamanho da janela.
Ray mouse_ray(const OrbitCamera& cam, float x, float y, float w, float h);

// Ponto do raio no plano y = h; false se o raio não desce (ou sobe) até ele.
bool ground(const Ray& ray, float h, glm::vec3& out);

// Instância mais perto do olho cuja caixa local do tipo o raio atravessa, entre as que passam no
// corte (camada, apagada, raio de desenho); -1 se nenhuma. `max_t` é a distância do olho até o terreno
// ao longo do raio (infinito se não importa): caixas que o raio só alcança depois dele ficam de fora.
int pick(const Ray& ray, const Instances& inst, const InstanceRenderer& objects, const glm::vec3& target, float draw_dist,
         const Layers& layers, float max_t = std::numeric_limits<float>::infinity());

}  // namespace dr2::render
