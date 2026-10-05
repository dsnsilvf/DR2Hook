# Os 5 slots de fantasma: papéis, por que só 2 carros, e limites para N > 5

Análise só de leitura feita em 2026-10-04 com o jogo aberto (pid do `Z:\...\dirtrally2.exe`, especial offline). O jogo estava com o hook de 3 carros do GhostLab ativo (`dr2hook_ghost_cars.txt` valia 3 na carga; agora vale 4) e com 4 cópias de volta feitas pelo F7. As leituras ao vivo abaixo refletem esse estado e, onde isso importa, está dito.

Legenda: **FATO** = conferido no binário (endereço + trecho) ou lido na memória viva; **HIPÓTESE** = dedução plausível ainda sem teste; **DESCONHECIDO** = não determinado.

Ferramentas usadas (fora do repo, em `~/.claude/jobs/371cb0a9/tmp/agent_ghost_slots/`): desmontagem completa do `.text` por faixas do `.pdata` com capstone (`all.txt`), busca de xrefs RIP-relativas (`xref.py`) e leitor de `/proc/pid/mem` (`mem.py`).

---

## 0. Resumo em 8 linhas

1. Os 5 slots **não são genéricos**. Cada entrada de fantasma da sessão tem um **tipo** (byte `+0x10` do registro de 0x38 bytes em `sessão+0x3320`, vindo de `registro+0xb0`): **0 = fantasma do próprio jogador** (o "seu melhor", salvo), **1 = fantasma de outro jogador do ranking** (amigo/global), **2 = `RecordingGhost`** (a melhor volta feita *nesta sessão*). Os registros de enchimento também levam tipo 1, com id 0.
2. A conta é **4 escolhidos + 1 RecordingGhost = 5**. A lista de fantasmas escolhidos tem capacidade fixa **4** (um próprio + até 3 do ranking). A UI diz `ghosts_total_count = 3` (`0x1402874c8`), o que corresponde a `ghost_1..3`.
3. Offline só existe 1 fantasma escolhido (o próprio, id −1), porque os do ranking vêm da rede. A lista fica assim: 1 próprio + `RecordingGhost` + 3 enchimentos. Por isso **nascem 2 carros**: o próprio e o da gravação.
4. Desses 2, **só 1 é desenhado por vez, de propósito**. `PickVisibleGhost` alterna entre o tipo 0 e o tipo 2, mostrando o mais rápido. O carro da gravação (`car 3`) nasce oculto (`registro+0x00 = 0`).
5. Os 3 enchimentos são as **vagas para fantasmas do ranking**. Elas também são usadas *dentro* da especial pelo ranking da pausa (`0x14029f640` pega a entrada com id 0, até 5 ativas). Nesse caminho o carro não é recriado, porque o spawn só roda na carga.
6. **Não existe um array fixo de 5 controladores/slots.** `BindGhostController` aloca slot (`0x258`, gravador) e controlador (`0x70`) **por entrada**, no alocador do gerenciador, e insere no `std::map`. A chave do mapa é o **ponteiro da entrada da sessão**, não um id. O "5" vem de três capacidades fixas: a lista de 4 escolhidos e os vetores de 0x38 com capacidade 5, um na montagem da corrida e outro na sessão.
7. Os limites duros a seguir, por ordem de quem estoura primeiro, são: o vetor de 0x38 da sessão e da montagem (5, armazenamento **embutido** no objeto), o laço de enchimento `5 - n` (com n > 5, conta ~4 bilhões de voltas), os **16 objetos de render de carro** (`0x1409462e7: cmp ebx, 0x10`), os **24 corpos de física ativos** (`0x140da7410: cmp edx, 0x18`), os 24 ponteiros por parâmetro de `GhostCarValues` e o pool de 150 entradas.
8. Duas correções aos "fatos" de partida. A saída de `EvaluateGhostState` vai para a **pilha** (`r9 = rsp+0x20` em `0x1405184ed`), não para `controlador+0x48`, que é um ponteiro. E `registro+0x08` é um **id** (u64, −1 = local), não um ponteiro. O objeto em `+0x10..+0x20` não é dono de nada (o destrutor `0x140c8f160` só regrava vtables), então copiar o registro de 0xb8 bytes não causa double free *pelo registro*.

---

## 1. Para que servem os 5 slots

### 1.1 Onde a lista de 5 é montada (FATO)

`0x1405ba200` (nome provável: **`RaceSetup_EnterStage`**) é o 16º ponteiro (`+0x78`) de uma tabela de callbacks de estado em `0x14128ca08`. O 15º e o 17º ponteiros dessa tabela também são dela. A função não tem chamador direto. `this` (r15, recarregado de `[rbp+0xb9d0]`) é o objeto cujo construtor é `0x140577740`.

O construtor fixa a lista de escolhidos com **capacidade 4 e armazenamento embutido**:

```
140577785  mov qword ptr [rdi+0x3710], rcx
14057778e  mov qword ptr [rdi+0x3720], rax     ; dados = this+0x3740 (embutido)
14057779b  mov qword ptr [rdi+0x3730], rcx     ; contagem = 0
1405777a9  mov qword ptr [rdi+0x3728], 4       ; capacidade = 4
1405777b4  mov qword ptr [rdi+0x3a20], rcx     ; campos seguintes (0x3740 + 4*0xb8 = 0x3a20)
```

Dentro de `0x1405ba200`, as entradas de fantasma são criadas em três passos:

```
; (a) cada registro escolhido (this+0x3720, passo 0xb8), 6º argumento = 1
1405ba855  imul rdi, [r15+0x3730], 0xb8
1405ba880  mov r9d, [rbx+0xac]   ; id do participante (0 → resolve por +0xa8)
1405ba95b  mov byte [rsp+0x28], 1
1405ba972  call 0x14057df00      ; AddGhostEntry(this, &vet0x38, gerenciador, id, registro, 1)

; (b) um registro "RecordingGhost", tipo 2, 6º argumento = 1
1405baa61  mov word  [rbp+0xb0], 0            ; registro+0x00 = 0 → nasce sem desenho
1405baa6a  mov qword [rbp+0xb8], r12          ; registro+0x08 = 0x14060bc80([0x141695200]) (−1 se não houver)
1405baa82  lea rdx, [0x14128d6d8]             ; "RecordingGhost" → nome em registro+0x28
1405baa96  mov byte [rbp+0x160], 2            ; registro+0xb0 = 2
1405baacf  call 0x14057df00

; (c) enchimento até 5, registros vazios: id 0, nome "", tipo 1, 6º argumento = 0
1405bac01  sub eax, [rbp+0xe90]   ; eax = 5 - contagem do vetor 0x38
1405bac92  mov byte [rbp+0x240], 1            ; registro+0xb0 = 1
1405bacaf  mov byte [rsp+0x28], r12b          ; 6º argumento = 0 → controlador+0x63 = 0
1405bacc9  call 0x14057df00
```

`[rbp+0xe80]` é um vetor de registros de 0x38 bytes **com capacidade fixa 5**. Ele fica dentro da estrutura local de montagem em `rbp+0x720`, criada por `0x140577a20`:

```
140577b72  mov qword ptr [rbx+0x760], rax      ; dados = +0x780 (embutido)
140577b79  mov qword ptr [rbx+0x768], 5        ; capacidade 5
140577b84  mov qword ptr [rbx+0x770], rdi      ; contagem
140577b8b  mov qword ptr [rbx+0x8a0], rdi      ; próximo campo logo após 5*0x38
```

O equivalente na sessão `[0x1416951e8]` é criado no construtor `0x140413bc0`:

```
140413ce8  mov qword ptr [rbx+0x3328], 5       ; capacidade 5
140413cfa  mov qword ptr [rbx+0x3320], rax     ; dados = +0x3340 (embutido, até 0x3458)
140413d08  mov qword ptr [rbx+0x3330], rdi
140413d19  mov qword ptr [rbx+0x3470], rax     ; próximo campo
```

Ao vivo (FATO, `sessão = 0x15f887a0`): `+0x3320 = 0x15f8bae0`, capacidade 5, contagem 5.

### 1.2 O registro de 0xb8 bytes (`GhostRecord`) (FATO)

Layout reconstruído pelo construtor `0x140249f20` (`BuildGhostRecord`), pela atribuição `0x14024b080` e por `AddGhostEntry`:

| offset | conteúdo |
|---|---|
| `+0x00` | byte: desenho inicial (`SetControllerDrawFlag` em `0x14057e044` recebe `movzx r8d, byte [rdi]`) |
| `+0x01` | byte (vai para `0x38+0x30`) |
| `+0x08` | u64: **id do fantasma** (do ranking; −1 = local; 0 = vazio → `entrada+0xb4 = 1`) |
| `+0x10/+0x18` | vtables `0x141233468`/`0x141233420` de um "handle" sem dono (o destrutor `0x140c8f160` só regrava vtables) |
| `+0x20` | ponteiro cru (vai para `0x38+0x28`) |
| `+0x28` | `char[0x80]`: nome (`"RecordingGhost"`, nome do jogador, `""`) |
| `+0xa8` | u32 |
| `+0xac` | u32: id do participante (se 0, `0x1405ba890` resolve por `+0xa8`) |
| `+0xb0` | byte: **tipo** (0 próprio, 1 ranking/enchimento, 2 gravação) |

O registro de 0x38 bytes (sessão `+0x3320`) é montado em `0x14057df85..0x14057dfbd`: `+0x00` id (= `registro+0x08`), `+0x08` ponteiro da entrada da sessão (gravado depois, em `0x14057e011`), `+0x10` tipo (= `registro+0xb0`), `+0x18/+0x20` vtables, `+0x28` = `registro+0x20`, `+0x30` = `registro+0x01`.

### 1.3 Quem preenche a lista de escolhidos e o que o tipo significa (FATO)

- O único construtor de `GhostRecord` é `0x140249f20`, e o único chamador dele é `0x1402a6b0b`, dentro de `0x1402a6a00` (nome provável: **`Leaderboard_ToggleGhost`**). Ela é chamada da tela de ranking `0x140286be0` (strings `friends_global_state`, `lng_global_filter`, `is_ghost_time`, `back_ghosts`; ação de hash `0xc80a2ceb`), sobre uma linha de ranking de 0xd8 bytes.
- O tipo vem do 7º argumento: `bpl = (id da linha != [ui+0xd8])` (`0x1402a6aa5..0x1402a6aaa`). Ou seja, **0 = a linha do próprio jogador, 1 = outro jogador**. O desenho inicial (`+0x00`) é sempre 1 (`0x1402a6aea: mov byte [rsp+0x38], 1`).
- Fora da especial (`0x140562000()` = objeto de montagem `[0x1416953c0]` ativo), a função empurra o registro em `[0x1416953c0]+0x20` (dados), `+0x28` (capacidade), `+0x30` (contagem), só se `contagem < capacidade` (`0x1402a6a92: cmp rdi, rsi; jae`). Se o fantasma já está na lista, remove (`0x140277230`).
- Essa lista viaja como mensagem: `0x1405af3f0` (caso 1 do despachante `0x1405989f0`) copia o payload para `this+0x3720` (`0x1405af415..0x1405af465`). `0x1405a4750`/`0x1405a4a90` copiam de volta para `[0x1416953c0]+0x20` e `[0x1416953d8]+0x10`. **Toda cópia** usa `0x14024b080`, que para silenciosamente na capacidade (`0x14024b0ca: cmp rax, [rbx+8]; jae`). Toda instância temporária cria a lista com capacidade 4 (`0x1402492ee`, `0x14037b5fa`, `0x1405a9710`).
- Ao vivo (FATO): `[0x1416953c0] = 0x15fe5a00`, lista com capacidade 4 e contagem 1. O registro é `+0x00 = 1`, id `0xffffffffffffffff`, `+0xa8 = 0x218`, `+0xac = 0xb5e`, **tipo 0**. `[0x1416953d8]+0x10`: capacidade 4, contagem 0.

**Quem preenche cada slot com dados de volta (FATO):**
- **Tipo 2 (`RecordingGhost`)**: `0x140504b80` (método do gerenciador) acha a entrada de tipo 2 (`0x140422eb0(sessão, 2)`). Ela copia o gravador corrente (`gerenciador+0x38`) para o slot dessa entrada com `CopyGhostLapData` (`0x140504c71`) se o slot está vazio ou se a volta nova é mais rápida (`0x140920e40`, tempos ofuscados por XOR com `0x140183d60()`), e marca `gerenciador+0x32 = 1`. Ou seja: **é a melhor volta feita nesta sessão**.
- **Tipo 0 (próprio)**: `0x1405e393a` acha a entrada de tipo 0 (`0x140422eb0(sessão, 0)` em `0x1405e3911`) e copia para o slot dela, com `CopyGhostLapData` (`0x1405e3a18`), a volta carregada em `[rdi+0x188]` (fantasma salvo), conforme o tempo. Também zera `entrada+0xb4` (`0x1405e39f9`).
- **Tipo 1 do ranking**: o download online (`StateTimeTrialGhostDownload`, `GhostCar.Download`), bloqueado pelo NetworkGuard. DESCONHECIDO: a função exata que grava o slot no download.

**Chave do `std::map`** (FATO): `BindGhostController` compara `[nó+0x20]` com `rdx` = **ponteiro da entrada da sessão** (`0x140511ac0: cmp [rax+0x20], r15`). Ao vivo, as chaves eram `0x15fa19a0`, `0x15fa1ae0`, `0x15fa1c20`, `0x15fa1d60` e `0x15fa1ea0`, exatamente `sessão+0x30[1..5]`. Por isso a ordem do mapa é a ordem dos endereços das entradas do pool.

### 1.4 Visibilidade: `gerenciador+0x30/+0x31/+0x32` (FATO)

- `+0x31` = opção do perfil **"Fantasma ligado/desligado"**. `0x1405161c0` (**`SetGhostsVisible(mgr, bool)`**: `mov [rcx+0x31], dl` e copia para todo `controlador+0x62`) é chamada de:
  - `0x1405ba812` (entrada na especial), com o valor de `0x1405dcc90()+0x40`, um mapa de opções por usuário em `[0x141695268]`;
  - `0x1402cd77a`, na tela de opções: id de opção 7, `dl = ([rdi+0x100] == 1)`.
- `0x140516130` = **`SetGhostDrawn(mgr, entrada, v)`**: `controlador+0x62 = (mgr+0x31 ? v : 0)`.
- `0x140508250` = **`IsGhostDrawn(mgr, entrada)`**: devolve `controlador+0x62` se `mgr+0x31`.
- `+0x30` liga a troca automática para a gravação quando ela fica mais rápida (teste em `0x140516009`).
- Ao vivo: `mgr+0x30..0x33 = 01 01 00 00`. O construtor do gerenciador (`0x1404f6e40`) grava `+0x30` como 0x100: `+0x30 = 0`, `+0x31 = 1` por padrão.

---

## 2. Por que só 2 carros nascem e só 1 aparece

### 2.1 O mecanismo, passo a passo (FATO)

1. **Na montagem**: offline, a lista de escolhidos tem 1 registro (próprio, tipo 0, id −1). Somado ao `RecordingGhost`, o vetor de 0x38 fica com 2 registros com id. O laço `0x1405bac01` completa com `5 - 2 = 3` registros vazios.
2. **`AddGhostEntry` (`0x14057df00`)**, para cada registro:
   - tira uma entrada do pool (`0x1404c09c0([0x1416951f0], id)`) e grava o nome (`0x1405164e0`);
   - chama `BindGhostController` (cria slot e controlador se a entrada for nova);
   - grava o ponteiro da entrada no vetor de 0x38;
   - faz `controlador+0x63 = 6º argumento`;
   - se `registro+0x08 == 0`, faz `entrada+0xb4 = 1` (`0x14057e02c..0x14057e033`);
   - por fim, `SetGhostDrawn(entrada, registro+0x00)`.
3. **No spawn**, o passe 2 de `SpawnStageVehicles` pula as entradas com `+0xb4 = 1`. Sobram `car 2` (próprio) e `car 3` (gravação).
4. **No desenho**: o próprio nasce com `+0x62 = 1` (registro `+0x00 = 1`). A gravação nasce com `+0x62 = 0` (`0x1405baa61` grava `+0x00 = 0`).
5. **`PickVisibleGhost` (`0x140515f00`)** acha a primeira entrada de tipo 2 (`rsi`) e a primeira de tipo 0 (`rbp`) via `0x140422eb0`, e lê os tempos dos slots prontos (`0x1409d2bb0`, `slot+0x30` ofuscado):
   - se a gravação tem tempo e é mais rápida (`0x140920e40`) e `mgr+0x30`:
     - se nenhuma das duas está desenhada e o slot do próprio está pronto, mantém as duas ocultas;
     - senão, desenha a gravação e oculta o próprio (`0x140516050..0x140516098`);
   - caso contrário: se a gravação estava desenhada e o slot dela está pronto, passa o desenho para o próprio; e **sempre** oculta a gravação (`0x140516064..0x140516098`).

   Chamada de `0x140509c6c`, `0x140509c93`, `0x14050a875`, `0x14050a89c` e `0x140512dda`. HIPÓTESE: largada/reinício.

Conclusão: **"2" não é limite**. É 1 fantasma escolhido (o único possível offline) mais o slot da gravação. E o desenho mostra **um** dos dois: o mais rápido entre "seu melhor salvo" e "seu melhor desta sessão". O `car 3` invisível é o carro da gravação, à espera de ser trocado pelo próprio.

### 2.2 O que são as 3 vagas extras (FATO + HIPÓTESE)

- FATO: são vagas para **fantasmas de outros jogadores** (tipo 1):
  - a UI conta exatamente esses. Fora da especial, conta registros com `+0xb0 == 1` em `[0x1416953c0]+0x20` (`0x1402b5b30`). Dentro dela, conta registros de 0x38 com id ≠ 0 e tipo 1 (`0x1402b5c45..0x1402b5c51`) e grava em `ghosts_current_count` (`0x1402b5c95`);
  - o total exibido é a constante 3 (`0x1402874c8: mov dword [rbp+0x67], 3` → `ghosts_total_count`, só se `show_ghosts_counter`, `[rbx+0x179]`);
  - a UI tem `ghost_1..3_visible` e `ghost_1..3_header`.
- FATO: **dentro da especial**, o mesmo `Leaderboard_ToggleGhost` segue outro ramo (`0x1402a6b6b..0x1402a6c35`):
  - conta controladores com `+0x63 != 0`;
  - se o fantasma já está ativo, remove (`0x14029adb0`: limpa o slot com `0x1409d76f0`, faz `id = 0`, `+0x63 = 0`, `entrada+0xb4 = 1`);
  - se há **menos de 5 ativos** (`0x1402a6c05: cmp edi, 5`), pega a entrada de id 0, ou seja, um enchimento (`0x140422e50(sessão, 0)`, que pula o tipo 2), e chama `0x14029f640` (**`AddInStageGhost`**). Esta faz `entrada+0xb4 = 0`, grava nome e carro (`0x1404d4320`, `entrada+0x98`), liga o desenho e `+0x63 = 1`, e limpa o slot para receber o download.
- FATO: não há outro chamador de `CreateStageVehicle` além de `SpawnStageVehicles` (`0x14046b3a6`, `0x14046b400`), e `SpawnStageVehicles` só é chamado de `0x1404a892b`. Logo, um fantasma posto numa vaga **durante** a especial não ganha carro até a próxima carga completa.
- HIPÓTESE: o caminho dentro da especial foi pensado para um fluxo em que a pausa leva a uma recarga, ou é resquício de outro jogo da série (DiRT 4 tem a mesma UI de ranking). Não testado. Também não se sabe se o desenho de um carro sem veículo é inofensivo; o atualizador `~0x140518400` lê `controlador+0x00`.

### 2.3 O que decide o 5 e o 4

| constante | onde | o que limita |
|---|---|---|
| 4 | `0x1405777a9`, `0x1402492ee`, `0x14037b5fa`, `0x1405a9710`, e a capacidade de `[0x1416953c0]+0x28` (lida ao vivo: 4) | fantasmas escolhidos (próprio + 3 do ranking) |
| 3 | `0x1402874c8` | só o contador da UI |
| 5 | `0x1405ba800` e `0x1405ba987` (`mov r13d, 5`), usado em `0x1405bac01` | alvo do enchimento |
| 5 | `0x140577b79` (montagem `+0x768`), `0x140413ce8` (sessão `+0x3328`) | vetor de 0x38 (embutido) |
| 5 | `0x1402b5b76` (`mov esi, 5`, vetor local de 0x38 na UI) | cópia para a UI |
| 5 | `0x1402a6c05` (`cmp edi, 5`) | fantasmas ativos adicionados na especial |
| 150 | `0x1405ba653`, `0x1405bad19` (`0x96`), pool `contexto+0x4e0` | entradas da sessão / vetores de veículos |

`[config+0xd0]` não entra: em `0x1405ba5d7` é um ponteiro de alocador (`[r12+0xd0]`, chamada `[rax+0x18]` com `edx = 0x960 = 150 * 0x10`), não uma contagem de fantasmas.

---

## 3. Limites reais para passar de 3–5 carros

Ao vivo, com 3 carros fantasma: 6 entradas na sessão; 4 corpos de física ativos (`[0x14201b930] = 4`); 4 ponteiros em `GhostCarValues`.

| # | estrutura | capacidade | evidência | ao estourar |
|---|---|---|---|---|
| 1 | lista de escolhidos (`GhostRecord`) | **4** | `0x1405777a9`; `0x14024b0ca` | o excedente some em silêncio |
| 2 | vetor de 0x38 da montagem (`rbp+0xe80`) e da sessão (`+0x3320`) | **5**, armazenamento embutido | `0x140577b79`, `0x140413ce8`; o campo seguinte vem logo depois (`+0x8a0`; `+0x3470`) | `AddGhostEntry` não empurra (`0x14057df83: jae`), mas **grava a entrada nova em `[último]+0x08`** (`0x14057e009..0x14057e011`, com a contagem velha): o 5º registro perde a ligação com a entrada dele, e a sessão e o mapa ficam com uma entrada a mais |
| 3 | enchimento `5 - n` | n ≤ 5 | `0x1405bac01..0x1405bac0d` (`mov esi, eax`, laço `sub rsi, 1; jne`) | com n = 6, `esi = 0xffffffff`: ~4 bilhões de `AddGhostEntry` → esgota o pool (150) e trava/cai |
| 4 | pool de entradas da sessão | **150** | `0x1404c09c0`; vetores de 150 em `0x1405ba653`/`0x1405bad19` | DESCONHECIDO: o que `0x1404c09c0` devolve sem entrada livre (provável nulo → AV em `0x1405164e0`) |
| 5 | slots e controladores | **sem teto fixo** | `BindGhostController` (`0x140511a80`): `0x258` via `[mgr]->vt+0x18` + construtor `0x1409cb190` (slot = gravador); objeto de 0x80 (`0x140511baa`); controlador de 0x70 (`0x140511c17`, construtor `0x1404f7190`, que grava `+0x60 = 0x01010000`); nó do mapa de 0x48 (`0x140511d05`) | só memória (~0x258 + 0x80 + 0x70 + 0x48 por entrada, mais as amostras do slot, que chegam a ~30 min de capacidade no gravador; ver `ghosts.md` §3) |
| 6 | objetos de render de carro | **16** | `0x140946294`: `[rsi+0x2108] = rsi+0x2110` (embutido, passo 0x1730); laço até `0x1409462e7: cmp ebx, 0x10`; lista livre de 16 `u32` em `+0x194a8` | DESCONHECIDO o efeito ao esgotar (falha em `0x140986280`/`0x140b92c83`). Jogador + fantasmas (+ outros carros da cena) ≤ 16 → **no máximo ~15 fantasmas** |
| 7 | `GhostCarValues` e parâmetros por carro | **24** por parâmetro | `0x140969195`: `[r15+rbp*8+0x1a0f0]`; o próximo array começa em `+0x1a1b0` (`0x1409691a8`), 0xc0 = 24 ponteiros; seguem `+0x1a270`, `+0x1a330`, `+0x1a3f0` | indexado pelo índice do render de carro (< 16), então não estoura antes do item 6. Ao vivo: `bloco = 0x4b395210`, 4 ponteiros em `+0x1a0f0..+0x1a108` e zeros depois |
| 8 | lista de corpos de física ativos | **24** | `0x140da7410: cmp edx, 0x18 / jge` (inclusão em `0x14201b7b0`, contagem `0x14201b930`); a segunda lista vai de `0x14201b870` a `0x14201b934` | o corpo excedente **não entra na lista** (sem erro): o passo `0x140dbc500` não o atualiza. Ao vivo: 4 |
| 9 | UI dentro da especial | 5 | `0x1402b5b76`, `0x1402a6c05` | só a UI |
| 10 | fantasma "de comparação" | 1 de cada tipo | `0x140422eb0` devolve o **primeiro** do tipo | um 2º tipo 2 ou tipo 0 é ignorado por `PickVisibleGhost`, por `0x140504b80` e por `0x1405e393a` |

**O que é alocado por carro fantasma** (FATO parcial): a entrada da sessão (do pool); slot (0x258), objeto de 0x80, controlador (0x70) e nó do mapa (0x48); o veículo (`CreateStageVehicle`, 0x980, `"car %u"`) com corpo de física (`veículo+0x30`); um objeto de render de 0x1730 (fixo, um dos 16); e os ponteiros de parâmetros. Os recursos de modelo e textura são do carro. HIPÓTESE: são compartilhados se o carro for o mesmo, porque as cópias usam o mesmo `+0xac`/`+0xa8`. DESCONHECIDO: áudio por carro (fantasma provavelmente sem som) e o `GhostedTransparencyManager` (`0x140056a60` é só o registro do tipo; não achei array por carro).

**Estimativa para N grande:**
- **N = 8**: passa nos itens 6–8. Exige mudar os itens 1–3 (e 9 se quiser a UI).
- **N = 16**: estoura os 16 render de carro (jogador + 16 = 17), então é preciso saber o que acontece no item 6.
- **N > 23**: estoura também os 24 corpos de física.

---

## 4. Como os controladores e slots são criados; dá para subir o `mov r13d, 5`?

- FATO: os controladores não são um array de 5. `BindGhostController` (chamado em `AddGhostEntry` `0x14057e004`, em `BuildParticipants` `0x1404bdcf3` e em `0x1405b1f17`) procura a entrada no mapa. Se não acha (`0x140511b98: cmp rsi, [r12+0x18]; jne`), aloca slot, objeto e controlador e insere (`0x1404dd050`). Ao vivo os controladores estão a cada 0x100 (`0x6b72a080`, `…180`, …), com um cabeçalho de 0x10 do alocador em `-0x10` (`0x6cf00b02`, `0x14083fe4a`). HIPÓTESE: é um alocador por classe de tamanho, não um bloco de 5.
- FATO (contesta o ponto de partida): `controlador+0x48` é um **ponteiro** (ao vivo `0x6b72a000`; em `0x1405184d3` lê-se `[rdi+0x48]` e depois `[rax+0x30]`). A saída de `EvaluateGhostState` vai para a pilha (`0x1405184ed: lea r9, [rsp+0x20]`). O dono é `controlador+0x08`, e `[dono+0x20]` = `controlador+0x28` = slot.
- **Subir `mov r13d, 5` sozinho NÃO é seguro**. Com alvo 6 e 6 registros, o 6º `AddGhostEntry` cai no item 2 (capacidade 5 embutida): não empurra e sobrescreve `[último]+0x08`. O vetor da sessão também tem capacidade 5 embutida, e a cópia para ele (`0x1404389d0..0x140438a1b`) também para na capacidade.
- Para N > 5, é preciso mudar **juntos**:
  1. os dois `mov r13d, 5` (`0x1405ba800`, `0x1405ba987`), ou não depender do enchimento;
  2. a capacidade e os dados do vetor de 0x38 da montagem (`rbp+0xe80` = montagem `+0x760/+0x768`) e da sessão (`+0x3320/+0x3328`). Como o armazenamento é embutido e colado no campo seguinte, não basta mudar a capacidade: é preciso **apontar `+0x3320`/`+0x760` para um buffer externo** (por exemplo, estático no mod) com a capacidade nova. HIPÓTESE: os iteradores usam sempre `[+0x3320]` e `[+0x3330]`, o que favorece isso. DESCONHECIDO: se algum destrutor ou cópia compara com o endereço embutido;
  3. para fantasmas reais (não cópias), a lista de escolhidos (capacidade 4 embutida em `this+0x3740`, colada em `+0x3a20`), pela mesma técnica;
  4. opcional: `0x1402a6c05` e `0x1402b5b76` para a UI.
- Alternativa de menor risco, que o hook atual já usa: **manter n ≤ 5** e trocar os enchimentos por registros com id ≠ 0. Até 5 carros não há nenhuma estrutura de capacidade fixa a mudar.

---

## 5. Hipóteses, riscos e próximos experimentos (do menor risco para o maior)

### 5.1 Achados que mudam o hook atual

- FATO: o hook copia "o último registro real visto", que é o **`RecordingGhost`** (tipo 2, chamado depois dos escolhidos). Ao vivo, as entradas 2 e 3 do vetor de 0x38 têm tipo 2 (`15f8bb18+010 = 2`, `15f8bb50+010 = 2`). Efeitos:
  - (a) a cópia nasce com `+0x00 = 0` (oculta), por isso o mod tem de ligar `+0x62`;
  - (b) `0x140504b80` grava a melhor volta da sessão só no **primeiro** tipo 2; as cópias ficam com o que o mod puser;
  - (c) o nome é `"RecordingGhost"`.
- Recomendação (HIPÓTESE, não testada): copiar o registro de **tipo 0** (o primeiro real, `+0x00 = 1`) ou montar um tipo 1 com id próprio (≠ 0, ≠ −1). Assim o desenho nasce ligado e não se confunde com a gravação. Cuidado: `0x140422e50` procura por id; dois registros com o mesmo id confundem a busca da UI.
- FATO: `registro+0x08` é um id, e o destrutor do "handle" `+0x10` não libera nada (`0x140c8f160`). O risco de double free *pela cópia do registro* fica refutado. DESCONHECIDO: a desmontagem dos slots ao sair da especial (cada slot tem buffers próprios; `CopyGhostLapData` aloca).

### 5.2 Leituras ao vivo (risco zero)

1. **Na tela de seleção, antes da especial**: `q([0x1416953c0]+0x20)` (dados), `+0x28` (capacidade, espera 4), `+0x30` (contagem); para cada registro, 0xb8 bytes (`+0x00`, `+0x08`, `+0x28` nome, `+0xa8`, `+0xac`, `+0xb0`). Confirma que offline só há o próprio.
2. **Depois de terminar uma passada e Reiniciar**: `gerenciador+0x32` (vira 1); `slot+0x1a8` do slot da entrada de tipo 2 (vira 2); `controlador+0x62` das entradas 1 e 2, para ver a troca do `PickVisibleGhost` quando a passada foi mais rápida.
3. **Render de carro**: `[r13+0xf8]` → `+0x194a8` (16 `u32` da lista livre) e `+0x2108` (passo 0x1730, byte `+0x1b0` ocupado). Mede quantos dos 16 estão em uso numa especial normal (jogador + 2). DESCONHECIDO: o caminho até `r13`. Ponto de partida: o chamador de `0x140b923a0`.
4. **Física**: `[0x14201b930]` e `[0x14201b934]` com 3 e 4 carros fantasma.

### 5.3 Varreduras estáticas (risco zero) ainda por fazer

- O que `0x1404c09c0` devolve sem entrada livre (pool de 150).
- O caminho de falha de `0x140986280` quando não há índice de render livre (decide o teto de ~15).
- Quem lê o vetor de 0x38 com índice fixo (`imul ..., 0x38` com constante), para achar outras suposições de "≤ 5".
- Destrutores da sessão (`+0x3320`) e da montagem (`+0x760`): se liberam `[+0x3320]` quando difere do embutido. Isso é pré-requisito do buffer externo.

### 5.4 Patches, em ordem

1. **4 e 5 carros com o hook atual** (risco baixo): `dr2hook_ghost_cars.txt = 4` e depois `5`. Já não há estrutura fixa no caminho. Conferir `[0x14201b930]` (5/6), os ponteiros de `GhostCarValues` e a saída da especial.
2. **Trocar a fonte da cópia para o registro de tipo 0** (risco baixo): o desenho deve nascer ligado sem forçar `+0x62`. `+0x63` continua vindo do 6º argumento (0 no enchimento), então o mod ainda precisa passar 1 ou ligar `+0x63`.
3. **6+ carros** (risco médio/alto): buffer externo para os vetores de 0x38 da montagem e da sessão, mais o alvo do enchimento. Fazer só depois das varreduras de 5.3. Falhas esperadas: sobrescrita em `[último]+0x08` se faltar algum vetor; laço gigante se o alvo ficar menor que n.
4. **Acima de ~15** (risco alto): exige ampliar os 16 objetos de render (array embutido de 0x1730 bytes cada; impraticável sem realocar a estrutura) e, acima de 23, a lista de 24 corpos (array estático em `.data`, colado em `0x14201b870`). Não recomendo.

### 5.5 O que NÃO consegui determinar

- O efeito exato de esgotar os 16 render de carro e o pool de 150.
- Quem grava o slot de um fantasma do ranking ao baixar (o caminho online está bloqueado).
- Se o caminho "adicionar fantasma do ranking na pausa" chega a ser usado no DR2 e o que acontece com uma entrada ativa sem veículo.
- Áudio por carro e o `GhostedTransparencyManager` (`+0x290`): não analisados a fundo.
- O papel de `[0x1416951f8]+0x3600` (condição de `FillStageEntries`): não reexaminado. Fica como no ponto de partida.
- Os momentos exatos em que `PickVisibleGhost` roda (5 chamadores, não seguidos).
