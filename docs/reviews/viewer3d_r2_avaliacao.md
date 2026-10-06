# Rodada 2: UX e produtividade do `tools/viewer3d` (commit e2297b2)

Avaliador: agente 1. A cópia foi compilada em `$S/build`, com `S=/tmp/claude-0/-home-user-DR2Hook/04e0f535-6bba-572c-b584-3c3caa091cdf/scratchpad/r2eval`. Não modifiquei nada em `/home/user/DR2Hook`.

## Método

- **Build e testes.** `git archive e2297b2 tools/viewer3d` e build com Ninja: 26/26 alvos, sem erro. `edit_test OK (118 verificações)`, `camera_test OK`.
- **Ambiente.** Xvfb `:96` (1600x900x24), llvmpipe, pista `build/uiview/tracks/synthetic__dr2hook_ring`: 1011 instâncias na `route_0` e 995 na `route_1`, 12 tipos, 15 materiais.
- **Sessões.** Oito sessões com `xdotool` (`$S/drv.py`, `$S/s1.py` a `$S/s8.py`). O título da janela foi lido com `xdotool getwindowname` e as capturas foram tiradas com `import -window`, em `$S/shots/`. Toda gravação foi feita com `--out` para `$S/out`, para não tocar em `build/uiview/saves`.
- **Comparação com o web.** Captura com Playwright de `build/synth_preview`, servido em 127.0.0.1:8796 com a seleção do pórtico: `$S/shots/web_sel.png`. Também li `trackview.js`, `index.html` e o `i18n` (chaves `trk.*`), e a parte de gizmo e atalhos do `carview.js`.
- **Limitação.** O Xvfb não tem gerenciador de janelas, então não vi a barra de título renderizada. A legibilidade dela foi estimada pelo comprimento da string (INFERENCE).
- **Observação.** A árvore de trabalho de `/home/user/DR2Hook` tem modificações não commitadas de outro agente: `src/app/ui.cpp` e `third_party/imgui` 1.91.9b. Não avaliei esse código; tudo abaixo vale para o e2297b2.

## Resumo executivo

O núcleo funciona: seleção, mover, girar, duplicar, desfazer, rotas e gravação atômica estão corretos (CONFIRMED BY TEST). Mas o editor ainda não serve para um dia de trabalho, por três motivos:

1. **Perde trabalho com teclas comuns.** Esc também sai do programa (P0-1).
2. **Não retoma sessões.** O `edits.json` existente não é carregado, e o Ctrl+S seguinte o substitui (P0-2).
3. **Toda a informação está no título da janela.** O título passa de 278 caracteres, e "não gravado" só aparece a partir do caractere 203 (P1-1).

Faltam ainda as peças que o próprio web já tem: campos X/Y/Z, botões de ação, seletor de pista e de rota, e camadas visíveis. Também faltam as que fazem um editor "profissional": Scene Tree com busca, gizmo, caixa de seleção, snapping e histórico visível.

| ID | Prioridade | Título |
| --- | --- | --- |
| P0-1 | P0 | Esc sai do editor; Esc, Esc, Esc descarta edições não gravadas |
| P0-2 | P0 | Sessão não retoma: `edits.json` existente é ignorado e substituído |
| P1-1 | P1 | Descoberta: nenhuma UI na tela, tudo no título (~280 caracteres, truncado em 512 bytes) |
| P1-2 | P1 | Sem Scene Tree nem busca: objetos sem malha e apagados ficam inalcançáveis |
| P1-3 | P1 | Objeto apagado não pode ser selecionado nem restaurado (só Ctrl+Z linear) |
| P1-4 | P1 | Sem edição numérica nem snapping: 40 px de arraste = 56 m |
| P1-5 | P1 | Não abre outra pista nem escolhe a rota numa lista; sem `--track` abre o cubo de teste |
| P1-6 | P1 | Realce da seleção fraco, sem caixa nem pivô nem gizmo; invisível de longe |
| P2-1 | P2 | Mover/Girar: errar o objeto orbita a câmera; arrastar outro objeto edita esse outro |
| P2-2 | P2 | Mensagem de status nunca expira e contradiz o estado ("não gravado" com "gravado") |
| P2-3 | P2 | Ações recusadas sem feedback (Ctrl+D em árvore, Q/E/R/Delete sem seleção) |
| P2-4 | P2 | `--out` padrão relativo ao diretório atual |
| P2-5 | P2 | Atalhos conflitantes com Blender, Unreal e o próprio Car Explorer (R, Q/E, G, F1, Tab) |
| P2-6 | P2 | Camadas e distância de desenho sem indicador |
| P2-7 | P2 | Seleção atravessa o terreno (picking ignora o relevo) |
| P2-8 | P2 | Histórico invisível e por rota |
| P2-9 | P2 | Sem seleção múltipla (534 barreiras na rota 0) |
| P2-10 | P2 | Mover não acompanha o relevo (altura fixa) |
| P2-11 | P2 | Erros só no stderr; falha na abertura fecha o programa |
| P3-1 | P3 | Câmera: zoom não vai ao cursor, pode entrar sob o terreno, sem modo voo |
| P3-2 | P3 | Seleção mostra `idnum`, não o id de texto (`ens_ids`); `kind` é uma letra |
| P3-3 | P3 | F enquadra pela origem do objeto, não pelo centro da caixa |
| P3-4 | P3 | Materiais e texturas não inspecionáveis (dados e GLuint já existem) |
| P3-5 | P3 | Desfazer e trocar de rota perdem a seleção |
| P3-6 | P3 | Cursor não muda com a ferramenta; título atualiza a cada 0,5 s |
| P3-7 | P3 | Portões e IA só da rota atual; splits do `progress` não aparecem |

---

## P0

### P0-1: Esc sai do editor; Esc, Esc, Esc descarta edições

- **Problema.** Esc tem dois sentidos: tira a seleção ou, sem seleção, sai do programa.
  - Sem edições, Esc com nada selecionado fecha na hora.
  - Com edições, o primeiro Esc sem seleção só escreve um aviso no fim do título, e um segundo Esc em até 3 s sai sem gravar.
- **Evidência.**
  - CONFIRMED BY TEST (sessão s3/s4). Sequência: selecionar barreira, Mover, E, Girar, Ctrl+D, Delete, Ctrl+Z, Esc, Esc. O Delete já tinha tirado a seleção, então o 1º Esc foi o aviso ("edições não gravadas: Ctrl+S grava; Esc ou fechar de novo em 3 s sai sem gravar") e o 2º Esc fechou o processo. `$S/out` ficou sem `s3.edits.json`, e as 5 entradas de histórico se perderam.
  - CONFIRMED BY TEST (s5): sem edições, Esc com seleção tira a seleção e o Esc seguinte fecha a janela (`alive False`).
  - FACT: o aviso fica nos caracteres 217+ de um título de 305 caracteres. INFERENCE: numa barra de título de 1280 px ele não aparece.
- **Arquivo.** `src/app/main.cpp:280-309` (`confirm_quit`, `SDLK_ESCAPE`).
- **Causa.** Esc foi sobrecarregado para "sair". Não há diálogo de confirmação nem autosave.
- **Impacto.** Perda de trabalho com a tecla mais usada para "cancelar". Nenhum editor de referência (Blender, Unreal, Car Explorer web) sai com Esc.
- **Solução recomendada.**
  1. Agora, sem ImGui: Esc só tira a seleção, cancela um arraste ou fecha um popup; nunca sai. Sair passa a ser Ctrl+Q ou fechar a janela.
  2. Com ImGui: `SDL_EVENT_QUIT` com edições abre um modal "Gravar e sair / Sair sem gravar / Cancelar".
  3. Autosave opcional em `<out>.autosave` a cada N minutos ou a cada 20 passos.
- **Como testar.**
  - `xdotool`: fazer 1 edição e mandar Esc ×5; o processo continua vivo e o histórico fica igual.
  - Fechar a janela abre o modal; Cancelar mantém tudo.
  - `--frames` continua saindo com 0.

### P0-2: sessão não retoma; `edits.json` existente é ignorado e substituído

- **Problema.** Ao abrir com um `--out` que já existe, as edições dele não são aplicadas. O primeiro Ctrl+S grava só as edições da sessão nova; o arquivo anterior vira `.1.bak`.
- **Evidência.**
  - CONFIRMED BY TEST (s7). Sessão A: apagar o pórtico (`synth_start_arch`, index 546) e Ctrl+S, que grava 1 edição `deleted:true`.
  - Sessão B, mesmo `--out`: o título mostra `inst 930/1 011 | hist 0/0` e o pórtico está de volta. Girar a arquibancada e Ctrl+S deixam o arquivo com `[('synth_grandstand', 547, False)]`: a deleção sumiu do arquivo principal e ficou só em `s7.edits.json.1.bak`.
  - FACT: o web também não importa edições. Não há leitor de edits em `trackview.js`.
- **Arquivo.** `src/app/track_view.cpp:140-158` (construtor: só avisa no stderr que o arquivo existe). `src/edit/edits_json.*` só escreve.
- **Causa.** Falta um leitor de `edits.json` e a aplicação dele às rotas.
- **Impacto.** Não dá para trabalhar numa pista em mais de uma sessão sem juntar JSON à mão. O `.bak` evita a perda total, mas a perda é silenciosa: o aviso vai só para o stderr.
- **Solução recomendada.** Criar `edit::read_edits(track, text)` em `dr2edit` (sem GL) que devolve as entradas por rota. Depois, no `TrackView`:
  1. Carregar as rotas citadas no arquivo (em `saved_`).
  2. Achar a instância por (`kind`, `type`, `index`) e conferir `m0` contra o arquivo. Se divergir, avisar e pular.
  3. Aplicar `m` e `deleted`, e recriar as cópias `added` com `duplicate` a partir de `src`.
  4. Gravar `saved_text_ = current_edits()`, para não contar como "não gravado".
  5. Mostrar "N edições carregadas de <arquivo>".

  Na UI, oferecer "Arquivo > Abrir edições…" e, ao abrir uma pista com `--out` existente, perguntar "Continuar as edições / Começar do zero".
- **Como testar.**
  - Rodar o roteiro s7 de novo: depois de reabrir, o pórtico continua apagado e o Ctrl+S grava 2 edições.
  - `edit_roundtrip.py` aprova o resultado.
  - `edit_test` ganha um caso de ida e volta: escrever, ler, aplicar e escrever de novo dá o mesmo texto.

---

## P1

### P1-1: descoberta; nenhuma UI na tela, tudo no título

- **Problema.** A janela mostra só o 3D (`$S/shots/initial.png`, `s1_start.png`). Ferramenta ativa, seleção, "não gravado", camadas e mensagens ficam todos no título.
- **Evidência.**
  - CONFIRMED BY TEST: um título típico tem 278 caracteres. Os primeiros 100 são `DR2 Viewer3D | llvmpipe (LLVM…) | GL 4.5 (Core Profile) Mesa …`; a ferramenta começa no caractere 103 e "não gravado" no 203.
  - CONFIRMED BY TEST (s7): o buffer de 512 bytes (`main.cpp:422`) cortou o título em `...s7.edits.json.1.`.
  - FACT: `--help` só lista as opções de linha de comando, não os controles (`main.cpp:76`).
  - FACT: o título só é atualizado a cada 0,5 s (`main.cpp:421`).
- **Arquivo.** `src/app/main.cpp:418-429`, `src/app/track_view.cpp:449-470`.
- **Causa.** A decisão do MVP de não ter ImGui (`docs/plans/viewer3d/README.md:17`).
- **Impacto.** Quem chega não sabe que existem as ferramentas 1/2/3, Ctrl+S, Tab ou F1–F4. O estado crítico ("não gravado", avisos) fica fora da vista.
- **Solução recomendada.** ImGui 1.91.9b com os backends SDL3 e OpenGL3:
  - barra de menus;
  - barra de ferramentas com os botões e o atalho no tooltip;
  - barra de status com ferramenta, seleção, visíveis/total, número de edições, "não gravado" em destaque, mensagem com prazo e fps;
  - janela de atalhos em F1, ou em "?".

  Encurtar o título para `<pista> - <rota>[*] - DR2 Viewer3D` (`*` = não gravado), com renderer e GL só na barra de status ou em Ajuda > Sobre.
- **Como testar.**
  - Captura com `--frames 30 --screenshot`: toolbar e status bar presentes.
  - Título com menos de 80 caracteres.
  - Depois de uma edição, `*` no título e "não gravado" na status bar no mesmo quadro.

### P1-2: sem Scene Tree nem busca

- **Problema.** Não há lista de objetos. Achar "a arquibancada" ou "a barreira 0003" exige navegar e clicar.
- **Evidência.**
  - FACT: o tipo `e:synth_spawn_marker` tem `count: 0` malhas e 2 instâncias na `route_0`. Com `ty.empty` ele não é desenhado nem selecionável (`instances.cpp: passes`), então essas 2 instâncias não podem ser editadas no nativo.
  - HYPOTHESIS: nas pistas reais haverá mais tipos sem malha (o `export.py:445` grava `count 0` quando não acha o nó).
  - FACT: o web também não tem lista de instâncias; a lista lateral dele é de pistas.
  - FACT: o Car Explorer tem árvore LOD → nó → fatia, com busca, olho, expandir e recolher.
- **Causa.** Não existe UI de painel.
- **Impacto.** Não dá para selecionar por nome, ver o que foi editado, ocultar um tipo inteiro (por exemplo as 534 barreiras) ou alcançar instâncias sem malha.
- **Solução recomendada.** Painel "Cena", à esquerda, 280 px:
  - **Árvore.** Pista, depois Rota (com o número de instâncias e um marcador de editada), depois Camada (Objetos `e:`/`o:`, Árvores `t:`, Terreno distante), depois Tipo (nome sem prefixo, `visíveis/total`, ícone de "sem malha"), depois Instância (`ens_ids[idnum]` para `e:`, `orn<idnum>` ou `tree<idnum>` para os demais, como `tvInstInfo`; sufixos "movida", "apagada", "cópia de N").
  - **Por tipo.** Olho para visibilidade só de exibição (não é edição): `std::vector<uint8_t> type_off` no `InstanceRenderer`, consultado em `passes()` e incluído na chave do `cull`. Cadeado contra seleção (por exemplo, travar as árvores).
  - **Busca.** Campo que filtra tipo, id de texto e índice.
  - **Filtros.** "só editados", "só apagados", "sem malha".
  - **Ligação com o viewport.** Clique seleciona, duplo clique enquadra, e selecionar no viewport rola a árvore até o item.
  - **Desempenho.** `ImGuiListClipper` nas instâncias. A Polônia tem cerca de 305 mil (FACT, `track_explorer.md`).
- **Como testar.**
  - Buscar "grandstand": 2 resultados; clique seleciona e duplo clique enquadra.
  - Olho em `e:synth_barrier~a`: visíveis caem em 534 no status.
  - Instância de `spawn_marker` selecionável na árvore, com Inspector mostrando posição e "sem malha".

### P1-3: objeto apagado não volta

- **Problema.** Depois de Delete, o objeto não pode mais ser selecionado. "Restaurar" (R) só vale para o selecionado, então o único caminho de volta é Ctrl+Z, desfazendo também tudo o que veio depois.
- **Evidência.** CONFIRMED BY TEST (s7): apagar o pórtico, clicar no mesmo ponto não seleciona nada, e R não muda nada (`hist 1/1`, `inst 929`). FACT: `passes()` exclui `hidden`.
- **Arquivo.** `src/render/instances.cpp: passes`, `src/app/track_view.cpp:281-286`.
- **Impacto.** Uma deleção de 50 passos atrás não tem conserto sem perder o trabalho feito depois dela.
- **Solução recomendada.** Filtro "apagados" na Scene Tree, com seleção e "Restaurar" no Inspector. Opção "mostrar apagados como fantasma" (desenho com `uHi` e alfa, sem picking por padrão). Ações "Restaurar todos os apagados do tipo X" e "Restaurar tudo", com passo de histórico, como no Car Explorer.
- **Como testar.** Apagar, fazer 3 outras edições, restaurar pela árvore: as 3 edições continuam e o histórico ganha 1 passo "Restaurar".

### P1-4: sem edição numérica nem snapping

- **Problema.** A posição só muda arrastando, o giro só com arraste ou ±15°/±90°, e não há grade nem passo.
- **Evidência.**
  - CONFIRMED BY TEST (s8): na vista geral (`frame_route`), arrastar a arquibancada 40 px moveu x de 58,76 para 114,84, ou seja 56 m.
  - FACT: o giro por arraste é de 0,01 rad/px, sem passo (`track_view.cpp:384`).
  - FACT: o web tem campos X/Y/Z (`tvSetPos`) e botões ±15°/±90°. O README do nativo lista isso como "ficou de fora".
- **Impacto.** Não dá para alinhar barreiras, pôr um objeto numa coordenada conhecida ou desfazer um empurrão de 1 px com precisão.
- **Solução recomendada.** No Inspector:
  - `DragFloat3`/`InputFloat3` de posição, com velocidade proporcional a `cam.dist` e commit só em `IsItemDeactivatedAfterEdit` (um passo de histórico por edição, não um por quadro).
  - Campo "Yaw (°)", derivado de `atan2` das linhas da matriz e aplicado com `edit::spin` a partir de `m0`.
  - Escala por eixo só leitura (comprimento das linhas: as árvores têm 1,16 a 1,3).
  - Na toolbar, snap de posição (0,1 / 0,5 / 1 m) e de ângulo (5° / 15°), com Ctrl invertendo o snap durante o arraste.
  - Na API do `TrackView`, `set_matrix(i, m, label)`, que faz o commit.
- **Como testar.** Digitar X = 60,00: a matriz muda só em `m[9]`, `hist +1` e o `edits.json` tem `m[9] = 60`. Com snap de 0,5 m, todo arraste termina em múltiplo de 0,5.

### P1-5: não troca de pista; rota só por Tab

- **Problema.** A pista é fixada por `--track` na linha de comando. Sem `--track`, abre o cubo de teste, sem caminho para uma pista. A rota só cicla com Tab e não há lista com contagens.
- **Evidência.**
  - FACT: `main.cpp:368-373`; `TrackView` é construído uma vez.
  - FACT: o web tem um combo de pista, um combo de rota (`route_0 · 1.011`) e a lista lateral com busca (`web_sel.png`).
  - FACT: `build/uiview/tracks/*/track.json` e `build/uiview/data/tracks.js` já servem de índice.
  - CONFIRMED BY TEST (s6): Tab troca de rota e o título mostra `route_1`, mas não "2 de 2" nem quais rotas têm edições.
- **Impacto.** Comparar pistas ou abrir outra exige fechar o programa, com o risco de P0-1 e P0-2.
- **Solução recomendada.**
  - "Arquivo > Abrir pista" com uma lista do `build/uiview/tracks/*/track.json` (id, país, rotas, instâncias), busca e recentes.
  - Sem `--track`, abrir essa janela em vez do cubo; o cubo fica para `--test-scene`.
  - Para trocar de pista: confirmar as edições não gravadas, construir o `TrackView` novo antes de soltar o atual (se lançar exceção, o atual fica) e trocar o `unique_ptr`.
  - Combo de rota na toolbar com `name · instâncias · *editada`.
- **Como testar.** Abrir sem argumentos: aparece a janela de pistas. Escolher a sintética abre `route_0`. Escolher uma pasta truncada mostra o erro e mantém a pista atual.

### P1-6: realce de seleção fraco; sem caixa, pivô ou gizmo

- **Problema.** A seleção é só uma mistura de 55% de laranja na cor (`track_shader.cpp:54`).
- **Evidência.**
  - CONFIRMED BY TEST: de perto o realce é claro (`s4_dup.png`, `s6_frame_tree.png`).
  - CONFIRMED BY TEST (s8, `s8_far_sel.png`): na vista geral, a arquibancada selecionada vira um borrão de poucos pixels. Árvores selecionadas de longe (1 a 3 px) não se distinguem.
  - INFERENCE: em objetos laranja ou vermelhos (cones, faixas das barreiras) o contraste cai.
  - FACT: não há caixa, eixo ou pivô desenhados. A cópia do Ctrl+D nasce 2 m em x, sobreposta ao original (`s4_dup.png`), e só o realce diz qual é qual.
- **Impacto.** O usuário não sabe o que está selecionado, onde fica o pivô do giro nem em que direção vai mover.
- **Solução recomendada.**
  - Caixa orientada do tipo (`ty.lo/hi` transformada pela matriz) em linhas, com `RouteLines` ou um `LineBatch` novo de cor fixa, desenhada também sem teste de profundidade em tom mais fraco.
  - Marcador de pivô.
  - Um rótulo fixo na tela em `ImGui::GetForegroundDrawList()` com o nome, quando o objeto está pequeno.
  - Na fase C, um gizmo de translação nos eixos X/Z, Y e plano XZ, e um anel de yaw. Pode ser desenhado com a drawlist do ImGui projetando pontos 3D, como faz `carGizmoFrame`/`carview.js:785+`, ou com ImGuizmo se a dependência for aceita.
- **Como testar.** Captura com seleção na vista geral: caixa visível, mais de 20 px, nas cores esperadas. `xdotool` arrastando a seta X: só `m[9]` muda.

---

## P2

### P2-1: errar o objeto em Mover/Girar orbita a câmera

- **Evidência.** CONFIRMED BY TEST (s2): com a ferramenta 2, um arraste que começou a poucos pixels da barreira orbitou a câmera (`s2_moved.png`) e não moveu nada. FACT: `begin_edit` usa o objeto sob o mouse, não o selecionado; arrastar outro objeto o seleciona e edita.
- **Arquivo.** `main.cpp:317-321`, `track_view.cpp:344-360`.
- **Impacto.** A vista gira sem querer (desorienta) e objetos vizinhos são editados por acidente.
- **Solução recomendada.** Com o gizmo, só as alças movem. Fora delas, clique seleciona e arraste faz seleção por retângulo (fase C) ou nada. Orbitar fica no botão do meio, ou Alt + esquerdo, como no Blender e no Unreal, sem tirar o botão direito, que hoje é pan.
- **Como testar.** Ferramenta Mover, arrastar no vazio: yaw, pitch e alvo da câmera inalterados.

### P2-2: status nunca expira e contradiz o estado

- **Evidência.** CONFIRMED BY TEST (s6): depois de gravar e editar de novo, o título diz `... | não gravado | ... | gravado build/uiview/saves/... (1 edições)`. "1 edições" está sem concordância. FACT: `status_` só muda em `set_status`.
- **Solução recomendada.** Status com hora e prazo (por exemplo 6 s) na status bar, um painel "Log" com as últimas 50 mensagens e plural correto.
- **Como testar.** Gravar, editar e esperar 6 s: a status bar mostra só "não gravado".

### P2-3: ações recusadas sem feedback

- **Evidência.**
  - CONFIRMED BY TEST (s6): Ctrl+D com uma bétula (`t:`) selecionada não faz nada e não avisa.
  - FACT: Q, E, R e Delete sem seleção, e Tab durante um arraste, também retornam em silêncio (`track_view.cpp:262, 282, 289, 336`).
  - FACT: o web esconde o botão Duplicar para `o:`/`t:` e tem a string `trk.dupbin` ("Duplicar só vale para objetos de objects.ens."), que não é usada.
- **Solução recomendada.** Botões desabilitados, com tooltip do motivo, e uma mensagem na status bar quando o atalho é recusado.

### P2-4: `--out` padrão relativo ao diretório atual

- **Evidência.** CONFIRMED BY TEST (s6): rodando em `$S/cwdtest` sem `--out`, o Ctrl+S gravou `$S/cwdtest/build/uiview/saves/synthetic__dr2hook_ring.edits.json`, e o título mostrou o caminho relativo.
- **Arquivo.** `track_view.cpp:142`.
- **Impacto.** Gravações espalhadas e "gravei, mas onde?".
- **Solução recomendada.** Resolver o caminho pela pasta da pista (`<dir>/../../saves/<id>.edits.json`, já que as pistas ficam em `build/uiview/tracks/<id>`) e mostrar sempre o caminho absoluto. Adicionar "Gravar como…" e "Abrir pasta".

### P2-5: atalhos conflitantes e inconsistentes

FACT para o mapa atual, e INFERENCE para o efeito no usuário:

| Tecla | Nativo hoje | Web Track | Car Explorer | Blender / Unreal | Risco |
| --- | --- | --- | --- | --- | --- |
| Esc | tira a seleção, senão **sai** | tira a seleção | tira a seleção | cancela | P0-1 |
| R | **Restaurar** ao arquivo | — | — | girar / escala | apaga a edição em silêncio (dá para desfazer) |
| Q / E | girar −/+15° | — | ferramentas Navegar / Girar | Unreal: descer / subir no voo | girar sem querer |
| W | andar | andar | ferramenta Mover | Unreal: ferramenta Mover | aceitável se as ferramentas ficarem em 1/2/3 |
| G | portões | — | — | Blender: mover | um usuário do Blender esconde os portões |
| H | — | **apagar** (a dica diz "H oculta") | ocultar | ocultar | o web tem um bug de rótulo; não copiar |
| F1 | terreno | — | — | ajuda | — |
| Tab | próxima rota | — | — | Blender: modo de edição; ImGui: navegação | conflito quando houver campos de texto |
| 1/2/3 | ferramentas | ferramentas | — | — | consistente com o web, não com o carro |

- INFERENCE (não testado): num teclado AZERTY, a tecla física A gera o scancode A (andar à esquerda, `walk_keys`) e o keycode Q (girar −15°, `key()`). Andar com algo selecionado giraria o objeto. O ABNT2 do dono é QWERTY e não tem o problema.
- **Solução recomendada.** Mapa proposto:

  | Ação | Teclas |
  | --- | --- |
  | Ferramentas | 1/2/3, como no web, mostradas na toolbar. Não usar W/E/R, que já são andar e girar |
  | Girar 15° | `,` e `.` (ou Q/E só com seleção e ferramenta Girar ativa) |
  | Restaurar | Ctrl+R, ou só o botão do Inspector |
  | Ocultar ou mostrar (exibição, não edição) | H; Alt+H mostra tudo |
  | Apagar | Delete ou X |
  | Enquadrar seleção | F; Home enquadra a rota |
  | Rota | Ctrl+Tab / Ctrl+Shift+Tab, ou PageDown/PageUp |
  | Ajuda e atalhos | F1; camadas saem das teclas F e vão para o menu Exibir, com Alt+1..6 opcionais |
  | Arquivo | Ctrl+O abre a pista, Ctrl+S grava, Ctrl+Shift+S grava como, Ctrl+Q sai |

  Ler os atalhos de letra por scancode, ou exibir na ajuda a tecla real (`SDL_GetKeyName`).
- **Como testar.** Teste de tabela no `edit_test` ou num `ui_test` (tecla → ação). `xdotool`: R não muda a matriz e Ctrl+R restaura.

### P2-6: camadas e distância de desenho sem indicador

- **Evidência.** CONFIRMED BY TEST (s6): F1 desliga o terreno (`s6_f1.png`, fps de 50 para 315) e o título continua igual. F2–F4, G e I também não aparecem. A distância só aparece como `700 m`. O web tem checkboxes e um slider na toolbar.
- **Solução recomendada.** Menu Exibir e checkboxes na toolbar, mais um slider de distância de 100 a 4000 m. Os campos `show_*`, `layers_` e `draw_dist_` precisam de getters e setters no `TrackView`.

### P2-7: seleção atravessa o terreno

- **Evidência.** CONFIRMED BY TEST (s5, `s5_under_sel.png`): com a câmera sob o relevo, o clique selecionou `synth_pine` 7, invisível atrás do terreno. INFERENCE: o mesmo vale para objetos atrás de um morro, porque `pick()` só testa a caixa dos objetos (`pick.cpp:324-358`) e o terreno não entra no picking.
- **Impacto.** Com Mover, um arraste pode pegar um objeto escondido e editá-lo sem o usuário ver.
- **Solução recomendada.** Ler a profundidade no pixel do clique (`glReadPixels(GL_DEPTH_COMPONENT)` antes do ImGui, ou no quadro seguinte), desprojetar e recusar acertos de caixa mais distantes que essa profundidade mais uma margem. Não precisa de cópia do terreno na CPU.
- **Como testar.** Repetir s5: o clique não seleciona nada.

### P2-8: histórico invisível e por rota

- **Evidência.**
  - FACT: `History::entries()` já guarda rótulos ("Mover", "Girar", "Apagar"…), mas nada os mostra. O título mostra `hist p/n` só da rota atual.
  - CONFIRMED BY TEST (s6): depois de Tab, `hist 0/0` na `route_1` e "não gravado" global, sem dizer onde estão as edições.
  - FACT: o Car Explorer tem painel de histórico clicável.
- **Solução recomendada.** Painel "Histórico" com uma lista clicável por rota (`hist_go(pos)`) e o total de edições por rota.

### P2-9: sem seleção múltipla

- **Evidência.** FACT: `sel_` é um `int`. O web também não tem seleção múltipla. A rota 0 tem 534 barreiras, e alinhar ou mover uma fileira exige dezenas de arrastes. FACT: `edit::Snap` e `History::commit` já aceitam vários índices.
- **Solução recomendada.** Fase C: `std::vector<uint32_t> sel` com Ctrl+clique, retângulo e "selecionar todos do tipo" na árvore. Mover e girar em grupo (pivô no centro) com um único passo de histórico.

### P2-10: Mover não acompanha o relevo

- **Evidência.** FACT: `edit_drag` fixa `m[10] = base[10]` e arrasta no plano horizontal da altura original (`track_view.cpp:372-381`). INFERENCE: um objeto levado morro acima entra no chão, e um levado morro abaixo fica flutuando. O web faz igual.
- **Solução recomendada.** Opção "Assentar no chão" (tecla End, ou um checkbox no snap) que usa a profundidade do terreno, como em P2-7, ou um raio contra uma cópia da malha na CPU (hoje descartada em `load_route`, `track_view.cpp:172-182`).

### P2-11: erros só no stderr; falha ao abrir fecha o programa

- **Evidência.** CONFIRMED BY TEST: `--track /nao/existe` imprime `não abriu /nao/existe/track.json` e sai com rc=1. Texturas que falham só aparecem no stderr. Quem abre pelo gerenciador de arquivos não vê nada.
- **Solução recomendada.** Com ImGui, falha ao abrir mostra um modal e a janela de pistas. Avisos (texturas, contagens divergentes, `inst_*.bin` diferente do `track.json`) vão para o painel Log e para um contador na status bar.

---

## P3

- **P3-1: câmera.**
  - FACT: o zoom vai sempre ao alvo, não ao cursor.
  - FACT: WASD só muda `target.x/z`; o `target.y` fica fixo.
  - CONFIRMED BY TEST (s5): com `pitch` −0,2 a câmera vai para baixo do terreno.
  - FACT: não há modo voo (botão direito + WASD/QE, como no Unreal), duplo clique para focar nem marcadores de câmera.
  - **Solução recomendada.** Zoom para o ponto sob o cursor (usando a profundidade), alvo assentado no chão ao andar, mínimo de altura do olho e Ctrl+1..9 para guardar e recuperar vistas.
- **P3-2: identificação.** FACT: o título mostra `idnum` (`546`) e `kind e`. O web mostra `id synth_start_arch_0546` (de `ens_ids`) e "objeto (objects.ens / ornaments.bin)" (`web_sel.png`). Os `ens_ids` já são lidos em `track.cpp:54`, mas a app não os usa. **Solução recomendada.** Mostrar o id de texto e o arquivo de origem por extenso.
- **P3-3: F pela origem.** CONFIRMED BY TEST (`s6_frame_tree.png`): o pinheiro enquadrado fica acima do centro, porque o alvo é `m[9..11]`, a base. **Solução recomendada.** Usar o centro da caixa transformada.
- **P3-4: materiais e texturas.** FACT: `InstanceRenderer::Part` guarda `material` por malha, `track.materials` mapeia material para `tex/*.webp`, e `TextureCache::for_material` devolve um `GLuint`. Isso dá para usar direto em `ImGui::Image((ImTextureID)(intptr_t)tex, …)`. FACT: `track.json` ainda tem `textures.wanted/found` e `terrain.meshes/verts`, por rota; não tem LOD nem câmeras. **Solução recomendada.** Seção "Tipo / Materiais" no Inspector: malhas, material, miniatura, caminho e "sem textura (cor fixa)". Uma aba "Materiais da pista" com as 15 entradas e as que falharam.
- **P3-5: seleção perdida.** CONFIRMED BY TEST (s4): Ctrl+Z que desfaz um Delete não devolve a seleção. Tab também tira a seleção. **Solução recomendada.** Guardar a seleção por rota e reselecionar o objeto que o undo trouxe de volta.
- **P3-6: cursor e latência.** FACT: não há `SDL_SetCursor`; o web troca o cursor por ferramenta (`move`, `ew-resize`). O título leva até 0,5 s para refletir uma ação. FACT: `unsaved()` serializa o JSON inteiro de todas as rotas abertas a cada atualização do título (INFERENCE: custo linear em pistas de cerca de 305 mil instâncias). **Solução recomendada.** Cursor por ferramenta e por alça. Um contador de revisões (`edit_rev_` contra a revisão gravada) em vez de comparar strings.
- **P3-7: linhas.** FACT: portões e IA só da rota atual (o README já admite). O `progress.routes[].splits` (tipo e portão dos splits) é exportado e nunca aparece.

---

## Paridade com o Track Explorer web, item a item

| Recurso | Web | Nativo e2297b2 |
| --- | --- | --- |
| Escolher pista (combo e lista com busca) | sim | não (`--track`) |
| Escolher rota (combo com contagem) | sim | só Tab, sem lista |
| Camadas (6 checkboxes) | sim, visíveis | F1–F4, G, I, sem indicador |
| Distância de desenho (slider com valor) | sim | `[` e `]`; valor no título |
| Enquadrar a pista (botão) | sim | F sem seleção |
| Ferramentas (ícones, tooltip com atalho, cursor) | sim | 1/2/3, sem indicação na tela |
| Desfazer/refazer (botões com estado desabilitado) | sim | só atalhos; `hist p/n` no título |
| Gravar | "Salvar .nefs" (servidor) e "Exportar edições" | Ctrl+S, `edits.json` atômico com `.bak` (melhor que o web) |
| Inspector da pista (terreno, rotas, objetos, tipos) | sim | não |
| Inspector da seleção (id de texto, tipo, origem, malhas) | sim | parcial, no título (`idnum`, `kind`) |
| X/Y/Z editáveis | sim | **não** |
| ±15°/±90° | botões | teclas Q/E (Shift = 90°) |
| Duplicar, Apagar, Restaurar | botões | Ctrl+D, Delete, R |
| Aviso de colisão (`trk.collision`) | sim | não |
| Número de edições | sim ("N objetos alterados") | não; só "não gravado" |
| Status bar (blocos, instâncias desenhadas, tipos, seleção) | sim | no título |
| Seleção múltipla, Scene Tree, gizmo, snapping | não | não |
| Recarregar edições | não | não |
| Confirmação ao sair com edições | não (a página perde tudo ao fechar) | aviso no título; Esc, Esc sai |
| Esc | tira a seleção | tira a seleção ou **sai** |
| H | apaga (a dica diz "oculta") | — |

O nativo já é melhor que o web em três pontos: gravação atômica com `.bak`, F sem seleção enquadra a rota, e aviso antes de sair. O web é melhor em todo o resto da descoberta e da edição precisa.

---

## Proposta de layout (ImGui 1.91.9b, sem recriar o renderer)

FACT: a tag 1.91.9b, em `/home/user/ocornut/imgui` e em `third_party/imgui`, **não tem docking** (nenhum `DockSpace` em `imgui.h`). O layout deve ser fixo, calculado por quadro a partir de `ImGui::GetMainViewport()`, com `SetNextWindowPos/Size` e as flags `NoMove | NoCollapse`. Larguras ajustáveis podem vir de um splitter próprio (`InvisibleButton`, como o `makeResizer` do Car Explorer), guardadas no `imgui.ini` ou num ini próprio.

```
+--------------------------------------------------------------------------------------+
| Arquivo  Editar  Exibir  Seleção  Ajuda                              (MainMenuBar)    |
+--------------------------------------------------------------------------------------+
| [Pista v][Rota: route_0 · 1011 *][Nav 1][Mover 2][Girar 3] | Snap [x]0,5 m [x]15° |    |
| [Desfazer][Refazer] [Gravar*] | Terreno Obj Árv Dist Portões IA | Dist ===o=== 700 m |  |
+----------------+---------------------------------------------------+-----------------+
| CENA (280 px)  |                                                   | INSPECTOR       |
| [buscar...]    |                                                   | (320 px)        |
| [ ]editados    |          VIEWPORT 3D                              | > Pista / Rota  |
| [ ]apagados    |   glViewport = retângulo central                  | > Seleção       |
| v route_0 *    |   caixa da seleção, pivô, gizmo (fase C)          |   id, tipo,     |
|   v Objetos    |   HUD canto: ferramenta e snap                    |   origem, cópia |
|     > barrier  |                                                   | > Transformação |
|       534/534  |                                                   |   X Y Z, Yaw,   |
|     > grandst. |                                                   |   escala (ro),  |
|   > Árvores    |                                                   |   matriz m/m0   |
|   > Distante   |                                                   | > Tipo/Materiais|
| > route_1      |                                                   | > Ações         |
|                |                                                   +-----------------+
|                |                                                   | HISTÓRICO / LOG |
+----------------+---------------------------------------------------+-----------------+
| Mover | synth_grandstand (547) | 930/1011 visíveis | 3 edições, NÃO GRAVADO | msg | 36 fps |
+--------------------------------------------------------------------------------------+
```

### Prioridades

- **Fase A** (P0 e P1, desbloqueia o uso; sem gizmo):
  1. Esc sem sair, mais o modal de saída (P0-1). Pode ser feito antes do ImGui.
  2. Carregar `edits.json` e perguntar se continua (P0-2).
  3. Status bar e título curto (P1-1).
  4. Toolbar: ferramentas, desfazer/refazer, gravar, combo de rota, camadas e distância (P1-1, P1-5, P2-6).
  5. Inspector com o id de texto, X/Y/Z, Yaw, botões ±15/±90, Duplicar, Apagar e Restaurar (com tooltips), e o aviso de colisão (P1-4, P2-3, P3-2).
- **Fase B:**
  1. Scene Tree com busca, filtros, olho e cadeado por tipo, e apagados (P1-2, P1-3).
  2. Janela de abrir pista (P1-5).
  3. Caixa da seleção e pivô (P1-6).
  4. Snapping (P1-4).
  5. Painel de histórico e log (P2-8, P2-2, P2-11).
  6. `--out` absoluto (P2-4).
  7. Novo mapa de atalhos e F1 com a ajuda (P2-5).
- **Fase C:**
  1. Gizmo (P1-6, P2-1).
  2. Picking com profundidade (P2-7) e assentar no chão (P2-10).
  3. Seleção múltipla (P2-9).
  4. Materiais e texturas (P3-4).
  5. Câmera: zoom ao cursor, voo e marcadores (P3-1).

### Pontos de integração (para não quebrar o que funciona)

1. **Entrada.** Chamar `ImGui_ImplSDL3_ProcessEvent` antes de `handle_event`.
   - Ignorar mouse quando `io.WantCaptureMouse`, exceto um arraste que começou no viewport.
   - Ignorar teclas quando `io.WantCaptureKeyboard`.
   - **`walk_keys` lê `SDL_GetKeyboardState` direto** e precisa do mesmo bloqueio. Sem isso, digitar "s" num campo anda a câmera e **Delete num campo apaga o objeto** (INFERENCE pelo código, `main.cpp:310, 350-357`).
2. **Viewport.**
   - Desenhar o 3D no retângulo central: `glViewport` e `glScissor` com o retângulo, aspecto do retângulo.
   - Chamar `mouse_ray(cam, x - rx, y - ry, rw, rh)`, porque a função já recebe w e h.
   - Em HiDPI, o mouse vem em coordenadas de janela e o viewport em pixels (`SDL_GetWindowSizeInPixels`); o ImGui usa `DisplayFramebufferScale`.
   - `F` e `frame_route` passam a enquadrar no retângulo visível.
3. **Capturas.** `--frames` e `--screenshot` precisam de `--no-ui`, ou de capturar antes do ImGui, para as comparações byte a byte com o web (etapas 3 a 6) continuarem válidas.
4. **Estado GL.** O `imgui_impl_opengl3` guarda e restaura o estado GL (FACT da versão 1.91.x, conferir na integração). Mesmo assim, reativar `GL_DEPTH_TEST` e `glDepthFunc(GL_LESS)` no começo de cada quadro 3D. O loader próprio do backend convive com o GLEW (HYPOTHESIS, testar `glGetError` depois do primeiro quadro).
5. **Fonte.** HYPOTHESIS: a fonte padrão (ProggyClean) pode não ter os glifos de "ã", "ç", "é". Testar "edições não gravadas" numa captura e, se faltar, carregar uma TTF com a faixa Latin-1.
6. **Texto.** Os campos de texto no SDL3 precisam de `SDL_StartTextInput`; o backend cuida quando `WantTextInput`. Testar digitando "60.5" num campo X com `xdotool type`.
7. **API do `TrackView`** que falta:
   - `select(int)` e seleção em vetor;
   - `set_matrix(i, m, label)`, que faz o commit;
   - `hist_go(pos)` e acesso às entradas do histórico por rota;
   - getters e setters de camadas, `show_*`, `draw_dist_` e visibilidade e trava por tipo;
   - `edit_count()` por rota;
   - `load_edits(path)`;
   - `route_index()` e `switch_to(index)`.

   Abrir outra pista é trocar o `unique_ptr<TrackView>`, construindo o novo antes de soltar o atual.

## Arquivos desta avaliação

- Relatório: `$S/avaliacao.md`
- Roteiros: `$S/drv.py`, `$S/s1.py` a `$S/s8.py`, `$S/webshot.js`
- Logs e saídas: `$S/out/` (`s7.edits.json` e `s7.edits.json.1.bak` são a evidência de P0-2; `cwdtest/build/uiview/saves/…` é a de P2-4)
- Capturas: `$S/shots/`
  - `initial.png`: nenhuma UI
  - `s4_dup.png`: realce de perto, cópia sobreposta
  - `s5_under_sel.png`: seleção através do terreno
  - `s6_frame_tree.png`: F pela base
  - `s8_far_sel.png`: realce invisível de longe
  - `s2_moved.png`: arraste que errou e orbitou
  - `web_sel.png`: painéis do web
