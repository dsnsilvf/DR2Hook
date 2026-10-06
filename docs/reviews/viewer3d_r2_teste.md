# viewer3d rodada 2: teste e revisão (Agente 3)

Commit testado: 07275a7, uma cópia exata em `r2test/src`. "Antes" é e2297b2, em `r2test/before`. Compilei em Release (`build/`), em ASan+UBSan Debug (`build-asan/`) e o antes em Release (`build-before/`). Tudo rodou no llvmpipe sob o Xvfb :96. A máquina tinha 4 núcleos e load de 3,6 a 5,3 por causa dos outros agentes, então os FPS têm ruído.
Os testes de interface foram feitos com xdotool, com scripts em `r2test/t/t*.py`. As capturas estão em `r2test/t/s*.png`.
Rótulos usados: **FACT** (li no código), **CONFIRMED BY TEST**, **INFERENCE**, **HYPOTHESIS**.

## Resumo

A rodada 2 cumpre quase tudo o que promete. Não encontrei regressão da rodada 1. Mas encontrei **um P1 de corrupção de dados** com uma sequência natural: digitar num campo do Inspector e, sem Enter, clicar em outro objeto. Também encontrei 5 P2:
- `inf` vira JSON inválido.
- Retomar o edits.json aplica só parte das edições quando falha.
- Reabrir a pista pelo menu perde o `--out`.
- A mensagem de erro de abrir pista fica presa na barra de status.
- Clicar no centro do objeto pega o eixo Y do gizmo.

Há também vários P3 de UX.

## Bugs novos

### P1-1: um valor digitado no Inspector vai parar em outro objeto, e a edição fica sem histórico (CONFIRMED BY TEST)
**Sequência:** selecionar a barreira #18, Ctrl+clique no X do Inspector, digitar `200` **sem Enter**, clicar em outra barreira (#16) no 3D.
- **Com Navegar** (`t10.py`): a #18 ficou com x=200, mas recebeu a **rotação e o Y/Z da #16**. A matriz gravada foi `[0.99907,0,-0.04304,…, 200,102.1298,-253.708]`, que é a rotação e o y/z da #16. O giro de 45° que eu tinha feito na #18 sumiu. O histórico registrou "Mover (campo)" com a matriz corrompida.
- **Com Mover** (`t9.py`): a **#16** foi para x=200 (passo de histórico 1/1). A #18 também ficou em x=200 **sem passo de histórico**. Ctrl+Z desfaz só a #16. A #18 com x=200 foi gravada no edits.json e não há como desfazer.
- **Causa (FACT):**
  - Os campos do Inspector têm o mesmo ID do ImGui qualquer que seja a seleção (`##pos`, `##yaw`, em `ui.cpp:506,521`). No quadro seguinte ao clique, o campo de texto ainda ativo reaplica o buffer "200" à nova seleção. `ui.cpp:508-516` copia a matriz **atual da nova seleção** (`m`) e chama `set_matrix`, que escreve em `drag_.i`, a seleção antiga.
  - `TrackView::click` (`track_view.cpp:394`) e `begin_edit` (`track_view.cpp:340`) não olham `drag_.active`. `begin_edit` **sobrescreve** a mudança numérica aberta: `drag_ = EditDrag{}` descarta o `before` da #18.
  - `gizmo_press` falha nesse estado porque `begin_change` recusa, e o código cai em `begin_edit`.
- **Correção mínima:**
  1. Em `inspector()`, `ImGui::PushID(i)` antes dos campos de transformação, para que trocar a seleção gere outro ID e desative o campo antigo.
  2. Em `click`/`begin_edit`/`select`, se `drag_.numeric`, chamar `end_change` antes de trocar (ou recusar o clique).
  3. Em `set_matrix`, receber o índice e ignorar se for diferente de `drag_.i`.

### P2-1: digitar `1e39` grava `inf` e o edits.json fica inválido (CONFIRMED BY TEST)
- **Teste** (`t16.py`): Ctrl+clique no Y, digitei `1e39` e Enter. O título mostrou `72.31 inf -254.04`. Depois do Ctrl+S, o arquivo tem `...,72.3081131,inf,-254.038681]`. `json.load` falha com "Expecting value: line 2 column 160". O próprio viewer não consegue retomar esse arquivo e o `edit.py` não o aplica.
- `nan` digitado foi rejeitado (sem mudança).
- **Causa (FACT):** o ImGui usa `sscanf("%f")` (`imgui_widgets.cpp:2314`). `edits_json.cpp:20` (`floats`) escreve `%.9g` sem checar se o número é finito.
- **Correção:** em `ui.cpp`, recusar valor não finito ou fora de ±1e6 antes de `set_matrix`. Em `edits_json`, lançar erro ou pular entradas não finitas (defesa em profundidade). `apply_edits` também aceita valores enormes (`huge.json`: `1e300` foi aceito como inf).

### P2-2: retomar aplica só parte quando uma rota falha, e a mensagem diz o contrário (CONFIRMED BY TEST)
- **Teste:** edits.json com entradas em route_0 e route_1.
  - Na pista `r1/bad/inst1_trunc` (inst_route_1.bin truncado): a mensagem foi "não leu …: DR2I: n maior que o arquivo; **começando sem edições**". Mas o título mostra `inst 995/1 012 | não gravado`: as edições da route_0 (a #18 movida e a cópia) **foram aplicadas**.
  - O mesmo acontece quando uma entrada da route_1 não tem a chave `"m"` (`missing_m.json`).
  - Um Ctrl+S nesse estado sobrescreve o arquivo só com a route_0. A route_1 só sobra no `.bak`.
  - A cópia acrescentada não aparece (995 visíveis): `regroup` não foi chamado.
- **Causa (FACT):** em `track_view.cpp:66-93`, `resume_edits` aplica direto em `inst_`, e o `catch` não desfaz. `apply_edits` usa `e["m"]`, `e["m0"]` e `doc["edits"]` (`edits_json.cpp:99,107`), e o `operator[]` **lança exceção** quando a chave falta (`json.cpp:236`). Assim, uma entrada ruim derruba tudo em vez de virar "entrada incompleta".
- **Correção:** aplicar em cópias (`Instances tmp = inst_` e um mapa das outras rotas) e só trocar no fim. Usar `find()` para `m`/`m0`/`edits`. Ler uma rota que falha deve virar `skipped`, não exceção.

### P2-3: reabrir pelo menu a pista da linha de comando perde o `--out` (CONFIRMED BY TEST)
- **Teste** (`t7c.py`): `--track /home/user/DR2Hook/build/uiview/tracks/synthetic__dr2hook_ring --out om.json` (caminho absoluto), com uma edição gravada em om.json. Abri `synthetic__copia_x` pelo menu e depois `synthetic__dr2hook_ring` de novo. O Ctrl+S gravou em `<cwd>/build/uiview/saves/synthetic__dr2hook_ring.edits.json (0 edições)`: om.json não foi retomado nem usado.
- Além disso, o menu **não marca a pista atual** (as duas aparecem habilitadas). A comparação é textual: `ui.cpp:213` e `main.cpp` `open_track` (`dir == opt.track`).
- **Correção:** comparar com `std::filesystem::equivalent`/`weakly_canonical` nos dois lugares. Também `opt.fresh` nunca vale no menu (P3, coerente com o README).

### P2-4: a mensagem do painel fica presa e esconde "NÃO GRAVOU" (CONFIRMED BY TEST)
- **Teste** (`t17.py`): abri pelo menu uma pista quebrada (link para `r1/bad/inst0_trunc`). A barra mostrou "não abriu …: DR2I: n maior que o arquivo", em vermelho. Em seguida dei Ctrl+S com `--out` inválido. A barra **continuou** com "não abriu …", e "NÃO GRAVOU" só saiu no terminal e no título (`s17st.png`).
- **Causa (FACT):** em `ui.cpp:636-639`, `message_` tem prioridade e é considerada sempre "fresca". Só é limpa quando uma abertura dá certo.
- **Correção:** guardar a hora de `message_` e mostrar a mensagem mais nova entre `message_` e `track->status()`, ou limpar `message_` quando `status_age()` for menor que a idade dela.

### P2-5: clicar no centro do objeto com Mover pega o eixo Y do gizmo (CONFIRMED BY TEST)
- **Teste** (`t11.py`): com Mover, arrastei da origem (846,556) **para a direita** e o objeto **subiu**: y passou de 102.25 para 103.04. Na captura `s0b.png`, o eixo Y aparece realçado com o mouse sobre o centro do objeto.
- **Causa (FACT):** `gizmo_hit` (`ui.cpp:751-755`) mede a distância aos segmentos origem→ponta, com raio de 9 px. Perto da origem, sempre pega algum eixo. O README diz que arrastar o objeto move no chão. Para objetos pequenos ou distantes, o objeto inteiro fica dentro da zona do gizmo.
- **Correção:** ignorar os primeiros ~12 px de cada eixo (começar o segmento em `origin + 0.25*(tip-origin)`), ou dar prioridade ao `pick` do objeto quando o clique está a menos de N px da origem.

### P3
1. **Esc fecha o combo e também tira a seleção** (CONFIRMED BY TEST, `t17.py`): com o combo "Encaixe" aberto, o Esc fechou o combo e deselecionou a #18. O teclado não fica capturado sem campo ativo (`ui.cpp:119-121`). Correção: tratar como `ui_took` quando `ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)`.
2. **Não dá para rolar o Histórico estando no último passo** (CONFIRMED BY TEST, `s13all.png`): a 25/25, a roda não rolou. A 22/25, rolou. Causa: `ui.cpp:439` chama `SetScrollHereY(1.0f)` em **todo** quadro em que `pos==size`. Correção: chamar só quando `size` muda.
3. **Textos cortados** (CONFIRMED BY TEST, capturas):
   - "Grava em /tmp/claude-0/-home-user-DR2Hook/" no Inspector sem seleção (a informação útil some e não há tooltip).
   - "534 instância(s) nesta ro…" e "Clique num objeto (no 3D ou na Cena) para inspeci…".
   - Na janela Atalhos, "orbitar (Navegar ou fora de objeto); clique sel…".
   - Com a janela em 900×560, a barra de ferramentas transborda (o slider Distância some) e o texto da barra de status **se sobrepõe** a "inst …/… · fps" (`s15ac.png`).
   - Correção: `TextWrapped` ou tooltip com o caminho completo, `PushTextWrapPos` e cortar o texto da esquerda pela largura disponível.
4. **Os campos X/Y/Z não têm rótulo**: são três caixas numéricas sem "X Y Z" (`s0b.png`). Só o tooltip explica.
5. **`std::clamp` com lo>hi** quando a janela tem menos de ~445 px (`ui.cpp:182-183`: `clamp(right_w_, 200, W*0.45)`). Pela norma é comportamento indefinido, mas a libstdc++ devolve um dos limites. Testei 400×300 no ASan e o resultado foi sem erro e sem queda; o viewport fica com uns poucos px. Correção: `std::min(max_w, std::max(min_w, v))`.
6. **Tab entre os campos X→Y gera dois passos** ("Mover (campo)" ×2). É aceitável e coerente com "um passo por edição".
7. **O encaixe do giro é relativo e o do mover é absoluto.** Testei com yaw inicial de 2,29° e passo de 15°, e o resultado foi 17,29° / −87,71°. Não é bug, mas convém dizer no tooltip.
8. **O "(arquivo aberto)" do Histórico depois de retomar** volta ao estado retomado, não ao arquivo exportado. O rótulo engana. Sugestão: "(início da sessão)".
9. **`apply_edits` trunca índice fracionário** (`18.7` vira 18) e aceita `edits` sendo objeto em silêncio ("retomadas 0 edições"). Também aceita várias entradas para o mesmo índice (a última vence) e conta todas em "retomadas N". Quando há entradas de fora, `saved_text_` vira o estado parcial (`track_view.cpp:87`): sair não pergunta nada, e o próximo Ctrl+S apaga as entradas de fora (há `.bak`).
10. **Esc no filtro da Cena** apaga o que foi digitado: o ImGui reverte o texto ao valor da ativação.
11. **Teclas 1/2/3 durante um arraste** (do mouse ou do gizmo) trocam a ferramenta: `key()` muda `tool_` direto, e só `set_tool` tem a guarda. Durante o gizmo, o anel aparece com os eixos ainda presos. **Esc durante um arraste** tira a seleção, mas o objeto continua sendo movido.
12. **O tamanho do gizmo é recalculado a cada quadro** (`world_len`, `ui.cpp:835`) enquanto se arrasta. Se o objeto se aproxima ou se afasta da câmera, a escala de `t` muda e o arraste dá pequenos saltos (INFERENCE, não medi).
13. **O menu só procura `build/uiview/tracks` relativo à pasta atual** (`ui.cpp:147`). Rodando de outra pasta, o menu fica vazio mesmo com a pista aberta dentro de `<raiz>/tracks/`. Sugestão: procurar também na pasta-mãe da pista atual.

## Regressões da rodada 1
Testei tudo abaixo e não encontrei regressão.

| Item | Resultado |
| --- | --- |
| camera_test, edit_test (123), core_tests --track (61), Release e ASan | OK (CONFIRMED BY TEST) |
| 26 pistas corrompidas `r1/bad/*` com --frames 3: código de saída antes × depois (ASan) | idênticos em todas; 0 erro de sanitizer |
| Gravar com `--out /proc/nao/pode.json` | "NÃO GRAVOU" em vermelho e continua aberto; "Gravar e sair" com falha fecha o modal e não sai (CONFIRMED BY TEST) |
| Desfazer/refazer (Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z), duplicar, Q/E/Shift+E, Delete, restaurar | passos de histórico certos (t1.py) |
| F1/F2/F3/F4/G/I, WASD | o 3D muda (F2: visíveis 935→356→936); W move a câmera |
| Tab/Shift+Tab com edições nas duas rotas, gravação, `edit_roundtrip.py` | OK (2/2 e 4/4 edições conferidas) |
| Tab numa rota quebrada | coberto pelas pistas `inst1_*`/`route1_*` no --frames; com o menu, a rota atual continua intacta |

## Itens da rodada 2

| Item | Veredito |
| --- | --- |
| Árvore: filtro, seleção, duplo clique enquadra, apagadas em cinza e selecionáveis, Restaurar | **OK** (t5, t6, t14). Digitar no filtro não aciona atalhos (o 3D ficou idêntico e o histórico não mudou). Ressalvas P3-10. |
| Inspector: X/Y/Z e giro por arraste e digitados | **Funciona, com o P1-1 e o P2-1.** Um passo por edição: arraste 1, digitado 1, clique sem mudar 0. |
| Digitar num campo não aciona atalhos (q, w, 1, 2, e, Delete, Tab, F10) | **OK** (CONFIRMED BY TEST) |
| Gizmo X/Y/Z e anel | **OK**: só o eixo muda e há um passo por arraste. Ressalva P2-5. |
| Encaixe 0,5 m / 15° (arraste no chão, gizmo, anel, girar por arraste) | **OK** (76,0; 73,0/−260,5; Δ −90°; Δ 15°) |
| Histórico clicável (voltar ou avançar, futuros em cinza) | **OK**, com o P3-2 |
| Abrir outra pista pelo menu, com modal "Abrir sem gravar" | **OK**. A falha mantém a pista atual. Ressalvas P2-3 e P2-4. |
| Modal de saída: Gravar e sair, Sair sem gravar, Cancelar, Esc, SIGTERM (=fechar), Ctrl+Q | **OK**. As teclas no modal não editam. Esc sem modal não sai. |
| Retomar edits.json (cópias added, route_1, gravar de novo dá o mesmo conjunto) | **OK** no caso normal. Corrompido, de outra pista e com m0 diferente: mensagem certa. Ressalva P2-2. |
| `--fresh` | ignora o arquivo e avisa |
| Saída padrão relativa à raiz da pista | **OK**: `<cwd>/build/uiview/saves/synthetic__copia_x.edits.json` |
| --panels 0, F10, F11 | **OK** |
| Redimensionar janela e painéis | **OK** para picking: o painel da Cena alargado para 450 px e o clique no centro após F seleciona o mesmo objeto. Com janela estreita, P3-3 e P3-5. |
| Seleção atravessando painéis | um arraste começado no 3D continua sobre os painéis (FACT, `main.cpp`: `ui_took && !drag.active`). Um clique sobre um painel não chega ao 3D. |

## Estabilidade (ASan+UBSan)
Fiz uma sessão de 35 ciclos no build ASan+UBSan (`t/stress.py`), com 1.120 operações pela interface e pelo 3D. Cada ciclo teve:
- seleção e arraste no chão;
- gizmo X e anel;
- E/Q, Ctrl+D, Delete, 3×Ctrl+Z e 2×Ctrl+Y;
- clique na árvore e no histórico;
- campo digitado com Ctrl+clique e arraste do campo Y;
- Tab, edição em route_1 e Shift+Tab;
- F10, F2 e F11 duas vezes cada;
- roda do mouse;
- filtro digitado e apagado;
- Ctrl+S;
- a cada 5 ciclos, Ctrl+Q seguido de Esc no modal.

Ao fim, "Gravar e sair".

**CONFIRMED BY TEST:** 0 `runtime error`, 0 relatórios do ASan e nenhum vazamento relatado pelo LSan ao sair (`detect_leaks=1`). Código de saída 0.

**RSS:** começou em 569 MB, teve picos de 640 MB e terminou em 634 MB (+11%). Subiu até o ciclo ~25 e ficou entre 625 e 640 MB nos últimos 10.
- INFERENCE: o crescimento é compatível com a quarentena do ASan (256 MB por padrão) e o cache de texturas/miniaturas, não com um vazamento por operação.
- Não repeti a medida no build Release.

Uma primeira tentativa "morreu" no ciclo 4. Era o meu script: deu Ctrl+Q sem edições pendentes, e o editor saiu como deve.

## Performance (FPS, `--vsync 0 --frames 300`, mediana de 3, llvmpipe com CPU disputada)

| Câmera | antes (1280×720 de 3D, ~0,92 Mpx) | depois com painéis (3D 800×719, ~0,58 Mpx) | depois `--panels 0` (3D ~1440×764, ~1,10 Mpx) |
| --- | --- | --- | --- |
| enquadrada (padrão) | 15,8 | 19,2 | 14,4 |
| perto (`0.8,0.55,90,…`) | 30,2 | 35,2 | 26,3 |
| baixa (`2.4,0.12,350,0,100,0`) | 18,4 | 22,0 | 16,3 |

Valores brutos estão em `perf/result.txt`.

**INFERENCE:**
- No llvmpipe, o custo é dominado pelos pixels do 3D, e a janela nova (1440×810) tem 19% mais área de 3D quando não há painéis. Isso explica os −9 a −13% do `--panels 0` em relação ao antes.
- Com painéis, o viewport fica ~38% menor, e o FPS sobe 16 a 22%.
- O custo próprio do ImGui (menu, status, árvore com clipper, Inspector) não aparece acima do ruído. **Não consegui isolar** esse custo, porque o tamanho da janela mudou entre as versões e não há opção de tamanho. Para medir direito, seria preciso uma opção `--size` ou rodar o antes em 1440×810.
- Numa GPU real, a conta deve ser outra (HYPOTHESIS).

## Revisão de código (diff e2297b2..07275a7): o que mais olhei
- `begin_change`/`end_change`: os campos usam `IsItemActivated`/`IsItemDeactivated`. Funcionam por clique, arraste e Ctrl+clique (testado). O desequilíbrio real é o do P1-1: `click`/`begin_edit` não respeitam a mudança numérica aberta. Abrir uma pista com um arraste ativo não é alcançável pela interface (o menu e o modal exigem o mouse livre), e o `main` zera `drag`. Trocar de rota durante o gizmo é bloqueado (`open_route` checa `drag_.active`).
- `unsaved()` em cache por `edit_rev_` e `saves_`: todos os caminhos que mexem em `inst_` incrementam `edit_rev_` (FACT). Enquanto se arrasta, recalcula `current_edits()` a cada quadro (O(n) instâncias), sem problema na pista sintética.
- `duplicate` em cópia de cópia mantém `kAdded+src`, e `apply_edits` recria uma cópia por entrada `added`. Ida e volta verificadas.
- `edits_json`: nada impede `inf`/`nan` (P2-1).
- `history_go` para quando `undo()`/`redo()` devolve false: não entra em laço infinito.

## O que não deu para testar
- Fechar a janela pelo gerenciador de janelas: não há WM. Usei SIGTERM, que o SDL converte em SDL_EVENT_QUIT (`xdotool windowclose` destrói a janela e não serve).
- Monitor HiDPI (escala ≠ 1) e GPU real.
- Pista grande (Montalegre).
- Medir o custo dos painéis isolado (ver Performance).
- Digitar valores na Distância (o Ctrl+clique no slider não entrou em modo texto no meu script). Por leitura, o `SliderFloat` aceita valores fora de 100 a 4000 pelo Ctrl+clique, sem `AlwaysClamp` (FACT, `ui.cpp:331`; P3, HYPOTHESIS de efeito).
