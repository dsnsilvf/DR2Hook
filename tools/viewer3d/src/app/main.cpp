// DR2 Viewer3D: janela SDL3 com contexto OpenGL 3.3 core.
//
//   viewer3d [--frames N] [--screenshot arq.ppm]
//
// Com --frames, roda N quadros, imprime "OK renderer=... gl=... frames=N" e sai com 0.
// Com --screenshot, grava o último quadro em PPM antes de sair.

#include "render/gl.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>

namespace {

struct Options {
    long frames = -1;  // -1 = até fechar a janela
    const char* screenshot = nullptr;
};

bool parse_args(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            char* end = nullptr;
            opt.frames = std::strtol(argv[++i], &end, 10);
            if (*end != '\0' || opt.frames < 1) {
                std::fprintf(stderr, "--frames precisa de um inteiro >= 1\n");
                return false;
            }
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            opt.screenshot = argv[++i];
        } else {
            std::fprintf(stderr, "uso: viewer3d [--frames N] [--screenshot arq.ppm]\n");
            return false;
        }
    }
    if (opt.screenshot && opt.frames < 1) {
        std::fprintf(stderr, "--screenshot precisa de --frames\n");
        return false;
    }
    return true;
}

std::runtime_error sdl_error(const char* step) { return std::runtime_error(std::string(step) + ": " + SDL_GetError()); }

const char* gl_string(GLenum name) {
    const GLubyte* s = glGetString(name);
    return s ? reinterpret_cast<const char*>(s) : "?";
}

// SDL, janela e contexto GL. O destrutor desfaz na ordem inversa.
class Platform {
public:
    Platform() {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw sdl_error("SDL_Init");
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        window_ = SDL_CreateWindow("DR2 Viewer3D", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
        if (!window_) {
            auto err = sdl_error("SDL_CreateWindow");
            release();
            throw err;
        }
        context_ = SDL_GL_CreateContext(window_);
        if (!context_) {
            auto err = sdl_error("SDL_GL_CreateContext (OpenGL 3.3 core)");
            release();
            throw err;
        }
        SDL_GL_SetSwapInterval(1);

        glewExperimental = GL_TRUE;
        GLenum glew = glewInit();
        bool glew_ok = glew == GLEW_OK;
#ifdef GLEW_ERROR_NO_GLX_DISPLAY
        // SDL em Wayland/EGL: o GLEW procura GLX e reclama, mas as funções do GL já foram carregadas.
        glew_ok = glew_ok || glew == GLEW_ERROR_NO_GLX_DISPLAY;
#endif
        if (!glew_ok) {
            std::string msg = "glewInit: " + std::string(reinterpret_cast<const char*>(glewGetErrorString(glew)));
            release();
            throw std::runtime_error(msg);
        }
        glGetError();  // o GLEW provoca GL_INVALID_ENUM em perfil core; descarta
    }
    ~Platform() { release(); }
    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    SDL_Window* window() const { return window_; }

private:
    void release() {
        if (context_) SDL_GL_DestroyContext(context_);
        if (window_) SDL_DestroyWindow(window_);
        context_ = nullptr;
        window_ = nullptr;
        SDL_Quit();
    }

    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
};

const char* const kTriangleVs = R"glsl(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aCol;
uniform float uAspect;
out vec3 vCol;
void main() { vCol = aCol; gl_Position = vec4(aPos.x / uAspect, aPos.y, 0.0, 1.0); }
)glsl";

const char* const kTriangleFs = R"glsl(#version 330 core
in vec3 vCol;
out vec4 oColor;
void main() { oColor = vec4(vCol, 1.0); }
)glsl";

// Triângulo de teste da etapa 2: posição xy e cor rgb por vértice.
class Triangle {
public:
    Triangle() : program_(kTriangleVs, kTriangleFs), aspect_(program_.uniform("uAspect")) {
        const float verts[] = {
            -0.6f, -0.5f, 1.0f, 0.0f, 0.0f,
             0.6f, -0.5f, 0.0f, 1.0f, 0.0f,
             0.0f,  0.6f, 0.0f, 0.0f, 1.0f,
        };
        vao_.bind();
        vbo_.upload(GL_ARRAY_BUFFER, verts, sizeof verts);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
        glBindVertexArray(0);
    }

    void draw(float aspect) const {
        program_.use();
        glUniform1f(aspect_, aspect);
        vao_.bind();
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }

private:
    dr2::gl::Program program_;
    GLint aspect_;
    dr2::gl::Vao vao_;
    dr2::gl::Buffer vbo_;
};

int run(const Options& opt) {
    Platform platform;
    SDL_Window* window = platform.window();
    const std::string renderer = gl_string(GL_RENDERER);
    const std::string version = gl_string(GL_VERSION);

    Triangle triangle;
    dr2::gl::check("criação da cena");

    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 title_t0 = SDL_GetPerformanceCounter();
    long title_frames = 0;
    long frames = 0;
    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) running = false;
        }

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.55f, 0.68f, 0.82f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        triangle.draw(h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.0f);
        dr2::gl::check("quadro");

        ++frames;
        const bool last = opt.frames > 0 && frames >= opt.frames;
        if (last && opt.screenshot) dr2::gl::save_ppm(opt.screenshot, w, h);  // antes do swap: lê o back buffer
        SDL_GL_SwapWindow(window);

        ++title_frames;
        const Uint64 now = SDL_GetPerformanceCounter();
        const double elapsed = static_cast<double>(now - title_t0) / static_cast<double>(freq);
        if (elapsed >= 0.5) {
            char title[512];
            std::snprintf(title, sizeof title, "DR2 Viewer3D | %s | GL %s | %.0f fps", renderer.c_str(), version.c_str(),
                          title_frames / elapsed);
            SDL_SetWindowTitle(window, title);
            title_t0 = now;
            title_frames = 0;
        }
        if (last) running = false;
    }

    if (opt.frames > 0) std::printf("OK renderer=%s gl=%s frames=%ld\n", renderer.c_str(), version.c_str(), frames);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) return 1;
    try {
        return run(opt);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "viewer3d: %s\n", e.what());
        return 1;
    }
}
