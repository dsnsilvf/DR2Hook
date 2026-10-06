# viewer3d: relatório final das 3 rodadas

Editor nativo de **pistas** (`tools/viewer3d`, C++20, SDL3, OpenGL 3.3), commits `1c18b91` a `3c9a6e1`. Cada rodada teve um avaliador e um testador separados (sub-agentes sem permissão de mudar o repositório) e um implementador. Os relatórios estão nesta pasta: `viewer3d_r{1,2,3}_avaliacao.md` e `viewer3d_r{1,2,3}_teste.md`.

**Onde foi testado:** Mesa llvmpipe (renderização em software) sob Xvfb, sem GPU e sem o jogo. As pistas foram a sintética e uma de estresse do tamanho da Polônia. Os FPS servem só como comparação entre antes e depois. VRAM não foi medida. Nada foi testado na RTX 4050 nem com pistas reais.

**Escopo:** o prompt fala em editor "de modelos", com LODs e câmeras. O editor nativo que existe é de pistas e não abre carros. Fiquei no escopo dele. LODs e câmeras de carro continuam só no Car Explorer web (veja o item 7).

## 1. O que foi encontrado

| Rodada | Avaliação | Teste |
| --- | --- | --- |
| 1, fundamentos | 4 P0, 4 P1, 5 P2, 6 P3 | Os P0 e P1 confirmados como corrigidos; 11 achados novos (R-1 a R-11), todos P2 ou P3 |
| 2, UX | 2 P0, 6 P1, 11 P2, vários P3 | 1 P1 novo (valor digitado ia para outro objeto), 5 P2 |
| 3, robustez | 1 P1, 9 P2, 10 P3 | 1 regressão P2, 4 P2 novos, vários P3 |

Os mais graves:

- **Rodada 1, perda de dados.**
  - Um Ctrl+S que falhava fechava o programa e perdia tudo.
  - Um Tab para uma rota com arquivo ruim perdia as edições de todas as rotas.
  - O Ctrl+S sobrescrevia em silêncio o `edits.json` de outra sessão.
  - Um tipo com nome curto derrubava o título.
  - O realce da seleção nunca aparecia (`glDepthFunc` faltando).
- **Rodada 2, uso.**
  - Esc saía do programa.
  - Não dava para continuar uma sessão.
  - A interface inteira cabia no título da janela.
  - Não havia árvore, Inspector, edição numérica nem gizmo.
- **Rodada 3, escala.** O terreno inteiro era desenhado em todo quadro, sem corte, e ficava com 95 % do tempo do quadro na pista de estresse.

## 2. O que foi corrigido

| Componente | Alteração | Motivo | Teste |
| --- | --- | --- | --- |
| `edit/edits_json.cpp` `write_text` | Grava em `.tmp` e renomeia; segue link simbólico e mantém as permissões | Uma falha estragava o arquivo | `edit_test`: pai sendo arquivo, destino sendo pasta, link e permissão 0600 |
| `app/track_view.cpp` `save` | Erro vira mensagem; `.<n>.bak` uma vez por sessão | P0-1, P0-3 | xdotool: `--out /proc/...` mantém o editor aberto com "NÃO GRAVOU"; `.1.bak` igual ao original |
| `load_route` / `switch_route` | Lê tudo antes de mudar o estado; se falhar, a rota atual volta | P0-2, R-5 | 4 pistas corrompidas com Tab: editor vivo, `hist` preservado |
| `title()`, Cena | Nomes de tipo curtos ou longos | P0-4, P3 | Pistas `shortname` e `emptyname`, e nomes de 200 caracteres |
| `render/instances.cpp` `draw_one` | `GL_LEQUAL` no realce | P1-1 | 450 px diferentes com seleção (antes 0) |
| `main.cpp` / `ui.cpp` | Sair com Ctrl+Q ou fechando a janela, com janela Gravar / Sair sem gravar / Cancelar | P1-2 (R1), P0-1 (R2) | Esc, Ctrl+Q e SIGTERM testados |
| `edit_drag` | Shift antes do clique e troca de Shift no meio do arraste | P1-3 | Altura 101,72 → 108,44, mantida ao soltar |
| `gl::warn` | Erro de GL num quadro avisa e mostra no status, sem fechar | P1-4 | Injeção de erro com `LD_PRELOAD` (testador) |
| `edit::snap_to_file` | Girar 360° em passos volta exatamente à matriz do arquivo | P2-1 | `edit_test`: 0 edições |
| Inspector (`ui.cpp`) | `PushID` por instância; mudança aberta fecha ao trocar a seleção; `set_matrix` só na instância certa; recusa inf e 1e39 | P1-1 e P2-1 (teste R2) | xdotool: 200 digitado sem Enter fica na barreira certa e se desfaz |
| `apply_edits` / retomar | Aplica em cópias; entrada ruim fica de fora em vez de lançar; índice | P2-2 (teste R2), P2-3 (R3) | `edit_test`: ida e volta, entradas ruins, 50 mil edições em 0,05 s |
| `open_track` | Fecha a pista atual antes de abrir outra; se a nova falha, reabre a anterior com as edições não gravadas | P2-4 (R3), P2-a (teste R3) | `t_fail.py`: as edições voltam |
| Filtro de teclas do ImGui | Atalhos não entram na fila do ImGui; repetição e soltura de uma tecla que ele recebeu continuam indo | P2-7 (R3), P2-b | Menu depois de 100 teclas: 31,4 → 2,8 s; Backspace não fica preso |

## 3. O que foi melhorado (funções novas)

- **Painéis** (`app/ui.cpp`, Dear ImGui 1.91.9b em `third_party/imgui`):
  - menu e barra de ferramentas;
  - **Cena**: camadas com visibilidade, tipos com contagem e filtro, instâncias com id de texto e estado (editada, apagada, nova);
  - **Histórico** clicável;
  - **Inspector**: pista; objeto com X/Y/Z e giro Y editáveis, matriz atual e do arquivo, materiais com miniatura da textura;
  - barra de status.
- **Gizmo**: setas X/Y/Z no Mover e anel no Girar; caixa da seleção; encaixe de 0,1/0,5/1 m e 5/15/45°.
- **Sessão**:
  - retomar o `edits.json` ao abrir (`--fresh` ignora);
  - autosave a cada 60 s, com aviso e **Arquivo > Recuperar autosave**;
  - abrir outra pista pelo menu;
  - saída padrão relativa à pasta da pista.
- **Testes de carga**: `tests/make_stress.py` gera uma pista de 9,7 M vértices e 323 mil instâncias em menos de 1 s.

## 4. Testes executados

- **Unitários:**
  - `edit_test`: de 107 para 129 verificações (giro 360, gravação e backup, link e permissão, retomar ida e volta, entradas ruins, inf, histórico cheio, 50 mil edições);
  - `core_tests`: 61;
  - `camera_test`.
- **Sanitizers:** ASan, UBSan e LSan nas 3 rodadas, em sessões de 1 120 a 2 170 operações e de 4 a 7 min, com troca de rota, gravação e abertura pelo menu. **0 erros, 0 vazamentos**; objetos GL estáveis (`buf=17 vao=15 tex=14`).
- **Robustez:** 26 pistas corrompidas (JSON, DR2M, DR2I, texturas, rotas); janela de 1×1 até 320×240; pista sem instâncias; rota sem portões; textura de 8191×6007.
- **Imagem:** capturas da pista sintética **byte a byte iguais** antes e depois do corte do terreno, em 8 câmeras.
- **Interface:** sessões com xdotool para seleção no 3D e na Cena, gizmo, campos, histórico, janelas de confirmação, retomar, autosave com `kill` e menu.
- **Ida e volta:** `edit_roundtrip.py`, que aplica o `edits.json` com o `track/edit.py`, passou em todos os arquivos gravados.

## 5. Problemas encontrados durante os testes

- **Erros meus que os testadores pegaram:**
  - o valor digitado ia para outro objeto (P1);
  - o `inf` entrava no JSON;
  - a retomada era parcial;
  - a regressão da rodada 3 (falha ao abrir perdia edições);
  - a tecla presa no ImGui;
  - o raio do terreno medido do olho deixava a pista flutuando.

  Todos foram corrigidos e verificados de novo com os roteiros dos testadores.
- **Erros que encontrei na minha própria verificação, durante a implementação:**
  - a lógica de sair estava invertida: o primeiro Esc fechava;
  - o gizmo pegava o "hover" do quadro anterior.
- **Falsos problemas do ambiente:** `xdotool windowmove` sem gerenciador de janelas desalinha o mouse do ImGui em 80×45 px. Um `windowactivate` de outro agente fechou uma janela minha.

## 6. Problemas que continuam

- **Texturas** carregam no meio do quadro, sem compressão e sem limite de tamanho. Uma textura de 8191² trava o primeiro quadro por 2,5 s. Com muitas texturas de 2048², a VRAM da 4050 pode estourar (HYPOTHESIS).
- **O corte das instâncias** ainda varre as 323 mil a cada passo de 8 m andando, e a cada movimento de um arraste (cerca de 4 ms).
- **O picking** ignora o terreno: dá para selecionar um objeto atrás de um morro.
- **Ao soltar um arraste com 50 mil edições**, o `edits.json` é recalculado uma vez (~0,25 s).
- **Recuperar o autosave** e reabrir depois de falhar perdem o histórico (as edições ficam).
- **Esc** com um combo aberto é engolido e não fecha o combo.
- **Com fps muito baixo**, teclas digitadas logo depois de um Ctrl+clique podem se perder.
- **Na janela estreita**, a barra de status se sobrepõe.
- **Pequenos:**
  - o encaixe do giro é relativo e o do mover é absoluto;
  - o menu só procura pistas em `build/uiview/tracks` relativo à pasta atual;
  - `Track::raw` e os índices de 32 bits ocupam memória sem necessidade.
- **Aviso "pasta sem escrita" na partida:** não dá para verificar como root, porque `access()` sempre diz que sim.

## 7. Limitações

- **Não é um editor de modelos.** Carros, LODs e câmeras do jogo só existem no Car Explorer web. Trazê-los é trabalho novo: um leitor de DR2C em `dr2core` e uma árvore LOD → nó → fatia no mesmo `ui.cpp`.
- **Sem colisão:** `track.jpk` não é decodificado, então mover objetos não move a colisão do chão.
- **Edição limitada:** não há seleção múltipla, assentar no terreno, docking de painéis (a 1.91.9b não tem) nem zoom para o cursor.
- **Nada testado na RTX 4050, nem na Montalegre, nem no Windows.**

## 8. Performance (pista de estresse, llvmpipe)

| Medida | Antes | Depois |
| --- | --- | --- |
| FPS, câmera padrão | 3,3 | 18,7–18,9 |
| FPS, rente ao chão | 1,7–1,8 | 2,6–2,7 (17,9 com `--terrain-dist 3000`) |
| FPS, panorâmica | 1,3–1,4 | 1,8 (5,7 com o raio) |
| Pico de RSS na carga | 1678 MB | 942 MB |
| Abrir o estresse 3× pelo menu (pico) | 2198 MB | 1011 MB |
| Envio à GPU | 0,63–0,70 s | 0,44–0,49 s |
| Retomar 50 mil edições | 4,8 s | 0,05 s (só o `apply_edits`) |
| Menu depois de 100 teclas a ~1–2 fps | 143,6 s | 2,5 s |

O que mudou no código:

- `render/terrain.cpp`:
  - corte por frustum por malha;
  - `glMultiDrawElements` por material;
  - textura resolvida uma vez por parte;
  - envio malha por malha com `glBufferSubData`;
  - raio opcional, medido a partir do alvo.
- `malloc_trim` depois da carga.
- A biblioteca de malhas dos objetos não fica mais viva depois do envio.
- `unsaved()` não recalcula durante um arraste.
- A chave do corte das instâncias não inclui mais o zoom.

Na pista pequena, o FPS ficou igual.

## 9. Melhorias de UX

Ver o item 3. O que mais muda o uso:

- dá para ver e escolher o que está na pista (a Cena);
- dá para digitar posição e giro;
- dá para mover num eixo só;
- o histórico aparece e é clicável;
- não se perde trabalho por Esc, falha de gravação, troca de rota, crash ou abertura de outra pista;
- as mensagens aparecem na tela, não só no terminal.

## 10. Estado da arquitetura

**Bom:**

- as camadas continuam: `dr2core` sem dependências (o CMake verifica), `dr2edit` sem GL, `dr2render` sem SDL;
- os dados de edição (`History`, `edits_json`, `apply_edits`) são testáveis sem janela, e por isso cresceram com testes;
- o ImGui está isolado em `app/ui.cpp`.

**Ruim:**

- **`app/ui.cpp` tem ~940 linhas** num arquivo só: menu, árvore, Inspector, gizmo, janelas e filtro de teclas. É o próximo ponto a dividir (gizmo e Cena primeiro).
- **`TrackView` acumula papéis:** estado da sessão, carga e GL, edição, gravação, autosave e mensagens. Com cerca de 60 funções declaradas no cabeçalho, é o "objeto deus" do app. Antes de trazer carros, a sessão de edição (instâncias, histórico, gravar, retomar) precisa virar uma classe sem GL em `dr2edit`, e o `TrackView` fica só com o desenho.
- **Mudança numérica e arraste compartilham o mesmo `EditDrag`.** Funciona, mas foi a origem do P1 da rodada 2. Uma máquina de estados explícita evitaria isso.
- **Estado em `static` dentro de `inspector()`** (`base`, `yaw0`): frágil se houver mais de um Inspector.
- **Ponto positivo:** os relatórios das rodadas ficam no repositório, com rótulos e evidência.

## 11. Riscos futuros

1. **GPU real ou outro driver:** os erros de GL só são vistos no envio estático e por quadro. Nada foi testado na NVIDIA, e o caminho de `glMultiDrawElements` e do `GL_LEQUAL` pode se comportar diferente.
2. **Pistas reais:** a Polônia sintética não tem a distribuição de materiais nem as texturas das reais. A estimativa de ~60 fps com 17 M de triângulos na 4050 é INFERENCE.
3. **Formato `edits.json`:** retomar e recuperar dependem de o `m0` bater com a exportação. Reexportar com outro `export.py` deixa edições de fora (com aviso).
4. **Atualizar o ImGui:** a cópia é fixa (1.91.9b) e separada da `vendor/imgui` do hook. Atualizar uma sem a outra pode confundir quem mantém.
5. **Trazer carros para cá** sem antes separar a sessão do `TrackView` (item 10) duplicaria toda a lógica de edição.

## 12. Os 5 próximos passos

As melhorias de GPU (feitas, a fazer e o checklist para a 4050) estão anotadas em [`plans/viewer3d/melhorias_gpu.md`](../plans/viewer3d/melhorias_gpu.md).

1. **Testar na sua máquina:** Montalegre, RTX 4050, `--vsync 0`, e o `edit_roundtrip` com o `python -m tools.uiview.track.edit` num `.nefs` novo. Tudo até aqui foi em software.
2. **Texturas assíncronas e comprimidas (BC1/BC3),** com limite de tamanho e contagem de memória de vídeo. É o maior risco de VRAM que sobrou.
3. **Separar a sessão de edição do `TrackView`** e dividir o `ui.cpp`, antes de crescer mais.
4. **Picking com profundidade** (ler o z-buffer no clique), para não selecionar através do terreno, mais **assentar no terreno** ao mover.
5. **Decidir sobre carros:** se o editor nativo deve ser também o de modelos, fazer o leitor DR2C e a árvore LOD → nó → fatia → material, reusando Cena, Inspector e gizmo.
