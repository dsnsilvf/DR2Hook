# Avaliação da Rodada 3: polimento e robustez do `tools/viewer3d` (commit 07275a7)

Avaliador: agente 1. Nada foi alterado em `/home/user/DR2Hook`. Tudo rodou na cópia em `$S/src/tools/viewer3d`, com `S=/tmp/claude-0/-home-user-DR2Hook/04e0f535-6bba-572c-b584-3c3caa091cdf/scratchpad/r3eval`.

Ambiente: 4 CPUs, 15 GB de RAM, Mesa llvmpipe (LLVM 20.1.2) em Xvfb. A maioria das medidas foi feita num Xvfb próprio (`:197`), porque o `:95` é compartilhado e os cliques do xdotool se perdiam nele. Não há GPU. Os FPS absolutos não valem para a RTX 4050; o que vale é a proporção entre os custos. A VRAM foi estimada pelos tamanhos que o código envia; no llvmpipe, os buffers ficam na RAM e aparecem no RSS.

Builds usados:
- `$S/build`: o código original (RelWithDebInfo, como o README).
- `$S/build_inst`: uma cópia instrumentada (`$S/inst`, diff em `$S/instrumentacao.diff`). Ela acrescenta só medição e chaves de teste. Sem as variáveis de ambiente, o comportamento é o do original; os `fprintf` de corte, picking e regroup estão sempre ligados.
  - Medição: `PROF=1` mede cada etapa do quadro com `glFinish`. Há também temporizadores em `cull`, `pick`, `regroup`, `resume_edits` e `save`, e contadores de objetos GL vivos (`GLCOUNT=1`).
  - Chaves de teste: `DD=` (distância de desenho), `NOTER`/`NOOBJ` (tiram camadas), `FRUSTUM=1` (corte por frustum por malha do terreno: protótipo da solução), `MERGE=1` (um só draw call no terreno), `TRIM=1` (`malloc_trim` depois da carga) e `NOTRICKLE=1` (`io.ConfigInputTrickleEventQueue=false`).

Rótulos usados em cada afirmação:
- **FACT**: lido no código.
- **CONFIRMED BY TEST**: medido ou reproduzido aqui.
- **INFERENCE**: deduzido de números e de conhecimento de hardware.
- **HYPOTHESIS**: não verificado.

---

## Resumo

| Sev. | # | Problema |
| --- | --- | --- |
| P0 | — | Nenhum crash nem perda de dados reproduzido. Foram testados: sessão longa, 61 trocas de rota, 8 aberturas de pista pelo menu, histórico cheio, 30 duplicações, edits.json de 50 mil edições, janela de 1×1, nomes longos, caminhos com espaço e acento, pista sem instâncias e rota sem portões/IA |
| P1 | 1 | O terreno é desenhado inteiro em todo quadro: não há corte por frustum nem por distância. É 95 % do quadro na pista de estresse |
| P2 | 2 | `unsaved()` refaz o edits.json inteiro a cada movimento do mouse durante um arraste. O custo cresce com o número de instâncias e de edições (6–278 ms por quadro) |
| P2 | 3 | Retomar o edits.json custa O(edições × instâncias): 4,8 s com 50 mil edições |
| P2 | 4 | O corte das instâncias em CPU varre as 323 mil a cada quadro enquanto se anda, dá zoom ou arrasta (3,4–4,6 ms; picos de 12 ms) |
| P2 | 5 | Pico de memória na carga: 4 cópias da geometria (1,68 GB). Ficam 340 MB retidos no heap. Abrir outra pista pelo menu mantém a antiga viva (pico de 2,25 GB) |
| P2 | 6 | Texturas decodificadas no meio do quadro, sob demanda: uma 8191×6007 travou 2,5 s. Ficam em RGBA8 sem compressão (risco de VRAM em pista real) |
| P2 | 7 | Fila de entrada do ImGui (trickle) a poucos FPS: 100 teclas deixam os painéis 48 s sem responder |
| P2 | 8 | Com o histórico cheio (300), "(arquivo aberto)" e o Ctrl+Z até o fim não voltam ao arquivo, e a interface não avisa |
| P2 | 9 | Erros de GL (inclusive `GL_OUT_OF_MEMORY`) só são avisados no stderr. Abrir pista pelo menu não verifica o envio |
| P2 | 10 | Não há autosave: um crash perde todas as edições não gravadas. SIGTERM vira pedido de confirmação |
| P3 | 11 | Nomes de tipo longos (> ~85 caracteres) fazem conflito de ID no ImGui: os tipos abrem juntos e aparece um erro na tela |
| P3 | 12 | Rota sem portões nem IA: a câmera não enquadra a pista (fica no padrão de teste) |
| P3 | 13 | Janela estreita: `std::clamp` com lo > hi (UB), painéis maiores que a janela e barra de status sobreposta |
| P3 | 14 | HiDPI: tamanhos fixos em pixels não escalam, a barra corta a "Distância" com escala 1,5, e a escala é lida só ao abrir |
| P3 | 15 | Picking O(n): 4–6 ms por clique em 323 mil instâncias |
| P3 | 16 | Profundidade: com near 0,3 e far 20 000 m, a precisão a 3 km é ~1,8 m (risco de z-fighting distante; mesmo valor do web) |
| P3 | 17 | Linhas da IA e portões sempre por cima do terreno (depth test desligado, igual ao web), sem opção |
| P3 | 18 | Recorte por alfa (0,4) com mipmaps: a folhagem tende a sumir ao longe |
| P3 | 19 | Memória morta: `library_` (objects.bin) e `Track::raw` (DOM do track.json) ficam vivos. Índices de 16 bits viram 32 bits |
| P3 | 20 | Sem `objects.bin`, nenhuma pista abre, mesmo sem instâncias |

Itens verificados sem problema (CONFIRMED BY TEST, salvo indicação):
- Nenhum objeto GL vaza ao reabrir pistas: os contadores ficaram em `buf=17 vao=15 tex=14` em todas as 8 aberturas.
- O RSS se estabiliza em 4 ciclos ringue↔estresse (536 → 573 → 573 → 573 MB no ringue).
- Sessão aleatória de 4 min no ringue (cliques, arrastes, E/Q, Ctrl+D, Delete, Ctrl+Z/Y, Tab ×60, zoom, [ ]): RSS entre 141 e 170 MB, sem crescer, e Ctrl+S gravou um JSON válido.
- 300 passos de histórico não mudam o RSS (950 448 kB antes e depois de 330 giros). Um passo guarda ~2×56 B de `Snap` (FACT, `history.hpp`).
- 30 duplicações: `regroup` custa 0,81 ms em média no estresse.
- `title()` custa 0,01–0,03 ms.
- Pista sem instâncias, tipo sem malha (`synth_spawn_marker`), track.json com campos extras (objetos, `null`, `1e300`, acentos), pasta com espaço e acento ("pista com espaço çãé"), 40 rotas com nomes de 90 caracteres e Tab ×45: tudo abre, desenha e grava.
- Textura não potência de 2 (8191×6007 e 4095×3001): carrega e mostra. O WebP não passa de 16383 px, então não excede `GL_MAX_TEXTURE_SIZE` (FACT, limite do formato).
- Limites de 32 bits: o deslocamento `uint32` do terreno só estoura acima de 4,29 G vértices (~100 GB de VBO), e `GLsizeiptr` é de 64 bits. Na prática, o limite é a VRAM, bem antes (FACT, `terrain.cpp:34,57`).

---

## Tabela de medidas

### Carga (pista em cache de página)

| Pista | Leitura DR2M | Envio à GPU (intercalar + glBufferData + glFinish) | Texturas do terreno | Total até o 1º quadro | Pico de RSS | RSS depois |
| --- | --- | --- | --- | --- | --- | --- |
| ringue (152 mil vért.) | 0,007–0,008 s (5,3 MB) | 0,010–0,011 s | 0,035 s | 0,30–0,31 s | — | 170 MB |
| estresse (9,7 M vért.) | 0,51–0,59 s (339 MB) | 0,62–0,69 s | 0,010–0,028 s | 1,71–1,76 s | 1678 MB | 959 MB (619 MB com `malloc_trim`) |
| estresse aberto pelo menu a partir de outro estresse | — | — | — | — | 2251 MB | 1068 MB |

O envio medido aqui (0,62–0,69 s) é 5× menor que os 3,4 s da linha de base do implementador. A leitura e o FPS batem; a diferença provavelmente veio de carga na máquina na hora da medida dele (HYPOTHESIS).

### Quadro (FPS com `--vsync 0`, 1440×810; ringue 60 quadros, estresse 10)

| Pista | Câmera | Painéis 1 | Painéis 0 |
| --- | --- | --- | --- |
| ringue | padrão (frame_route) | 22,7 | 18,0 |
| ringue | rente ao chão `0.8,0.0,40,0,100,0` | 37,1 | 27,7 |
| ringue | panorâmica `0.8,1.4,15000,-1200,100,-1400` | 24,3 | 24,8 |
| estresse | padrão | 3,3 | 3,5 |
| estresse | rente ao chão | 1,7 | 1,5 |
| estresse | panorâmica | 1,0 | 0,8 |

Sem painéis, a área 3D é maior e há mais pixels; por isso o FPS cai.

### Etapas por quadro no estresse (`PROF=1`, ms por quadro, glFinish entre etapas)

| Câmera | corte (média) | terreno | objetos | ui.frame | ui.render | swap |
| --- | --- | --- | --- | --- | --- | --- |
| padrão, 700 m | 0,17–0,48 (recorte completo: 4,1–4,6) | **280–290** | 12,8–13,7 | 0,4 | 3,6 | 1,4 |
| padrão, 4000 m (42 872 inst. visíveis) | 0,50 (recorte: 4,9) | 268 | 25,2 | — | — | — |
| rente ao chão | — | **516** | 6,1 | — | — | — |
| panorâmica | — | **1020** | 0,02 | — | — | — |
| sessão de 1682 quadros (edição) | 0,59 | 267,7 | 7,5 | 1,05 | 8,9 | 1,3 |

### Protótipo de corte por frustum (`FRUSTUM=1`, caixa por malha, 8 cantos contra os planos)

| Câmera | Malhas desenhadas | Triângulos | Terreno (ms) | FPS |
| --- | --- | --- | --- | --- |
| padrão | 137–165 de 10 496 | 450–485 mil de 16,9 M | 280 → **38** | 3,3 → **16,5** |
| rente ao chão | 2306 | 4,49 M | 516 → 403 | 1,9 → 2,4 |
| panorâmica | 5858 | 10,9 M | 1020 → 1049 | 1,0 → 0,9 |

A captura com e sem frustum na câmera padrão é **igual pixel a pixel** (`ImageChops.difference(...).getbbox() == None`; CONFIRMED BY TEST). Juntar o terreno num só draw call (`MERGE=1`) quase não mudou o tempo (280 → 267 ms): no llvmpipe, o gargalo é o número de triângulos, não o de chamadas (CONFIRMED BY TEST).

### Outras medidas

| O quê | Resultado |
| --- | --- |
| `pick()` em 323 520 instâncias | 4,2–6,2 ms por clique |
| `cull()` ao andar com W por 4 s (terreno desligado, ~70 fps) | 293 recortes (um por quadro), média 3,4 ms, máximo 12,4 ms |
| 10 cliques de roda | 10 recortes |
| arraste do gizmo com 20 movimentos | 21 recortes e 21 `current_edits()` |
| `current_edits()` no estresse, 0–31 edições | 5,4–6,3 ms |
| idem, 1000 / 10 000 / 50 000 edições | 10,7 / 52,1 / 278 ms |
| `resume_edits` com 1000 / 10 000 / 50 000 edições | 97 ms / 0,90 s / 4,78 s |
| `save()` com 31 edições no estresse | 7,0 ms (9,7 kB) |
| `regroup` (Ctrl+D) no estresse | 0,81 ms |
| painéis sem resposta depois de 100 teclas `e` a ~4 fps | 48,2 s com trickle (padrão) e 2,0 s sem trickle |
| textura de objeto 8191×6007 carregada no 1º quadro | quadro de objetos de 2484 ms |
| RSS ao abrir 4× estresse ↔ ringue | estresse 992–999 MB, ringue 536 → 573 → 573 → 573 MB; GL `buf=17 vao=15 tex=14` sempre |

---

## P1

### 1. O terreno é desenhado inteiro em todo quadro, sem corte por frustum nem por distância

- **Problema**: `Terrain::draw` percorre todas as partes (10 496 no estresse) e manda todas, mesmo as que estão atrás da câmera ou a 20 km.
- **Evidência**:
  - Na câmera padrão do estresse, o terreno leva 280–290 ms de um quadro de ~300 ms; o resto do quadro soma ~20 ms (CONFIRMED BY TEST).
  - Com corte por frustum, só 137 das 10 496 malhas (450 mil triângulos) estão na tela, a imagem fica idêntica e o FPS vai de 3,3 para 16,5 (CONFIRMED BY TEST).
  - Rente ao chão, o frustum ainda deixa 4,5 M triângulos (até o horizonte, far = 20 km). Na panorâmica, deixa 10,9 M: só o frustum não basta (CONFIRMED BY TEST).
- **Arquivo:linha**: `src/render/terrain.cpp:72-96` (laço sem teste), `src/render/terrain.hpp:24-30` (Part sem caixa), `src/app/track_view.cpp:230`.
- **Causa**: o MVP só tem uma faixa de índices por malha, sem caixa, e não recebe a view_proj (FACT).
- **Impacto (INFERENCE para a RTX 4050)**:
  - Uma Polônia de ~9 M vértices tem ~17 M triângulos e ~10 mil draw calls com 3–4 glUniform cada.
  - GPU: ~5–10 ms só de vértices e rasterização.
  - CPU do driver: ~10–20 mil chamadas GL por quadro, ~5–15 ms.
  - Somado às instâncias e ao ImGui, o quadro fica perto ou abaixo de 60 fps, e cai muito na panorâmica (muitos triângulos subpixel).
  - Num notebook híbrido que rode na iGPU (o README avisa do PRIME offload), fica injogável.
  - Como o terreno é > 90 % do custo em todas as câmeras medidas e a correção é barata, classifico como P1.
- **Solução recomendada (incremental)**:
  1. Calcular `lo/hi` por Part no construtor (o laço já passa por cada vértice) e, em `draw`, pular a parte cuja caixa fica fora de algum plano do frustum. O protótipo tem ~25 linhas; veja `instrumentacao.diff`, `render/terrain.cpp`.
  2. Corte por distância opcional para o terreno, com uma distância de terreno separada (por exemplo, 3–5 km padrão, ou "infinito"), mais um fog suave para esconder a borda. É o que resolve a câmera rente ao chão.
  3. Na panorâmica (dist > ~3 km), ou pular as malhas cuja caixa projetada tem menos de N pixels (corte por tamanho na tela), ou aceitar o custo.
  4. Agrupar por material num `glMultiDrawElements` (GL 1.4), para reduzir as chamadas de CPU depois do corte, e mandar `set_texture`/`set_vertex_color` só quando mudam.
- **Como testar**: `FRUSTUM=1 PROF=1 $S/build_inst/viewer3d --track <estresse> --fresh --out $S/e.json --vsync 0 --frames 10 [--camera ...]`, e comparar `--screenshot` com e sem o corte (bbox da diferença = None). Na máquina do dono, medir o FPS nas três câmeras da tabela, antes e depois.

---

## P2

### 2. `unsaved()` refaz o edits.json inteiro a cada movimento durante o arraste

- **Problema**:
  - Cada `set_matrix` (gizmo, campos do Inspector) e cada `edit_drag` incrementa `edit_rev_`.
  - No quadro seguinte, a barra de ferramentas e a barra de status chamam `unsaved()`, que chama `current_edits()`.
  - `current_edits()` percorre todas as instâncias de todas as rotas abertas e formata 24 floats com `%.9g` por edição.
- **Evidência (CONFIRMED BY TEST)**:
  - Arraste do gizmo com 20 movimentos: 21 chamadas.
  - No estresse: 5,4–6,3 ms com até 31 edições, 10,7 ms com 1000, 52 ms com 10 mil e 278 ms com 50 mil.
- **Arquivo:linha**: `src/app/track_view.cpp:312` (`set_matrix` → `++edit_rev_`), `:250` (`edit_drag`), `:435-443` (`unsaved`), `src/edit/edits_json.cpp:31-53`, `src/app/ui.cpp:292` e `:633`.
- **Causa**: o cache de `unsaved()` é invalidado por qualquer revisão, inclusive as intermediárias de um arraste (FACT).
- **Impacto**:
  - Numa pista real com milhares de instâncias e centenas de edições: 1–10 ms extras por quadro durante o arraste (INFERENCE).
  - Com milhares de edições, o arraste trava.
- **Solução**:
  - Durante `drag_.active`, devolver `true` sem recalcular (uma edição ativa já é "não gravado") e recalcular só no `end_change`/`end_edit`.
  - Melhor ainda: manter um contador de instâncias alteradas (`changed()`) atualizado incrementalmente em `commit`/`apply_snap`, e comparar com o texto só ao gravar.
- **Como testar**: retomar `$S/edits_1000.json` em `$S/tracks/stress_u`, arrastar o gizmo e ver `PROF unsaved/current_edits n=` e a média. Deve ser ~1 chamada por arraste.

### 3. Retomar o edits.json é quadrático

- **Problema**: `apply_edits` procura cada edição com uma varredura linear de `inst.n`.
- **Evidência**: 50 mil edições num track de 323 mil instâncias com idnums únicos (`$S/tracks/stress_u`) levam 4,78 s; 10 mil levam 0,90 s; 1000 levam 97 ms (CONFIRMED BY TEST).
- **Arquivo:linha**: `src/edit/edits_json.cpp:118-120`.
- **Causa**: não há índice (idnum, tipo) → posição (FACT).
- **Impacto**: arquivos de edição grandes atrasam a abertura em segundos. Com o número realista de edições (< 1000), o efeito é pequeno (INFERENCE).
- **Solução**: montar uma vez um `unordered_map<uint64_t(idnum<<16|tipo), índice>` antes do laço.
- **Como testar**: `PROF=1 $S/build_inst/viewer3d --track $S/tracks/stress_u --out <cópia de edits_50000.json> --frames 3` e ver `PROF resume_edits`.

### 4. O corte das instâncias em CPU varre as 323 mil a cada quadro em movimento

- **Problema**: a chave do corte inclui o alvo em passos de 8 m, `cam_dist/8` e `edit_rev`. Ao andar, dar zoom ou arrastar, a chave muda a cada quadro, e o corte percorre todos os grupos e reenvia (`glBufferData`) os buffers de todos os tipos.
- **Evidência (CONFIRMED BY TEST)**:
  - W por 4 s gerou 293 recortes (um por quadro), média 3,4 ms e máximo 12,4 ms.
  - Cada clique de roda gera um recorte.
  - Cada movimento do gizmo gera um recorte.
- **Arquivo:linha**: `src/render/instances.cpp:119-142`; a chave está em `:122-124`.
- **Causa**: o corte é uma varredura linear sem estrutura espacial. `cam_dist` entra na chave sem ser usado no teste (`passes` só usa o alvo e a distância de desenho; FACT, `instances.cpp:109-117`).
- **Impacto (INFERENCE)**: ~2–4 ms de CPU por quadro na máquina do dono enquanto se move, numa pista com centenas de milhares de instâncias; em pistas típicas (~3 mil), é desprezível.
- **Solução**:
  - Tirar `cam_dist` da chave.
  - Grade uniforme em XZ (por exemplo, células de 64 m) com as instâncias de cada tipo, e o corte só visita as células dentro do raio.
  - Opcional: refazer só quando o alvo andar mais que ~5 % do raio (histerese), e no arraste atualizar só a instância editada com `glBufferSubData` em vez de recortar tudo.
- **Como testar**: log `PROF cull-recompute` ao segurar W; o número por segundo e a média devem cair.

### 5. Pico de memória na carga e na troca de pista

- **Problema**: durante `load_route` existem ao mesmo tempo:
  - o arquivo inteiro (`bytes`, 339 MB);
  - `vector<Mesh>` com pos/uv/col/índices em 32 bits (~436 MB);
  - os vetores intercalados do `Terrain` (~436 MB);
  - a cópia do driver (436 MB no llvmpipe).

  Depois da carga, ~340 MB ficam presos no heap: são milhares de vetores pequenos por malha, que fragmentam o heap. E `open_track` constrói a pista nova antes de destruir a antiga.
- **Evidência (CONFIRMED BY TEST)**:
  - Pico de 1678 MB e residual de 959 MB; com `malloc_trim(0)` depois da carga, o residual cai para 619 MB.
  - Estresse → estresse2 pelo menu: pico de 2251 MB.
  - Ringue depois de ter aberto o estresse: 536–573 MB, contra 170 MB no início. Não é vazamento: estabiliza em 4 ciclos e os objetos GL não crescem.
- **Arquivo:linha**: `src/app/track_view.cpp:107-115` (`bytes` vive até o fim do bloco), `src/render/terrain.cpp:28-58`, `src/core/dr2m.cpp:19-36`, `src/app/main.cpp:384-385`.
- **Impacto (INFERENCE)**:
  - Na máquina do dono, sem a cópia do llvmpipe, o pico deve ficar em ~1,2 GB e o residual em ~0,5 GB.
  - Na GPU, a VRAM do terreno ~ vértices×24 + triângulos×12 B: 436 MB no estresse, ~430 MB numa Polônia.
  - Na troca de pista, as duas pistas ficam na VRAM ao mesmo tempo por um instante.
  - Não quebra numa máquina de 16 GB, mas pesa em 8 GB.
- **Solução (incremental)**:
  1. Liberar `bytes` logo depois de `read_dr2m`.
  2. No `Terrain`, alocar o VBO/IBO com `glBufferData(nullptr)` e enviar por malha com `glBufferSubData` (ou montar em blocos de ~16 MB), sem o vetor intercalado inteiro.
  3. Ou ler direto do arquivo para o formato intercalado, sem `vector<Mesh>`.
  4. `malloc_trim(0)` depois da carga, no Linux.
  5. Em `open_track`, destruir a pista atual antes de criar a nova. Isso exige recarregar a antiga se a nova falhar, ou validar os arquivos antes (ler track.json e o tamanho dos .bin) e só então trocar.
- **Como testar**: `$S/rss.sh <out> $S/build/viewer3d --track <estresse> ... --frames 5` (pico e residual), e a troca pelo menu com a pasta `$S/wd` (veja Comandos).

### 6. Texturas carregadas no meio do quadro e sem compressão

- **Problema**:
  - `TextureCache::for_material` decodifica o WebP (libwebp), envia em RGBA8 e gera mipmaps na primeira vez que um material aparece, dentro do desenho.
  - As texturas dos objetos chegam ao entrar na área e ao abrir o Inspector (miniaturas).
  - Nada é comprimido nem limitado em tamanho.
- **Evidência**: com a textura de um objeto em 8191×6007 (`$S/tracks/e5_tex`), o primeiro quadro de objetos levou 2484 ms; os seguintes, ~0 (CONFIRMED BY TEST). O README conta 820 materiais na Montalegre.
- **Arquivo:linha**: `src/render/texture.cpp:28-74` (`load` síncrono, `glTexImage2D(... GL_RGBA8 ...)` em `:61`), `src/render/instances.cpp:146`, `src/app/ui.cpp:597`.
- **Impacto (INFERENCE / HYPOTHESIS)**:
  - Engasgos de 50–500 ms ao virar a câmera para uma área com tipos novos (texturas 1–2k decodificadas por CPU).
  - Em VRAM: 300 arquivos de 2048² em RGBA8 com mipmaps dão ~6,4 GB, mais que os 6 GB da 4050. Com 1024², ~1,6 GB.
  - Não sei o tamanho real das texturas exportadas (HYPOTHESIS: medir na Montalegre).
- **Solução**:
  1. Pré-carregar na abertura todas as texturas dos tipos com instância na rota, com barra de progresso, ou decodificar o WebP numa thread e só fazer o `glTexImage2D` no thread do GL (fila, N por quadro).
  2. Usar formato interno comprimido (`GL_COMPRESSED_RGBA_S3TC_DXT5_EXT` / `GL_COMPRESSED_RGB_S3TC_DXT1_EXT`, que o driver comprime no envio; extensão presente na NVIDIA) e/ou limitar a 2048 (ou a um valor configurável).
  3. Mostrar `bytes_rgba()` com mipmaps (×4/3) no Inspector; hoje só aparece o nível 0.
- **Como testar**: `PROF=1 $S/build_inst/viewer3d --track $S/tracks/e5_tex --frames 1` (tempo de objetos do 1º quadro). Na Montalegre real, somar `bytes_rgba` com todas as camadas ligadas.

### 7. Fila de entrada do ImGui (trickle) atrasa os painéis quando o FPS é baixo

- **Problema**:
  - Todo evento SDL vai para o ImGui (`ImGui_ImplSDL3_ProcessEvent`), mesmo as teclas que o app usa como atalho (E, Q, Ctrl+Z…).
  - Com `ConfigInputTrickleEventQueue = true` (padrão), o ImGui processa uma mudança de tecla por quadro.
  - A 3–4 fps, 100 toques em `e` deixam o menu Arquivo sem abrir por 48 s; sem trickle, abre em 2 s.
  - Antes, uma rajada de 330 E + 305 Ctrl+Z deixou o modal "Edições não gravadas" sem responder por mais de 3 min: os cliques acabaram processados e gravaram.
- **Evidência**: `$S/trickle.sh` dá 48,2 s com trickle e 2,0 s com `NOTRICKLE=1` (CONFIRMED BY TEST). Sem trickle, um clique rápido (botão apertado e solto no mesmo quadro) se perdeu a 3 fps (CONFIRMED BY TEST). Desligar o trickle sozinho não resolve.
- **Arquivo:linha**: `src/app/ui.cpp:112-125`, `third_party/imgui/imgui.cpp:1461`.
- **Impacto**:
  - A 60 fps, uma tecla segurada (repetição ~30/s) não acumula (INFERENCE).
  - O problema aparece quando o FPS cai: estresse, iGPU, panorâmica. Aí o usuário vê os painéis "congelados" com o 3D andando.
- **Solução**: em `EditorUi::event`, não repassar `SDL_EVENT_KEY_DOWN/KEY_UP` de teclas não modificadoras quando `!io.WantCaptureKeyboard && !io.WantTextInput`, mantendo os modificadores e o mouse. Assim a fila do ImGui só recebe o que ele vai usar.
- **Como testar**: `$S/trickle.sh X=1` no estresse; o tempo deve cair para < 3 s.

### 8. Com o histórico cheio, "(arquivo aberto)" deixa de ser o arquivo

- **Problema**: `History::commit` descarta o passo mais antigo depois de 300. O primeiro item da lista continua chamado "(arquivo aberto)", e `history_go(0)` só desfaz os 300 passos restantes.
- **Evidência**: 30 Ctrl+D e 330 E, depois Ctrl+Z ×305: o histórico mostra "0/300" e a lista diz "(arquivo aberto)", mas a cópia continua "Cópia nova", girada -18,51°, e a barra diz "não gravado" (CONFIRMED BY TEST, capturas `$S/s6c.png` e `$S/s7c.png`).
- **Arquivo:linha**: `src/edit/history.cpp:43`, `src/app/ui.cpp:429`.
- **Impacto**: o usuário acredita ter voltado ao arquivo e grava edições que pensava ter desfeito. Não há perda; há edição indesejada (P2 de correção de UX).
- **Solução**:
  - Quando houve descarte, mostrar "(início do histórico: N passos antigos descartados)" no lugar de "(arquivo aberto)".
  - Oferecer "Restaurar tudo do arquivo", que aplica `restore` em todas as instâncias alteradas, num só passo de histórico.
- **Como testar**: o mesmo roteiro (o xdotool está nos comandos), depois conferir o Inspector e `edits.json`.

### 9. `GL_OUT_OF_MEMORY` e outros erros de GL só viram aviso no stderr

- **Problema**:
  - `Buffer::upload` não verifica erro.
  - Na abertura pela linha de comando, `gl::check("criação da cena")` lança uma exceção e o programa sai com 1, o que é aceitável.
  - Ao abrir uma pista pelo menu (`open_track`) ou trocar de rota com outro terreno, nada é verificado. O terreno fica sem buffer (nada ou lixo na tela) e só aparecem até 20 linhas "erro de GL 0x0505 em quadro" no terminal.
  - Texturas que falham no envio ficam com id válido e sem dados (preto).
- **Evidência**: FACT (`src/render/gl.cpp:110-113`, `src/app/main.cpp:419,463`, `src/app/track_view.cpp:115-116`, `src/render/texture.cpp:57-66`). Não reproduzi o OOM: o llvmpipe usa a RAM, e esgotá-la com o host compartilhado seria arriscado (HYPOTHESIS sobre o efeito visual).
- **Impacto**: numa GPU de 6 GB com pista grande e texturas RGBA, o usuário veria o terreno sumir sem explicação. As edições continuam gravando, então não há perda de dados.
- **Solução**:
  - Em `load_route` (depois de `make_unique<Terrain>`) e em `TextureCache::load`, chamar `glGetError()`. Em `GL_OUT_OF_MEMORY`, lançar uma exceção: a rota/pista atual continua, como já acontece com arquivos inválidos, e a mensagem vai para a barra de status.
  - Para texturas, em erro, apagar a textura e devolver 0 (cor fixa) com aviso.
- **Como testar**: forçar em build de teste com um `glBufferData` gigante (por exemplo, multiplicar o tamanho do VBO por 64) e conferir a mensagem e que a pista anterior continua.

### 10. Não há autosave; um crash perde as edições

- **Problema**: as edições só vão para o disco no Ctrl+S. SIGTERM vira `SDL_EVENT_QUIT` → `ask_quit` → modal, e o processo não termina (bom contra perda). Mas um SIGKILL (logout que força o fim depois do TERM, OOM killer, crash do driver) perde tudo.
- **Evidência**: `kill <pid>` (TERM) num viewer com edições não gravadas não o encerrou; foi preciso `kill -9` (CONFIRMED BY TEST). Não há código de autosave (FACT, grep).
- **Arquivo:linha**: `src/app/main.cpp:295-297`, `src/app/track_view.cpp:398-424` (`save`).
- **Impacto**: numa sessão longa com centenas de edições, um crash do driver da GPU (comum em notebooks híbridos) apaga horas de trabalho (INFERENCE).
- **Solução**: a cada N segundos com `unsaved()`, ou a cada K passos, gravar `<out>.autosave.json` com `write_text` (já atômico). Na abertura, se o autosave for mais novo que `out`, oferecer retomar. Apagar o autosave no Ctrl+S.
- **Como testar**: editar, matar com `kill -9`, reabrir e conferir a pergunta e as edições.

---

## P3

### 11. Nomes longos fazem conflito de ID no ImGui

- **Problema**: o rótulo do nó de tipo é montado com `snprintf(label, 96, "%s  (%zu)%s###t%zu", ...)`. Com o nome além de ~85 caracteres, o sufixo `###t<n>` é cortado, e tipos com prefixo comum ficam com o mesmo ID.
- **Evidência**: pista `$S/tracks/pista com espaço çãé` (tipos com prefixo de 120 'x'). Ao abrir um tipo, o ImGui mostra "Programmer error: 9 visible items with conflicting ID!" e todos abrem juntos (CONFIRMED BY TEST, `$S/e3a.png`).
- **Arquivo:linha**: `src/app/ui.cpp:370,378` (e `:434-435` no histórico, com o mesmo padrão).
- **Solução**: `ImGui::PushID(static_cast<int>(t))` com o nome sem `###`, ou um buffer `std::string`. Na barra lateral, cortar o nome visível com "…" e mostrar o nome inteiro na dica.
- **Como testar**: abrir essa pista e expandir um tipo.

### 12. Rota sem portões nem IA: a câmera não enquadra

- **Problema**: `frame_route` retorna sem fazer nada quando `route_bounds` é falso. A câmera fica no padrão da cena de teste (alvo 0, 1430, -430), e **F** também não faz nada.
- **Evidência**: `$S/tracks/e2_sem_ia` mostra só a encosta de um morro (CONFIRMED BY TEST, `$S/edge12.png`, em cima).
- **Arquivo:linha**: `src/app/track_view.cpp:179-186`.
- **Solução**: como alternativa, usar a caixa do terreno (guardar lo/hi no `Terrain`; o item 1 já calcula por parte) ou a das instâncias.

### 13. Janela estreita

- **Problema**:
  - `std::clamp(left_w_, 160, W*0.45)` e `std::clamp(right_w_, 200, W*0.45)` têm lo > hi quando W < 445 px, o que é comportamento indefinido em `std::clamp`.
  - A área 3D chega a ~30 px de largura.
  - Na barra de status, o texto da direita se sobrepõe ao da esquerda.
- **Evidência**: com a janela em 320×240, 200×120, 60×40 e 1×1, não houve crash. A captura em 320×240 mostra painéis cobrindo quase tudo e o status sobreposto (CONFIRMED BY TEST, `$S/w1c.png`). A UB é FACT.
- **Arquivo:linha**: `src/app/ui.cpp:182-183`, `:652`.
- **Solução**: `std::min(std::max(v, lo), std::max(lo, hi))`. Abaixo de ~700 px, esconder as laterais automaticamente (como o F10) e cortar o texto do status.

### 14. HiDPI

- **Problema**:
  - A escala vem de `SDL_GetWindowDisplayScale` só no construtor; `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED` não é tratado.
  - Vários tamanhos estão fixos em pixels: `SetNextItemWidth(160)`, `SameLine(0, 18)`, o raio de acerto de 9 px do gizmo, as setas de 5,5 px, a dica de 256 px e a janela de ajuda de 520 px.
  - Com escala 1,5 (`SDL_VIDEO_X11_SCALING_FACTOR=1.5`), a barra de ferramentas passa da largura de 1440 px e a "Distância" fica cortada. Com escala 1, ela já corta em janelas com menos de ~1200 px.
- **Evidência**: `$S/h.png` (CONFIRMED BY TEST); `src/app/ui.cpp:96,330,743`.
- **HYPOTHESIS (Wayland/KDE do dono)**: sem `SDL_WINDOW_HIGH_PIXEL_DENSITY`, a janela é de baixa densidade. O compositor amplia o quadro (3D borrado), e `SDL_GetWindowDisplayScale` pode aplicar a escala de novo à UI. Testar com escala de 125/150/200 % no Plasma.
- **Solução**: guardar `scale` e multiplicar os tamanhos fixos por ele. Tratar `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED` (refazer a fonte e o estilo). Criar a janela com `SDL_WINDOW_HIGH_PIXEL_DENSITY` (o main já converte pontos → pixels em `main.cpp:446-456`). Deixar a barra quebrar linha, ou mover a distância para o menu Exibir, quando não couber.

### 15. Picking O(n)

- **Evidência**: 4,2–6,2 ms por clique em 323 mil instâncias (CONFIRMED BY TEST). `pick` refaz o inverso 3×3 de cada instância a cada clique (FACT, `src/render/pick.cpp:55-90`).
- **Impacto**: imperceptível num clique; só pesa se virar hover (INFERENCE).
- **Solução**: usar a mesma grade espacial do item 4 e testar só as células atravessadas pelo raio até a distância de desenho.

### 16. Precisão de profundidade

- **Problema**: `near = max(0,3, dist/200)` e `far = max(20 000, dist×20)`, com profundidade de 24 bits. A resolução vale Δz ≈ z²/(near·2²⁴).

  | Câmera | Δz a 1 km | Δz a 3 km | Δz a 15 km |
  | --- | --- | --- | --- |
  | dist ≤ 60 (near 0,3, rente ao chão e editando) | 0,2 m | 1,8 m | — |
  | dist 400 (near 2) | 0,03 m | 0,27 m | — |
  | dist 15 000 (near 75) | — | — | 0,18 m |

- **Evidência**: FACT (`src/render/camera.hpp:30-31`) e as contas acima (INFERENCE). Os valores são os mesmos do web (`trackview.js:308`). A pista sintética não tem camadas coplanares para reproduzir o efeito (HYPOTHESIS: estradas e decalques a 1–3 km piscam quando a câmera está perto do chão).
- **Solução incremental**: `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)` (ARB_clip_control, presente na NVIDIA) com Z invertido e depth buffer float num FBO, ou só subir o near quando o alvo estiver longe do chão (por exemplo, `max(0.3, dist/200, 0.5)`), e `glPolygonOffset` nos materiais de decalque.

### 17. Linhas da IA e portões sempre por cima

- **Evidência**: FACT. `glDisable(GL_DEPTH_TEST)` em `src/render/lines.cpp:50`, igual ao web (`trackview.js:416`).
- **Impacto**: atrás de um morro, a linha aparece "flutuando" na frente do terreno, o que confunde a leitura de profundidade (P3, escolha de paridade).
- **Solução**: opção "linhas atrás do terreno" no menu Exibir, com depth test e `glDepthFunc(GL_LEQUAL)` mais um pequeno deslocamento em Y, ou desenhar duas vezes: forte com depth e tracejado ou fraco sem.

### 18. Recorte por alfa com mipmaps

- **Problema**: o shader descarta com `alpha < 0.4` sobre `GL_LINEAR_MIPMAP_LINEAR`. Nos níveis menores, o alfa médio cai e as copas somem ao longe.
- **Evidência**: FACT (`src/render/track_shader.cpp`, linha `if (uCut == 1 && tx.a < 0.4) discard;`, e `texture.cpp:63-64`). O efeito visual é HYPOTHESIS: as texturas sintéticas têm poucos pixels.
- **Solução**: no `TextureCache::load`, escalar o alfa de cada nível de mip para preservar a cobertura (gerar os mips na CPU), ou usar `GL_SAMPLE_ALPHA_TO_COVERAGE` com MSAA.

### 19. Memória morta e índices largos

- **Problema**:
  - `library_` (todas as malhas de objects.bin) e `Track::raw` (o DOM inteiro do track.json) ficam vivos a sessão toda, mas só são usados na construção. No estresse são 11 kB e 35 kB; na Montalegre, provavelmente MBs (HYPOTHESIS).
  - `read_dr2m` expande índices de 16 bits para 32 bits, na CPU e na GPU.
- **Evidência**: FACT. `src/app/track_view.hpp:118`, `track_view.cpp:50-51` (único uso), `src/core/track.hpp:47`, `src/core/dr2m.cpp:28-33`.
- **Solução**:
  - Liberar `library_` depois de `InstanceRenderer` (`library_.clear(); library_.shrink_to_fit();`) e soltar `raw` depois de `read_track`.
  - Manter os índices de 16 bits por malha e desenhar com `glDrawElementsBaseVertex` (GL 3.2), o que reduz o IBO em até metade nas malhas estreitas.

### 20. `objects.bin` obrigatório

- **Evidência**: FACT. `read_file(join_path(dir, "objects.bin"))` no construtor lança uma exceção, e a pista não abre mesmo que só se queira ver o terreno (`src/app/track_view.cpp:50`).
- **Solução**: se o arquivo faltar, usar uma biblioteca vazia e avisar na barra de status.

---

## Comandos para repetir

```bash
S=/tmp/claude-0/-home-user-DR2Hook/04e0f535-6bba-572c-b584-3c3caa091cdf/scratchpad/r3eval
ST=/home/user/DR2Hook/build/stress/tracks/synthetic__stress
R=/home/user/DR2Hook/build/uiview/tracks/synthetic__dr2hook_ring
export LD_LIBRARY_PATH=/opt/sdl3/lib
Xvfb :197 -screen 0 1600x900x24 &   # display próprio (o :95 é compartilhado)
export DISPLAY=:197

# builds
cmake -S $S/src/tools/viewer3d -B $S/build -G Ninja -DCMAKE_PREFIX_PATH=/opt/sdl3 && cmake --build $S/build
cmake -S $S/inst -B $S/build_inst -G Ninja -DCMAKE_PREFIX_PATH=/opt/sdl3 && cmake --build $S/build_inst

# carga, FPS e pico de RSS
cd $S && $S/rss.sh $S/r1.out $S/build/viewer3d --track $ST --out $S/e.json --fresh --vsync 0 --frames 60
# FPS por câmera (padrão, rente ao chão, panorâmica), com e sem painéis
for cam in "" "0.8,0.0,40,0,100,0" "0.8,1.4,15000,-1200,100,-1400"; do for pan in 1 0; do
  $S/build/viewer3d --track $ST --out $S/e.json --fresh --vsync 0 --frames 10 --panels $pan ${cam:+--camera $cam} | grep -o "fps=[0-9.]*"; done; done
# etapas do quadro e protótipo de frustum
PROF=1 $S/build_inst/viewer3d --track $ST --out $S/e.json --fresh --vsync 0 --frames 10 2>&1 | grep PROF
FRUSTUM=1 PROF=1 $S/build_inst/viewer3d --track $ST --out $S/e.json --fresh --vsync 0 --frames 10 2>&1 | grep -E "PROF (frus|terreno)"
# memória retida
TRIM=1 $S/rss.sh $S/r2.out $S/build_inst/viewer3d --track $ST --out $S/e.json --fresh --vsync 0 --frames 5
# edits.json grande (pista com idnums únicos em $S/tracks/stress_u; arquivos $S/edits_{1000,10000,50000}.json)
cp $S/edits_50000.json $S/res.json && PROF=1 $S/build_inst/viewer3d --track $S/tracks/stress_u --out $S/res.json --vsync 0 --frames 3 2>&1 | grep -E "PROF (resume|unsaved)"
# trickle do ImGui (abre o estresse, seleciona, 100 x 'e', mede quando o menu abre)
$S/trickle.sh X=1; $S/trickle.sh NOTRICKLE=1
# casos de borda
for t in e1_sem_inst e2_sem_ia "pista com espaço çãé" e5_tex; do $S/build/viewer3d --track "$S/tracks/$t" --out "$S/tracks/o.json" --fresh --frames 20; done
SDL_VIDEO_X11_SCALING_FACTOR=1.5 $S/build/viewer3d --track $R --out $S/h.json --fresh --frames 10 --screenshot $S/h.ppm
# troca de pista pelo menu e sessão longa: pasta $S/wd (build/uiview/tracks com links para ringue, estresse e estresse2),
# $S/x.sh (funções xdotool com a janela em 80,45), $S/mon.sh PID ARQ (amostra RSS), $S/long.sh SEGUNDOS (ações aleatórias)
cd $S/wd && GLCOUNT=1 $S/build_inst/viewer3d --track build/uiview/tracks/synthetic__dr2hook_ring --out $S/long.json --fresh &
```

Arquivos de apoio em `$S`: `instrumentacao.diff`, `rss.sh`, `mon.sh`, `x.sh`, `long.sh`, `trickle.sh`, `tracks/` (casos de borda e `stress_u`), `edits_*.json`, e as capturas `a.png` (estresse padrão), `s6c.png`/`s7c.png` (histórico), `e3a.png` (ID), `edge12.png` (sem IA / nomes longos), `w1c.png` (320×240), `h.png` (escala 1,5) e `e5c.png` (textura NPOT).
