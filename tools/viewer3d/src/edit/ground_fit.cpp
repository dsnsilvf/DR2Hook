#include "edit/ground_fit.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace dr2::edit {

namespace {

using V3 = std::array<double, 3>;

V3 cross(const V3& a, const V3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

double dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

double len(const V3& a) { return std::sqrt(dot(a, a)); }

V3 unit(const V3& a) {
    const double k = len(a);
    return {a[0] / k, a[1] / k, a[2] / k};
}

double det3(const double a[3][3]) {
    return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
           a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
}

}  // namespace

bool fit_to_ground(const float* m, const Footprint& foot, FitMode mode, const HeightFn& height, float* out,
                   float max_tilt_deg) {
    // escala de cada eixo e rumo: o X local projetado no chão (se ele está de pé, vale o Z)
    const V3 r0{m[0], m[1], m[2]}, r1{m[3], m[4], m[5]}, r2{m[6], m[7], m[8]};
    const double sx = len(r0), sy = len(r1), sz = len(r2);
    if (sx <= 0 || sy <= 0 || sz <= 0) return false;
    V3 hx{r0[0], 0.0, r0[2]};
    if (len(hx) < 1e-6 * sx) {
        const V3 hz{r2[0], 0.0, r2[2]};
        hx = {hz[2], 0.0, -hz[0]};  // X = Y × Z com Y para cima
    }
    hx = unit(hx);
    const V3 hz{-hx[2], 0.0, hx[0]};                          // Z = X × Y
    const bool mirrored = dot(cross(r0, r1), r2) < 0;         // matriz espelhada: mantém o espelho

    const double x = m[9], z = m[11];
    struct S {
        double lx, lz, y;
    };
    std::array<S, 9> s{};
    int k = 0;
    const double xs[3] = {foot.x0 * sx, (foot.x0 + foot.x1) * 0.5 * sx, foot.x1 * sx};
    const double zs[3] = {foot.z0 * sz, (foot.z0 + foot.z1) * 0.5 * sz, foot.z1 * sz};
    for (double lx : xs)
        for (double lz : zs) {
            float y = 0;
            if (!height(static_cast<float>(x + lx * hx[0] + lz * hz[0]), static_cast<float>(z + lx * hx[2] + lz * hz[2]), y))
                return false;
            s[static_cast<std::size_t>(k++)] = {lx, lz, y};
        }

    V3 ex = hx, ey{0.0, 1.0, 0.0};
    double a = 0;
    if (mode == FitMode::Upright) {
        a = std::min_element(s.begin(), s.end(), [](const S& p, const S& q) { return p.y < q.y; })->y;
    } else {
        // y = a + b·lx + d·lz (equações normais, Cramer); a base estreita (placa) ainda tem 3 colunas de pontos
        double n = 9, slx = 0, slz = 0, sly = 0, sxx = 0, szz = 0, sxz = 0, sxy = 0, szy = 0;
        for (const S& p : s) {
            slx += p.lx, slz += p.lz, sly += p.y;
            sxx += p.lx * p.lx, szz += p.lz * p.lz, sxz += p.lx * p.lz;
            sxy += p.lx * p.y, szy += p.lz * p.y;
        }
        const double A[3][3] = {{n, slx, slz}, {slx, sxx, sxz}, {slz, sxz, szz}};
        const double r[3] = {sly, sxy, szy};
        const double D = det3(A);
        double sol[3] = {0, 0, 0};
        if (std::fabs(D) > 1e-12) {
            for (int c = 0; c < 3; ++c) {
                double B[3][3];
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) B[i][j] = j == c ? r[i] : A[i][j];
                sol[c] = det3(B) / D;
            }
        } else {
            sol[0] = sly / n;  // base de largura zero: só a altura média
        }
        a = sol[0];
        double b = sol[1], d = sol[2];
        const double lim = std::tan(static_cast<double>(max_tilt_deg) * 3.14159265358979 / 180.0);
        const double slope = std::hypot(b, d);
        if (slope > lim) b *= lim / slope, d *= lim / slope;
        double above = 0;  // o quanto o plano passa por cima do chão em algum ponto: desce isso
        for (const S& p : s) above = std::max(above, a + b * p.lx + d * p.lz - p.y);
        a -= above;
        const V3 tx{hx[0], b, hx[2]}, tz{hz[0], d, hz[2]};
        ex = unit(tx);
        ey = unit(cross(tz, tx));  // Z × X = Y
    }
    V3 ez = cross(ex, ey);         // X × Y = Z
    if (mirrored) ez = {-ez[0], -ez[1], -ez[2]};
    const float res[kInstFloats] = {
        static_cast<float>(ex[0] * sx), static_cast<float>(ex[1] * sx), static_cast<float>(ex[2] * sx),
        static_cast<float>(ey[0] * sy), static_cast<float>(ey[1] * sy), static_cast<float>(ey[2] * sy),
        static_cast<float>(ez[0] * sz), static_cast<float>(ez[1] * sz), static_cast<float>(ez[2] * sz),
        m[9], static_cast<float>(a), m[11]};
    std::copy(res, res + kInstFloats, out);
    return true;
}

}  // namespace dr2::edit
