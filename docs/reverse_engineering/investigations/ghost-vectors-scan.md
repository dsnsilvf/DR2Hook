# Vetores de 0x38 dos fantasmas (montagem e sessão): varredura para buffer externo

Análise feita em 2026-10-04 só por leitura: desmontagem do exe e leitura de `/proc/996404/mem` com o jogo aberto. Não houve escrita na memória, nenhum comando enviado ao jogo e nenhuma mudança de código. Complementa `ghost-slots-analysis.md`, seções 1.1, 2, 3, 4 e 5.3.

Legenda: **FATO** = conferido no binário (endereço + trecho) ou lido na memória viva. **HIPÓTESE** = dedução ainda sem teste. **DESCONHECIDO** = não determinado.

Ferramentas (fora do repo, em `~/.claude/jobs/371cb0a9/tmp/agent_vec_scan/`):
- `rawscan.py`: procura os bytes do deslocamento de 32 bits em todo o `.text`, incluindo funções-folha sem `.pdata`, que o `all.txt` antigo não cobre;
- `after.py`: desmonta a partir de um endereço;
- `ctorsites.py`: acha os acessos à montagem em cada função que a constrói;
- `live_vec.py`: lê o vetor da sessão na memória viva.

---

## 0. Resumo

1. **Ninguém libera esses vetores** (FATO). O tipo é um vetor de capacidade fixa com armazenamento embutido e não tem nenhum caminho de alocação ou liberação. A "destruição" (`0x1401fd020` e o trecho em `0x140435e30`) só chama `0x140c8f160` em `elem+0x18`, que regrava duas vtables, e zera a contagem. Nada compara o ponteiro com o endereço embutido nem chama `free`. Um buffer externo estático do mod **não** leva a free inválido.
2. **Não há small-buffer optimization nem crescimento** (FATO). Todo push é "se `contagem < capacidade`, grava; senão descarta" (`0x14057df83`, `0x1404389de`, `0x14024b1de`, `0x1405b1e39`). `lea [+0x3340]` só aparece no construtor `0x140413cf4`. O que o 6º `AddGhostEntry` faz está confirmado na seção 2.
3. **A cópia montagem→sessão** (`0x1404389a3..0x140438a80`) vai elemento por elemento. Ela usa os dados e a contagem da **origem** (`[montagem+0x760]`, `[+0x770]`) e os dados e a capacidade do **destino** (`[sessão+0x3320]`, `[+0x3328]`). Basta trocar ponteiro e capacidade dos dois lados. Antes da cópia, o reset `0x140435e30` zera só a contagem e não toca em `+0x3320`/`+0x3328`.
4. **`r13d = 5` é uma constante compartilhada.** É ao mesmo tempo o alvo do enchimento e a capacidade de um **vetor local de ponteiros com 5 vagas na pilha** (`rbp-0x80`, dados em `rbp-0x60`, usado em `0x1405bb66b`). Quem alimenta `0x1405bac01`:
   - `0x1405ba987`, quando a lista de escolhidos não está vazia (o caso normal);
   - `0x1405ba800`, quando a lista está vazia.

   Patchar os dois imediatos para N ≥ 6 é seguro: com N ≥ 6 o enchimento sempre roda, e `0x1405bace4` (`lea r13d, [rsi+5]`) devolve 5 ao `r13` antes do uso como capacidade.
5. **Consumidores que assumem ≤ 5 ou ≤ 4** (seção 3):
   - **Perigoso**: `0x1405be820` grava cada entrada de **tipo 1 com id ≠ 0** num array fixo de **4 × 0x130** (`corrida+0xa0040`) **sem checar limite**. Do 5º tipo 1 em diante, transborda. As cópias têm de continuar com tipo 0 (como no commit `0faab8e`) ou tipo 2.
   - **Só truncam**: vetor local na pilha com capacidade 5 (jogador + tipos 0/1), cópias locais de 5 (`0x1401886bc`, `0x1402b5b76`), cópia da UI de capacidade 5 (`0x140249e08`), "pode adicionar" (`0x140266f37: cmp ebx, 5`) e `0x1402a6c05`. Efeito colateral provável: com contagem > 5, abrir e fechar o ranking na pausa sempre marca "fantasmas mudaram" (`mgr+0x33`).
6. **Ordem segura**: (a) sessão: trocar `+0x3320`/`+0x3328` uma vez, quando a sessão já existe e antes de `0x1405bb871`; (b) montagem: trocar no **primeiro** `AddGhostEntry` (o mod já faz hook nele), quando `rdx->dados == rdx+0x20`; (c) patchar os imediatos `0x1405ba802` e `0x1405ba989`. Detalhes na seção 6.

---

## 1. Quem toca nos campos e o que faz (pergunta 1)

### 1.1 Sessão `[0x1416951e8]` (dados `+0x3320`, capacidade `+0x3328`, contagem `+0x3330`, embutido `+0x3340..+0x3458`)

Varredura dos bytes `20 33 00 00`, `28 33 00 00`, `30 33 00 00` e `40 33 00 00` em todo o `.text` (FATO):

| função | endereços | o que faz |
|---|---|---|
| `0x140413bc0` (construtor da sessão; único chamador `0x14055f773`) | `0x140413ce8` cap = 5, `0x140413cf4 lea rax,[rbx+0x3340]`, `0x140413cfa` dados = embutido, `0x140413d08` contagem = 0 | **escreve** (única escrita de dados/capacidade em todo o exe) |
| `0x140416140` (desmontagem da sessão; chamador `0x14056be09`) | `0x140416152 call 0x140435e30(this, 2)`; `0x140416177 lea rcx,[rbx+0x3320]` → `call 0x1401fd020` | **limpa** (destrói elementos, contagem = 0). **Não libera** |
| `0x140435e30` (reset da sessão; chamadores `0x140416152`, `0x14042bb55`, `0x14042d460`, `0x1404381dd`) | `0x14043609a..0x1404360c3`: laço `call 0x140c8f160(elem+0x18)` e depois `mov [rbx+0x3330], 0` | **limpa** só a contagem. Não toca em dados nem capacidade |
| `0x1404381c0` (carga da sessão a partir da montagem; trecho `0x1404389ad`) | `0x1404389d0` contagem, `0x1404389d7 cmp cap`, `0x1404389e4 add rcx,[+0x3320]`, `0x140438a1b inc contagem` | **copia** (push com teto na capacidade do destino) |
| `0x140422e50` (procura por id, pula tipo 2) e `0x140422eb0` (procura por tipo) | `imul [rcx+0x3330],0x38` / `mov [rcx+0x3320]` | **lê** e devolve um ponteiro para dentro do vetor |
| `0x14043a650` | `0x14043a685`/`0x14043a68d` | **lê** (laço até a contagem) |
| `0x1404269b0` = **`GetGhostVector(sessão)`**: `lea rax,[rcx+0x3320]; ret` | 17 chamadores (seção 3) | entrega `&vetor`, e o resto do código usa `[v]`, `[v+8]` e `[v+0x10]` |
| `0x140c573c0`, `0x140c57e81`, `0x140c57f30` | `lea [reg+0x3330]` e depois `[+0x38]` | **falso positivo**: destrutor de `std::function` de outra classe (padrão `cmp rcx, rdi; setne dl; call [vt+0x20]`) |

Os hits de `+0x3340` fora do construtor (`0x140ae4310` e seguintes, `0x140b...`) são de outra classe (render): leem dword e não se misturam com a sessão.

```
; 0x1401fd020: "clear" genérico do vetor de 0x38 (também é o destrutor inline)
1401fd02f  imul rdi, [rcx+0x10], 0x38
1401fd034  mov  rbx, [rcx]
1401fd042  lea  rcx, [rbx+0x18]
1401fd046  call 0x140c8f160          ; só regrava vtables do "handle"
1401fd04b  add  rbx, 0x38 / cmp / jne
1401fd054  mov  qword [rsi+0x10], 0  ; contagem = 0. Sem free, sem tocar em [rcx] nem [rcx+8]
```

Ao vivo (FATO, `sessão = 0x15f887a0`): `+0x3320 = 0x15f8bae0` (= embutido `+0x3340`), capacidade 5, contagem 5. Tipos `00 02 00 00 00` (o próprio, a gravação e 3 cópias de tipo 0, como faz o hook atual); id −1 nos 5. A lista de entradas `+0x30` tem capacidade 150 e contagem 6. Logo após o embutido: `+0x3458..+0x345f` fica vago; depois vêm `+0x3460` (byte), `+0x3464`, `+0x346c` e `+0x3470` (= `0xfffffffffffffffc` ao vivo), gravados pelo reset.

### 1.2 Montagem (objeto local de `0x140577a20`: dados `+0x760`, capacidade `+0x768`, contagem `+0x770`, embutido `+0x780`)

`0x140577a20` tem 7 chamadores. Todos montam o objeto **na pilha** de uma função de estado e passam o objeto a `0x1404381c0` (FATO, `ctorsites.py`):

| função de estado | base | o que faz com o vetor |
|---|---|---|
| **`0x1405ba200`** (entra na especial, contra o relógio) | `rbp+0x720` → vetor `rbp+0xe80` | `0x1405ba817..0x1405ba849` **limpa**; `0x1405ba960`, `0x1405baabd`, `0x1405bacbf` passam `&vetor` a `AddGhostEntry` (**push**); `0x1405bac01` lê a contagem; `0x1405baf1e`, `0x1405bb6c8` e `0x1405bb7aa` **leem**; `0x1405bb86a` → `0x1404381c0` (**copia** para a sessão); `0x1405bb8c1` → `0x1401fd020` (**limpa** no fim; sem free) |
| `0x1405b0b80` (outro modo; registros tipo 1 vindos de `[rbp+0x150]`) | `rbp+0x1fe0` → vetor `rbp+0x2740` | limpa (`0x1405b1d73`); **push próprio com teto** (`0x1405b1e2b: cmp rax,[rbp+0x2748]; jae`); liga controladores direto (`0x1405b1f17 call 0x140511a80`), **sem** `AddGhostEntry`; copia (`0x1405b1fa9`); limpa (`0x1405b22d7`) |
| `0x1405b50f0`, `0x1405b5bc0`, trecho `0x1405b6680` (função até `0x1405b6cc5`), `0x1405b8a10`, `0x1405b9290` | variadas | só limpam e copiam um vetor vazio para a sessão (zera os fantasmas da sessão nesses estados) |

O único outro uso de `&montagem` em `0x1405ba200` é `0x1405bb127`: o ponteiro vai para um despachante de evento (`0x1405a86d0` / `0x140592ff0`, hash `0x1c35364a`). `0x140592ff0` compara só `[montagem+0xc]`. HIPÓTESE: nenhum ouvinte desse evento lê `+0x760`. DESCONHECIDO: os ouvintes chamados indiretamente.

**Conclusão da pergunta 1 (FATO):** nenhum destrutor ou limpeza libera `[+0x3320]` ou `[+0x760]`, nem sempre nem condicionalmente. Os únicos lugares que gravam o ponteiro e a capacidade são os dois construtores (`0x140413cfa`/`0x140413ce8` e `0x140577b72`/`0x140577b79`).

---

## 2. Comparação com o embutido, crescimento e o 6º `AddGhostEntry` (pergunta 2)

- **Comparação com o endereço embutido** (FATO: não existe). `+0x3340` só aparece em `0x140413cf4` (construtor). `+0x780` da montagem só no construtor (`0x140577b72`). Nenhum laço ou destrutor faz `lea [base+emb]` / `cmp`.
- **Crescimento ou realocação** (FATO: não existe). Todos os pushes que achei descartam quando está cheio:
  - `0x14057df7b..0x14057dfc0` (`AddGhostEntry`);
  - `0x1404389d0..0x140438a1b` (cópia para a sessão);
  - `0x14024b1a0` (atribuição de vetor: limpa o destino e empurra até `[destino+8]`);
  - `0x1405b1e2b` (estado `0x1405b0b80`);
  - as cópias locais `0x1401886e7` e `0x1402b5bc6`.
- **O 6º `AddGhostEntry`** (FATO, confirmado):

```
14057df7b  mov rax, [rbx+0x10]          ; contagem
14057df7f  cmp rax, [rbx+0x8]           ; capacidade
14057df83  jae 0x14057dfc4              ; cheio: não grava e NÃO incrementa
...
14057dfdc  call 0x1404c09c0             ; ainda tira uma entrada do pool
14057e004  call 0x140511a80             ; ainda cria slot e controlador (BindGhostController)
14057e009  imul rdx, [rbx+0x10], 0x38   ; contagem (= capacidade)
14057e00e  mov  rcx, [rbx]
14057e011  mov  [rdx+rcx-0x30], rsi     ; [dados + cap*0x38 - 0x30] = [último]+0x08 = entrada nova
14057e029  mov  [rax+0x63], cl          ; e segue: +0x63, +0xb4, SetGhostDrawn
```

  Resultado: o 5º registro passa a apontar para a entrada nova. A entrada antiga do 5º fica na sessão e no mapa sem registro.

---

## 3. Outros leitores e suposições de tamanho (pergunta 3)

Os 17 chamadores de `GetGhostVector` (`0x1404269b0`), mais os acessos diretos da seção 1:

| chamador | o que faz | supõe ≤ 5? |
|---|---|---|
| `0x1401886a8` (trecho `0x1401882bc`) | copia para um vetor **local de capacidade 5** (`0x1401886bc mov eax,5`; teto `0x1401886e7`) e trata entradas com `+0x2c == 1` e slot `+0x1a8 == 2` | **trunca** nos 5 primeiros |
| `0x140266f00` | conta id ≠ 0; devolve `contagem < 5` (`0x140266f37 cmp ebx,5; setb`) | só UI ("pode adicionar fantasma") |
| `0x1402675e7` | laço; remove fantasmas ativos (`0x14029adb0`) | não |
| `0x14027558d` | **snapshot** da UI: `0x14024b1a0(ui+0x1b0, vetor)`; a capacidade de `ui+0x1b0` é 5 (`0x140249e08`) | **trunca** |
| `0x140286ed8` | compara o snapshot com a sessão (`0x14024b8f0`: contagens iguais e ids iguais). `mgr+0x33 = !igual` (`0x140286efd`) | com contagem > 5 o snapshot (5) nunca é igual, então `mgr+0x33 = 1` ao sair do ranking. `0x140379dd8` lê `mgr+0x33` (se 0, `[rsi+0x3c] = 5`). HIPÓTESE: muda o fluxo do menu de pausa (pede recarga) |
| `0x14028668c`, `0x140286697`, `0x140286731` | laço até a contagem (UI) | não |
| `0x14029f919`, `0x14032c50b` | laço até a contagem; para no primeiro que serve | não |
| `0x1402a6b91` | conta controladores com `+0x63` | o teto `0x1402a6c05 cmp edi,5` é só para adicionar na especial |
| `0x1402b5b67` | cópia **local de capacidade 5** (`0x1402b5b76 mov esi,5`; teto `0x1402b5bc6`), conta tipo 1 | **trunca** (UI) |
| `0x14035e686` (`0x14035e610`, UI) | tipo 0 e 2 → chave 0; tipo 1 → chaves 1..3 (`0x14035e762 cmp r14,3; ja pula`) | com limite e sem estouro (o 4º tipo 1 em diante é ignorado) |
| `0x1405315c4` | só testa `contagem != 0` | não |
| **`0x1405be855`** (`0x1405be820`, alcançado por `jmp` em `0x140598b40`, num estado que faz `[corrida+0x1a38] = 3`) | por entrada com id ≠ 0: **tipo 1** → `0x1405bd810(corrida, rbp)` com `rbp = corrida+0xa0040`, **`rbp += 0x130` sem limite** (`0x1405be873`, `0x1405be8b7`); **tipo 0** → destino fixo `corrida+0xa0500` (`0x1405be913`); tipo 2 é ignorado | **SIM, sem checagem.** O subobjeto `corrida+0x9fe70` (reset `0x140405e10`) tem em `+0xa0` um array de **5 × 0x130** (`0x140405ea7: lea rcx,[rax+0x5f0]`), ou seja `corrida+0x9ff10..+0xa0500`. O índice 0 é o do jogador; 1..4 (`+0xa0040..`) são para os tipo 1. **Do 5º tipo 1 com id ≠ 0 em diante, grava em `+0xa0500` (vaga do tipo 0), depois em `+0xa0630` e assim por diante: corrupção de memória** |
| `0x14062cdec`, `0x1406306ee` | laço até a contagem (`0x1406306ee` limpa slots) | não |

Leitores que não usam o getter:
- `0x1405ba200`, em `0x1405bb6c8`/`0x1405bb7aa`: monta o **vetor local de ponteiros** `{dados = rbp-0x60, cap = r13, cont}` em `rbp-0x80`. Primeiro entram os participantes (`[rbp+0x970]`), depois as entradas da montagem com tipo ≤ 1 (`0x1405bb6e3 cmp byte [rcx],1; ja`). Teto `cmp rdx, r13` com `r13 = 5`, ou seja **jogador + até 4 fantasmas tipo 0/1**; o resto é descartado. O vetor vai para `[corrida+0x2890]` ou `[corrida+0x2fd0]` → `vt+0x20` (`0x1405b8600` / `0x1405b8800`), que registra cada um por um callback e chama `0x14050bde0([0x141695240], …)`. **Trunca.** DESCONHECIDO: para que serve esse registro. HIPÓTESE: não é necessário para desenhar, porque as cópias de tipo 2 do hook antigo ficavam fora dele e ainda assim apareciam (3 carros validados). Com as cópias de tipo 0 atuais, hoje é exatamente 1 + 4 = 5.
- `0x1405baf1e..0x1405bb00e`: monta o vetor de 150 (`rbp+0x250`, `0x1405bad19 mov …,0x96`). Não estoura.
- `0x140422eb0` / `0x140422e50`: devolvem o **primeiro** que casa. Com várias entradas de tipo 0, as cópias nunca são o "fantasma de comparação".

Não achei laço com `cmp …, 4` / `0x1f` sobre esses vetores além dos listados. Não fiz varredura de leitura com índice fixo (`[dados+k*0x38]` com constante): DESCONHECIDO, mas todos os leitores conhecidos iteram até a contagem.

---

## 4. Cópia montagem→sessão (pergunta 4)

`0x1404381c0(sessão = rcx, montagem = rdx)`, chamada em `0x1405bb871`:

```
1404381db  mov edx, [rdx]               ; modo da montagem
1404381dd  call 0x140435e30             ; reset da sessão: contagem = 0 (dados e cap intactos)
...
14043899b  imul r11, [rsi+0x770], 0x38  ; contagem da ORIGEM
1404389a3  mov  r8,  [rsi+0x760]        ; dados da ORIGEM
1404389d0  mov  rax, [rdi+0x3330]       ; contagem do DESTINO
1404389d7  cmp  rax, [rdi+0x3328]       ; capacidade do DESTINO
1404389de  jae  0x140438a22             ; cheio: não copia (mas segue o laço)
1404389e4  add  rcx, [rdi+0x3320]       ; dados do DESTINO
1404389ed..140438a18                     ; copia campo a campo (+0,+8,+0x10,+0x28,+0x30; vtables refeitas em +0x18/+0x20)
140438a1b  inc  [rdi+0x3330]
140438a22..140438a75                     ; empurra [elem+8] (entrada) em sessão+0x30 se ainda não estiver (cap [+0x38] = 150)
140438a79  add r8, 0x38 / cmp r11 / jne
```

FATO: é cópia **por elemento**, não `memcpy` do bloco embutido. Ela usa a contagem e os dados da origem e a capacidade e os dados do destino. Basta trocar `montagem+0x760/+0x768` e `sessão+0x3320/+0x3328`; nada mais precisa de ajuste nessa função. Como os elementos não são donos de nada (só vtables e valores), mover elementos com `memcpy` para o buffer novo é seguro.

---

## 5. `mov r13d, 5` (pergunta 5)

Todos os usos de `r13` em `0x1405ba200` (FATO):

```
1405ba800  41 bd 05 00 00 00   mov r13d,5     ; A: antes de SetGhostsVisible
1405ba809  je 0x1405bacfd                     ;    gerenciador nulo: pula tudo com r13 = 5
1405ba86d  cmp rbx,rdi / je 0x1405ba98d       ;    lista de escolhidos vazia: pula o laço (e também B)
1405ba876  movabs r13,0x7245e18500000000      ;    laço dos escolhidos ESTRAGA r13
1405ba987  41 bd 05 00 00 00   mov r13d,5     ; B: saída normal do laço dos escolhidos
1405babfe  41 8b c5            mov eax,r13d   ; alvo do enchimento
1405bac01  2b 85 90 0e 00 00   sub eax,[rbp+0xe90]
1405bac07  0f 84 db 00 00 00   je 0x1405bace8 ; n == alvo: pula enchimento E pula o lea abaixo
1405bac0f  lea r13,[0x141233468]              ;    enchimento usa r13 como vtable
1405bace4  44 8d 6e 05         lea r13d,[rsi+5]   ; C: fim do enchimento (rsi = 0) → r13 = 5
1405bad38  mov r13d,[0x142049310]             ;    laço de participantes estraga r13
1405baf18  41 bd 05 00 00 00   mov r13d,5     ; D: fim do laço de participantes (pulado se a lista está vazia)
1405bb66b  mov [rbp-0x78], r13                ; r13 = CAPACIDADE do vetor local de ponteiros (rbp-0x60, 5 vagas = 0x28 bytes)
```

- **Quem controla o laço de `0x1405bac01`:** **B** (`0x1405ba987`) quando há ao menos 1 fantasma escolhido (o caso normal, offline também: o próprio). **A** (`0x1405ba800`) quando a lista de escolhidos está vazia. Os dois servem ao mesmo uso; o compilador rematerializa a constante 5 depois que o laço estraga `r13`. **C** e **D** são outras rematerializações do mesmo 5, agora para a **capacidade do vetor local** em `0x1405bb66b`.
- **Bytes e deslocamentos para patchar o alvo** (FATO):

| instrução | VA | RVA | offset no arquivo | bytes | imediato (4 bytes) em |
|---|---|---|---|---|---|
| A | `0x1405ba800` | `0x5ba800` | `0x5b9c00` | `41 bd 05 00 00 00` | VA `0x1405ba802` (arquivo `0x5b9c02`) |
| B | `0x1405ba987` | `0x5ba987` | `0x5b9d87` | `41 bd 05 00 00 00` | VA `0x1405ba989` (arquivo `0x5b9d89`) |
| C (não mexer) | `0x1405bace4` | `0x5bace4` | `0x5ba0e4` | `44 8d 6e 05` | disp8 em `0x1405bace7` |
| D (não mexer) | `0x1405baf18` | `0x5baf18` | `0x5ba318` | `41 bd 05 00 00 00` | `0x1405baf1a` |

- **Patchar só A e B para N** é suficiente para o enchimento e não estoura o vetor local (FATO + raciocínio):
  - com N ≥ 6 e n ≤ 5 (escolhidos ≤ 4, capacidade `0x1405777a9`, mais 1 gravação), `eax = N - n ≥ 1`. O enchimento sempre roda e **C** recoloca 5;
  - se o laço de participantes rodar, **D** também recoloca 5. Então `r13 = 5` em `0x1405bb66b`;
  - únicos caminhos em que `r13 = N` chega a `0x1405bb66b`: (i) gerenciador nulo (`0x1405ba809`) **e** lista de participantes vazia; (ii) `n == N` **e** participantes vazios. Em (i) não há nenhum fantasma na montagem. (ii) não acontece com N ≥ 6. O número de pushes nesse vetor é limitado pelos itens reais (participantes + tipos 0/1). HIPÓTESE: sem participantes não há o que estourar.
  - **Não** patchar C nem D (manteriam a capacidade 5 do vetor da pilha; mudar para N > 5 estoura a pilha em `rbp-0x38..`).
- **Alternativa de 1 byte** (HIPÓTESE, não testada): `0x1405bac08` `84 → 8e` (`je → jle`) faz o enchimento só completar até 5 e nunca dar a volta quando n > 5. Útil se o mod passar a empurrar as entradas extras ele mesmo.

---

## 6. Plano: é seguro? Em que ordem? Onde trocar cada ponteiro (pergunta 6)

### 6.1 Ordem de execução relevante (FATO)

1. `0x14055f773` → `0x140413bc0`: a sessão nasce **uma vez**, com `+0x3320 = +0x3340` e capacidade 5. Nada mais grava esses dois campos.
2. Entrada na especial, `0x1405ba200`:
   1. `0x1405ba23c`: construtor da montagem na pilha (`rbp+0x720`; vetor `rbp+0xe80`, embutido `rbp+0xea0`, capacidade 5);
   2. `0x1405ba817..0x1405ba849`: limpa o vetor da montagem (contagem = 0);
   3. `0x1405ba972` (escolhidos), `0x1405baacf` (gravação) e `0x1405bacc9` (enchimento): `AddGhostEntry(this, &vetor_montagem, …)`;
   4. `0x1405baf1e..` e `0x1405bb6c8..`: leem o vetor da montagem;
   5. `0x1405bb871`: `0x1404381c0(sessão, &montagem)`: reset da sessão (contagem = 0) e cópia;
   6. `0x1405bb8dd`: `0x1401fd020(&vetor_montagem)`: só destrói elementos e zera a contagem. Escreve em `elem+0x18/+0x20` (vtables) dos `contagem` elementos do buffer, sem liberar.

### 6.2 Pontos de troca

- **Sessão (`+0x3320`, `+0x3328`)**: qualquer momento depois do construtor e antes de `0x1405bb871`. Recomendado (HIPÓTESE): no hook de `AddGhostEntry`, na primeira chamada em que `[0x1416951e8]` não é nulo e `[s+0x3320] == s+0x3340`. Copiar os `[s+0x3330]` elementos atuais (até 5, sobras da especial anterior) para o buffer externo; **depois** gravar `+0x3320 = buf`; **depois** `+0x3328 = N`. Nessa ordem, um leitor concorrente sempre vê `contagem ≤ capacidade` com dados válidos. Basta uma vez por processo: nem o reset nem a desmontagem mexem no ponteiro.
- **Montagem (`+0x760`, `+0x768`)**: entre `0x1405ba23c` e o primeiro push. Recomendado: no próprio hook de `AddGhostEntry`, se `*(rdx) == rdx + 0x20` e `*(rdx+8) == 5`: copiar os `*(rdx+0x10)` elementos (0 na primeira chamada), gravar `*(rdx) = buf_montagem` e `*(rdx+8) = N`. É preciso um buffer **separado** do da sessão, porque a cópia `0x1404389d0` lê de um e grava no outro. O buffer tem de durar até o fim de `0x1405ba200` (a limpeza de `0x1405bb8dd` escreve nele); estático no mod serve. A montagem é refeita na pilha a cada entrada, então a troca vale **por entrada na especial**. A condição `dados == rdx+0x20` reconhece a montagem nova.
- **Alvo do enchimento**: patchar os 4 bytes em `0x1405ba802` e `0x1405ba989` para N (com N ≥ 6). Fazer **depois** (ou no mesmo instante) da garantia de que os dois vetores serão trocados. Se o alvo for N e um vetor continuar com capacidade 5, volta a sobrescrita de `[último]+0x08` (seção 2).

### 6.3 Riscos

| risco | situação |
|---|---|
| free inválido do buffer externo | **refutado** (FATO, seção 1): não há free |
| vetor copiado por valor ou snapshot antes da troca | a sessão é copiada **por elemento** (`0x14024b1a0`, cópias locais). Ninguém guarda `{dados, cap}` por valor. `0x140422eb0`/`0x140422e50` devolvem ponteiros para dentro do buffer, então **não trocar o buffer da sessão durante a especial** (só antes de `0x1405bb871`); depois ele não deve mudar nem ser liberado |
| **transbordo em `0x1405be820`** | do 5º tipo 1 com id ≠ 0 em diante, grava além do array de 4 × 0x130 (`corrida+0xa0040`). **Manter as cópias com tipo 0** (o hook atual) ou tipo 2. Para tipo 1 real (ranking) com N > 4, seria preciso outro patch |
| vetor local de 5 na pilha (`0x1405bb66b`) | só trunca (jogador + 4 tipos 0/1). Não mudar C nem D. Efeito do truncamento DESCONHECIDO (registro em `[0x141695240]`) |
| snapshot da UI com capacidade 5 (`0x140249e08`) | com contagem > 5, `mgr+0x33 = 1` ao sair do ranking. HIPÓTESE: força a recarga no menu de pausa. Evitar abrir o ranking ou trocar também `ui+0x1b0` |
| estado `0x1405b0b80` | empurra sozinho com teto na capacidade da montagem. Sem a troca, para em 5 (sem corrupção) |
| limites seguintes | 16 objetos de render de carro, 24 corpos de física, pool de 150 (`ghost-slots-analysis.md` §3) |

### 6.4 Veredito

**HIPÓTESE (forte, apoiada pelos FATOS acima):** é seguro, para N entre 6 e ~14 cópias de tipo 0, fazer:
1. troca do buffer da sessão uma vez;
2. troca do buffer da montagem no primeiro `AddGhostEntry` de cada entrada;
3. imediatos A e B = N.

O que ainda não foi testado no jogo: o efeito do truncamento do vetor de 5 na pilha e o efeito de N carros nos outros limites.

---

## 7. O que não consegui determinar

- Para que serve o vetor local de 5 ponteiros (`0x1405bb66b`, `0x1405b8600` → `0x14050bde0([0x141695240])`) e o que um fantasma fora dele perde.
- Os ouvintes do evento disparado em `0x1405bb14e` (hash `0x1c35364a`, payload `&montagem`): não segui as chamadas indiretas.
- O efeito exato de `mgr+0x33 = 1` em `0x140379dd8` (menu de pausa).
- Leitura com índice fixo (`[dados + k*0x38]` com k constante): não varri. Todos os leitores conhecidos iteram até a contagem.
- O que é a função `0x1401882bc..0x140188980` (cópia local de 5) em alto nível.
- Se `[rbp+0x970]` (participantes) pode estar vazio na prática. Só importa para o caso-limite de `r13 = N` na seção 5.
