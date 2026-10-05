# Documentação do DR2Hook

| Pasta | Conteúdo |
| --- | --- |
| [`guides/`](guides/) | Para quem usa e escreve mods: [instalação](guides/install.md), [guia de mods Lua](guides/modding_guide.md) e [canal de comandos remoto](guides/remote_commands.md) |
| [`architecture/`](architecture/) | Como o loader funciona por dentro: [exports do `dxgi.dll`](architecture/dxgi_exports.md), [harness do tick de física](architecture/physics_tick_harness.md) e a [decisão sobre savestate](architecture/adr_savestate_safety.md) |
| [`tools/`](tools/) | Ferramentas fora do jogo: [`dr2rec`](tools/dr2rec.md) (caixa-preta de sessão) e [DR2 UI Viewer](tools/uiview.md) (Car e Track Explorer) |
| [`reverse_engineering/`](reverse_engineering/README.md) | Engenharia reversa por subsistema, com grau de evidência em cada afirmação, e as investigações em `investigations/` |
| [`demands/`](demands/README.md) | Ideias ainda não iniciadas (multiplayer com fantasmas, editores). Tudo ali é especulação |

Fora desta pasta: o [`README.md`](../README.md) da raiz (visão geral e roadmap) e o [`SSOT.md`](../SSOT.md) (especificação e estado das fases).

## Convenções

- Nomes de arquivo em `snake_case`, minúsculos.
- Documentos de engenharia reversa marcam cada afirmação com `CONFIRMADO`, `PROVÁVEL`, `INCONCLUSIVO` ou `REJEITADO`. Nome de string no executável não é variável localizada.
- `investigations/` guarda registros datados de investigações. Eles não são reescritos quando o código muda de lugar, por isso podem citar caminhos antigos.
