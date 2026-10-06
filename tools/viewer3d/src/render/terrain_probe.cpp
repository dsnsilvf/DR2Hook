#include "render/terrain_probe.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace dr2::render {

namespace {

constexpr int kSize = 5;  // o pixel do meio (2, 2) é o raio

const char* const kVs = R"glsl(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uVp;
out vec3 vW;
void main() { vW = aPos; gl_Position = uVp * vec4(aPos, 1.0); }
)glsl";

const char* const kFs = R"glsl(#version 330 core
in vec3 vW;
uniform vec3 uEye;
out float oDist;
void main() { oDist = length(vW - uEye); }
)glsl";

constexpr float kNoHit = 1e30f;

}  // namespace

TerrainProbe::TerrainProbe() : program_(kVs, kFs), vp_(program_.uniform("uVp")), eye_(program_.uniform("uEye")) {
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, kSize, kSize, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo_);
    GLint prev = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_, 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev));
    if (status != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("TerrainProbe: framebuffer R32F incompleto");
}

TerrainProbe::~TerrainProbe() {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (tex_) glDeleteTextures(1, &tex_);
}

bool TerrainProbe::cast(const Terrain& terrain, const glm::vec3& o, const glm::vec3& d, float max_dist, float& out) {
    if (!(max_dist > 0.0f) || !std::isfinite(max_dist) || !std::isfinite(o.x + o.y + o.z + d.x + d.y + d.z)) return false;
    ++casts_;
    // estado que a sonda mexe, para devolver como estava
    GLint prev_fbo = 0, prev_prog = 0, vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
    glGetIntegerv(GL_VIEWPORT, vp);
    const GLboolean was_depth = glIsEnabled(GL_DEPTH_TEST), was_blend = glIsEnabled(GL_BLEND);
    GLint prev_eq = GL_FUNC_ADD;
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &prev_eq);

    // câmera no olho, olhando ao longo do raio; campo de visão minúsculo: o pixel do meio é o raio
    const glm::vec3 up = std::fabs(d.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    // a direção sai de `d`, não de (o + d) - o: com o a milhares de metros, a soma em float perderia ~2e-4 rad
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), d, up) * glm::translate(glm::mat4(1.0f), -o);
    const float near_plane = 0.01f;
    const glm::mat4 proj = glm::perspective(0.004f, 1.0f, near_plane, max_dist + 1.0f);
    const glm::mat4 vp_mat = proj * view;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, kSize, kSize);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendEquation(GL_MIN);  // vence a menor distância, na ordem que for
    glBlendFunc(GL_ONE, GL_ONE);
    const float clear[4] = {kNoHit, 0, 0, 0};
    glClearBufferfv(GL_COLOR, 0, clear);
    program_.use();
    glUniformMatrix4fv(vp_, 1, GL_FALSE, &vp_mat[0][0]);
    glUniform3f(eye_, o.x, o.y, o.z);
    terrain.draw_probe(vp_mat);
    float dist = kNoHit;
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(kSize / 2, kSize / 2, 1, 1, GL_RED, GL_FLOAT, &dist);

    glBlendEquation(static_cast<GLenum>(prev_eq));
    if (!was_blend) glDisable(GL_BLEND);
    if (was_depth) glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glUseProgram(static_cast<GLuint>(prev_prog));

    if (!(dist < kNoHit * 0.5f) || dist > max_dist) return false;
    out = dist;
    return true;
}

bool TerrainProbe::height(const Terrain& terrain, float x, float z, float y_from, float& out_y) {
    float t = 0;
    if (cast(terrain, {x, y_from, z}, {0.0f, -1.0f, 0.0f}, 20000.0f, t)) {
        out_y = y_from - t;
        return true;
    }
    // nada abaixo (o objeto está enterrado ou fora): vê o terreno de cima
    const float top = y_from + 3000.0f;
    if (cast(terrain, {x, top, z}, {0.0f, -1.0f, 0.0f}, 23000.0f, t)) {
        out_y = top - t;
        return true;
    }
    return false;
}

}  // namespace dr2::render
