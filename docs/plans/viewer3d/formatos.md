# Formatos de entrada e saída do viewer 3D

Especificação byte a byte do que o Python exporta (e o viewer nativo lê) e do que o viewer grava. **A fonte de verdade é o código**; este documento foi conferido contra ele em 2026-10-05 e contra a pista Montalegre exportada.

| Formato | Escrito por | Lido hoje por |
| --- | --- | --- |
| DR2M (malhas) | `pack_geom` em `tools/uiview/mesh.py` | `tvParse` em `trackview.js` |
| DR2I (instâncias) | `pack_instances` em `tools/uiview/track/export.py` | `tvParseInst` em `trackview.js` |
| `track.json` | `export_track` em `tools/uiview/track/export.py` | `tvOpen` e outros em `trackview.js` |
| `tex/*.webp` | `export_textures` (Pillow, WebP) | `tvTexture` |
| `<pista>.edits.json` | `tvDoc` em `trackview.js` | `tools/uiview/track/edit.py` |

(Se o [plano 01](../01_python_camadas.md) já foi aplicado, os módulos Python estão em `tools/egodata/`, `tools/dr2assets/` e `tools/dr2edit/`; os formatos não mudam.)

Tudo é **little-endian**, `float32` IEEE, sem compressão. Unidade: metro. Eixos: **Y para cima**; X e Z horizontais (o mesmo sistema do jogo; nenhuma conversão é feita na exportação).

## Pasta de uma pista

```
build/uiview/tracks/<id>/
  track.json            índice, rotas, tipos, materiais (≈108 KB na Montalegre)
  terrain_<n>.bin       DR2M: blocos do terreno e do fundo, por rota (11,9 MB)
  objects.bin           DR2M: malhas dos tipos de objeto, árvore e ornamento (20 MB)
  inst_<rota>.bin       DR2I: instâncias da rota (156 KB)
  tex/*.webp            uma textura de cor por material (381 arquivos, 12 MB)
build/uiview/data/tracks.js   índice de todas as pistas exportadas (JS, não é lido pelo nativo)
```

Gerar: `python -m tools.uiview.track --tracks montalegre -o build/uiview` (~15 s). `<id>` da Montalegre: `portugal__montalegre_rallycross`.

## DR2M — malhas

```
char[4]  "DR2M"
uint32   n_malhas
n_malhas vezes:
    uint16  tamanho_nome        uint16  tamanho_material
    uint32  n_vertices          uint32  n_indices
    uint32  flags               (bit 0: índices de 32 bits; bit 1: há cor por vértice)
    bytes   nome[tamanho_nome]  bytes   material[tamanho_material]   (UTF-8, sem terminador)
    <preenche com zeros até múltiplo de 4>
    float32 posicao[n_vertices][3]
    float32 uv[n_vertices][2]
    uint8   cor[n_vertices][4]          (só se flags & 2; RGBA)
    uint16 ou uint32 indice[n_indices]  (uint32 se flags & 1)
    <preenche com zeros até múltiplo de 4>
```

- Triângulos soltos (lista), 3 índices por triângulo. O sentido do enrolamento não foi normalizado: **desligue o culling de faces** (o viewer web também não faz culling).
- O `material` tem prefixo que diz de onde veio: `g|` terreno (`tracksplit.pssg`), `o|` objetos/ornamentos, `t|` árvores. O prefixo faz parte da chave em `track.json → materials`.
- Arquivo cheio, sem índice: para saltar uma malha sem lê-la, some `16 + tamanho_nome + tamanho_material`, alinhe a 4, e some `n_vertices * (24 se flags&2 senão 20) + n_indices * (4 ou 2)`, alinhe a 4. É o que faz `summarize_geom`.
- Referência de leitura: `tvParse` (JS) e `unpack_geom` (Python). Os arrays são alinhados a 4 bytes dentro do arquivo, então dá para usar o buffer mapeado em memória direto como `float*`.

Montalegre: `terrain_0.bin` tem 1324 malhas e 428 789 vértices (confere com `routes[0].terrain`); `objects.bin` tem 802 malhas.

## DR2I — instâncias de uma rota

```
char[4]  "DR2I"
uint32   n
uint16   tipo[n]          índice em track.json → type_order      <preenche com zeros até múltiplo de 4>
uint32   idnum[n]         posição do registro no arquivo de origem (veja abaixo)
float32  m[n][12]         linhas 0..2 da matriz 3×3 (já com escala) e depois a posição
```

`m[i][0..2]` = linha 0 (eixo X do objeto), `[3..5]` = linha 1 (Y), `[6..8]` = linha 2 (Z), `[9..11]` = posição no mundo. **Convenção de vetor-linha**: `mundo = p.x * linha0 + p.y * linha1 + p.z * linha2 + posição`. Em GLM/GL (colunas), monte `mat4(vec4(linha0,0), vec4(linha1,0), vec4(linha2,0), vec4(posição,1))` e use `M * vec4(p,1)`. É exatamente o que o vertex shader do web faz com `aX*p.x + aY*p.y + aZ*p.z + aP`.

`idnum` identifica a instância no arquivo de origem e é o que o editor devolve em `edits.json → index`: para `e:` (objetos) é o índice da instância no `objects.ens` (ordem do documento, ignorando as inutilizáveis); para `o:` (ornamentos) é o registro em `ornaments.bin`; para `t:` (árvores) é o registro em `trees.bin`. O viewer web usa `0x80000000 + índice da origem` para **cópias** feitas na sessão (`TV_ADDED`); o arquivo exportado nunca tem valores assim.

Montalegre `route_0`: 2897 instâncias (confere com `routes[0].instances`).

## `track.json`

Campos de primeiro nível (Montalegre):

| Campo | Tipo | Conteúdo |
| --- | --- | --- |
| `id`, `src`, `base` | string | `portugal__montalegre_rallycross`, `locations/portugal__montalegre_rallycross.nefs`, `tracks/locations/portugal/montalegre_rallycross/` |
| `terrain` | `{meshes, verts}` | totais do pacote (2075 blocos, 472 069 vértices) — **não** o da rota |
| `routes` | lista | uma por `route_N` (veja abaixo) |
| `types` | `{nome: {node, first, count}}` | 324 tipos. `first`/`count` = faixa de malhas **em `objects.bin`** (`count == 0` = tipo sem malha; não desenhar) |
| `type_order` | `[nome]` | a ordem dos tipos; o `tipo[i]` do DR2I indexa esta lista |
| `materials` | `{material: "tex/arquivo.webp"}` | 820 materiais → textura de cor. Material ausente = sem textura (usar cor fixa) |
| `textures` | `{wanted, found}` | só estatística |

Nome de tipo: `<k>:<nome>` onde `k` é `e` (objeto de `objects.ens`), `o` (ornamento) ou `t` (árvore). Árvores cujo nome contém `_dist_` são "terreno distante" (`dist`): desenhadas sem corte por distância. Montalegre: 190 `o`, 108 `t`, 26 `e`.

Cada item de `routes`:

| Campo | Conteúdo |
| --- | --- |
| `name` | `route_0` (nome do arquivo `inst_<name>.bin`) |
| `terrain` | `{file, meshes, verts}`: qual `terrain_<n>.bin` e as contagens (1324 malhas, 428 789 vértices) |
| `instances` | quantas instâncias no DR2I |
| `progress` | `{routes: [...], gates: [{d, l:[x,y,z], r:[x,y,z]}...]}`: portões de progresso (distância `d` ao longo da pista, ponto esquerdo e direito). 143 portões na Montalegre |
| `ai` | lista de `{name, pts: [[x,y,z]...]}`: linhas da IA. `name == "default"` é a principal. 164 pontos na Montalegre |
| `ens_ids` | o `id` de texto de cada instância do `objects.ens`, indexado por `idnum`; só serve para rotular a seleção |
| `replay` | opcional, só nas pistas do `synthtrack`: câmeras do replay (`cameras`: `name`, `kind` trackside/static/dolly, `role`, `s`, `pos`, `aim`, e nas dolly `path`/`target` em Bézier de 4 pontos e `duration`), zonas de troca (`zones`: `name`, `s`, `l`, `r`, `lap`, `switch: [{camera, p}]`) e prismas (`bounds`: `name`, `corners` xz, `y0`, `y1`). Detalhes em [`synthtrack.md`](../../tools/synthtrack.md); o viewer desenha com a tecla C |
| `grids` | opcional, só nas pistas do `synthtrack`: vagas de largada, uma entrada por grade (`name`, `role`, `pos`, `fwd`, `s`, `slots`, `markers`). Cada vaga tem `name`, `pos` (centro do carro), `fwd` (unitário), `s`, `lat` e `size` [largura, comprimento]; as `markers` têm só `name`, `pos` e `fwd`. Detalhes em [`synthtrack.md`](../../tools/synthtrack.md); o viewer desenha com a tecla L |

As rotas repetem a contagem de distância `d`: quando `d` diminui, é outra volta/ramo; o viewer web **quebra a linha** aí (`tvLinesFrom`). Cores usadas no web: portão esquerdo `(0.95, 0.4, 0.4)`, direito `(0.4, 0.6, 1.0)`, degraus `(0.5, 0.5, 0.55)`, IA principal `(0.3, 0.95, 0.4)`, IA outras `(0.95, 0.8, 0.25)`.

Estatística do que o web usa para desenhar:

- Cor base do terreno `(0.42, 0.45, 0.34)`, dos objetos `(0.8, 0.75, 0.7)`, céu `(0.55, 0.68, 0.82)`.
- Luz: sem normais no arquivo; o shader calcula a normal por derivadas de tela (`cross(dFdx(w), dFdy(w))`) e usa `abs(dot(n, normalize(0.35, 0.85, 0.4))) * 0.7 + 0.3`. Em OpenGL 3.3 core isso é `dFdx/dFdy` do GLSL 330, que existe sem extensão.
- Material com prefixo `g` (terreno): sem corte por alfa. Outros: `discard` se `alfa < 0.4` (folhas, grades).
- A textura vem dos `materials`; as `webp` têm canal alfa quando precisam.

## `<pista>.edits.json`

```json
{ "format": "dr2-track-edits", "version": 1, "track": "<id>", "src": "locations/....nefs",
  "edits": [
    { "route": "route_0", "kind": "e", "type": "core_barr_aframe_a~a", "index": 41,
      "deleted": false, "m": [12 floats], "m0": [12 floats] },
    { "route": "route_0", "kind": "e", "type": "...", "added": true, "src": 41, "index": -1,
      "deleted": false, "m": [12], "m0": [12] }
  ] }
```

- `kind` = primeiro caractere do tipo (`e`, `o`, `t`); `type` = nome sem o `k:`.
- `index` = `idnum` do DR2I; `m` = matriz nova; `m0` = a original (12 floats cada, mesma ordem do DR2I).
- Só entram instâncias **alteradas ou apagadas** (`deleted: true`) e **cópias** (`added: true`, `src` = índice da instância copiada, `index: -1`; só para `kind: "e"`).
- Apagar ornamento ou árvore zera a matriz e põe `y = -10000` no registro (a contagem é fixa); cópias de `o`/`t` são recusadas pelo Python.
- Quem aplica: `python -m tools.uiview.track.edit <arquivo> -o build/uiview/saves/<pista>.nefs` (ou `tools.dr2edit.apply` após o plano 01). O viewer nativo **só grava este JSON**; não escreve `.nefs`.

## Como validar o seu leitor contra o Python

```bash
python3 - <<'PY'
import struct, sys
sys.path.insert(0, ".")
from tools.uiview.mesh import unpack_geom           # após o plano 01: tools.dr2assets.dr2m
tid = "portugal__montalegre_rallycross"
ms = unpack_geom(open(f"build/uiview/tracks/{tid}/terrain_0.bin", "rb").read())
print(len(ms), sum(len(m["positions"]) for m in ms), sum(len(m["indices"]) // 3 for m in ms))
PY
```

Na Montalegre (`terrain_0.bin`) a saída é `1324 428789 557900` (malhas, vértices, triângulos). Esses três números são o que o leitor C++ tem de reproduzir **exatamente** (etapa 4). Para o DR2I use `n` do cabeçalho contra `routes[0].instances`.
