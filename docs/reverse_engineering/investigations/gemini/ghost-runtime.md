> Relatório bruto do Gemini 3.8 Flash (agy), 2026-10-02. Não revisado linha a linha; o que foi conferido está em ../../ghosts.md.

# Engenharia Reversa do Sistema de Carro Fantasma (Ghost) — DiRT Rally 2.0

**Data:** 02 de Outubro de 2026  
**Alvo:** `dirtrally2.exe` (ImageBase `0x140000000`, v1.18.x)  
**Dump analisado:** `/home/deivison/Projetos/DR2ModLoader/captures/dumps/20261001-race-paused`

---

## 1. Resumo Executivo e Mapeamento Geral

O sistema de fantasmas do DiRT Rally 2.0 (EGO Engine) opera como um subsistema modular composto por três camadas:
1. **Camada de Orquestração (`game::GhostLapSystem` / `ghost_lap_manager`):** Gerencia o ciclo de vida dos participantes fantasmas, o agendamento de tarefas (`GHOST_LAP_MANAGER`, `GHOST_LAP_RECORDER`, `UPDATE_TEMPORARY`) e mantém uma tabela associativa (`std::map<uint64_t, GhostSlot*>`) mapeando participantes da sessão aos slots de telemetria.
2. **Camada de Gravação (`ghost_lap_recorder` / `GhostSlot`):** Amostra a posição do chassi, orientação (quaternion comprimido), velocidades e dinâmica de rodas com taxas diferenciadas por canal (quaternion a 8 Hz / 125 ms, posição a 4 Hz / 250 ms, métricas secundárias a 1 Hz, e gatilhos de eventos forçados em passagens de setor/checkpoint). Os buffers de memória são pré-alocados para um teto exato de 30 minutos de especial (1800 s).
3. **Camada de Reprodução e Interpolação:** Amostra o tempo decorrido de corrida em alta precisão (`double` segundos). Para cada frame de renderização, realiza **interpolação cúbica Catmull-Rom (Hermite)** sobre 4 amostras de posição e **interpolação esférica (SLERP)** sobre quaternions normalizados. O carro fantasma não possui corpo rígido de física Havok: é uma entidade visual pura (`VehicleVisual` / `VisualModel`) animada com shaders translúcidos dedicados (`ghost_car_depth`, `ghost_car_transparent`).

---

## 2. Respostas Detalhadas às Perguntas

### Pergunta 1: Gravação (Sampling, Frequência, Campos e Tamanhos)

#### [VERIFICADO] Arquitetura do Gravador
- O gravador é instanciado em `0x1404f6f70` com tamanho de **`0x258` bytes** (alinhado para `0x260` no heap) e construtor em [`0x1409cb190`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe).
- O loop de tick por frame da gravação é acionado em [`0x14054a3b2`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe), chamando [`0x140546d90`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe), que invoca o Amostrador Central em [`0x1409d59d0`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe).

#### [VERIFICADO] Frequência de Amostragem Multi-Canal
O jogo utiliza taxas adaptativas baseadas em temporizadores de passo fixo somados ao timestamp da última amostra:
1. **Canal 1 (Orientação / Rotação):**
   - Intervalo padrão: **`0.125 s` (8 Hz / 8 amostras por segundo)**.
   - Constante `double 0.125` em `0x1412820a0` lida em `0x1409d5b1e`.
   - Adicionalmente disparado se a variação angular instantânea do chassi exceder o limiar de tolerância.
2. **Canal 0 (Posição X, Y, Z + Progresso):**
   - Intervalo padrão: **`0.25 s` (4 Hz / 4 amostras por segundo)**.
   - Constante `double 0.25` em `0x14139ad68` lida em `0x1409d5bc3`.
   - Adicionalmente disparado se o deslocamento linear (`subps` + `mulps` em `0x1409d5c14`) exceder o limiar em `0x1412820a8`.
3. **Canal 3 (Telemetria / Escalares de Motor/Velocidade):**
   - Intervalo: **`1.0 s` (1 Hz)** via constante em `0x1411f41c0` lida em `0x1409d5c6b`.
4. **Canal 4 (Cinemática / Forças de Roda):**
   - Intervalo: **`0.25 s` (4 Hz)** lida em `0x1409d5d19`.
5. **Canal 5 (Gatilhos de Evento / Checkpoints / Setores):**
   - Disparo assíncrono imediato com flag `force = 1` ao cruzar a linha de largada, split de setor ou linha de chegada em [`0x14053f200`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe).

#### [VERIFICADO] Campos Gravados e Tamanho das Amostras

| Canal | Descrição dos Campos | Tamanho no Stream (Disco/Rede) | Tamanho no Buffer de Memória | Layout da Amostra em Memória |
|---|---|---|---|---|
| **Canal 0** | Posição (X, Y, Z float) + Progresso normalizado (uint16) | **14 bytes** (`0x0e`) | **48 bytes** (`0x30`) | `+0x00`: vtable / reserva<br>`+0x08`: Timestamp uint32 (ms)<br>`+0x10`: `vec4` X, Y, Z, 0.0 (float32)<br>`+0x20`: Progresso float32 (`uint16 / 65535.0`) |
| **Canal 1** | Orientação (Quaternion normalizado compactado) | **4 bytes** (`0x04`) | **24 bytes** (`0x18`) | `+0x00`: vtable / reserva<br>`+0x08`: Timestamp uint32 (ms)<br>`+0x10`: 4x `int8` compactados `[qx, qy, qz, qw]` |
| **Canal 2** | Marcha / Inputs / Estado de assistência | **4 bytes** (`0x04`) | **24 bytes** (`0x18`) | `+0x08`: Timestamp uint32<br>`+0x10`: uint32 flags / marcha / freio de mão |
| **Canal 3** | Escalar de RPM / Velocidade | **4 bytes** (`0x04`) | **48 bytes** (`0x30`) | `+0x08`: Timestamp uint32<br>`+0x10`: float32 escalar |
| **Canal 4** | Forças longitudinais/laterais das 4 rodas (`longx..w`, `latx..w`) | **8 bytes** (`0x08`) | **48 bytes** (`0x30`) | `+0x08`: Timestamp uint32<br>`+0x10`: 2x `vec4` de forças |
| **Canal 5** | Marcadores de setor, tempo parcial e penalidades | **4 bytes** (`0x04`) | **24 bytes** (`0x18`) | `+0x08`: Timestamp uint32<br>`+0x10`: uint32 tipo de evento / split ID |
| **Canal 6** | Dinâmica avançada de suspensão / esteira | **20 bytes** (`0x14`) | **40 bytes** (`0x28`) | `+0x08`: Timestamp uint32<br>`+0x10`: 16 bytes vetor + 4 bytes escalar |

#### [VERIFICADO] Capacidades Pré-alocadas para 30 Minutos
A alocação em `0x1409cd610` dimensiona os anéis de amostragem exatamente para **1800 segundos (30 min)**:
- Buffer Canal 1 (8 Hz): `1800 * 8 = 14.400` amostras (`0x3840` em `recorder+0x220`).
- Buffer Canal 0 (4 Hz): `1800 * 4 = 7.200` amostras (`0x1c20` em `recorder+0x228`).
- Buffer Canal 3 (1 Hz): `1800 * 1 = 1.800` amostras (`0x708` em `recorder+0x238`).

---

### Pergunta 2: Reprodução (Interpolação, Tempo, Objeto e Relógio)

#### [VERIFICADO] Relógio da Reprodução
- O relógio de reprodução não utiliza distância na spline como base principal: utiliza o **tempo decorrido de corrida em precisão double (segundos)** obtido via conversor QPC em [`0x140929880`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe).
- O timestamp das amostras (`+0x08`) é armazenado em milissegundos inteiros. A cada frame, a rotina [`0x1409ce8b0`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe) localiza o par de keyframes `(t0 <= t_atual <= t1)` e calcula o fator normalizado:
  $$t_{factor} = \frac{t_{atual} - t_0}{t_1 - t_0}$$
  Instruções: `divsd xmm0, xmm6; cvtsd2ss xmm5, xmm0` em `0x1409ce8f7`.

#### [VERIFICADO] Algoritmo de Interpolação
1. **Posição Tridimensional (Catmull-Rom Cúbico):**
   - Função: [`0x1409ced50`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe).
   - Se o índice atual estiver entre $2$ e $N-2$ (verificado em `0x1409cf19e`), o motor carrega 4 amostras consecutivas: $P_{i-2}, P_{i-1}, P_i, P_{i+1}$ (`0x1409cf1c4`).
   - Aplica interpolação cúbica de spline (Catmull-Rom / Hermite) para garantir continuidade $C^1$ suave de aceleração e velocidade.
   - Caso o índice esteja nas pontas ($i < 2$ ou $i > N-2$) ou se a ordem temporal falhar, o código salta para `0x1409cf5ae`, executando LERP linear simples.
2. **Orientação do Chassi (SLERP de Quaternions):**
   - Função: [`0x1409ce690`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe).
   - Os 4 bytes inteiros $q_x, q_y, q_z, q_w$ são desempacotados via `cvtdq2ps` e divididos por `127.0f` (`divps xmm3, xmm4`).
   - Invoca a rotina SLERP do motor em [`0x14052cd90`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe).
   - O quaternion resultante é renormalizado em `0x1409ce9d6` via `sqrtps` e `divps`.
   - Converte o quaternion final em matriz de rotação 3x3 em [`0x1400ba0b0`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe).

#### [VERIFICADO] Objeto do Carro Fantasma
- O fantasma **NÃO é um veículo Havok completo**. Ele não possui malha de colisão com o mundo nem roda simulação dinâmica de suspensão física.
- É uma instância visual de cena (`VehicleVisual` / `VisualModel`).
- No frame update em [`0x14051845e`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe), a matriz resultante da interpolação alimenta diretamente o nó visual através de:
  - [`0x140db9410`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe): Ajusta a matriz de transformação do modelo visual e marca o nó como dirty.
  - [`0x140db90e0`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe): Ajusta o vetor linear de velocidade visual.
  - [`0x140db8930`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe): Ajusta o vetor angular de velocidade visual.
- Os materiais gráficos aplicados ao modelo são os shaders transparentes (`ghost_car_depth` em `0x14139a8a8`, `ghost_car_transparent` em `0x14139a8d0`).

---

### Pergunta 3: Quantidade (Limite de 3 Fantasmas e Slots)

#### [VERIFICADO] Limite de UI vs Limite de Engine
- **O limite de 3 fantasmas é puramente de UI / Máquina de Estados de Menu**, não uma restrição do subsistema de reprodução da engine:
  - Na UI (`StateTimetrialGhostSelect` em `0x140042aa8` e `0x1403744d6`), existem exatamente 3 slots de download/seleção referenciados por strings: `ghost_1_header`..`ghost_3_header` e `ghost_1_visible`..`ghost_3_visible` (além do personal best / local entrant).
  - No núcleo do motor, a classe `GhostLapSystem` mantém um contêiner `std::map<uint64_t, GhostSlot*>` (árvore rubro-negra) em `ghostSys + 0x18`.
- **Comprovação no Dump de Memória:**
  - Na captura analisada, o mapa de slots (`0x7acb4980`) continha **5 nós ativos simultaneamente**:
    1. Slot 0 (`0x150b19a0` -> `0x193cfc070`): Entrante local do jogador (`ghost_local_entrant`), alimentando o `ghost_lap_recorder`.
    2. Slot 1 (`0x150b1ae0` -> `0x193cfc2d0`): Fantasma 1 (Personal Best).
    3. Slot 2 (`0x150b1c20` -> `0x193cfc530`): Fantasma 2.
    4. Slot 3 (`0x150b1d60` -> `0x193cfc790`): Fantasma 3.
    5. Slot 4 (`0x150b1ea0` -> `0x193cfc9f0`): Fantasma 4.
  - A chave de cada nó no mapa é o ponteiro do participante da corrida (`raceSession + 0x1ae8`), espaçados exatamente em `0x140` bytes.

#### [HIPÓTESE / ENGENHARIA] O que é necessário para criar mais fantasmas
Como a engine processa qualquer quantidade de nós presentes em `std::map`, para ultrapassar o limite de 3 fantasmas sem alterar a UI complexa do Flash/Scaleform, basta:
1. Alocar instâncias adicionais de `GhostSlot` (tamanho `0x258`, inicializadas via `0x1409cb190`).
2. Criar entidades visuais adicionais (`VehicleVisual`) instanciadas pelo resource manager do jogo.
3. Inserir os slots no mapa de `GhostLapSystem` ou interceptar a função de atualização [`0x14051845e`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe) para alimentar $N$ modelos visuais adicionais a partir de faixas arbitrárias de memória.

---

### Pergunta 4: Manipulação (Pontos Estáveis e Candidatos a Hook)

#### (a) Ler Posição / Estado do Fantasma por Frame
- **Ponto Ideal:** [`0x1409ce4d0`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe) (`EvaluateGhostState`).
  - **Assinatura provável:**
    ```cpp
    int __fastcall EvaluateGhostState(
        GhostPlaybackTrack* track,      // RCX (rdi + 8)
        const int64_t* pEvalTime,       // RDX (tempo atual em escala interna)
        void* pReserved,                // R8  (contexto auxiliar)
        GhostSampledState* outState     // R9  (estrutura de saída calculada)
    );
    ```
  - **Campos de `GhostSampledState` (tamanho `0x80` bytes):**
    - `+0x00`: Matriz Coluna 0 (16 bytes, `xmm0`)
    - `+0x10`: Matriz Coluna 1 (16 bytes, `xmm1`)
    - `+0x20`: Matriz Coluna 2 (16 bytes, `xmm2`)
    - `+0x30`: **Posição Mundial `(X, Y, Z, 1.0)`** (16 bytes, `xmmword`)
    - `+0x40`: **Velocidade Linear `(Vx, Vy, Vz, 0.0)`** (16 bytes, `xmmword`)
    - `+0x54`: Progresso / Escalar da pista (float32)
    - `+0x58`: Timestamp avaliado
    - `+0x60`: Flag booleano `isValid` (`1` se interpolado com sucesso)
    - `+0x64..0x6c`: Inputs estimados (volante, acelerador, freio)
    - `+0x70`: Cinemática das 4 rodas
  - Alternativa de alto nível: [`0x14051845e`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe), onde `outState` é copiado para o nó visual do veículo fantasma.

#### (b) Injetar Dados de Volta Própria como Fantasma
Existem três abordagens estáveis:
1. **Injeção em Memória no Slot Carregado (Recomendada para mods em tempo real):**
   - Obter o ponteiro `GhostSlot` do fantasma desejado a partir do mapa em `ghostSys + 0x18`.
   - Preencher o array do Canal 0 (`slot + 0x1d0`, stride 48 bytes) com as posições desejadas e timestamps crescentes.
   - Preencher o array do Canal 1 (`slot + 0x1c8`, stride 24 bytes) com os quaternions compactados (`int8 x 4`).
   - Definir contadores `slot + 0x1b0` (número de amostras de posição) e `slot + 0x1ac` (amostras de rotação).
   - Ajustar `slot + 0x1a8 = 2` (marca o slot como pronto para reprodução).
2. **Hook de Deserialização do Arquivo GHST:**
   - Hookar [`0x1409d10b0`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe) (`DeserializeGhostFile`):
     ```cpp
     int __fastcall DeserializeGhostFile(GhostSlot* slot, IInputStream* stream);
     ```
   - O formato binário esperado pelo parser é:
     - Magic de 4 bytes: `"GHST"` (`0x54534847`)
     - Checksum / tamanho de 4 bytes
     - Versão de 1 byte ($\le 7$)
     - Máscara de canais em Varint LEB128 (ex.: `0x03` para Posição + Rotação)
     - Sequência de blocos com delta-time de 1 byte + payload de cada canal.
3. **Hook de Cópia da Volta Concluída:**
   - Hookar [`0x1409cfd30`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe) (`CopyGhostLapData`):
     ```cpp
     void __fastcall CopyGhostLapData(GhostSlot* dest, const GhostSlot* src, bool force);
     ```
   - Esta rotina é chamada quando o jogador completa a especial para copiar a volta gravada para o slot de melhor volta. Substituir o conteúdo de `src` antes da cópia permite gravar qualquer trajetória customizada.

#### (c) Criar Fantasmas Extras
- **Ponto de Hook:** [`0x14051845e`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe) (Função de atualização dos carros fantasmas).
  - Criar um array próprio de $N$ instâncias `GhostSlot` e associar cada uma a um modelo visual instanciado via [`0x140db9410`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe).
  - Executar [`0x1409ce4d0`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe) para cada fantasma extra no mesmo timestamp global e aplicar as matrizes calculadas aos modelos visuais.

---

### Pergunta 5: Objetos Observados no Dump de Memória

No dump `/home/deivison/Projetos/DR2ModLoader/captures/dumps/20261001-race-paused`, os seguintes objetos ativos foram localizados e mapeados:

#### [VERIFICADO] 1. `GhostLapSystem` (Endereço: `0x7be90200`)
- Tamanho: `0xf0` bytes.
- Layout observado:
  - `+0x00`: `0x0000000002c35520` (Ponteiro para Task Context `GHOST_LAP_MANAGER`)
  - `+0x08`: `0x0000000002c33b98` (Ponteiro para Task Context `UPDATE_TEMPORARY`)
  - `+0x18`: `0x000000007acb4980` (Raiz do `std::map<uint64_t, GhostSlot*>`)
  - `+0x20`: `0x0000000000000005` (Tamanho do mapa = 5 slots ativos)
  - `+0x30`: `0x0000000000000001` (Flag de sistema ativo / habilitado)
  - `+0x38`: `0x000000006c0350e0` (`std::shared_ptr` -> instância do `ghost_lap_recorder`)
  - `+0x40`: `0x0000000002a24200` (Control block do shared_ptr do gravador)
  - `+0x48`: `0x000000006c1a2e60` (`std::shared_ptr` secundário)
  - `+0x90`: `0x0000000037a00f18` (Ponteiro para objeto do veículo do jogador)
  - `+0xc0`: `0x00000001412802e8` (Vtable de `ITaskUpdateable` do GhostLapSystem)
  - `+0xd8`: `0x000000007be90200` (Back-pointer para si mesmo)
  - `+0xe0`: `0x00000001406bfe70` (Callback de atualização)
  - `+0xe8`: `0x000000007be902c0` (Ponteiro para a sub-interface `ITaskUpdateable` em `+0xc0`)

#### [VERIFICADO] 2. `GhostLapRecorder` (Endereço: `0x6c0350e0`)
- Tamanho: `0x258` bytes (bloco alocado de `0x260` bytes).
- Vtable em `+0x0a8`: [`0x1414277b0`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe).
- Vtable/metadados em `+0x0d0`: `0x141273fa0`.
- Ponteiros para buffers de canais alocados:
  - `+0x1c8`: `0x000000006c035340` (Buffer Canal 0 — Posições)
  - `+0x1d0`: `0x000000006c089970` (Buffer Canal 1 — Rotações)
  - `+0x1d8`: `0x000000006c0ddfa0` (Buffer Canal 2 — Marcha/Inputs)
  - `+0x1e0`: `0x000000006c1325d0` (Buffer Canal 3 — Escalares)
  - `+0x1e8`: `0x000000006c13cec0` (Buffer Canal 4 — Cinemática de Rodas)
  - `+0x218`: `0x000000006c1914f0` (Buffer Canal 6 — Dinâmica de Rodas)
- Capacidades de amostragem (30 minutos = 1800 s):
  - `+0x220`: `14.400` amostras (`0x3840`)
  - `+0x228`: `7.200` amostras (`0x1c20`)
  - `+0x230`: `14.400` amostras (`0x3840`)
  - `+0x238`: `1.800` amostras (`0x708`)
  - `+0x240`: `7.200` amostras (`0x1c20`)
  - `+0x248`: `1.800` amostras (`0x708`)

#### [VERIFICADO] 3. Tabela de Slots de Fantasma (`std::map` em `0x7acb4980`)
A árvore rubro-negra contém 5 nós mapeados para instâncias de `GhostSlot`:
- Nó `0x7acc49a0`: Chave Participante `0x150b19a0` $\rightarrow$ Slot `0x193cfc070`
- Nó `0x7acc4a00`: Chave Participante `0x150b1ae0` $\rightarrow$ Slot `0x193cfc2d0`
- Nó `0x7acc4a60`: Chave Participante `0x150b1c20` $\rightarrow$ Slot `0x193cfc530`
- Nó `0x7acc4ac0`: Chave Participante `0x150b1d60` $\rightarrow$ Slot `0x193cfc790`
- Nó `0x7acc4b20`: Chave Participante `0x150b1ea0` $\rightarrow$ Slot `0x193cfc9f0`

---

## 3. Tabela Resumo de Funções e Hooks Candidatos

| Endereço (VA) | Função / Propósito | Assinatura Provável | Uso Prático / Modding |
|---|---|---|---|
| [`0x1409ce4d0`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe) | `EvaluateGhostState` | `int __fastcall(GhostSlot* slot, const int64_t* pTime, void*, GhostSampledState* outState)` | **Ponto primário para ler posição, velocidade, rotação e telemetria do fantasma por frame.** |
| [`0x14051845e`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe) | `UpdateGhostVisualPlayback` | `void __fastcall(GhostPlaybackContext* ctx, float dt, ...)` | **Ponto para orquestrar múltiplos fantasmas extras** e atualizar nós de renderização visual. |
| [`0x1409d59d0`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe) | `RecordVehicleSample` | `int __fastcall(GhostRecorder* rec, VehicleRig* rig, float scalar, uint32_t flags, bool force)` | **Interceptar gravação da volta em tempo real** ou alterar regras de compressão de telemetria. |
| [`0x1409cfd30`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe) | `CopyGhostLapData` | `void __fastcall(GhostSlot* dest, const GhostSlot* src, bool force)` | **Injetar telemetria pré-gravada no momento de conclusão da volta** sem precisar tocar em arquivos salvos. |
| [`0x1409d10b0`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe) | `DeserializeGhostFile` | `int __fastcall(GhostSlot* slot, IInputStream* stream)` | **Carregar arquivos de fantasma customizados em formato `.ghst`** diretamente no carregamento da pista. |
| [`0x1409d7f00`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe) | `SerializeGhostFile` | `int __fastcall(const GhostSlot* slot, IOutputStream* stream)` | **Exportar o arquivo de telemetria cru** em formato aberto antes de ser enviado para a nuvem Steam cifrada. |
| [`0x140db9410`](file:///tmp/claude-1000/-home-deivison-Projetos-DR2ModLoader/1d23cba1-bca7-49d6-9918-a48ea9bb4def/scratchpad/ghost_runtime/dirtrally2.exe) | `SetVisualTransform` | `void __fastcall(VisualModel* model, const Matrix4x4* mat)` | **Ajustar diretamente o posicionamento de qualquer modelo 3D visual de carro na cena.** |

---

## 4. Scripts e Metodologia Utilizados

Durante a pesquisa foram desenvolvidas e executadas rotinas em Python utilizando `dr2re_helper.py`, `pefile`, `capstone` e análise binária de dumps:
1. `dr2re_helper.py`: Desmontagem e varredura de referências relativas RIP no segmento `.text`.
2. Busca binária otimizada em memória do `.text` para instruções com campos de deslocamento (`0x2948`, `0x1ac`, `0x1b0`, `0x1c8`, `0x2d0`, `0x2e0`).
3. Mapeador de regiões e leitor direto dos arquivos `.bin` de `/home/deivison/Projetos/DR2ModLoader/captures/dumps/20261001-race-paused` para recuperação das estruturas vivas de `GhostLapSystem`, `GhostLapRecorder` e `std::map`.
