# Prompt para o Grok: por que só existem 2 carros fantasma e como subir o limite

Cole tudo abaixo. Anexe `docs/reverse_engineering/ghosts.md` (§3, §6.2, §6.3) e, se puder, `dirtrally2.exe` (Ghidra/IDA).

---

Você é um engenheiro de engenharia reversa (x64, Windows). Preciso saber de onde vem o limite de **2 veículos de fantasma** no dirtrally2.exe (DiRT Rally 2.0, imagem base 0x140000000, sem relocação) e como criar mais de 2, com carro desenhado.

## Sintoma e o que já sei
1. Na especial existem 3 veículos físicos de `0x9b0` bytes, vtable `0x14127cc00`, nome em `+0x48`: `car 1` (jogador), `car 2` e `car 3` (fantasmas).
2. O gerenciador global `[0x141695228]` tem **5 slots de fantasma** (stride `0x260`, `std::map<uint64_t, GhostSlot*>` em `+0x18/+0x20`, estado em slot`+0x1a8`, 2 = pronto) e **5 controladores** de `0x100` bytes contíguos (`+0x00` veículo, `+0x08` dono passado a `EvaluateGhostState`, `+0x28` slot, `+0x58` tempo, `+0x60..+0x63` flags). Alocação: `mov r13d, 5` em `0x1405ba800` (função `0x1405ba200`).
3. Meu mod copia o fantasma para os slots livres (`CopyGhostLapData` `0x1409cfd30`). Medido: **4 cópias de dados cabem** (5 slots menos a fonte), o jogo não cai. Mas **só 1 cópia é desenhada**: o controlador do `car 3` fica com `+0x62 = 0` e eu religo à mão. Os controladores dos slots 4 e 5 provavelmente não têm veículo (`+0x00 == 0`; a confirmar ao vivo).
4. **Correção de uma hipótese anterior (não repita):** os `mov eax,2` em `0x140a8834f` e `mov r12d,2` em `0x140a884d1` criam `windscreen_camera`, `windscreen_activation_map` e `QuadBlitRenderInstance`. **Não** são o criador dos veículos fantasma.
5. Pistas fracas: a lista global de veículos físicos é percorrida em `0x14046b33c` (loop sobre objetos com tipo `+0x2c == 3`, chama `0x140426760` e `0x14046af20`). Atualizador do controlador ~`0x140518400` (sai se `+0x63 == 0`; `+0x62` liga `0x1409da680`). Fantasma é só visual (sem corpo Havok), posição por `EvaluateGhostState` `0x1409ce4d0` → `0x140db9410`. Shaders `ghost_car_depth` / `ghost_car_transparent`. Opacidade em `GhostCarValues` (`render+0x4f80`, entradas em `bloco+0x1a0f0/f8/100`, ou seja, jogador + 2 fantasmas; `+0x1a108` é 0).

## O que quero de você (nesta ordem)
1. **Quem cria os veículos `car 2` e `car 3`**: a função que aloca `0x9b0` bytes e grava o nome `"car %d"`/equivalente. Dê o RVA, o chamador, e **de onde vem a contagem** (constante, campo de config, tamanho de uma lista de participantes, número de fantasmas escolhidos na UI).
2. **Quem decide o número de fantasmas ativos** quando o jogador escolhe fantasmas na tela de seleção (limite de UI de 3 contando o jogador?) e como isso chega ao criador do item 1. Há um campo "ghost count" no `GhostLapSystem` (`0xf0` bytes, ctor `0x1404f6e40`) ou no gerenciador?
3. **Como ligar um controlador a um veículo novo**: quem escreve `controlador+0x00` (veículo), `+0x08` (dono), `+0x28` (slot) e `+0x60..+0x63`; quem liga `+0x62`; o que mais o veículo precisa para ser desenhado (descritor de render no array `+0x290` do `GhostedTransparencyManager`, buffer de instância `+0x08`, entradas de `GhostCarValues`). Compare `car 2` (desenha) com `car 3` (não desenha sem meu `+0x62`).
4. **Proponha um patch mínimo** para ter N veículos fantasma (N até 5): bytes a trocar (RVA, bytes antes/depois) e/ou uma chamada a uma função do jogo com argumentos, feita de dentro do processo depois do carregamento da especial. Diga o que acontece com o limite de 3 entradas do `GhostedTransparencyManager`/`GhostCarValues` e se ele precisa crescer.
5. Diga o risco de crash de cada passo (o que outros sistemas leem com tamanho fixo: arrays de 2 ou 3 posições, filas de render, áudio).

## Formato
- Separe **FATO** (visto no código, com RVA e trecho de disassembly) de **HIPÓTESE**.
- RVAs absolutos (0x14....) e nome provável de cada função.
- Se faltar dado, diga exatamente qual leitura ao vivo eu devo fazer (endereço + tamanho) em vez de chutar.
- Não repita: o limite de 5 slots já está confirmado; a origem do "2 carros" NÃO é `0x140a8834f`.
