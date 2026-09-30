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
| `rig + 0x4c0` | qword | Começa em zero na inicialização. Depois recebe a qword de `[owner + 0xe8]`. No rig `0x4b76bab0` essa qword é `0xaa9a5c850716b59f`. Sem nome. | Valor observado; nome ausente |
| `rig + 0x4d0` | bloco | Destino de `0x140733e90`. | Destino da chamada, como lido no código |
| `rig + 0x5a0` | `Vector3` + padding | Ponto esquerdo do eixo dianteiro, no referencial do chassi. | Cópia bit a bit no rig `0x4b76bab0` |
| `rig + 0x5b0` | `Vector3` + padding | Ponto esquerdo do eixo traseiro. | Cópia bit a bit no rig `0x4b76bab0` |
| `rig + 0x5d8` | float | Escalar vertical copiado para `+0x38` das dianteiras. | Origem da cópia, como lida no código |
| `rig + 0x5ec` | float | Escalar vertical copiado para `+0x38` das traseiras. | Origem da cópia, como lida no código |
| `rig + 0x1120` | ponteiro | Gravado só em `0x140746b5f`, com o quarto argumento da inicialização do rig (`0x1407469fa`). | Único store citado |
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

## Confirmed

- Os quatro floats de geometria não têm store próprio: entram pelo `memcpy` do buffer decodificado de `CarType::buildFromBinary`.
- `L` no rig `0x4b76bab0` é cópia bit a bit do ponto esquerdo, com X negado à direita, e o quarto float em zero.
- `+0x2514` e `+0x2518` receberam `1,424` e `2,473`.
- `c` em uso foi `0,301` nos dois eixos nesse rig, e `0,194` no rig em que o byte valia `2`.

## Unknown

- Nome dos floats dentro do buffer.
- Nome da qword em `+0x4c0` (`0xaa9a5c850716b59f` no rig observado).
- O que são os seis vetores em `+0x1130`, além de serem a fonte `S` quando `+0x1190` vale 1.

## Related Systems

- [Suspension](suspension.md) — `v = (L.x, L.y - c, L.z)` e o termo `[+0x1504] * Up`
- [Tyres](tyres.md) — o byte `2` troca `+0x38` por `+0x3c`
- [Vehicle Transform](vehicle_transform.md) — quatérnion que gira `L`
- [Wheels](wheels.md) — retângulo medido em `+0x270`
- [PhysicsRig](physics_rig.md)
