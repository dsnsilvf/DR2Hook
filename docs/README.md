# Documentação do DR2Hook

| Pasta | Conteúdo |
| --- | --- |
| [`guides/`](guides/) | Para quem usa e escreve mods: [instalação](guides/install.md), [guia de mods Lua](guides/modding_guide.md), [canal de comandos remoto](guides/remote_commands.md) e [pistas próprias](guides/custom_tracks.md) |
| [`architecture/`](architecture/) | Como o loader funciona por dentro: [exports do `dxgi.dll`](architecture/dxgi_exports.md), [harness do tick de física](architecture/physics_tick_harness.md) e a [decisão sobre savestate](architecture/adr_savestate_safety.md) |
| [`tools/`](tools/) | Ferramentas fora do jogo: [`dr2rec`](tools/dr2rec.md) (caixa-preta de sessão) e [DR2 UI Viewer](tools/uiview.md), com [Car Explorer](tools/car_explorer.md), [Track Explorer](tools/track_explorer.md) e [telas do jogo](tools/ui_screens.md); e a [pista sintética](tools/synthtrack.md) (`tools/synthtrack`) |
| [`reverse_engineering/`](reverse_engineering/README.md) | Engenharia reversa por subsistema, com grau de evidência em cada afirmação, e as investigações em `investigations/` |
| [`plans/`](plans/README.md) | Planos de execução (Python em camadas, web modular, docs por ferramenta) e o [viewer 3D](plans/viewer3d/README.md) — MVP fechado; melhorias GPU e Polônia ficam em `plans/viewer3d/` |
| [`demands/`](demands/README.md) | Ideias de produto (TAS, comparador, multiplayer…) e status do editor / pista sintética |

Fora desta pasta: o [`README.md`](../README.md) da raiz (visão geral e roadmap), o [`SSOT.md`](../SSOT.md) (especificação e estado das fases) e [`examples/`](../examples/README.md) (a pista sintética pronta para abrir nos viewers).

## Convenções

- Nomes de arquivo em `snake_case`, minúsculos.
- Documentos de engenharia reversa marcam cada afirmação com `CONFIRMADO`, `PROVÁVEL`, `INCONCLUSIVO` ou `REJEITADO`. Nome de string no executável não é variável localizada.
- `investigations/` guarda registros datados de investigações. Eles não são reescritos quando o código muda de lugar, por isso podem citar caminhos antigos.
