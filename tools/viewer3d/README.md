# DR2 Viewer3D

Visualizador 3D nativo (C++20, SDL3, OpenGL 3.3 core) que vai abrir as pistas já exportadas pelo Python. Plano e etapas: [`docs/plans/viewer3d/`](../../docs/plans/viewer3d/README.md).

## Dependências

SDL3, GLEW, OpenGL e CMake ≥ 3.20 (Ninja opcional).

| Sistema | Pacotes |
| --- | --- |
| CachyOS / Arch | `sdl3 glew` |
| Ubuntu 24.04 | `libglew-dev libgl-dev`; o SDL3 não tem pacote: compile o fonte (`release-3.2.x` ou mais novo) e passe `-DCMAKE_PREFIX_PATH=<prefixo>` |

## Compilar e rodar

```bash
cmake -S tools/viewer3d -B build/viewer3d -G Ninja
cmake --build build/viewer3d
./build/viewer3d/viewer3d                # janela; Esc ou fechar sai
./build/viewer3d/viewer3d --frames 120   # roda 120 quadros, imprime "OK renderer=... gl=..." e sai com 0
./build/viewer3d/camera_test             # confere a câmera contra as constantes do web
```

Para usar a NVIDIA num notebook híbrido:

```bash
__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia ./build/viewer3d/viewer3d --frames 120
```

Se a janela não abrir em Wayland, tente `SDL_VIDEO_DRIVER=x11`.

### Sem tela (CI, nuvem)

Com Mesa (`libgl1-mesa-dri`) e Xvfb o contexto 3.3 core sai pelo llvmpipe, em software:

```bash
Xvfb :99 -screen 0 1600x900x24 &
DISPLAY=:99 ./build/viewer3d/viewer3d --frames 120
```

## Controles

Os mesmos do Track Explorer web (`tvCam`, `tvVp`, `tvKeys` em `tools/uiview/web/js/trackview.js`).

| Ação | Entrada |
| --- | --- |
| Orbitar | botão esquerdo |
| Pan | botão direito, ou Shift + esquerdo |
| Zoom | roda |
| Andar | W A S D (Shift = ×3) |
| Enquadrar | F (volta ao estado inicial) |
| Sair | Esc |

## Opções

| Opção | Efeito |
| --- | --- |
| `--frames N` | roda `N` quadros, imprime `OK renderer=<GL_RENDERER> gl=<GL_VERSION> frames=N` e sai com 0 |
| `--screenshot arq.ppm` | com `--frames`, grava o último quadro em PPM (P6) antes de sair |

Ver o PPM: `python3 -c "from PIL import Image; Image.open('arq.ppm').save('arq.png')"`.

## Verificado

| Etapa | Ambiente | Resultado |
| --- | --- | --- |
| 1 | Ubuntu 24.04, Xvfb, SDL 3.2.24, GLEW 2.2.0, Mesa 25.2.8 | `OK renderer=llvmpipe (LLVM 20.1.2, 256 bits) gl=4.5 (Core Profile) Mesa 25.2.8-0ubuntu0.24.04.2 frames=120` |
| 2 | o mesmo | `--frames 30 --screenshot`: canto `(140, 173, 209)`, 54 196 cores distintas, triângulo vermelho/verde/azul |
| 3 | o mesmo | `camera_test OK`; `--frames 30 --screenshot`: cubo (3 faces visíveis) e grade; com `xdotool` no Xvfb, orbitar, roda, pan e `D` mudam o quadro e `F` volta ao quadro inicial byte a byte |

## Ideias

(Fora do plano; anotar aqui e seguir.)
