# Viewer3D, Rodada 1: teste e revisão das correções (commit e2297b2)

Testador: Agente 3. Alvo: cópia de `tools/viewer3d` do commit e2297b2. Conferi com `diff -r` contra `git archive e2297b2` e ela é idêntica. O "antes" é o commit 94ae0e8 (`r1test/before`).
Builds: `r1test/build` (RelWithDebInfo), `r1test/build-asan` (Debug, ASan+UBSan) e `r1test/build-before`. Os três compilaram com **0 avisos** (`-Wall -Wextra`).
Ambiente: Mesa llvmpipe (LLVM 20.1.2, GL 4.5 core), Xvfb `:97`, pista `build/uiview/tracks/synthetic__dr2hook_ring`. O repositório não foi tocado: todos os `--out` apontam para `r1test/out`, e rodei o `edit_roundtrip.py` com `PYTHONDONTWRITEBYTECODE=1 -B`.
Rótulos: **FACT** (lido no código), **CONFIRMED BY TEST**, **INFERENCE**, **HYPOTHESIS**.

## 1. Veredito por item do relatório do avaliador

Rodei cada cenário nos dois builds. A coluna "antes" mostra que o defeito se reproduz aqui no 94ae0e8. Scripts: `t_p0.py`, `t_p12.py`, logs em `t_p0_{new,old}.log` e `t_p12_{new,old}.log`.

| ID | Antes (94ae0e8) | Depois (e2297b2) | Veredito |
|---|---|---|---|
| P0-1 gravação falha | `/proc/nope/x.json`, `afile/x.json` e `--out` que é pasta: **MORREU rc=1** | Vivo, com `NÃO GRAVOU: filesystem error: …` no título. Consertei a pasta no meio da sessão, dei Ctrl+S de novo e gravou 1 edição. `--out` que é pasta é recusado na partida (rc=1) | **Corrigido** (CONFIRMED BY TEST). Sobra o caso do §2 R-6: com `--out` impossível de consertar, não há como gravar em outro lugar |
| P0-2 Tab para rota ruim | `inst1_trunc`, `route1_terrain_missing`, `route1_noterrain`, `inst1_missing`: **MORREU rc=1** | Os 4 casos ficam vivos na `route_0` com `hist 2/2` e `não abriu route_1: <motivo>`. Shift+Tab idem. Ctrl+Z, Ctrl+Y e Ctrl+S gravam 1 edição | **Corrigido** (CONFIRMED BY TEST). Há uma fragilidade latente na transação (R-5) |
| P0-3 sobrescrever edits.json antigo | Sem cópia (`['p03.json']`) | 1ª sessão: `.1.bak` == original. 2ª sessão: `.2.bak` | **Corrigido** (CONFIRMED BY TEST). Efeitos colaterais em R-7 |
| P0-4 nome de tipo curto | `shortname`/`emptyname`: **MORREU** (`substr`) | Vivos. O título mostra `e \| kind e` e ` \| kind ?`. E e Ctrl+S funcionam | **Corrigido** (CONFIRMED BY TEST) |
| P1-1 realce | 0 px diferentes com e sem seleção | **450 px** (o mesmo número que o avaliador mediu com F2) | **Corrigido** (CONFIRMED BY TEST) |
| P1-2 Esc/fechar sai sem avisar | 2º Esc: **saiu** | 2º Esc avisa e fica vivo. Esc depois de 3,3 s avisa de novo. Esc+Esc rápido sai (rc 0). Editar e desfazer: 1 Esc sai. Gravar: sai direto. `WM_DELETE_WINDOW` (`wmdelete.c`): 1º avisa, 2º sai. SIGTERM: idem | **Corrigido** (CONFIRMED BY TEST). Ver R-1, R-2 e R-8 |
| P1-3 Shift no Mover | Shift antes do clique: posição igual (fez pan) | Shift antes do clique: y 101,72 → 108,44. Subir com Shift e soltar no meio: 108,35 mantido, só x/z mudam. Shift fora de objeto continua pan | **Corrigido** (CONFIRMED BY TEST) |
| P1-4 erro de GL fecha | Com `glerr.so` (LD_PRELOAD que injeta GL_INVALID_ENUM a cada `glClear`): **rc=1** `erro de GL 0x0500` | 20 avisos, depois "mais erros…", e rc=0 com 60 quadros | **Corrigido** (CONFIRMED BY TEST). O título não mostra nada (R-9) |
| P2-1 giro 360° | E×24 + Shift+E×4: 2 edições espúrias | **0 edições**, sem "não gravado" | **Corrigido** (CONFIRMED BY TEST) |
| P2-2 seleção em oculto | Ctrl+Y mantém a seleção, E×3 vira `hist 4/4`, R ressuscita a cópia | Ctrl+Y tira a seleção, E×3 não faz nada (`hist 1/1`). Desfazer o Duplicar tira a seleção e R não faz nada. Gravado: só `(10, deleted)` | **Corrigido** (CONFIRMED BY TEST) |
| P2-3 soltar Ctrl antes do S | 428 085 px mudam | **0 px**. S sozinho continua andando (452 793 px) | **Corrigido** (CONFIRMED BY TEST), mas criou a regressão R-3 |
| P2-4 `--out` validado só no Ctrl+S | `--out` pasta/impossível: rc=0 na partida | Pasta: rc=1. `/proc/nope/x.json` e `afile/x.json` (pai é arquivo): **rc=0** na partida | **Parcialmente** (CONFIRMED BY TEST) |
| P2-5 `--camera` sem validação | NaN/inf/dist 0/dist −100/pitch 10 aceitos | NaN, inf, dist ≤ 0 e \|pitch\| > 1,5 são recusados (rc=1). **Ainda aceitos:** dist 1 (tela de 1 cor), dist 1e30 (1 cor), alvo x 1e30 (1 cor), dist 1e6 (3 cores), pitch −1,4 (abaixo de kPitchMin = −0,2). `--vsync abc` e `--frames 99999999999999999999` continuam rc=0 | **Parcialmente** (CONFIRMED BY TEST) |

Os itens P3 não fazem parte do commit. Só a parte de P3-3 (gravação atômica) entrou, e eu a testei (R-7).

## 2. Regressões e problemas encontrados

### R-1 (P2, UX). A mensagem de status nunca expira e chega a contradizer o estado
- **Evidência (CONFIRMED BY TEST, `t_p12_new.log`).** Testei Esc com pendências, esperei 3,5 s, e o título continuou com "…Esc ou fechar de novo em 3 s sai sem gravar", que já não vale. Na parte P2-2, o mesmo aviso continuava no título várias ações depois. Em `t_r1b.py`, depois de um Ctrl+S, o título mostrou ao mesmo tempo `não gravado | … | gravado …r1b.json (1 edições)`. Depois de um Tab que falhou, "não abriu route_1" fica até o fim da sessão.
- **Onde.** `track_view.cpp:329-332` (`set_status` não guarda hora) e `:359-360` (o título anexa `status_` sempre).
- **Sugestão.** Guardar `status_t_` (SDL_GetTicks) e mostrar o status só por ~5 s. Ou limpá-lo na próxima edição ou gravação. O aviso de saída deveria sumir junto com o `quit_until`.

### R-2 (P2, UX). O título é truncado em 511 bytes e perde o status e o FPS
- **Evidência (CONFIRMED BY TEST, `t_extra.py`).** Testei com um `--out` de caminho longo (~190 caracteres, possível no Windows com usuário ou pasta longos), uma seleção e o primeiro Ctrl+S com `.bak`. O título ficou com **511 bytes**, sem o "fps", e cortou no meio de "anterior em /tm". O corte pode cair no meio de um caractere UTF-8 multibyte (INFERENCE: `snprintf` corta por byte).
- **Onde.** `main.cpp` (`char title[512]` no laço de quadros). As mensagens de gravação repetem o caminho completo duas vezes.
- **Sugestão.** Usar `std::string` em vez do buffer fixo. Nas mensagens, mostrar só o nome do arquivo (`path.filename()`) e deixar o caminho completo no stderr.

### R-3 (P3, regressão nova). Segurar W e tocar Ctrl para de andar até soltar W
- **Evidência (CONFIRMED BY TEST, `t_extra.py`).** Segurei W por 0,6 s, apertei Ctrl por 0,6 s e soltei o Ctrl com W ainda apertado. **Antes:** a câmera volta a andar (557 669 px). **Depois:** 0 px. Causa (FACT, `main.cpp`, `case SDL_EVENT_KEY_DOWN`): o autorepeat de W chega com Ctrl no `mod`, marca `chord[W] = true`, e W só volta a valer quando é solto.
- **Sugestão.** Ignorar `event.key.repeat` ao marcar `chord`, e marcar só a tecla que foi apertada junto com o Ctrl (não os repeats de teclas já seguras).

### R-4 (P3). Um SIGTERM sozinho nunca fecha o editor com pendências
- **Evidência (CONFIRMED BY TEST).** Mandei `kill` (SIGTERM) duas vezes com mais de 3 s entre elas no viewer ASan. Ele só avisou nas duas e continuou vivo. Fechou com 2 sinais em menos de 3 s. O SDL traduz SIGTERM/SIGINT em `SDL_EVENT_QUIT`, que agora passa pelo `confirm_quit` (`main.cpp`, `case SDL_EVENT_QUIT`).
- **Impacto (INFERENCE).** `timeout`, `systemctl` ou o logout mandam um TERM só e depois matam com KILL, e as edições se perdem do mesmo jeito. Ctrl+C no terminal também passa a exigir dois toques.
- **Sugestão.** Num sinal (não num clique), gravar `<out>.autosave.json` e sair. Ou ao menos gravar o autosave antes de ignorar.

### R-5 (P3, latente). `load_route` não é totalmente transacional, e o `catch` pode desreferenciar `end()`
- **FACT (`track_view.cpp:73-75`).** Quando o terreno muda, `terrain_`, `terrain_file_` e **`route_`** trocam antes de `textures_.for_material`, antes de `RouteLines` (`:92`) e antes de `regroup` (`:105`). Se algo lançar depois da linha 73, o estado fica misto: terreno e nome da rota novos, instâncias e `route_index_` antigos. Aí `save()` grava as edições da rota antiga com o **nome da rota nova** (`routes.push_back({route_, &inst_})`).
- **FACT (`:90-97` e `:123`).** Se lançar depois da linha 90, `route_index_` já é o novo e o `catch` faz `saved_.find(route_index_)`, que dá `end()` (rota nova lida do disco, ou a entrada já apagada em `:97`). Desreferenciar isso é UB.
- **INFERENCE.** Hoje só `std::bad_alloc` lança nesses trechos (procurei `throw` em `render/` e `RouteLines`: não há nenhum), então a probabilidade é baixa.
- **Sugestão.** Tirar o `route_ = &route` de `:75` (é redundante com o de `:91`). Construir `lines` num `unique_ptr` local antes de mexer em qualquer membro. Fazer todas as atribuições num bloco final `noexcept`.

### R-6 (P2). Com `--out` inválido, o editor sobrevive mas não há como gravar
- **Evidência (CONFIRMED BY TEST, `t_stress_badout.log`).** 15 Ctrl+S deram 15 `NÃO GRAVOU`. A única saída é o aviso e o Esc duplo, que perde tudo. A validação na partida (P2-4) só recusa pasta, não `/proc/nope/x.json` nem um pai que é arquivo.
- **Sugestão.** Na partida, fazer `create_directories(parent)` e abrir `<out>.tmp` para teste (e apagá-lo), saindo com rc=1 se falhar. Na falha em tempo de execução, cair para um caminho reserva (ex.: `saves/<id>.<timestamp>.edits.json` ou a pasta temporária) e dizer no título onde gravou.

### R-7 (P3). Efeitos colaterais da gravação atômica e do .bak
Testei com `wt_edge.cpp`, ligado à `libdr2edit`. Todos os casos abaixo são CONFIRMED BY TEST.
- **Symlink.** Se `--out` é um symlink, o `rename` troca o link por um arquivo comum e o alvo continua com o conteúdo antigo. Antes, o `ofstream` escrevia através do link.
- **Permissões.** Um arquivo 0600 vira 0644 depois de gravar.
- **Falha depois do backup.** Se o backup sai mas a gravação falha, `wrote_` continua `false` e cada nova tentativa cria outro `.bak` igual (`.1.bak`, `.2.bak`, `.3.bak`).
- **999 cópias.** Com 999 `.bak`, `backup_existing` lança, o `save()` inteiro falha ("já há 999 cópias") e **não há como gravar nessa sessão**. É preciso uma sessão com gravação por dia durante ~3 anos no caminho padrão. Toda sessão que grava deixa um `.bak`, então a pasta `saves/` acumula lixo.
- **`.tmp` alheio.** Na falha, `remove(tmp)` apaga uma pasta vazia chamada `<out>.tmp` que não é do editor (trivial).
- **Sem fsync** antes do rename (INFERENCE). Numa queda de energia, alguns sistemas de arquivos podem deixar o arquivo novo vazio. O tmp fica na mesma pasta, então não há EXDEV: o rename é atômico no mesmo sistema de arquivos (FACT, `edits_json.cpp`).
- **Sugestão.** Se `backup_existing` falhar, só avisar, sem bloquear a gravação. Usar um nome com timestamp ou limitar a N cópias, apagando as mais velhas. Ligar `wrote_` (ou um `backed_up_`) logo depois do backup. Usar `fs::canonical` para gravar através de symlinks.

### R-8 (P3, UX). Esc é ao mesmo tempo "cancelar" e "sair"
- **Evidência (CONFIRMED BY TEST, sem querer).** No meu primeiro script de estresse, Ctrl+Z desfez o Duplicar. O `after_history` tirou a seleção e não havia pendências, então o Esc seguinte (que eu mandei para desselecionar) **fechou o programa**. Nada se perdeu, mas o usuário perde a sessão: câmera, rota aberta e histórico de desfazer.
- **Sugestão.** Sair só com Esc duplo, ou com Ctrl+Q, mesmo sem pendências.

### R-9 (P3). Os erros de GL só aparecem no stderr
Depois de 20 erros (`gl.cpp`, `warn`), o programa não mostra mais nada, nem no título (FACT). O contador `static int` estoura depois de ~2^31 erros, o que é UB teórico. Sugestão: um indicador "erro de GL" no título.

### R-10 (P3). Mensagem do `--camera` imprecisa
O texto diz "a câmera limita a ~1,45", mas `kPitchMax = 1.5f` (FACT, `camera.hpp:16`). A validação usa `|pitch| ≤ 1,5` em vez de `[kPitchMin, kPitchMax]` e não limita `dist` a `[kDistMin, kDistMax]` (veja P2-5).

### R-11 (P3, teste, não é do commit). `edit_roundtrip.py` erra o índice quando há cópia antes de uma edição
- **Evidência (CONFIRMED BY TEST).** Um edits.json com uma cópia da barreira 7 e uma edição na 10 falha com `AssertionError (10, …)`. O `edit_ens` insere a cópia logo depois da origem, e o script só desconta os apagados.
- Com a correção `pos += #cópias com src < index` (em `r1test/roundtrip_fix.py`), deu `OK 4 edições conferidas`. O viewer está certo, o script de teste não.
- **Onde.** `tools/viewer3d/tests/edit_roundtrip.py:59`.

## 3. Funcionalidades antigas (regressão)

Rodei o `t_func.py` (build normal) e o `t_r1b.py`. Tudo CONFIRMED BY TEST:
- **Testes.** `camera_test OK`, `edit_test OK (118 verificações)` e `core_tests --track … OK (61 verificações)`, no build normal e no ASan.
- **Camadas.** F1, F2, F3, G e I mudam a imagem (826 495, 89 229, 2 522, 1 943 e 1 295 px) e voltam a 0 px ao desligar. F4 dá 0 px nesta câmera, como esperado: não há terreno distante perto.
- **Distância de desenho.** `[`×3 → 400 m (inst 504), `]`×5 → 900 m (inst 996), `[`×2 → 700 m.
- **Navegação.** Orbitar, roda, W e pan com o botão direito mudam a imagem.
- **Edição.** Testei selecionar, mover no chão (40,39 → 44,42), girar por arraste, E, Q, Shift+E, R (volta ao arquivo e some o "não gravado"), Ctrl+Z, Ctrl+Shift+Z, Ctrl+D (só em `e:`; na árvore não faz nada), Delete e E na árvore (`t:`). Todos funcionam.
- **Rotas.** Testei Tab e Shift+Tab com edições nas duas rotas. O histórico se preserva (`hist 2/2` ao voltar), e o Ctrl+S grava as duas rotas (`[('route_0',10,False), ('route_1',10,True)]`).
- **`edit_roundtrip.py`.** `p02_*.json`, `r1b.json` e os 3 arquivos do avaliador dão OK. `func_NEW.json` deu OK com o script corrigido (R-11).

## 4. Estabilidade (ASan + UBSan + LSan)

Usei o `t_stress.py` no build ASan com `--vsync 0`. O container reiniciou no meio da primeira tentativa (150 iterações), e eu repeti numa versão mais curta. Cada iteração tem ~31 operações: clique, mover e desfazer, girar e desfazer, E×3, Q×2, Shift+E, Ctrl+Z×3, Ctrl+Y×2, Ctrl+Shift+Z, R, Ctrl+D (que fica) ou Delete+Ctrl+Z, clique no vazio, 2 teclas de camada ou `[ ]`, Tab a cada 2 e Ctrl+S.

| Sessão | Iterações / ~operações | RSS (kB) nas amostras | Erros ASan/UBSan/LSan | Resultado |
|---|---|---|---|---|
| pista boa | 40 / 1 240 | 525 224 → 555 764 → 544 292 → 551 392 → 547 060 | 0 / 0 / 0 | `hist 148/148`, 40 gravações, rc 0 |
| `--out /proc/nope/x.json` | 15 / 465 | 545 672 → 557 668 → 546 376 | 0 / 0 / 0 | 15 `NÃO GRAVOU`, vivo, aviso de saída funcionou, rc 0 |
| `bad/inst1_trunc` (Tab quebrado) | 15 / 465 | 545 652 → 562 452 → 548 452 | 0 / 0 / 0 | 8 `não abriu route_1`, vivo na route_0, 15 gravações, rc 0 |

O RSS ficou estável, oscilando ±2 % sem tendência. A sessão interrompida pelo reinício também não teve nenhum erro de sanitizer até a iteração 20.

A confirmação de saída funcionou em todas as variantes que testei: Esc, `WM_DELETE_WINDOW` (`r1test/wmdelete.c`, porque o xdotool 2016 não tem `windowquit` e `windowclose` faz XDestroyWindow) e SIGTERM (§1 P1-2, R-4).

## 5. Desempenho antes × depois

Medi com `--vsync 0 --frames 300` e 3 repetições, alternando os builds.

| Câmera | Antes (94ae0e8) FPS | Depois (e2297b2) FPS |
|---|---|---|
| padrão (sem `--camera`) | 24,7 (25,8 / 24,1 / 24,3) | 24,8 (25,5 / 24,4 / 24,6) |
| `0.8,1.25,60,45,101.5,-262` | 67,3 (66,9 / 66,4 / 68,7) | 67,1 (66,2 / 66,3 / 68,8) |
| `0.8,0.55,90,45,101.5,-262` | 47,1 (46,9 / 46,9 / 47,6) | 47,0 (47,5 / 46,7 / 46,8) |

- **Interativo** (`t_perfedits.py`, FPS do título, 6 amostras). Os dois builds ficam iguais (62–72 fps) com e sem seleção, e também com **300 cópias** (`hist 300/300`, 1 311 instâncias). O realce com `GL_LEQUAL` não custa nada mensurável.
- **Custo do `unsaved()`** (`bench_unsaved.cpp`, uma chamada de `edits_json` + comparação, -O2). Ele roda a cada 0,5 s no título e também em cada `confirm_quit`:

| Instâncias | 0 edições | 10 | 300 | 3 000 |
|---|---|---|---|---|
| 1 000 | 0,009 ms | 0,04 ms | 1,0 ms | – |
| 20 000 | 0,17 ms | 0,21 ms | 1,2 ms | 10,6 ms |
| 100 000 | 0,9 ms | 0,9 ms | 2,0 ms | 11,9 ms |

  INFERENCE: até centenas de edições, o custo é desprezível. Com milhares, dá um soluço de ~11 ms a cada 0,5 s, quase um quadro inteiro a 60 Hz (P3). Sugestão: só recalcular quando `edit_rev_` mudar.
- **Tempo de carga** (`--frames 1`, 5 execuções): antes 0,186–0,209 s, depois 0,184–0,204 s. Terreno: leitura 0,007 s e envio 0,008–0,010 s nos dois. Sem diferença.

## 6. UX das mensagens no título

- **Claras:** "NÃO GRAVOU: <erro>", "não abriu route_1: DR2I: n maior que o arquivo", "gravado X (n edições); anterior em X.1.bak" e o marcador "não gravado". O aviso de saída diz o que fazer.
- **Confusas:**
  - O status não expira (R-1): o aviso de "3 s" e um "gravado" velho aparecem ao lado de "não gravado".
  - O título passa de 511 bytes com caminhos longos e corta a mensagem (R-2).
  - Os erros do `filesystem` saem em inglês, misturados no português ("filesystem error: cannot create directories…").
  - O aviso de que o arquivo já existe ao abrir ("o primeiro Ctrl+S guarda uma cópia…") só vai para o stderr.
- **README.** Ele diz "Se o arquivo já existia **ao abrir**", mas o código copia o que existir no primeiro Ctrl+S (FACT). A diferença é pequena.

## 7. O que não deu para testar

- **Exceção no meio de `load_route`** (R-5). Só `bad_alloc` dispara esse caminho, e não induzi um. Fica como FACT/INFERENCE do código.
- **Comportamento no Windows/NTFS** do `rename` sobre um arquivo aberto por outro processo, e perda de energia sem fsync: não há Windows aqui.
- **GPU real, perda de contexto, pista grande (Montalegre).** O custo de `unsaved()` com instâncias reais saiu do microbenchmark sintético, não de uma pista real.
- **Fechar a janela num gerenciador de janelas de verdade.** Simulei com `WM_DELETE_WINDOW` enviado pelo `wmdelete.c`.
- **Sessão de estresse longa.** A versão de 150 iterações foi interrompida pelo reinício do container. Rodei 40 + 15 + 15 iterações (~2 170 operações) em vez de "centenas de iterações".

## Arquivos (em `scratchpad/r1test/`)

- **Scripts:** `drv.py`, `t_p0.py`, `t_p12.py`, `t_func.py`, `t_r1b.py`, `t_stress.py`, `stress_short.sh`, `t_perfedits.py`, `t_extra.py`, `perf.sh`.
- **Ferramentas C/C++:** `glerr.c`/`glerr.so` (injeção de erro de GL), `wmdelete.c`, `wt_edge.cpp`, `bench_unsaved.cpp`, `roundtrip_fix.py`.
- **Logs:** `t_*_{new,old}.log`, `t_stress_*.log`, `perf.log` e `out/`.
