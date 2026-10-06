#include "render/gl.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace dr2::gl {

namespace {

GLuint compile_shader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<std::size_t>(len > 1 ? len : 1), '\0');
        glGetShaderInfoLog(sh, len, nullptr, log.data());
        glDeleteShader(sh);
        throw std::runtime_error(std::string(type == GL_VERTEX_SHADER ? "shader de vértice: " : "shader de fragmento: ") +
                                 log.c_str());
    }
    return sh;
}

}  // namespace

GLuint compile_program(const char* vs, const char* fs) {
    GLuint v = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint f = 0;
    try {
        f = compile_shader(GL_FRAGMENT_SHADER, fs);
    } catch (...) {
        glDeleteShader(v);
        throw;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    glDetachShader(program, v);
    glDetachShader(program, f);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<std::size_t>(len > 1 ? len : 1), '\0');
        glGetProgramInfoLog(program, len, nullptr, log.data());
        glDeleteProgram(program);
        throw std::runtime_error("link do programa: " + std::string(log.c_str()));
    }
    return program;
}

void check(const char* where) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        char msg[128];
        std::snprintf(msg, sizeof msg, "erro de GL 0x%04x em %s", err, where);
        throw std::runtime_error(msg);
    }
}

namespace {
int reported = 0;
}

int warned_errors() { return reported; }

bool warn(const char* where) {
    bool ok = true;
    for (GLenum err; (err = glGetError()) != GL_NO_ERROR;) {
        ok = false;
        if (reported < 20) std::fprintf(stderr, "viewer3d: aviso: erro de GL 0x%04x em %s\n", err, where);
        if (++reported == 20) std::fprintf(stderr, "viewer3d: aviso: mais erros de GL; os próximos não serão mostrados\n");
    }
    return ok;
}

void save_ppm(const char* path, int width, int height) {
    if (width <= 0 || height <= 0) throw std::runtime_error("save_ppm: tamanho inválido");
    const std::size_t row = static_cast<std::size_t>(width) * 3;
    std::vector<unsigned char> pixels(row * static_cast<std::size_t>(height));
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    check("glReadPixels");

    std::FILE* fh = std::fopen(path, "wb");
    if (!fh) throw std::runtime_error(std::string("save_ppm: não abriu ") + path);
    std::fprintf(fh, "P6\n%d %d\n255\n", width, height);
    bool ok = true;
    for (int y = height - 1; y >= 0 && ok; --y)  // o GL lê de baixo para cima
        ok = std::fwrite(pixels.data() + row * static_cast<std::size_t>(y), 1, row, fh) == row;
    ok = std::fclose(fh) == 0 && ok;
    if (!ok) throw std::runtime_error(std::string("save_ppm: falha ao gravar ") + path);
}

Buffer& Buffer::operator=(Buffer&& o) noexcept {
    if (this != &o) {
        if (id_) glDeleteBuffers(1, &id_);
        id_ = o.id_;
        o.id_ = 0;
    }
    return *this;
}

void Buffer::upload(GLenum target, const void* data, std::size_t bytes, GLenum usage) {
    glBindBuffer(target, id_);
    glBufferData(target, static_cast<GLsizeiptr>(bytes), data, usage);
    // na carga (estático), falta de memória de vídeo vira erro de carga: a pista não abre, nada fica pela metade
    if (usage != GL_STATIC_DRAW) return;
    bool oom = false;
    for (GLenum err; (err = glGetError()) != GL_NO_ERROR;) oom = oom || err == GL_OUT_OF_MEMORY;  // erro antigo não esconde o OOM
    if (oom) throw std::runtime_error("sem memória de vídeo para " + std::to_string(bytes / (1024 * 1024)) + " MB");
}

Vao& Vao::operator=(Vao&& o) noexcept {
    if (this != &o) {
        if (id_) glDeleteVertexArrays(1, &id_);
        id_ = o.id_;
        o.id_ = 0;
    }
    return *this;
}

}  // namespace dr2::gl
