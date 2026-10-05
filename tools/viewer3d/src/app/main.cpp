// DR2 Viewer3D: janela SDL3 com contexto OpenGL 3.3 core.
//
//   viewer3d [--frames N]
//
// Com --frames, roda N quadros, imprime "OK renderer=... gl=... frames=N" e sai com 0.

#include <GL/glew.h>
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

struct Options {
    long frames = -1;  // -1 = até fechar a janela
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
        } else {
            std::fprintf(stderr, "uso: viewer3d [--frames N]\n");
            return false;
        }
    }
    return true;
}

int fail(const char* step) {
    std::fprintf(stderr, "viewer3d: %s: %s\n", step, SDL_GetError());
    return 1;
}

const char* gl_string(GLenum name) {
    const GLubyte* s = glGetString(name);
    return s ? reinterpret_cast<const char*>(s) : "?";
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) return 1;

    if (!SDL_Init(SDL_INIT_VIDEO)) return fail("SDL_Init");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow("DR2 Viewer3D", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        int rc = fail("SDL_CreateWindow");
        SDL_Quit();
        return rc;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        int rc = fail("SDL_GL_CreateContext (OpenGL 3.3 core)");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return rc;
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
        std::fprintf(stderr, "viewer3d: glewInit: %s\n", reinterpret_cast<const char*>(glewGetErrorString(glew)));
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    glGetError();  // o GLEW provoca GL_INVALID_ENUM em perfil core; descarta

    const std::string renderer = gl_string(GL_RENDERER);
    const std::string version = gl_string(GL_VERSION);

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
        SDL_GL_SwapWindow(window);

        ++frames;
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
        if (opt.frames > 0 && frames >= opt.frames) running = false;
    }

    if (opt.frames > 0) std::printf("OK renderer=%s gl=%s frames=%ld\n", renderer.c_str(), version.c_str(), frames);

    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
