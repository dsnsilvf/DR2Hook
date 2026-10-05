# Prompt 2 para o Grok: a carga da especial espera para sempre quando nasce o 3º carro fantasma

Cole tudo abaixo. Anexe `docs/reverse_engineering/ghosts.md` §6.3 e, se puder, `dirtrally2.exe`. Este prompt continua a sua resposta anterior (CreateStageVehicle `0x14046af20`, SpawnStageVehicles `0x14046b320`, BuildParticipants `0x1404bd940`, FillStageEntries `0x1405b25c0`, BindGhostController `0x140511a80`, PickVisibleGhost `0x140515f00`).

---

Sua análise anterior estava certa na estrutura, mas a causa do limite estava errada. Testei no jogo ao vivo. Resultados:

## FATO medido
1. Na hora do spawn (hook em `0x14046b320`), a lista da sessão `[0x1416951e8]+0x30` (contagem `+0x40`) tem **6 entradas**, não 3: jogador mais **5 fantasmas** (tipo `+0x2c == 1`). A cada carga os endereços das entradas são os mesmos (pool reutilizado).
2. O byte `entrada+0xb4` (lido pelo método virtual `+0x108`, função `0x1404c9b00`, que faz o passe 2 pular a entrada) chega ao spawn assim, tipo/b4: `3/0  1/0  1/0  1/1  1/1  1/1`. Ou seja, as entradas de fantasma 3, 4 e 5 já chegam com `+0xb4 = 1` e **por isso só nascem `car 2` e `car 3`**. O número de participantes não é o limite.
3. Gravadores de `+0xb4 = 1` que achei: `0x140516b91` (laço em `0x140516ac0`: marca a entrada que nenhum veículo do vetor `[this+0x30]+0xf8..+0x100` referencia por `veículo+0xb0`; sem chamador direto, deve ser virtual) e `0x1404d18f4` (desmontagem de veículos com `+0xbf != 0`). `BuildParticipants` zera o byte em `0x1404bdbca`. **Não sei quem põe 1 nas entradas 3 a 5 antes do spawn.**
4. Experimento: antes do spawn eu zero `+0xb4` da entrada 3 (lista passa a `3/0 1/0 1/0 1/0 1/1 1/1`). O spawn **retorna normalmente** (log depois do `call`), o IO de carga continua uns segundos e a **carga nunca termina**. Não há exceção (um VEH registra violações de acesso: nenhuma). Repetido 3 vezes, em uma delas o processo morreu em silêncio ~5 s após o spawn, nas outras duas ficou travado.
5. Durante o travamento a thread principal (a do laço de frames: `0x1403c3a10` chama o `Sleep` de `0x14109f440`, aberto por `0x1405fa5c8`) **continua rodando**; é a tela de carga que espera uma condição. Threads de trabalho estão em espera do ntdll. Retornos da pilha da thread principal: `140870354 1403c3c83 1405fa5cd 140eeeefc 140ef3913 140ed9ffc 140ed9ba1 140ed9927 140ed899c 140ed7d14 140ed74c3 140eda59b ...` (máquina de estados recursiva em `0x140ed....`).
6. Durante a carga travada, os 5 controladores do gerenciador `[0x141695228]` ainda têm `+0x00 == 0` (nenhum veículo ligado) e todos os slots com estado `+0x1a8 == 0`; `gerenciador+0x31 == 1`. Sem travamento (carga normal), os controladores 1 e 2 ganham veículo depois.

## O que quero de você (nesta ordem)
1. **Qual condição a tela de carga da especial espera por veículo?** Procure a função de "todos os carros prontos" (loop sobre o vetor de veículos `[...+0x30]+0xf8..+0x100`, ou sobre as entradas da sessão) que decide sair do estado de carga. Diga RVA, o campo testado por veículo e quem o liga, e por que um veículo de fantasma extra (entrada 3, `+0xb0` apontando para ela) nunca o liga. Hipóteses a checar: o veículo precisa de um controlador ligado por `BindGhostController` (que só roda no ramo do fantasma do `BuildParticipants`, para entradas que casam no mapa) e a entrada 3 não passou por ali; ou espera dados do slot (`slot+0x1a8 == 2`) que o slot da entrada 3 não tem; ou espera um recurso de render/áudio.
2. **Quem põe `+0xb4 = 1` nas entradas 3 a 5 antes do spawn?** Se você achar, diga como evitar na origem (melhor que eu zerar à mão no spawn).
3. **Como dar à entrada extra o mesmo que as entradas 1 e 2 têm** para a carga terminar (registro no mapa do gerenciador, controlador ligado, slot com volta pronta etc.). Se for preciso chamar `BindGhostController` (`0x140511a80`) ou outra função, dê os argumentos e em que ponto do caminho.
4. Se `CreateStageVehicle` registrar o veículo em algum contador/lista que a carga compara com um total esperado (ex.: "N carros esperados = 2 fantasmas + 1"), diga onde esse total é gravado.

## Formato
- Separe **FATO** (RVA + trecho de disassembly) de **HIPÓTESE**.
- RVAs absolutos (0x14....) e nome provável de cada função.
- Se faltar dado, diga a leitura ao vivo exata (endereço + tamanho) que devo fazer enquanto a carga está travada (o jogo fica vivo, dá para ler a memória).
