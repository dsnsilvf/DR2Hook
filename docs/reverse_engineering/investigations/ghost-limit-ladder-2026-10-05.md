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

## Tentativa de passar de 15 (2026-10-05, core `5ceef66`/`065f1b5`) — falhou

Patch `PatchRenderObjectArray` (array de 16 objetos de render de 0x1730 bytes em `+0x2110` trocado por buffer externo de 24, rascunho do `dwords[16]` em `+0x194a8`, laço de construção e dois laços de busca 16→24). Log: `array de objetos de render trocado por buffer externo de 24 elementos (ok)`, 24 entradas, 23 controladores.

- O crash antigo (`exe+0x463af2`, registro de participante lixo) **sumiu**; o spawn terminou.
- Novo crash logo depois (`exe+0x49d260`, escrita em `0x140469cf0`): rotina de limpeza de lista (`0x14049d230`) chamada de `0x1404a79cd`, dentro da função por carro `0x1404a4920` (chamada de `0x1404a94b0`). No crash o laço do chamador estava no **índice 16** (`r15 = 0x80`, passo 8; arg 5 = elemento de render nº 10 do buffer novo): o ponteiro `[obj + 0x1620 + 8*i]` caiu em `obj+0x16a0`, onde está o objeto estático "dummy" (`0x141276930`), não uma lista.
- O objeto (`[rbp+0xbd0]` do chamador) tem **3 arrays de 16 ponteiros** em `+0x1520`, `+0x15a0`, `+0x1620` (0x80 bytes cada; cada carro aponta para 3 blocos de 0x80 dentro de uma alocação de 0x180), lidos sem checagem em `0x1404a944e/9456/953b/95c5/95cf/99c8/99fe/aa14e/aa24d` e varridos com `lea ...,[+0x1520]` em `0x1404aa0db/aa4af`.
- A função do chamador tem frame de ~0xc88 bytes com **vetores locais de 16 posições** (16 × 0x88 com destrutor em `0x1404aa506`, laços `cmp ..,0x10` em `0x1404aa476/aa498`). Passar de 16 exige refazer frames de pilha, não só relocar arrays estáticos.

Conclusão: **o máximo prático continua 15 fantasmas + jogador**. O core agora limita N a 15 por padrão; `DR2HOOK_GHOST_EXPERIMENT=1` no ambiente libera até 23 (crasha).

## Crash do modelo no objeto de render 17 (N = 16, 2026-10-05)

Com o experimento, o spawn de 17 carros passa e o objeto de render 16 recebe modelo (`GhostLab[modelos]`). O seguinte crash é `exe+0x966a49` (escrita em `0xde8`, `rdi = 0`), retorno `0x1409698eb`.

- `0x1409698df` lê `[objeto de render+0x1d0]` e chama `0x1409669f0`, que faz `[estado+0xde8] = modelo` (`+0x1c8`). No objeto 17 esse ponteiro é nulo.
- O construtor `0x140946850` zera `+0x1d0`. Quem o preenche é `0x14095f490` ← `0x14095f3e0` ← `0x140477c8d`, um laço no vetor de veículos `[ctx+0xf8..+0x100)`. Há 17 carros, então só os índices 0..16 ganham o bloco de `0xb4d0` (allocator `[dono+0x218]`; lista no dono compartilhado `gerenciador+0x196e0`, em `+0x10 + i*8`, contagem `+0x210`, cabe 64). Não há teto de 17.
- O índice 17 estava com `+0x1b0` (ocupado) porque o laço `0x1404aa480`, ligado pelas flags `VEHICLE_SYSTEM+0x501/+0x508`, marca o array inteiro, e o patch tinha trocado o `cmp r15d,0x10` (`0x1404aa49b`) para 24. Os slots vazios acendiam e a carga extra de modelo (`LoadExtraCarModels`) chamava `0x1409690d0` neles.

Correção: `0x1404aa49b` permanece 16; a carga extra só roda com `+0x1d0` não nulo. Os outros dois `cmp` desse trecho (apagam o ocupado dos slots além da contagem) continuam em 24.

## Crash da vtable no 17º slot (`exe+0x938829`)

Na rodada seguinte o `exe+0x966a49` não repetiu. O log gravou o modelo do objeto de render 16 e, 28 ms depois:

```
GhostLab[crash]: excecao 0xc0000005 rip=0x140938829 (exe+0x938829) acesso=0x178 tipo=0
rax=0 rbx=0x6bc79998 rcx=0x6bc79998 ... rsi=0x6bc17310
pilha: 0x140937a4f 0x14049cdb8 0x14049cf6e 0x1404b0319
```

`0x140938829` é `call [rax+0x178]` depois de `mov rax,[rbx]`. `rax = 0`: a vtable do objeto em `rbx` é nula. `rbx - rsi = 0x62688 = 0x408 + 16*0x6228`, o índice 16 de um array de 16.

- `0x1409379f0` (retorno `0x140937a4f`) lê a contagem em `[dono+0x400]`, toma `dono+0x408 + contagem*0x6228`, incrementa a contagem, grava o veículo em `[elemento+0x588]` e chama `0x140938680`. Quem chama é `0x14049cd50`, um laço no vetor de veículos `[sistema+0xf8..+0x100)`, sem teto de 16.
- O dono é o objeto de `0x62750` bytes alocado em `0x140526ebb` (`call 0x14091ecc0`). O array mora num subobjeto em `+0x330`: 16 × `0x6228` a partir de `+0xd8` (`0x14091ea20`, `lea ebx,[rdi+0x10]` em `0x14091eb7f`, elemento em `0x14091f550`). `+0xd8` do subobjeto = `+0x408` do externo. A cauda (`+0x62358` e os campos até `+0x62410`; no externo, `+0x62748`) começa exatamente onde o 17º elemento precisaria nascer.
- O reset `0x140920220` também conta 16 (`mov edx,0x10`) a partir de `+0x62358`.

Correção no experimento: `dxgi.dll` aloca `0x93890` e libera o mesmo tamanho (`0x1409218dd`; o subobjeto sozinho, `0x140921879`, passa de `0x62418` para `0x93558`). O core, se entrar nos primeiros 20 s e achar `DR2HOOK_SLOTOWNER_SIZE`, empurra a cauda `0x31140` bytes e constrói 24 elementos.

Testado às 10:15: `GhostLab[slots] ... 16 -> 24 (ok)`, modelo no objeto de render 16, `corpos de fisica=0/17` aos 8 s. O `exe+0x938829` não repetiu.

## Crash do áudio no 17º carro (`exe+0xf959ce`)

```
GhostLab[crash]: excecao 0xc0000005 rip=0x140f959ce (exe+0xf959ce) acesso=0x140b0b5d8 tipo=1
rax=4c1af230 rbx=0 rcx=3793b140 rdx=3ba4f6c0 rsi=4c1af230 rdi=3793b140 r8=3ba4f6c0 r9=4
r12=140b0b5d8 r15=4
pilha: 0x140b0b455 0x140b0b5d8 0x140b0aa53 0x140ec1998 0x1403cb2ac 0x140871706
```

`0x140f959ce` é `mov byte [r12], 0` no começo de `0x140f959a0` (vtable `0x1414277b0` + `0x38`). `r12` é o 6º argumento, que `0x140b0a735` não passa: com só a sombra de 0x20 na pilha, o slot lido é o endereço de retorno de `0x140b0a660`, `0x140b0b5d8`. `r9 = 4` bate com o `mov r9d, 0x4` dessa chamada (estado 7 do carro de áudio).

O banco (`0x140ae5e90`) constrói 16 blocos de `0x3390` em `+0x1730` e guarda a contagem em `+0x35030`. `0x140b0488a` recusa o 17º. Contorno atual: `0x140b0b5b0` só chama `0x140b0a660` nos 16 primeiros. Ampliar o banco (mover a cauda a partir de `+0x35030` por `8*0x3390`) ainda não foi feito.

## Crash seguinte do parâmetro de áudio (`exe+0xf93508`)

Repetiu às 10:30 e às 10:39, com o corte de `0x140b0b5b0` já no log e sem a linha da segunda varredura: o processo morre dentro de `call 0x140a0fdb0`, cujo retorno é `0x140b1dde5`, antes do laço em `[+0xf8,+0x100)`.

```
rip=0x140f93508 rcx=2064696c00367665 r13=3793b140 rdx=0x77
pilha: 0x140f34bd2 0x140f3c068 0x140f3ad36 0x140f8dd3a 0x14085d97a 0x140f93378 0x140b1dde5
```

`rcx` em little-endian é `ev6\0lid `. `mov rcx,[rcx]` em `0x140f93502` já tinha carregado os 8 bytes de `[r13+0x40]`; `mov rax,[rcx]` trata esse texto como ponteiro. `+0x48` não era zero, então o `cmove` não anulou o ponteiro. O caminho é `0x140f3bf70` com tipo 3, salto em `0x140f3c07f` para `0x140f933a0`.

Contorno: esses 12 bytes viram um salto que só executa `call [vtable+8]` se o valor e a vtable tiverem os bits acima de 46 zerados. Caso contrário conta e segue em `0x140f9350e`.

Testado às 10:50: as duas linhas de áudio apareceram e o `exe+0xf93508` não repetiu. Seis segundos depois, dois crashes novos.

## Lista de ponteiros do dono dos slots (`exe+0x93cf2d`)

A contagem `+0x62358` já estava em `+0x93498`, mas os 7 acessos a `+0x62360` (a lista de ponteiros, 16 qwords) não. `0x14093b7b1` gravava no endereço velho e `0x14093cf25` lia de lá: `rcx = 0`, `mov rax,[rcx]`. Também faltavam o byte `+0x623c8` e o byte `+0x623f1` (logo depois de `+0x623f0`). Todos andam os mesmos `0x31140`.

## Rodas do 17º carro (`exe+0x9b17cd`)

```
rip=0x1409b17cd rax=57722ce8cf8b48 rbx=4acd5e10 rcx=141392460 r8=9 rdx=0
```

`0x1409b17cd` é `mov rdi,[rax+0x40]` dentro de `0x1409b1710`, chamado por `0x1409a9d3a`. O texto em `rcx` é `car_wheel`. O índice em `[objeto+0x728]` acima de 15 troca o objeto de render pelo slot 0, mas o estado de roda continua `indice*0x70` a partir de `+0x20`. São 16 blocos; o 17º cai em cima dos campos `+0x720`. Os dois `cmova eax, esi` (`0x1409a9d09` e `0x1409a9de2`) passam a saltar a chamada quando o índice é maior que 15.

## Passo de render por carro (`0x1409ed100`), travamento às 11:03

Com rodas e lista `+0x62360` corrigidas, a carga parou aos ~13 s com a imagem congelada e a thread principal a 98% de CPU. Pilha real (winedbg): `EnterCriticalSection` ← `0x1408d2000` (trava em `[objeto+0x68]`) ← `0x140906966` ← `0x1409f3084` (dispositivo de render `0x141f45258`, método `+0x1c0`) ← `0x1409ed461`. `rax` era `0xC0000008`: a trava não tinha dono, o handle é que era inválido, e o Wine repetia a espera para sempre.

`0x1409ed100` percorre os carros (`[[0x14159dad8]+0x19640]`, passo `0x1730`) e, para cada um, 4 recursos em `[rdi+0x18]`. O ponteiro (`r12`, ou `rbx` no ramo `ecx==1`) não volta entre carros: são 64 vagas, que acabam em `+0x218`. O 17º carro lê `[rdi+0x218]`, que é o objeto da própria função, como recurso. O ramo `ecx==1` também anda `rdx` em `0x880` por carro.

Contorno: os dois `cmp <reg>,[rax+0x19640]` (`0x1409ed49f` e `0x1409ed70a`) viram `call` para uma caverna que compara com `min(contagem, 16)`. O 17º carro fica sem esse passo. Log esperado: `GhostLab[passo]: passo de render por carro limitado aos 16 primeiros.`

Observação: anexar o winedbg e soltar o processo deixa o jogo marcado como depurado, e ele crashou logo depois (`0xC0000008` em `ntdll+0x66928`). Esse crash foi do diagnóstico, não do mod.

## Tarefas por carro (`0x14097aa40`), crash `rip=0xba` às 11:16

Com o passo de render limitado, a carga terminou (tocou o som de fim da carga) e o jogo caiu em `rip=0xba`, com `rsi=0x10` e `r15=0x11`. Topo da pilha: `0x14097ab11`, retorno de `call [rax+0x30]`. `0x14097aa40` (chamada de `0x1409a99a7`, `0x1409a99f9` e `0x140996296`) percorre os veículos da lista do sistema e usa um bloco de `0x48` por carro em `[gerenciador+0x194e8]`. O `cmova` em `0x14097aac7` limita a 15 só o índice usado para ver se o carro existe; o bloco continua andando, e no 17º a vtable é lixo.

Contorno: `cmp rsi,15; mov rax,rsi; cmova rax,rcx` vira `cmp rsi,15; ja 0x14097ab2f; mov rax,rsi; nop; nop`. O 17º carro fica sem essa tarefa. Log: `GhostLab[tarefas]: carro 16+ sem o bloco de tarefa de 16 vagas.`

## Atualização com tarefas (`0x1409889d0`), crash `exe+0x9a84d5` às 11:20

Thread de trabalho, `0x1409a84c0` com `rcx=0` (`cmp byte [rcx+0x51]`), vindo da tarefa `0x140964de0` (vtable `0x141394548`, conjunto de 32 tarefas de `0x38` em `+0x228`). `0x1409a83e0` atualiza os objetos da lista `[obj+0x10]` (contagem `+0x210`). Sem sistema de tarefas, o próprio jogo limita a contagem a 16 (`cmovg` em `0x1409a845c`). Com tarefas (`[obj+0x220] != 0`), salta para `0x1409889d0`, que monta na pilha `2*contagem` dwords em `rsp+0x30` e `2*contagem` ponteiros em `rsp+0xb0`: 32 vagas de cada. Com 17 objetos, os dwords 32 e 33 caem em cima do primeiro ponteiro.

Contorno: o `je` em `0x1409a8416` vira `jmp`, e a atualização sempre segue pelo caminho sem tarefas, limitado a 16. Custo: essa atualização deixa de ser paralela. Log: `GhostLab[serial]: ...`.

## Gerenciador de rodas transbordando em si mesmo (`0x140978b00`), crash na largada às 11:24

A especial carregou (16 carros desenhados, 17 corpos), mas com lag forte em alguns momentos, e na largada o jogo caiu em `ntdll+0x2393b` (`EnterCriticalSection`, `rcx=ffaeaeae00000000`). Pilha: `0x14083fe1e` (trava em `[x+0x2c8]`) ← `0x1408419d5` ← `0x1409a9c99` (`[gerenciador+0x748]`) ← laço de entidades `0x140bc66e5` (método `+0x40`).

O gerenciador de rodas (vtable `0x141394910`) tem 16 blocos de `0x70` em `+0x20`. Logo depois vêm a contagem (`+0x720`), o índice cíclico (`+0x724`), o carro atual (`+0x728`) e o ponteiro `+0x748`. `0x140978b00` copia a contagem de carros (`+0x19640`, 17) para `+0x720` sem limite e monta um bloco por carro (`0x140972ff0`). O 17º bloco é o próprio `+0x720..+0x790`. O laço cíclico de `0x1409a9d59` usa essa contagem estragada (provável causa do lag), e o ponteiro `+0x748` vira lixo.

Contorno: `mov edx,[rax+0x19640]` (`0x140978b1a`) passa por uma caverna que limita a contagem a 16. Log: `GhostLab[rodas]: gerenciador de rodas limitado a 16 blocos.`

## Contagem de slots embaralhada (`0x1405f6330`), crash `exe+0x5f638a` às 11:28

Thread de trabalho, logo antes de os clones aparecerem. `0x1405f6330` decodifica a contagem de slots (chave global de `0x1400a0c00`, `0x1415d8510`, XOR `[dono+0x623e0]`, movido para `+0x93520`) e percorre os slots com `0x14092e410` (`dono + i*0x6228 + 0x660`). Veio `r14 = 0xbd53d83`, e no `i = 24` o "slot" era texto (`_desc_3`). Gravações da contagem: construtor `0x14091ebb0` (só a chave, contagem 0) e `0x140409ad7` (`max(contagem, [obj+0xb0]+1)`). Os 31 acessos a `0x623dX`/`0x623eX` estão na tabela do patch, e as três alocações foram ampliadas. Hipótese: no dono lido, `+0x93520` estava zerado (contagem decodificada = a própria chave). Não confirmado; o minidump não guarda o heap.

Contorno com diagnóstico: a leitura passa por uma caverna que limita a 24 e guarda quantas vezes passou de 24, o último valor e o dono. `LogSlotCountBad` escreve isso (com `+0x623e0` e `+0x93520` do dono) a cada 5 s por 60 s depois da carga.

## Lista de ponteiros com 13 vagas (`+0x62360`): causa da contagem embaralhada e do lag

O diagnóstico da caverna mostrou `+0x93520` com `0x6bb47718` = dono + `0xd8` (endereço do slot 0), ou seja, um ponteiro da lista e não a contagem. Foram 86 leituras ruins em 5 s com o jogo lento. A lista de ponteiros começa em `+0x62360`, e o primeiro campo depois dela é `+0x623c8` (dword embaralhado, gravado por `0x140933c13`), então ela tinha 13 vagas. A inserção (`0x14093b7b1`) não confere limite. Com o mesmo deslocamento para tudo (`0x31140`), as entradas 13+ caíam em `+0x93508` e a 17ª exatamente na contagem `+0x93520`.

Correção: a lista fica com 24 vagas (`0x934a0..0x93560`), e os campos de `+0x623c8` a `+0x62417`, mais o externo `+0x62748`, andam `0x31198`. A dxgi aloca `0x938e8` (subobjeto `0x935b0`) e avisa o tamanho em `DR2HOOK_SLOTOWNER_SIZE`; o core só aplica com `938e8`. Se a contagem não voltar a passar de 24, a caverna de `0x1405f6330` fica só como guarda.

## Array de 0xac0 por carro (`0x140445350`), crash `exe+0x44cf20` às 11:43

Com retrovisores desligados, 12 s depois de continuar a corrida, uma thread de trabalho caiu em `0x14044cf20` (`cmp [rdx+0x3030]`, `rdx=0xbebe2499bdb82fe1`). `0x14044c130(obj, carro, ...)` usa `r15 = [obj+0x90] + carro*0xac0 + roda*0x260`. O construtor `0x140445350` aloca esse array (`0x140442f40`, `n*0xac0`) com 16 fixos: `mov edx,0x10` (`0x140445454`) e `[obj+0x80]=0x10` (`0x14044545c`). Os laços usam `[obj+0x80]`. Contorno: os dois imediatos 16 → 24. Não houve minidump novo; análise só pelo log.

## Validado às 11:49

Com todos os patches acima, o usuário jogou cerca de 2 minutos com 16 fantasmas (retrovisores desligados): sem crash, sem travamento, sem leitura de slots acima de 24 no log. Resumo e tabela dos patches: `ghosts.md` §6.8.
