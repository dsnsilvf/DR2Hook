# Telas, Cenas, Imagens, Textos, Diálogos e Modelos

## O que é

As abas do [DR2 UI Viewer](uiview.md) que mostram a interface do jogo e os modelos genéricos. Elas leem a UI de `game_1.dat` e desenham as telas do jogo num canvas 1920×1080. A aba **Carros** tem documento próprio ([car_explorer.md](car_explorer.md)), e a aba **Pistas** também ([track_explorer.md](track_explorer.md)).

A descrição detalhada de como uma tela é composta e desenhada (cena, materiais, curvas, regras de visibilidade) está em [`reverse_engineering/ui_render.md`](../reverse_engineering/ui_render.md#visualizador-toolsuiview). Este documento resume o uso.

## Como abrir

```bash
python -m tools.uiview [--game PASTA] [-o build/uiview] [--scene-images-only] [--models 037] [--open]
```

- `--scene-images-only` exporta só as imagens usadas pelas cenas e telas: é mais rápido, mas a aba **Imagens** fica parcial.
- `--force-textures` regrava as texturas já exportadas.
- `--models` e `--all-models` controlam a malha dos modelos (veja [car_explorer.md](car_explorer.md#como-abrir)).

A página abre direto do arquivo (`build/uiview/index.html`). A aba **Modelos** e as abas 3D buscam arquivos `.bin` com `fetch`; se o navegador bloquear `file://`, sirva a pasta por HTTP (`python -m tools.uiview.serve` ou `python -m http.server` dentro de `build/uiview`).

## Abas

| Aba | O que mostra | Filtros | Hash da URL |
| --- | --- | --- | --- |
| **Telas** | As telas de `screens.bin` compostas sobre as cenas PSSG, em 1920×1080. O inspetor mostra objeto, cena, tela-mãe, estados e eventos do fluxo, cada item e o XML. Clicar num item ou usar as setas põe o item em foco | com estado no fluxo, citada por outra tela, nome no executável, sem referência | `#s=<id>` |
| **Cenas** | Qualquer raiz das três bases de cena, com a árvore de nós e quem a inclui | telas, componentes, HUD, carregamento | `#c=<nome>` |
| **Imagens** | Grade de miniaturas e, no painel, a imagem em tamanho real e as cenas e telas que a citam | por grupo | `#i=<id>` |
| **Textos** | Chaves `.lng` em português e inglês, com quantas telas usam cada uma (até 300 linhas) | todos, usados nas telas (padrão), sem português | `#t=<chave>` |
| **Diálogos** | Título, corpo e opções de `frontend/message_dialogs/*.xml` e os estados que citam o id | por arquivo de origem | `#d=<id>` |
| **Modelos** | O índice 3D (carro, interior, LOD baixo, personagem, genérico, local, prop). Quem tem malha exportada abre um preview WebGL com o LOD0 fundido por material; piso asfalto, terra ou neve; «Virar V» | todos, com malha, por tipo | `#m=<id>` |

Controles da aba **Modelos**: arrastar com o esquerdo orbita; botão direito, do meio ou Shift + esquerdo arrasta o alvo; a roda aproxima.

### Opções das telas

Barra sob o canvas: animação (`open`, `close`, `activate`… na tela inteira ou só no item em foco, com velocidade e quadro arrastável) e variantes de switch. Caixas de opção:

| Opção | Efeito |
| --- | --- |
| Caixas dos itens | contorna cada item com o nome dele |
| Mostrar ocultos | desenha em magenta tracejado o que existe na cena mas está escondido agora, e lista o motivo no painel |
| Campos de dados | mostra em laranja os campos que o jogo preencheria (`‹data_path›`) |
| Nomes dos nós | escreve o nome dos nós de texto que não recebem texto |
| Esconder condicionais | esconde itens que só aparecem em certas condições do jogo |
| Lista inteira | mostra a lista toda, sem o recorte da rolagem |
| Sem animação | usa os valores gravados nos materiais, sem avaliar as curvas |
| Fundo de jogo | fundo escuro parecido com o do jogo |

### Idioma

O seletor do topo troca juntos os rótulos do viewer e os textos do jogo (português `bra` ou inglês `eng`). Uma chave ausente na língua escolhida sai com † e o texto da outra.

## O que lê e o que escreve

Lê só a pasta do jogo:

| Fonte | Para quê |
| --- | --- |
| `game_1.dat`: `system/screens.bin`, `states.bin`, `flow.bin` | telas, estados e fluxo |
| `game_1.dat`: `frontend/databases/persistentDB.pssg`, `d_osd.pssg`, `loadingScreen.pssg` | cenas do frontend, do HUD e do carregamento |
| `frontend/bundles/*.pssg`, `frontend/streamed_textures/**/*.tpk` (procurados em `game_1.dat`, `game.nefs` e `game.dat`) | imagens (os `.tpk` sem bloco de dados ficam de fora) |
| `game_1.dat`: `language/language_eng.lng`, `language_bra.lng` | textos |
| `frontend/message_dialogs/flow.xml`, `network.xml`, `save.xml` (no primeiro dos mesmos três pacotes que os tiver) | diálogos |
| `dirtrally2.exe` | os ids de tela que aparecem como string no executável (filtro "nome no exe") |

Grava em `build/uiview/` (ou `-o`):

| Arquivo | Conteúdo |
| --- | --- |
| `data/ui.js` | telas, estados, fluxo, estilos, diálogos e ids citados pelo executável |
| `data/scenes.js` | cenas com malhas, materiais, curvas e a tabela de texturas |
| `data/strings.js` | textos em inglês e português |
| `data/assets.js`, `img/`, `thumb/` | imagens em WebP (até 2048 px) e miniaturas (256 px) |
| `data/models.js`, `models/*.bin` | índice 3D e malhas DR2M (veja [car_explorer.md](car_explorer.md)) |
| `data/content_cache.json` | cache das imagens: se os pacotes não mudaram e os arquivos existem, os bundles não são reabertos |
| `index.html`, `js/`, `css/` | cópia de `tools/uiview/web/` |

## Limites e o que não foi testado

- O texto usa Barlow Condensed e Roboto Condensed no lugar das fontes MSDF do jogo; as fontes vêm do Google Fonts, então a página precisa de rede para usá-las.
- Dados que o C++ preencheria (`tabs.info`, listas, imagens de `BTextureData`) aparecem como caminho ou vazios.
- A distorção de tempo das curvas é ignorada: entre duas chaves o quadro anda em linha reta.
- O preview da aba **Modelos** só lê o difuso, usa um osso por vértice e não toca animação esquelética.
- Não há teste automático das abas além do `scripts/dev/web_smoke.py`, que abre **Telas** e confere que não há erro de JavaScript.

Mais limites e medidas em [`ui_render.md`](../reverse_engineering/ui_render.md).

## Onde está o código

| Arquivo | Papel |
| --- | --- |
| `tools/uiview/export.py` | Orquestra a exportação (`run`): telas (`export_screens`), estilos (`export_styles`), cenas (`SceneExporter`), textos; `python -m tools.uiview` |
| `tools/uiview/content.py` | Imagens (`ContentExporter`, `export_pssg_images`, `decode_texture`) e diálogos (`export_dialogs`) |
| `tools/uiview/car/models.py` | Índice e malhas da aba **Modelos** |
| `tools/uiview/mesh.py` | Malha DR2M dos modelos |
| `tools/uiview/web/js/viewer.js` | Abas **Telas** e **Cenas**, composição em canvas 2D e o esqueleto do app (abas, busca, filtros) |
| `tools/uiview/web/js/content.js` | Abas **Imagens**, **Textos**, **Diálogos** e **Modelos** |
| `tools/uiview/web/js/i18n.js` | Rótulos em português e inglês |

## Formatos

- [`reverse_engineering/ui_render.md`](../reverse_engineering/ui_render.md): cena, materiais, curvas e o visualizador.
- [`reverse_engineering/ui_data.md`](../reverse_engineering/ui_data.md): `screens.bin`, `states.bin`, `flow.bin`, `links.bin`.
- [`reverse_engineering/ui_tabs.md`](../reverse_engineering/ui_tabs.md): estados com abas.
