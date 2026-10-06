// Auxiliares de OpenGL 3.3 core: programa, buffers e VAO com RAII, checagem de erro e captura do quadro.
#pragma once

#include <GL/glew.h>

#include <cstddef>

namespace dr2::gl {

// Compila e linka; em erro lança std::runtime_error com o log do GL.
GLuint compile_program(const char* vs, const char* fs);

// Lança std::runtime_error se glGetError() não for GL_NO_ERROR.
void check(const char* where);
// Como check, mas só avisa no stderr (até 20 vezes) e devolve false: para o laço de quadros.
bool warn(const char* where);
// Quantos erros de GL warn já viu (os painéis mostram, mesmo depois que o stderr para de avisar).
int warned_errors();

// VRAM da GPU em KB, pelas extensões GL_NVX_gpu_memory_info (NVIDIA) ou GL_ATI_meminfo (AMD): `total` é
// a que o driver oferece, `free` a livre agora (AMD não informa o total: vem 0). false se nenhuma existe
// (Mesa/Intel).
bool vram_kb(std::size_t& total, std::size_t& free);

// Lê o back buffer (RGB) e grava PPM P6, com a primeira linha do arquivo no topo da imagem.
// Chame antes do SDL_GL_SwapWindow do quadro que quer gravar.
void save_ppm(const char* path, int width, int height);

class Buffer {
public:
    Buffer() { glGenBuffers(1, &id_); }
    ~Buffer() { if (id_) glDeleteBuffers(1, &id_); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& o) noexcept : id_(o.id_) { o.id_ = 0; }
    Buffer& operator=(Buffer&& o) noexcept;

    // Vincula a `target` e envia os dados. Para GL_ELEMENT_ARRAY_BUFFER, vincule o VAO antes.
    void upload(GLenum target, const void* data, std::size_t bytes, GLenum usage = GL_STATIC_DRAW);
    GLuint id() const { return id_; }

private:
    GLuint id_ = 0;
};

class Vao {
public:
    Vao() { glGenVertexArrays(1, &id_); }
    ~Vao() { if (id_) glDeleteVertexArrays(1, &id_); }
    Vao(const Vao&) = delete;
    Vao& operator=(const Vao&) = delete;
    Vao(Vao&& o) noexcept : id_(o.id_) { o.id_ = 0; }
    Vao& operator=(Vao&& o) noexcept;

    void bind() const { glBindVertexArray(id_); }
    GLuint id() const { return id_; }

private:
    GLuint id_ = 0;
};

class Program {
public:
    Program(const char* vs, const char* fs) : id_(compile_program(vs, fs)) {}
    ~Program() { if (id_) glDeleteProgram(id_); }
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;

    void use() const { glUseProgram(id_); }
    GLint uniform(const char* name) const { return glGetUniformLocation(id_, name); }
    GLuint id() const { return id_; }

private:
    GLuint id_ = 0;
};

}  // namespace dr2::gl
