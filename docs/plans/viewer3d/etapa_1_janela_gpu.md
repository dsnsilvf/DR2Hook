# Etapa 1 — Janela e GPU

Antes: [README do viewer 3D](README.md). Anterior: nenhuma.

## Objetivo

Abrir uma janela nativa com contexto OpenGL 3.3 core, limpar a tela com a cor do céu do viewer web e mostrar no título qual GPU está em uso e o FPS.

## Arquivos

- `tools/viewer3d/CMakeLists.txt` (novo)
- `tools/viewer3d/src/app/main.cpp` (novo)
- `tools/viewer3d/README.md` (novo; como compilar e rodar)

## Implementação

**CMake.** `cmake_minimum_required(VERSION 3.20)`, `project(dr2viewer3d CXX)`, `set(CMAKE_CXX_STANDARD 20)`. `find_package(SDL3 CONFIG REQUIRED)`, `find_package(GLEW REQUIRED)`, `find_package(OpenGL REQUIRED)`. Alvo `viewer3d` com `src/app/main.cpp`, ligado a `SDL3::SDL3`, `GLEW::GLEW`, `OpenGL::GL`. Avisos: `-Wall -Wextra` (e `/W4` no MSVC). Padrão de build: `RelWithDebInfo` se nada for dado.

**`main.cpp`.** Em SDL3 (a API mudou em relação à SDL2):

1. `SDL_Init(SDL_INIT_VIDEO)`.
2. Antes de criar a janela: `SDL_GL_SetAttribute` para `SDL_GL_CONTEXT_MAJOR_VERSION = 3`, `MINOR = 3`, `SDL_GL_CONTEXT_PROFILE_MASK = SDL_GL_CONTEXT_PROFILE_CORE`, `SDL_GL_DEPTH_SIZE = 24`, `SDL_GL_DOUBLEBUFFER = 1`.
3. `SDL_CreateWindow("DR2 Viewer3D", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE)` (sem posição x,y: na SDL3 a assinatura é `(título, w, h, flags)`).
4. `SDL_GL_CreateContext(janela)`; `SDL_GL_SetSwapInterval(1)`.
5. `glewExperimental = GL_TRUE; glewInit();` e **depois** `glGetError()` uma vez para descartar o `GL_INVALID_ENUM` que o GLEW provoca em perfil core.
6. Laço: `SDL_PollEvent`; sair em `SDL_EVENT_QUIT` ou em `SDL_EVENT_KEY_DOWN` com `event.key.key == SDLK_ESCAPE`; `glClearColor(0.55f, 0.68f, 0.82f, 1.0f)`; `glClear(COLOR | DEPTH)`; `SDL_GL_SwapWindow`.
7. A cada ~0,5 s atualize o título: `DR2 Viewer3D | <GL_RENDERER> | GL <GL_VERSION> | <fps> fps` (`SDL_SetWindowTitle`; contador com `SDL_GetPerformanceCounter`).
8. Argumento `--frames N`: roda `N` quadros, imprime `OK renderer=<...> gl=<...> frames=N` e sai com 0 (para verificar sem olhar a janela). Falha de qualquer passo: mensagem em `stderr` com `SDL_GetError()` e código 1.

## Critério de pronto

Todos, por comando:

```bash
cmake -S tools/viewer3d -B build/viewer3d -G Ninja && cmake --build build/viewer3d   # sem avisos
./build/viewer3d/viewer3d --frames 120                                               # imprime OK renderer=... e sai com 0
__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia ./build/viewer3d/viewer3d --frames 120
```

- O segundo comando mostra um renderer (Intel/Mesa por padrão); o terceiro mostra `NVIDIA GeForce RTX 4050`. Registre os dois no commit.
- Sem `--frames`, a janela abre azul-céu, o título mostra renderer e FPS e fecha com Esc ou no botão de fechar. (Verificação visual; se não houver tela, o `--frames` basta.)

## Riscos e bloqueios

- **Mesa escolhe a Intel**: use as variáveis de ambiente do README; não tente forçar via código.
- **Sem contexto 3.3** (driver antigo): imprima o erro do SDL e saia; não caia para um perfil de compatibilidade.
- **Wayland vs X11**: se a janela não abrir, tente `SDL_VIDEO_DRIVER=x11`. Registre qual funcionou no `tools/viewer3d/README.md`.
- `find_package(SDL3)` falha se o pacote de desenvolvimento não estiver visível: verifique `pkg-config --modversion sdl3` (3.4.16 na máquina do dono).

## Fora do escopo

Shaders, buffers, câmera, ImGui, qualquer leitura de arquivo do jogo.
