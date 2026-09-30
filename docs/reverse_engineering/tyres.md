# Tyres

## Overview

O estado do pneu e da roda fica fora do bloco de `0x420`. São quatro bytes em `rig + 0x2cf0`, na ordem RL, RR, FL, FR. O nome do campo não aparece no executável. As strings de desgaste, composto, temperatura e pressão também não foram ligadas a um campo neste build, salvo onde a tabela abaixo diz o contrário.

O efeito visual e o efeito no escalar vertical foram lidos na FL do rig `0x4b29bab0`. Os canais de dano nomeados no executável não foram ligados a este byte; estão em [Damage](damage.md).

## Known Structures

O objeto cuja base é `rig + 0x2bd0` guarda o byte em `+0x220 + i` e um float acumulado em `+0x130 + 4·i` (`rig + 0x2d00 + 4·i`).

`0x2bd0 + 0x220 = 0x2cf0`. `0x2bd0 + 0x130 = 0x2d00`.

## Memory Layout

| Offset | Tipo | Descrição | Confiança |
| :--- | :--- | :--- | :--- |
| `rig + 0x2cf0 + i` | `byte` | Estado do pneu e da roda. `i` na ordem RL, RR, FL, FR. | CONFIRMADO |
| `rig + 0x2bd0 + 0x220 + i` | `byte` | O mesmo byte, visto a partir desse objeto. | CONFIRMADO |
| `rig + 0x2d00 + 4·i` | `float` | Acumulado. No evento da FL ficou em `27.8`; nas outras rodas, em `4.34`, `0` e `1.02`. Gravar `0` no byte não limpou esse acumulado. | Valor observado; nome não dado |
| `objeto + 0x268` | `float` | Escala. `0.5` quando o byte é `2`; `1.0` nos outros valores testados. | CONFIRMADO |
| `objeto + 0x38` / `objeto + 0x3c` / `objeto + 0x68` | `float` | Escalar vertical de setup. Com o byte em `2`, a cópia usa `+0x3c`. No rig `0x4b29bab0` isso foi `0.194` em vez de `0.301`. O caminho de cópia está em [Vehicle Setup](vehicle_setup.md). | CONFIRMADO |

| Valor | O que a FL mostrou | Classificação |
| :--- | :--- | :--- |
| `0` | Pneu intacto. A física do pneu voltou à das outras rodas. | CONFIRMADO |
| `1` | Aparência de pneu furado. Escala e escalar vertical iguais aos do pneu intacto. | CONFIRMADO |
| `2` | Só o aro de ferro. Escala `0.5` e escalar vertical vindo de `objeto + 0x3c` (`0.194` em vez de `0.301`). | CONFIRMADO |
| `3` | A roda se solta do carro. | CONFIRMADO |

No rig `0x4b29bab0` a FL foi o único canto afetado. O byte foi lido em `2` com a roda só no aro, e depois gravado em `1`, `0` e `3`. Cada valor permaneceu até a gravação seguinte.

## Functions

| Endereço | Função | Relevância |
| :--- | :--- | :--- |
| `0x1407426d7` | Chama `0x1407636b0` em laço | Sobe o byte. |
| `0x1407636b0` | Dois limiares | Acima do maior, grava `2` em `0x1407639fe`. Entre os dois, grava `1` em `0x140763a15` se o byte ainda for menor que `1`. Se o byte já for `3` ou mais, `0x1407639da` não escreve. Não devolve o byte para `0`. |
| `0x140730300` | Devolve `0.5` com o byte em `2`, e `1.0` nos outros | Escala. |
| `0x1407501ec` | Grava essa escala em `objeto + 0x268` | Store da escala. |
| `0x140730628` | Copia `objeto + 0x3c` para `objeto + 0x68` quando o byte é `2` | Nos outros valores copia `objeto + 0x38`. O ramo `0x140730634` está em [Vehicle Setup](vehicle_setup.md). |
| `0x14073da44` | Zera três floats de saída | Observado no caminho do byte `2`. |
| `0x140738c0f` | Multiplica a escala de `+0x268` numa saída | Observado no caminho do byte `2`. |
| `0x1407305dc` | Devolve verdadeiro com o byte em `3` | Teste de roda solta. |
| `0x1407495ee`, `0x14074dc66`, `0x1407502b4` | Pulam a roda quando o byte é `3` | O último deixa de chamar `0x140730e50`. |
| `0x140730e50` | Escreve `objeto + 0x9c` e `objeto + 0xa4` | Não é chamado para a roda com byte `3`. |

A escala e o escalar vertical não mudam no valor `3`: esses dois só seguem o valor `2`.

O som de estouro continuou com o byte em `1`. Não é uma leitura contínua desse byte. As strings `puncture_tyre`, `blow_tyre` e `lose_wheel` existem num bloco de efeitos e não foram ligadas a esse endereço.

## Data Flow

```text
acumulado em rig + 0x2d00 + 4·i
        │
        ▼
0x1407636b0
        ├── acima do limiar maior → byte 2
        ├── entre os limiares, se byte < 1 → byte 1
        └── byte já ≥ 3 → não escreve
        │
        ▼
rig + 0x2cf0 + i
        ├── 2 → escala 0.5 em objeto + 0x268; vertical copiado de +0x3c
        ├── 0 ou 1 → escala 1.0; vertical copiado de +0x38
        └── 3 → 0x1407495ee, 0x14074dc66 e 0x1407502b4 pulam a roda
```

Os limiares numéricos não estão no material. Só está o acumulado observado (`27.8` na FL) e que a função não desce o byte.

## Confirmed

Os quatro valores da tabela, o que a FL mostrou em cada um, a escala `0.5` só no valor `2`, o escalar vertical `0.194` contra `0.301`, e o pulo da roda no valor `3`.

`tyre_wear_state`, `WheelsWear`, `PersistentWear` e `tyre_compound_id` aparecem no executável. A escala `0.5` / `1.0` em `objeto + 0x268` é o efeito do byte `2`, não um campo de desgaste localizado.

## Rejeitado

Não há, neste fio, uma hipótese de pneu que o material tenha marcado como rejeitada. Hipóteses de dano em outros offsets estão em [Damage](damage.md).

## Unknown

| Item | Classe |
| :--- | :--- |
| Nome do byte no executável | Ausente |
| Ligação de `puncture_tyre`, `blow_tyre`, `lose_wheel` com este endereço | Não ligada. O áudio de estouro continuou depois que o byte saiu de `2` |
| Valores numéricos dos dois limiares | Não citados |
| `tyre_wear_state`, `WheelsWear`, `PersistentWear`, `tyre_compound_id` | UNKNOWN. Só no executável |
| Temperatura de pneu, pressão, grip restante | UNKNOWN. Sem campo localizado neste build |
| `wheel_rotation_rate` / `wheel_speed` | UNKNOWN. Nomes no executável; offset interno não isolado. Ver [Wheels](wheels.md) |

## Related Systems

- [Damage](damage.md) — canais nomeados, não ligados a este byte
- [Vehicle Setup](vehicle_setup.md) — `+0x38`, `+0x3c`, `+0x68`
- [Wheels](wheels.md) — o byte não está dentro do bloco de `0x420`
- [Suspension](suspension.md) — o curso em `+0x1504` é outro campo
- [PhysicsRig](physics_rig.md)
- [../BLACKBOX.md](../BLACKBOX.md) — o byte fica fora da janela `0x0000:0x2600`
