// DR2 Viewer3D: janela SDL3 com contexto OpenGL 3.3 core e câmera orbital do Track Explorer.
//
//   viewer3d [--track DIR] [--frames N] [--screenshot arq.ppm] [--vsync 0|1] [--camera yaw,pitch,dist,x,y,z]
//            [--out edits.json]
//
// --out é onde Ctrl+S grava o edits.json (padrão build/uiview/saves/<id>.edits.json); um caminho
// dentro da pasta do jogo é recusado.
// --camera põe a câmera num estado exato (para comparar capturas com o viewer web).
// --look NOME começa vendo pela câmera do replay com esse nome (ex.: camera_r0_spectator_001) ou do banco do
// piloto da vaga de largada com esse nome (ex.: slot_0 ou grid_start_standing_01/slot_05).
// --grid-check imprime a folga de cada vaga de largada até o terreno e sai com 1 se alguma está dentro do chão.
// --launch abre a janela do Testar no jogo (F5) ao começar (para capturas; o teste só começa com Enter).
// Sem --track, mostra a cena de teste (cubo e grade). Com --track, abre a pista exportada em DIR
// (track.json, terrain_<n>.bin, ...). Com --frames, roda N quadros, imprime
// "OK renderer=... gl=... frames=N fps=..." e sai com 0. Com --screenshot, grava o último quadro em PPM.

#include "app/track_view.hpp"
#include "app/ui.hpp"
#include "render/camera.hpp"
#include "render/gl.hpp"

#include <SDL3/SDL.h>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    long frames = -1;  // -1 = até fechar a janela
    const char* screenshot = nullptr;
    const char* track = nullptr;
    const char* out = nullptr;  // edits.json
    int vsync = 1;
    bool panels = true;
    bool fresh = false;  // não retoma o edits.json que já existe
    float terrain_dist = 0.0f;
    float walk = 0.0f;  // metros por quadro que a câmera anda em x (para medir o corte)
    dr2::render::TextureCache::Options tex;
    const char* hide = nullptr;         // camadas escondidas, separadas por vírgula: terrain, obj, tree, dist, lines (portões, IA, replay e largada), replay, grids
    int shot_w = 0, shot_h = 0;         // --shot-size: a captura sai de um framebuffer fora da tela, só com o 3D
    const char* touch_test = nullptr;   // "IDX,dx,dy,dz[,commit]": desloca a instância em 8 quadros só pelo reenvio parcial (ou, com commit, fecha o passo e força o corte completo)
    const char* settle_list = nullptr;  // arquivo com índices de instância: assenta todas antes de seguir (teste do reenvio parcial)
    bool settle_redo = false;           // depois de assentar, desfaz e refaz tudo (reenvio completo): a imagem tem de ser a mesma
    const char* align_check = nullptr;  // arquivo com índices: sobe cada uma 3 m, alinha ao terreno e imprime a base e a matriz
    const char* ground_check = nullptr;  // arquivo com índices de instância: assenta cada uma, testa o picking de cima e de baixo do terreno e sai
    const char* probe_rays = nullptr;  // arquivo com raios (ox oy oz dx dy dz por linha): imprime a distância até o terreno e sai
    bool wait_textures = false;  // só conta quadros com a fila de texturas vazia (capturas iguais entre execuções)
    double autosave = 60.0;  // segundos entre autosaves (0 = desligado)
    const char* look = nullptr;  // nome de câmera do replay ou de vaga de largada: começa vendo por ela
    bool grid_check = false;     // imprime a folga das vagas de largada até o terreno e sai
    bool launch = false;         // abre a janela do Testar no jogo (F5) ao começar
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
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            opt.out = argv[++i];
        } else if (std::strcmp(argv[i], "--camera") == 0 && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%f,%f,%f,%f,%f,%f", &opt.camera[0], &opt.camera[1], &opt.camera[2], &opt.camera[3],
                            &opt.camera[4], &opt.camera[5]) != 6) {
                std::fprintf(stderr, "--camera precisa de yaw,pitch,dist,x,y,z\n");
                return false;
            }
            const float* c = opt.camera;
            bool finite = true;
            for (int k = 0; k < 6; ++k) finite = finite && std::isfinite(c[k]);
            using Cam = dr2::render::OrbitCamera;
            if (!finite || c[2] < Cam::kDistMin || c[2] > Cam::kDistMax || c[1] < Cam::kPitchMin || c[1] > Cam::kPitchMax ||
                std::fabs(c[3]) > 1e6f || std::fabs(c[4]) > 1e6f || std::fabs(c[5]) > 1e6f) {
                std::fprintf(stderr, "--camera: dist de %g a %g, pitch de %g a %g, alvo com |x|,|y|,|z| <= 1e6\n",
                             static_cast<double>(Cam::kDistMin), static_cast<double>(Cam::kDistMax),
                             static_cast<double>(Cam::kPitchMin), static_cast<double>(Cam::kPitchMax));
                return false;
            }
            opt.has_camera = true;
        } else if (std::strcmp(argv[i], "--vsync") == 0 && i + 1 < argc) {
            opt.vsync = std::atoi(argv[++i]) != 0 ? 1 : 0;
        } else if (std::strcmp(argv[i], "--terrain-dist") == 0 && i + 1 < argc) {
            opt.terrain_dist = std::strtof(argv[++i], nullptr);
            if (!std::isfinite(opt.terrain_dist) || opt.terrain_dist < 0.0f) {
                std::fprintf(stderr, "--terrain-dist precisa de metros >= 0 (0 = sem limite)\n");
                return false;
            }
        } else if (std::strcmp(argv[i], "--tex-mb") == 0 && i + 1 < argc) {
            const double mb = std::strtod(argv[++i], nullptr);
            if (!std::isfinite(mb) || mb < 16) {
                std::fprintf(stderr, "--tex-mb precisa de megabytes >= 16 (limite de VRAM das texturas)\n");
                return false;
            }
            opt.tex.budget_bytes = static_cast<std::size_t>(mb * 1048576.0);
        } else if (std::strcmp(argv[i], "--tex-max-side") == 0 && i + 1 < argc) {
            opt.tex.max_side = static_cast<std::size_t>(std::strtoul(argv[++i], nullptr, 10));  // 0 = sem limite
        } else if (std::strcmp(argv[i], "--tex-threads") == 0 && i + 1 < argc) {
            opt.tex.threads = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--probe-rays") == 0 && i + 1 < argc) {
            opt.probe_rays = argv[++i];
        } else if (std::strcmp(argv[i], "--shot-size") == 0 && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%dx%d", &opt.shot_w, &opt.shot_h) != 2 || opt.shot_w < 16 || opt.shot_h < 16 ||
                opt.shot_w > 16384 || opt.shot_h > 16384) {
                std::fprintf(stderr, "--shot-size precisa de LxA, de 16 a 16384 (ex.: 4608x2592)\n");
                return false;
            }
        } else if (std::strcmp(argv[i], "--look") == 0 && i + 1 < argc) {
            opt.look = argv[++i];
        } else if (std::strcmp(argv[i], "--hide") == 0 && i + 1 < argc) {
            opt.hide = argv[++i];
        } else if (std::strcmp(argv[i], "--touch-test") == 0 && i + 1 < argc) {
            opt.touch_test = argv[++i];
        } else if (std::strcmp(argv[i], "--settle-list") == 0 && i + 1 < argc) {
            opt.settle_list = argv[++i];
        } else if (std::strcmp(argv[i], "--settle-redo") == 0) {
            opt.settle_redo = true;
        } else if (std::strcmp(argv[i], "--align-check") == 0 && i + 1 < argc) {
            opt.align_check = argv[++i];
        } else if (std::strcmp(argv[i], "--ground-check") == 0 && i + 1 < argc) {
            opt.ground_check = argv[++i];
        } else if (std::strcmp(argv[i], "--grid-check") == 0) {
            opt.grid_check = true;
        } else if (std::strcmp(argv[i], "--launch") == 0) {
            opt.launch = true;
        } else if (std::strcmp(argv[i], "--wait-textures") == 0) {
            opt.wait_textures = true;
        } else if (std::strcmp(argv[i], "--walk") == 0 && i + 1 < argc) {
            opt.walk = std::strtof(argv[++i], nullptr);
        } else if (std::strcmp(argv[i], "--autosave") == 0 && i + 1 < argc) {
            opt.autosave = std::strtod(argv[++i], nullptr);
            if (!std::isfinite(opt.autosave) || opt.autosave < 0) {
                std::fprintf(stderr, "--autosave precisa de segundos >= 0 (0 = desligado)\n");
                return false;
            }
        } else if (std::strcmp(argv[i], "--fresh") == 0) {
            opt.fresh = true;
        } else if (std::strcmp(argv[i], "--panels") == 0 && i + 1 < argc) {
            opt.panels = std::atoi(argv[++i]) != 0;
        } else {
            std::fprintf(stderr, "uso: viewer3d [--track DIR] [--frames N] [--screenshot arq.ppm] [--vsync 0|1] [--panels 0|1] [--fresh] [--terrain-dist M] [--walk M] [--tex-mb M] [--tex-max-side PX] [--tex-threads N] [--wait-textures] [--hide terrain,obj,tree,dist,lines,replay,grids] [--look CÂMERA|VAGA] [--grid-check] [--launch] [--shot-size LxA] [--probe-rays arq] [--ground-check arq] [--align-check arq] [--settle-list arq [--settle-redo]] [--touch-test IDX,dx,dy,dz[,commit]] [--autosave S] [--camera yaw,pitch,dist,x,y,z] [--out edits.json]\n");
            return false;
        }
    }
    if (opt.shot_w > 0 && !opt.screenshot) {
        std::fprintf(stderr, "--shot-size precisa de --screenshot\n");
        return false;
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

        window_ = SDL_CreateWindow("DR2 Viewer3D", 1440, 810, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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
    SDL_GLContext context() const { return context_; }

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

// Arraste em curso: orbitar, pan (decidido no clique, como no web) ou editar um objeto.
struct Drag {
    bool active = false;
    bool pan = false;
    bool edit = false;
    bool gizmo = false;
    Uint8 button = 0;
    float moved = 0;
};

// Estado do teclado que não é da câmera nem da pista.
struct Input {
    // Teclas apertadas durante um atalho com Ctrl: não andam até serem soltas (soltar o Ctrl antes do S
    // não move a câmera).
    bool chord[SDL_SCANCODE_COUNT] = {};
};

// Traduz eventos SDL em chamadas da câmera, da pista e dos painéis. Devolve false para sair.
// `vp` é a área do 3D na janela: o mouse fora dela é dos painéis.
bool handle_event(const SDL_Event& event, dr2::render::OrbitCamera& cam, Drag& drag, dr2::app::TrackView* track,
                  dr2::app::EditorUi& ui, const dr2::app::Rect& vp, Input& input) {
    const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    // um arraste começado no 3D continua mesmo se o mouse passar sobre um painel
    const bool ui_took = ui.event(event) && !drag.active;
    switch (event.type) {
    case SDL_EVENT_QUIT:
        if (ui.ask_quit(track)) return false;
        break;
    case SDL_EVENT_KEY_UP:
        input.chord[event.key.scancode] = false;
        break;
    case SDL_EVENT_KEY_DOWN:
        // o autorrepetir de um W já apertado não é atalho: só a tecla apertada junto com o Ctrl
        if ((event.key.mod & SDL_KMOD_CTRL) && !event.key.repeat) input.chord[event.key.scancode] = true;
        if (ui_took) break;
        if (event.key.key == SDLK_F10) {
            ui.panels = !ui.panels;
            break;
        }
        if (event.key.key == SDLK_F11) {
            ui.toggle_help();
            break;
        }
        if (event.key.key == SDLK_F5 && !event.key.repeat) {
            ui.open_launch(track);  // testar no jogo
            break;
        }
        if (event.key.key == SDLK_ESCAPE) {
            if (track) track->deselect();  // Esc só tira a seleção; sair é Ctrl+Q ou fechar a janela
            else if (ui.ask_quit(track)) return false;  // cena de teste: nada a perder
            break;
        }
        if (event.key.key == SDLK_Q && (event.key.mod & SDL_KMOD_CTRL)) {
            if (ui.ask_quit(track)) return false;
            break;
        }
        if (track && track->key(event.key.key, event.key.mod, cam)) break;
        if (event.key.key == SDLK_F) cam.reset();
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (drag.active || ui_took) break;
        const float x = event.button.x, y = event.button.y;
        if (!vp.contains(x, y)) break;
        drag = Drag{};
        drag.button = event.button.button;
        if (event.button.button == SDL_BUTTON_LEFT) {
            drag.active = true;
            if (track) drag.gizmo = ui.gizmo_press(*track, cam, x, y);
            // com Mover/Girar sobre um objeto, Shift é subir e descer; fora de objeto, Shift é pan
            if (track && !drag.gizmo) drag.edit = track->begin_edit(x - vp.x, y - vp.y, vp.w, vp.h, cam, shift);
            drag.pan = shift && !drag.edit && !drag.gizmo;
        } else if (event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE) {
            drag.active = true;
            drag.pan = true;
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (!drag.active || event.button.button != drag.button) break;
        if (drag.gizmo) ui.gizmo_release(*track);
        else if (drag.edit) track->end_edit();
        else if (track && drag.button == SDL_BUTTON_LEFT && drag.moved < 5)
            track->click(event.button.x - vp.x, event.button.y - vp.y, vp.w, vp.h, cam);
        drag = Drag{};
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (!drag.active) break;
        drag.moved += std::fabs(event.motion.xrel) + std::fabs(event.motion.yrel);
        if (drag.gizmo) ui.gizmo_drag(*track, cam, event.motion.x, event.motion.y);
        else if (drag.edit) track->edit_drag(event.motion.x - vp.x, event.motion.y - vp.y, vp.w, vp.h, cam, shift);
        else if (drag.pan) cam.pan(event.motion.xrel, event.motion.yrel);
        else cam.orbit(event.motion.xrel, event.motion.yrel);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (ui_took) break;
        cam.zoom(event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y);
        break;
    default:
        break;
    }
    return true;
}

// WASD a cada quadro, pelo estado do teclado (segurar a tecla anda continuamente).
void walk_keys(dr2::render::OrbitCamera& cam, float dt, const Input& input, const dr2::app::EditorUi& ui) {
    if (SDL_GetModState() & SDL_KMOD_CTRL) return;  // Ctrl+S, Ctrl+Z…: atalhos, não andar
    if (ui.wants_keyboard()) return;                // digitando num campo
    const bool* keys = SDL_GetKeyboardState(nullptr);
    auto down = [&](SDL_Scancode k) { return keys[k] && !input.chord[k] ? 1.0f : 0.0f; };
    const float ahead = down(SDL_SCANCODE_W) - down(SDL_SCANCODE_S);
    const float side = down(SDL_SCANCODE_D) - down(SDL_SCANCODE_A);
    if (ahead != 0.0f || side != 0.0f) cam.walk(ahead, side, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0, dt);
}

// Abre a pista em `dir` no lugar da atual. A atual sai antes (duas pistas grandes juntas dobrariam a
// memória); se a nova falhar, a anterior é aberta de novo (com o edits.json dela) e a mensagem vai
// para os painéis. Quem chama já confirmou descartar as edições não gravadas.
void open_track(const std::string& dir, const Options& opt, std::unique_ptr<dr2::app::TrackView>& track,
                std::unique_ptr<TestScene>& scene, dr2::render::OrbitCamera& cam, dr2::app::EditorUi& ui) {
    std::string old_dir, old_out, old_edits;
    bool old_unsaved = false;
    if (track) {
        old_dir = track->track().dir;
        old_out = track->out_path();
        old_unsaved = track->unsaved();
        if (old_unsaved) old_edits = track->current_edits();  // se a nova falhar, a anterior volta como estava
    }
    auto open = [&](const std::string& d, const std::string& out) {
        auto next = std::make_unique<dr2::app::TrackView>(d, out, !opt.fresh, opt.tex);
        next->terrain_dist() = opt.terrain_dist;
        track = std::move(next);
        scene.reset();
        track->frame_route(cam);
    };
    try {
        std::error_code ec;
        const bool same = opt.track && std::filesystem::equivalent(dir, opt.track, ec);
        track.reset();
        open(dir, same && opt.out ? opt.out : "");
        ui.show_message("");
    } catch (const std::exception& e) {
        const std::string why = "não abriu " + dir + ": " + e.what();
        std::fprintf(stderr, "viewer3d: %s\n", why.c_str());
        if (!old_dir.empty()) {
            try {
                open(old_dir, old_out);
                if (old_unsaved) track->recover(old_edits, "a sessão antes de abrir " + dir);
            } catch (const std::exception& e2) {
                std::fprintf(stderr, "viewer3d: nem a anterior reabriu: %s\n", e2.what());
            }
        }
        if (!track && !scene) scene = std::make_unique<TestScene>(cam.target);
        ui.show_message(why);
    }
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
        track = std::make_unique<dr2::app::TrackView>(opt.track, opt.out ? opt.out : "", !opt.fresh, opt.tex);
        track->terrain_dist() = opt.terrain_dist;
        if (opt.hide) {
            const std::string h = std::string(",") + opt.hide + ",";
            if (h.find(",terrain,") != std::string::npos) track->show_terrain() = false;
            if (h.find(",obj,") != std::string::npos) track->layers().obj = false;
            if (h.find(",tree,") != std::string::npos) track->layers().tree = false;
            if (h.find(",dist,") != std::string::npos) track->layers().dist = false;
            const bool lines = h.find(",lines,") != std::string::npos;
            if (lines) track->show_gates() = track->show_ai() = false;
            if (lines || h.find(",replay,") != std::string::npos) track->show_replay() = false;
            if (lines || h.find(",grids,") != std::string::npos) track->show_grids() = false;
        }
        track->frame_route(cam);
    } else {
        scene = std::make_unique<TestScene>(cam.target);
    }
    if (opt.look) {
        const int i = track ? track->route().replay.find(opt.look) : -1;
        const dr2::SlotRef slot = track ? track->route().find_slot(opt.look) : dr2::SlotRef{};
        if (!(i >= 0 ? track->look_through(i, cam) : slot && track->look_from_slot(slot, cam))) {
            std::fprintf(stderr, "--look: \"%s\" não é câmera do replay nem vaga de largada desta rota\n", opt.look);
            return 1;
        }
    }
    if (opt.has_camera) {
        cam.yaw = opt.camera[0];
        cam.pitch = opt.camera[1];
        cam.dist = opt.camera[2];
        cam.target = {opt.camera[3], opt.camera[4], opt.camera[5]};
    }
    if (opt.probe_rays) {
        // teste da sonda de raio: uma linha por raio, "ox oy oz dx dy dz"; imprime a distância ou "none"
        if (!track) {
            std::fprintf(stderr, "--probe-rays precisa de --track\n");
            return 1;
        }
        std::FILE* f = std::fopen(opt.probe_rays, "r");
        if (!f) {
            std::fprintf(stderr, "não abriu %s\n", opt.probe_rays);
            return 1;
        }
        float v[6];
        while (std::fscanf(f, "%f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
            const glm::vec3 d = glm::normalize(glm::vec3(v[3], v[4], v[5]));
            float t = 0;
            if (track->terrain_hit({{v[0], v[1], v[2]}, d}, t)) std::printf("%.5f\n", static_cast<double>(t));
            else std::printf("none\n");
        }
        std::fclose(f);
        return 0;
    }
    if (opt.align_check) {
        // teste do alinhar: por instância, sobe 3 m (o alinhar tem de trazer de volta), alinha e imprime
        // "i em_pé x0 x1 z0 z1 m0..m11 tipo"; "i skip tipo" se o tipo não alinha ou não há terreno
        if (!track) {
            std::fprintf(stderr, "--align-check precisa de --track\n");
            return 1;
        }
        std::FILE* f = std::fopen(opt.align_check, "r");
        if (!f) {
            std::fprintf(stderr, "não abriu %s\n", opt.align_check);
            return 1;
        }
        unsigned idx;
        while (std::fscanf(f, "%u", &idx) == 1) {
            if (idx >= track->instances().n) continue;
            float x0, x1, z0, z1;
            bool upright = false;
            if (!track->fit_footprint(idx, x0, x1, z0, z1, upright)) {
                std::printf("%u skip %s\n", idx, track->track().types[track->instances().type[idx]].name.c_str());
                continue;
            }
            track->select(static_cast<int>(idx));
            float up[12];
            std::copy(track->instances().matrix(idx), track->instances().matrix(idx) + 12, up);
            up[10] += 3.0f;
            if (track->begin_change(idx)) {
                track->set_matrix(idx, up);
                track->end_change("subir 3 m");
            }
            track->align_selected();  // (o histórico tem teto de 300 passos: a prova é a matriz mudar)
            if (std::equal(up, up + 12, track->instances().matrix(idx))) {
                std::printf("%u skip %s (%s)\n", idx, track->track().types[track->instances().type[idx]].name.c_str(), track->status().c_str());
                continue;
            }
            const float* m = track->instances().matrix(idx);
            std::printf("%u %d %.4f %.4f %.4f %.4f", idx, upright ? 1 : 0, static_cast<double>(x0), static_cast<double>(x1),
                        static_cast<double>(z0), static_cast<double>(z1));
            for (int k = 0; k < 12; ++k) std::printf(" %.6f", static_cast<double>(m[k]));
            std::printf(" %s\n", track->track().types[track->instances().type[idx]].name.c_str());
        }
        std::fclose(f);
        return 0;
    }
    if (opt.grid_check) {
        // por vaga: "grade/vaga x y z folga_centro folga_rodas" (ou "sem-terreno"); sai com 1 se alguma afunda
        if (!track) {
            std::fprintf(stderr, "--grid-check precisa de --track\n");
            return 1;
        }
        int bad = 0;
        for (const dr2::Grid& g : track->route().grids)
            for (const dr2::GridSlot& s : g.slots) {
                float center = 0, wheels = 0;
                std::printf("%s/%s %.3f %.3f %.3f ", g.name.c_str(), s.name.c_str(), static_cast<double>(s.pos[0]),
                            static_cast<double>(s.pos[1]), static_cast<double>(s.pos[2]));
                if (!track->slot_clearance(s, center, wheels)) {
                    std::printf("sem-terreno\n");
                    continue;
                }
                const bool low = wheels < dr2::app::TrackView::kSlotLow;
                bad += low;
                std::printf("%.3f %.3f%s\n", static_cast<double>(center), static_cast<double>(wheels), low ? " DENTRO-DO-CHAO" : "");
            }
        return bad ? 1 : 0;
    }
    if (opt.ground_check) {
        // teste do assentar e da oclusão: por instância, "i x z y_antes y_depois pick_de_cima pick_de_baixo".
        // Cima: raio 40 m acima, vertical, até o centro. Baixo: 30 m abaixo do centro, para cima (o terreno, se existe, tapa).
        if (!track) {
            std::fprintf(stderr, "--ground-check precisa de --track\n");
            return 1;
        }
        std::FILE* f = std::fopen(opt.ground_check, "r");
        if (!f) {
            std::fprintf(stderr, "não abriu %s\n", opt.ground_check);
            return 1;
        }
        unsigned idx;
        while (std::fscanf(f, "%u", &idx) == 1) {
            if (idx >= track->instances().n) continue;
            const float* m = track->instances().matrix(idx);
            const glm::vec3 c{m[9], m[10], m[11]};
            const int above = track->pick_ray({c + glm::vec3(0, 40, 0), {0, -1, 0}}, c);
            const int below = track->pick_ray({c - glm::vec3(0, 30, 0), {0, 1, 0}}, c);
            track->select(static_cast<int>(idx));
            track->settle_selected();
            const float y1 = track->instances().matrix(idx)[10];
            std::printf("%u %.5f %.5f %.5f %.5f %d %d %s\n", idx, static_cast<double>(c.x), static_cast<double>(c.z), static_cast<double>(c.y),
                        static_cast<double>(y1), above, below, track->track().types[track->instances().type[idx]].name.c_str());
            track->undo();
        }
        std::fclose(f);
        return 0;
    }
    if (opt.settle_list && track) {
        std::FILE* f = std::fopen(opt.settle_list, "r");
        if (!f) {
            std::fprintf(stderr, "não abriu %s\n", opt.settle_list);
            return 1;
        }
        unsigned idx;
        std::size_t n = 0;
        while (std::fscanf(f, "%u", &idx) == 1) {
            if (idx >= track->instances().n) continue;
            track->select(static_cast<int>(idx));
            track->settle_selected();
            ++n;
        }
        std::fclose(f);
        track->deselect();
        std::printf("assentadas %zu instâncias; %zu passos no histórico\n", n, track->history().size());
        if (opt.settle_redo) {
            while (track->undo()) {}
            while (track->redo()) {}
        }
    }
    dr2::app::EditorUi ui(window, platform.context());
    ui.panels = opt.panels;
    if (opt.launch) ui.open_launch(track.get());
    glEnable(GL_DEPTH_TEST);  // sem culling: o enrolamento dos arquivos do jogo não é normalizado
    dr2::gl::check("criação da cena");

    Drag drag;
    Input input;
    dr2::app::Rect vp;
    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 title_t0 = SDL_GetPerformanceCounter();
    Uint64 last_t = title_t0;
    const Uint64 start_t = title_t0;
    long title_frames = 0;
    long frames = 0;
    float fps = 0.0f;
    double worst_ms = 0.0;
    long worst_frame = 0;
    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (!handle_event(event, cam, drag, track.get(), ui, vp, input)) running = false;
        const Uint64 frame_t = SDL_GetPerformanceCounter();
        const double frame_ms = static_cast<double>(frame_t - last_t) * 1000.0 / static_cast<double>(freq);
        if (frames > 10 && frame_ms > worst_ms) worst_ms = frame_ms, worst_frame = frames;
        const float dt = std::min(0.1f, static_cast<float>(frame_t - last_t) / static_cast<float>(freq));
        last_t = frame_t;
        walk_keys(cam, dt, input, ui);
        if (opt.walk != 0.0f) cam.target.x += opt.walk;

        if (track && opt.autosave > 0) track->autosave_tick(opt.autosave);
        // painéis primeiro: dizem quanto sobra para o 3D
        vp = ui.frame(track.get(), cam, fps);
        if (ui.quit_request) running = false;

        int w = 0, h = 0, ww = 1, wh = 1;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        SDL_GetWindowSize(window, &ww, &wh);
        const float sx = static_cast<float>(w) / static_cast<float>(std::max(1, ww));
        const float sy = static_cast<float>(h) / static_cast<float>(std::max(1, wh));
        glViewport(0, 0, w, h);
        glClearColor(0.55f, 0.68f, 0.82f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const int vx = static_cast<int>(vp.x * sx), vw = std::max(1, static_cast<int>(vp.w * sx));
        const int vh = std::max(1, static_cast<int>(vp.h * sy)), vy = h - static_cast<int>(vp.y * sy) - vh;
        glViewport(vx, vy, vw, vh);
        const float aspect = static_cast<float>(vw) / static_cast<float>(vh);
        const glm::mat4 view_proj = cam.proj(aspect) * cam.view();
        if (track && opt.touch_test) {
            // teste do reenvio parcial: do quadro 20 ao 27 move a instância um oitavo do deslocamento por quadro
            static unsigned idx = 0;
            static float off[3], base[12];
            static bool commit = false, parsed = false;
            if (!parsed) {
                parsed = true;
                char tail[16] = {};
                if (std::sscanf(opt.touch_test, "%u,%f,%f,%f,%15s", &idx, &off[0], &off[1], &off[2], tail) >= 4) commit = std::strcmp(tail, "commit") == 0;
                else idx = ~0u;
            }
            if (idx < track->instances().n) {
                if (frames == 20 && track->begin_change(idx)) std::copy(track->instances().matrix(idx), track->instances().matrix(idx) + 12, base);
                if (frames >= 20 && frames < 28 && track->changing()) {
                    const float k = static_cast<float>(frames - 19) / 8.0f;
                    float m[12];
                    std::copy(base, base + 12, m);
                    for (int c = 0; c < 3; ++c) m[9 + c] += off[c] * k;
                    track->set_matrix(idx, m);
                }
                if (frames == 28 && commit && track->changing()) track->end_change("teste do touch");
            }
        }
        if (track) track->draw(view_proj, cam);
        else scene->draw(view_proj);
        glViewport(0, 0, w, h);
        ui.render(track.get(), cam, view_proj, vp);
        dr2::gl::warn("quadro");  // um erro de GL num quadro avisa, não fecha o editor com edições abertas

        // com --wait-textures o quadro só conta quando não há textura a caminho (limite de 5000 espera)
        static long waited = 0;
        const bool settling = opt.wait_textures && track && track->textures().busy() && ++waited < 5000;
        if (!settling) ++frames;
        const bool last = opt.frames > 0 && frames >= opt.frames;
        if (last && opt.screenshot && opt.shot_w > 0) {
            // mesma câmera, só a cena, no tamanho pedido (pode passar do tamanho da tela)
            GLuint fbo = 0, rb[2] = {};
            glGenFramebuffers(1, &fbo);
            glGenRenderbuffers(2, rb);
            glBindRenderbuffer(GL_RENDERBUFFER, rb[0]);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, opt.shot_w, opt.shot_h);
            glBindRenderbuffer(GL_RENDERBUFFER, rb[1]);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, opt.shot_w, opt.shot_h);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[0]);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb[1]);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("--shot-size: framebuffer incompleto");
            glViewport(0, 0, opt.shot_w, opt.shot_h);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            const glm::mat4 shot_vp = cam.proj(static_cast<float>(opt.shot_w) / static_cast<float>(opt.shot_h)) * cam.view();
            if (track) track->draw(shot_vp, cam);
            else scene->draw(shot_vp);
            dr2::gl::save_ppm(opt.screenshot, opt.shot_w, opt.shot_h, GL_COLOR_ATTACHMENT0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteRenderbuffers(2, rb);
            glDeleteFramebuffers(1, &fbo);
            glViewport(0, 0, w, h);
        } else if (last && opt.screenshot) {
            dr2::gl::save_ppm(opt.screenshot, w, h);  // antes do swap: lê o back buffer
        }
        SDL_GL_SwapWindow(window);

        if (!ui.open_request.empty()) {
            const std::string dir = std::move(ui.open_request);
            ui.open_request.clear();
            drag = Drag{};
            open_track(dir, opt, track, scene, cam, ui);
        }

        ++title_frames;
        const Uint64 now = SDL_GetPerformanceCounter();
        const double elapsed = static_cast<double>(now - title_t0) / static_cast<double>(freq);
        if (elapsed >= 0.5) {
            fps = static_cast<float>(title_frames / elapsed);
            char tail[32];
            std::snprintf(tail, sizeof tail, " | %.0f fps", static_cast<double>(fps));
            const std::string title = "DR2 Viewer3D | " + renderer + " | GL " + version + (track ? " | " + track->title() : "") + tail;
            SDL_SetWindowTitle(window, title.c_str());
            title_t0 = now;
            title_frames = 0;
        }
        if (last) running = false;
    }

    if (opt.frames > 0) {
        const double total = static_cast<double>(SDL_GetPerformanceCounter() - start_t) / static_cast<double>(freq);
        std::printf("OK renderer=%s gl=%s frames=%ld fps=%.1f\n", renderer.c_str(), version.c_str(), frames, frames / total);
        if (track) {
            std::printf("%s\n", track->title().c_str());
            const auto& o = track->objects();
            const auto& tx = track->textures();
            std::printf("pior quadro %.1f ms (quadro %ld)\n", worst_ms, worst_frame);
            std::printf("texturas: %zu na GPU, %zu enviadas, %zu descartadas, %zu reduzidas, %zu falharam, %zu na fila; %.0f de %.0f MB; decodificação %.2f s\n",
                        tx.loaded(), tx.created(), tx.evicted(), tx.downscaled(), tx.failed(), tx.pending(),
                        static_cast<double>(tx.gpu_bytes()) / 1048576.0, static_cast<double>(tx.budget()) / 1048576.0, tx.decode_seconds());
            if (std::size_t total = 0, free = 0; dr2::gl::vram_kb(total, free))
                std::printf("VRAM da GPU: %.0f MB usados de %.0f MB\n", static_cast<double>(total - free) / 1024.0, static_cast<double>(total) / 1024.0);
            std::printf("cortes=%zu (%.3f ms cada) reenvios parciais=%zu células no último=%zu\n", o.culls(),
                        o.culls() ? o.cull_seconds() * 1000.0 / static_cast<double>(o.culls()) : 0.0, o.partial_updates(), o.last_cells());
        }
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
