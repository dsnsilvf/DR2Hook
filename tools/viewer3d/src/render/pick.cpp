#include "render/pick.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace dr2::render {

Ray mouse_ray(const OrbitCamera& cam, float x, float y, float w, float h) {
    const float nx = (x / w) * 2.0f - 1.0f, ny = 1.0f - (y / h) * 2.0f;
    const glm::vec3 eye = cam.eye();
    const glm::vec3 f = glm::normalize(cam.target - eye);
    glm::vec3 r(-f.z, 0.0f, f.x);
    const float rl = glm::length(r);
    r = rl > 0.0f ? r / rl : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 u = glm::cross(r, f);
    const float th = std::tan(OrbitCamera::kFovY / 2.0f), asp = w / h;
    return {eye, glm::normalize(f + r * (nx * th * asp) + u * (ny * th))};
}

bool ground(const Ray& ray, float h, glm::vec3& out) {
    if (std::fabs(ray.d.y) < 1e-6f) return false;
    const float t = (h - ray.o.y) / ray.d.y;
    if (t < 0.0f) return false;
    out = ray.o + ray.d * t;
    return true;
}

namespace {

// Inverso do bloco 3×3 por linhas (a..i), como tvInvAffine; determinante 0 vira 1 (não divide por zero).
void inv_affine(const float* m, float* r) {
    const float a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (det == 0.0f) det = 1.0f;
    r[0] = (e * i - f * h) / det;
    r[1] = (c * h - b * i) / det;
    r[2] = (b * f - c * e) / det;
    r[3] = (f * g - d * i) / det;
    r[4] = (a * i - c * g) / det;
    r[5] = (c * d - a * f) / det;
    r[6] = (d * h - e * g) / det;
    r[7] = (b * g - a * h) / det;
    r[8] = (a * e - b * d) / det;
}

// p_local = v · R⁻¹ (vetor-linha)
glm::vec3 to_local(const glm::vec3& v, const float* inv) {
    return {v.x * inv[0] + v.y * inv[3] + v.z * inv[6], v.x * inv[1] + v.y * inv[4] + v.z * inv[7],
            v.x * inv[2] + v.y * inv[5] + v.z * inv[8]};
}

}  // namespace

int pick(const Ray& ray, const Instances& inst, const InstanceRenderer& objects, const glm::vec3& target, float draw_dist,
         const Layers& layers) {
    int best = -1;
    float bt = std::numeric_limits<float>::infinity();
    const auto& types = objects.types();
    for (std::size_t t = 0; t < types.size(); ++t) {
        const auto& ty = types[t];
        if (ty.empty || !layers.on(ty.layer)) continue;
        for (std::uint32_t i : ty.group) {
            if (!objects.passes(inst, i, target, draw_dist, layers)) continue;
            const float* m = inst.matrix(i);
            float inv[9];
            inv_affine(m, inv);
            const glm::vec3 o = to_local(ray.o - glm::vec3(m[9], m[10], m[11]), inv);
            const glm::vec3 d = to_local(ray.d, inv);
            float t0 = 0.0f, t1 = std::numeric_limits<float>::infinity();
            bool ok = true;
            for (int k = 0; k < 3 && ok; ++k) {
                if (std::fabs(d[k]) < 1e-9f) {
                    if (o[k] < ty.lo[k] || o[k] > ty.hi[k]) ok = false;
                    continue;
                }
                float a = (ty.lo[k] - o[k]) / d[k], c = (ty.hi[k] - o[k]) / d[k];
                if (a > c) std::swap(a, c);
                t0 = std::max(t0, a);
                t1 = std::min(t1, c);
                if (t0 > t1) ok = false;
            }
            if (ok && t0 < bt) {
                bt = t0;
                best = static_cast<int>(i);
            }
        }
    }
    return best;
}

}  // namespace dr2::render
