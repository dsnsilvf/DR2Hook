# Etapa 4 — Terreno da Montalegre (primeiro asset real)

Antes: [README do viewer 3D](README.md) e [formatos.md](formatos.md). Anterior: [etapa 3](etapa_3_camera.md) cumprida.

## Objetivo

Ler em C++ o `track.json` e o `terrain_0.bin` (DR2M) exportados pelo Python e desenhar o terreno da Montalegre na GPU, com cor fixa e iluminação por derivadas, orbitável. É o marco "primeiro asset real do jogo na GPU".

## Arquivos

- `src/core/json.{hpp,cpp}` (novos): leitor de JSON mínimo.
- `src/core/dr2m.{hpp,cpp}` (novos): leitor de DR2M.
- `src/core/track.{hpp,cpp}` (novos): `track.json` → estrutura.
- `src/render/terrain.{hpp,cpp}` (novos): sobe as malhas para a GPU e desenha.
- `src/app/main.cpp`: `--track DIR`, enquadramento inicial, título com contagens.
- `tests/core_tests.cpp` (novo) e alvo `core_tests` (liga só `dr2core`).
- `CMakeLists.txt`: alvo `dr2core` (biblioteca estática: `json`, `dr2m`, `track`), **sem** SDL/GL/GLEW.

## Implementação

**`json`**: valor variante (`null`, `bool`, `double`, `string`, `vector<Value>`, mapa ordenado). Aceita o que o `json.dump` do Python produz com `separators=(",",":")`: objetos, listas, números (inclusive `-1.5e-05`), strings com escapes `\" \\ \/ \b \f \n \r \t \uXXXX` (os nomes de tipo têm `|`, `~`, `!`, `>` e `:`; nenhum precisa de `\u` hoje, mas trate), `true`/`false`/`null`. Erro de sintaxe lança `std::runtime_error` com o deslocamento. Sem dependências. (Não há nlohmann/json na máquina e não deve ser baixado neste MVP.)

**`dr2m`**: `struct Mesh { std::string name, material; uint32_t verts, indices; std::vector<float> pos /*3n*/, uv /*2n*/; std::vector<uint8_t> col /*4n ou vazio*/; std::vector<uint32_t> idx; }` e `std::vector<Mesh> read_dr2m(const std::vector<uint8_t>&)` seguindo a [especificação](formatos.md#dr2m--malhas) **exatamente** (alinhamento a 4 depois dos nomes e depois dos índices; `flags & 1` = índices de 32 bits). Converta os índices para `uint32_t` ao ler. Valide: magia `DR2M`, nenhum acesso além do fim do arquivo, nenhum índice `>= verts` (lance com o nome da malha). Use `memcpy` para ler floats (não assuma alinhamento do buffer).

**`track`**: lê `track.json` e guarda por enquanto: `id`, `routes[]` (`name`, `terrain.file/meshes/verts`, `instances`, `progress.gates[]` com `l`,`r`, `ai[]` com `pts`). O resto (tipos, materiais) fica para as etapas 5 e 6, mas **guarde o `json::Value` bruto** para não reler.

**`terrain`**: um VBO único com `[pos3 uv2]` de todas as malhas concatenadas e um IBO único de `uint32` com o `vertex offset` de cada malha já somado aos índices; por malha, guarde `{first_index, index_count, material}` (será usado na etapa 5). Desenhe com `glDrawElements` por malha (1324 chamadas é aceitável) ou agrupando intervalos contíguos. Shader: igual ao da etapa 3 mas com a luz do web: normal por `normalize(cross(dFdx(vW), dFdy(vW)))` e `light = abs(dot(n, normalize(vec3(0.35,0.85,0.4)))) * 0.7 + 0.3`; cor base `(0.42, 0.45, 0.34)`. Sem culling.

**Enquadramento inicial** (de `tvFrameRoute`): caixa que contém todos os pontos `ai[].pts` e `progress.gates[].l/r` de todas as rotas; `target` = centro da caixa; `dist = max(60, hypot(dx, dz) * 0.9)`; `yaw = 0.8`, `pitch = 0.7`.

**Título**: `... | route_0 | 1324 malhas | 428 789 vértices | 557 900 tri | <fps> fps`.

## Critério de pronto

```bash
python3 -m tools.uiview.track --tracks montalegre -o build/uiview            # dados (se ainda não existem)
cmake --build build/viewer3d
./build/viewer3d/core_tests --track build/uiview/tracks/portugal__montalegre_rallycross
./build/viewer3d/viewer3d --track build/uiview/tracks/portugal__montalegre_rallycross --frames 60 --screenshot build/viewer3d/e4.ppm
```

- `core_tests` (executável com `assert`s) confere: o parser de JSON em casos pequenos (aninhados, escapes, números); o DR2M da Montalegre dá **1324 malhas, 428 789 vértices, 557 900 triângulos** (os mesmos números do Python; veja o snippet em [formatos.md](formatos.md#como-validar-o-seu-leitor-contra-o-python)) e batem com `routes[0].terrain.meshes/verts` do `track.json`; um DR2M truncado e um com índice fora da faixa lançam.
- Sem `--frames`, o terreno aparece com relevo e luz, dá para orbitar, e o título mostra as contagens iguais às acima.
- O `e4.ppm` não é só céu (mais de 1000 cores distintas).
- Imprima no `stderr` o tempo de leitura do `terrain_0.bin` (12 MB) e o de envio à GPU, e registre os dois números no commit. Não há meta de tempo nesta etapa; o número serve de linha de base para a Polônia.

## Riscos e bloqueios

- **É a primeira vez que C++ lê o formato.** Qualquer divergência aparece como contagem diferente: compare **malha a malha** (`name`, `material`, `verts`, `indices`) com `unpack_geom` antes de desenhar.
- **Índices de 16 e 32 bits** misturados no mesmo arquivo: a conversão para 32 bits na leitura evita ramificação no desenho.
- **Precisão float**: as posições do terreno estão em coordenadas do jogo (a Montalegre fica perto de `y ≈ 1430`); GL em `float` aguenta, mas não subtraia o `target` em `float` depois de multiplicar por matriz grande; use `view` que já translada.
- **Polônia não é meta aqui.** Se alguém tentar abri-la, o programa deve avisar o tamanho (`verts > 5 milhões`) e continuar sem travar o MVP; não otimize para ela agora.

## Fora do escopo

Texturas (etapa 5), objetos (etapa 6), rotas além da primeira, LOD.
