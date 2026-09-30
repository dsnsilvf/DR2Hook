# Damage

## Overview

O que o material isola como dano são nomes de canal numa tabela de definição no heap, mais offsets do bloco da roda que foram testados na sessão do carro danificado e não acompanharam o dano. O byte que mudou com o aro e com a roda solta é o estado do pneu e da roda, documentado em [Tyres](tyres.md). Este arquivo não trata esse byte como um desses canais: a ligação não foi feita.

A sessão de comparação usou o `PhysicsRig` `0x4b1abab0`, carro parado, marcha `0`, virabrequim perto de `900` RPM, um estado praticamente intacto e outro com a dianteira esquerda visualmente torta. Esterçamento, aceleração, frenagem, irregularidade e suspensão em movimento não foram medidos nessa comparação.

## Known Structures

Tabela de definição no heap. O índice é `0` RL, `1` RR, `2` FL, `3` FR. É a mesma ordem da telemetria UDP e é externa ao `PhysicsRig`. Essa coincidência de ordem não liga os blocos `+0x1680`, `+0x1aa0`, `+0x1ec0` e `+0x22e0` a esses cantos. A ligação dos blocos foi a captura em [Suspension](suspension.md).

## Memory Layout

| Canal | Índice | Float do registro, igual nas quatro rodas | Confiança |
| :--- | :--- | :--- | :--- |
| `wheel_misalign_angle_damage_*` | `0` RL, `1` RR, `2` FL, `3` FR | `0.10` | Definição CONFIRMADA; valor ao vivo UNKNOWN |
| `wheel_rotational_resistance_damage_*` | mesma ordem | `0.05` | Definição CONFIRMADA; valor ao vivo UNKNOWN |
| `wheel_wobble_angle_damage_*` | mesma ordem | `0.05` | Definição CONFIRMADA; valor ao vivo UNKNOWN |
| `wheel_steering_disconnect_damage_*` | mesma ordem | `0.90` | Definição CONFIRMADA; valor ao vivo UNKNOWN |

O material diz que o float do registro é igual nas quatro rodas do mesmo canal (`0.10`, `0.05`, `0.05`, `0.90`), na ordem dos quatro nomes acima.

Não há offset de instância ao vivo para esses canais.

## Functions

O escritor que sobe o byte de pneu por dois limiares é `0x1407636b0`, chamado em laço por `0x1407426d7`. O comportamento e os stores `0x1407639fe`, `0x140763a15` e `0x1407639da` estão em [Tyres](tyres.md). Essa função não foi identificada com os quatro canais da tabela.

## Experiments

Na comparação intacto/danificado, com a FL visualmente torta:

| Offset | O que aconteceu | Classe |
| :--- | :--- | :--- |
| `+0x3d8` | Float só na FL, já no estado intacto (`≈ -0.28`, depois `≈ -0.26`). | REJEITADO como dano novo da FL |
| `+0x220` | `−0.712 / +0.712 / −0.712 / 0` nos dois estados. | REJEITADO como dano desta sessão |
| `+0x10` | Mudou em mais de uma roda. O laço o usa como direção de um produto escalar, não como ângulo, flag ou dano. | REJEITADO como o canal `wheel_misalign_angle_damage` |

O detalhe de `+0x10` está em [Wheels](wheels.md).

As strings `puncture_tyre`, `blow_tyre` e `lose_wheel` existem num bloco de efeitos. O som de estouro continuou depois que o byte saiu de `2`. Não foram ligadas ao endereço do byte.

## Confirmed

A existência da tabela de definição, o índice `0..3` = RL, RR, FL, FR, e os quatro floats de registro iguais nas quatro rodas de cada canal.

## Rejeitado

`+0x3d8` como dano novo da FL. `+0x220` como dano desta sessão. `+0x10` como `wheel_misalign_angle_damage`, como vetor que muda só na roda torta, e como atitude do chassi. As duas últimas rejeições estão desenvolvidas em [Wheels](wheels.md).

## Unknown

O valor ao vivo dos quatro canais. Nenhum offset de instância foi isolado. Não está provado que eles escrevam o byte em `+0x2cf0`.

## Related Systems

- [Tyres](tyres.md) — byte `0` intacto, `1` furado, `2` só o aro, `3` roda solta
- [Wheels](wheels.md) — `+0x10` e `+0x220`
- [Telemetry](telemetry.md) — a ordem UDP repete `0` RL … `3` FR e também é externa ao rig
- [PhysicsRig](physics_rig.md)
