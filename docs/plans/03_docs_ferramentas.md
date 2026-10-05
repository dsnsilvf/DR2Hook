# Plano 03 — Documentação por ferramenta

Leia antes: [README dos planos](README.md) (regras).

## Por que

O DR2 UI Viewer tem quatro ferramentas (Telas, Modelos/Carros, Pistas) mas um único documento, [`docs/tools/uiview.md`](../tools/uiview.md) (em inglês, 77 linhas), e quase tudo nele é do Track Explorer. O Car Model Explorer não tem documento: seus recursos só aparecem no código (`tools/uiview/car/`, `web/js/carview.js`) e em notas pessoais. Este plano cria um documento por ferramenta e transforma o `uiview.md` numa visão geral.

Este plano **só escreve documentação**. Não altere código, exceto para corrigir um erro que a escrita revelar (e nesse caso faça um commit separado).

## Entregáveis

| Arquivo | Conteúdo |
| --- | --- |
| `docs/tools/uiview.md` | Visão geral: o que é, requisitos, tabela de abas com links, comandos, layout das pastas, como rodar os testes. Passa para **português** (hoje está em inglês; o resto de `docs/` é em português). Os blocos de Track Explorer saem daqui para o novo `track_explorer.md`. |
| `docs/tools/car_explorer.md` | **Novo.** Car Model Explorer. |
| `docs/tools/track_explorer.md` | **Novo.** Track Explorer e editor de pistas (recebe o que hoje é o miolo do `uiview.md`). |
| `docs/tools/ui_screens.md` | **Novo.** Abas Telas, Cenas, Imagens, Textos, Diálogos e a aba Modelos (genérica). |
| `docs/README.md` | Índice atualizado com os três novos. |
| `README.md` (raiz) | Os links `docs/tools/uiview.md` ficam; acrescente um link para cada ferramenta na seção "DR2 UI Viewer" (linha 45 da tabela e o parágrafo da seção 4). |

Convenções (as mesmas de `docs/README.md`): nomes em `snake_case`, português, afirmações técnicas com grau de evidência quando houver dúvida (`CONFIRMADO`, `PROVÁVEL`, `INCONCLUSIVO`), e **nunca afirmar o que não foi lido no código ou testado**. Quando algo só está na memória de quem escreveu, verifique no código antes de pôr no texto.

## Como escrever cada documento

Estrutura comum, nesta ordem: **O que é** (3 linhas) → **Como abrir** (comandos copiáveis) → **Controles** (tabela) → **O que lê e o que escreve** → **Limites e o que não foi testado** → **Onde está o código** (tabela de arquivo → papel) → **Formatos** (links para `reverse_engineering/`).

Cada doc deve ter, no fim, a seção **"Limites"** honesta. Se um recurso nunca foi testado com mouse real, no jogo ou em GPU de verdade, escreva isso.

### `car_explorer.md`

Fontes a ler (nesta ordem): `tools/uiview/car/carmodel.py` (cabeçalho e `build_car_model`), `tools/uiview/car/models.py` (`export_models`, `_car_cameras`, `classify_path`), `tools/uiview/web/js/carview.js` (atalhos e painéis em `ensureCarStage`), `docs/reverse_engineering/ui_render.md` (seção do visualizador) e o `docs/demands/vehicle-editor.md`.

Pontos que o documento **deve** cobrir (todos existem hoje; confirme cada um no código antes de escrever):

1. **Exportação:** `python -m tools.uiview.car --models 037 -o build/uiview` (só o carro) e `python -m tools.uiview --models 037` (com a interface). `--all-models` e o limite de tamanho (40 MB padrão, 120 MB com `--all-models`; acima disso fica só no índice).
2. **Saída por carro:** `models/<id>.car.json` (árvore) + `.car.bin` (buffers compartilhados, formato DR2C; ver `pack_resources`), texturas em `img/`, `cameras` no JSON.
3. **Árvore real:** LOD → `MATRIXPALETTEJOINTNODE` → fatias `MATRIXPALETTEJOINTRENDERINSTANCE` (`streamOffset`, `indexOffset`, `jointID`, `indices`); agrupamento por categoria (`CAR_GROUPS`, mínimo `CAR_GROUP_MIN`); busca; poses `_pos_NN` ocultas (limpadores).
4. **Seleção e inspeção:** por clique ou pela árvore; seções do inspetor (`car.sec.*`); materiais, texturas (preview), buffers, origem no PSSG.
5. **Edição em memória:** ferramentas Navegar/Mover/Girar (**Q/W/E**) com gizmo (eixos só globais), ocultar/isolar nó e fatia, **Ctrl+Z/Ctrl+Y** com histórico de 200 passos e painel clicável. **As edições não são salvas** (nenhum botão grava; confirme em `carview.js`).
6. **Câmeras do jogo:** lidas de `cars/models/<id>/cameras.xml` (via `egodata/bxml.py`), mostradas como marcadores e "ver pela câmera". Liste as aproximações **não** conferidas (FOV vertical 1,0 rad assumido; sinal do pitch; `chase_*` com elevação inventada; `bumper` relativo à caixa do modelo). Só `head-cam` e `chase_close` do 037 foram conferidos visualmente.
7. **Remendos de roda e disco:** o que `carFixWheels` faz (corta o centro aberto da roda, "borrachiza" o barril interno, mapeia UV do disco) e por que existe; que o barril interno ainda tem UV errado.
8. **Visual:** grade sem textura (cinza; `car_grill.fx` não tem textura nos arquivos), vidro com alfa fixo em 40%, sem normal map, textura escolhida por nome (`guessTexture`).
9. **Interface:** workspace `#car-ws`, árvore e inspetor acopláveis (`carDock`), painéis redimensionáveis (`makeResizer`), gavetas em telas estreitas (≤860 px e ≤640 px), hash de URL `#k=<id>`.
10. **Limites:** edições só na página; nada escrito no `.nefs` (a escrita existe em `egodata/nefs_write.py`, mas o Car Explorer não a usa); gizmo testado só com eventos simulados; sem edição de escala; seleção e troca de ferramenta não entram no histórico; sem importação de geometria.

Tabela "Onde está o código" com os arquivos reais (atualize conforme os planos 01 e 02 tenham sido aplicados: se `tools/uiview/car/` e `web/js/car/` forem divididos, aponte para os novos arquivos).

### `track_explorer.md`

Fonte principal: o conteúdo atual de `docs/tools/uiview.md` (seções "Track Explorer controls", "How an edit reaches a .nefs", "Limits") — **mova e traduza**, não copie. Leia também `tools/uiview/track/export.py`, `track/edit.py`, `serve.py`, `web/js/trackview.js` e `docs/reverse_engineering/track_formats.md`.

Acrescente o que falta hoje:

1. **Comandos:** exportar (`python -m tools.uiview.track --tracks montalegre,poland_rally_01 -o build/uiview`; vazio só reindexa), servir (`python -m tools.uiview.serve`, porta 8790, `127.0.0.1`, recusa raiz dentro da pasta do jogo), aplicar edições à mão (`python -m tools.uiview.track.edit <pista>.edits.json -o build/uiview/saves/<pista>.nefs`).
2. **Saída por pista:** `tracks/<id>/{track.json, terrain_<n>.bin, objects.bin, inst_<rota>.bin, tex/*.webp}` e `data/tracks.js`. Descreva cada arquivo em uma linha e aponte para [`plans/viewer3d/formatos.md`](viewer3d/formatos.md), que tem a especificação byte a byte (DR2M, DR2I, `track.json`, `edits.json`).
3. **Camadas e rotas:** terreno, objetos, árvores, terreno distante, limites da pista (portões), linha da IA; distância de desenho (700 m padrão); uma rota por vez; histórico por rota.
4. **Formato do `.edits.json`** (`format: "dr2-track-edits"`, `version: 1`; campos de cada edição: `route`, `kind`, `type`, `index`, `deleted`, `m`, `m0`, e para cópias `added`, `src`, `index: -1`). Confirme no `tvDoc`/`tvEditList` (`trackview.js`).
5. **Fluxo de salvar:** botão **Salvar .nefs** → `POST /api/save` → `build/uiview/saves/<pista>.nefs`; sem servidor, cai no download do JSON. Passos da escrita (`track/edit.py` → `nefs_write.replace_files`; mesmo número de blocos de 64 KiB; ~80 cópias de espaço).
6. **Limites** (do texto atual): não testado no jogo; colisão não muda (`track.jpk`, 480 tiles `.vcqtc` não decodificados); pistas de rali pesadas (Polônia ~9 milhões de vértices e ~305 mil instâncias; precisa de GPU de verdade); `batched_track.fx` sem textura; exportar as 40 pistas custaria dezenas de GB; ornamentos e árvores não duplicam.
7. **Como o editor mapeia para o `.ens`:** o clone recebe `id=<orig>_dupN` e `instanceID`/`instance_tag` novos (máximo + 1 + k).

### `ui_screens.md`

Abas **Telas**, **Cenas**, **Imagens**, **Textos**, **Diálogos** e **Modelos** (visualizador genérico de malhas; **Carros** tem documento próprio). Fontes: `tools/uiview/export.py` (`export_screens`, `export_styles`, `SceneExporter`), `content.py` (`ContentExporter`, `export_dialogs`), `web/js/viewer.js`, `web/js/content.js`, e `docs/reverse_engineering/ui_render.md`, `ui_data.md`, `ui_tabs.md`. Cubra: o que cada aba mostra, de onde vêm os dados (`frontend/bundles/*.pssg`, `persistentDB`, `d_osd`, `loadingScreen`, `.lng`), os arquivos `data/*.js` gerados, idiomas (pt/en), cache de conteúdo (`data/content_cache.json`).

### `uiview.md` (visão geral)

Mantenha o que é comum: o que é, requisitos (Python 3 com Pillow para as texturas; navegador com WebGL), a tabela de abas **com um link para cada documento**, a lista de comandos, o bloco "Layout" (atualize se os planos 01/02 já tiverem sido feitos) e a seção de testes (`bash scripts/dev/test_tools.sh`, `python3 scripts/dev/web_smoke.py`). Traduza para português.

## Passos (um commit por passo)

1. `docs: ferramentas do uiview — track_explorer.md` (mova e complete o conteúdo de Pistas; deixe um link no `uiview.md`).
2. `docs: ferramentas do uiview — car_explorer.md`.
3. `docs: ferramentas do uiview — ui_screens.md`.
4. `docs: uiview.md vira visão geral em português; índices` (atualize `docs/README.md`, README raiz e `SSOT.md` se citar o `uiview.md`).
5. Verificação de links (script no [plano 01](01_python_camadas.md#verificação-de-links-dos-md)).

## Pronto quando

- Os quatro `.md` existem e se linkam entre si; `docs/README.md` os lista.
- Todo comando citado foi **executado** pelo autor (`--help` serve para os que dependem do jogo) e funciona como descrito.
- Todo caminho de arquivo citado existe (`git ls-files | grep` cada um).
- Nenhuma afirmação de "testado no jogo" que não tenha sido testada; os limites estão escritos.
- A verificação de links não aponta nada novo.

## Fora do escopo

Documentar o viewer nativo (o [plano dele](viewer3d/README.md) tem a documentação própria), mudar código, traduzir `docs/reverse_engineering/` ou `docs/demands/` (este último é em inglês de propósito).
