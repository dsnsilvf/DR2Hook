# DR2 Viewer3D

Visualizador e editor 3D nativo (C++20, SDL3, OpenGL 3.3 core) das pistas já exportadas pelo Python (`python -m tools.uiview.track`). Ele abre o terreno com texturas, os objetos instanciados, os portões e a linha da IA. Também seleciona, move, gira e apaga objetos, e grava o `edits.json` que `python -m tools.uiview.track.edit` aplica num `.nefs` novo. Plano e etapas: [`docs/plans/viewer3d/`](../../docs/plans/viewer3d/README.md). Formatos lidos e gravados: [`formatos.md`](../../docs/plans/viewer3d/formatos.md).

## Dependências

SDL3, GLEW, OpenGL, libwebp, GLM (só cabeçalhos), pkg-config e CMake ≥ 3.20 (Ninja opcional). O Dear ImGui 1.91.9b (painéis) vem em `third_party/imgui/` ([nota](third_party/imgui/README.md)); uma fonte com acentos é lida do sistema (DejaVu Sans ou Noto Sans), senão a do ImGui.

| Sistema | Pacotes |
| --- | --- |
| CachyOS / Arch | `sdl3 glew glm libwebp` |
| Ubuntu 24.04 | `libglew-dev libgl-dev libglm-dev libwebp-dev pkg-config`; o SDL3 não tem pacote: compile o fonte (`release-3.2.x` ou mais novo) e passe `-DCMAKE_PREFIX_PATH=<prefixo>` |

## Compilar e rodar

```bash
cmake -S tools/viewer3d -B build/viewer3d -G Ninja
cmake --build build/viewer3d
./build/viewer3d/viewer3d                                   # cena de teste (cubo e grade)
./build/viewer3d/viewer3d --track build/uiview/tracks/portugal__montalegre_rallycross
```

Sem o jogo, use a pista sintética: `python -m tools.synthtrack` grava `build/uiview/tracks/synthetic__dr2hook_ring` ([docs](../../docs/tools/synthtrack.md)).

Para usar a NVIDIA num notebook híbrido:

```bash
__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia ./build/viewer3d/viewer3d --track ...
```

Se a janela não abrir em Wayland, tente `SDL_VIDEO_DRIVER=x11`.

### Testes

```bash
./build/viewer3d/camera_test                         # câmera contra as constantes do web
./build/viewer3d/edit_test                           # histórico, giro, edits.json, recusa da pasta do jogo (sem GL)
./build/viewer3d/core_tests [--track DIR]            # JSON, DR2M, DR2I; com --track, a pista contra track.json,
                                                     # o expected.json da pista sintética ou os números da Montalegre
python3 tools/viewer3d/tests/edit_roundtrip.py <edits.json> build/uiview/tracks/synthetic__dr2hook_ring
                                                     # aplica um edits.json do viewer com as funções de track/edit.py
```

### Sem tela (CI, nuvem)

Com Mesa (`libgl1-mesa-dri`) e Xvfb, o contexto 3.3 core sai pelo llvmpipe, em software:

```bash
Xvfb :99 -screen 0 1600x900x24 &
DISPLAY=:99 ./build/viewer3d/viewer3d --track ... --frames 60 --screenshot e.ppm
```

## Janela

- **Menu**: Arquivo (abrir outra pista de `build/uiview/tracks/`, gravar, sair), Editar (as ações abaixo), Exibir (camadas, painéis, histórico), Ajuda (atalhos).
- **Barra de ferramentas**: Navegar/Mover/Girar, Desfazer/Refazer, Gravar (laranja quando há edições não gravadas), rota, encaixe do mover (0,1/0,5/1 m) e do girar (5/15/45°), distância de desenho.
- **Cena** (esquerda): terreno e camadas com visibilidade, tipos com contagem (filtro por nome), instâncias com o id de texto do `objects.ens`; editadas em amarelo, apagadas em cinza (clicar numa apagada seleciona para Restaurar). Duplo clique enquadra. Embaixo, o **Histórico**: clicar num passo volta ou avança até ele.
- **Inspector** (direita): sem seleção, a pista (rotas, contagens, texturas, onde grava); com seleção, tipo, kind, arquivo de origem, idnum, estado, **posição X/Y/Z e giro Y editáveis** (arraste ou Ctrl+clique para digitar; um passo de histórico por edição), escala, botões de giro, Enquadrar/Restaurar/Duplicar/Apagar, a matriz atual e a do arquivo, e os materiais do tipo com a textura em miniatura (passe o mouse para ampliar).
- **Viewport**: caixa amarela na seleção; com Mover, gizmo de setas X/Y/Z (arraste uma seta para mover só naquele eixo); com Girar, um anel em volta do objeto.
- **Barra de status**: ferramenta, gravado/não gravado, última mensagem (some depois de 8 s; erros em vermelho, 30 s), instâncias, raio, fps.
- **Autosave**: a cada 60 s com edições não gravadas, grava `<arquivo>.autosave.json` (o Ctrl+S apaga). Se o editor cair, ao abrir de novo a barra de status avisa; `--out <arquivo>.autosave.json` retoma dele.
- **Pistas grandes**: o terreno só desenha as malhas que a câmera vê (corte por frustum, um `glMultiDrawElements` por material) e, opcionalmente, até um raio (**Terreno** na barra de ferramentas, ou `--terrain-dist`). O envio do terreno à GPU é malha por malha, sem uma cópia da pista inteira na RAM.
- **Retomar**: se o `edits.json` de saída já existe, as edições dele voltam ao abrir (as que não batem com a pista exportada ficam de fora, com aviso). `--fresh` ignora o arquivo.

## Controles

A câmera é a do Track Explorer web (`tvCam`, `tvVp`, `tvKeys` em `tools/uiview/web/js/trackview.js`).

| Ação | Entrada |
| --- | --- |
| Orbitar | botão esquerdo (na ferramenta Navegar, ou fora de um objeto) |
| Pan | botão direito ou do meio, ou Shift + esquerdo |
| Zoom | roda |
| Andar | W A S D (Shift = ×3; parado com Ctrl apertado) |
| Ferramentas | **1** Navegar, **2** Mover (arrastar no chão; com Shift, antes ou durante o arraste, sobe e desce), **3** Girar (arrastar para os lados) |
| Selecionar | clique sem arrastar (ou na Cena); clique no vazio ou **Esc** tira a seleção |
| Apagar | **Delete** |
| Duplicar | **Ctrl+D**: cópia 2 m adiante em x, já selecionada (só objetos `e:` de `objects.ens`, como no web) |
| Girar no teclado | **E** +15°, **Q** −15°; com **Shift**, ±90° |
| Restaurar | **R** volta o selecionado à matriz do arquivo e o mostra |
| Rota | **Tab** próxima, **Shift+Tab** anterior; cada rota guarda as próprias edições e histórico |
| Histórico | **Ctrl+Z** desfaz; **Ctrl+Y** ou **Ctrl+Shift+Z** refaz (300 passos) |
| Gravar | **Ctrl+S** grava o `edits.json` com as edições de todas as rotas abertas (o plano diz **S**, mas **S** já é andar para trás). Se o arquivo já existia ao abrir, o primeiro Ctrl+S guarda uma cópia em `<arquivo>.<n>.bak`. A gravação é atômica (`.tmp` e renomeia); se falhar, o editor avisa na barra de status e continua aberto |
| Enquadrar | **F**: o selecionado, ou a rota sem seleção (na cena de teste, volta ao início) |
| Camadas | **F1** terreno, **F2** objetos, **F3** árvores, **F4** terreno distante, **G** portões, **I** linha da IA |
| Distância de desenho | **[** e **]** (100 a 4000 m, padrão 700 m) |
| Sair | **Ctrl+Q** ou fechar a janela; com edições não gravadas, pergunta (Gravar e sair / Sair sem gravar / Cancelar). **Esc** não sai |
| Painéis | **F10** esconde e mostra; **F11** atalhos |

O título mostra a ferramenta, a rota, as contagens do terreno, as instâncias visíveis/total, o raio, o histórico, "não gravado" quando há edições fora do arquivo, a seleção (tipo, `kind`, `idnum`, posição) e a última mensagem (gravou, não gravou, não abriu a rota).

Se uma rota não abre no **Tab** (arquivo truncado ou ausente), a rota atual fica como estava, com edições e histórico, e o título diz o motivo.

## Opções

| Opção | Efeito |
| --- | --- |
| `--track DIR` | abre a pista exportada em `DIR` (rota 0) |
| `--out arq.json` | onde **Ctrl+S** grava (padrão `<raiz>/saves/<id>.edits.json` para uma pista em `<raiz>/tracks/<id>`, independente da pasta atual); caminho na pasta do jogo, ou uma pasta, é recusado com código 1 |
| `--frames N` | roda `N` quadros, imprime `OK renderer=... gl=... frames=N fps=...` (e as contagens da pista) e sai com 0 |
| `--screenshot arq.ppm` | com `--frames`, grava o último quadro em PPM (P6) antes de sair |
| `--vsync 0\|1` | sincronia vertical (padrão 1); `0` para medir FPS |
| `--panels 0\|1` | começa sem os painéis (o 3D ocupa a janela, para comparar capturas com o web) |
| `--terrain-dist M` | raio do terreno em metros a partir da câmera (padrão 0 = sem limite) |
| `--autosave S` | segundos entre autosaves (padrão 60; 0 desliga) |
| `--fresh` | não retoma o `edits.json` que já existe (o primeiro Ctrl+S guarda uma cópia dele) |
| `--camera yaw,pitch,dist,x,y,z` | estado exato da câmera, para comparar capturas com o viewer web (valores finitos, `dist` > 0, \|pitch\| ≤ 1,5) |

Ver o PPM: `python3 -c "from PIL import Image; Image.open('arq.ppm').save('arq.png')"`. No `stderr` saem os tempos de leitura e de envio à GPU do terreno, e os das texturas.

## Camadas do código

| Pasta | Alvo | Depende de |
| --- | --- | --- |
| `src/core/` | `dr2core`: JSON, DR2M, DR2I, `track.json` | nada (o CMake falha se ganhar dependência) |
| `src/edit/` | `dr2edit`: histórico, giro, `edits.json` | `dr2core` |
| `src/render/` | `dr2render`: GL, câmera, shader da pista, terreno, texturas, instâncias, linhas, picking | `dr2core`, GLEW, GLM, libwebp |
| `src/app/` | `viewer3d`: janela, entrada, laço, `TrackView`, painéis (`ui.cpp`) | tudo, SDL3 e `imgui` (`third_party/imgui`) |

## Verificado

Neste ambiente não há GPU nem jogo: tudo rodou no Mesa llvmpipe sob Xvfb, com a pista sintética.

| Etapa | Resultado |
| --- | --- |
| 1 | `OK renderer=llvmpipe (LLVM 20.1.2, 256 bits) gl=4.5 (Core Profile) Mesa 25.2.8-0ubuntu0.24.04.2 frames=120` (Ubuntu 24.04, SDL 3.2.24, GLEW 2.2.0) |
| 2 | `--frames 30 --screenshot`: canto `(140, 173, 209)`, 54 196 cores distintas, triângulo vermelho/verde/azul |
| 3 | `camera_test OK`; cubo e grade; com `xdotool`, orbitar, roda, pan e `D` mudam o quadro e `F` volta ao quadro inicial byte a byte |
| 4 e 5 | `core_tests --track`: 164 malhas / 151 989 vértices / 263 862 triângulos iguais ao `unpack_geom`; leitura 0,007 s (5,3 MB), envio 0,009 s; 5 texturas do terreno; mesmo quadro e mesmas texturas que o web |
| 6 | 1011 instâncias; nas mesmas câmeras do web, 995, 935 e 980 visíveis, os mesmos números do web; ~20 fps no llvmpipe |
| 7+ | duplicar, restaurar, ±15°/±90° e troca de rota (pista sintética com `route_1`): sessão com `xdotool` → 3 edições em duas rotas (giro de 105°, cópia `added`, apagar na `route_1`), aprovadas pelo `edit_roundtrip.py` |
| R2 | painéis: com `xdotool`, seleção no 3D e na Cena (filtro "tyre"), seta X do gizmo move 72,31→80,86, X digitado no Inspector, clique no Histórico volta ao arquivo, Ctrl+Q pede confirmação, retomar o edits.json gravado (1 edição de volta) |
| R3 | pista de estresse (`tests/make_stress.py`: 9,7 M vértices, 16,9 M triângulos, 323 mil instâncias): câmera padrão 3,3 → 18,7 fps, rente ao chão 1,8 → 2,6 fps (17,5 com `--terrain-dist 3000`), panorâmica 1,4 → 1,8 (13,9); pico de RSS 1679 → 943 MB; envio 0,67 → 0,45 s; capturas da pista sintética idênticas byte a byte às de antes; retomar 50 mil edições 4,8 → 0,05 s; menu depois de 100 teclas a ~3 fps: 31,4 → 2,8 s; autosave e retomada depois de um kill |
| 7 | `edit_test OK`; sessão com `xdotool`: mover, girar, apagar, Ctrl+Z ×2, Ctrl+Y, Ctrl+S → 2 edições (x/z de uma barreira, linhas da matriz de outra); segunda sessão com apagar barreira, apagar árvore e mover árvore → 3 edições; as duas passam no `edit_roundtrip.py` |

Falta, na máquina do dono, com a Montalegre:
- os números das etapas 4 a 6: 1324 / 428 789 / 557 900, 820 materiais, 2897 instâncias e ≥ 60 FPS na RTX 4050;
- a comparação com o web;
- a ida e volta da etapa 7 com `python -m tools.uiview.track.edit`.

## O que ficou de fora

- As linhas de portões e da IA mostram só a rota atual (o web desenha as de todas as rotas).
- PSSG em C++, Polônia, Windows.
- Seleção múltipla, assentar no terreno, picking que respeita o terreno (hoje um objeto atrás de um morro pode ser selecionado), zoom em direção ao cursor.
- Docking de painéis (a 1.91.9b sem o ramo docking): o layout é fixo, as larguras das laterais se ajustam arrastando a borda.

## Ideias

(Fora do plano; anotar aqui e seguir.)

- Um modo `--script` que leia uma lista de ações (selecionar, mover, gravar) para testar a edição sem `xdotool`.
