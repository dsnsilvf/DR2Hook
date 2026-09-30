# Engine

## Overview

Campos estáticos e o escalar dinâmico de rotação do virabrequim no `PhysicsRig`. A UI do mod lê o conta-giros e o corte daqui. O layout do rig está em [PhysicsRig](physics_rig.md).

## Memory Layout

Offsets relativos ao `PhysicsRig`.

| Offset | Tipo | Descrição | Confiança |
| :--- | :--- | :--- | :--- |
| `+0x13d8` | `float` (4 B) | Velocidade angular do virabrequim que o conta-giros usa, em rad/s. | Declarado como campo mapeado; a UI lê este float |
| `+0x8e8` | `float` (4 B) | Especificação estática da marcha lenta, em RPM. Exemplo: `1080.0f`. | Declarado como campo mapeado |
| `+0x918` | `float` (4 B) | Rotação de potência máxima, em RPM. Exemplo: `5500.0f`. Não é o corte de giro. Não entra no conta-giros. | Declarado como campo mapeado |
| `+0x140c` | `float` (4 B) | Corte de giro, em rad/s. No Golf GTI 16v é `785.398` rad/s, exatamente `7500` RPM. O corte desenhado na barra é este campo, na mesma unidade de `+0x13d8`. | Declarado como campo mapeado |

No corte do Golf GTI 16v, `+0x13d8` fica logo abaixo de `785.4` rad/s (`7500` RPM).

## Mathematics

```text
RPM = valor_em_rad/s × 60 / (2π)
```

A conversão vale para `+0x13d8` e para ler `+0x140c` como RPM. `+0x8e8` e `+0x918` já estão em RPM.

## Confirmed

- O conta-giros do mod é `+0x13d8`, convertido com `× 60 / (2π)`.
- O corte desenhado na barra é `+0x140c`.
- `+0x918` é a rotação de potência máxima e não é o corte.

A quantidade de marchas e a marcha engatada estão em [Gearbox](gearbox.md).

## Probable / Hypotheses

A anotação externa de telemetria que trata o offset UDP `148` como rpm/10 não é este campo. O parser Rust guarda o float cru em `rpms`. Essa divergência está em [Telemetry](telemetry.md) e não relocaliza `+0x13d8`.

## Unknown

Não há, neste material, mapa de torque, acelerador, curva de potência além do escalar `+0x918`, nem o escritor de `+0x13d8`.

## Related Systems

- [PhysicsRig](physics_rig.md)
- [Gearbox](gearbox.md)
- [Telemetry](telemetry.md)
