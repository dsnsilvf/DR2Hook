# DR2Hook Editor de Pistas (`viewer3d`)

Visualizador e editor 3D nativo (C++20, SDL3, OpenGL 3.3 core) das pistas já exportadas pelo Python (`python -m tools.uiview.track`). Ele abre o terreno com texturas, os objetos instanciados, os portões e a linha da IA. Também seleciona, move, gira e apaga objetos, e grava o `edits.json` que `python -m tools.uiview.track.edit` aplica num `.nefs` novo. Plano e etapas: [`docs/plans/viewer3d/`](../../docs/plans/viewer3d/README.md). Formatos lidos e gravados: [`formatos.md`](../../docs/plans/viewer3d/formatos.md).

## Dependências

SDL3, GLEW, OpenGL, libwebp, GLM (só cabeçalhos), pkg-config e CMake ≥ 3.20 (Ninja opcional). O Dear ImGui 1.91.9b, ramo docking (painéis), vem em `third_party/imgui/` ([nota](third_party/imgui/README.md)), e o gizmo de mover e girar é o ImGuizmo, em `third_party/imguizmo/` ([nota](third_party/imguizmo/README.md)). As fontes (Inter no texto e os ícones Lucide) vão dentro do executável ([`assets/fonts/`](assets/fonts/README.md)): nada depende das fontes do sistema. Com o FreeType instalado (opcional), o texto sai com o rasterizador dele, mais nítido.

| Sistema | Pacotes |
| --- | --- |
| CachyOS / Arch | `sdl3 glew glm libwebp` (opcional: `freetype2`) |
| Ubuntu 24.04 | `libglew-dev libgl-dev libglm-dev libwebp-dev pkg-config` (opcional: `libfreetype-dev`); o SDL3 não tem pacote: compile o fonte (`release-3.2.x` ou mais novo) e passe `-DCMAKE_PREFIX_PATH=<prefixo>` |

## Compilar e rodar

```bash
cmake -S tools/viewer3d -B build/viewer3d -G Ninja
cmake --build build/viewer3d
./build/viewer3d/viewer3d                                   # cena de teste (cubo e grade)
./build/viewer3d/viewer3d --track build/uiview/tracks/portugal__montalegre_rallycross
```

Sem o jogo, use a pista sintética que vem no repositório: `--track examples/tracks/synthetic__dr2hook_ring` ([exemplos](../../examples/README.md)). Para gerar outra cópia, `python -m tools.synthtrack` grava `build/uiview/tracks/synthetic__dr2hook_ring` ([docs](../../docs/tools/synthtrack.md)).

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
python3 tools/viewer3d/tests/probe_check.py <pista>   # sonda de raio contra Möller–Trumbore em numpy (0 divergências)
python3 tools/viewer3d/tests/ground_check.py <pista>  # Pôr no chão e oclusão do picking contra a força bruta
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

Um editor com painéis encaixáveis em volta do 3D, como os de motor de jogo:

- **Menu**: Arquivo (abrir outra pista de `build/uiview/tracks/` ou dos exemplos, gravar, recuperar o autosave, sair), Editar (as ações abaixo), Exibir (camadas, vistas, painéis, **Restaurar layout padrão**), Jogo (testar, pausar, parar) e Ajuda (atalhos e **Sobre**: autor e agradecimentos). Com o jogo aberto ou um teste rodando, o canto direito do menu diz.
- **Barra de ferramentas**, da esquerda para a direita: as ferramentas **Selecionar**, **Mover** e **Girar**; os **eixos do gizmo** (globo: da pista; cubo: do objeto, **X**); **Desfazer**, **Refazer** e **Gravar** (com um ponto amarelo quando há edições não gravadas); o **encaixe** (ímã), com o passo do mover (0.1 a 5 m) e do girar (1 a 90°) ao lado, e escolher um passo liga o encaixe; **Grudar no chão**. No meio, o grupo do jogo: **Testar no jogo**, **Pausar** e **Parar**. Na direita, a **rota** e a **Exibição** (olho): distância de desenho dos objetos e do terreno, as camadas e o jogo ao vivo (carro e vistas). Cada botão tem dica com o atalho.
- **Painéis**: **Cena** e **Histórico** à esquerda, **Inspector** à direita; o 3D fica com o meio. Arraste um painel pela aba para outro lado, para cima de outro (vira aba) ou para fora (janela solta); as bordas entre eles mudam a largura. O **X** da aba fecha e **Exibir** abre de novo no mesmo lugar. O layout, os painéis abertos, o encaixe e os passos, **Grudar no chão**, os eixos do gizmo e as opções do **Testar no jogo** ficam no `imgui.ini` da pasta de preferências (`~/.local/share/DR2ModLoader/viewer3d/` no Linux, `%APPDATA%\DR2ModLoader\viewer3d\` no Windows). Com `--frames` o arquivo não é lido nem gravado: as capturas saem sempre no layout padrão.
- **Cena**: terreno, portões, linha da IA, largada, câmeras do replay, carro do jogo e as camadas de objetos, cada um com a contagem e um olho que mostra ou esconde. Os tipos têm filtro por nome; as instâncias mostram o id de texto do `objects.ens`, editadas e novas em amarelo, apagadas em cinza (clicar numa apagada seleciona para Restaurar). Duplo clique enquadra (na vaga ou na câmera, olha por ela).
- **Histórico**: clicar num passo volta ou avança até ele; os passos à frente ficam em cinza.
- **Inspector**: mostra o último clicado. Um objeto: tipo, origem, estado, **posição X/Y/Z e giro Y editáveis** (arraste ou Ctrl+clique para digitar; um passo de histórico por edição), escala, botões de giro, as ações (Enquadrar, Restaurar, Duplicar, Apagar, Pôr no chão, Alinhar ao terreno, Alinhar todos deste tipo, com **Manter em pé** para prédios e placas), a matriz atual e a do arquivo e os materiais com a textura em miniatura (passe o mouse para ampliar). Uma vaga de largada: posição, lado da pista e altura sobre o chão. Uma câmera do replay: posição, caminho e as zonas que a ligam. Sem nada selecionado, a pista (rota, contagens, onde grava, memória de vídeo).
- **Viewport**: caixa amarela na seleção. Com Mover e Girar, o gizmo do [ImGuizmo](third_party/imguizmo/README.md): o ponto agarrado fica sob o cursor (arraste pelo raio do mouse, não por pixels), e ao lado aparece quanto andou ou girou. No Mover, as setas X/Y/Z prendem num eixo, os quadrados prendem num plano (o verde é o chão, X e Z) e o centro move junto com a tela. No Girar, o anel gira em Y. Com os eixos da pista as setas seguem X, Y e Z; com os do objeto (**X** ou o botão da barra) elas giram com ele. Cada arraste é um passo de histórico (“Mover X”, “Mover no chão (XZ)”, “Girar Y”…). O encaixe vale no gizmo: no mover pelos eixos da pista, a posição cai na grade (como no arraste no chão); pelos eixos do objeto e no girar, em passos contados de onde começou. Com **Grudar no chão**, a altura acompanha o terreno, exceto na seta verde.
- **Barra de status**: ferramenta, gravado/não gravado, a última mensagem (some depois de 8 s; erros em vermelho, 30 s; sem mensagem, a dica da ferramenta), o andamento do teste no jogo, o jogo ao vivo, instâncias, raio e fps.
- **Título da janela**: a pista e o arquivo de edições, com `*` enquanto houver edições não gravadas, e o nome do editor (`pista · arquivo * — DR2Hook Editor de Pistas`).
- **Testar no jogo**: o botão verde (▶) no meio da barra, ou **F5**. Abre uma janela com **Inicialização rápida** (pula a tela de carregamento: foto aérea e traçado, fica a da última vez; e o jogo fica de tela preta, sem menu e sem som, até a largada, com o log da carga ao vivo como num terminal) e **Quem dirige**: bot, câmera livre (F9 na largada) ou "Eu dirijo", que ainda está desligado porque o AutoStage é o benchmark do jogo e não passa o controle ao jogador. **Enter** começa: o editor grava as edições atuais (inclusive as não gravadas, só da `route_0`) em `build/re/ring_deploy/viewer.edits.json` e roda `scripts/research/ring_deploy.py`. A janela mostra a etapa, uma barra com % e tempo e o log ao vivo; depois do porte, as etapas são as fases da carga no `dr2hook.log` (iniciando, dados do jogo, pista, largada), e um crash do jogo para o teste na hora. **Esc** esconde a janela; enquanto roda, o botão gira com a barra de progresso embaixo, e clicar nele mostra a janela de novo. **Cancelar o teste** mata o porte (o jogo já aberto segue). Só para o DR2 Hook Ring (`synthetic__dr2hook_ring`) e no Linux; detalhes em `docs/reverse_engineering/track_loading.md` §12.3.
- **Pausar** (**F6**): pausa ou continua a especial aberta no jogo, pelo canal de comandos (`ring_deploy.py --cmd pause|unpause`). **Parar** (**Shift+F5**): cancela o teste em andamento e fecha o jogo (`ring_deploy.py --close`). O resultado aparece na barra de status.
- **Autosave**: a cada 60 s com edições não gravadas, grava `<arquivo>.autosave.json` (o Ctrl+S e a saída normal apagam). Se o editor cair, ao abrir de novo a barra de status avisa em vermelho; **Arquivo > Recuperar autosave** volta às edições dele (sem histórico, como não gravadas; o Ctrl+S grava no arquivo de sempre).
- **Abrir outra pista** pelo menu fecha a atual antes (memória); se a nova não abre, a anterior volta com as edições não gravadas (sem o histórico).
- **Pistas grandes**: o terreno só desenha as malhas que a câmera vê (corte por frustum, um `glMultiDrawElements` por material) e, opcionalmente, até um raio (**Terreno** na Exibição, ou `--terrain-dist`). O envio do terreno à GPU é malha por malha, sem uma cópia da pista inteira na RAM.
- **Retomar**: se o `edits.json` de saída já existe, as edições dele voltam ao abrir (as que não batem com a pista exportada ficam de fora, com aviso). `--fresh` ignora o arquivo.

## Controles

A câmera é a do Track Explorer web (`tvCam`, `tvVp`, `tvKeys` em `tools/uiview/web/js/trackview.js`).

| Ação | Entrada |
| --- | --- |
| Orbitar | botão esquerdo (na ferramenta Selecionar, ou fora de um objeto) |
| Pan | botão direito ou do meio, ou Shift + esquerdo |
| Zoom | roda |
| Andar | W A S D (Shift = ×3; parado com Ctrl apertado) |
| Ferramentas | **1** Selecionar, **2** Mover (arrastar no chão; com Shift, antes ou durante o arraste, sobe e desce), **3** Girar (arrastar para os lados); **X** troca os eixos do gizmo entre os da pista e os do objeto |
| Selecionar | clique sem arrastar (ou na Cena): objetos, câmeras do replay e vagas de largada (as duas últimas têm prioridade, porque aparecem por cima de tudo; de longe valem uns 10 pixels em volta); clique no vazio tira a seleção, **Esc** tira a do objeto |
| Apagar | **Delete** |
| Duplicar | **Ctrl+D**: cópia 2 m adiante em x, já selecionada (só objetos `e:` de `objects.ens`, como no web) |
| Girar no teclado | **E** +15°, **Q** −15°; com **Shift**, ±90° |
| Restaurar | **R** volta o selecionado à matriz do arquivo e o mostra |
| Rota | **Tab** próxima, **Shift+Tab** anterior; cada rota guarda as próprias edições e histórico |
| Histórico | **Ctrl+Z** desfaz; **Ctrl+Y** ou **Ctrl+Shift+Z** refaz (300 passos) |
| Gravar | **Ctrl+S** grava o `edits.json` com as edições de todas as rotas abertas (o plano diz **S**, mas **S** já é andar para trás). Se o arquivo já existia ao abrir, o primeiro Ctrl+S guarda uma cópia em `<arquivo>.<n>.bak`. A gravação é atômica (`.tmp` e renomeia); se falhar, o editor avisa na barra de status e continua aberto |
| Enquadrar | **F**: o selecionado, ou a rota sem seleção (na cena de teste, volta ao início) |
| Pôr no chão | **T** (ou o botão do Inspector, ou Editar): a altura do selecionado vira a do terreno sob ele, um passo de histórico. A barra tem **Grudar no chão**: ao arrastar, a altura segue o terreno |
| Alinhar ao terreno | **Shift+T** (ou o botão do Inspector, ou Editar): o selecionado assenta no terreno sob a base, inclinado com o chão (até 25°) e baixado até nenhum canto ficar no ar; mantém o rumo e a escala. Árvores e **Manter em pé** (Inspector) só descem até o canto mais baixo, sem inclinar. **Todos deste tipo** alinha de uma vez todas as cópias visíveis do tipo, um passo de histórico |
| Camadas | **F1** terreno, **F2** objetos, **F3** árvores, **F4** terreno distante, **G** portões, **I** linha da IA, **C** câmeras do replay, **L** largada (vagas do carro) |
| Câmeras do replay | Pistas do `synthtrack` trazem `replay` no `track.json`. Cada câmera é um ícone de câmera em arame (corpo, lente apontando para onde ela olha e dois rolos de filme em cima), com uma linha até onde mira; a em destaque mostra também o cone de visão. Laranja: câmera da beira da pista; ciano: fixa; magenta: com trilho (caminho Bézier) e rosa: o alvo dele; amarelo: zona de troca (verde-água: só na 1ª volta), com linhas até as câmeras que ela liga; vermelho: prismas em volta das peças altas. **Shift+C** (ou Exibir → Ver pela próxima câmera) põe a vista no lugar da próxima câmera, olhando para onde ela olha. Na Cena, **Câmeras do replay** lista todas; clique destaca em branco (com as zonas que a ligam), duplo clique ou **Ver por esta câmera** olha por ela |
| Largada | Pistas do `synthtrack` trazem `grids` no `track.json`: as vagas onde o carro nasce no jogo. Cada vaga é uma caixa do tamanho do carro com seta no teto apontando a frente e um poste de 2,5 m. Cores: verde contra-relógio/treino, cinza-azulado reset perto da largada, azul largada parada, lilás largada escalonada, bege paddock; as cruzes são os nós de apoio. **Shift+L** (ou Exibir → Ver da próxima vaga) põe a vista no banco do piloto da próxima vaga. Na Cena, **Largada** lista as vagas por grade. Clicar destaca a vaga em branco e mostra posição, lado da pista e altura sobre o chão: vermelho se o centro ou uma roda fica abaixo de 0,2 m (no jogo o carro nasce enterrado e a carga trava), amarelo se passa de 1,5 m. Duplo clique ou **Ver do carro** olha de dentro dela |
| Distância de desenho | **[** e **]** (100 a 4000 m, padrão 700 m), ou a Exibição |
| Sair | **Ctrl+Q** ou fechar a janela; com edições não gravadas, pergunta (Gravar e sair / Sair sem gravar / Cancelar). **Esc** não sai |
| Testar no jogo | **F5** (ou o botão verde): porta a pista e abre o jogo nela. Na janela, **↑↓** escolhe quem dirige, **R** liga e desliga a rápida, **Enter** começa, **Esc** fecha |
| Pausar o jogo | **F6** pausa ou continua a especial aberta |
| Parar | **Shift+F5** cancela o teste em andamento e fecha o jogo |
| Painéis | **F10** esconde e mostra a barra e os painéis (o menu e a barra de status ficam); **F11** atalhos |

Se uma rota não abre no **Tab** (arquivo truncado ou ausente), a rota atual fica como estava, com edições e histórico, e a barra de status diz o motivo.

## Opções

| Opção | Efeito |
| --- | --- |
| `--track DIR` | abre a pista exportada em `DIR` (rota 0) |
| `--out arq.json` | onde **Ctrl+S** grava (padrão `<raiz>/saves/<id>.edits.json` para uma pista em `<raiz>/tracks/<id>`, independente da pasta atual); caminho na pasta do jogo, ou uma pasta, é recusado com código 1 |
| `--frames N` | roda `N` quadros, imprime `OK renderer=... gl=... frames=N fps=...` (e as contagens da pista) e sai com 0; não lê nem grava o `imgui.ini` (layout padrão) |
| `--screenshot arq.ppm` | com `--frames`, grava o último quadro em PPM (P6) antes de sair |
| `--vsync 0\|1` | sincronia vertical (padrão 1); `0` para medir FPS |
| `--panels 0\|1` | começa sem os painéis (o 3D ocupa a janela, para comparar capturas com o web) |
| `--terrain-dist M` | raio do terreno em metros a partir do alvo da câmera, no plano xz (padrão 0 = sem limite) |
| `--autosave S` | segundos entre autosaves (padrão 60; 0 desliga) |
| `--fresh` | não retoma o `edits.json` que já existe (o primeiro Ctrl+S guarda uma cópia dele) |
| `--walk M` | anda com a câmera `M` metros por quadro (para medir o corte das instâncias) |
| `--tex-mb M` | limite de VRAM estimada das texturas, em MB (padrão 1536; as menos usadas saem) |
| `--tex-max-side PX` | maior lado de textura na GPU (padrão 2048; 0 = sem limite) |
| `--tex-threads N` | threads de decodificação de WebP (padrão 2) |
| `--wait-textures` | só conta quadro com a fila de texturas vazia (capturas repetíveis) |
| `--hide terrain,obj,tree,dist,lines,replay,grids` | começa com essas camadas escondidas (`lines`: portões, linha da IA, câmeras do replay e vagas; `replay`: só as câmeras; `grids`: só as vagas) |
| `--look NOME` | começa vendo pela câmera do replay `NOME` (ex.: `camera_r0_spectator_003`) ou do banco do piloto da vaga `NOME` (`slot_0`, `grid_start_standing_01/slot_05`); sai com 1 se não existe na rota |
| `--grid-check` | imprime cada vaga (`grade/vaga x y z centro rodas`, alturas sobre o chão), marca `DENTRO-DO-CHAO` abaixo de 0,2 m e sai com 1 se houver alguma |
| `--launch` | começa com a janela do Testar no jogo aberta (para capturas; nada roda sem o Enter) |
| `--select IDX` | começa com a instância `IDX` selecionada e enquadrada (o Inspector mostra o objeto); sai com 1 se não existe na rota |
| `--drag X0,Y0,X1,Y1` | arrasta com o botão esquerdo de um ponto a outro da janela (pontos), do 10º ao 22º quadro, com eventos do SDL como os do mouse de verdade, e imprime `arraste: <passo do histórico>; seleção N em x y z`. Testa o gizmo: `--select 0 --tool move --drag 745,452,845,452` dá `Mover X` |
| `--click X,Y` | clica (sem arrastar) nesse ponto da janela, em pontos, no 10º quadro: testes da seleção com `--frames` |
| `--tool select\|move\|rotate` | começa nessa ferramenta (com `--select`, mostra o gizmo) |
| `--ui exibicao\|atalhos\|sobre` | começa com a janela Exibição, Atalhos ou Sobre aberta (capturas) |
| `--shot-size LxA` | com `--screenshot`, grava o último quadro num framebuffer fora da tela desse tamanho, só o 3D (sem painéis); pode passar do tamanho da tela |
| `--probe-rays arq` | testes: cada linha `ox oy oz dx dy dz`, imprime a distância até o terreno (ou `none`) e sai; `tests/probe_check.py` confere contra a força bruta |
| `--ground-check arq` | testes: cada linha é um índice de instância; assenta, testa o picking de cima e de baixo e sai; `tests/ground_check.py` confere |
| `--settle-list arq [--settle-redo]` | testes: assenta as instâncias da lista e segue (com `--settle-redo`, desfaz e refaz tudo, para comparar capturas) |
| `--touch-test IDX,dx,dy,dz[,commit]` | testes: desloca a instância em 8 quadros só pelo reenvio parcial (com `commit`, fecha o passo e força o corte completo) |
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
| R3 | pista de estresse (`tests/make_stress.py`: 9,7 M vértices, 16,9 M triângulos, 323 mil instâncias): câmera padrão 3,3 → 18,7 fps, rente ao chão 1,8 → 2,6 fps (17,9 com `--terrain-dist 3000`), panorâmica 1,4 → 1,8 (5,7 com o raio, que corta malha a malha e deixa buracos onde só a malha de fundo de um bloco passa); pico de RSS 1679 → 943 MB; envio 0,67 → 0,45 s; capturas da pista sintética idênticas byte a byte às de antes; retomar 50 mil edições 4,8 → 0,05 s; menu depois de 100 teclas a ~3 fps: 31,4 → 2,8 s; autosave e retomada depois de um kill |
| 7 | `edit_test OK`; sessão com `xdotool`: mover, girar, apagar, Ctrl+Z ×2, Ctrl+Y, Ctrl+S → 2 edições (x/z de uma barreira, linhas da matriz de outra); segunda sessão com apagar barreira, apagar árvore e mover árvore → 3 edições; as duas passam no `edit_roundtrip.py` |
| Painéis 2026-10-08 | ramo docking, tema, fontes embutidas e barra de ícones: `camera_test`, `edit_test` e `core_tests` OK; capturas a 1440×810 (GNOME/Wayland, sem `xdotool`) da pista, de um objeto com o gizmo do Mover (`--select 0 --tool move`), de uma vaga (`--look`), de uma câmera do replay, da Exibição, dos Atalhos e do Testar no jogo; `imgui.ini` de ida e volta (encaixe, passo de 2 m, modo do teste e o Histórico fechado, com a Cena ocupando a altura toda). Cliques e arrastes com o mouse de verdade ficam para a máquina do dono |
| Gizmo 2026-10-08 | ImGuizmo no lugar do gizmo próprio: `camera_test`, `edit_test` e `core_tests` OK; capturas do Mover e do Girar (eixos da pista e do objeto, num objeto girado); com `--drag`, a seta X levou o objeto 0 de x 0,25 a 1,74 (“Mover X”), o quadrado verde moveu em X e Z (“Mover no chão (XZ)”) e o anel girou (“Girar Y”), um passo de histórico cada. Encaixe e Grudar no chão pelo gizmo e o mouse de verdade ficam para a máquina do dono |

Falta, na máquina do dono, com a Montalegre:
- os números das etapas 4 a 6: 1324 / 428 789 / 557 900, 820 materiais, 2897 instâncias e ≥ 60 FPS na RTX 4050;
- a comparação com o web;
- a ida e volta da etapa 7 com `python -m tools.uiview.track.edit`.

## O que ficou de fora

- As linhas de portões e da IA mostram só a rota atual (o web desenha as de todas as rotas).
- PSSG em C++, Polônia, Windows.
- Seleção múltipla, zoom em direção ao cursor.

## Ideias

(Fora do plano; anotar aqui e seguir.)

- Melhorias de GPU (texturas comprimidas e em segundo plano, corte incremental das instâncias, picking com profundidade, índices de 16 bits, medição de VRAM): [`melhorias_gpu.md`](../../docs/plans/viewer3d/melhorias_gpu.md).

- Um modo `--script` que leia uma lista de ações (selecionar, mover, gravar) para testar a edição sem `xdotool`.
