# Etapa 6 — Objetos instanciados

Antes: [README do viewer 3D](README.md) e [formatos.md](formatos.md). Anterior: [etapa 5](etapa_5_texturas.md) cumprida.

## Objetivo

Desenhar na GPU as **2897 instâncias** da rota `route_0` da Montalegre (barreiras, cercas, árvores, ornamentos) com `glDrawElementsInstanced`, texturizadas, com corte por distância e camadas liga/desliga, a 60 FPS ou mais na RTX 4050.

## Arquivos

- `src/core/dr2i.{hpp,cpp}` (novos): leitor de DR2I.
- `src/core/track.{hpp,cpp}`: expõe `types`, `type_order`.
- `src/render/instances.{hpp,cpp}` (novos): biblioteca de tipos, buffers por tipo, corte, desenho.
- `src/render/texture.cpp`: carrega também as texturas dos materiais `o|` e `t|`.
- `src/app/main.cpp`: teclas de camada, `--vsync 0`.
- `tests/core_tests.cpp`: casos do DR2I.

## Implementação

**`dr2i`**: `struct Instances { uint32_t n; std::vector<uint16_t> type; std::vector<uint32_t> idnum; std::vector<float> m /*12n*/; }` e `read_dr2i(bytes)` conforme a [especificação](formatos.md#dr2i--instâncias-de-uma-rota) (o array `uint16 tipo[n]` é preenchido a múltiplo de 4 antes de `idnum`). Valide a magia `DR2I`, o tamanho exato (`8 + pad4(2n) + 4n + 48n`) e `tipo[i] < type_order.size()`.

**Biblioteca de tipos** (`objects.bin`): leia-o com `read_dr2m` (802 malhas na Montalegre). Para cada nome em `type_order[t]`: `types[nome] = {first, count}` dá a faixa de malhas em `objects.bin`. Tipos com `count == 0` não têm malha: **pule**. Por tipo guarde as malhas (VBO/IBO próprios ou faixas de um VBO/IBO único) e a **caixa local** `lo/hi` (mín./máx. das posições de todas as malhas do tipo); a etapa 7 precisa dela.

**Tipos de camada** (igual a `tvKind`): `t:...` com `_dist_` no nome → `dist`; outros `t:` → `tree`; `e:` e `o:` → `obj`.

**Instanciamento.** Por tipo, um VBO dinâmico de `12 * visíveis` floats (as linhas 0, 1, 2 da matriz e a posição). Atributos por instância com `glVertexAttribDivisor(loc, 1)`: `aX = (m0,m1,m2)`, `aY = (m3,m4,m5)`, `aZ = (m6,m7,m8)`, `aP = (m9,m10,m11)`, quatro `vec3`. Vertex shader (idêntico ao de `tvGL` em `trackview.js`):

```glsl
#version 330 core
layout(location=0) in vec3 aPos;  layout(location=1) in vec2 aUv;
layout(location=3) in vec3 aX; layout(location=4) in vec3 aY; layout(location=5) in vec3 aZ; layout(location=6) in vec3 aP;
uniform mat4 uVp; out vec3 vW; out vec2 vUv;
void main() { vec3 w = aX*aPos.x + aY*aPos.y + aZ*aPos.z + aP; vW = w; vUv = aUv; gl_Position = uVp * vec4(w, 1.0); }
```

(Vetor-linha: **não** transponha a matriz no C++; o shader acima já faz a conta certa. Para terreno sem instância, mantenha o programa da etapa 5 ou use atributos constantes com a identidade.) Fragmento: o da etapa 5 com `if (uCut == 1 && tx.a < 0.4) discard;` para materiais que **não** começam com `g` (folhas e grades têm alfa).

**Corte por distância** (de `tvCull`): agrupe as instâncias por tipo uma vez (`groups[tipo] = [índices]`). A cada vez que mudar a chave `round(target/8)`, `round(dist/8)`, camadas, distância de desenho ou revisão de edição, refaça o buffer de cada tipo com as instâncias cujo `(x - target.x)² + (z - target.z)² <= R²` (`R = 700 m` por padrão). Instâncias do tipo `dist` **não** são cortadas. Instâncias `hidden` (apagadas na etapa 7) ficam fora.

**Teclas** (as barras de camadas do web, como atalhos): `F1` terreno, `F2` objetos (`e`/`o`), `F3` árvores, `F4` terreno distante, `[` e `]` distância de desenho ∓100 m (limites 100–4000). O título mostra `inst visíveis/total`.

**`--vsync 0`**: `SDL_GL_SetSwapInterval(0)`, para medir FPS sem o teto do monitor.

## Critério de pronto

```bash
cmake --build build/viewer3d
./build/viewer3d/core_tests --track build/uiview/tracks/portugal__montalegre_rallycross
__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia ./build/viewer3d/viewer3d \
    --track build/uiview/tracks/portugal__montalegre_rallycross --vsync 0 --frames 600 --screenshot build/viewer3d/e6.ppm
```

- `core_tests`: o DR2I de `route_0` tem **2897** instâncias (igual a `routes[0].instances`); todos os `tipo[i]` são válidos; arquivo truncado lança.
- O `--frames 600` imprime FPS médio **≥ 60** na RTX com a câmera inicial (registre o número). Se não chegar, o primeiro suspeito é refazer os buffers todo quadro (a chave de corte deve mudar só quando a câmera se mexe) ou um desenho por malha sem agrupar por material.
- Visualmente, comparando com o web na mesma câmera: barreiras de concreto e cercas ao longo da pista, árvores, prédios dos boxes; contagem de instâncias visíveis parecida (não precisa ser idêntica ao dígito: o web corta pelo mesmo critério, então deve ficar muito próxima).
- `F2`/`F3` ligam e desligam as camadas; `]` aumenta a quantidade visível.

## Riscos e bloqueios

- **Matriz transposta.** O sintoma é objetos "deitados", espelhados ou no lugar errado: confira que `aX/aY/aZ` são as linhas, não as colunas.
- **Tipos sem malha** e **malhas sem material**: ignore sem erro.
- **Tipos de uma malha só com milhares de instâncias**: um desenho instanciado por tipo resolve; não crie um VBO por instância.
- **Alfa/ordenação**: o web usa `discard` e não ordena transparência; faça igual. Mistura (blending) fica fora do MVP.
- **Rotas**: só a `route_0` (ou a primeira). Trocar de rota não é do MVP.

## Fora do escopo

LOD, oclusão, rotas além da primeira, linhas de portão e da IA (se quiser desenhá-las, é um adicional pequeno e opcional: `GL_LINE_STRIP` a partir de `progress.gates` e `ai[].pts`, com as cores de [formatos.md](formatos.md)).
