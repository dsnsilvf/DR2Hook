# Vehicle Transform

## Overview

Posição do centro de massa, quatérnion e base ortonormal do chassi no `PhysicsRig`, mais a âncora visual no container. A cadeia até o rig está em [PhysicsRig](physics_rig.md).

## Known Structures

Três `Vector3` SIMD de 16 bytes e um `Vector4` no rig. A âncora visual é um bloco separado, no container, não no rig.

## Memory Layout

| Offset | Tipo | Descrição | Confiança |
| :--- | :--- | :--- | :--- |
| `rig + 0x2d0` | `Vector3` (SIMD 16 B) | Posição física do centro de massa. No layout mapeado, \(\vec{p} = (x, y, z, 0)\) em metros. | Mapeado; usado como `P2` na conta das rodas |
| `rig + 0x2e0` | `Vector4` (SIMD 16 B) | Quatérnion de rotação \((q_x, q_y, q_z, q_w)\). | CONFIRMADO na medição abaixo |
| `rig + 0x2f0` | `Vector3` (SIMD 16 B) | Linha 0 da matriz de orientação. Eixo lateral / Right, no espaço do mundo. | CONFIRMADO |
| `rig + 0x300` | `Vector3` (SIMD 16 B) | Linha 1. Eixo vertical / Up. | CONFIRMADO |
| `rig + 0x310` | `Vector3` (SIMD 16 B) | Linha 2. Eixo frontal / Forward. | CONFIRMADO |
| `container + 0xcd0` | `x, y, z, yaw` | Ancoragem visual da carroceria. | Mapeado, com a relação de altura abaixo |

## Mathematics

`+0x2f0`, `+0x300` e `+0x310` são Right, Up e Forward do corpo, no espaço do mundo. São ortonormais. O produto misto `Right · (Up × Forward) = +1`, e `Right × Up = Forward`. Base destra.

O quatérnion em `+0x2e0` é `(x, y, z, w)`, com módulo `1`. As linhas da matriz de rotação do conjugado coincidem com Right, Up e Forward. A base armazenada é o frame do corpo escrito no mundo.

`rotate(q, (1,0,0))`, `rotate(q, (0,1,0))` e `rotate(q, (0,0,1))` reproduzem `+0x2f0`, `+0x300` e `+0x310`. O ponto de setup `L` está nesse referencial: X lateral, Y vertical, Z longitudinal. A origem de `L` está em [Vehicle Setup](vehicle_setup.md).

Além da posição física em `+0x2d0`, o container mantém a carroceria em `container + 0xcd0`, onde \(y_{visual} \approx y_{fisico} - 0.44\,\mathrm{m}\) em repouso estático sobre a suspensão.

A rotação de um vetor pelo quatérnion do chassi, usada ao montar o ponto da roda, é a rotina abaixo. Não é uma rotina de roda: há 69 chamadas, e ela não lê o `PhysicsRig`.

```text
q * v * conjugado(q)
```

## Functions

| Endereço | Função | Relevância |
| :--- | :--- | :--- |
| `0x14072f8e0` | Recebe um quatérnion em `rcx`, um vetor em `r8` e grava três floats em `rdx`. | `q * v * conjugado(q)`. Usada pelo escritor do ponto em [Suspension](suspension.md). |

Os offsets UDP `44`–`64`, anotados ora como right/forward, ora como roll/pitch, não identificam `+0x2f0` nem `+0x310`. Ver [Telemetry](telemetry.md).

## Confirmed

- Right, Up e Forward são ortonormais, no mundo, base destra, `Right × Up = Forward`.
- O quatérnion `(x, y, z, w)` de módulo 1 reproduz essa base pelo conjugado, e `rotate` dos eixos canônicos reproduz os três vetores.
- O referencial de `L` é o do chassi: X lateral, Y vertical, Z longitudinal.

## Unknown

Não há, neste material, a convenção de sinal do `yaw` em `container + 0xcd0` além do nome do componente, nem um escritor isolado de `+0x2d0` / `+0x2e0`.

## Related Systems

- [PhysicsRig](physics_rig.md)
- [Vehicle Physics](vehicle_physics.md) — velocidades logo após esta base, em `+0x320` e `+0x330`
- [Wheels](wheels.md) — os vetores da roda foram medidos neste espaço
- [Suspension](suspension.md) — `Up` do chassi entra no termo de `+0x1660`
- [Vehicle Setup](vehicle_setup.md)
