# Vehicle Physics

## Overview

Velocidade linear e velocidade angular no `PhysicsRig`, e os blocos que uma rotina copia para esses dois campos. A posição e a orientação estão em [Vehicle Transform](vehicle_transform.md). A conta por roda que consome os símbolos `T` e `V` está em [Wheels](wheels.md).

## Memory Layout

| Offset | Tipo | Descrição | Confiança |
| :--- | :--- | :--- | :--- |
| `+0x320` | `Vector3` (SIMD 16 B) | Velocidade linear \(\vec{v} = (v_x, v_y, v_z, 0)\) em m/s. | Campo já conhecido; destino da cópia abaixo |
| `+0x330` | `Vector3` (SIMD 16 B) | Velocidade angular \(\vec{\omega} = (\omega_x, \omega_y, \omega_z, 0)\) em rad/s. | Campo já conhecido; destino da cópia abaixo |
| `+0x180` | vetor | Origem da cópia que enche `+0x2c0`. Também é gravado em `+0x210` depois de uma chamada virtual. | CONFIRMED (cópia) / UNKNOWN (nome) |
| `+0x210` | vetor | Destino adicional da mesma origem `+0x180`. Não é o `+0x10` de cada roda. | CONFIRMED (cópia) / UNKNOWN (nome) |
| `+0x2b0` | 16 bytes iniciais, símbolo `T0` / `T` | Lido pela conta das rodas. Copiado para `+0x320` por `0x140746150`. Store direto do campo não achado. | INCONCLUSIVE |
| `+0x2c0` | símbolo `V` | Copiado de `+0x180`. A conta das rodas o usa como `V`. | CONFIRMED (cópia) / UNKNOWN (nome) |
| `+0x370` | float | Ao vivo, `1083,53`. Entra na conta das rodas como `s = [rig + 0x370] * 0,0001`. | Valor medido; nome físico não dado |

A cópia de `0x140746150` não diz que `T` e `V` sejam as velocidades: ela lê o bloco em `rig + 0x2b0` e escreve os campos de velocidade. Não grava de volta `[bloco]` nem `[bloco + 0x10]`.

## Functions

| Endereço | Função | Relevância |
| :--- | :--- | :--- |
| `0x140746700` | `movups [rcx + 0x10], [rdx]`, com `rcx = rig + 0x2b0` e `rdx = rig + 0x180` | Copia `+0x180` para `+0x2c0`. |
| chamada virtual `[vtable + 0x30]` | No mesmo trecho em que `+0x180` também vai para `rig + 0x210` | Destino virtual não nomeado. |
| `0x140746150` | Bloco em `rig + 0x2b0` | Copia o primeiro vetor do bloco para `rig + 0x320` e o segundo para `rig + 0x330`. |
| `0x14074a368`, `0x14074a7ba`, `0x14074ab97` | Store em `rig + 0x210` | Um vetor só no rig, não o `+0x10` de cada roda. |
| `0x14073e266` / `0x14073e3ed` | Snapshot e restauração | A gravação em `0x14073e3ed` restaura o snapshot tirado em `0x14073e266`. |

A constante `0,0001` usada com `+0x370` está em `0x141227fc0`. Na conta das rodas, `s` valeu `0,10835`.

## Data Flow

```text
rig + 0x180
    │
    ├── 0x140746700 ──► rig + 0x2c0   (V)
    └── chamada [vtable + 0x30] ──► rig + 0x210

rig + 0x2b0   (T; store direto não achado)
    │
    └── 0x140746150
            ├── primeiro vetor ──► rig + 0x320   velocidade linear
            └── segundo vetor  ──► rig + 0x330   velocidade angular
```

`0x140746150` não escreve de volta no bloco.

Na conta das rodas, `T` e `V` entram em `W = T - cross(P1 - P2, V)`. O produto `dot(Up, T + V × P)` é gravado em `+0x16d0` pelo leitor descrito em [Suspension](suspension.md). As duas fórmulas usam estes campos; o nome físico de `T` e de `V` continua o da tabela acima.

## Confirmed

- `+0x320` e `+0x330` são os campos já conhecidos de velocidade linear e angular, e são o destino da cópia em `0x140746150`.
- `V` em `+0x2c0` é cópia de `+0x180`. A mesma origem também vai para `+0x210`.
- Essa cópia não identifica `T` nem `V` com as velocidades.

## Probable / Hypotheses

Nenhuma. O material deixa `T` em `INCONCLUSIVE` e o nome de `V` em `UNKNOWN`.

## Unknown

- Store direto de `rig + 0x2b0`.
- Nome físico de `T`, de `V`, de `+0x180` e de `+0x210`.
- O que a chamada `[vtable + 0x30]` representa.

## Related Systems

- [Vehicle Transform](vehicle_transform.md)
- [Wheels](wheels.md) — equação que consome `T` e `V`
- [Suspension](suspension.md) — `dot(Up, T + V × P)` em `+0x16d0`
- [PhysicsRig](physics_rig.md)
