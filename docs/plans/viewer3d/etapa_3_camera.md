# Etapa 3 — Câmera orbital

Antes: [README do viewer 3D](README.md). Anterior: [etapa 2](etapa_2_triangulo_shader.md) cumprida.

## Objetivo

Navegar numa cena 3D com a **mesma câmera e os mesmos controles do Track Explorer web**, para que o comportamento seja familiar e comparável. A cena de teste é um cubo e uma grade de chão.

## Arquivos

- `src/render/camera.hpp` (novo, só cabeçalho): estado e matrizes da câmera.
- `src/app/main.cpp`: entrada (mouse, roda, teclado) e cena de teste.
- `src/render/gl.{hpp,cpp}`: adicione `uniform` helpers para `mat4` se precisar.
- `tests/camera_test.cpp` (novo) e alvo `camera_test` no CMake.

## Convenções que **devem** bater com o web

Valem as de `trackview.js` (`tvCam`, `tvVp`, `tvKeys` e os manipuladores de ponteiro). Eixo **Y para cima**.

```
yaw = 0.8, pitch = 0.6, dist = 400, target = (0, 1430, -430)         estado inicial
eye = target + dist * ( cos(pitch)*sin(yaw),  sin(pitch),  cos(pitch)*cos(yaw) )
view = lookAt(eye, target, (0,1,0))
proj = perspective(fovY = 0.9 rad, aspect, near = max(0.3, dist/200), far = max(20000, dist*20))
```

Controles (mapeie para SDL3; `dx`, `dy` em pixels desde o evento anterior):

| Ação | Entrada | Efeito (idêntico ao web) |
| --- | --- | --- |
| Orbitar | botão esquerdo | `yaw -= dx*0.005`; `pitch = clamp(pitch + dy*0.005, -0.2, 1.5)` |
| Pan | botão direito, ou Shift + esquerdo | `k = dist*0.0016`; `r = (cos yaw, -sin yaw)`; `f = (-sin yaw, -cos yaw)`; `target.xz -= r*dx*k`; `target.xz += f*dy*k` |
| Zoom | roda | `dist = clamp(dist * exp(-roda_y * 0.12), 2, 15000)` (a SDL3 dá `+1` por "clique" para cima/longe; o navegador dá `deltaY ≈ ±100` com sinal oposto, daí o `0.0012 * 100 = 0.12`) |
| Andar | W A S D (Shift = ×3) | por quadro a 60 Hz: `step = dist*0.012*(shift ? 3 : 1)`; `W` = `+f`, `S` = `-f`, `A` = `-r`, `D` = `+r` em `target.xz`. Multiplique por `dt*60` para não depender do FPS |
| Enquadrar | F | volta ao estado inicial (nesta etapa; a etapa 7 enquadra o selecionado) |
| Sair | Esc | — |

## Implementação

- `struct OrbitCamera { float yaw, pitch, dist; glm::vec3 target; glm::vec3 eye() const; glm::mat4 view() const; glm::mat4 proj(float aspect) const; void orbit(float dx, float dy); void pan(float dx, float dy); void zoom(float wheel); void walk(...); }` em `camera.hpp`, com as fórmulas acima. **Nenhuma** dependência de SDL ou OpenGL (só GLM): a lógica de câmera é do `render`, a leitura de eventos é do `app`.
- Cena de teste: cubo de 10 m com 12 triângulos coloridos por face (cores diferentes por face) no centro do `target` inicial, e uma grade de linhas (`GL_LINES`) no plano `y = target.y`, 200 × 200 m, linhas a cada 10 m. Shader com `uniform mat4 uMvp` e cor por vértice.
- Ligue `glEnable(GL_DEPTH_TEST)`. Sem culling.

## Critério de pronto

```bash
cmake --build build/viewer3d && ./build/viewer3d/camera_test
./build/viewer3d/viewer3d --frames 30 --screenshot build/viewer3d/e3.ppm
```

- `camera_test` (executável com `assert`s, sem framework) confere, com tolerância `1e-3`: com o estado inicial, `eye() == (236.8238, 1655.857, -199.9933)`; `near == 2.0` e `far == 20000`; depois de `orbit(100, 0)`, `yaw == 0.3`; `pitch` nunca sai de `[-0.2, 1.5]`; `zoom` nunca sai de `[2, 15000]`; `walk` com `W` e `yaw = 0` move `target.z` para menos.
- Visualmente: o cubo e a grade aparecem, orbitar/pan/zoom/WASD funcionam, **o chão não atravessa o cubo** (profundidade certa) e o cubo não "pula" ao redimensionar a janela.
- O `e3.ppm` mostra o cubo (mais de 4 cores além do céu e das linhas).

## Riscos e bloqueios

- **Convenção de matriz.** O viewer web e o formato DR2I usam vetor-linha; a câmera **não** é afetada (ela usa `lookAt`/`perspective` normais do GLM em colunas). Só as matrizes de **instância** (etapa 6) precisam da conversão descrita em [formatos.md](formatos.md).
- **Sentido da roda.** Se o zoom ficar invertido, o sinal do `exp(-roda_y*0.12)` está errado; na SDL3 existe `event.wheel.direction` (normal ou invertido pelo sistema): use `y * (direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1)`.
- **Eventos de mouse com captura.** Use `SDL_CaptureMouse` ou `SDL_SetWindowRelativeMouseMode` apenas se o arraste sair da janela; o web não captura, então comece sem.

## Fora do escopo

Texturas, arquivos do jogo, seleção.
