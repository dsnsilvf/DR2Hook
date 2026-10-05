# Etapa 2 — Triângulo e shader

Antes: [README do viewer 3D](README.md). Anterior: [etapa 1](etapa_1_janela_gpu.md) cumprida.

## Objetivo

Provar o pipeline de shader e de buffers: compilar um programa GLSL 330, subir um VBO/VAO e desenhar um triângulo colorido; ter os auxiliares de GL que as próximas etapas reaproveitam; poder gravar o quadro em arquivo.

## Arquivos

- `src/render/gl.hpp`, `src/render/gl.cpp` (novos): auxiliares de GL.
- `src/app/main.cpp`: desenha o triângulo, aceita `--screenshot`.
- `CMakeLists.txt`: alvo `dr2render` (biblioteca estática; `gl.cpp`; liga `GLEW::GLEW`, `OpenGL::GL`); `viewer3d` passa a ligar `dr2render`.

## Implementação

**`gl.hpp/.cpp`** (namespace `dr2::gl`):

- `GLuint compile_program(const char* vs, const char* fs)`: compila os dois shaders, linka, e em erro **lança `std::runtime_error` com o log do GL** (`glGetShaderInfoLog`/`glGetProgramInfoLog`). Apaga os shaders depois de linkar.
- `struct Buffer { GLuint id; ... }` com construtor/destrutor (RAII) para VBO/IBO, e `upload(target, data, bytes, usage)`.
- `struct Vao` (RAII).
- `void check(const char* onde)`: lê `glGetError()` e lança se for diferente de `GL_NO_ERROR`; use em modo de depuração.
- `void save_ppm(const char* caminho, int largura, int altura)`: `glReadPixels` (RGB, `GL_UNSIGNED_BYTE`, alinhamento 1) e grava **P6** com as linhas invertidas (o GL lê de baixo para cima).

**Shader** (embutido em `main.cpp` como `R"glsl(...)glsl"`):

```glsl
// vértice                                  // fragmento
#version 330 core                           #version 330 core
layout(location=0) in vec2 aPos;            in vec3 vCol;
layout(location=1) in vec3 aCol;            out vec4 oColor;
uniform float uAspect;                      void main() { oColor = vec4(vCol, 1.0); }
out vec3 vCol;
void main() { vCol = aCol; gl_Position = vec4(aPos.x / uAspect, aPos.y, 0.0, 1.0); }
```

Triângulo com vértices `(-0.6,-0.5)` vermelho, `(0.6,-0.5)` verde, `(0.0,0.6)` azul. `uAspect = largura/altura` a cada quadro, para a proporção não esticar ao redimensionar. Use `SDL_GetWindowSizeInPixels` para o `glViewport`.

**`--screenshot arq.ppm`**: ao fim do último quadro (use junto com `--frames`), chame `save_ppm` antes de sair.

## Critério de pronto

```bash
cmake --build build/viewer3d                                              # sem avisos
./build/viewer3d/viewer3d --frames 30 --screenshot build/viewer3d/e2.ppm  # exit 0
python3 - <<'PY'
from PIL import Image
im = Image.open("build/viewer3d/e2.ppm").convert("RGB")
w, h = im.size
print(im.size, im.getpixel((5, 5)), len(set(im.getdata())) > 100)
PY
```

- O canto `(5,5)` é a cor do céu `(140, 173, 209)` (0.55, 0.68, 0.82 × 255, ±1) e há mais de 100 cores distintas (o gradiente do triângulo).
- Visualmente: triângulo com as três cores interpolando, que **mantém a proporção** ao redimensionar a janela.
- Nenhum erro de GL (`check` não lançou) em 30 quadros.

## Riscos e bloqueios

- **`#version 330 core` recusado**: o contexto não é 3.3 core; volte à etapa 1.
- **`glReadPixels` em buffer duplo**: leia do back buffer **antes** do `SDL_GL_SwapWindow` do último quadro (`glReadBuffer(GL_BACK)`).
- **VAO obrigatório no core**: sem VAO vinculado o desenho falha silenciosamente; crie um por conjunto de atributos.

## Fora do escopo

Matrizes, câmera, textura, leitura de arquivos.
