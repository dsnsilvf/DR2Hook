# Plano 02 — Web em módulos

Leia antes: [README dos planos](README.md) (regras e provas).

## Por que

Em `tools/uiview/web/js/` há 5 arquivos para quatro ferramentas, e três deles misturam assuntos:

| Arquivo | Linhas | O que mistura |
| --- | --- | --- |
| `viewer.js` | 1502 | Telas do jogo em canvas 2D **e** o esqueleto do app (abas, busca, filtros, `start()`) |
| `carview.js` | 1698 | Dados, GPU, árvore de nós, materiais, gizmo, histórico, câmeras do jogo e painel, tudo do Car Explorer |
| `trackview.js` | 875 | Dados, GPU, picking, histórico, edição, exportação/salvar e painel, tudo do Track Explorer |
| `content.js` | 842 | Imagens, textos, diálogos **e** o visualizador genérico de modelos (`modelGL`, `loadModel`) **e** as funções de matriz usadas por todas as abas |

Há duplicação: `mat4Mul`, `mat4Perspective`, `mat4Look` ficam em `content.js` mas são usadas por `trackview.js` e `carview.js`; `tvInvAffine` e `carInvAffine` fazem o mesmo; os três `*GL()` compilam shader com o mesmo código (`createShader`/`compileShader` em `content.js:532`, `trackview.js:332`, `carview.js:576`); os cortes de roda (`openWheelCenter`, `rubberizeInnerWheel` em `content.js`) reaparecem em `carview.js` (`carOpenCenter`, `carRubberInner`, com um comentário dizendo "mesmos cortes").

O objetivo é separar por responsabilidade **sem mudar nenhum comportamento**. Isso também prepara o terreno para o viewer nativo, cujas camadas (`render`, `edit`, `app`) saem do mesmo desenho.

## Regras deste plano

- **Sem bundler, sem npm, sem ES modules.** O viewer abre por `file://` e por `python -m tools.uiview.serve`; scripts clássicos em ordem no `index.html` continuam sendo o mecanismo. Não introduza ferramentas de build.
- **Primeira passada: não renomeie nada.** Os nomes globais (`tv*`, `car*`, `TV_*`, `CAR_*`, `MODES`, `state`, `tv`, `cv`) ficam; só os arquivos mudam. Quem chama continua chamando do mesmo jeito. (O smoke test e as notas do projeto citam esses nomes.)
- Scripts clássicos compartilham o escopo global: **a ordem das tags `<script>` é a ordem de dependência**. Funções declaradas (`function x()`) podem ser usadas antes, `const`/`let` não: um `const` de topo precisa estar num arquivo carregado antes de quem o lê **em tempo de carga** (dentro de função não importa).
- Não mexa em `i18n.js` além de, se quiser, mover chaves; não mude textos.
- Car Explorer: **comportamento idêntico**. Se a diferença for visível, é bug seu.

## Prova de que nada quebrou

Não há testes de JS. Use as duas provas abaixo **a cada passo**:

1. **Fumaça automática** — `scripts/dev/web_smoke.py` carrega o `web/` atual num Edge/Chromium headless, abre as abas Telas, Pistas e Carros, abre a primeira pista e o primeiro carro e falha se houver erro de JavaScript.

   ```bash
   python3 -m tools.uiview --scene-images-only --models 037 -o build/uiview    # uma vez (~15 s)
   python3 -m tools.uiview.track --tracks montalegre -o build/uiview           # uma vez (~15 s)
   python3 scripts/dev/web_smoke.py                                            # a cada passo (~5 s); exit 0 e "errors": []
   ```

   Esperado hoje: `screens.items` = 288, pista com `types` 324 e `instances` 2897, carro `loaded: true`. Se você renomear `tv`, `cv`, `TRACKS`, `CAR_LIST`, `openCar` ou os ids `trk-open`/`car-open`, atualize o `DRIVER` do script **no mesmo commit**.
2. **Conferência manual curta** (5 min, ao final de cada passo que mexe em `trackview`/`carview`). Abra `python -m tools.uiview.serve` e `http://127.0.0.1:8790/`, e confira, na aba **Pistas** (Montalegre): ferramentas 1/2/3 (orbitar, mover, girar), zoom, WASD, `F` para enquadrar, selecionar objeto, apagar (Delete), duplicar (Ctrl+D), desfazer/refazer (Ctrl+Z, Ctrl+Shift+Z e Ctrl+Y), alternar camadas, trocar de rota, **Exportar edições** (baixa o JSON). Na aba **Carros**: abrir um carro, árvore de nós, ocultar/isolar nó, mover/girar com o gizmo, desfazer, câmeras do jogo, trocar LOD e superfície. Anote no commit o que foi conferido. **Não** use o botão "Salvar .nefs" nesses testes (ele grava um `.nefs` novo em `build/uiview/saves/`; é inofensivo, mas desnecessário aqui).

O `golden_export.py` do README **não** serve neste plano: ele prova a saída do Python, não o comportamento do navegador. Rode-o uma vez no fim, só para garantir que a cópia do `web/` para a saída (`_copy_web`) continua funcionando.

## Estrutura de destino

```
tools/uiview/web/
  index.html
  css/viewer.css
  js/
    shared/
      mat4.js        mat4Mul, mat4Perspective, mat4Look (hoje em content.js)
      gl.js          compileProgram(gl, vs, fs), upload de textura (a parte comum dos três *GL())
      i18n.js        (hoje js/i18n.js, mesmo conteúdo)
    app/
      viewer.js      telas do jogo em canvas 2D (o que sobra de viewer.js)
      shell.js       MODES, state, abas, busca, filtros, start() (extraído de viewer.js e carview.js:1698)
    content/
      images.js texts.js dialogs.js     (content.js até a linha dos diálogos)
    models/
      models.js      MODEL_*, modelKind, parseGeom, modelGL, loadModel, MODES.models
      wheels.js      wheelGroups, assignWheels, openWheelCenter, rubberizeInnerWheel, mapDiscUv
    car/
      state.js data.js gpu.js wheels.js tree.js materials.js gizmo.js history.js cameras.js ui.js
    track/
      state.js data.js gpu.js render.js input.js edit.js ui.js
```

## Mapa de destino (por nome de função, não por linha)

### `trackview.js` → `track/`

| Arquivo | Conteúdo |
| --- | --- |
| `state.js` | `TRACKS`, `tv`, `tvLog`, `TV_FOV`, `TV_ADDED`, `TV_HIST_MAX`, `TV_WASD`, `TV_ACTS` (constantes) |
| `data.js` | `tvParse` (DR2M), `tvParseInst` (DR2I), `tvLinesFrom`, `tvOpen`, `tvLoadTerrain`, `tvLoadRoute`, `tvGroups`, `tvName`, `tvKind`, `tvLayerOn`, `tvRouteEntry` |
| `gpu.js` | `tvHash`, `tvColor`, `tvUpload`, `tvTexture`, `tvFree`, `tvBox`, `tvXf` |
| `render.js` | `tvCull`, `tvCam`, `tvVp`, `tvFrameRoute`, `tvFrameInst`, `tvGL` (o laço de desenho e o programa de shader) |
| `input.js` | `tvKeys`, `tvRay`, `tvGround`, `tvInvAffine`, `tvPick`, `tvPickIndex` |
| `edit.js` | histórico (`tvSnap`, `tvApplySnap`, `tvCommit`, `tvHistGo`, `tvUndo`, `tvRedo`), `tvEditList`, `tvEdited`, `tvDoc`, `tvSave`, `tvExport`, `tvSpin`, `tvDuplicateSel`, `tvDeleteSel`, `tvSetPos`, `tvTurnSel`, `tvRestoreSel`, `tvSelect` |
| `ui.js` | `tvInstInfo`, `tvStatus`, `tvInspect`, `tvDirtyHtml`, `tvTool`, `tvRouteSelect`, `ensureTrackStage`, `MODES.tracks = {...}` |

### `carview.js` → `car/`

| Arquivo | Conteúdo |
| --- | --- |
| `state.js` | `CAR_LIST`, `cv`, `carLog`, `CAR_ACCENT`, `CAR_*` (constantes), `carKindLabel` |
| `data.js` | `carIndex`, `parseCarRes`, `openCar`, `carCorners`, `carAabb`, `carSelNode`, `carFrame`, `carResetCamera`, `carHidePoses` |
| `gpu.js` | `carFreeGpu`, `carUpload`, `carLines`, `carCollect`, `carGL` |
| `wheels.js` | `carFixKind`, `carWheelMesh`, `carOpenCenter`, `carRubberInner`, `carFixWheels` |
| `tree.js` | `CAR_GROUPS`, `CAR_GROUP_MIN`, `carGroupOf`, `carGroups`, `carNodeMatches`, `carRows`, `carListClick`, `carSelect`, `carToggleHidden`, `carToggleSliceHidden`, `carTreeTools` |
| `materials.js` | `carMaterial`, `carSliceVisible`, `carIsGrill`, `carMatTexId`, `carIsBlend`, `carTexture`, `carMaterialHtml`, `carTexturesHtml`, `carTexHtml`, `carTexUpdate`, `carTexClick` |
| `gizmo.js` | `carInvAffine`, `carRaySlice`, `carInvM`, `carTranslateM`, `carRotateM`, `carViewProj`, `carProject`, `carGizmoFrame`, `carRingPoints`, `carSegDist`, `carGz*`, `carPick`, `carReveal` |
| `history.js` | `carApplyDelta`, `carRestoreEdits`, `carSnap*`, `carSetNodes`, `carSetView`, `carViewEdit`, `carCommit`, `carHistChanged`, `carHistGo`, `carUndo`, `carRedo`, `carHistRender`, `carFixModel` |
| `cameras.js` | `carFrontZ`, `carCamPose`, `carViewMat`, `carCamLines`, `carDrawCams`, `carSetCamView`, `carCamera` |
| `ui.js` | `makeResizer`, `carSelLabel`, `carStatus`, `carStatusEdit`, `carSyncBar`, `carDock`, `ensureCarStage`, `carKv`, `carMatrix`, `carExtra`, `carNodeOff`, `carLodHtml`, `carSourceRows`, `carBufferText`, `carInspect`, `MODES.cars = {...}` |

Se uma função não está na lista, coloque-a no arquivo do seu assunto e mencione no commit. Funções que o `carview.js` define **e** o `content.js` também define (as de roda) **não** são unidas na primeira passada (veja passo 8).

### `content.js` → `content/` e `models/`

- Imagens, textos e diálogos (`MODES.images`, `MODES.texts`, `MODES.dialogs` e seus auxiliares, até `dialogText`) → `content/images.js`, `texts.js`, `dialogs.js`.
- `MODEL_PACK`, `MODEL_LIST`, `modelKind`, `meshVariant`, `meshVisible`, `parseGeom`, `modelGL`, `modelCamera`, `uploadModel`, `ensureModelTexture`, `loadModel`, `ensureModelStage`, `MODES.models` → `models/models.js`.
- `fract1`, `wheelGroups`, `assignWheels`, `openWheelCenter`, `rubberizeInnerWheel`, `mapDiscUv`, `colorOf`, `surfaceToken`, `isTread`, `wheelDiffuse`, `guessTexture` → `models/wheels.js`.
- `mat4Mul`, `mat4Perspective`, `mat4Look` → `shared/mat4.js`.

### `viewer.js`

`MODES`, `state`, as funções de aba/busca/filtro (por volta de `viewer.js:1290–1500`: `MODES.screens`, `MODES.components`, troca de modo, `start()`) → `app/shell.js`. O restante (canvas 2D das telas) fica em `app/viewer.js`. A chamada final `start();` que hoje está no fim de `carview.js:1698` passa para o **último** script do `index.html` (`app/start.js`, uma linha) para não depender de quem é o último módulo.

## Passos

Um commit por passo. Em todos: `web_smoke.py` com `"errors": []` e a conferência manual quando indicada.

### Passo 0 — Linha de base
- Rode as duas exportações e o smoke. Guarde a saída (`screens.items`, `types`, `instances`).
- Faça a conferência manual completa uma vez para saber como **deve** ser.

### Passo 1 — Pastas, sem partir arquivos
- Já feito: `web/js/` e `web/css/`. Crie `shared/`, `app/`, `content/`, `models/`, `car/`, `track/`, mova `i18n.js` → `shared/`, `viewer.js` → `app/`, `content.js` → `content/content.js` (ainda inteiro), `trackview.js` → `track/trackview.js`, `carview.js` → `car/carview.js`. Atualize as tags no `index.html`.
- `_copy_web` (`tools/uiview/export.py`) usa `copytree`, então subpastas novas entram sozinhas.

### Passo 2 — Matriz e GL comuns
- Crie `shared/mat4.js` e mova `mat4Mul`/`mat4Perspective`/`mat4Look` (copie, rode o smoke, **depois** apague de `content.js`). Carregue antes de todos.
- Crie `shared/gl.js` com `compileProgram(gl, vsSource, fsSource)` (retorna o programa e lança com o log de erro) e use-a nos três `*GL()`. Compare os corpos de `content.js:532`, `trackview.js:332` e `carview.js:576` antes: se algum tratar erro de outro jeito, preserve esse jeito passando um parâmetro.

### Passo 3 — `trackview.js` em módulos
- Divida conforme a tabela, na ordem `state → data → gpu → render → input → edit → ui`. Cada arquivo novo é uma tag `<script>` nessa ordem; `ui.js` por último porque registra `MODES.tracks` em tempo de carga.
- Rode o smoke e a conferência manual de **Pistas** (inclua duplicar, desfazer e exportar edições; o JSON exportado deve ser idêntico ao de antes para a mesma sequência de operações — faça uma sequência curta, exporte antes e depois, `diff`).

### Passo 4 — `carview.js` em módulos
- Mesma técnica: `state → data → gpu → wheels → materials → tree → gizmo → history → cameras → ui`. `ui.js` registra `MODES.cars`.
- Remova o `start();` do fim e crie `app/start.js` (o último script).
- Conferência manual de **Carros** completa. É a parte de maior risco do plano.

### Passo 5 — `content.js` e `viewer.js`
- Separe `content/` e `models/` como no mapa. Extraia `app/shell.js`. Smoke e conferência das abas Imagens, Textos, Diálogos, Modelos e Telas.

### Passo 6 — Documentação
- Atualize `docs/tools/uiview.md` (bloco "Layout") e o comentário de cabeçalho de cada arquivo novo (uma linha: o que faz e quem o carrega).
- Não crie documentação de uso (isso é o [plano 03](03_docs_ferramentas.md)).

### Passo 7 (opcional) — Unir o gizmo de pista e de carro
- Só se sobrar tempo e **depois** de tudo acima estar commitado: `tvInvAffine`/`carInvAffine` fazem a mesma coisa. Una em `shared/mat4.js` com um nome neutro e troque as chamadas; é a única mudança de nome permitida neste plano.

### Passo 8 (opcional) — Rodas
- `carOpenCenter`/`carRubberInner` e `openWheelCenter`/`rubberizeInnerWheel` são "os mesmos cortes" mas operam em estruturas diferentes (o carro mantém o dono de cada triângulo). **Não una** sem uma prova: escreva uma página de teste que rode as duas versões sobre os mesmos vértices e compare. Se não for trivial, deixe como está e registre em `docs/tools/car_explorer.md`.

## Pronto quando

1. A estrutura de destino existe; nenhum arquivo `.js` passa de ~600 linhas (exceto `app/viewer.js`, que é o canvas 2D das telas e pode ficar como está).
2. `web_smoke.py` sai com 0 e os mesmos números da linha de base.
3. Conferência manual feita nas abas Pistas, Carros e Modelos, anotada nos commits.
4. `python3 -m tools.uiview.serve` abre o viewer e o botão **Salvar .nefs** continua habilitado só quando há edição (verifique o estado do botão, sem clicar nele).
5. `bash scripts/dev/test_tools.sh` verde (nada de Python mudou, mas o `_copy_web` é Python).

## Riscos

- **Ordem dos scripts.** O erro típico é `X is not defined` ou `Cannot access 'X' before initialization` por causa de um `const` de topo carregado depois de quem o usa em tempo de carga. O smoke pega; o log do navegador diz o arquivo e a linha.
- **Estado compartilhado por nome.** `tv`, `cv` e `state` são objetos globais lidos por vários arquivos; não os recrie, só os declare uma vez no `state.js`.
- **Funções ligadas pelo HTML gerado.** Hoje não há `onclick=` nos arquivos (`grep -c "onclick="` dá 0); os eventos são ligados com `addEventListener` e `data-*` dentro de `ensure*Stage()`. Mantenha assim, e antes de mover uma função que parece "privada" procure o nome dela em `data-` e em `addEventListener`.
- **Cache do navegador.** `_bust_cache` (`tools/uiview/export.py`) acrescenta `?v=<hash>` a todo `src`/`href` terminado em `.js` ou `.css` do `index.html` (a expressão aceita subpastas), mas só roda na exportação completa (`python -m tools.uiview`), não em `tools.uiview.track`. Depois de mover arquivos, rode a exportação completa uma vez e confirme no navegador (aba Rede) que os scripts de `js/*/` carregam com `?v=`; no desenvolvimento use recarga forçada (Ctrl+Shift+R).

## Fora do escopo

TypeScript, ES modules, bundler, mudar visual, mudar textos, mexer no Python (plano 01), documentar o uso (plano 03).
