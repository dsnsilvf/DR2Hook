# Etapa 7 — Seleção e mover (primeira edição)

Antes: [README do viewer 3D](README.md) e [formatos.md](formatos.md). Anterior: [etapa 6](etapa_6_instancias.md) cumprida.

## Objetivo

Selecionar um objeto com o mouse, **movê-lo**, **girá-lo**, **apagá-lo**, desfazer/refazer e **gravar o `edits.json`** no mesmo formato do viewer web, de modo que `track/edit.py` gere um `.nefs` novo a partir dele. É a primeira estrutura de edição e fecha o MVP.

A lógica de escrever o `.nefs` **não** é reimplementada em C++: o app só produz o JSON. Isso mantém uma única implementação (Python, testada) da parte perigosa.

## Arquivos

- `src/render/pick.{hpp,cpp}` (novos): raio e interseção com a caixa local de cada tipo.
- `src/edit/history.{hpp,cpp}` (novos): histórico de edições por rota.
- `src/edit/edits_json.{hpp,cpp}` (novos): serialização do `edits.json`.
- `src/render/instances.cpp`: realce do selecionado e atualização de buffers quando uma matriz muda.
- `src/app/main.cpp`: mouse, teclas, `--out`.
- `tests/edit_test.cpp` (novo) e alvo `edit_test` (liga `dr2core` e `dr2edit`, sem GL).

## Implementação

**Picking** (porte de `tvRay` e `tvPickIndex`, em `trackview.js`):

1. Raio do mouse: `ndc = (2x/w − 1, 1 − 2y/h)`; `origem = eye`; direção a partir da câmera orbital (`f = normalize(target − eye)`, `r = normalize(−f.z, 0, f.x)`, `u = cross(r, f)`, `d = normalize(f + r*ndc.x*tan(fov/2)*aspect + u*ndc.y*tan(fov/2))`).
2. Para cada instância **visível** (camada ligada, não apagada, dentro do raio de desenho exceto `dist`): leve o raio ao espaço local do objeto — `p_local = (p − posição) · R⁻¹` onde `R` é o bloco 3×3 por **linhas** (convenção de vetor-linha; `tvInvAffine` calcula a inversa) — e teste o raio contra a caixa local `lo/hi` do **tipo** (método das *slabs*, `t0 = max(...)`, `t1 = min(...)`). Fica a instância com o menor `t0`.
3. Clique sem arraste (`moved < 5 px`) com o botão esquerdo seleciona; clique no vazio desseleciona (Esc também).

**Realce:** desenhe a instância selecionada de novo com `uHi = 1` (mistura com laranja `(1.0, 0.6, 0.15)` a 55%, como `uHi * 0.55` do web). O título mostra `tipo | kind | idnum | x y z`.

**Ferramentas** (teclas `1`, `2`, `3`, como no web): `1` navegar (etapa 3), `2` mover, `3` girar.

| Ferramenta | Arrastar com o botão esquerdo sobre o selecionado |
| --- | --- |
| Mover | Ponto do raio no plano horizontal `y = y_do_objeto` (`tvGround`): `pos.xz = base.xz + (p − p_início).xz`. Com **Shift**: `pos.y = base.y − (mouse_y − mouse_y_início) * max(0.02, dist*0.002)` |
| Girar | `tvSpin(base, (mouse_x − mouse_x_início) * 0.01)`: gira as **três linhas** da matriz em torno de Y: `x' = x*cos + z*sin`, `z' = −x*sin + z*cos`; a posição não muda |

Outras teclas: **Delete** apaga o selecionado (marca `hidden`; não remove do array); **Ctrl+Z** desfaz, **Ctrl+Shift+Z** ou **Ctrl+Y** refaz; **F** enquadra o selecionado (`target = posição`; `dist = max(6, diagonal(lo,hi) * 3)`); **S** grava o JSON.

**Histórico** (porte de `tvSnap/tvCommit/tvHistGo`): ao soltar o mouse (ou ao apagar) empilhe `{rótulo, antes[], depois[]}` com `antes/depois = {índice, 12 floats, hidden}`; ignore se nada mudou; truncar o futuro ao empilhar; no máximo **300** passos **por rota**; Ctrl+Z aplica `antes`, refazer aplica `depois`. A cada mudança invalide a chave de corte (etapa 6) para reenviar os buffers.

**`edits.json`** (formato em [formatos.md](formatos.md#pistaeditsjson)): `{"format":"dr2-track-edits","version":1,"track":<id>,"src":<track.json.src>,"edits":[...]}`. Uma entrada por instância **movida** (alguma das 12 floats de `m` ≠ `m0`) ou **apagada**:

```json
{"route":"route_0","kind":"e","type":"core_barr_aframe_a~a","index":41,"deleted":false,"m":[12],"m0":[12]}
```

`kind` = primeira letra do nome do tipo; `type` = nome depois de `k:`; `index` = `idnum`; `m0` = a matriz original lida do DR2I (guarde uma cópia ao carregar). **Não** implemente duplicar nesta etapa (o Python aceita, mas é extra). Escreva com `snprintf("%.9g")` nos floats para ida e volta exata. `S` grava em `--out` (padrão `build/uiview/saves/<id>.edits.json`, criando a pasta) e imprime o caminho e a contagem de edições.

**`--out`**: caminho do JSON. **Recuse** (erro e código 1) um caminho dentro da pasta do jogo (`.../steamapps/common/DiRT Rally 2.0`) — o `track/edit.py` faz a mesma verificação para o `.nefs`.

## Critério de pronto

```bash
cmake --build build/viewer3d
./build/viewer3d/edit_test                      # histórico e serialização, sem GL
./build/viewer3d/viewer3d --track build/uiview/tracks/portugal__montalegre_rallycross   # uso interativo (abaixo)
```

1. **`edit_test`** (asserts): mover → desfazer → refazer devolve as mesmas floats; apagar e desfazer devolve `hidden = 0`; histórico trunca o futuro e respeita o limite de 300; a serialização só inclui instâncias alteradas, com `kind`/`type`/`index` certos; o JSON gerado é lido de volta pelo `json.cpp` da etapa 4 e tem as 12 floats exatas.
2. **Interativo** (anote no commit): selecionar uma barreira `e:` (o título mostra `kind e`), `2` e arrastar, `3` e girar, `Delete` em outra, `Ctrl+Z` duas vezes, `Ctrl+Y` uma, `S`.
3. **Ida e volta com o Python** — a prova de que o MVP fecha:

```bash
python3 -m tools.uiview.track.edit build/uiview/saves/portugal__montalegre_rallycross.edits.json \
        -o build/uiview/saves/portugal__montalegre_rallycross.nefs
python3 - <<'PY'
import json, sys
sys.path.insert(0, ".")
from tools.egodata.nefs import NefsArchive
from tools.uiview.track import export as track           # após o plano 01: tools.egodata.track_formats
doc = json.load(open("build/uiview/saves/portugal__montalegre_rallycross.edits.json"))
arc = NefsArchive.open_path("build/uiview/saves/portugal__montalegre_rallycross.nefs")
base = track.track_base(arc)
cache = {}
for e in doc["edits"]:
    if e["kind"] != "e" or e["deleted"]: continue
    ens = cache.setdefault(e["route"], track.instances(arc.read(f"{base}{e['route']}/objects.ens")))
    m = ens[e["index"]]["m"]                              # 16 floats, linha-maior
    got = [m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10], m[12], m[13], m[14]]
    assert all(abs(a - b) < 1e-3 for a, b in zip(got, e["m"])), (e["index"], got, e["m"])
print("OK", len(doc["edits"]), "edições conferidas")
PY
```

   O `track.edit` não pode dar erro (ele confere os blocos de 64 KiB), o `.nefs` sai em `build/uiview/saves/` (≈884 MB na Montalegre, ~1 s) e o script imprime `OK`. **Não** teste esse `.nefs` dentro do jogo; isso é decisão do dono.

## Riscos e bloqueios

- **Formato do JSON divergente** é o risco principal: o Python lê `route`, `kind`, `type`, `index`, `deleted`, `m` (e `added`/`src` nas cópias). Confira com `python -m tools.uiview.track.edit` antes de dar a etapa por feita.
- **Índice errado.** `index` é o `idnum` do DR2I, não a posição no array do app (as duas coincidem na Montalegre porque as cópias da sessão não existem aqui, mas não conte com isso).
- **Convenção de vetor-linha** no picking e na rotação: um erro de transposição faz o clique acertar o lugar errado ou o objeto girar para o lado errado. O `tvSpin` acima já está no sentido certo; use-o literalmente.
- **Apagar ornamento/árvore** (`kind` `o`/`t`): o Python aceita (zera a matriz e põe `y = -10000`). Mover também. Só **cópias** de `o`/`t` são recusadas.
- **Arraste e histórico:** empilhe **uma** entrada por arraste (ao soltar), não uma por movimento do mouse.

## Fora do escopo

Duplicar, editar numericamente, múltipla seleção, gizmo, outras rotas, escrever `.nefs` em C++, qualquer teste no jogo.

## Depois desta etapa

O MVP está fechado. Atualize o quadro "Estado" no [README](README.md), escreva em `tools/viewer3d/README.md` o que ficou sem fazer, e **pare**: o que vem depois (Polônia, `dr2core` com PSSG, ImGui, Windows) é um novo plano, decidido pelo dono.
