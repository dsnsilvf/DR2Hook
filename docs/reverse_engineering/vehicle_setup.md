# Vehicle Setup

## Overview

Bloco de setup dentro do `PhysicsRig`, a partir de `rig + 0x4d0`, decodificado de um buffer binário do tipo de carro. Deste bloco saem o ponto esquerdo de cada eixo e um escalar vertical por eixo. A cópia para o objeto da roda e o uso no curso estão ligados aqui; o integrador do curso está em [Suspension](suspension.md).

## Known Structures

O bloco começa em `rig + 0x4d0`. Dois vetores de 16 bytes:

```text
dianteiro esquerdo = rig + 0x5a0
traseiro esquerdo  = rig + 0x5b0
```

Em relação ao bloco, `+0x5a0`, `+0x5b0`, `+0x5d8` e `+0x5ec` são `+0xd0`, `+0xe0`, `+0x108` e `+0x11c`. Não há nome de campo no executável para esses floats.

O RTTI da vtable do streambuf é o `membuf` local de:

```text
vehicleDynamics::CarType::buildFromBinary(
    VehicleDataBuffer const&,
    ModificationSpec const&,
    ITuningModuleInterface&,
    IAllocator&)
```

## Memory Layout

| Offset | Tipo | Descrição | Confiança |
| :--- | :--- | :--- | :--- |
| `rig + 0x4c0` | qword | Começa em zero na inicialização. Depois recebe a qword de `[owner + 0xe8]`, o carimbo do acerto (`TuningSetup + 0xe8`, ver [Acerto do carro](#acerto-do-carro-tuningsetup)). No rig `0x4b76bab0` essa qword é `0xaa9a5c850716b59f`. | Comparação em `0x140749c80`; valor observado |
| `rig + 0x4d0` | bloco | Destino de `0x140733e90`. | Destino da chamada, como lido no código |
| `rig + 0x5a0` | `Vector3` + padding | Ponto esquerdo do eixo dianteiro, no referencial do chassi. | Cópia bit a bit no rig `0x4b76bab0` |
| `rig + 0x5b0` | `Vector3` + padding | Ponto esquerdo do eixo traseiro. | Cópia bit a bit no rig `0x4b76bab0` |
| `rig + 0x5d8` | float | Escalar vertical copiado para `+0x38` das dianteiras. | Origem da cópia, como lida no código |
| `rig + 0x5ec` | float | Escalar vertical copiado para `+0x38` das traseiras. | Origem da cópia, como lida no código |
| `rig + 0x1120` | ponteiro | Gravado só em `0x140746b5f`, com o quarto argumento da inicialização do rig (`0x1407469fa`): o `TuningSetup` do veículo (chamador `0x140730f61`). | Único store citado |
| `rig + 0x1130` | seis vetores | Destino de `0x14074f4a0`. Usado quando `[rig + 0x1190]` vale 1. | Cópia descrita no código |
| `rig + 0x1190` | byte | Ligado por `0x14074f4a0`. | Byte que essa função liga |
| `rig + 0x2514` | float | `\|FR.x - FL.x\|`. Observado `1,424`. | Store dessa expressão; valor observado |
| `rig + 0x2518` | float | `0,5 * \|(FL.z + FR.z) - (RL.z + RR.z)\|`. Observado `2,473`. | Store dessa expressão; valor observado |

No objeto da roda (`rig + i·0x420`, base `0x1480`):

| Offset no objeto / no rig (`i = 0`) | Descrição | Confiança |
| :--- | :--- | :--- |
| `+0x1480` (`L`) | Três floats. Ponto do eixo no referencial do chassi. | Cópia bit a bit; quarto float em zero |
| objeto `+0x38` | Recebe `+0x5d8` (dianteiras) ou `+0x5ec` (traseiras). | Caminho de `0x1407301d0` |
| objeto `+0x3c` | Usado no lugar de `+0x38` quando o byte de [Tyres](tyres.md) vale `2`. | Ramo de `0x140730634` |
| `+0x14e8` (`c`), objeto `+0x68` | Cópia de `+0x38`, ou de `+0x3c` no ramo do byte `2`. | Caminho de `0x140730650` |
| `+0x1490` | Vetor diferença contra o ponto esquerdo do eixo. | A conta reproduz o valor em memória no rig observado |

O quarto float de `L` fica em zero. No rig `0x4b76bab0` a cópia de `L` bate bit a bit. `L` forma o retângulo `x = ±0,712`, `z = 0,935` nas dianteiras e `-1,538` nas traseiras. `c` valeu `0,301` nas quatro. No rig `0x4b29bab0`, com o byte em `2`, o escalar em uso passou a `0,194`.

O referencial é o do chassi: X lateral, Y vertical, Z longitudinal. Ver [Vehicle Transform](vehicle_transform.md).

## Functions

| Endereço | Função | Relevância |
| :--- | :--- | :--- |
| `0x140749c80` | Chama `0x140734e60` quando `[rig + 0x4c0]` difere de `[[rig + 0x1120] + 0xe8]` | Dispara a recarga do setup. Também chama `0x140750f20` sob a mesma diferença. |
| `0x140734e60` | Salta para `0x140734840` | Encaminha a decodificação. |
| `0x140734840` | Chama `0x140733e90` | Destino `rig + 0x4d0`, origem `[[[rig + 0x1120]] + 0x18]`. |
| `0x140733e90` | Não grava `+0x5a0`, `+0x5b0`, `+0x5d8` nem `+0x5ec` com store próprio | Esses offsets caem dentro de um `memcpy`. |
| `0x140ec9960` | `memcpy` do buffer decodificado | Origem dos quatro floats. |
| `0x140733f67` | Constrói o streambuf | Vtable `0x1412afd70`. A mesma vtable aparece em outros vinte pontos. |
| `0x140750f20` | Monta um vetor por roda | Chamado de `0x140749c80`. |
| `0x1407308e0` | Grava 16 bytes em `rig + i·0x420 + 0x1480` | Store de `L`. |
| `0x1407301d0` | Copia o escalar vertical do setup para objeto `+0x38` | Dianteiro e traseiro, origens distintas. |
| `0x140730650` | Copia objeto `+0x38` para objeto `+0x68` (`+0x14e8`) | Caminho em que o byte não é `2`. |
| `0x140730634` | Usa objeto `+0x3c` | Quando o byte da seção de pneu vale `2`. |
| `0x14074ee50` | Grava o vetor de `+0x1490` | Conta abaixo. |
| `0x14074f4a0` | Copia seis vetores para `rig + 0x1130` e liga `+0x1190` | Fonte alternativa de `S`. |
| `0x1411f7d10` | Constante `-1` | X negado na roda direita. |

O construtor `0x14072fd20` zera os primeiros `0x40` bytes de cada objeto de `0x420`, inclusive `+0x1480`. O valor geométrico é escrito depois.

## Data Flow

```text
[[[rig + 0x1120]] + 0x18]
        │
        ▼
0x140733e90 / memcpy 0x140ec9960
        │
        ▼
rig + 0x4d0
        ├── +0x5a0  dianteiro esquerdo
        ├── +0x5b0  traseiro esquerdo
        ├── +0x5d8  escalar vertical dianteiro
        └── +0x5ec  escalar vertical traseiro
                │
                ▼
        0x140750f20 / 0x1407308e0
                │
                ▼
        L em rig + i·0x420 + 0x1480
```

Montagem de `L`, com a ordem RL, RR, FL, FR fechada em [Suspension](suspension.md):

```text
i = 0 (RL)  copia rig + 0x5b0
i = 1 (RR)  ( -[rig + 0x5b0], [rig + 0x5b4], [rig + 0x5b8] )
i = 2 (FL)  copia rig + 0x5a0
i = 3 (FR)  ( -[rig + 0x5a0], [rig + 0x5a4], [rig + 0x5a8] )
```

`L` é o ponto esquerdo do eixo, no referencial do chassi, com o X negado na roda direita. `c` é o escalar vertical do setup subtraído de `L.y`. O curso continua em `+0x1504`.

Para `+0x1490`, `S` é o bloco em `rdx`: `rig + 0x1130` quando `[rig + 0x1190]` vale 1, senão `rig + 0x5a0`.

```text
RL = S + 0x10 - (rig + 0x5b0)
RR = essa diferença, com o X multiplicado por -1
FL = S + 0x00 - (rig + 0x5a0)
FR = essa diferença, com o X multiplicado por -1
```

No rig observado a conta reproduz o valor em memória.

## Mathematics

Bitola e entre-eixos gravados depois de ler os quatro vetores:

```text
[rig + 0x2514] = |FR.x - FL.x|                         → 1,424
[rig + 0x2518] = 0,5 * |(FL.z + FR.z) - (RL.z + RR.z)| → 2,473
```

A medição anterior do retângulo de `+0x270`, com bitola `1,42 m` e distância entre eixos `2,472 m`, está em [Wheels](wheels.md). São números próximos, de campos diferentes.

## Acerto do carro (TuningSetup)

O acerto que o jogador muda na oficina (freios, diferencial, câmbio, suspensão, amortecedores) vive num `TuningSetup` de `0xf0` bytes por veículo. A definição do carro (`TuningDefinition`, `D`) diz faixa, passos e padrão de cada um dos 51 parâmetros. O AutoStage grava valores próprios nele na carga do benchmark; o editor (F5 > aba Acerto) monta esses valores.

### Os 51 parâmetros

Índice `p`, na ordem do jogo (os nomes são as chaves `tuning_<nome>` do `dr2hook_autostage.ini`):

| `p` | Nomes |
| :--- | :--- |
| 0–2 | `brakes_bias`, `brakes_strength`, `brakes_rear_saturation` |
| 3–15 | `differential_{front,centre,rear}_{driving_lock,braking_lock,preload,viscosity}`, com `differential_centre_torque_distribution` depois da viscosidade central |
| 16–22 | `gearbox_1` … `gearbox_7` |
| 23 | `gearbox_final_drive` |
| 24–35 | `suspension_{front,rear}_{ride_height,spring_rate,tender_spring_rate,anti_roll,camber,toe}` |
| 36–47 | `damping_{front,rear}_{bump,fast_bump,fast_bump_threshold,rebound,fast_rebound,fast_rebound_threshold}` |
| 48 | `spare_tyre_count` |
| 49–50 | `suspension_front_air_pressure`, `suspension_rear_air_pressure` |

A lista exata está em `kTuningNames` (`src/core/auto_stage.cpp`).

### Layout

`TuningDefinition` (`D`):

| Offset | Tipo | Descrição |
| :--- | :--- | :--- |
| `D + 0x18` | `0x2b8` bytes | Dados do veículo, cifrados no `.nefs`. Por isso as faixas não saem offline. |
| `D + 0xc40` | — | Semente do carimbo. |
| `D + 0xc90 + p·16` | `{float, u32, u32, u32}` | Padrão (float puro), mínimo, máximo e passos (os três cifrados). |
| `D + 0xff4 + p·4` | `int32` | Estado: `1` ajustável, `0` o carro não tem, `2` fixo. |
| `D + 0x10c8` | qword | Usada em `T + 0xe0` quando o veículo não traz a sua. |

Decodificação (aritmética de 32 bits):

```text
min   = bits(min)   · 0xccd5aa19   (reinterpretado como float)
max   = bits(max)   · 0xa069a253   (reinterpretado como float)
steps = enc(steps)  · inverso(0x993908c7) mod 2^32
valor do passo k = min + k · (max − min) / (steps − 1)
```

Conferido: no `mcs`, `brakes_bias` vai de 0,15 a 0,9 em 11 passos, e o padrão 0,75 cai no passo 8. `suspension_front_ride_height` vai de −0,06 a −0,02 em 21 passos, padrão −0,04. `gearbox_1` vai de 0,318 a 1,25 em 201 passos, padrão 0,3646 (passo 10).

`TuningSetup` (`T`, `0xf0` bytes):

| Offset | Tipo | Descrição |
| :--- | :--- | :--- |
| `T + 0x0` | ponteiro | `D` |
| `T + 0x8`, `T + 0xc` | float | Gravados por `0x140734fe0` (do veículo `+0x278`/`+0x27c`). |
| `T + 0x10 + p·4` | float | Os 51 valores. |
| `T + 0xe0` | qword | Do veículo `+0x280`, ou `D + 0x10c8` quando `+0x280` vale o sentinela `[0x1416bc2c0]` (zero). |
| `T + 0xe8` | qword | Carimbo. |

### Carga

`0x14098c830` prepara o acerto de cada objeto de veículo `V`:

1. `V + 0x131` é o id do carro (`"mcs"`), e `T = V + 0x188`.
2. Sem `D`: `0x140734ca0` cria a definição do carro, `0x140735000` grava `D` em `T`, volta aos padrões (`0x140734f60`) e carimba.
3. Grava `T + 0x8/0xc` e `T + 0xe0` (ver acima).
4. `0x140734e70` limita cada valor de estado `1` a `[min, max]` (NaN vira o padrão). Única chamada: `0x14098cc06`.
5. `0x140735040` recalcula o carimbo: hash ×`0x21` sobre os bytes `T + 0x10 … 0x10 + 0x22·6`, semeado por `D + 0xc40`, depois `T + 0xe0..0xe7` e `T + 0x8..0xf`. `0x140734fe0` grava `T + 0x8/0xc` e cai em `0x140735040`.

A física guarda `T` em `rig + 0x1120` (construtor `0x1407469f0`) e, em `0x140749c80`, compara `rig + 0x4c0` com `T + 0xe8`. Quando o carimbo muda, recarrega o acerto.

`0x14098c830` roda depois do preenchimento da tela de carregamento (`0x14022ded0`).

### Hook do AutoStage

A dxgi liga `0x140734e70` (prólogo `48 89 5c 24 18 55 56 57 41 56 41 57`). Depois do original, se o carro do acerto é o do AutoStage (`V + 0x131` igual ao `vehicle`, e ao `tuning_car` se houver):

- cada valor do ini com estado `1` é limitado à faixa e gravado em `T + 0x10 + p·4`;
- estado `0` ou `2`: o valor é ignorado e contado;
- o carimbo sai certo porque `0x140735040` roda em seguida.

O relatório vai para o log no primeiro `RaceEvent` (ainda não há evento antes de `0x14098c830`):

```text
AutoStage: acerto do mcs: 4 valor(es) do ini aplicado(s), 1 limitado(s) a faixa, 1 que o carro nao ajusta, 1 nome(s) desconhecido(s); faixas em dr2hook_tuning/mcs.json.
```

A dxgi grava `<jogo>/dr2hook_tuning/<carro>.json` (só quando muda) com nome, estado, padrão, mínimo, máximo e passos de cada parâmetro. O `ring_deploy.py` copia para `build/viewer3d/cars/tuning/`, de onde o editor tira os controles.

Formato no ini:

```ini
tuning_car = mcs
tuning_brakes_bias = 0.6
tuning_suspension_front_ride_height = -0.03
```

Conferido em memória: `brakes_bias` 0,6, altura dianteira −0,03, `gearbox_1` 0,4, cambagem traseira −2 limitada a −1,5, `suspension_front_anti_roll` (estado 0 no `mcs`) ignorado. O carimbo da física bateu.

Limites:

- as faixas só existem depois que o carro carregou uma vez no jogo (os dados são cifrados);
- depois do AutoStage, o acerto armado vale para a sessão: uma carga posterior do mesmo carro, na carreira, também o recebe.

## Confirmed

- Os quatro floats de geometria não têm store próprio: entram pelo `memcpy` do buffer decodificado de `CarType::buildFromBinary`.
- `L` no rig `0x4b76bab0` é cópia bit a bit do ponto esquerdo, com X negado à direita, e o quarto float em zero.
- `+0x2514` e `+0x2518` receberam `1,424` e `2,473`.
- `c` em uso foi `0,301` nos dois eixos nesse rig, e `0,194` no rig em que o byte valia `2`.
- Acerto: faixas decodificadas de `D + 0xc90`, valores gravados em `T + 0x10` depois de `0x140734e70` e carimbo recalculado; a física recarregou com eles (`mcs`, 2026-10-12).

## Unknown

- Nome dos floats dentro do buffer.
- Unidades exatas de `brakes_strength`, das relações do câmbio e de `suspension_*_air_pressure`; o editor mostra o número do jogo.
- O estado `2` (fixo) não apareceu nos dois carros lidos (`mcs`, `fr5`).
- O que são os seis vetores em `+0x1130`, além de serem a fonte `S` quando `+0x1190` vale 1.

## Related Systems

- [Suspension](suspension.md) — `v = (L.x, L.y - c, L.z)` e o termo `[+0x1504] * Up`
- [Tyres](tyres.md) — o byte `2` troca `+0x38` por `+0x3c`
- [Vehicle Transform](vehicle_transform.md) — quatérnion que gira `L`
- [Wheels](wheels.md) — retângulo medido em `+0x270`
- [PhysicsRig](physics_rig.md)
