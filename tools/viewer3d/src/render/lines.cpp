#include "render/lines.hpp"

namespace dr2::render {

RouteLines::RouteLines(const Route& route) {
    std::vector<float> data;
    auto add = [&](const std::vector<Vec3>& pts, glm::vec3 color, GLenum mode, bool gate) {
        if (pts.size() < 2) return;
        strips_.push_back({static_cast<GLint>(data.size() / 3), static_cast<GLsizei>(pts.size()), mode, color, gate});
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
            add(l, {0.95f, 0.4f, 0.4f}, GL_LINE_STRIP, true);
            add(r, {0.4f, 0.6f, 1.0f}, GL_LINE_STRIP, true);
            add(rungs, {0.5f, 0.5f, 0.55f}, GL_LINES, true);
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
        add(a.pts, a.name == "default" ? glm::vec3(0.3f, 0.95f, 0.4f) : glm::vec3(0.95f, 0.8f, 0.25f), GL_LINE_STRIP, false);

    vao_.bind();
    vbo_.upload(GL_ARRAY_BUFFER, data.data(), data.size() * sizeof(float));
    glEnableVertexAttribArray(kPos);
    glVertexAttribPointer(kPos, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

void RouteLines::draw(const TrackShader& shader, bool gates, bool ai) const {
    vao_.bind();
    TrackShader::identity_rows();
    shader.set_line(true);
    glDisable(GL_DEPTH_TEST);
    for (const Strip& s : strips_) {
        if (s.gate ? !gates : !ai) continue;
        shader.set_color(s.color);
        glDrawArrays(s.mode, s.first, s.count);
    }
    glEnable(GL_DEPTH_TEST);
    shader.set_line(false);
    glBindVertexArray(0);
}

}  // namespace dr2::render
