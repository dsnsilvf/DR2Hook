# Plano 01 — Python em camadas

Leia antes: [README dos planos](README.md) (regras e prova de saída).

## Por que

Hoje cada módulo do `tools/uiview` lê formato, converte para o formato de troca e escreve arquivos de saída no mesmo lugar:

- `track/export.py` (574 linhas) lê `objects.ens`, `trees.bin` e `ornaments.bin` **e** empacota DR2M/DR2I **e** exporta texturas **e** grava `track.json`.
- `car/carmodel.py` monta a árvore do carro a partir do PSSG **e** serializa o `.car.bin`.
- `content.py` decodifica textura **e** exporta imagens da UI **e** os diálogos.
- `mesh.py`, `export.py` e `content.py` repetem os mesmos auxiliares de PSSG (`_attr`, `_index`, `_ref`, meia precisão).

Isso impede reaproveitar o leitor sem arrastar a exportação, e é o oposto do que o app nativo vai precisar (um `dr2core` sem gráfico, sem UI). Este plano separa as camadas no Python, **sem mudar nenhuma saída**.

## Camadas e regra de dependência

```
tools/egodata/    CORE     contêineres e formatos. Só stdlib. Não grava saída de ferramenta.
tools/dr2assets/  ASSETS   conversão para formatos de troca/cache (DR2M, DR2I, DR2R, WebP). Importa só egodata.
tools/dr2edit/    EDIT     edição de pistas e escrita de .nefs novos. Importa egodata (e dr2assets se precisar).
tools/uiview/     APP      linhas de comando, orquestração, servidor, web. Importa todas as de baixo.
```

Dependência **só para baixo**: `egodata` não importa nada de fora de `egodata`; `dr2assets` não importa `dr2edit` nem `uiview`; `dr2edit` não importa `uiview`.

Decisão de nome: o core continua se chamando **`egodata`** (não renomear para `dr2core`). O `python -m tools.egodata` está documentado em vários lugares e o nome `dr2core` fica reservado para a futura biblioteca C++. Na documentação, chame `egodata` de "camada core".

Sem camadas de compatibilidade: nada de `from x import *` para manter caminhos velhos. Mova, atualize **todos** os imports, docs e testes no mesmo commit.

## Mapa de destino

### `tools/egodata/` (core) — recebe

| Origem | Símbolos | Destino |
| --- | --- | --- |
| `uiview/mesh.py`, `uiview/export.py`, `uiview/content.py` | `_attr`, `_ref`, `_index`, `_parents`, `_matrix`, `_world`, `_half`/`decode_half` (duplicados) | `egodata/pssg_util.py` (uma única cópia, nomes sem `_`) |
| `uiview/mesh.py` | `transform_point`, `multiply_matrix`, `_read_source`, `_lod_level`, `_keep_detailed_lod`, `_has_lod0`, `extract_meshes`, `GEOM_MAGIC` vai para `dr2assets` | `egodata/geometry.py` |
| `uiview/track/export.py` | `track_id`, `track_base`, `instances`, `_records`, `trees_bin`, `ornaments_bin`, `references`, `nick_index`, `object_renderables`, `route_points`, `near_route`, `shader_textures`, `pick_diffuse`, `node_meshes`, `_compose`, `_route_dirs`, `_gates_progress`, `_ai_line`, `_floats`, `_read_xml` | `egodata/track_formats.py` |
| `uiview/car/carmodel.py` | `_Builder`, `build_car_model`, `read_material`, `lod_label`, `_group_inputs`, `_skin_sets`, `_count`, `_bbox`, `_plain`, `_extra`, `_rounded`, `CAR_REV` | `egodata/car_model.py` |
| `uiview/car/models.py` | `package_list`, `open_package`, `classify_path`, `_files`, `_model_id`, `_group`, `surface_suffix`, `_car_cameras` (lê `cameras.xml`) | `egodata/packages.py` (acesso e classificação de pacotes) e `egodata/car_model.py` (`_car_cameras`) |

### `tools/dr2assets/` (assets) — novo

| Origem | Símbolos | Destino |
| --- | --- | --- |
| `uiview/mesh.py` | `pack_geom`, `unpack_geom`, `summarize_geom`, `_align`, `GEOM_MAGIC` | `dr2assets/dr2m.py` |
| `uiview/track/export.py` | `pack_instances`, `INST_MAGIC`, `_pack_meshes` | `dr2assets/dr2i.py` (+ `unpack_instances`, para o teste de ida e volta) |
| `uiview/car/carmodel.py` | `pack_resources`, `unpack_resources`, `_align` | `dr2assets/dr2r.py` |
| `uiview/content.py` | `dds`, `decode_texture`, `save_pair`, `texture_name`, `export_pssg_images`, `safe_name` | `dr2assets/textures.py` |
| `uiview/track/export.py` | `export_textures`, `_tex_file` | `dr2assets/textures.py` |

### `tools/dr2edit/` (edit) — novo

| Origem | Símbolos | Destino |
| --- | --- | --- |
| `uiview/track/edit.py` | `ens_spans`, `_clone`, `edit_ens`, `_retransform`, `edit_bin`, `_fmt` | `dr2edit/track_edits.py` |
| `uiview/track/edit.py` | `build_changes`, `check_blocks`, `apply`, `main` | `dr2edit/apply.py` (`python -m tools.dr2edit.apply` substitui `tools.uiview.track.edit`; `main` fica aqui) |

`tools/egodata/nefs_write.py` (`replace_files`) já é core e **fica**.

### `tools/uiview/` (app) — fica

- `__main__.py`, `serve.py` (passa a importar `dr2edit.apply`).
- `export.py`: `Reader`, `SceneExporter`, `export_screens`, `export_styles`, `_write_js`, `_load_js`, `_copy_web`, `_bust_cache`, `run`, `_refresh_models`. Dividir em `screens.py` (SceneExporter, export_screens, export_styles) e `export.py` (orquestração) é opcional.
- `content.py` reduzido a `ContentExporter` e `export_dialogs` (exportação das imagens e diálogos da UI).
- `car/models.py` reduzido a `export_models` e seus auxiliares de escrita (`_export_package`, `_export_car`, `_bind_textures`, `_decide`, `parse_decision`, `_cache_ok`...).
- `track/export.py` reduzido a `export_track`, `index_entry`, `write_index`, `main` (orquestra `egodata.track_formats` + `dr2assets`).
- `web/` intacto (plano 02).

## Passos

Faça um commit por passo. **Em todos:** `bash scripts/dev/test_tools.sh` verde e `golden_export.py` igual ao de antes (veja o [README](README.md)).

### Passo 0 — Linha de base
- Gere `build/antes.txt` com o golden (commit **não**: é só para comparar).
- Rode `bash scripts/dev/test_tools.sh` e anote: 20 + 5 + 23 = 48 testes.
- Escreva `tools/egodata/tests/test_layers.py` (teste de camadas, veja "Teste de camadas") já com as regras, marcando como `skip` os pacotes que ainda não existem. Ele passa a valer de verdade no passo 7.

### Passo 1 — Auxiliares de PSSG sem duplicata
- Crie `egodata/pssg_util.py` com uma cópia de cada auxiliar duplicado. Antes de apagar uma cópia, **compare os corpos**: se diferirem, pare e decida qual vale (provavelmente `mesh.py`, que é a mais usada) e registre no commit.
- Troque `mesh.py`, `export.py` e `content.py` para importar de lá. `content.py` hoje importa `export._attr/_index` dentro de funções (`content.py:145`, `content.py:270`) para evitar import circular; isso some.
- Pronto quando: nenhum `def _attr`, `def _index`, `def _ref` fora de `pssg_util.py` (`git grep -n "def _attr\|def _index\|def _ref" -- tools`).

### Passo 2 — Geometria
- `mesh.py` → divide: leitura em `egodata/geometry.py`, DR2M em `dr2assets/dr2m.py`. Crie `tools/dr2assets/__init__.py`.
- Mova `tests/test_mesh.py` para quem testa cada metade: o que toca `pack_geom/unpack_geom/summarize_geom` vai para `tools/dr2assets/tests/test_dr2m.py`; o que toca `extract_meshes/transform_point/decode_half` vai para `tools/egodata/tests/test_geometry.py`. Os testes de `classify_path`/`parse_decision` ficam com `models` (passo 6).
- Atualize `pyproject.toml` se precisar (já inclui `tools*`) e `scripts/dev/test_tools.sh` (adicione as novas pastas de teste).

### Passo 3 — Formatos de pista
- `track/export.py` → `egodata/track_formats.py` (parsers) e `dr2assets/dr2i.py` (`pack_instances`, `_pack_meshes`).
- Crie `unpack_instances` em `dr2assets/dr2i.py` e um teste de ida e volta (`pack_instances` → `unpack_instances`), usando os itens de `tools/uiview/tests/test_track.py`. O viewer nativo vai precisar desse formato; o teste fixa o contrato.
- `export_track` continua em `uiview/track/export.py` importando das duas camadas.

### Passo 4 — Modelo de carro
- `carmodel.py` → `egodata/car_model.py` (montagem) e `dr2assets/dr2r.py` (`pack_resources`/`unpack_resources`). `car/models.py` passa a importar de lá.
- `_car_cameras` (lê `cameras.xml`) vai para `egodata/car_model.py`.
- `tools/uiview/tests/test_carmodel.py` é dividido do mesmo modo que no passo 2.

### Passo 5 — Texturas
- `content.py`: `dds`, `decode_texture`, `save_pair`, `texture_name`, `export_pssg_images`, `safe_name` e, de `track/export.py`, `export_textures`/`_tex_file` → `dr2assets/textures.py`.
- Atenção: Pillow é importado dentro das funções (`from PIL import Image`). **Mantenha assim**: o core e os formatos não podem depender de Pillow.

### Passo 6 — Pacotes
- `car/models.py`: `package_list`, `open_package`, `classify_path`, `_files`, `_model_id`, `_group`, `surface_suffix` → `egodata/packages.py`. Importadores a atualizar: `track/export.py`, `track/edit.py` (depois `dr2edit`), `export.py`, os testes.
- Fica em `car/models.py` só o que escreve (`export_models` e auxiliares).

### Passo 7 — Edição
- `track/edit.py` → `dr2edit/track_edits.py` + `dr2edit/apply.py`; `tools/uiview/serve.py` passa a `from tools.dr2edit import apply as track_apply`.
- `tools/uiview/tests/test_track.py` se divide pelo que cada teste importa: `test_edit_*` e `test_trees_bin_roundtrip_and_edit` (usa `edit_bin`) → `tools/dr2edit/tests/test_track_edits.py`; `test_instances_*`, `test_spans_*`, `test_ornaments_bin_layout`, `test_pick_diffuse_*`, `test_near_route_*` → `tools/egodata/tests/test_track_formats.py`; `test_pack_instances_layout` → `tools/dr2assets/tests/test_dr2i.py`.
- Substitua `python -m tools.uiview.track.edit` por `python -m tools.dr2edit.apply` em **todos os docs** (`git grep -n "track.edit\|track_edit"`). Mantenha `python -m tools.uiview.track` e `python -m tools.uiview.car` (são o contrato do golden e dos docs).
- Remova o teste `skip` do passo 0: o teste de camadas agora cobre os três pacotes.

### Passo 8 — Fechamento
- Reduza `uiview/export.py` (opcional `screens.py`) conforme "tools/uiview/ (app) — fica".
- Atualize: `README.md` (tabela "Repository layout" e a linha de testes), `docs/tools/uiview.md` (bloco "Layout" e comandos), `docs/reverse_engineering/track_formats.md` (caminhos de arquivo citados), `docs/README.md` se citar caminhos, `SSOT.md` (linha das ferramentas), `pyproject.toml` (descrição). Rode `git grep -n "tools/uiview/mesh\|uiview/carmodel\|uiview.track.edit\|tools/uiview/track/"` e corrija o que sobrar.
- Rode a verificação de links dos `.md` (script na seção abaixo).

## Teste de camadas

`tools/egodata/tests/test_layers.py`, só com `ast` (sem dependências): para cada arquivo `.py` de um pacote (exceto `tests/`), coleta os `import`/`from ... import` que começam com `tools.` e confere:

| Pacote | Pode importar de `tools.` |
| --- | --- |
| `egodata` | só `tools.egodata` |
| `dr2assets` | `tools.egodata`, `tools.dr2assets` |
| `dr2edit` | `tools.egodata`, `tools.dr2assets`, `tools.dr2edit` |
| `uiview` | qualquer um dos quatro |

Também proíba `import PIL`/`numpy` no nível do módulo em `egodata` (importar dentro de função é permitido só em `dr2assets`).

## Verificação de links dos `.md`

```bash
python3 - <<'PY'
import os, re, subprocess
for f in subprocess.check_output(["git","ls-files","*.md"],text=True).split():
    if f.startswith("vendor/"): continue
    for n,l in enumerate(open(f,encoding="utf-8"),1):
        for m in re.finditer(r"\]\(([^)\s#]+)(#[^)\s]*)?\)",l):
            t=m.group(1)
            if re.match(r"[a-z]+:",t) or t.startswith("/"): continue
            if not os.path.exists(os.path.normpath(os.path.join(os.path.dirname(f),t))): print(f"{f}:{n}: {t}")
PY
```

Quatro linhas já aparecem hoje como "quebradas" por serem prosa que parece link (`ui_tabs.md:212` e três em `INV-01-static-report.md`); ignore essas.

## Pronto quando

1. `egodata`, `dr2assets`, `dr2edit` existem, cada um com `tests/`; `uiview` só orquestra.
2. `test_layers.py` passa; os 48 testes antigos continuam existindo (movidos) e mais os de ida e volta do DR2I.
3. `diff build/antes.txt build/depois.txt` vazio.
4. `python -m tools.uiview`, `python -m tools.uiview.track`, `python -m tools.uiview.car`, `python -m tools.uiview.serve`, `python -m tools.dr2edit.apply --help` e `python -m tools.egodata --help` funcionam.
5. Nenhum `.md` aponta para caminho que não existe mais.

## Riscos

- **Import circular escondido.** Muitos imports são feitos dentro de funções (`track/export.py:247`, `:372`, `content.py:145`). O passo 1 e a regra de camadas existem para eliminá-los; ao mover, prefira importar no topo do módulo.
- **Processos filhos.** `car/models.py:444` exporta pacotes com `ProcessPoolExecutor`. A função enviada ao pool (`_export_package`) precisa continuar importável pelo nome do módulo; scripts que a chamam sem `if __name__ == "__main__":` quebram (aconteceu nos testes deste plano). O `golden_export.py` passa por esse caminho: use-o.
- **Cache de modelos.** `data/model_cache.json` guarda `MESH_REV` (hoje em `car/models.py:30`, valor 2) e `CAR_REV` (`car/carmodel.py:53`, valor 1). Ao mover as constantes (`MESH_REV` acompanha o DR2M em `dr2assets/dr2m.py`; `CAR_REV` o DR2R em `dr2assets/dr2r.py`), **não mude os valores** (mudar invalida os caches do usuário e o golden).
- **Ordem de chaves nos `.json`.** Varia entre execuções (iteração de `set`); o golden já normaliza. Não "conserte" isso neste plano.

## Fora do escopo

Mudar formato de arquivo, otimizar, mexer em `web/`, tocar em `src/` ou `include/` (C++), renomear `egodata`.
