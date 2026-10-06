# Viewer3D, Rodada 1 (fundamentos): avaliação

Avaliador: Agente 1. Alvo: `tools/viewer3d` (HEAD do repositório, árvore limpa: `git status` vazio antes e depois).
Ambiente: Mesa llvmpipe sob Xvfb `:99`, SDL 3 em `/opt/sdl3`, pista `build/uiview/tracks/synthetic__dr2hook_ring` (rotas `route_0` e `route_1`, as duas com `terrain_0.bin`).
Todos os scripts estão em `SP=/tmp/claude-0/-home-user-DR2Hook/04e0f535-6bba-572c-b584-3c3caa091cdf/scratchpad/r1`. O driver interativo é `drv.py` (xdotool). Ele inclui `ckey()`, que aperta Ctrl, depois a tecla, e solta Ctrl por último.

Rótulos: **FACT** (lido no código), **CONFIRMED BY TEST** (reproduzido aqui), **INFERENCE** (deduzido do código, sem teste), **HYPOTHESIS** (não verificado).

> **Aviso sobre o ambiente.** Num teste de diagnóstico (`t_dbg2.py`, primeira versão, sem `--out`), um Ctrl+S gravou o caminho padrão `build/uiview/saves/synthetic__dr2hook_ring.edits.json` com 0 edições (134 bytes, 2026-10-06 00:05). O arquivo já existia: o mtime da pasta, 23:49:57, é anterior. Não sei o que ele continha antes. Está em `build/`, que o git ignora, e o `git status` continua limpo. Esse acidente também é a evidência do problema P0-3. Além disso, rodar `edit_roundtrip.py` recompilou `tools/synthtrack/__pycache__/build.cpython-311.pyc`, um bytecode que o git ignora.

---

## Resumo

| ID | Sev. | Problema |
|---|---|---|
| P0-1 | P0 | Falha ao gravar (Ctrl+S com `--out` ruim) fecha o programa e perde todas as edições |
| P0-2 | P0 | Falha ao carregar outra rota (Tab) fecha o programa e perde as edições de todas as rotas |
| P0-3 | P0 | Ctrl+S sobrescreve sem aviso o `edits.json` de uma sessão anterior (o viewer não o relê) |
| P0-4 | P0 | Tipo com nome de menos de 2 caracteres: selecionar fecha o programa (`substr`) |
| P1-1 | P1 | O realce do objeto selecionado não aparece (depth test GL_LESS) |
| P1-2 | P1 | Esc com seleção tira a seleção; o segundo Esc sai sem gravar e sem avisar |
| P1-3 | P1 | Shift+arrastar com Mover faz pan, não sobe; soltar o Shift no meio do arraste joga a altura fora |
| P1-4 | P1 | Qualquer erro de GL num quadro fecha o programa e perde as edições |
| P2-1 | P2 | Giro de 360° (24×E, 4×Shift+E) não volta exato e gera edição espúria no edits.json |
| P2-2 | P2 | Seleção continua num objeto oculto (refazer Apagar, desfazer Duplicar); E/R agem nele; R "ressuscita" uma cópia desfeita |
| P2-3 | P2 | Soltar Ctrl antes de S/D/A anda com a câmera (Ctrl+S/Ctrl+D movem a vista) |
| P2-4 | P2 | `--out` só é validado no primeiro Ctrl+S, não na partida |
| P2-5 | P2 | `--camera` aceita NaN/inf/dist ≤ 0/pitch fora da faixa: tela vazia, sem erro |
| P3-1 | P3 | Picking ignora o terreno e usa a AABB do tipo (seleciona através de morros e de vãos) |
| P3-2 | P3 | `count()` do track.json: double → size_t fora da faixa é UB |
| P3-3 | P3 | Gravação não atômica do edits.json (trunc + write) |
| P3-4 | P3 | Arrastar perto do horizonte manda o objeto para longe (~100 m com 40 px) |
| P3-5 | P3 | Tipos sem malha (`e:synth_spawn_marker`) não se veem e não se selecionam (logo não se editam) |
| P3-6 | P3 | Troca para rota com outro terreno relê o arquivo do disco a cada Tab |

---

## P0: crash ou perda de dados

### P0-1: falha ao gravar fecha o programa e perde as edições
- **Problema.** `TrackView::save()` não trata exceções. Se `write_text` falha (pasta impossível, destino é uma pasta, componente do caminho é um arquivo), a exceção sai de `handle_event`, sai de `run` e cai no `catch` de `main`. O programa termina com código 1 e as edições em memória se perdem.
- **Evidência (CONFIRMED BY TEST).** `python3 $SP/t_fail.py <pista> <out> save` seleciona a barreira 10, aperta E duas vezes e depois Ctrl+S:
  - `--out /proc/nope/x.json` → `SAIU rc=1`, `viewer3d: filesystem error: cannot create directories: No such file or directory [/proc/nope]`
  - `--out $SP/outdir_is_dir.json` (é uma pasta) → `SAIU rc=1`, `viewer3d: não gravou .../outdir_is_dir.json`
  - `--out $SP/afile/x.json` (`afile` é um arquivo) → `SAIU rc=1`, `filesystem error: cannot create directories: Not a directory`
- **Onde.** `src/app/track_view.cpp:130` (`case SDLK_S: save()`), `:270` (`edit::write_text`); `src/edit/edits_json.cpp:65-66`; `src/app/main.cpp:415`.
- **Causa (FACT).** Não há `try/catch` entre a tecla e `main`.
- **Impacto.** Perda total da sessão justamente quando o usuário tenta salvar.
- **Solução mínima.** Em `TrackView::save()`, envolver `edits_json` + `write_text` em `try { … } catch (const std::exception& e) { fprintf(stderr, "viewer3d: não gravou: %s\n", e.what()); return 0; }`. Mostrar "não gravou" no título por alguns segundos (ex.: um campo `status_` em `title()`).
- **Teste.** Repetir os três casos de `t_fail.py`: o processo continua vivo (`vivo`). Depois, um Ctrl+S com `--out` válido na mesma sessão grava as edições.

### P0-2: falha ao carregar outra rota fecha o programa e perde as edições
- **Problema.** Em `switch_route`, as edições da rota atual vão para `saved_` (move) e depois roda `load_route(next)`. Se ele lança (DR2I truncado ou ausente, rota sem `terrain`, terreno ausente ou truncado), o programa sai com código 1 e perde as edições de todas as rotas. Mesmo que a exceção fosse capturada, o estado já estaria pela metade: `route_index_` e `route_` trocados, `terrain_` possivelmente zerado em `terrain_.reset()`, `inst_` movido.
- **Evidência (CONFIRMED BY TEST).** `python3 $SP/t_fail.py $SP/bad/<caso> x.json tab` (seleciona, E×2, Tab):
  - `inst1_trunc` → `SAIU rc=1`, `viewer3d: DR2I: n maior que o arquivo`
  - `route1_terrain_missing` → `SAIU rc=1`, `viewer3d: não abriu .../terrain_9.bin`
  - `route1_noterrain` → `SAIU rc=1`, `viewer3d: track.json: a rota route_1 não tem terreno`
  - Com `--frames 5` essas pistas abrem normalmente (`rc=0`), porque só a rota 0 é lida na partida. O defeito só aparece no Tab.
- **Onde.** `src/app/track_view.cpp:107` (`saved_[route_index_] = …`), `:46-100` (`load_route` muda membros antes de ler tudo), `:62` (`terrain_.reset()`).
- **Causa (FACT).** `load_route` não é transacional, e não há `catch` no Tab.
- **Solução mínima (incremental).**
  1. Em `load_route`, ler e validar tudo em variáveis locais primeiro: bytes e malhas do terreno se mudou, e `Instances` novo se não está em `saved_`. Só depois atribuir os membros (`route_index_`, `route_`, `terrain_`, `lines_`, `inst_`, `hist_`).
  2. Em `switch_route`: `try { load_route(next) } catch (...) { /* devolve inst_/hist_ de saved_[route_index_], loga, fica na rota atual */ }`.
- **Teste.** Com as pistas `bad/inst1_trunc`, `bad/route1_terrain_missing` e `bad/route1_noterrain` (geradas por `$SP/mkbad.sh`): seleciona, E, Tab → continua vivo na `route_0`, com `hist 2/2`, e o Ctrl+S grava a edição.

### P0-3: Ctrl+S sobrescreve sem aviso o edits.json de uma sessão anterior
- **Problema.** O caminho padrão é fixo por pista (`build/uiview/saves/<id>.edits.json`). O viewer não lê esse arquivo ao abrir, e `write_text` abre com `trunc`. Numa sessão nova, qualquer Ctrl+S, mesmo sem edições, apaga as edições gravadas antes.
- **Evidência (CONFIRMED BY TEST, sem querer).** Sessão sem `--out` e Ctrl+S sem edições → `build/uiview/saves/synthetic__dr2hook_ring.edits.json` virou 134 bytes com `"edits":[]`. O arquivo existia antes (veja o aviso no topo).
- **Onde.** `src/app/track_view.cpp:32`; `src/edit/edits_json.cpp:66`.
- **Impacto.** Perde-se o trabalho de uma sessão anterior ainda não aplicado com `track.edit`.
- **Solução mínima.** Antes de sobrescrever, renomear o existente para `<out>.bak`. Ou, no primeiro save da sessão, se o arquivo existe e tem conteúdo diferente, gravar `<id>.<timestamp>.edits.json` e avisar no stderr. Não carregar as edições antigas agora (isso muda a arquitetura); só não destruir.
- **Teste.** Criar um `edits.json` com 1 edição, abrir o viewer sem editar, Ctrl+S, e verificar que o `.bak` tem a edição original.

### P0-4: tipo com nome curto fecha o programa ao selecionar
- **Problema.** `title()` usa `name.substr(2)`. Com um nome de tipo de 0 ou 1 caractere no `type_order`, `std::out_of_range` sai pelo laço e o programa morre no primeiro título depois do clique.
- **Evidência (CONFIRMED BY TEST).** `t_fail.py $SP/bad/shortname x.json none` → `viewer3d: basic_string::substr: __pos (which is 2) > this->size() (which is 1)`, `rc=1`. `bad/emptyname` → `… size() (which is 0)`.
- **Onde.** `src/app/track_view.cpp:286`. `edits_json.cpp` já se protege (`name.size() > 2 ? … : ""`).
- **Causa (FACT).** Falta checar o tamanho. A entrada é malformada, mas é um crash com perda de edições.
- **Solução mínima.** `name.size() > 2 ? name.substr(2) : name`, e `name.empty() ? '?' : name[0]`. Opcional: validar em `read_track` que o nome tem a forma `k:...`.
- **Teste.** `bad/shortname` e `bad/emptyname`: clicar na barreira não fecha o programa.

---

## P1: bug grave ou funcionalidade quebrada

### P1-1: o realce do selecionado não aparece
- **Problema.** `draw_one` redesenha o selecionado com `uHi = 1`, mas o objeto já foi desenhado no passo instanciado com a mesma profundidade. Com o `glDepthFunc` padrão (`GL_LESS`) e sem polygon offset, todo fragmento do realce falha no teste de profundidade. O usuário só vê a seleção no título.
- **Evidência (CONFIRMED BY TEST).** `python3 $SP/t_hl.py` faz capturas com `import -window` e compara:
  - sem seleção × com a barreira 10 selecionada: **0 pixels diferentes**
  - com F2 (objetos desligados): sem seleção × com seleção: **450 pixels** na caixa x 492–533, y 376–416, cor laranja (`[207 151 86]`). Ou seja, o realce só aparece quando o passo instanciado não ocupa o z-buffer.
- **Onde.** `src/app/track_view.cpp:185`; `src/render/instances.cpp:179` (`draw_one`). Não há `glDepthFunc` em lugar nenhum (FACT: `grep glDepthFunc` vazio). `main.cpp:348` só liga `GL_DEPTH_TEST`.
- **Solução mínima.** Em `InstanceRenderer::draw_one`: `glDepthFunc(GL_LEQUAL)` antes de `draw_parts` e `glDepthFunc(GL_LESS)` depois. HYPOTHESIS: o caminho instanciado (atributo por instância) e o constante (`glVertexAttrib3f`) dão a mesma profundidade bit a bit. Se não derem no hardware do dono, a alternativa é excluir `sel_` do buffer instanciado em `cull` (passar o índice selecionado e entrar na chave).
- **Teste.** `t_hl.py`: `h_sel.png` × `h0.png` precisa ter algumas centenas de pixels diferentes na caixa da barreira.

### P1-2: o segundo Esc sai sem gravar e sem avisar
- **Problema.** Esc com seleção tira a seleção. Esc sem seleção fecha o programa na hora, mesmo com edições não gravadas. Fechar a janela faz o mesmo. Esc é a tecla natural para "cancelar", então apertar duas vezes é comum.
- **Evidência (CONFIRMED BY TEST).** `t_sel.py`, parte 4: com `hist 6/6` não gravado, `Escape`, `Escape` → `depois de Esc Esc: saiu rc=0`. Nada foi gravado. No meu próprio teste de picking, um Esc depois de um clique no vazio fechou o programa no meio da varredura.
- **Onde.** `src/app/main.cpp:275-279`, `:272` (`SDL_EVENT_QUIT`).
- **Solução mínima.** Guardar `saved_rev_` (o `edit_rev_` ou a soma das posições do histórico no último save). Ao sair com edições pendentes, a primeira tentativa só mostra "edições não gravadas: Esc de novo para sair, Ctrl+S para gravar" no título, e a segunda sai. Alternativa mais simples: ao sair com pendências, gravar `<out>.autosave.json`.
- **Teste.** Selecionar, E, Esc, Esc → continua vivo, com o aviso no título. O terceiro Esc sai. Sem edições, Esc sai direto como hoje.

### P1-3: Shift+arrastar com Mover faz pan, e soltar o Shift no meio joga a altura fora
- **Problema.** (a) O README diz "Mover (arrastar no chão; Shift sobe e desce)". Mas com Shift apertado no clique, `drag.pan = shift` e o arraste vira pan da câmera. A altura só muda se o Shift for apertado depois do botão. (b) Se o Shift é solto durante o arraste, o ramo sem Shift faz `m[10] = drag_.base[10]`: a altura volta à original e o objeto salta em x/z.
- **Evidência (CONFIRMED BY TEST).**
  - (a) `t_asan.py`: com a ferramenta 2, `drag(..., mods="shift")` → posição inalterada (`44.42 101.72 -253.97` antes e depois), sem passo no histórico, e a câmera se moveu (os cliques seguintes erraram). `t_asan2.py` com Shift depois do clique → `42.73 105.32 -253.39` (subiu).
  - (b) `t_sel.py`, parte 3: subindo com Shift `48.38 109.07 -252.99`; soltou o Shift e continuou → `45.21 101.87 -257.22`. A altura de 109.07 se perdeu.
- **Onde.** `src/app/main.cpp:289-290`; `src/app/track_view.cpp:236` e `:242`.
- **Solução mínima.** (a) Com a ferramenta Mover ou Girar ativa e o clique sobre um objeto, tentar `begin_edit` mesmo com Shift (pan só se não pegou objeto). (b) Na troca de modo durante o arraste, rebasear: guardar a altura e o ponto do chão atuais como nova base (`drag_.base[10] = m[10]; drag_.sy = y; ground(...)` para refazer `start`) em vez de usar a base do clique.
- **Teste.** Shift apertado antes do clique sobe o objeto. Subir com Shift, soltar e continuar arrastando mantém a altura nova.

### P1-4: qualquer erro de GL num quadro fecha o programa e perde as edições
- **Problema.** `dr2::gl::check("quadro")` lança a cada quadro se `glGetError() != 0`, e a exceção sai do laço.
- **Evidência.** FACT: `src/app/main.cpp:378` e `src/render/gl.cpp` (`check` lança). Nenhum erro de GL apareceu em nenhuma sessão aqui (CONFIRMED BY TEST: ninguém saiu com "erro de GL"). INFERENCE: um `GL_OUT_OF_MEMORY` numa textura carregada sob demanda (`TextureCache::load`, chamada dentro de `draw`) numa pista grande, ou um erro de driver, mata a sessão.
- **Solução mínima.** No laço, trocar o `throw` por log com limitação (ex.: imprimir o erro e o código uma vez por segundo). Manter `check` com throw só na criação da cena. Opcional: verificar `glGetError` logo após `glTexImage2D` em `TextureCache::load` e marcar a textura como falha.
- **Teste.** Injetar um erro (ex.: chamar `glBindTexture(GL_TEXTURE_2D, 99999)` atrás de uma variável de ambiente de depuração) e ver que o programa continua.

---

## P2: bug menor, robustez ou UX

### P2-1: giro de 360° não volta exato e gera edição espúria
- **Evidência (CONFIRMED BY TEST).**
  - `$SP/rot_test.cpp` (liga `history.cpp`): 24×15° → `changed=1 max|m-m0|=5.96e-08`. 4×90° → `changed=1`. E seguido de Q → `changed=0`.
  - Não há deriva: com 24 000 passos, o erro máximo é o mesmo (5.96e-08) e o desvio da ortonormalidade é 7.54e-08 (CONFIRMED BY TEST).
  - Na aplicação (`t_360.py`): E×24 na barreira 10 e Shift+E×4 na 7, depois Ctrl+S → `gravado … (2 edições)`, com `m` diferente de `m0` só na 7ª casa decimal.
- **Onde.** `src/edit/history.cpp:103` (`changed` compara floats exatamente) e `:99` (π em float).
- **Impacto.** O edits.json ganha entradas que não mudam nada, e o `.nefs` é regravado à toa. Pequeno, mas confunde a contagem de edições.
- **Solução mínima.** Em `changed()`, tolerância absoluta de ~1e-5 nas 9 casas da rotação e na posição. Ou guardar no histórico o ângulo acumulado e, quando ele for múltiplo de 360°, copiar `m0`. A primeira opção é uma linha.
- **Teste.** Em `rot_test`, 24×15° → `changed=0`. Repetir `t_360.py` → `0 edições`.

### P2-2: a seleção continua em objeto oculto, e R ressuscita uma cópia desfeita
- **Evidência (CONFIRMED BY TEST, `t_sel.py`).**
  - Apagar, Ctrl+Z, clicar no mesmo objeto, Ctrl+Y → `inst 934/1 011`, mas o título continua com `… | 10 | …`. E×3 → `hist 4/4`, e o edits.json grava `"deleted":true` com `m` girado.
  - Ctrl+D, Ctrl+Z → a cópia some (`inst 934`) mas continua selecionada (`cópia de 7`). R → `inst 935`, `hist 5/5`: a cópia desfeita volta e o redo é descartado.
- **Onde.** `src/app/track_view.cpp:125` (undo/redo não revalida `sel_`), `:152` (R).
- **Solução mínima.** Depois de `undo`/`redo`, `if (sel_ >= 0 && inst_.hidden[sel_]) sel_ = -1;`.
- **Teste.** Repetir `t_sel.py` partes 1 e 2: depois do Ctrl+Y ou Ctrl+Z, o título fica sem seleção, e E e R não fazem nada.

### P2-3: soltar Ctrl antes de S/D/A anda com a câmera
- **Evidência (CONFIRMED BY TEST, `t_dbg3.py`).** Ctrl↓ S↓ S↑ Ctrl↑ → 0 pixels mudam. Ctrl↓ S↓ Ctrl↑ (300 ms) S↑ → 414 685 pixels mudam: a câmera andou para trás durante o tempo em que S ficou apertado sem Ctrl. Com `xdotool key ctrl+s` também muda (135 k pixels), porque o xdotool solta nessa ordem. Por isso todos os meus scripts usam `ckey()`.
- **Onde.** `src/app/main.cpp:320` (`walk_keys` consulta só o modificador atual).
- **Solução mínima.** Guardar as teclas W/A/S/D que foram apertadas junto com Ctrl e ignorá-las até serem soltas (um `std::bitset` de scancodes "consumidos" em `SDL_EVENT_KEY_DOWN` com Ctrl, limpo em `SDL_EVENT_KEY_UP`).
- **Teste.** `t_dbg3.py`: a segunda sequência também dá 0 pixels.

### P2-4: `--out` só é validado no primeiro Ctrl+S
- **Evidência (CONFIRMED BY TEST).** Os casos de P0-1 abrem a pista normalmente e só falham no Ctrl+S. FACT: o construtor só checa `inside_game_folder` (`track_view.cpp:33`).
- **Solução mínima.** No construtor, `create_directories(parent)` e um teste de abrir para escrita sem truncar (ou `access(W_OK)` na pasta). Se falhar, sair com código 1 antes da primeira edição.
- **Teste.** `--out /proc/nope/x.json --frames 1` → código 1 com mensagem clara.

### P2-5: `--camera` sem validação
- **Evidência (CONFIRMED BY TEST, ASan+UBSan, sem erro de sanitizer).**
  - `nan,…`, dist `0`, dist `1e30`, alvo x `1e30` e alvo x `inf` → `rc=0`, captura com **1 cor** (tela vazia).
  - Pitch `1.5707964` e pitch `10` renderizam (pitch fora de `[-0.2, 1.5]`).
  - Dist `-100` renderiza invertido.
  - `--frames 0`, `-5`, `1e3` e `abc` são recusados corretamente.
  - `--vsync abc` vira 0 sem aviso.
  - `--frames 99999999999999999999` satura em `LONG_MAX` sem erro.
- **Onde.** `src/app/main.cpp:60` e `:343-346`.
- **Solução mínima.** Rejeitar valores não finitos, `dist` fora de `[kDistMin, kDistMax]` e `pitch` fora de `[kPitchMin, kPitchMax]` (ou aplicar `std::clamp`). Checar `errno == ERANGE` no `strtol`.
- **Teste.** `--camera nan,0.7,100,0,100,-250` → código 1 com a mensagem de uso.

---

## P3: melhorias

- **P3-1. Picking.** INFERENCE (FACT: `src/render/pick.cpp:79-83`, sem teste de terreno). O picking usa só a AABB local do tipo, com `t0` limitado a 0. Por isso: (a) seleciona objetos atrás de morros (o terreno não oclui); (b) a caixa de um arco ou arquibancada bloqueia o que está sob o vão; (c) com o olho dentro de uma caixa, `t0 = 0` e essa instância sempre vence. Não testei porque a pista sintética é plana. Melhoria mínima para (c): exigir `t1 > 0` e preferir o menor `t0 > 0`. Para (a), um teste de raio contra o terreno (cálculo de CPU) seria maior, então fica para depois.
- **P3-2. UB em `count()`.** `src/core/track.cpp:20`: `static_cast<size_t>(1e300)`. CONFIRMED BY TEST com `-fsanitize=float-cast-overflow` (`$SP/rt.cpp`, pista `$SP/bad2/huge`): `runtime error: 1e+300 is outside the range of representable values of type 'long unsigned int'`. Solução: `d > 0 && d < 1e15 ? … : 0` (ou lançar). O mesmo vale para `vec3()`, de double para float.
- **P3-3. Gravação não atômica.** `edits_json.cpp:66` trunca e escreve. Um crash ou disco cheio no meio deixa o arquivo vazio ou truncado. Solução: gravar `<out>.tmp` e fazer `std::filesystem::rename`. INFERENCE.
- **P3-4. Mover perto do horizonte.** CONFIRMED BY TEST (`t_horizon.py`, pitch 0,15): arrastar 42 px para cima levou a barreira 0 de `0.25 … -251.00` para `-97.76 … -285.72`. Solução: limitar o deslocamento por evento, ou o `t` do `ground()` a algo como `draw_dist_`.
- **P3-5. Tipos sem malha não são editáveis.** CONFIRMED BY TEST: na varredura de picking (`t_picksweep.py`), 31/33 acertos em 1280×720 e 26/28 em 800×600 depois do resize. Os 2 erros são as instâncias 570/571, `e:synth_spawn_marker` com `count: 0`. É como o web (FACT: `ty.empty` não desenha nem seleciona). Sugestão: desenhar um marcador (cruz de linhas) e uma caixa de 1 m para pickar.
- **P3-6. Releitura do terreno a cada Tab.** CONFIRMED BY TEST com `$SP/two_terrains` (`route_1` → `terrain_1.bin`): cada Tab relê do disco e reenvia (0,002 s + 0,003 s na pista sintética). Na Montalegre (1324 malhas e 428 k vértices) pode virar segundos por Tab. HYPOTHESIS, não medido. Sugestão: cache de um `Terrain` por arquivo (há poucos por pista).
- **Outros (FACT, menores).**
  - `frame_route` usa a caixa de todas as rotas, não a da atual.
  - O caminho padrão de `--out` é relativo ao diretório de trabalho.
  - Com o histórico no limite de 300, "desfazer tudo" não volta ao arquivo (a barreira 7 ficou girada 120° depois de E×500 e Ctrl+Z×320; é o comportamento do web, e R resolve).
  - Uma instância com NaN na matriz conta sempre como "mudada" (`NaN != NaN`), e o `%.9g` grava `nan`, que não é JSON válido (INFERENCE; a pista `bad/inst_nan` abre e renderiza sem erro).

---

## O que testei e funcionou

- **Build limpo** (`$SP/build-clean`, RelWithDebInfo, `-Wall -Wextra`): **0 avisos**. O build ASan+UBSan (`$SP/build-asan`, Debug) também compila sem avisos.
- **Testes existentes** (normal e ASan): `camera_test OK`, `edit_test OK (107 verificações)`, `core_tests --track … OK (61 verificações)`. Testei também o `edit_roundtrip.py` sobre 3 edits.json gerados aqui (`asan2_edits.json` com movido, girado, 2 cópias e um apagado na `route_1`; `sel.json`; `hz.json`): `OK 5 edições conferidas … em 2 arquivo(s)`, `OK 1 …`, `OK 1 …`.
- **Sessões ASan+UBSan** (`t_asan.py`, `t_asan2.py`, `t_tab_asan.py`, `t_input.py`) com carga, Tab×4, selecionar, mover, Shift-altura, girar por arraste, E×24, E×500 (histórico 300/300), Ctrl+Z×320 (0/300), Ctrl+D×3, Delete, Ctrl+S nas duas rotas, F1–F4/G/I/[ ], zoom ×1200 e resize: **nenhum erro de ASan/UBSan**. **LSan não reportou nenhum vazamento**, nem do projeto nem do Mesa/SDL. Confirmei que o LSan funciona aqui com um programa que vaza 77 bytes (`$SP/leak.cpp`).
- **Entradas sem efeito** não fazem nada e não quebram (`t_asan2.py`, linha "vazio": `hist 0/0`): Delete, Ctrl+Z, Ctrl+Y e Ctrl+D sem seleção, R e E sem seleção. Ctrl+D numa árvore (`t:`) também não faz nada. F sem seleção enquadra a rota.
- **Picking.** Testei 33 barreiras e 1 árvore, a 1280×720 e a 800×600 depois de `windowsize`: todas as que têm malha acertaram o `idnum` (P3-5 explica as 2 exceções). Depois de mover, o clique no lugar novo seleciona e no antigo não (`t_pickmove.py`). Depois de girar 90°, o centro ainda seleciona. Depois de apagar, o clique não seleciona.
- **Histórico.** O limite de 300 funciona (`hist 300/300` com E×500). Undo e redo depois de Apagar funcionam (`t_undo.py`: 6/6 → 5/6 → 2/6 → 4/6).
- **Robustez de carga** (`$SP/mkbad.sh` + `$SP/runbad.sh`, ASan, `--frames 5`). Os erros na partida saem como mensagem clara com código 1, sem sinal e sem erro de sanitizer:
  - **track.json:** truncado (`json: string sem fim no byte 300`), lixo, sem rotas, aninhamento de 100 000 (`aninhamento fundo demais no byte 257`) e sem `type_order` (`DR2I: instância 0 tem tipo 0 com 0 tipos`).
  - **Terreno:** truncado, vazio, contagem 0xFFFFFFFF e índice fora da malha (`DR2M: malha "terrain_0_0" tem índice 50170 com 1 vértices`).
  - **inst_route_0:** truncado, vazio e n = 0xFFFFFFFF.
  - **objects.bin:** truncado e ausente.
  - **Pasta inexistente:** `não abriu …/track.json`.
  - **Degradação graciosa** nas texturas ausentes, truncadas ou com lixo (aviso por arquivo, cor fixa, `rc=0`) e nos `first`/`count` absurdos dos tipos (`rc=0`).
- **Entrada extrema.** Testei zoom com 400 cliques de roda para dentro e 800 para fora, pitch nos extremos por arraste e janela em 1×1, 5×300, 2000×50, minimizada e restaurada: o programa continua vivo e sem erro de sanitizer (`t_input.py`).
- **Recursos.** Testei Tab×50 nas duas variantes. Com o mesmo terreno, o VmRSS ficou estável (173 768 → 173 880 kB, igual do Tab 10 ao 50). Com terrenos diferentes, recriando `Terrain` a cada Tab, ficou em 173 612 → 173 856 kB, também estável. Com ASan, Tab×20 deu 600 → 602 MB, sem vazamentos no LSan. FACT do código: `Buffer`, `Vao` e `Program` são RAII; `TextureCache` libera no destrutor; `Terrain` e `RouteLines` são `unique_ptr` recriados; `InstanceRenderer` é único (o buffer de instâncias é reespecificado com `glBufferData`); `TrackView` é destruído antes do contexto (`Platform` é declarado antes, em `run`).
- **Tempo e desempenho** (llvmpipe, 4 CPUs, `--vsync 0`, 120 quadros). Carga: `objects.bin` 0,000 s; terreno com leitura de 0,007–0,008 s (5,3 MB) e envio de 0,009 s; texturas 0,025–0,030 s; parede total de `--frames 1` ≈ 0,22 s. FPS por câmera:

  | Câmera | FPS |
  |---|---|
  | enquadramento padrão | 21,3 / 20,7 |
  | `0.8,1.25,60,…` | 58,7 / 55,4 |
  | `0.8,0.15,60,…` | 30,2 / 32,8 |
  | dist 15 000 | 46,7 / 44,4 |
  | dist 5 rente ao chão | 40,0 / 42,3 |

  Só serve para comparar; a pista é pequena e todo o terreno é desenhado sempre (não há frustum culling).

## O que não consegui testar e por quê

- **Perda de contexto GL e GPU real/NVIDIA:** só há o llvmpipe. Não há como forçar reset de contexto no Xvfb.
- **Erro de GL em tempo de execução** (P1-4): não achei como provocar um erro real sem mudar o código. As texturas do WebP não passam de 16383 px, e o `GL_MAX_TEXTURE_SIZE` do llvmpipe é 16384.
- **Oclusão do picking pelo terreno e vãos de arcos** (P3-1): a pista sintética não tem morro nem vão para isso. Ficou como INFERENCE.
- **Desempenho e tempo de troca de rota numa pista real (Montalegre):** não há jogo nem pista real aqui.
- **Uma ocorrência sem explicação:** na primeira execução de `t_asan2.py` (ASan), o processo não saiu em 180 s depois de Esc e foi morto (`rc -9`). Três reexecuções e `t_exit.py` (com 5 e 400 edições) saíram em ~1,3 s com `rc 0`. Não reproduzi. HYPOTHESIS: foco ou Esc perdido no Xvfb; não há indício de deadlock no código.
- **Diálogo de fechar a janela** (`SDL_EVENT_QUIT` pelo gerenciador de janelas): o Xvfb não tem gerenciador de janelas. Pelo código (FACT, `main.cpp:272`), sai sem perguntar, como o Esc.

## Como repetir

```bash
SP=/tmp/claude-0/-home-user-DR2Hook/04e0f535-6bba-572c-b584-3c3caa091cdf/scratchpad/r1
export DISPLAY=:99 LD_LIBRARY_PATH=/opt/sdl3/lib
cd /home/user/DR2Hook
cmake -S tools/viewer3d -B $SP/build-asan -G Ninja -DCMAKE_PREFIX_PATH=/opt/sdl3 -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g -fno-omit-frame-pointer" && cmake --build $SP/build-asan
cd $SP
./mkbad.sh && ./runbad.sh                       # carga com pistas corrompidas (ASan)
python3 t_fail.py bad/inst1_trunc x.json tab    # P0-2 (também route1_terrain_missing, route1_noterrain)
python3 t_fail.py /home/user/DR2Hook/build/uiview/tracks/synthetic__dr2hook_ring /proc/nope/x.json save   # P0-1
python3 t_fail.py bad/shortname x.json none     # P0-4
python3 t_hl.py                                  # P1-1 (compare h0/h_sel/h_f2/h_sel_f2)
python3 t_sel.py                                 # P1-2, P1-3b, P2-2
python3 t_dbg3.py                                # P2-3
./rot_test; python3 t_360.py                     # P2-1
python3 t_asan2.py                               # sessão ASan completa
python3 t_tab.py /home/user/DR2Hook/build/uiview/tracks/synthetic__dr2hook_ring 50; python3 t_tab.py $SP/two_terrains 50
python3 t_picksweep.py; python3 t_pickmove.py; python3 t_input.py $SP/build-asan/viewer3d
```
Os scripts que clicam ou gravam passam `--out` dentro de `$SP`, e nenhum deles grava no repositório. O único que gravou no caminho padrão foi a primeira versão de `t_dbg2.py`, já corrigida (veja o aviso no topo).
