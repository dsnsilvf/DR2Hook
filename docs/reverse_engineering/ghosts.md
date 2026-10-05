# Carros fantasma (ghosts)

Investigação de 2026-10-02. Análise superficial minha + dois despachos para o Gemini 3.8 Flash (`agy`), cujos relatórios brutos estão em `investigations/gemini/`. Abaixo, **verificado** = conferido por mim nos bytes/binário; **hipótese** = afirmado pelo Gemini ou deduzido, ainda sem teste.

## 1. Arquivos salvos

Verificado:
- Ficam no **Steam Cloud**: `~/.local/share/Steam/userdata/<conta>/690790/remote/`. Um arquivo por fantasma, `savegame@ghosts#ENDFX-0..14.*` (21–70 KB), dois pequenos (`#GTSRB.UEL`, `#QKRHMYXE`, índices?) e o perfil (`savegame@profile#NXDSMWW.BWW`, 146 KB, + `profile_backup`). Nada em `Documents/My Games`.
- Todos **cifrados**: entropia ~8 bits/byte, tamanhos múltiplos de 16, e os mesmos 16 primeiros bytes em todos (`64 bb 12 42 3e 54 78 3e 40 78 db 46 bd 90 0d 73`; os `#QKRHMYXE` têm outro bloco, igual entre si) → cifra de bloco com chave/IV fixos ou ECB sobre um cabeçalho igual.
- `bcrypt.dll` é importado só para hash (`BCryptOpenAlgorithmProvider/CreateHash/HashData/FinishHash/...`) e `BCryptGenRandom`: a cifra está no próprio exe (chave possivelmente derivada de hash).
- Formato decifrado: o desserializador `0x1409d10b0` compara o magic **`GHST`** (`0x54534847`) em `0x1409d10e9`.

Serializador (Gemini, não conferido): `0x1409d7f00`. O formato está descrito em "Formato GHST", abaixo.

### Cifra e contêiner (resolvido em 2026-10-02)

Verificado (derivação reimplementada do zero em `tools/dr2save.py`; decifra os 21 arquivos copiados):
- **AES-256-ECB, sem IV**, chave **fixa** (igual para qualquer conta): `91d84b7138a2cc4dadc022db4ebd1edd6c3454746acb235b618b404170b86e71`.
- Derivação, na inicialização do jogo (`0x140527140..0x1405271ce`): `0x14009f4d0("rp17", 3)` (FNV-1a de `"rp1"`), `xor 0x37` e `* 0x1000193` completam o FNV-1a de `"rp17"` = `0x7dc96bc7`. `0x14080f360(cifra, 0x7dc96bc7, 2)` semeia um MT19937 (init LCG 69069, semente `| 1`, `0x140859b80`), gera 64 dígitos hex com `rand % 15` (nunca sai `F`) e passa a string a `0x140815510(cifra, 2 = 256 bits, hex)`, que converte os pares em 32 bytes e expande a chave (14 rodadas, `0x140805f10`).
- Objeto de cifra: 0x250 bytes, construtor `0x1407fbe00` (vtable `0x1412ca1f8`); `+0x240` = CPU tem AES-NI (`cpuid` ecx bit 25); `+0x30` escolhe decifrar (0) ou cifrar (1) em `0x1407fbd40`, com rotinas AES-NI (`0x140806xxx`) ou T-tables (Te0 em `0x1412c68d0`, rotinas `0x14080b290..0x14080bd90`). Guardado em `[sistema+0x1c08]` e entregue a `0x140cc3910` (objeto de 0x370 bytes, bloco 0x10).
- Contêiner decifrado: cabeçalho de 24 bytes `u32 versão = 4`, `u32 tamanho do cabeçalho = 24`, `u32 compressão = 2` (zlib), `u32 0`, `u64 tamanho descomprimido` (bate exatamente nos 18 contêineres), depois zlib. O payload é serialização EGO (começa com `37 dd bb 4e`).
- Fantasmas (`#ENDFX-N`): dentro do payload, a partir do byte 137, há um segundo stream zlib cujo conteúdo começa com **`GHST`** (21–98 KB).
- `#QKRHMYXE` (fantasmas, perfil e backup) não são contêineres: decifram para o texto `Save System 2 Demo - Display Name` seguido de bytes binários. `#GTSRB.UEL` é contêiner (1240 bytes, sem `GHST`). O perfil descomprime para 2 658 560 bytes.

Relatório bruto: `investigations/gemini/ghost-cipher.md`.

### Formato GHST (resolvido em 2026-10-02)

Verificado: `tools/dr2ghost.py` lê os 15 fantasmas até o último byte, com as contagens do cabeçalho batendo, e o ENDFX-9 (carregado no jogo naquele momento) é idêntico, amostra por amostra, aos arrays do slot na memória (§3, "Na memória").

```
"GHST"  u32 tamanho (= arquivo - 8)  u8 versão (7)  u8 máscara de canais (0x7f)
por bit da máscara:  u32 contagem, u8 tamanho da amostra no arquivo
u16 tamanho dos metadados, depois itens: u8 id, u8 len, valor
segmentos até o fim do arquivo:  u32 tamanho, u32 t0 (ms), registros
registro:  u8 delta_ms, u8 máscara, payload de cada canal marcado (em ordem de bit)
```

- O relógio começa em `t0` e soma `delta_ms` a partir do 2º registro. `ff 00` (delta 255, nenhum canal) só avança o tempo em intervalos maiores que 255 ms.
- Vários segmentos aparecem quando há salto de tempo (reset para a pista?). Muitos arquivos começam com um segmento vazio (tamanho 4, só `t0 = 0`).

| bit | canal | bytes | conteúdo | memória (amostra) |
|---|---|---|---|---|
| 0 | posição | 14 | 3 `float` + `u16` progresso (÷65535) | 0x30: `+0x10` vec3, `+0x20` progresso `float`, `+0x24` 4×`u16` (rodas?) |
| 1 | rotação | 4 | quaternion 4×`int8` (÷127), só quando muda | 0x18: `+0x10` os mesmos 4 bytes |
| 2 | entradas | 4 | 4×`u8` (acelerador 255, freio, direção?, marcha?) | 0x18: `+0x10` os mesmos bytes |
| 3 | 1 Hz | 4 | `float` (195.7, 54.5, −81.2… rumo em graus?) | 0x18: 12 bytes em outra forma, conversão desconhecida |
| 4 | rodas | 8 | 8×`u8` (÷127; 0x54 → 0,6614 parado) | 0x30: 8 `float`, ordem `0 1 4 5 2 3 6 7` |
| 5 | único | 4 | uma amostra no início (zeros) | não carregado no slot |
| 6 | eventos | 20 | marcadores (bytes 15–16 sobem 1, 2, 3…) | 0x28: `+0x10` os mesmos 20 bytes |

- A memória guarda uma amostra a menos por canal (a última do arquivo fica de fora).
- Metadados: `0` = texto de 4 dígitos que muda com o carro (igual a `slot+0xc0`); `3` = tempos de setor em ms (4 ou 8 setores; a soma é o tempo total); `4` = tempo total em ms; `1` (`float`, 1083.5 / 1162.5 / 1359.2), `10` e `15` acompanham o carro; `5`, `8`, `9` (≈0,97) mudam por volta; `11`, `12` (15), `2`, `13`, `14`, `16` (zeros) ainda sem significado.
- Intervalos reais: 133 / 267 / 1017 ms = 8 / 16 / 61 ticks de 60 Hz (o teste é `elapsed > período`, então cai no tick seguinte).

## 2. Sistemas no executável

Strings: `game::GhostLapSystem`, `ghost_lap_recorder` (0x141273588), `ghost_lap_manager` (0x141273570), `GhostData`, `ghost_data`, `ghost_index`, `ghostsys_start`, estados `StateSaveLoadGhost`, `StateTimetrialGhostSelect`, `StateTimeTrialGhostDownload`, `GhostCar.Download`, e na UI `ghost_1..3_visible` / `ghost_1..3_header` (até 3 fantasmas na seleção). O download de fantasmas do ranking é online (bloqueado pelo NetworkGuard).

## 3. Gravação e reprodução

Verificado:
- Amostragem por canais, com períodos em constantes `double`: **rotação a cada 0,125 s (8 Hz)** — `0x1412820a0`, lida em `0x1409d5b1e`; **posição a cada 0,25 s (4 Hz)** — `0x14139ad68`, lida em `0x1409d5bc3`. As duas leituras ficam no gravador `0x1409d59d0`.
- Funções citadas pelo Gemini começam onde ele indicou (via `.pdata`): `0x1409ce4d0`, `0x1409d59d0`, `0x1409cfd30`. Vtable `0x1412802e8` (GhostLapSystem) com slot 0 `0x14051eb10`.

Na memória (verificado ao vivo em 2026-10-02, especial da Finlândia pausada com fantasma):
- Único `GhostLapSystem` (vtable `0x1412802e8`), cujo `+0x18` aponta para o gerenciador. No gerenciador: `+0x18` = cabeça do `std::map` (MSVC; nó: esquerda/pai/direita, `+0x19` isnil, chave `+0x20` = participante, valor `+0x28` = slot), `+0x20` = tamanho (5), `+0x38` = gravador da corrida atual, `+0x60`/`+0x70` = dois pontos ao lado da chegada (postes da linha?).
- Slot (0x260): `+0x1a8` estado (2 = pronto); contagens `u32` em `+0x1ac` rotação, `+0x1b0` posição, `+0x1b4` entradas, `+0x1b8` 1 Hz, `+0x1bc` rodas, `+0x1c0` eventos; ponteiros em `+0x1c8` rotação, `+0x1d0` posição, `+0x1d8` entradas, `+0x1e0` 1 Hz, `+0x1e8` rodas, `+0x218` eventos; capacidades em `+0x220..+0x248` (gravador: 14400/7200/14400/1800/7200/1800 = 30 min). Toda amostra tem vtable própria por canal (`0x14139a550`, `…590`, `…5d8`, `…658`, `…6a0`, `…720`) e `u32` tempo em ms em `+0x08`.
- Dos 5 slots, só o 0 tinha dados (o fantasma); a vtable `0x1414277b0` não é do gravador (aparece 1284 vezes no heap, é de um subobjeto em `slot+0xa8`).
- A matriz de mundo do carro fantasma (4×4, translação em `+0x30`, cópia em `+0x40`) estava em t≈6,35 s do trajeto gravado; a do jogador, em t≈5,6 s.
- `EvaluateGhostState` `0x1409ce4d0` (desmontado): `rcx` = dono com `[rcx+0x20]` = slot; `rdx` = `&tempo` (8 bytes); `r9` = saída. Retorna 2 se `slot+0x1a8 != 2`. Antes de avaliar, soma ao tempo o `float` global `0x141f593e0` (0 por padrão; serve para adiantar/atrasar o fantasma) e, se as flags `0x141f593e4`/`e5` estiverem ligadas, mais dois ajustes (`0x1415b3d78`/`7c`, 1,0). Saída: `+0x00..0x30` rotação, `+0x30` posição, `+0x40` velocidade, `+0x50`, `+0x54` progresso, `+0x58` tempo, `+0x60` válido, `+0x61`, `+0x64/68/6c` entradas, `+0x70` rodas.

Hipóteses (Gemini; endereços vistos por ele no binário/dump, não testados em jogo):
- Canal de rotação reproduzido com SLERP (`0x14052cd90`, normalização em `0x1409ce9d6`).
- Posição interpolada com **Catmull-Rom** sobre 4 pontos (`0x1409cf1c4`; linear em `0x1409cf5ae`).
- Métricas a 1 Hz; gatilhos em checkpoints/splits.
- Reprodução guiada pelo tempo de corrida: `EvaluateGhostState(slot, &tempo, ?, out)` em `0x1409ce4d0` — `out+0x00` matriz de rotação, `+0x30` posição, `+0x40` velocidade.
- O carro fantasma é **só visual** (sem corpo Havok): atualizado em `0x14051845e` via `0x140db9410` (matriz), `0x140db90e0` (vel. linear), `0x140db8930` (vel. angular); shaders `ghost_car_depth`/`ghost_car_transparent`.
- `GhostLapSystem` (0xf0 bytes; ctor `0x1404f6e40`, dtor `0x1404fa700`); `ghost_lap_recorder` (0x258 bytes; ctor `0x1409cb190`; vtable `0x1414277b0`).
- Slots num `std::map<uint64_t, GhostSlot*>` em `ghostSys+0x18`; no dump de 2026-10-01 havia **5 slots ativos** (1 local + 4 fantasmas), participante 0x140 bytes, `GhostSlot` 0x260. Estado do slot em `+0x1a8` (2 = pronto?).
- Cópia da volta gravada para o slot permanente ao fim da especial: `CopyGhostLapData` `0x1409cfd30`.
- Limite de 3: da UI de seleção; o motor aceita N slots.

## 4. O que dá para fazer (avaliação)

1. **Diferença ao vivo para o fantasma**: FEITO e validado no jogo (§6, `GhostLab`).
2. **Injetar volta própria** (ex.: gravada a partir de um checkpoint) via `CopyGhostLapData` ou os buffers do slot. O caminho está provado pelas cópias (§6); falta a fonte da volta.
3. **Vários fantasmas**: 2 na tela validados (original + 1 cópia). Mais que 2 exige subir o limite de carros fantasma (§6.3).
4. **Fantasma sólido** (resolvido e validado no jogo em 2026-10-02): opacidade em `GhostCarValues.x`; a aura, onde o carro some de vez colado no jogador, é o descarte do desenho quando o fator de esmaecimento vale exatamente 1,0 (§6.4). A opção *Solid ghost car* força `x = 1` e segura o fator em 0,999.
5. Exportar/importar fantasmas (`GHST`): cifra e formato resolvidos (§1, `tools/dr2save.py`, `tools/dr2ghost.py`); falta recifrar e reempacotar o contêiner EGO.

## 5. Arquivos de apoio

- `tools/dr2save.py`: decifra os saves (contêiner e `GHST`).
- `tools/dr2ghost.py`: lê o `GHST` (cabeçalho, metadados, canais) e exporta a trajetória em CSV.
- `tools/pssg.py`: parser PSSG escrito pelo Gemini (usado no texto rico; ver `ui_tabs.md`).
- `investigations/gemini/ghost-runtime.md`, `investigations/gemini/pssg-ui-text.md`: relatórios brutos.

## 6. GhostLab: testes no jogo (2026-10-02, tarde)

Código: `src/core/ghost_lab.cpp` (core, recarrega com F8), opções no mod Practice Mode (*Live gap to ghost*, *Extra ghost copies*, *Ghost copy spacing*, *Ghost head start*, *Solid ghost car*) e Lua `Ghost.status/clone/setOpaque/setTimeOffset/setHud`. Branch `feat/ghost-live`. Relatórios brutos do Gemini usados aqui (fora do git): `captures/gemini/ghost_visual/REPORT.md` e `captures/gemini/ghost_transparency/REPORT.md`, ambos com erros pontuais corrigidos abaixo.

### 6.1 Diferença ao vivo (validado)

- Hook em `EvaluateGhostState` (`0x1409ce4d0`, thread do jogo): no fantasma de referência copia o trajeto do slot (canal de posição) e guarda a posição avaliada.
- A cada frame, jogador e fantasma são projetados no trajeto (segmento mais próximo, busca perto do último índice e no todo se ficar a > 30 m). Diferença = tempo do fantasma − tempo em que ele passou onde o jogador está; metros = distância acumulada no trajeto.
- HUD no overlay (topo central): vermelho = atrás, verde = à frente. Some na pausa (o jogo não avalia fantasmas pausado).

### 6.2 Cópias e o 2º carro (validado)

- Gerenciador global `[0x141695228]`; slots no `std::map` em `+0x18`/`+0x20` (§3).
- Controladores de carro fantasma: 5, de 0x100 bytes, contíguos; `+0x00` veículo, `+0x08` dono passado a `EvaluateGhostState`, `+0x28` slot, `+0x48` saída, `+0x58` tempo, `+0x60..+0x63` flags. Atualizador ~`0x140518400`: sai se `+0x63 == 0`; `+0x62` liga a chamada a `0x1409da680` a cada frame; `+0x61` dispara o reset `0x140512700`.
- Cópia: `CopyGhostLapData(dest, fonte, false)` (`0x1409cfd30`, aloca com o alocador do jogo via `0x1409cd610`) num slot livre + todos os tempos deslocados de k × espaçamento.
- O jogo cria só 2 veículos de fantasma (`car 2`, `car 3`; `car 1` = jogador). O controlador do `car 3` é avaliado, mas fica com `+0x62 = 0` e o carro não é desenhado. **Ligar `controlador+0x62 = 1` faz a cópia aparecer** (validado com o jogo recém-aberto). O jogo zera essa flag no Reiniciar; o mod religa a cada frame. `veículo+0x140` (ponteiro de volta, segundo o Gemini) não importa: fica 0 nos dois carros.
- `0x1409da680` mede a distância ao jogador e grava em `veículo+0x94` (`0x14099db20(veículo, v, 3, 0)`). Ao vivo (2026-10-02, noite) esse float alimenta `GhostCarValues.x` (§6.4): ~0,5 com o fantasma a centenas de metros (opacidade 0,5) e sobe para ~0,87–0,98 quando o fantasma está perto (opacidade 0,13 e 0,02). Os vizinhos `+0x88`, `+0x8c`, `+0x90`, `+0x98` e `+0x9c` estavam em 0 no jogador e nos dois fantasmas. Com `x` forçado em 1 o carro ainda sumia dentro da aura: colado no jogador esse float chega a exatamente 1,0 e o desenho descarta o carro (§6.4).
- O jogo reorganiza os slots na largada, e uma versão anterior chegou a deixar o fantasma original deslocado 1 s. Desde `27697b3`, uma cópia é reconhecida pela volta (contagem de posições + 1ª posição), a fonte é a que começa mais cedo e o tempo original é restaurado se ela aparecer deslocada. Ainda falta confirmar no jogo que a cópia volta em vários Reiniciar seguidos.

### 6.3 Máximo de fantasmas

- 5 slots/controladores: `mov r13d, 5` em `0x1405ba800` (função `0x1405ba200`).
- 2 carros fantasma: no criador de veículos `0x140a882f9`, `mov eax, 2` em `0x140a8834f` e `mov r12d, 2` em `0x140a884d1` (conferido nos bytes).
- Para 3 ou mais: subir esses limites (até 5) e garantir os descritores de instância de render dos carros novos. Não testado.

**Teste de limite (2026-10-04, tecla F7, log `GhostLab[limite]`):** cada F7 pede mais uma cópia (`GhostLab::SpawnClone`). Com 5 slots (1 fonte), o jogo aceitou 4 cópias e não caiu: a 5ª e a 6ª pedidas deram "feitas 4: sem slot livre". Ou seja, o teto das cópias de dados é 4 (limite do gerenciador, `mov r13d, 5`). Só 1 cópia aparece na tela (`car 3`); as outras avaliam sem carro desenhado.

**Correção:** o trecho `0x140a8834f`/`0x140a884d1` (`mov eax, 2`, `mov r12d, 2`) cria `windscreen_camera`, `windscreen_activation_map` e `QuadBlitRenderInstance` (strings em `0x1413aad78`, `0x1413aad90`, `0x1413a6ba0`); não é o criador dos veículos fantasma. O número de veículos fantasma (2) vem de uma lista de dados lida em `0x14046b33c`/`0x140426760` (entradas com tipo `+0x2c == 3`) e ainda não foi localizado. Para ver mais de 1 cópia, falta achar onde essa lista é montada.

### 6.4 Transparência e aura (resolvidas em 2026-10-02, noite)

Dois efeitos empilhados, os dois vindos do mesmo fator de esmaecimento. O primeiro é a opacidade do shader. O segundo é o descarte: com o fator em exatamente 1,0 (fantasma a 5 m ou menos do jogador) as funções de desenho pulam o carro, e ele some por inteiro mesmo com a opacidade forçada em 1. É isso que se chamava de "aura": o carro some ao encostar e volta ao sair da caixa do carro.

#### Onde a opacidade mora

O shader do carro lê um `float4` `GhostCarValues` por instância (`$Globals+512` no shaderpack; o nome não aparece em `game.dat`, `game_1.dat`, `game_2.dat` nem `game.nefs`, só no exe). Bloco em `[0x14159dad8]` (o qword no endereço `0x14159d9e0+0xf8`; não é `*(*0x14159d9e0+0xf8)`). Nome em `bloco+0x1a0b8`. As entradas são os ponteiros em `bloco+0x1a0f0`, `+0x1a0f8` e `+0x1a100` (jogador e dois fantasmas; `+0x1a108` é 0). `+0x1a0d0` é um descritor estático no exe, não a lista. Cada entrada é `objetoDeRender+0x4f80`, registrada em `0x140969195` como `lea rax, [rbx+0x4f80]`.

Layout visto ao vivo:

| offset | jogador | fantasma |
|---|---|---|
| `+0x00` | `(1, 0, 1, 0)` | `(opacidade, 0, 1, 1)` |
| `+0x10` | posição | posição |
| `+0x20` | `(0,5, 0, 0,5, 0)` | o mesmo |

`w` não é um tipo gravado à parte: o packer põe `w = 0` quando `x == 1` e `w = 1` no resto.

Quem grava o float4 todo frame é a folha `0x140bd7500`, única chamada em `0x1409655b1` com `edx = 0`:

- `x = clamp(1 - [objeto+0x5058], 0, 1)`
- `y` = `[objeto+0x4f24]` (veio de `veículo+0xa0`; estava 0)
- `z` = 1
- `w` = 0 se `x == 1`, senão 1

`[objeto+0x5058]` é o máximo de `veículo+0x88`, `+0x8c`, `+0x90`, `+0x94`, `+0x98` e `+0x9c` (`0x140965551`). Esse máximo só entra com a câmera de jogo no ramo normal: `[rdi+0x38]` não nulo, `+0x3c6 == 0` e `+0x360 != 0`. No outro ramo o máximo é 0 e o carro sai opaco. Conferido ao vivo: o máximo bateu com `1 - GhostCarValues.x` nos três carros (vtable do veículo `0x14127cc00`). No jogador os seis floats eram 0 (tipo `+0xbc = 0`). No fantasma (tipo 3) só `+0x94` saía de zero.

#### A curva de 5 m a 50 m

`0x1409da680` (chamada em `0x140518625` quando `controlador+0x62 != 0`) mede a distância ao jogador e grava o resultado em `veículo+0x94` via `0x14099db20(veículo, valor, 3, 0)`. O remap é `0x1409cf9f0`, constantes em `0x1415b3d40`:

- distância ≤ 5 m → 1
- distância ≥ 50 m → 0,5
- no meio, interpola de 1 até 0,5

Ao vivo: a centenas de metros, `+0x94 = 0,5` e a opacidade era 0,5. Perto, `+0x94` foi a 0,8686 e 0,979 e a opacidade caiu para 0,131 e 0,021. `0x14099db20` é um armazenador genérico (15 chamadas); o índice `(r8, r9) = (3, 0)` cai em `+0x88 + 12`.

#### O que a opção faz

*Solid ghost car* (`Ghost.setOpaque`):

1. Hook em `0x140bd7500`. Se a entrada saiu com `w >= 0,5`, regrava `x = 1` e `w = 0`. O jogador não entra nesse caso. Log: `fantasma solido (GhostCarValues.x = 1)`.
2. Hook em `0x140986320`. Essa função lê `veículo+0xbc` para `[rbp+0xf8]` (`0x14098637d`). `0` segue o passe opaco (`[rsp+0x68] = 1` em `0x140986830`). Qualquer outro valor cai no `jne 0x14098687d` (`0x1409867cf`) e o passe transparente fica com `[rsp+0x68] = 0`. Com a opção ligada, se a vtable é `0x14127cc00` e o tipo é 3, o campo vira 0 só durante a chamada e volta a 3 antes do retorno.

3. No mesmo hook do item 1, antes de chamar o original: se `objeto+0x5058 >= 1` (canal 0), grava 0,999. O desenho deixa de descartar o carro, e o item 1 continua forçando a opacidade em 1.

Validado no jogo (2026-10-02, noite): com os três itens, o fantasma fica sólido longe e continua visível e sólido encostado no jogador. Os itens 1 e 3 são os que contam. O item 2 já estava ligado no teste, mas não se mediu se ele muda algo sozinho.

#### Descarte com fator 1,0 (achado por dump)

Achado comparando três dumps da memória gravável (`captures/dumps/memdump.py`, fora do git): A longe, B encostado em um fantasma (esse invisível, o outro visível), C longe de novo.

| | fantasma 1 | fantasma 2 |
|---|---|---|
| A (longe) | `+0x94 = 0,561` | `0,500` |
| B (1 encostado, invisível) | **`1,000`** | `0,767` (visível) |
| C (longe) | `0,511` | `0,846` |

`GhostCarValues.x` estava em 1 nos dois no B (hook do item 1), e `objeto+0x5058` repetia `veículo+0x94`. Só o valor 1,0 exato esconde o carro; 0,846 continua visível.

O getter é `0x140bcf9d0` (`movss xmm0, [rcx+rdx*4+0x5058]; ret`). As chamadas que descartam o carro comparam com 1,0 (`[0x1411f7cb8]`) por igualdade e pulam:

- `0x1409535f8` → `ucomiss xmm0, xmm9` / `je 0x1409539b7`
- `0x140953a8d` → `ucomiss xmm0, xmm8` / `je 0x140953bda`
- `0x140953add` e `0x140953c96` (mesmo padrão)
- `0x140955dc5` → `ucomiss xmm0, xmm14` / `je 0x140955e9d`
- `0x140b516d5` → `comiss xmm0, xmm7` / `jae` (pula com fator ≥ 1)

Outros leitores: `0x1403a8e2a` (grava `1 - fator`), `0x1409653d8` e `0x140a7e100` (`ja` contra 0). O fator é gravado em `0x140c1e311`, `0x140bd62d3` e `0x140bcabe7`, e lido pelo packer em `0x140bd7510`.

#### Descartado

- **Pular `0x14095fe90`** (troca dos 23 materiais `car_matt`, `carpaint_metallic`, `carglass`, … pelos `*_ghost`, mais `ShadowsDisabled = (1,1,1,1)` via `0x14090a030`). Chamada por `GhostCarPlugin::Init` `0x140b94650` em `0x140b948c5`, uma vez por execução, na abertura. Pulada desde a abertura, o fantasma continua transparente por causa de `GhostCarValues.x`. A opção ainda instala esse hook; não é o que deixa o carro sólido.
- **`veículo+0xbc = 0` permanente.** O campo é o tipo (0 = carro físico; 3 = fantasma, técnica `Instanced3`), gravado em `0x140a8847f` e `0x140b9840d`. Deixado em 0, na largada o fantasma colidiu: dano terminal e peças voando. A troca durante `0x140986320` existe para usar o mesmo ramo de desenho sem deixar o tipo físico para a simulação.
- **Gravar `1` em `veículo+0x94`.** Como a opacidade é `1 - max(...)`, isso zera o `x` e o fantasma some. O sentido visto ao vivo é o contrário do que se anotou primeiro (não é 1 longe e 0,5 perto).
- **Caminhar a lista de `GhostCarValues` a cada Present** com um ponteiro a mais e tratando `+0x1a0d0` como vetor. Não achava as entradas. O packer é o escritor.
- **`xrayEffectParameter`** (`bloco+0x19a10`, valor `0; 1,843; 1,775; 1`), ligado por `0x1409a2ee0` via `0x14090a030`: uma cópia na memória; zerar não mudou nada visível.
- **NOP em `0x1409a9a3c`** (modo de render) e o `GhostedTransparencyManager` (`0x140056a60`): não são o escritor de `GhostCarValues.x`. O `jne` em `0x1409867cf` é o desvio do passe, tratado no item 2 acima, não um NOP deixado no exe.

### 6.5 Colisão com o fantasma (2026-10-03)

Experimento só no core (`src/core/ghost_lab.cpp`): (sem tecla desde 2026-10-03; F11 virou insta crash) `GhostLab::ToggleCollision` liga/desliga a colisão dos carros fantasma com o jogador. Carregar, largar e pausar desligam. Um F11 apertado com o jogo pausado fica esperando e só se aplica quando o jogo volta a rodar.

#### O corpo físico (`veículo+0x30`)

| Campo | Carro | Fantasma | O que é |
|---|---|---|---|
| `+0xf0` | 1 | 0 | pedido de "corpo sólido" |
| `+0xf1` | 1 | 0 | estado aplicado |
| `+0xf2` | 0 | 1 | "sujo": aplique `f0` no próximo passo |
| `+0x194` (dword alto das flags em `+0x190`) | `0xffffffff` | `0xfffffffb` | guardado ofuscado: `gravado = valor * 0x45fa8d8d`, `valor = gravado * 0x6427d45`. O fantasma tem o bit 4 apagado |
| `+0x198` | 3 | 2 | modo da grade de contato (3 = 5×7, 2 = 1×1). `0x140dbcd00` regrava conforme `+0xd48` e a distância |
| `+0xc98` | forma | nulo | forma de contato instalada |
| `+0xce0`/`+0xce4` | 5/7 | 1/1 | dimensões da grade, **não** máscara de colisão (gravar não liga nem desliga) |
| `+0xbf50` | 10.0 | 1.0 | gravado pela montagem |

- **Passo da física** (`0x140dbc500`, para os corpos da lista `0x14201b7b0`, contagem `0x14201b930`): se `f2` está ligado, zera `f2` e copia `f0` para `f1`. Se o valor mudou, monta as formas (`0x140da94c0(corpo, dl=1)`, que só entra com `f1 != 0`) ou desmonta (`0x140da6770`).
- **Criação** (`0x14097ad60`, bloco só para o tipo 3 em `0x14097b1d8`):
  - apaga o bit 4 via `0x140db8970`/`0x140db9220`;
  - chama `0x140dad720`, que grava `f0 = 0` e `f2 = 1`. **O tipo 3 também chama essa função a cada frame.**
- **Ligar:** bit 4, `+0x198 = 3`, `f0 = 1`, `f2 = 1`. O passo seguinte monta as formas.
- **Desligar:** `f0 = 0`, `f2 = 1`, sem tocar em `f1`. O passo vê a mudança e desmonta.
  - **Armadilha:** zerar `f0` e `f1` juntos deixa a forma montada para sempre (`f0 == f1`, nada a aplicar). O fantasma colide até a especial ser recarregada. Foi o "não desliga" de vários testes.

#### Tipo do veículo e replay

- Trocar `veículo+0xbc` de 3 para 0 no meio da especial liga a colisão (com os campos acima), mas **quebra o replay**: crash em `0x140adb33e` ao processar a gravação, no fim da especial. Confirmado duas vezes, inclusive sem nenhum Reiniciar no meio.
- Um Reiniciar com o fantasma em tipo 0 também o põe no passe de spawn como carro. Resultado: batida na largada.
- Versão atual: o tipo fica em 3, e um hook em `0x140dad720` ignora os corpos com colisão ligada. **Ainda não testada no jogo.**

#### Descartado

- **Registros dos passes de spawn** (`0x14049ced0`: listas `0x1409f2f30`/`2fa0`/`2ed0`/`0x140434230`, objeto `0x1404d1740`, id `0x1400ce8f0`, callback `0x140c25090`). Lidos ao vivo com os fantasmas colidindo: só o jogador estava neles. A colisão carro com carro não passa por eles.
  - O sistema dos passes é um objeto único com vtable `0x141277370`.
  - `0x140db7550` só troca uma callback na tabela de 16 entradas em `corpo+0xc1e0`.
- **Hook em `0x1404af520`:** é um reinício, mas não é o do botão Reiniciar da pausa. Nunca disparou.
- **Cronômetro de 3 s:**
  - contado pelo tempo do fantasma no trajeto: ligou com 10 s, porque o fantasma fica parado depois da largada;
  - contado pelo tempo de jogo: trocado pela tecla F11.
- **F10:** é tecla de sistema no Windows, e o segundo toque se perdia.

Material: prompts e extratos do Grok em `captures/grok/` (r1, r2 e r3). Dumps D1, E1, E2 e F1 em `captures/dumps/`, fora do git.

### 6.6 Formato e ferramentas

- `tools/dr2ghost.py` lê o `GHST` (de um save cifrado ou de um `.ghst`) e exporta a trajetória em CSV (§1).
- Leitura e escrita ao vivo durante os testes: `/proc/<pid>/mem` (o heap muda a cada execução; partir do global `[0x141695228]`).
