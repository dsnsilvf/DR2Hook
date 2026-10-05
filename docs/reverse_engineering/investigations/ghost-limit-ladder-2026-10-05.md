# Escada de limite de fantasmas (2026-10-05)

Resultado: **máximo = 15 carros fantasma + o jogador (16 objetos de render de carro).** Registro completo da rodada, com os trechos de log (os logs originais ficam no tmp do job e somem com ele). Resumo no `ghosts.md` §6.7.

## Como foi feito

- Core `b335229` (suporte a N > 5, ver `ghosts.md` §6.7 e `ghost-vectors-scan.md` §6): `dr2hook_ghost_cars.txt` com N de 6 a 32; no 1º `AddGhostEntry` de cada carga os vetores de 0x38 da sessão e da montagem passam a buffers externos de capacidade 32 (`VirtualAlloc`), e os imediatos `0x1405ba802` / `0x1405ba989` (alvo do enchimento) viram N, restaurados para 5 no `Shutdown`. Os clones nascem sozinhos (`AutoClones`, sem F7).
- Cada valor de N = uma carga completa da especial **pelo menu** (o Reiniciar não recria as entradas). O `autostage` não serve: carrega a especial por outro caminho **sem fantasmas** (`entradas da sessão = 1`, `mapa de controladores = 0`).
- O usuário contou os carros na largada; o log `GhostLab[limites]` (`LogLimits`, logo após o spawn, 8 s e 25 s depois) registra as capacidades.
- Rastreio `GhostTrace` desligado (marcador renomeado para `dr2hook_ghost_trace.txt.off`).

## Resultados

| N | entradas da sessão | vetor | controladores | corpos de física (`[0x14201b930]/[+4]`) | resultado |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 5 | 6 | 5/5 | 5 | — | ok, 5 desenhados (`ghost cars drawn: 5`) |
| 8 | 9 | 8/32 | 8 | 0/9 | ok; usuário contou 8 |
| 12 | 13 | 12/32 | 12 | 0/13 | ok; usuário contou 12 |
| 14 | 15 | 14/32 | 14 | 0/15 → 3/12 aos 25 s, 13 desenhados | ok; usuário contou 14 |
| **15** | 16 | 15/32 | 15 | 0/16 | **ok; usuário contou 15** |
| 16 | 17 | 16/32 | 16 | — | **crash na carga** |
| 32 | 33 | 32/32 | 32 | — | **crash na carga**, idêntico ao de 16 |

Em N = 14 aos 25 s o log mostrou `corpos de fisica=3/12` e `carros desenhados=13` (instante da fotografia, enquanto os clones eram ligados); não repetiu em N = 15.

## O crash (N = 16 e N = 32)

Idêntico nos dois, thread da carga, poucos ms depois de `depois do spawn`:

```
GhostLab[crash]: excecao 0xc0000005 rip=0x140463af2 (exe+0x463af2) acesso=0x200000001
rax=102fc0 rbx=102e90 rcx=1e rdx=14fc40 rsi=4ac5e860 rdi=1e r8=1ffefd041 r9=4ab5b9d0 rsp=1029e0
pilha: 0x1404a6f48 0x14085d658 0x1408603fb 0x14085e91e 0x14085f2e0 0x140856fb7 0x14085d745 0x14085db5d
       0x1408571b6 0x14085d658 0x140857c67 0x140494ced 0x1404a8644 0x140aa7201 0x14096feeb 0x1404a94b5 ...
```

- `0x140463af2` é um laço de cópia de string com limite de 30 caracteres (`rcx = rdi = 0x1e`) dentro de uma função que copia um **registro de participante de 0x240 bytes** (`0x140463900`, chamada de `0x1404a6f43` com `rdx = [rbp+0x590] + 0x240`). O ponteiro do texto em `[rsi+0x128]` é lixo: o registro de origem não foi inicializado.
- Os registradores são idênticos com N = 16 e N = 32, ou seja, o primeiro elemento inválido está sempre na mesma posição, logo depois do fim do que o jogo tinha previsto, e não depende de N.
- Um 2º crash 1,6 s depois (`exe+0xbd6a20`, `acesso=0x1af`, `rax=16`, `rcx=3`, pilha `0x14099153b 0x140db3530 0x14097b280 0x14049405c`, outra thread) parece consequência do primeiro (hipótese).
- Interpretação (hipótese forte): N = 16 dá 17 carros (16 fantasmas + jogador) e o jogo só tem **16 objetos de render de carro** (`0x140946294`, `cmp ebx, 0x10`; ver `ghost-slots-analysis.md` §3 item 6). A fronteira exata bate: 15 + jogador = 16 funciona, 17 não.

Não foi identificado qual array concreto estoura nem quem preenche `[rbp+0x590]`; a função em torno de `0x1404a6f48` é grande (formatação de strings de 0x100 bytes) e não foi desmontada por completo.

## Como passar de 15 (não tentado)

Ampliar o array embutido dos 16 objetos de render de carro (`0x1730` bytes cada, em `+0x2108` do objeto de render; lista livre de 16 `u32` em `+0x194a8`): exige realocar a estrutura inteira; acima de 23 também a lista de 24 corpos de física (`0x140da7410: cmp edx, 0x18`). Alternativa de contorno (ideia, não testada): criar os carros extras por fora da lista de participantes com `CreateStageVehicle` (`0x14046af20`, sem teto próprio) e ligar cada um a um controlador, mas continuariam presos aos 16 de render e aos 24 de física.

## Outros achados da rodada

- **F8 no menu de pausa travou o jogo** (`futex_wait` na thread principal, ~0% de CPU, nenhuma linha de log do desligamento do core; usuário forçou a saída). Não é reprodutível por certeza; a causa é desconhecida (não consegui ler a pilha do processo Wine). Recomendação: não apertar F8 com o jogo no menu de pausa; abrir o jogo de novo carrega o core novo sem F8. Houve também um travamento anterior no menu de pausa depois de ~3,5 min parado (core `b787791`, sem exceção registrada).
- **A mensagem do toast** ("Ghost data copy 4; ghost cars drawn: 5 (the game creates only 2).") é do F7 de teste; com 4 cópias o F7 só repete "feitas 4: sem slot livre" (o gerenciador tem 5 slots). Com os clones automáticos o F7 deixa de ser necessário.
- **`AddGhostEntry` só roda na carga da especial**: o Reiniciar (`restart_race`) não recria as entradas; os dados dos clones (slots) são refeitos pelo `AutoClones` a cada 0,5 s.
- Estado deixado na pasta do jogo: `dr2hook_ghost_cars.txt` = 15, `dr2hook_ghost_trace.txt.off` (rastreio desligado), `dr2hook_autostage.ini` com `enabled = 0`, core `b335229`.
