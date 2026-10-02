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

1. **Ler o fantasma a cada frame** (hook em `0x1409ce4d0`): posição/velocidade/rotação → **diferença ao vivo para o melhor tempo** no Practice Mode (à frente/atrás em metros e segundos). Mais valioso e mais seguro (só leitura).
2. **Injetar volta própria** (ex.: gravada a partir de um checkpoint) como fantasma, via `CopyGhostLapData` ou os buffers do slot, sem passar pelo arquivo cifrado. Provável.
3. **Vários fantasmas**: acrescentar slots ao mapa; falta saber como instanciar o modelo visual de cada slot extra. Hipótese a testar.
4. Exportar/importar fantasmas (`GHST`) e convertê-los: cifra e formato resolvidos (§1, `tools/dr2save.py`, `tools/dr2ghost.py`); falta recifrar e reempacotar o contêiner EGO.

## 5. Arquivos de apoio

- `tools/dr2save.py`: decifra os saves (contêiner e `GHST`).
- `tools/dr2ghost.py`: lê o `GHST` (cabeçalho, metadados, canais) e exporta a trajetória em CSV.
- `tools/pssg.py`: parser PSSG escrito pelo Gemini (usado no texto rico; ver `ui_tabs.md`).
- `investigations/gemini/ghost-runtime.md`, `investigations/gemini/pssg-ui-text.md`: relatórios brutos.
