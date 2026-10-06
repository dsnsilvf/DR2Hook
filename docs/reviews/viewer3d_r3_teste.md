# Teste da Rodada 3 do `tools/viewer3d` (commit 861254d)

Testador: agente 3. Nada foi alterado em `/home/user/DR2Hook`.

- Pasta de trabalho: `S=/tmp/claude-0/-home-user-DR2Hook/04e0f535-6bba-572c-b584-3c3caa091cdf/scratchpad/r3test`.
- Ambiente: Xvfb `:96`, Mesa llvmpipe, 4 CPUs.
- "Antes": `r3eval/build/viewer3d` (07275a7, RelWithDebInfo).
- "Depois":
  - `$S/build` (Release, pedido);
  - `$S/build-rwdi` (RelWithDebInfo, igual ao "antes"; usado nos FPS);
  - `$S/build-asan` (ASan+UBSan);
  - `$S/build-inst` e `$S/build-inst-asan`: cópia com medição. Os temporizadores de `cull`, `current_edits` e `resume_edits`, as malhas desenhadas e os contadores de objetos GL estão em `$S/instrumentacao.diff`.

Rótulos: **FACT** (lido no código), **CONFIRMED BY TEST**, **INFERENCE**, **HYPOTHESIS**.

## Resumo

| Sev. | Achado |
| --- | --- |
| P0 | Nenhum crash, nenhum erro de ASan/UBSan/LSan, nenhum vazamento de GL |
| P2 | **Regressão**: abrir pelo menu uma pista que falha descarta as edições não gravadas da pista atual (antes, elas continuavam) |
| P2 | **Novo**: filtro de teclas deixa uma tecla "presa" no ImGui (autorrepetição + clique fora do campo); o próximo campo se apaga sozinho |
| P2 | **Novo**: o aviso de autosave é sobrescrito por "retomadas N edições" sempre que o `edits.json` já existe (o caso comum) |
| P2 | **Novo**: o autosave fica velho depois de uma saída limpa (falso aviso). O caminho de recuperação do README ressuscita edições desfeitas e grava no arquivo `.autosave.json`, não no `--out` |
| P2 | **Novo**: `--terrain-dist` mede a partir do olho. Na panorâmica, o terreno sob o alvo some (a pista flutua no céu). Os "13,9 fps" do README vêm daí |
| P3 | Teclas digitadas logo depois do Ctrl+clique num campo, a poucos fps, são perdidas (Enter incluso). Antes, chegavam |
| P3 | "Esc com combo aberto só fecha o combo" (commit) é falso: o Esc não fecha o combo e é engolido enquanto há popup |
| P3 | `unsaved()` diz "não gravado" com um campo só ativo, sem mudança. Um campo deixado ativo adia o autosave indefinidamente |
| P3 | Verificação de OOM parcial: `glGetError` lê um só erro (pode pegar outro e perder o OOM); texturas e `glBufferSubData` não são verificadas |
| P3 | Corte das instâncias ainda varre tudo a cada quadro ao andar e a cada movimento de arraste (item 4 parcial) |
| P3 | Comentários trocados de lugar (`terrain.hpp`, `track_view.hpp`); leitura de `imgui_keys_[sc]` antes do teste de limite; a barra de ferramentas ficou ~170 px mais larga |

---

## 1. Regressão

- **Testes (CONFIRMED BY TEST)**: `camera_test OK`, `edit_test OK (129 verificações)` (retomar 50 mil em 0,103 s; 0,633 s no ASan) e `core_tests --track <ringue> OK (61)`, nos builds Release e ASan+UBSan.
- **Capturas na pista pequena, `--panels 0`, 8 câmeras** (padrão, rente ao chão, panorâmica, a de seleção e mais 4 câmeras próprias):
  - O resultado foi idêntico byte a byte fora do contador de fps da barra de status, na caixa (1396,786)–(1404,797).
  - Esse contador varia de uma execução para outra até no mesmo binário: o "antes" contra ele mesmo dá 73 px diferentes ali.
  - Em 2 câmeras, até o contador bateu e o arquivo inteiro ficou igual.
  - No estresse (câmera padrão), também é idêntico fora do contador.
  - Rente ao chão no estresse, `--terrain-dist 3000` dá a mesma imagem que sem limite.
  - Script: `$S/cmpmask.py`. Capturas: `$S/shots/` (CONFIRMED BY TEST).
- **Funções das rodadas 1 e 2** (`$S/t_reg.py`, `t_modal.py`, `t_hist2.py`; CONFIRMED BY TEST): todas funcionaram.
  - Seleção no 3D (#18).
  - Seta X do gizmo (72,31 → 80,86).
  - E +15° no modo Girar.
  - Ctrl+Z ×2 e Ctrl+Y.
  - X digitado no Inspector (75).
  - Filtro "tyre" na Cena e clique na instância (#155).
  - Delete.
  - Tab para `route_1`, Delete lá e Tab de volta.
  - Ctrl+S com 3 edições.
  - Retomar ("retomadas 3 edições").
  - `edit_roundtrip.py` → "OK 3 edições conferidas".
  - Ctrl+Q com edição abre o modal; Esc fecha o modal; SIGTERM vira modal.
  - 310 giros: "Histórico 300/300" e "(início: 10 passos mais antigos descartados)", com a dica.
- Observação (CONFIRMED BY TEST, igual antes e depois): digitar "-250" num campo via `xdotool type` não muda o valor; "250" muda. HYPOTHESIS: é artefato do `xdotool` com o "-"; não é regressão.

## 2. Antes × depois na pista de estresse

FPS com `--vsync 0 --frames 40`, painéis ligados, 1440×810, duas rodadas (`$S/bench.sh`, `$S/bench.txt`):

| Câmera | Antes | Depois | Depois, `--terrain-dist 3000` |
| --- | --- | --- | --- |
| padrão | 3,3 / 3,3 | 18,9 / 18,9 | 19,0 / 17,8 |
| rente ao chão `0.8,0.08,30,0,101,0` | 1,7 / 1,7 | 2,7 / 2,6 | 16,6 / 18,6 (imagem idêntica à sem limite) |
| panorâmica `0.8,0.9,9000,0,100,0` | 1,3 / 1,4 | 1,8 / 1,8 | 13,9 / 13,1 (**o terreno sob o alvo some**, ver P2-e) |

| Medida | Antes | Depois |
| --- | --- | --- |
| Pico de RSS na carga | 1678 MB | 942 MB |
| Leitura do terreno | 0,51–0,57 s | 0,54–0,63 s |
| Envio à GPU | 0,63–0,70 s | 0,44–0,49 s |
| Carga + 1 quadro (parede, `--frames 1`) | 2,0–3,5 s | 1,35–1,41 s |
| Menu: ringue → estresse → estresse2 → estresse (pico no trecho) | 1713 / 2198 / 2189 MB | 967 / 1011 / 1011 MB |
| RSS depois de abrir o estresse pelo menu | 969–1043 MB | 618–648 MB |
| Ringue depois de ter aberto o estresse | 628 MB | 187 MB |
| Menu abre depois de 100 teclas `e` a ~1–2 fps (panorâmica) | 143,6 s | 2,5 s (2,1 s sem teclas) |
| Retomar 50 mil edições (`resume_edits` inteiro, com o parse do JSON de 22 MB) | 4,78 s (avaliador; não medi) | 0,69 s |
| Arraste do campo X / gizmo, 20 movimentos, 50 mil edições: `current_edits` | 21 × 278 ms (avaliador) | 1 × 233–246 ms, ao soltar |
| idem: recortes das instâncias | 21 (avaliador) | 21 × 4,1–4,4 ms |
| W por 4 s: recortes | 293, um por quadro, 3,4 ms (avaliador) | 104 em 121 quadros, média 4,5 ms, máximo 9,5 ms |
| 10 cliques de roda: recortes | 10 (avaliador) | 0 |
| Malhas do terreno desenhadas (padrão, depois do zoom) | 10 496 | 532 (38 com `--terrain-dist 3000`) |

Para a troca pelo menu, criei uma pasta `stress2` com links para os arquivos. Um link para a pasta inteira não abre: desde a R2, o menu a reconhece como a pista atual e desabilita o item. Scripts: `t_menu.py`, `t_trickle.py`, `t_drag.py` e `t_wasd.py`.

## 3. Itens do relatório do avaliador

| # | Veredito | Evidência |
| --- | --- | --- |
| 1 terreno sem corte | **Corrigido** na câmera padrão (×5,7). Rente ao chão e na panorâmica, só com `--terrain-dist`, cujo centro é discutível (P2-e) | tabela acima |
| 2 `unsaved()` no arraste | **Corrigido**: 1 `current_edits` por arraste. Sobra um engasgo de ~0,25 s ao soltar, com 50 mil edições | `t_drag.py` |
| 3 retomar quadrático | **Corrigido** (`unordered_map`; o tipo é `uint16`, então a chave `id<<16\|tipo` não colide; FACT) | edit_test e `t_drag.py` |
| 4 corte das instâncias | **Parcial**: o zoom não recorta mais. Andar e arrastar ainda varrem as 323 mil (4,5 ms) a cada quadro | `t_wasd.py` |
| 5 pico de memória | **Corrigido** (1678 → 942; menu 2198 → 1011). Porém criou a regressão P2-a | `t_menu.py`, `t_fail.py` |
| 6 texturas no meio do quadro | Não tratado (não era prometido) | FACT |
| 7 trickle | **Corrigido** (143,6 → 2,5 s). Criou P2-b, P3-a e P3-b | `t_trickle.py`, `t_stuck.py`, `t_type.py`, `t_combo.py` |
| 8 histórico cheio | **Corrigido** (rótulo e dica; sem "restaurar tudo") | `reg/hist2_c.png` |
| 9 OOM de GL | **Parcial**: só buffers `GL_STATIC_DRAW`; texturas e `glBufferSubData` não. OOM real não reproduzido | FACT, `gl.cpp:118-120` |
| 10 autosave | **Parcial**: grava e sobrevive ao kill -9. O aviso some (P2-c), o arquivo fica velho e a recuperação é armadilha (P2-d) | `t_as.py`, `t_as2.py` |
| 11 nomes longos | **Corrigido**: só um tipo abre, sem erro de ID | `edge/names1.png` |
| 12 rota sem portões | **Corrigido**: enquadra o terreno inteiro | `edge/a_e2_sem_ia.png` × `b_` |
| 13 janela estreita | Não tratado nesta rodada. Não houve crash em 640×360, 320×240 e 60×40, mas o status continua sobreposto | `edge/win_640.png` |
| 19 memória morta | **Parcial**: `library_` sai; `Track::raw` e os índices de 32 bits ficam | FACT, `core/track.hpp:47` |
| 14–18, 20 | Não tratados (não eram prometidos) | — |

## 4. Bugs novos e regressões

### P2-a. Falha ao abrir pelo menu descarta as edições não gravadas (regressão)

- **Teste (CONFIRMED BY TEST, `t_fail.py`)**: no ringue, apaguei a #18 ("hist 1/1, não gravado, inst 934"). Depois, Arquivo > Abrir pista > `zz_quebrada` (sem `terrain_0.bin`) > "Abrir sem gravar".
  - Depois: a pista é reaberta do disco ("hist 0/0, inst 995"), a edição sumiu e não há autosave.
  - Antes: "não abriu…" e a pista continuava com "hist 1/1, não gravado".
- **Por quê**:
  - O modal diz "Abrir outra pista descarta as edições desta", mas a outra pista não abriu.
  - A câmera também é reenquadrada.
  - Com `--fresh`, a reabertura também ignora o `edits.json` gravado nesta sessão (`open(old_dir, old_out)` usa `!opt.fresh`; INFERENCE pelo código).
- **Arquivo**: `src/app/main.cpp:402-435` (`track.reset()` antes de `open(dir…)`).
- **Correção mínima**:
  - guardar `old_edits = track->current_edits()` antes do `reset`;
  - na reabertura, aplicar esse texto (por exemplo, gravar num `<out>.autosave.json` e retomar dele sem trocar `out_`);
  - ou validar antes do `reset` que `track.json`, `objects.bin` e os `.bin` da rota 0 existem e são legíveis.

### P2-b. Tecla presa no ImGui pelo filtro de teclas

- **Teste (CONFIRMED BY TEST, `t_stuck.py`)**:
  1. Ctrl+clique em X e segurar Backspace (a autorrepetição apaga o campo).
  2. Clicar numa área vazia do Inspector, ainda segurando.
  3. Soltar.
  4. Ctrl+clique em Y.
- **Resultado**: o campo Y se apaga sozinho (`st_depois_2_c.png`). Antes, Y mostrava "102.250" (`st_antes_2_c.png`).
- **Causa (FACT, `src/app/ui.cpp:126-128`)**:
  - Um KEY_DOWN de repetição não repassado, porque o campo já não está ativo, faz `imgui_keys_[sc] = false`.
  - A soltura então não vai ao ImGui, que fica com a tecla apertada até a próxima vez que ela for apertada num campo.
- **Correção mínima**:

  ```cpp
  if (e.type == SDL_EVENT_KEY_DOWN) { forward = forward || imgui_keys_[sc]; if (forward) imgui_keys_[sc] = true; }
  else imgui_keys_[sc] = false;
  ```

  Isso também repassa as repetições de uma tecla que o ImGui já tem. Testar `sc < SDL_SCANCODE_COUNT` antes de ler `imgui_keys_[sc]`.

### P2-c. Aviso do autosave sobrescrito

- **Teste (CONFIRMED BY TEST, `t_as.py`)**:
  1. `--autosave 2`: Delete, Ctrl+S, Ctrl+Z e espera de 3,5 s. O `a.json.autosave.json` é criado.
  2. kill -9 e reabrir.
  3. No log aparecem "há edições de uma sessão que não gravou…" e, logo depois, "retomadas 1 edições…".
  4. A barra de status mostra só "retomadas…" e "gravado" (`as_reabre_c.png`).
- **Quando acontece**: em toda pista que já foi gravada uma vez. O teste do implementador (`r2impl/t_auto.py`) usou um `--out` que não existia, por isso passou.
- **Agravante**: mesmo sem ser sobrescrito, o aviso não começa com "não"/"NÃO", então some em 8 s.
- **Arquivo**: `src/app/track_view.cpp:76-90` (aviso antes de `resume_edits`, que chama `set_status` em `:132`) e `ui.cpp:677-680`.
- **Correção**: mostrar o aviso depois de `resume_edits` (ou juntar as duas mensagens) e tratá-lo como erro (30 s, vermelho), ou abrir um modal.

### P2-d. Ciclo de vida do autosave

- **Teste (CONFIRMED BY TEST, `t_as2.py`)**:
  1. `--out b.json --autosave 2`, Delete, espera de 3,5 s (o autosave é gravado), Ctrl+Z e Ctrl+Q. Sai sem pergunta (nada a gravar).
  2. O autosave **fica**. Ao reabrir, aparece o aviso "há edições de uma sessão que não gravou" (falso).
  3. Seguindo o README (`--out b.json.autosave.json`), vem "retomadas 1 edições", isto é, o Delete que tinha sido desfeito.
  4. O Ctrl+S grava em `b.json.autosave.json` (e cria `b.json.autosave.json.1.bak`). O `b.json` nunca é criado.
- O mesmo vale para "Sair sem gravar" e "Abrir sem gravar" (FACT: só o `save()` apaga o autosave).
- **Arquivo**: `src/app/track_view.cpp:542-552`, `:497`; README, item "Autosave".
- **Correção mínima**:
  - em `autosave_tick`, se `!unsaved()`, apagar o autosave;
  - apagá-lo também ao sair ou descartar de forma limpa;
  - recuperar com uma opção (por exemplo, `--recover`) que aplica o autosave mas mantém `out_` = o arquivo de verdade.

### P2-e. `--terrain-dist` medido do olho

- **Teste (CONFIRMED BY TEST)**: na panorâmica `0.8,0.9,9000,…` com 3000, só aparece o terreno perto do olho, a ~5,6 km do alvo. O ringue no centro fica sem chão (`shots/pan_3000.png` × `pan_0.png`). O ganho de 1,8 → 13,9 fps é o de não desenhar o que se quer ver.
- Os objetos usam o alvo como centro (FACT, `instances.cpp` `passes`). O terreno usa `cam.eye()` (FACT, `track_view.cpp:299`, `terrain.cpp` `draw`).
- **Correção**: medir do alvo, como os objetos, ou usar `max(raio, cam.dist + raio)`. Ajustar o README (as linhas da R3 com "13,9").

### P3

- **a. Digitação logo depois do Ctrl+clique (CONFIRMED BY TEST, `t_type.py`)**:
  - Teste: estresse `stress_u` com `LP_NUM_THREADS=1` (~13 fps), Ctrl+clique em X e "50"+Enter na mesma rajada do `xdotool`.
  - Depois: o campo fica aberto com "72.308" e o "50" e o Enter se perdem (`ty_depois_s0.png`). Antes: X = 50.00.
  - Com espera de 1,5 s, os dois funcionam. A 50 fps, também.
  - Causa (FACT): `WantTextInput` ainda é do quadro anterior (`ui.cpp:117-133`).
  - Correção: se o último botão do mouse foi do ImGui (`WantCaptureMouse` no clique), repassar as teclas da mesma leva.
- **b. Esc com combo (CONFIRMED BY TEST, `t_combo.py`)**:
  - O combo de rota continua aberto depois do Esc, antes e depois (ImGui 1.91.9b sem `NavEnableKeyboard`).
  - Depois, o Esc é engolido (a seleção fica); antes, tirava a seleção.
  - A frase do commit está errada.
  - Ctrl+Q e Ctrl+S também ficam mortos com um menu aberto.
  - Correção: com um popup aberto e Esc, chamar `ImGui::ClosePopupsExceptModals()` no próximo quadro.
- **c. `unsaved()` com `drag_.active`**:
  - Ativar um campo do Inspector (`begin_change`) já mostra "não gravado" sem mudança alguma (CONFIRMED BY TEST, `ty_depois_s0.png`).
  - `autosave_tick` reinicia o relógio e sai se `drag_.active` (`track_view.cpp:543-545`, FACT). Um campo deixado aberto adia o autosave para sempre (INFERENCE).
  - Correção: só pular o autosave sem reiniciar o relógio; em `unsaved()`, usar o cache quando `drag_.numeric` e não houve `set_matrix`.
- **d. Falha do autosave só no stderr (CONFIRMED BY TEST, `t_as3.py`)**: com `--out` impossível, a cada período só aparece "autosave falhou". A barra já avisa que não dá para gravar, então é menor.
  - O autosave também chama `current_edits()` duas vezes (`unsaved()` e a gravação).
  - INFERENCE: com 50 mil edições são ~0,5 s de engasgo a cada 60 s.
- **e. OOM**:
  - `glGetError()` em `gl.cpp:118` lê um só sinalizador. Um erro pendente de antes esconde o OOM, e o erro lido some do `gl::warn` (FACT).
  - Texturas e `glBufferSubData` ficam sem teste.
  - Não reproduzi um OOM real (llvmpipe).
- **f. Item 4 parcial**: veja a tabela de medidas (4,5 ms por quadro andando, recorte a cada movimento de arraste).
- **g. Detalhes de código (FACT)**:
  - `terrain.hpp`: "// malhas desenhadas no último quadro" ficou depois de `bounds()`, não em `drawn()`.
  - `track_view.hpp`: o comentário de `backed_up_` foi para `autosave_t_`.
  - `ui.cpp:126` lê `imgui_keys_[sc]` antes do `if (sc < SDL_SCANCODE_COUNT)` (HYPOTHESIS: o SDL não manda scancodes ≥ 512).
- **h. Barra de ferramentas**: o slider "Terreno" (150 + 18 px) termina em x ≈ 1278. Abaixo de ~1285 px de largura lógica (por exemplo, 1920 com escala 1,5), ele fica cortado (INFERENCE pela captura; agrava o item 14).
- **i. Bem feito (FACT)**:
  - O frustum (Gribb e Hartmann) usa as linhas certas da matriz em coluna do glm, inclusive o near `r3+r2` para z em [-w,w].
  - O teste do vértice positivo é conservador: câmera dentro da caixa nunca corta, e os planos não normalizados não importam para o sinal.
  - Partes com `count` 0 não entram (`glMultiDrawElements` nunca recebe contagem 0).
  - O agrupamento só junta partes com a mesma textura, ou o mesmo material sem textura com a mesma cor por vértice.
  - `malloc_trim` está protegido por `__GLIBC__`.
  - `meshes` sai do escopo antes do trim.

## 5. Uso prolongado (ASan+UBSan, `t_long.py`)

- **Sessão 1**: 420 s e 671 operações aleatórias no ringue (cliques, arrastes, 1/2/3, E/Q, Ctrl+D, Delete, Ctrl+Z/Y, Tab, roda, [ ], arraste direito, W). Pelo menu: estresse, ringue, `zz_quebrada` (falha), ringue, `zz_quebrada`, ringue. 2 autosaves.
  - Resultado: GL sempre `buf=17 vao=15 tex=14` (5 texturas logo depois de abrir o estresse).
  - O ringue ficou em 621–632 MB depois de ter aberto o estresse, sem crescer.
  - Nenhum relatório do sanitizer.
- **Sessão 2**: 150 s, 399 operações, estresse, ringue e `zz_quebrada`, e saída limpa (rc=0). Nenhum relatório de ASan, UBSan ou LSan.
- **Uma ressalva**: na sessão 1, o Ctrl+Q final não fechou. Um menu tinha ficado aberto: o script clicou na pista atual, que vem desabilitada. Com popup aberto, as teclas vão ao ImGui (P3-b). Foi artefato do script, não travamento.

## 6. Casos de borda

| Caso | Resultado |
| --- | --- |
| Janela 640×360, 320×240 e 60×40 (`xdotool windowsize`) | Sem crash; status sobreposto (`edge/win_*.png`) |
| Pista sem instâncias (`e1_sem_inst`) | rc 0, antes e depois |
| Rota sem portões nem IA (`e2_sem_ia`) | Enquadra o terreno; antes, só a encosta |
| Nomes longos (`pista com espaço çãé`) | Abre um tipo só, sem erro de ID. O nome passa da borda sem "…" nem dica |
| Textura 8191×6007 (`e5_tex`) | rc 0 |

Todos CONFIRMED BY TEST.

## Não testado

- OOM de vídeo real.
- GPU real e RTX 4050: os FPS valem só como proporção.
- HiDPI e Wayland.
- Windows e libc sem glibc (sem `malloc_trim`).
- Montalegre e pistas reais.
- O custo exato do autosave com 50 mil edições (só INFERENCE).
- O slider "Terreno" arrastado com o mouse (só `--terrain-dist`).
- O mesmo binário Release contra o "antes" RelWithDebInfo nos FPS: usei o build RelWithDebInfo de depois.
