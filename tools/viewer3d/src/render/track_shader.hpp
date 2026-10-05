// Programa único da pista, porte do de tvGL (web/js/trackview.js): terreno, objetos instanciados,
// realce e linhas. Os objetos chegam com as linhas da matriz em aX/aY/aZ e a posição em aP
// (vetor-linha); o terreno e as linhas usam esses atributos constantes na identidade.
#pragma once

#include "render/gl.hpp"

#include <glm/glm.hpp>

#include <string>

namespace dr2::render {

enum TrackAttrib : GLuint { kPos = 0, kUv = 1, kCol = 2, kRowX = 3, kRowY = 4, kRowZ = 5, kRowP = 6 };

class TrackShader {
public:
    TrackShader();
    void use() const { program_.use(); }
    void set_view_proj(const glm::mat4& vp) const;
    void set_color(const glm::vec3& c) const;
    void set_highlight(float h) const;
    void set_line(bool on) const;
    void set_texture(bool has, bool cut) const;  // textura na unidade 0; cut = descarta alfa < 0,4
    void set_vertex_color(bool on) const;

    // Atributos de instância constantes (arrays desligados): a matriz de 12 floats do DR2I.
    static void constant_rows(const float* m12);
    static void identity_rows();

private:
    gl::Program program_;
    GLint vp_, color_, hi_, line_, tex_, has_tex_, cut_, vcol_;
};

// Cor de um material sem textura, como tvColor: tom de terra fixo no terreno em lote, senão a base
// variada pelo hash FNV-1a do nome.
glm::vec3 material_color(const std::string& material, const glm::vec3& base);

}  // namespace dr2::render
