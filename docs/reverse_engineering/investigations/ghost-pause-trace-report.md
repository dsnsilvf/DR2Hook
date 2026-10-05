# Rastreio da pausa dos fantasmas: relatório da análise do log

Fonte: `dr2hook_ghost_trace.log` de 2026-10-05 (54 790 linhas, 305 s), `dr2hook.log` da mesma sessão e desmontagem de `dirtrally2.exe`. Os números de linha citados são do log de rastreio. Legenda: **[C]** = confirmado pelo log ou pela desmontagem; **[H]** = hipótese.

## Sessão

- Telas: `1251500` corrida, `1250b50` pausa e `12499f0` transição ao sair da pausa (dura cerca de 100 ms).
- Corrida 1: épocas 32, 35, 38, 41 e 44.
- Pausas: épocas 33 (2,7 s), 36 (1,8 s), 39 (0,7 s), 42 (0,5 s), 45 (seguida de **Reiniciar**: telas `1262e48 → 1259038 → 1266bd0 → 124b810 → 124c588 → 1251500`) e 52 (75 s, até o fim do log). Houve também uma pausa antes da largada (época 28, 32 s).
- **Sem clones nesta sessão.** O `dr2hook.log` diz `GhostLab[limite]: aplicando 0 copia(s) (slots=5 prontos=1)`. Só o controlador `c=0` tem slot pronto (`st=2`). Os `c=1..4` têm `st=0` e saem cedo; eles nunca chegam a `EVAL`/`APPLY`.

## 1. Onde a cadeia corta na pausa?

**[C] Tudo vai a 0, inclusive `sys`.** Em todas as pausas de corrida, cada `BEAT` traz `sys=0 ctl=0 eval=0 apply=0` (por exemplo, a linha 30351 em 184224 ms e a linha 36968; na pausa longa, todas as 714 batidas de 229657 a 305151 ms, linhas 54157 a 55140). Na corrida são cerca de 6 `sys` e 30 `ctl` por 100 ms (linha 31241), ou seja, 60 Hz. Na pausa ninguém chama o atualizador do sistema `0x140518bb0`, e o corte fica **acima** dele.

Dentro do sistema não há flag de pausa. `en` (`sys+0x30`) fica em 1 durante a pausa (todos os dumps de `sys`, `+0x30 = 0x101`) e só vale 0 nas telas de carga e de introdução, onde `SYS` continua sendo chamado (épocas 17 a 20, 27, 30, 31, 49 e 50).

Cadeia (`CHAIN`, sempre `+4b10a5 +4b0f69 +3cb2ac +871706`; `+98fa89` aparece às vezes e é um resto de pilha). A heurística de `Chain()` só aceita `call rel32`, `call [rip]` e `call reg`, e descarta `call [rax+disp8]` (`ff 50 xx`). Por isso as chamadas virtuais somem. Cadeia reconstruída pela desmontagem **[C]**:

```
0x1404b0ed0  GameTask::Update(this, double* dt)        vtable 0x141277180, slot 1
   ├─ 0x1404b0f3d call 0x140b3d7b0  Scheduler::SetTaskPaused(this, [this+0x19d0], pausado)
   └─ 0x1404b0f64 call 0x140b3ed70  Scheduler::Run(this, dt)          (retorno +4b0f69)
        └─ percorre std::map de tarefas em [this+0x20]; para cada nó chama o vtable+0x08 da tarefa
             └─ 0x1404b1040  SimTask::Update(this, double* dt)   vtable 0x141276fc8, slot 1
                  ├─ 0x140dbc500  passo da física (corpos)       [this+0x40]
                  ├─ 0x140518bb0  sistema de fantasmas            [this+0x50]   (retorno +4b10a5)
                  ├─ 0x1409ad670 / 0x1409ad1d0                     [this+0x48]
                  └─ 0x140e213a0                                   [this+0x18]
```

A decisão de não chamar fica em `Scheduler::Run` (`0x140b3ed70`). Para cada nó da tarefa:

- `0x140b3edf5`: se `byte [nó+0x5e] != 0`, **pula a tarefa** (só limpa `nó+0x5f`).
- `0x140b3ee06` e `0x140b3ee13`: pula se `[nó+0x54] & [sched+0x30] == 0` ou se `[nó+0x58] & [sched+0x34] == 0` (máscaras de grupo).
- Passo fixo (`nó+0x5c == 0`, de `0x140b3efc8` em diante): acumula `*dt` e chama `Update(&[nó+0x30])` uma vez por passo inteiro. Com `dt = 0`, não há nenhuma chamada.

O único escritor de `nó+0x5e` é `0x140b3d7b0`, e o único chamador dele é `0x1404b0f3d`, dentro de `GameTask::Update`:

```
if (game[0x19a9] == 0) { game[0x19c8]->[0x15ba] = 0; SetTaskPaused(game, game[0x19d0], 1); }
else                   { game[0x19c8]->[0x15ba] = 1; SetTaskPaused(game, game[0x19d0], 0); }
```

`game+0x19a9` tem um único escritor: o setter virtual `0x1404867f0` (slot 3, `+0x18`), que grava `+0x19a9 = dl` e `+0x19b0 = xmm2` (um `float`, talvez a escala de tempo).

**[H, forte]** A pausa chama esse setter com `false`. A tarefa `[game+0x19d0]` é a `SimTask` (`0x1404b1040`) e fica com `nó+0x5e = 1`, de modo que `Scheduler::Run` a pula inteira: física, fantasmas e veículos param juntos.

**Falta no log:** um hook em `0x1404b0ed0` registrando `this`, `*dt`, `[this+0x19a9]`, `[this+0x19d0]` e o endereço da `SimTask` (o `this` de `0x1404b1040`), mais um hook em `0x140b3d7b0` (`rdx`, `r8b`) para ver quem é pausado e quando. Isso separa "tarefa pulada por flag" de "dt = 0", que geram o mesmo log.

## 2. O tempo para ou a avaliação para?

**[C] Os dois param juntos, e o tempo continua de onde parou, sem salto.** Durante a pausa não há nenhuma linha `CTL`/`EVAL`. Os dados:

| Pausa | Último `clock` antes (linha) | Primeiro depois (linha) | Pausa real (parede) | Diferença do `clock` |
|---|---|---|---|---|
| 1 | 45.031532 (30073) | 45.048198 (31196) | 2869 ms | 0.016666 |
| 2 | 55.714438 (36690) | 55.731104 (37532) | 2008 ms | 0.016666 |
| 3 | 57.397704 (38891) | 57.414370 (39723) | 870 ms | 0.016666 |
| 4 | 62.880818 (42943) | 62.897484 (43775) | 737 ms | 0.016666 |
| 5 (Reiniciar) | 63.830780 | 0.000000 (46846) | — | volta a zero |

- O relógio é o da corrida, em passo fixo de 1/60 s. Em 4 827 quadros, todo `clock` sobe exatamente 0,016666, mesmo com 4 a 30 ms de parede entre as chamadas. A posição também é contínua: `pos -3314.99,527.30,-4335.83` antes e `-3315.21,527.31,-4335.65` depois, um passo a ~16,6 m/s.
- **`t58` não é `double`, e `d` = 0 é falso.** `0x14091ef70` grava `[ctl+0x58] = (uint64)(clock × 1e6) XOR chave`. A chave vem de `0x140183d60` (global `0x1415e3500`) e valeu `0x000001787e0df0cf` nesta sessão, igual ao `t58` com `clock = 0`. Decodificando: `0x1787ca2d0a3 ^ chave = 45 031 532 µs`. É esse valor ofuscado que vai para `EvaluateGhostState` (`rdx = &ctl+0x58`, em `0x1405184fd`).

## 3. Os bytes de controle mudam na pausa?

**[C] Não.** Em todas as 4 827 linhas de cada controlador, `pre`/`post` são constantes:

- `c=0`: `60=0 61=0 62=1 63=1`.
- `c=1`: `60=0 61=0 62=0 63=1`.
- `c=2..4`: `60=0 61=0 62=1 63=0`.

Não existe flag de pausa por controlador.

`+0x60` nunca foi diferente de 0 no log. Pela desmontagem **[C]** (`0x14051840c`), é "pular uma vez": `if ([ctl+0x60]) { [ctl+0x60] = 0; return; }`. `+0x61` dispara `0x140512700` e se zera sozinho (`0x140518638`).

## 4. O corpo segue o buffer?

**Não deu para medir.** `body`, `body_pre` e `body_post` dão sempre `0.00,0.00,0.00` (por exemplo, `APPLY` na linha 30072, com `bufpos = -3314.99,527.30,-4335.83`). Os 0x340 bytes despejados do corpo (`[veh+0x30]`) não têm nenhum `float` com cara de coordenada de mundo: `+0x2d0` não é a posição do corpo do fantasma. `res` foi sempre 0 para `c=0`; não houve `res=2` com slot pronto.

Pela desmontagem **[C]**, `APPLY` (`0x1409cdaa0`) exige `slot+0x1a8 == 2` e `buf+0x60 != 0`. Depois, com `corpo = [veh+0x30]`:

- `0x140db9410(corpo, buf)`: matriz/posição, gravada via `0x140731c70` e um nó em `corpo+0x840`.
- `0x140db90e0(corpo, buf+0x40)`: velocidade linear.
- `0x140db8930(corpo, 0)`: velocidade angular.

Ou seja, a cada quadro o corpo é teleportado e recebe a velocidade amostrada.

**Falta no log:** a posição real do corpo, a ser lida na matriz que `0x140db9410` grava (fazer hook nela e reler `corpo` depois, ou procurar a translação no nó `[corpo+0x840]`). Também falta ver se o passo `0x140dbc500` integra a velocidade de um corpo de fantasma (tipo 3) quando ninguém o reteleporta.

## 5. Os dumps

Comparei byte a byte os dumps de pausa (conjuntos com tela `1250b50` durante a corrida: 11 dumps) com os de corrida "600 ms depois" (6 dumps). O script está em `/tmp/ghost_pause_analysis/diff.py`.

- **`sys` (0x80): nenhum byte separa pausa de corrida.** O único campo que muda em toda a sessão é `+0x30` (`0x101` na corrida e na pausa, `0x100` na introdução e na carga).
- **Controladores:** só mudam `+0x08`, `+0x10` e `+0x18` (contadores/índices de amostra do dono), `+0x40` (`spd`) e `+0x58` (tempo). Todos ficam **congelados** na pausa: o dump da pausa é igual ao da troca de volta (conjuntos 51 a 54, 56 a 58, 61 a 63, 66 a 68). Nenhum deles é flag: nenhum tem um valor fixo na pausa e outro fixo na corrida.
- **Corpos e slots:** nenhum flag. Em `body0`/`body1`, os bytes `+0x150..+0x166` mudam o tempo todo (valores contínuos) e também ficam parados na pausa.

Conclusão **[C]**: não há candidato a flag de pausa nos objetos despejados. A pausa não toca neles.

## 6. Os clones se comportam como o fantasma original?

**Sem clones nesta sessão, então não dá para comparar.** O que o log mostra **[C]**:

- Em todos os 4 827 quadros, os 5 controladores recebem o **mesmo `clock`** (nenhum quadro tem dois valores).
- Os slots são distintos por controlador: `…ceb0`, `…d110`, `…d370`, `…d5d0`, `…d830`, separados por 0x260.
- Só `c=0` chegou a `EVAL`.

**[H]** Pelo código do GhostLab, as cópias são deslocadas no próprio slot (tempos das amostras), não no `clock`. Então o `EVAL.t` de um clone seria igual ao do original.

**Falta:** repetir o rastreio com `dr2hook_ghost_cars.txt ≥ 1` para ver `EVAL` com `own` diferentes e confirmar.

## 7. Resposta final

**Mecanismo: B, com efeito de A** (laço pulado por flag, e por isso o relógio não anda). Com a pausa, o agendador `0x140b3ed70` deixa de chamar a `SimTask` (`0x1404b1040`), que agrupa física, fantasmas e veículos. O relógio da corrida, em passo fixo de 1/60 s, também é avançado dentro desse laço e só volta a andar com ele. Por isso não há salto de tempo, e todos (inclusive a física) congelam juntos.

- **[C]:** a `SimTask` não é chamada, e o relógio continua sem salto.
- **[H, forte]:** o flag é `nó+0x5e`, gravado por `0x1404b0f3d` a partir de `game+0x19a9`.

O próprio fantasma não tem nenhum flag de pausa.

### Ponto mínimo para pausar um único controlador ou clone

Recomendado: **no hook de `EvaluateGhostState` que o GhostLab já tem** (`0x1409ce4d0`; `rcx = ctl+8`, `rdx = &[ctl+0x58]`, `r9 = saída`).

1. Identificar o controlador por `ctl = rcx - 8` e aplicar só aos clones (`IsClone`).
2. Na hora de pausar, guardar o valor bruto `*(uint64*)rdx` (`congelado`) e o `clock` atual.
3. Enquanto o clone estiver pausado: antes de chamar o original, gravar `*(uint64*)rdx = congelado`. A saída repete a mesma pose, válida, e o buffer `[ctl+0x48]` recebe a mesma pose.
4. Depois de chamar o original, zerar `saída+0x40..+0x4b` (velocidade). Assim `APPLY` aplica velocidade 0 ao corpo: sem desfoque nem deriva, e o corpo continua reteleportado a cada quadro, sem escorregar.
5. Ao despausar, acumular `pausado += agora - início`. Em todo quadro, avaliar o clone com `t = decodificar(*rdx) - pausado` e gravar `*rdx = codificar(t)`, onde `codificar(s) = (uint64)(s·1e6) ^ [0x1415e3500]` (ou chamar o próprio `0x14091ef70(&saída, s)`) e `decodificar(v) = (v ^ chave) / 1e6`. O clone segue de onde parou, sem salto.

Não é preciso tocar em bytes do controlador nem no agendador. A thread é a do jogo, e o hook já existe.

Alternativa mais simples, porém pior: escrever `[ctl+0x60] = 1` a cada quadro (é "pular uma vez" e se zera sozinho), ou pôr `[ctl+0x63] = 0`. O controlador sai antes de `EVAL`/`APPLY`. Riscos:

- O corpo fica com a última velocidade gravada por `0x140db90e0` e ninguém mais o reteleporta. Se a física (`0x140dbc500`) integrar essa velocidade, ele **deriva** **[H]**.
- `[ctl+0x58]` não é atualizado. Ao voltar, o clone **salta** para o tempo atual da corrida em vez de continuar de onde parou.
- O fator de distância/opacidade (`0x1409da680`, `veh+0x94`) congela.
- O jogo pode reescrever `+0x63` no Reiniciar, como faz com `+0x62`.

### Riscos do ponto recomendado

- **Velocidade:** se não for zerada, o corpo fica parado mas com velocidade de corrida (borrão de movimento ou roda girando) **[H]**.
- **Rodas:** a saída `+0x70` repete a mesma amostra, então devem ficar paradas **[H]**. O `APPLY` recebe `dt = clock` absoluto, usado em `0x14073b620` (provável animação) **[H]**. Isso não depende do tempo do clone, então algum elemento animado pode continuar.
- **Som:** o fantasma não tem motor audível, segundo os testes anteriores **[H]**.
- **Reiniciar:** o `clock` volta a 0 (linha 46846), e o `pausado` acumulado precisa ser zerado nesse momento. Detectar pela queda do `clock` ou pela troca de época.
- **Ajuste global:** o `float` `0x141f593e0` é somado depois, dentro do `EVAL`, e continua afetando todos os fantasmas.
- **Diferença ao vivo:** o GhostLab usa o `EVAL` do fantasma de referência para o HUD. A pausa não pode valer para o original.

## Lacunas do log (o que registrar na próxima rodada)

1. Hook em `0x1404b0ed0`: `this`, `*dt`, `[this+0x19a9]`, `[this+0x19d0]`. Hook em `0x140b3d7b0`: tarefa e valor. Isso confirma o flag `nó+0x5e` contra "dt = 0".
2. A posição real do corpo do fantasma: matriz gravada por `0x140db9410`/`0x140731c70`. O `+0x2d0` está errado.
3. Rodada com clones (`dr2hook_ghost_cars.txt ≥ 1`), para responder à pergunta 6.
4. Corrigir o log: `t58`/`d` devem ser decodificados (`^ chave`, `/1e6`), e a heuristica `Chain()` deve aceitar `ff 50 xx` / `ff 90 xx xx xx xx` (chamada virtual). Uma linha `CTL` saiu truncada no log (escrita intercalada).

Scripts e arquivos temporários em `/tmp/ghost_pause_analysis/`: `clk.py`, `dumps.py`, `diff.py`, `show.py`, `xref.py`, `dis.py`.
