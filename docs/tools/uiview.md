# DR2 UI Viewer (`tools/uiview/`)

Ferramentas de navegador que leem os arquivos do próprio jogo e os mostram localmente. Elas rodam **fora do jogo**, só leem a pasta do jogo e gravam apenas em `build/uiview/` (a pasta de saída). Nada aqui altera a pasta do jogo.

Requisitos: Python 3 com Pillow (texturas) e o jogo instalado (o caminho vem de `tools.egodata.cli.DEFAULT_GAME`; troque com `--game`), e um navegador com WebGL. As dependências opcionais estão no `pyproject.toml`.

## Abas

| Aba | Fonte | Documento |
| --- | --- | --- |
| **Telas**, **Cenas**, **Imagens**, **Textos**, **Diálogos** | UI de `game_1.dat`, imagens, textos `.lng` | [ui_screens.md](ui_screens.md) |
| **Modelos** | índice 3D de todos os pacotes; preview do LOD0 | [ui_screens.md](ui_screens.md#abas) |
| **Carros** | `cars/*.nefs` (PSSG) | [car_explorer.md](car_explorer.md): árvore real do carro, texturas, mover e girar com desfazer, câmeras do jogo |
| **Pistas** | `locations/*.nefs` | [track_explorer.md](track_explorer.md): terreno, objetos, árvores, limites, linha da IA; mover, girar, apagar, duplicar e gravar um `.nefs` novo |

## Comandos

```bash
# telas, imagens e modelos (saída padrão: build/uiview)
python -m tools.uiview [--game PASTA] [-o SAIDA] [--scene-images-only] [--models 037] [--all-models] [-j N] [--open]

# só a malha e as texturas de alguns carros (não atualiza o índice da página; veja car_explorer.md)
python -m tools.uiview.car --models 037 -o build/uiview [--force]

# exporta uma ou mais pistas (--tracks vazio só reindexa)
python -m tools.uiview.track --tracks montalegre,poland_rally_01 -o build/uiview

# serve build/uiview em 127.0.0.1 e liga o botão "Salvar .nefs"
python -m tools.uiview.serve [--port 8790] [--root build/uiview]

# aplica um arquivo de edições num .nefs NOVO (nunca dentro da pasta do jogo)
python -m tools.uiview.track.edit montalegre.edits.json -o build/uiview/saves/montalegre.nefs
```

## Testes

```bash
bash scripts/dev/test_tools.sh                      # testes Python de egodata, dr2rec e uiview (não precisa do jogo)
python3 -m unittest discover -s tools/uiview/tests -t .
python3 scripts/dev/web_smoke.py                    # abre a página num navegador headless e falha em erro de JavaScript
python3 scripts/dev/golden_export.py <pasta>        # um hash por arquivo exportado, para provar que um refactor não mudou a saída
```

A página não tem testes de unidade. O `web_smoke.py` e o `golden_export.py` precisam de uma exportação feita a partir do jogo.

## Layout

```
tools/uiview/
  export.py content.py mesh.py   comum: telas, texturas, geometria DR2M
  car/                           Car Model Explorer (carmodel.py; models.py: catálogo de pacotes e exportação)
  track/                         Track Explorer (export.py: exporta a pista; edit.py: grava o .nefs editado)
  serve.py                       servidor local com POST /api/save
  web/                           index.html, css/, js/ (copiados como estão para a saída)
  tests/
```

Para testar sem o jogo, `python -m tools.synthtrack` gera uma pista inventada que aparece na aba **Pistas** ([synthtrack.md](synthtrack.md)).

O viewer nativo em C++ que vai substituir o navegador para pistas grandes está em `tools/viewer3d/` (veja o [plano](../plans/viewer3d/README.md)).

## Formatos

- [`reverse_engineering/track_formats.md`](../reverse_engineering/track_formats.md): arquivos de pista do jogo.
- [`reverse_engineering/ui_render.md`](../reverse_engineering/ui_render.md): cenas e telas.
- [`plans/viewer3d/formatos.md`](../plans/viewer3d/formatos.md): os arquivos que a exportação grava (DR2M, DR2I, `track.json`, `edits.json`).
