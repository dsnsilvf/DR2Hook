#include "render/lines.hpp"

#include <algorithm>
#include <cmath>

namespace dr2::render {

namespace {

const glm::vec3 kTrackside{1.0f, 0.55f, 0.15f}, kStatic{0.3f, 0.9f, 1.0f}, kDolly{0.95f, 0.35f, 0.9f},
    kTarget{1.0f, 0.75f, 0.95f}, kZone{1.0f, 0.9f, 0.2f}, kLapZone{0.4f, 1.0f, 0.9f}, kBound{0.9f, 0.25f, 0.25f};

// vagas de largada, pela grade: a do contra-relógio (onde o carro nasce no treino) em verde vivo
glm::vec3 grid_color(const std::string& name) {
    if (name.rfind("grid_time_trial", 0) == 0) return {0.2f, 1.0f, 0.35f};
    if (name.rfind("grid_near_reset", 0) == 0) return {0.55f, 0.65f, 0.8f};
    if (name.rfind("grid_start_standing", 0) == 0) return {0.3f, 0.7f, 1.0f};
    if (name.rfind("grid_start_staggered", 0) == 0) return {0.75f, 0.5f, 1.0f};
    return {0.85f, 0.7f, 0.45f};
}

glm::vec3 v(const Vec3& p) { return {p[0], p[1], p[2]}; }
Vec3 a(const glm::vec3& p) { return {p.x, p.y, p.z}; }

// Pontos de curvas de Bézier cúbicas em grupos de 4 (o formato do replay do jogo).
std::vector<Vec3> bezier(const std::vector<Vec3>& ctl) {
    std::vector<Vec3> out;
    for (std::size_t g = 0; g + 3 < ctl.size(); g += 4) {
        const glm::vec3 p0 = v(ctl[g]), p1 = v(ctl[g + 1]), p2 = v(ctl[g + 2]), p3 = v(ctl[g + 3]);
        for (int k = (g == 0 ? 0 : 1); k <= 16; ++k) {
            const float t = static_cast<float>(k) / 16.0f, u = 1.0f - t;
            out.push_back(a(u * u * u * p0 + 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t * p3));
        }
    }
    return out;
}

// Eixos da câmera que olha de `pos` para `aim`: frente, direita e cima (cima perto do +Y do mundo).
void camera_axes(const Vec3& pos, const Vec3& aim, glm::vec3& f, glm::vec3& r, glm::vec3& u) {
    f = v(aim) - v(pos);
    if (glm::length(f) < 1e-3f) f = {0.0f, 0.0f, 1.0f};
    f = glm::normalize(f);
    r = glm::cross(f, glm::vec3(0.0f, 1.0f, 0.0f));
    r = glm::length(r) < 1e-3f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::normalize(r);
    u = glm::cross(r, f);
}

// Ícone de câmera em arame, como os dos editores (Unity, Godot): corpo, lente que abre para a frente com a
// boca em `pos`, e dois rolos de filme em cima (dizem onde é o alto). Uns 2,4 m de comprimento, em pares de
// GL_LINES.
std::vector<Vec3> camera_icon(const Vec3& pos, const Vec3& aim) {
    glm::vec3 f, r, u;
    camera_axes(pos, aim, f, r, u);
    const glm::vec3 o = v(pos);
    std::vector<Vec3> out;
    auto line = [&](const glm::vec3& p, const glm::vec3& q) {
        out.push_back(a(p));
        out.push_back(a(q));
    };
    // um quadro w × h perpendicular à frente, a `d` de `pos` (negativo = para trás), e o ponto k (0..3) dele
    auto corner = [&](float d, float w, float h, int k) {
        const float sr = (k == 0 || k == 3) ? -0.5f : 0.5f, su = k < 2 ? -0.5f : 0.5f;
        return o + f * d + r * (w * sr) + u * (h * su);
    };
    auto prism = [&](float d0, float w0, float h0, float d1, float w1, float h1) {
        for (int k = 0; k < 4; ++k) {
            line(corner(d0, w0, h0, k), corner(d0, w0, h0, (k + 1) % 4));
            line(corner(d1, w1, h1, k), corner(d1, w1, h1, (k + 1) % 4));
            line(corner(d0, w0, h0, k), corner(d1, w1, h1, k));
        }
    };
    prism(-2.4f, 0.9f, 1.1f, -0.7f, 0.9f, 1.1f);  // corpo
    prism(-0.7f, 0.45f, 0.45f, 0.0f, 0.9f, 0.7f);  // lente
    // rolos: círculos no plano frente-cima, apoiados no teto do corpo, com o eixo marcado
    const float radius = 0.42f;
    for (const float d : {-1.95f, -1.1f}) {
        const glm::vec3 c = o + f * d + u * (0.55f + radius);
        constexpr int n = 20;
        for (int k = 0; k < n; ++k) {
            const float t0 = 6.2831853f * static_cast<float>(k) / n, t1 = 6.2831853f * static_cast<float>(k + 1) / n;
            line(c + (f * std::cos(t0) + u * std::sin(t0)) * radius, c + (f * std::cos(t1) + u * std::sin(t1)) * radius);
        }
        line(c - f * (radius * 0.35f), c + f * (radius * 0.35f));
        line(c - u * (radius * 0.35f), c + u * (radius * 0.35f));
    }
    return out;
}

// Cone de visão (pirâmide de 4 m) de `pos` para `aim`, com um tique em cima, em pares de GL_LINES.
std::vector<Vec3> frustum(const Vec3& pos, const Vec3& aim) {
    const glm::vec3 o = v(pos);
    glm::vec3 f, r, u;
    camera_axes(pos, aim, f, r, u);
    const glm::vec3 c = o + f * 4.0f;
    const glm::vec3 k[4] = {c - r * 2.0f - u * 1.3f, c + r * 2.0f - u * 1.3f, c + r * 2.0f + u * 1.3f, c - r * 2.0f + u * 1.3f};
    std::vector<Vec3> out;
    for (int i = 0; i < 4; ++i) {
        out.push_back(pos);
        out.push_back(a(k[i]));
        out.push_back(a(k[i]));
        out.push_back(a(k[(i + 1) % 4]));
    }
    out.push_back(a(c + u * 1.3f));
    out.push_back(a(c + u * 2.0f));
    return out;
}

// Caixa do carro (largura × comprimento, de 0,5 m abaixo do centro a 0,9 m acima), seta no teto e mastro de 2,5 m.
std::vector<Vec3> slot_box(const GridSlot& s) {
    const glm::vec3 c = v(s.pos);
    glm::vec3 f = v(s.fwd);
    if (glm::length(f) < 1e-3f) f = {0.0f, 0.0f, 1.0f};
    f = glm::normalize(f);
    glm::vec3 r = glm::cross(f, glm::vec3(0.0f, 1.0f, 0.0f));
    r = glm::length(r) < 1e-3f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::normalize(r);
    const glm::vec3 u = glm::cross(r, f);
    const float hw = s.width / 2, hl = s.length / 2;
    const glm::vec3 lo = c - u * 0.5f, hi = c + u * 0.9f;
    const glm::vec3 k[4] = {r * hw + f * hl, -r * hw + f * hl, -r * hw - f * hl, r * hw - f * hl};
    std::vector<Vec3> out;
    auto line = [&](const glm::vec3& p, const glm::vec3& q) {
        out.push_back(a(p));
        out.push_back(a(q));
    };
    for (int i = 0; i < 4; ++i) {
        line(lo + k[i], lo + k[(i + 1) % 4]);
        line(hi + k[i], hi + k[(i + 1) % 4]);
        line(lo + k[i], hi + k[i]);
    }
    const glm::vec3 tip = hi + f * hl, back = hi - f * (hl - 0.6f);
    line(back, tip);
    line(tip, hi + f * (hl - 1.6f) + r * hw * 0.8f);
    line(tip, hi + f * (hl - 1.6f) - r * hw * 0.8f);
    line(hi, hi + u * 2.5f);
    return out;
}

// Nó de apoio (sem carro): cruz de 1 m e um traço de 2 m para a frente.
std::vector<Vec3> marker(const GridSlot& s) {
    const glm::vec3 c = v(s.pos);
    const glm::vec3 f = glm::length(v(s.fwd)) > 1e-3f ? glm::normalize(v(s.fwd)) : glm::vec3(0.0f, 0.0f, 1.0f);
    return {a(c - glm::vec3(1, 0, 0)), a(c + glm::vec3(1, 0, 0)), a(c - glm::vec3(0, 0, 1)), a(c + glm::vec3(0, 0, 1)),
            a(c - glm::vec3(0, 0.5f, 0)), a(c + glm::vec3(0, 1.5f, 0)), a(c), a(c + f * 2.0f)};
}

}  // namespace

RouteLines::RouteLines(const Route& route) {
    std::vector<float> data;
    auto add = [&](const std::vector<Vec3>& pts, glm::vec3 color, GLenum mode, Kind kind, int cam = -1, bool frustum = false,
                   bool sel_only = false) {
        if (pts.size() < 2) return;
        strips_.push_back({static_cast<GLint>(data.size() / 3), static_cast<GLsizei>(pts.size()), mode, color, kind, cam, frustum,
                           sel_only});
        for (const Vec3& p : pts) data.insert(data.end(), p.begin(), p.end());
    };
    // as rotas repetem a contagem de distância: um recuo quebra a linha
    std::vector<const Gate*> seg;
    auto flush = [&] {
        if (seg.size() > 1) {
            std::vector<Vec3> l, r, rungs;
            for (const Gate* g : seg) {
                l.push_back(g->l);
                r.push_back(g->r);
                rungs.push_back(g->l);
                rungs.push_back(g->r);
            }
            add(l, {0.95f, 0.4f, 0.4f}, GL_LINE_STRIP, Kind::Gate);
            add(r, {0.4f, 0.6f, 1.0f}, GL_LINE_STRIP, Kind::Gate);
            add(rungs, {0.5f, 0.5f, 0.55f}, GL_LINES, Kind::Gate);
        }
        seg.clear();
    };
    double last = -1.0;
    for (const Gate& g : route.gates) {
        if (g.d < last) flush();
        seg.push_back(&g);
        last = g.d;
    }
    flush();
    for (const AiLine& a : route.ai)
        add(a.pts, a.name == "default" ? glm::vec3(0.3f, 0.95f, 0.4f) : glm::vec3(0.95f, 0.8f, 0.25f), GL_LINE_STRIP, Kind::Ai);

    const Replay& rep = route.replay;
    for (std::size_t i = 0; i < rep.cameras.size(); ++i) {
        const ReplayCamera& c = rep.cameras[i];
        const int ci = static_cast<int>(i);
        const glm::vec3 color = c.kind == "dolly" ? kDolly : c.kind == "static" ? kStatic : kTrackside;
        add(camera_icon(c.pos, c.aim), color, GL_LINES, Kind::Replay, ci, true);
        add(frustum(c.pos, c.aim), color, GL_LINES, Kind::Replay, ci, true, true);  // o cone só na câmera em destaque
        add({c.pos, c.aim}, color * 0.5f, GL_LINES, Kind::Replay, ci);
        add(bezier(c.path), color, GL_LINE_STRIP, Kind::Replay, ci);
        add(bezier(c.target), kTarget, GL_LINE_STRIP, Kind::Replay, ci);
        if (!c.path.empty() && !c.target.empty()) {
            add({c.path.back(), c.target.back()}, kTarget * 0.5f, GL_LINES, Kind::Replay, ci);
        }
    }
    for (const ReplayZone& z : rep.zones) {
        const int ci = z.sw.empty() ? -1 : rep.find(z.sw.front().camera);
        const glm::vec3 color = z.lap > 0 ? kLapZone : kZone;
        Vec3 lt = z.l, rt = z.r;
        lt[1] += 3.0f;
        rt[1] += 3.0f;
        add({z.l, z.r, rt, lt, z.l}, color, GL_LINE_STRIP, Kind::Replay, ci);
        // da zona até cada câmera do replay que ela pode ligar
        const Vec3 mid = {(lt[0] + rt[0]) / 2, lt[1], (lt[2] + rt[2]) / 2};
        for (const ReplaySwitch& w : z.sw)
            if (const int k = rep.find(w.camera); k >= 0)
                add({mid, rep.cameras[static_cast<std::size_t>(k)].pos}, color * 0.45f, GL_LINES, Kind::Replay, k);
    }
    for (const ReplayBound& b : rep.bounds) {
        const float top = std::min(b.y1, b.y0 + 12.0f);  // os prismas sobem 100 m; desenha só o pé
        std::vector<Vec3> e;
        for (int k = 0; k < 4; ++k) {
            const auto& p = b.corners[static_cast<std::size_t>(k)];
            const auto& q = b.corners[static_cast<std::size_t>((k + 1) % 4)];
            for (float y : {b.y0, top}) {
                e.push_back({p[0], y, p[1]});
                e.push_back({q[0], y, q[1]});
            }
            e.push_back({p[0], b.y0, p[1]});
            e.push_back({p[0], top, p[1]});
        }
        add(e, kBound, GL_LINES, Kind::Replay);
    }

    int flat = 0;
    for (const Grid& g : route.grids) {
        const glm::vec3 color = grid_color(g.name);
        for (const GridSlot& s : g.slots) add(slot_box(s), color, GL_LINES, Kind::Grid, flat++, true);
        for (const GridSlot& m : g.markers) add(marker(m), color * 0.6f, GL_LINES, Kind::Grid);
    }

    vao_.bind();
    vbo_.upload(GL_ARRAY_BUFFER, data.data(), data.size() * sizeof(float));
    glEnableVertexAttribArray(kPos);
    glVertexAttribPointer(kPos, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

void RouteLines::draw(const TrackShader& shader, const LinesShow& show) const {
    vao_.bind();
    TrackShader::identity_rows();
    shader.set_line(true);
    glDisable(GL_DEPTH_TEST);
    for (const Strip& s : strips_) {
        const bool grid = s.kind == Kind::Grid;
        if (!(s.kind == Kind::Gate ? show.gates : s.kind == Kind::Ai ? show.ai : grid ? show.grids : show.replay)) continue;
        const int sel = grid ? show.sel_slot : show.sel_cam, inside = grid ? show.inside_slot : show.inside_cam;
        if (s.frustum && inside >= 0 && s.cam == inside) continue;
        if (s.sel_only && (sel < 0 || s.cam != sel)) continue;
        shader.set_color(sel >= 0 && s.cam == sel ? glm::vec3(1.0f) : s.color);
        glDrawArrays(s.mode, s.first, s.count);
    }
    glEnable(GL_DEPTH_TEST);
    shader.set_line(false);
    glBindVertexArray(0);
}

std::vector<glm::vec3> car_box(const glm::vec3& c, const glm::vec3& right, const glm::vec3& up, const glm::vec3& fwd,
                               float width, float length, float below, float above) {
    const float hw = width / 2, hl = length / 2;
    const glm::vec3 lo = c - up * below, hi = c + up * above;
    const glm::vec3 k[4] = {right * hw + fwd * hl, -right * hw + fwd * hl, -right * hw - fwd * hl, right * hw - fwd * hl};
    std::vector<glm::vec3> out;
    auto line = [&](const glm::vec3& p, const glm::vec3& q) {
        out.push_back(p);
        out.push_back(q);
    };
    for (int i = 0; i < 4; ++i) {
        line(lo + k[i], lo + k[(i + 1) % 4]);
        line(hi + k[i], hi + k[(i + 1) % 4]);
        line(lo + k[i], hi + k[i]);
    }
    // frente marcada: X na grade da frente e seta no teto
    line(lo + k[0], hi + k[1]);
    line(lo + k[1], hi + k[0]);
    const glm::vec3 tip = hi + fwd * hl, back = hi - fwd * (hl - 0.6f);
    line(back, tip);
    line(tip, hi + fwd * (hl - 1.4f) + right * hw * 0.8f);
    line(tip, hi + fwd * (hl - 1.4f) - right * hw * 0.8f);
    return out;
}

void DynamicLines::draw(const TrackShader& shader, const std::vector<Group>& groups) {
    std::vector<glm::vec3> all;
    for (const Group& g : groups) all.insert(all.end(), g.points.begin(), g.points.end());
    if (all.empty()) return;
    vao_.bind();
    vbo_.upload(GL_ARRAY_BUFFER, all.data(), all.size() * sizeof(glm::vec3), GL_STREAM_DRAW);
    glEnableVertexAttribArray(kPos);
    glVertexAttribPointer(kPos, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    TrackShader::identity_rows();
    shader.set_line(true);
    GLint first = 0;
    for (const Group& g : groups) {
        if (g.on_top) glDisable(GL_DEPTH_TEST);
        shader.set_color(g.color);
        glDrawArrays(GL_LINES, first, static_cast<GLsizei>(g.points.size()));
        if (g.on_top) glEnable(GL_DEPTH_TEST);
        first += static_cast<GLint>(g.points.size());
    }
    shader.set_line(false);
    glBindVertexArray(0);
}

}  // namespace dr2::render
