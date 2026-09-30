# Gearbox

## Overview

Quantidade de marchas à frente e marcha engatada no `PhysicsRig`. A UI do mod mostra a marcha a partir de `+0x1448`. O layout do rig está em [PhysicsRig](physics_rig.md).

## Memory Layout

| Offset | Tipo | Descrição | Confiança |
| :--- | :--- | :--- | :--- |
| `+0x8f4` | `float` (4 B) | Quantidade total de marchas à frente. Exemplo: `5.0f`. | Declarado como campo mapeado |
| `+0x1400` | `int32` (4 B) | Número de marchas à frente usado pelo câmbio. Exemplo: `5`. | Declarado como campo mapeado |
| `+0x1448` | `int32` (4 B) | Marcha engatada. `0` = neutro, `1..n` = marchas à frente, `10` = ré. A UI mostra `10` como `R`. | Declarado como campo mapeado; a UI lê este `int32` |

`+0x8f4` e `+0x1400` são dois campos. O material não diz que um seja a conversão do outro.

## Confirmed

A marcha na UI é o `int32` em `+0x1448` (`0` neutro, `1..n` à frente, `10` ré, mostrado como `R`).

A coluna do DiRT Rally na planilha `CodeMasters.html` chama o offset UDP `132` de marcha com `0` neutro e `10` ré, o que coincide com este `int32`. O tratamento diferente no parser Rust está em [Telemetry](telemetry.md).

## Unknown

Não há, neste material, relações de marcha, embreagem, nem o escritor de `+0x1448`. As sessões de roda citadas em [Wheels](wheels.md) foram feitas com marcha `0`.

## Related Systems

- [Engine](engine.md) — rotação do virabrequim, campo vizinho e distinto
- [Telemetry](telemetry.md) — offset UDP `132` e `Gear::from_f32`
- [PhysicsRig](physics_rig.md)
