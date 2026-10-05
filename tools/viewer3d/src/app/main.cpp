// DR2 Viewer3D: janela SDL3 com contexto OpenGL 3.3 core e câmera orbital do Track Explorer.
//
//   viewer3d [--track DIR] [--frames N] [--screenshot arq.ppm] [--vsync 0|1] [--camera yaw,pitch,dist,x,y,z]
//
// --camera põe a câmera num estado exato (para comparar capturas com o viewer web).
// Sem --track, mostra a cena de teste (cubo e grade). Com --track, abre a pista exportada em DIR
// (track.json, terrain_<n>.bin, ...). Com --frames, roda N quadros, imprime
// "OK renderer=... gl=... frames=N fps=..." e sai com 0. Com --screenshot, grava o último quadro em PPM.

#include "app/track_view.hpp"
#include "render/camera.hpp"
#include "render/gl.hpp"

#include <SDL3/SDL.h>
#include <glm/gtc/type_ptr.hpp>

#include <cstddef>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    long frames = -1;  // -1 = até fechar a janela
    const char* screenshot = nullptr;
    const char* track = nullptr;
    int vsync = 1;
    bool has_camera = false;
    float camera[6] = {};  // yaw, pitch, dist, alvo x, y, z
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
        } else if (std::strcmp(argv[i], "--track") == 0 && i + 1 < argc) {
            opt.track = argv[++i];
        } else if (std::strcmp(argv[i], "--camera") == 0 && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%f,%f,%f,%f,%f,%f", &opt.camera[0], &opt.camera[1], &opt.camera[2], &opt.camera[3],
                            &opt.camera[4], &opt.camera[5]) != 6) {
                std::fprintf(stderr, "--camera precisa de yaw,pitch,dist,x,y,z\n");
                return false;
            }
            opt.has_camera = true;
        } else if (std::strcmp(argv[i], "--vsync") == 0 && i + 1 < argc) {
            opt.vsync = std::atoi(argv[++i]) != 0 ? 1 : 0;
        } else {
            std::fprintf(stderr, "uso: viewer3d [--track DIR] [--frames N] [--screenshot arq.ppm] [--vsync 0|1] [--camera yaw,pitch,dist,x,y,z]\n");
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
    explicit Platform(int vsync) {
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
        SDL_GL_SetSwapInterval(vsync);

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

const char* const kColorVs = R"glsl(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aCol;
uniform mat4 uMvp;
out vec3 vCol;
void main() { vCol = aCol; gl_Position = uMvp * vec4(aPos, 1.0); }
)glsl";

const char* const kColorFs = R"glsl(#version 330 core
in vec3 vCol;
out vec4 oColor;
void main() { oColor = vec4(vCol, 1.0); }
)glsl";

struct ColorVertex {
    glm::vec3 pos;
    glm::vec3 col;
};

// Malha de cor por vértice, desenhada com glDrawArrays.
class ColorMesh {
public:
    ColorMesh(GLenum mode, const std::vector<ColorVertex>& verts) : mode_(mode), count_(static_cast<GLsizei>(verts.size())) {
        vao_.bind();
        vbo_.upload(GL_ARRAY_BUFFER, verts.data(), verts.size() * sizeof(ColorVertex));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ColorVertex), reinterpret_cast<void*>(offsetof(ColorVertex, pos)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ColorVertex), reinterpret_cast<void*>(offsetof(ColorVertex, col)));
        glBindVertexArray(0);
    }
    void draw() const {
        vao_.bind();
        glDrawArrays(mode_, 0, count_);
        glBindVertexArray(0);
    }

private:
    GLenum mode_;
    GLsizei count_;
    dr2::gl::Vao vao_;
    dr2::gl::Buffer vbo_;
};

// Cubo de `size` metros, uma cor por face, 12 triângulos.
std::vector<ColorVertex> cube_vertices(glm::vec3 c, float size) {
    const float h = size * 0.5f;
    // Cada face: eixo normal (0 = x, 1 = y, 2 = z), sinal e cor.
    struct Face { int axis; float sign; glm::vec3 col; };
    const Face faces[] = {
        {0, +1.0f, {0.90f, 0.30f, 0.25f}}, {0, -1.0f, {0.95f, 0.65f, 0.20f}},
        {1, +1.0f, {0.35f, 0.80f, 0.35f}}, {1, -1.0f, {0.25f, 0.45f, 0.30f}},
        {2, +1.0f, {0.30f, 0.50f, 0.95f}}, {2, -1.0f, {0.70f, 0.35f, 0.85f}},
    };
    std::vector<ColorVertex> out;
    for (const Face& f : faces) {
        const int u = (f.axis + 1) % 3, v = (f.axis + 2) % 3;
        glm::vec3 corner[4];
        const float su[4] = {-1, 1, 1, -1}, sv[4] = {-1, -1, 1, 1};
        for (int k = 0; k < 4; ++k) {
            glm::vec3 p(0.0f);
            p[f.axis] = f.sign * h;
            p[u] = su[k] * h;
            p[v] = sv[k] * h;
            corner[k] = c + p;
        }
        for (int k : {0, 1, 2, 0, 2, 3}) out.push_back({corner[k], f.col});
    }
    return out;
}

// Grade de linhas no plano y = c.y: `extent` metros de lado, uma linha a cada `step`.
std::vector<ColorVertex> grid_vertices(glm::vec3 c, float extent, float step) {
    const glm::vec3 col(0.30f, 0.32f, 0.36f);
    const float h = extent * 0.5f;
    const int n = static_cast<int>(extent / step);
    std::vector<ColorVertex> out;
    for (int i = 0; i <= n; ++i) {
        const float t = -h + static_cast<float>(i) * step;
        out.push_back({c + glm::vec3(t, 0.0f, -h), col});
        out.push_back({c + glm::vec3(t, 0.0f, h), col});
        out.push_back({c + glm::vec3(-h, 0.0f, t), col});
        out.push_back({c + glm::vec3(h, 0.0f, t), col});
    }
    return out;
}

// Cena de teste da etapa 3: cubo de 10 m no alvo inicial da câmera e grade de 200 × 200 m.
class TestScene {
public:
    explicit TestScene(glm::vec3 center)
        : program_(kColorVs, kColorFs),
          mvp_(program_.uniform("uMvp")),
          cube_(GL_TRIANGLES, cube_vertices(center, 10.0f)),
          grid_(GL_LINES, grid_vertices(center, 200.0f, 10.0f)) {}

    void draw(const glm::mat4& view_proj) const {
        program_.use();
        glUniformMatrix4fv(mvp_, 1, GL_FALSE, glm::value_ptr(view_proj));
        grid_.draw();
        cube_.draw();
    }

private:
    dr2::gl::Program program_;
    GLint mvp_;
    ColorMesh cube_;
    ColorMesh grid_;
};

// Arraste em curso: orbitar ou pan (decidido no clique, como no web).
struct Drag {
    bool active = false;
    bool pan = false;
};

// Traduz eventos SDL em chamadas da câmera; as teclas vão antes para a pista, se houver.
// Devolve false para sair.
bool handle_event(const SDL_Event& event, dr2::render::OrbitCamera& cam, Drag& drag, dr2::app::TrackView* track) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
        return false;
    case SDL_EVENT_KEY_DOWN:
        if (event.key.key == SDLK_ESCAPE) return false;
        if (track && track->key(event.key.key, cam)) break;
        if (event.key.key == SDLK_F) cam.reset();
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button == SDL_BUTTON_LEFT) {
            drag.active = true;
            drag.pan = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        } else if (event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE) {
            drag.active = true;
            drag.pan = true;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT ||
            event.button.button == SDL_BUTTON_MIDDLE)
            drag.active = false;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (drag.active) {
            if (drag.pan) cam.pan(event.motion.xrel, event.motion.yrel);
            else cam.orbit(event.motion.xrel, event.motion.yrel);
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        cam.zoom(event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y);
        break;
    default:
        break;
    }
    return true;
}

// WASD a cada quadro, pelo estado do teclado (segurar a tecla anda continuamente).
void walk_keys(dr2::render::OrbitCamera& cam, float dt) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    const float ahead = (keys[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_S] ? 1.0f : 0.0f);
    const float side = (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f);
    if (ahead != 0.0f || side != 0.0f) cam.walk(ahead, side, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0, dt);
}

int run(const Options& opt) {
    Platform platform(opt.vsync);
    SDL_Window* window = platform.window();
    const std::string renderer = gl_string(GL_RENDERER);
    const std::string version = gl_string(GL_VERSION);

    dr2::render::OrbitCamera cam;
    std::unique_ptr<TestScene> scene;
    std::unique_ptr<dr2::app::TrackView> track;
    if (opt.track) {
        track = std::make_unique<dr2::app::TrackView>(opt.track);
        track->frame_route(cam);
    } else {
        scene = std::make_unique<TestScene>(cam.target);
    }
    if (opt.has_camera) {
        cam.yaw = opt.camera[0];
        cam.pitch = opt.camera[1];
        cam.dist = opt.camera[2];
        cam.target = {opt.camera[3], opt.camera[4], opt.camera[5]};
    }
    glEnable(GL_DEPTH_TEST);  // sem culling: o enrolamento dos arquivos do jogo não é normalizado
    dr2::gl::check("criação da cena");

    Drag drag;
    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 title_t0 = SDL_GetPerformanceCounter();
    Uint64 last_t = title_t0;
    const Uint64 start_t = title_t0;
    long title_frames = 0;
    long frames = 0;
    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (!handle_event(event, cam, drag, track.get())) running = false;
        const Uint64 frame_t = SDL_GetPerformanceCounter();
        const float dt = std::min(0.1f, static_cast<float>(frame_t - last_t) / static_cast<float>(freq));
        last_t = frame_t;
        walk_keys(cam, dt);

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.55f, 0.68f, 0.82f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
        const glm::mat4 view_proj = cam.proj(aspect) * cam.view();
        if (track) track->draw(view_proj, cam);
        else scene->draw(view_proj);
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
            const std::string what = track ? " | " + track->title() : std::string();
            std::snprintf(title, sizeof title, "DR2 Viewer3D | %s | GL %s%s | %.0f fps", renderer.c_str(), version.c_str(),
                          what.c_str(), title_frames / elapsed);
            SDL_SetWindowTitle(window, title);
            title_t0 = now;
            title_frames = 0;
        }
        if (last) running = false;
    }

    if (opt.frames > 0) {
        const double total = static_cast<double>(SDL_GetPerformanceCounter() - start_t) / static_cast<double>(freq);
        std::printf("OK renderer=%s gl=%s frames=%ld fps=%.1f\n", renderer.c_str(), version.c_str(), frames, frames / total);
        if (track) std::printf("%s\n", track->title().c_str());
    }
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
