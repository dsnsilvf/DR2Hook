# Carros fantasma (ghosts)

Investigação de 2026-10-02. Análise superficial minha + dois despachos para o Gemini 3.8 Flash (`agy`), cujos relatórios brutos estão em `investigations/gemini/`. Abaixo, **verificado** = conferido por mim nos bytes/binário; **hipótese** = afirmado pelo Gemini ou deduzido, ainda sem teste.

## 1. Arquivos salvos

Verificado:
- Ficam no **Steam Cloud**: `~/.local/share/Steam/userdata/<conta>/690790/remote/`. Um arquivo por fantasma, `savegame@ghosts#ENDFX-0..14.*` (21–70 KB), dois pequenos (`#GTSRB.UEL`, `#QKRHMYXE`, índices?) e o perfil (`savegame@profile#NXDSMWW.BWW`, 146 KB, + `profile_backup`). Nada em `Documents/My Games`.
- Todos **cifrados**: entropia ~8 bits/byte, tamanhos múltiplos de 16, e os mesmos 16 primeiros bytes em todos (`64 bb 12 42 3e 54 78 3e 40 78 db 46 bd 90 0d 73`; os `#QKRHMYXE` têm outro bloco, igual entre si) → cifra de bloco com chave/IV fixos ou ECB sobre um cabeçalho igual.
- `bcrypt.dll` é importado só para hash (`BCryptOpenAlgorithmProvider/CreateHash/HashData/FinishHash/...`) e `BCryptGenRandom`: a cifra está no próprio exe (chave possivelmente derivada de hash).
- Formato decifrado: o desserializador `0x1409d10b0` compara o magic **`GHST`** (`0x54534847`) em `0x1409d10e9`.

Hipótese (Gemini): depois do magic, tamanho/CRC (4 bytes), versão (1 byte, ≤ 7) e máscara de canais em varint LEB128; serializador em `0x1409d7f00`.

Em andamento: um segundo despacho do Gemini procura a rotina de cifra e a chave para decifrar as cópias dos saves.

## 2. Sistemas no executável

Strings: `game::GhostLapSystem`, `ghost_lap_recorder` (0x141273588), `ghost_lap_manager` (0x141273570), `GhostData`, `ghost_data`, `ghost_index`, `ghostsys_start`, estados `StateSaveLoadGhost`, `StateTimetrialGhostSelect`, `StateTimeTrialGhostDownload`, `GhostCar.Download`, e na UI `ghost_1..3_visible` / `ghost_1..3_header` (até 3 fantasmas na seleção). O download de fantasmas do ranking é online (bloqueado pelo NetworkGuard).

## 3. Gravação e reprodução

Verificado:
- Amostragem por canais, com períodos em constantes `double`: **rotação a cada 0,125 s (8 Hz)** — `0x1412820a0`, lida em `0x1409d5b1e`; **posição a cada 0,25 s (4 Hz)** — `0x14139ad68`, lida em `0x1409d5bc3`. As duas leituras ficam no gravador `0x1409d59d0`.
- Funções citadas pelo Gemini começam onde ele indicou (via `.pdata`): `0x1409ce4d0`, `0x1409d59d0`, `0x1409cfd30`. Vtable `0x1412802e8` (GhostLapSystem) com slot 0 `0x14051eb10`.

Hipóteses (Gemini; endereços vistos por ele no binário/dump, não testados em jogo):
- Canal de rotação: quaternion em 4 `int8` (÷127), reproduzido com SLERP (`0x14052cd90`, normalização em `0x1409ce9d6`).
- Canal de posição: 3 floats + `uint16` de progresso normalizado (14 bytes no arquivo, 48 em memória), interpolado com **Catmull-Rom** sobre 4 pontos (`0x1409cf1c4`; linear em `0x1409cf5ae`).
- Métricas a 1 Hz; gatilhos em checkpoints/splits.
- Buffers para 30 min: 14 400 amostras a 8 Hz (`slot+0x220`), 7 200 a 4 Hz (`+0x228`), 1 800 a 1 Hz (`+0x238`).
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
4. Com a cifra resolvida: exportar/importar fantasmas (`GHST`) e convertê-los.

## 5. Arquivos de apoio

- `tools/pssg.py`: parser PSSG escrito pelo Gemini (usado no texto rico; ver `ui_tabs.md`).
- `investigations/gemini/ghost-runtime.md`, `investigations/gemini/pssg-ui-text.md`: relatórios brutos.
