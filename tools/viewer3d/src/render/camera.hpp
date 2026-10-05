// Câmera orbital com as mesmas constantes e controles do Track Explorer web
// (tvCam, tvVp, tvKeys e os manipuladores de ponteiro em web/js/trackview.js).
// Só depende de GLM: quem lê eventos (app) traduz a entrada para estas chamadas.
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace dr2::render {

struct OrbitCamera {
    static constexpr float kFovY = 0.9f;
    static constexpr float kPitchMin = -0.2f, kPitchMax = 1.5f;
    static constexpr float kDistMin = 2.0f, kDistMax = 15000.0f;

    float yaw = 0.8f;
    float pitch = 0.6f;
    float dist = 400.0f;
    glm::vec3 target{0.0f, 1430.0f, -430.0f};

    void reset() { *this = OrbitCamera{}; }

    glm::vec3 eye() const {
        const float cp = std::cos(pitch);
        return target + dist * glm::vec3(cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw));
    }
    float near_plane() const { return std::max(0.3f, dist / 200.0f); }
    float far_plane() const { return std::max(20000.0f, dist * 20.0f); }

    glm::mat4 view() const { return glm::lookAt(eye(), target, glm::vec3(0.0f, 1.0f, 0.0f)); }
    glm::mat4 proj(float aspect) const { return glm::perspective(kFovY, aspect, near_plane(), far_plane()); }

    // Direita e frente no plano xz (o "r" e o "f" do web).
    glm::vec2 right() const { return {std::cos(yaw), -std::sin(yaw)}; }
    glm::vec2 forward() const { return {-std::sin(yaw), -std::cos(yaw)}; }

    // Arraste com o botão esquerdo; dx, dy em pixels desde o evento anterior.
    void orbit(float dx, float dy) {
        yaw -= dx * 0.005f;
        pitch = std::clamp(pitch + dy * 0.005f, kPitchMin, kPitchMax);
    }

    // Botão direito, ou Shift + esquerdo.
    void pan(float dx, float dy) {
        const float k = dist * 0.0016f;
        const glm::vec2 r = right(), f = forward();
        target.x += -r.x * dx * k + f.x * dy * k;
        target.z += -r.y * dx * k + f.y * dy * k;
    }

    // Roda: positivo = para cima/longe do usuário = aproxima.
    void zoom(float wheel) { dist = std::clamp(dist * std::exp(-wheel * 0.12f), kDistMin, kDistMax); }

    // WASD: ahead = W - S, side = D - A. O passo do web é por quadro a 60 Hz; dt em segundos.
    void walk(float ahead, float side, bool fast, float dt) {
        const float step = dist * 0.012f * (fast ? 3.0f : 1.0f) * dt * 60.0f;
        const glm::vec2 r = right(), f = forward();
        target.x += (f.x * ahead + r.x * side) * step;
        target.z += (f.y * ahead + r.y * side) * step;
    }
};

}  // namespace dr2::render
