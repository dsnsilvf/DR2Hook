// Confere a câmera contra as constantes do Track Explorer web. Executável simples, sem framework.
#undef NDEBUG
#include "render/camera.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

using dr2::render::OrbitCamera;

namespace {

bool near(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; }

}  // namespace

int main() {
    OrbitCamera cam;
    const glm::vec3 eye = cam.eye();
    assert(near(eye.x, 236.8238f));
    assert(near(eye.y, 1655.857f));
    assert(near(eye.z, -199.9933f));
    assert(near(cam.near_plane(), 2.0f));
    assert(near(cam.far_plane(), 20000.0f));

    cam.orbit(100.0f, 0.0f);
    assert(near(cam.yaw, 0.3f));

    for (int i = 0; i < 1000; ++i) cam.orbit(0.0f, 50.0f);
    assert(cam.pitch <= OrbitCamera::kPitchMax && near(cam.pitch, 1.5f));
    for (int i = 0; i < 1000; ++i) cam.orbit(0.0f, -50.0f);
    assert(cam.pitch >= OrbitCamera::kPitchMin && near(cam.pitch, -0.2f));

    for (int i = 0; i < 1000; ++i) cam.zoom(1.0f);
    assert(cam.dist >= OrbitCamera::kDistMin && near(cam.dist, 2.0f));
    for (int i = 0; i < 1000; ++i) cam.zoom(-1.0f);
    assert(cam.dist <= OrbitCamera::kDistMax && near(cam.dist, 15000.0f));

    cam.reset();
    cam.yaw = 0.0f;
    const float z0 = cam.target.z, x0 = cam.target.x;
    cam.walk(1.0f, 0.0f, false, 1.0f / 60.0f);  // W
    assert(cam.target.z < z0);
    assert(near(cam.target.z, z0 - 400.0f * 0.012f));
    assert(near(cam.target.x, x0));
    cam.walk(0.0f, 1.0f, true, 1.0f / 60.0f);  // Shift+D, yaw 0: direita = +x
    assert(near(cam.target.x, x0 + 400.0f * 0.012f * 3.0f));

    cam.reset();
    cam.yaw = 0.0f;
    cam.pan(10.0f, 0.0f);  // arrastar para a direita leva o alvo para -x
    assert(near(cam.target.x, -10.0f * 400.0f * 0.0016f));
    cam.reset();
    assert(near(cam.dist, 400.0f) && near(cam.yaw, 0.8f) && near(cam.target.y, 1430.0f));

    std::puts("camera_test OK");
    return 0;
}
