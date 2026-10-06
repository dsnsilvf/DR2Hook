#include "render/track_shader.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <string>

namespace dr2::render {

namespace {

const char* const kVs = R"glsl(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aCol;
layout(location = 3) in vec3 aX;
layout(location = 4) in vec3 aY;
layout(location = 5) in vec3 aZ;
layout(location = 6) in vec3 aP;
uniform mat4 uVp;
out vec3 vW;
out vec2 vUv;
out vec4 vCol;
void main() {
    vec3 w = aX * aPos.x + aY * aPos.y + aZ * aPos.z + aP;
    vW = w;
    vUv = aUv;
    vCol = aCol;
    gl_Position = uVp * vec4(w, 1.0);
}
)glsl";

const char* const kFs = R"glsl(#version 330 core
in vec3 vW;
in vec2 vUv;
in vec4 vCol;
uniform vec3 uColor;
uniform float uHi;
uniform int uLine;
uniform sampler2D uTex;
uniform int uHasTex;
uniform int uCut;
uniform int uVCol;
uniform int uBlend;
out vec4 oColor;
void main() {
    if (uLine == 1) { oColor = vec4(uColor, 1.0); return; }
    vec3 n = normalize(cross(dFdx(vW), dFdy(vW)));
    float light = abs(dot(n, normalize(vec3(0.35, 0.85, 0.4)))) * 0.7 + 0.3;
    vec3 base = uVCol == 1 ? vCol.rgb : uColor;
    float alpha = 1.0;
    if (uHasTex == 1) {
        vec4 tx = texture(uTex, vUv);
        if (uCut == 1 && tx.a < 0.4) discard;
        base = tx.rgb;
        if (uBlend == 1) alpha = tx.a;
        if (uBlend == 2) { oColor = vec4(tx.rgb, 1.0); return; }
    }
    oColor = vec4(mix(base * light, vec3(1.0, 0.6, 0.15), uHi * 0.55), alpha);
}
)glsl";

}  // namespace

TrackShader::TrackShader()
    : program_(kVs, kFs),
      vp_(program_.uniform("uVp")),
      color_(program_.uniform("uColor")),
      hi_(program_.uniform("uHi")),
      line_(program_.uniform("uLine")),
      tex_(program_.uniform("uTex")),
      has_tex_(program_.uniform("uHasTex")),
      cut_(program_.uniform("uCut")),
      vcol_(program_.uniform("uVCol")),
      blend_(program_.uniform("uBlend")) {
    program_.use();
    glUniform1i(tex_, 0);
    glUniform1f(hi_, 0.0f);
    glUniform1i(line_, 0);
    glUniform1i(has_tex_, 0);
    glUniform1i(cut_, 0);
    glUniform1i(vcol_, 0);
    glUniform1i(blend_, 0);
}

void TrackShader::set_view_proj(const glm::mat4& vp) const { glUniformMatrix4fv(vp_, 1, GL_FALSE, glm::value_ptr(vp)); }
void TrackShader::set_color(const glm::vec3& c) const { glUniform3f(color_, c.x, c.y, c.z); }
void TrackShader::set_highlight(float h) const { glUniform1f(hi_, h); }
void TrackShader::set_line(bool on) const { glUniform1i(line_, on ? 1 : 0); }
void TrackShader::set_texture(bool has, bool cut) const {
    glUniform1i(has_tex_, has ? 1 : 0);
    glUniform1i(cut_, cut ? 1 : 0);
}
void TrackShader::set_vertex_color(bool on) const { glUniform1i(vcol_, on ? 1 : 0); }
void TrackShader::set_blend(int mode) const { glUniform1i(blend_, mode); }

void TrackShader::constant_rows(const float* m) {
    glVertexAttrib3f(kRowX, m[0], m[1], m[2]);
    glVertexAttrib3f(kRowY, m[3], m[4], m[5]);
    glVertexAttrib3f(kRowZ, m[6], m[7], m[8]);
    glVertexAttrib3f(kRowP, m[9], m[10], m[11]);
}

void TrackShader::identity_rows() {
    static const float ident[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    constant_rows(ident);
}

glm::vec3 material_color(const std::string& material, const glm::vec3& base) {
    if (material.rfind("g|batchmaterial", 0) == 0) return {0.36f, 0.34f, 0.28f};
    std::uint32_t h = 2166136261u;
    for (unsigned char c : material) {  // o web usa charCodeAt; os nomes são ASCII
        h ^= c;
        h *= 16777619u;
    }
    auto j = [&](int k) { return static_cast<float>((h >> k) & 255u) / 255.0f; };
    return {base.x * (0.75f + 0.5f * j(0)), base.y * (0.75f + 0.5f * j(8)), base.z * (0.75f + 0.5f * j(16))};
}

}  // namespace dr2::render
