# Rastreio da pausa dos fantasmas (prompt para análise do log)

Objetivo: descobrir **como o jogo pausa os carros fantasma** (o jogo pausado congela todos), para depois criar uma tecla que pausa só os clones. Esta passagem tem duas partes: o rastreio (código pronto, `src/core/ghost_trace.cpp`) e a análise do log por outro modelo.

## Como gerar o log (usuário)

1. O arquivo `dr2hook_ghost_trace.txt` já existe na pasta do jogo (apagar desliga o rastreio). `dr2hook_core.dll` novo já está lá. Abrir o jogo (ou F8 se já aberto).
2. Entrar numa especial com fantasmas (com `dr2hook_ghost_cars.txt` há clones; o rastreio funciona sem ele).
3. Largar, deixar os fantasmas andarem uns 20 s, **pausar (Esc) por uns 5 s, despausar**, andar mais 10 s, e repetir pausa/despausa umas 3 vezes. Se possível: uma pausa de 1 s e outra bem longa, e um Reiniciar.
4. O log é `dr2hook_ghost_trace.log` (na pasta do jogo, é recriado a cada carga do core; limite 256 MB). Mandar junto o `dr2hook.log`.

## Linhas do log (todas começam com o tempo em ms desde o início do core)

| Prefixo | Quando | Campos |
| :--- | :--- | :--- |
| `SYS` | cada chamada do atualizador do sistema `0x140518bb0` (1 por quadro) | `f` quadro, `this`, `en` (+0x30 ligado), `size` (+0x20 nº de controladores), `ret` RVA de quem chamou |
| `CTL` | cada atualizador de controlador `0x140518400` (1 por fantasma por quadro), impresso **depois** | `c` índice do controlador, `ctl`, `veh`, `slot`, `st` estado do slot (2 = pronto), `pre[60 61 62 63 t58]` bytes de controle e tempo (`+0x58`, 8 bytes) **antes**, `clock` (double recebido, vem do relógio), `post[...]`, `d` = `t58` como double, `spd` (+0x40), `out` posição do buffer, `body` posição do corpo (`corpo+0x2d0`) |
| `EVAL` | cada `EvaluateGhostState` `0x1409ce4d0` | `own`, `t`/`d` tempo apontado (hex e double), `off` deslocamento global `0x141f593e0`, `fl` flags `e4,e5`, `res` (0 = ok, 2 = slot não pronto), `valid`, `pos`, `vel`, `prog`, `t58` |
| `APPLY` | cada `0x1409cdaa0` (aplica o buffer ao corpo) | `dt` double, `res` (2 = não aplicou), `buf60`, `bufpos`, `body_pre`/`body_post` |
| `BEAT` | **a cada 100 ms, numa thread própria** (continua na pausa) | contagens de `SYS/CTL/EVAL/APPLY` no intervalo, `top` = topo da pilha de telas, `off`, `fl`, `epoch` |
| `STATE` / `STACK` | troca do topo da pilha de telas (`1251500` corrida, `1250b50` pausa) | estado antigo → novo; pilha completa |
| `CHAIN` | primeiro `SYS` de cada época | RVAs de retorno na pilha (cadeia acima do sistema) |
| `DUMPSET`/`DUMP` | na troca de estado e 600 ms depois | hex de `sys` (0x80), cada controlador (0x100), corpo (0x340, `+0x2d0` posição), slot (0x260) |

## Perguntas a responder (nesta ordem)

1. **Onde a cadeia corta na pausa?** Nos `BEAT` da pausa, quais contagens vão a 0 (`sys`, `ctl`, `eval`, `apply`)? Se `sys` cai a 0, a pausa está **acima** do sistema: usar `CHAIN` (antes da pausa) para listar quem chama e ver que função decide não chamar. Se `sys` continua e `ctl` cai, está entre os dois. Se tudo continua chamando, a pausa é só no tempo ou na física.
2. **O tempo para ou a avaliação para?** Em `CTL`/`EVAL`: durante a pausa o `clock`, `t58` e `d` ficam parados, avançam ou deixam de ser registrados? Comparar o `clock` do último quadro antes da pausa com o primeiro depois: **o tempo do fantasma continua de onde parou, ou salta o tempo da pausa?** (se pula, o relógio é de parede, se continua, é o da corrida).
3. **Os bytes de controle (`pre/post 60 61 63`) mudam na pausa?** Algum flag por controlador muda de valor ao pausar e volta ao despausar? Esse seria o candidato a alavanca por veículo. Dizer também se `+0x60` é "pular uma vez" (zera sozinho depois de pular).
4. **O corpo segue o buffer?** Em `APPLY`, `body_post` ≈ `bufpos`? Com `res=2` o corpo fica parado? Na pausa, o `body` muda?
5. **Os dumps:** comparar `DUMPSET` da troca para pausa e da troca de volta (e o de "600 ms depois"): listar os offsets (de `sys`, de cada controlador e do corpo) que mudam em **todos** os controladores da mesma forma, além de posição e tempo. São candidatos a flag de pausa.
6. **Os clones se comportam como o fantasma original?** Compare os controladores entre si (`c` = índice): mesmo `clock`, mesmo `t58`, ou cada clone com o seu tempo (`EVAL.t`)? O `slot` é diferente por clone?
7. **Resposta final:** em uma frase, qual é o mecanismo da pausa (A: relógio parado; B: laço pulado por flag; C: física/corpo parado) e qual é o ponto mínimo para pausar **um** controlador sem afetar os outros (um byte no controlador, um desvio no `EvaluateGhostState` que devolve o tempo congelado, ou outro).

## O que já se sabe (para não redescobrir)

- `EvaluateGhostState(owner = ctrl+8, &tempo, ?, out)`: função pura do tempo + o `float` global `0x141f593e0` (adianta/atrasa todos). Ver `ghosts.md` §3.
- Atualizador do controlador `0x140518400` (só chamado de `0x140518c78`, dentro de `0x140518bb0`, que percorre um `std::map` em `this+0x18`): começa com `if ([ctl+0x60] != 0) { [ctl+0x60] = 0; sai }` e `if ([ctl+0x63] == 0) sai`; exige `[ctl+0x28]` (slot) com `+0x1a8 == 2`. `[ctl+0x58]` recebe o tempo (calculado em `0x14091ef70` a partir do `double` do 2º argumento, que vem de `0x140929880` sobre um objeto de relógio obtido por chamada virtual `+0x58`). Se esse `double` é ≤ 0, a velocidade sai 0. Depois chama `0x1409cdaa0` (aplica ao corpo) e, com `[ctl+0x62]`, `0x1409da680` (desenho).
- Leitura dos hooks: nenhum altera comportamento; só registram.

## Depois da análise

Se achar o ponto mínimo, a tecla (por exemplo F10) vira um hook ali. Se for um flag por controlador, basta escrevê-lo só nos controladores dos clones (os que não são o primeiro slot, ver `ghost_lab.cpp`, `IsClone`).
