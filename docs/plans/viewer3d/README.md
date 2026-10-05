# Viewer 3D nativo (MVP em 7 etapas)

Leia antes: [README dos planos](../README.md) (regras gerais) e [formatos.md](formatos.md) (o que os arquivos exportados contêm).

## Objetivo

Um **viewer/editor 3D nativo para Linux** (Windows depois), em C++ com GPU, que abre uma pista **já exportada** pelo Python e permite orbitar, ver o terreno com textura e os objetos instanciados, selecionar um objeto, movê-lo e gravar o `edits.json` que o `track/edit.py` já sabe aplicar.

**Aceite mínimo:** etapas 1 a 4 (janela, shader, câmera e o terreno real da Montalegre na GPU). **Aceite completo do MVP:** etapas 1 a 7. Se as etapas 1 a 6 fecharem cedo, o próximo candidato é testar a **Polônia** (9 milhões de vértices, o motivo de o app nativo existir), mas isso é depois do MVP.

## Por que esta tecnologia

| Decisão | Motivo |
| --- | --- |
| **SDL3 + OpenGL 3.3 core (GLEW)** | Já instalados na máquina do dono (SDL3 3.4.16, GLEW 2.3.1), funcionam no CachyOS e no Windows, RenderDoc depura. Nada a baixar. |
| Renderer atrás de uma interface pequena | Trocar por bgfx, Vulkan ou wgpu depois sem reescrever o resto. |
| **Sem ImGui no MVP** | O `vendor/imgui` do repositório só tem backends Win32/DX11; adicionar SDL3+OpenGL3 é um passo à parte. A interface do MVP é o título da janela e atalhos. |
| **Python continua sendo o parser** | O app lê DR2M/DR2I/WebP/`track.json` que o Python já exporta (formato próprio, sem ambiguidade). Portar o PSSG para C++ é uma etapa cara que não é necessária para ver e editar a pista. |
| **CMake próprio em `tools/viewer3d/`** | O `CMakeLists.txt` da raiz é o do hook de Windows (DLLs, MinGW/MSVC); misturar quebraria os dois. |
| Edição = gravar `edits.json` | A lógica difícil (escrever `.nefs`, blocos de 64 KiB, clones de instância) já existe e é testada em Python. Não duplicar em C++. |

Ambiente da máquina (2026-10-05): CachyOS, g++ 16.2, clang++, CMake 3.31, `glslc`, SDL3, GLEW, GLM (só cabeçalhos em `/usr/include/glm`), libwebp 1.6. **Não há** nlohmann/json instalado: escreva um leitor de JSON mínimo (a etapa 4 diz como; o `track.json` só usa objetos, listas, números, strings e booleanos). GPUs: NVIDIA RTX 4050 e Intel (Mesa, OpenGL 4.6 de compatibilidade). O Mesa escolhe a Intel por padrão; para a NVIDIA use:

```bash
__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia ./build/viewer3d/viewer3d ...
# ou: DRI_PRIME=1 ./build/viewer3d/viewer3d ...
```

## Estrutura de pastas

Mesmo no MVP, o código já nasce separado nas camadas que o app definitivo terá, com dependência só para baixo:

```
tools/viewer3d/
  CMakeLists.txt
  README.md                  como compilar e rodar (escreva na etapa 1, atualize a cada etapa)
  src/
    core/     dr2m.{hpp,cpp}  dr2i.{hpp,cpp}  json.{hpp,cpp}  track.{hpp,cpp}   sem SDL, sem OpenGL
    render/   gl.{hpp,cpp}  camera.hpp  terrain.{hpp,cpp}  instances.{hpp,cpp}  texture.{hpp,cpp}  pick.{hpp,cpp}
    edit/     history.{hpp,cpp}  edits_json.{hpp,cpp}
    app/      main.cpp  (SDL3, entrada, laço principal, título da janela)
  tests/      core_tests.cpp  (executável simples com asserts, sem framework)
```

Alvos CMake: `dr2core` (biblioteca estática de `core/`, **sem** dependência de SDL/GL/GLEW), `dr2render` (usa `dr2core`, GLEW, GLM, libwebp), `dr2edit` (usa `dr2core`), `viewer3d` (executável; usa tudo e SDL3), `core_tests` (usa só `dr2core`). A regra é verificada pelo CMake: `dr2core` não pode ter `target_link_libraries` de nada além de si.

Compilar e rodar:

```bash
cmake -S tools/viewer3d -B build/viewer3d -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/viewer3d
./build/viewer3d/viewer3d --track build/uiview/tracks/portugal__montalegre_rallycross
```

`build/` é ignorado pelo git; não commite binários nem dados exportados.

## Dados de teste

```bash
python3 -m tools.uiview.track --tracks montalegre -o build/uiview     # ~15 s, ~45 MB
```

## Ferramentas de verificação embutidas

Para poder provar cada etapa sem ficar olhando para uma janela, o executável aceita desde a etapa 1:

| Opção | Efeito |
| --- | --- |
| `--frames N` | roda `N` quadros, imprime uma linha `OK renderer=... gl=... frames=N` e sai com 0 |
| `--screenshot arq.ppm` | (a partir da etapa 2) grava o último quadro em PPM (P6, sem dependências) antes de sair |
| `--track DIR` | (a partir da etapa 4) pasta da pista exportada |

Converter o PPM para ver: `python3 -c "from PIL import Image; Image.open('arq.ppm').save('arq.png')"`.

## Regras de trabalho

1. **Uma etapa por vez.** Só passe para a seguinte quando o **critério de pronto** da atual estiver cumprido e verificado por comando. Se uma etapa for grande demais, **reduza o escopo** dela para uma versão mínima que feche.
2. **Um commit por etapa**, `feat(viewer3d): etapa N — <título>`, com o resultado da verificação no corpo.
3. **Compilação limpa**: `-Wall -Wextra` sem avisos; C++20.
4. **Nada de funcionalidade fora do plano** (ImGui, parser PSSG em C++, Polônia, Windows, empacotamento, integração com o jogo). Se achar uma ideia boa, anote em "Ideias" no `tools/viewer3d/README.md` e siga.
5. **Não escreva na pasta do jogo.** O app só lê a pasta exportada (`build/uiview/tracks/...`) e só grava o `edits.json` em `build/uiview/saves/` ou onde o usuário indicar fora da pasta do jogo.
6. **Não rode o que gravar `.nefs`** dentro do jogo; aplicar o `edits.json` a um `.nefs` novo em `build/uiview/saves/` é permitido (é o que o `track/edit.py` faz), mas testar esse arquivo no jogo só com OK do dono.

## Etapas

| # | Documento | Entrega | Critério curto |
| --- | --- | --- | --- |
| 1 | [Janela e GPU](etapa_1_janela_gpu.md) | janela SDL3 com contexto OpenGL 3.3 core | título mostra `GL_RENDERER` e FPS |
| 2 | [Triângulo e shader](etapa_2_triangulo_shader.md) | pipeline de shader e buffers | triângulo colorido, `--screenshot` |
| 3 | [Câmera orbital](etapa_3_camera.md) | orbitar, pan, zoom, WASD | cubo e grade navegáveis, mesmas constantes do web |
| 4 | [Terreno da Montalegre](etapa_4_terreno.md) | DR2M + `track.json` lidos em C++ | 1324 malhas / 428 789 vértices / 557 900 triângulos, na tela |
| 5 | [Texturas](etapa_5_texturas.md) | WebP → textura, material → arquivo | terreno texturizado |
| 6 | [Objetos instanciados](etapa_6_instancias.md) | DR2I + `objects.bin` + `glDrawElementsInstanced` | 2897 instâncias, ≥ 60 FPS na RTX |
| 7 | [Seleção e mover](etapa_7_selecao_mover.md) | picking, mover, apagar, desfazer, gravar `edits.json` | `track/edit.py` gera um `.nefs` válido |

## Depois do MVP (não fazer agora)

Em ordem provável: (1) **Polônia** com LOD e streaming de blocos; (2) portar PSSG/NEFS para `dr2core` em C++ (mmap e cache em disco) comparando com o Python como oráculo; (3) ImGui (backends SDL3+OpenGL3) para painéis; (4) Windows nativo e empacotamento; (5) o Car Explorer no app nativo; (6) comunicação com o hook do jogo (canal local) para mostrar o carro no mapa. Nada disso entra no MVP.

## Estado

| Etapa | Estado |
| --- | --- |
| 1 | concluída (Ubuntu 24.04, Xvfb, Mesa llvmpipe; falta a GPU do dono) |
| 2 | concluída (Ubuntu 24.04, Xvfb, Mesa llvmpipe; falta a GPU do dono) |
| 3 | concluída (Ubuntu 24.04, Xvfb, Mesa llvmpipe; falta a GPU do dono) |
| 4 | feita com a pista sintética (Mesa llvmpipe); falta conferir na Montalegre e na GPU do dono |
| 5 | feita com a pista sintética (Mesa llvmpipe); falta conferir na Montalegre e na GPU do dono |
| 6 | feita com a pista sintética (Mesa llvmpipe); falta conferir na Montalegre e na GPU do dono |
| 7 | feita com a pista sintética (Mesa llvmpipe); falta conferir na Montalegre e na GPU do dono |
