# Car Model Explorer

## O que é

Aba **Carros** do [DR2 UI Viewer](uiview.md). Abre o modelo 3D de um carro com a hierarquia real do PSSG (LOD → nó → fatia), as texturas, os materiais e as câmeras do jogo. Permite inspecionar cada peça e movê-la ou girá-la **só na página**: nada é gravado de volta no jogo.

## Como abrir

```bash
# o viewer inteiro (telas, imagens, textos) mais os modelos pedidos; --scene-images-only é mais rápido
python -m tools.uiview --models 037 [-o build/uiview] [--scene-images-only] [--open]

# só a malha, a árvore e as texturas do carro, sem as telas do jogo
python -m tools.uiview.car --models 037 -o build/uiview [--force]

# a malha de todos os modelos até 120 MB, um pacote por vez
python -m tools.uiview --all-models
```

`--models` aceita trechos do nome do pacote ou do arquivo, separados por vírgula. Sem `--models`, o `tools.uiview` exporta a malha dos modelos principais de até **40 MB**. Com `--all-models`, entra tudo até **120 MB** (carro, personagem, local, prop, interior e LOD). Acima de 120 MB o arquivo fica só no índice, sem malha. O `--force` (no `tools.uiview.car`) regrava o que já foi exportado.

O `tools.uiview.car` só grava `models/`, as texturas e `data/model_cache.json`. Ele **não** atualiza o índice da página (`data/models.js`, `data/assets.js`) nem copia o `index.html`, então, sozinho, não faz o carro aparecer no viewer. Use-o para reexportar um carro que já está no índice; para pôr um carro novo na página, rode o `tools.uiview` (conferido no código, em `car/__main__.py` e `export.py`; não executado aqui, porque o jogo não está neste ambiente).

Depois, abra `build/uiview/index.html` e escolha **Carros**. O carro aberto vai para a URL (`#k=<id>`), então recarregar a página volta ao mesmo carro.

## Controles

| Ação | Controle |
| --- | --- |
| Orbitar / pan / zoom | Arrastar com o esquerdo / botão direito, do meio, ou Shift + esquerdo / roda |
| Ferramentas | **Q** Navegar, **W** Mover (eixos), **E** Girar (anéis) |
| Selecionar | Clique na peça no viewport ou na árvore |
| Enquadrar | **F** enquadra a seleção; **Resetar câmera** volta ao início |
| Ocultar | **H** oculta ou mostra o nó ou a fatia selecionada; o olho da árvore faz o mesmo |
| Isolar | **Só o selecionado**; **Mostrar tudo** desfaz ocultar e isolar |
| Histórico | **Ctrl+Z** desfaz; **Ctrl+Y** ou **Ctrl+Shift+Z** refaz; painel de histórico clicável (até 200 passos) |
| Restaurar | **Restaurar todas as posições originais** desfaz todos os movimentos (entra no histórico) |
| Visualização | Wireframe, materiais (liga ou desliga as texturas), vidro translúcido, inverter V das texturas, câmeras do jogo |
| Câmeras do jogo | Mostrar os marcadores; **Ver pela câmera do jogo** troca para a vista escolhida |

## Árvore e inspeção

A árvore é a do PSSG, sem fusão por material (diferente da aba **Modelos**):

```
ROOTNODE → LOD (MATRIXPALETTEBUNDLENODE "LOD0_" …) → MATRIXPALETTEJOINTNODE ("x0_wheel_fl" …)
         → MATRIXPALETTEJOINTRENDERINSTANCE (fatia)
```

- **Fatia** é um intervalo dentro de um `RENDERDATASOURCE` compartilhado: `streamOffset` e `elementCountFromOffset` (vértices) e `indexOffset` e `indicesCountFromOffset` (índices). Vários nós e fatias apontam para o mesmo buffer, que é exportado uma vez.
- Os vértices são locais ao osso; o nó entra com a matriz `world` do PSSG.
- Com 8 filhos ou mais (`CAR_GROUP_MIN`), os nós de um LOD são agrupados por categoria pelo prefixo do nome (`CAR_GROUPS`): Carroceria, Rodas e freios, Suspensão, Luzes, Vidros, Interior, Motor e escape, Outros.
- A busca filtra nós e materiais; o filtro padrão mostra o `LOD0`.
- Nós `<peça>_pos_NN` são poses extras de animação (por exemplo, o limpador de para-brisa) e começam ocultos.

O inspetor tem seções (`car.sec.*`): Carro, Objeto, Geometria, Material, Texturas (com preview: ajustar, 1:1, zoom e fundo xadrez, escuro ou claro), LOD e Técnico (buffers, origem no PSSG, campos crus em `extra` e as notas do parser).

## O que lê e o que escreve

A exportação lê `cars/<id>.nefs` (PSSG) e grava em `build/uiview/`:

| Arquivo | Conteúdo |
| --- | --- |
| `models/<id>.car.json` | árvore (`tree`), `lods`, `materials`, `skinSets`, `resources` (contagens), `notes`, `source` e `cameras` |
| `models/<id>.car.bin` | buffers compartilhados no formato **DR2C** (veja `pack_resources` em `tools/uiview/car/carmodel.py`): posição local ao osso, UV e índices de cada `RENDERDATASOURCE` |
| `img/<grupo>/*.webp`, `thumb/<grupo>/*.webp` | texturas e miniaturas; as pinturas vêm de `livery_00/textures_high/*.pssg` dentro do pacote |
| `data/model_cache.json` | cache da exportação de modelos (assinatura, modelos, texturas) |
| `data/models.js`, `data/assets.js` | índice dos modelos e das texturas, lido pela página; só o `tools.uiview` grava |

Nada é escrito no `.nefs`. As edições ficam na memória da página e somem ao recarregar.

### Câmeras do jogo

`_car_cameras` (`tools/uiview/car/models.py`) lê `cameras.xml` ao lado do PSSG (via `egodata/bxml.py`). Cada vista traz os parâmetros crus e, quando há, a posição e os ângulos. As câmeras de perseguição guardam só o alvo e a distância. O desenho (`carCamPose` em `carview.js`) usa aproximações **não conferidas no jogo**:

- O campo de visão vertical de 1,0 rad é assumido.
- O sinal do pitch (positivo olha para baixo) é suposição; nas câmeras de cabine o pitch está em graus.
- As câmeras `chase_*` ficam atrás do alvo com uma elevação inventada (pitch fixo de 0,18 rad).
- A câmera `bumper` (com `restrictOffsetToConvexHull`) é posta em relação à frente da caixa do modelo.

Segundo o autor do viewer, só `head-cam` e `chase_close` do carro 037 foram conferidas visualmente.

### Remendos de roda e disco

As rodas e os discos usam UV que, desenhados direto, mostram a parte errada do atlas. `carFixWheels` junta as fatias de roda e de disco por LOD e material, no espaço do carro, e:

- **disco** (`mapDiscUv`): remapeia o UV do disco;
- **roda** (`carOpenCenter`): corta os triângulos do centro aberto da roda que caem na área errada do atlas;
- **roda** (`carRubberInner`): manda os triângulos fundos do barril interno (a mais de 0,1 m da face externa) para a faixa de borracha do atlas.

Os mesmos cortes existem para a aba **Modelos** em `content.js` (`openWheelCenter`, `rubberizeInnerWheel`). Segundo o autor do viewer, o barril interno ainda sai com UV errado.

### Visual

- A grade do radiador (`car_grill.fx`) usa uma textura em tile que não está nos arquivos do carro; sai em cinza escuro.
- Vidro (material com `glass`) é desenhado com alfa fixo de 40% quando **Vidro translúcido** está ligado.
- Não há normal map; a luz vem de uma normal calculada por derivadas de tela.
- Quando o material não declara textura, a cor é escolhida pelo nome (`guessTexture` em `content.js`), e o inspetor marca "(textura escolhida pelo nome)". A banda de rodagem não tem textura própria: o shader amostra uma faixa do atlas da roda.

## Interface

- Workspace `#car-ws`: barra de ferramentas, árvore à esquerda, viewport e inspetor à direita.
- A árvore e o inspetor são acoplados ao workspace (`carDock`) e voltam ao layout comum nas outras abas.
- Os painéis são redimensionáveis (`makeResizer`): arrastar a divisória, setas do teclado de 16 em 16 px, duplo clique restaura. A largura fica no `localStorage`.
- Telas estreitas: até 860 px o inspetor vira gaveta; até 640 px a árvore também (`matchMedia` em `ensureCarStage`).

## Limites e o que não foi testado

- **As edições só existem na página.** Nenhum botão grava; a escrita de pacote existe em `tools/egodata/nefs_write.py`, mas o Car Explorer não a usa.
- O gizmo só usa eixos globais e foi testado com eventos simulados, não com mouse real em todas as combinações.
- Não há edição de escala nem importação de geometria.
- A seleção e a troca de ferramenta não entram no histórico; ocultar, isolar, mostrar tudo, mover, girar e restaurar entram.
- As câmeras do jogo e os remendos de roda são aproximações (veja acima).
- Nada disso foi levado ao jogo.

## Onde está o código

| Arquivo | Papel |
| --- | --- |
| `tools/uiview/car/carmodel.py` | Monta a árvore do carro a partir do PSSG (`build_car_model`) e o formato DR2C (`pack_resources`, `unpack_resources`) |
| `tools/uiview/car/models.py` | Catálogo de pacotes, classificação (`classify_path`), limites de tamanho, texturas, câmeras (`_car_cameras`) e exportação (`export_models`) |
| `tools/uiview/car/__main__.py` | `python -m tools.uiview.car` |
| `tools/uiview/web/js/carview.js` | Viewer: árvore, GPU, seleção, gizmo, histórico, câmeras do jogo, inspetor |
| `tools/uiview/web/js/content.js` | Funções comuns com a aba Modelos: matrizes, `guessTexture`, cortes de roda |
| `tools/uiview/tests/test_carmodel.py` | Testes da árvore e do DR2C |

## Formatos

- O formato DR2C está documentado no cabeçalho de `tools/uiview/car/carmodel.py`.
- O PSSG é lido por `tools/egodata/pssg.py`.
- A ideia de carros personalizados está em [`demands/vehicle-editor.md`](../demands/vehicle-editor.md) (especulação).
