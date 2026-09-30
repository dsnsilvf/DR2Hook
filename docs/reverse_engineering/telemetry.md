# Telemetry

## Overview

O jogo envia telemetria UDP. O material compara esse pacote com a leitura direta do `PhysicsRig` e registra o que o parser externo não converte. A identidade do curso de suspensão com a memória foi medida depois e está em [Suspension](suspension.md). Aqui fica o pacote e o que ele não identifica.

## Known Structures

Pacote binário emitido por `sendto()` (`ws2_32.dll`) no fim de cada tick de simulação. Configuração em `hardware_settings_config.xml` ou `DRSM_data.xml`.

- **Porta padrão:** UDP `20777` (configurável; o texto cita `20778`).
- O processo só passa a emitir UDP depois de ler `hardware_settings_config.xml` com `udp enabled="true"` e `extradata="3"`. A sessão já aberta não relê esse arquivo.
- `sendto` não é interceptado pelo [Network Guard](network_guard.md). A telemetria UDP local continua podendo sair pela loopback.

O parser citado é `cm_telemetry`, arquivo `dirt/rally2.rs`. Ele só lê `f32` little-endian. Não converte unidade e não aplica `scale`.

## Memory Layout

Ordem das quatro rodas nesse parser: traseira esquerda, traseira direita, dianteira esquerda, dianteira direita.

| Offset no pacote | Campo no parser / nome citado | Notas do material |
| :--- | :--- | :--- |
| `68`, `72`, `76`, `80` | `suspension_position` | RL, RR, FL, FR. No XML de exportação, suspensão tem `scale="1000.0"`. O parser não divide por mil. A igualdade com a memória está em [Suspension](suspension.md). |
| `84`–`96` | `suspension_velocity` | Quatro floats. Sem campo interno ligado neste material. |
| `100`–`112` | `wheel_patch_speed` no canal; `wheel_velocity` no Rust; “Velocity of Wheel” na coluna do DiRT | Nenhum dos três diz se é m/s, rad/s ou velocidade no contato. |
| `132` | Marcha, na coluna do DiRT Rally | `0` neutro e `10` ré, o que coincide com `PhysicsRig + 0x1448`. Ver [Gearbox](gearbox.md). |
| `148` | Rotação do motor | A planilha anota rpm/10. O Rust guarda o float cru no campo `rpms`. |
| `204`–`216` | `brake_temperature` | Quatro floats. Sem campo interno localizado. |

Outros conteúdos citados para o pacote, sem offset neste material: `total_time`, `lap_time`, `lap_distance`, velocidade vetorial, coordenadas `x, y, z`, forças G (`g_x, g_y, g_z`), rotação dos 4 pneus e curso das 4 suspensões.

A planilha em `CodeMasters.html` é outra fonte, e as colunas não concordam entre si.

| Offsets | Coluna F1 2016 / XML | Coluna DiRT |
| :--- | :--- | :--- |
| `44`–`52` | “right direction”; no XML, `left_dir_*` com `scale="-1.0"` | “Roll Vector” |
| `56`–`64` | forward; canal `forward_dir_*` com `scale="1.0"` | “Pitch Vector” |

Isso não identifica `+0x2f0` nem `+0x310`. Esses vetores do chassi estão em [Vehicle Transform](vehicle_transform.md).

Não há no pacote vetor normal, quaternion, Up, velocidade angular de roda, contato ou superfície.

A ordem UDP é a mesma da tabela de definições de dano (`0` RL, `1` RR, `2` FL, `3` FR). As duas são externas ao `PhysicsRig`. Não ligam, por si, `+0x1680`, `+0x1aa0`, `+0x1ec0` e `+0x22e0` a esses cantos. Quem fechou essa ligação foi a captura pareada, em [Suspension](suspension.md).

`Gear::from_f32` do Rust trata `10` como nona e só aceita ré se o float for negativo. Isso não é o comportamento do `int32` em `+0x1448`.

## Data Flow

```text
tick de simulação
        │
        ▼
sendto()  (não interceptado)
        │
        ▼
pacote UDP na porta configurada
        │
        ├── parser Rust: f32 cru, sem scale
        └── captura pareada: offsets 68–80 confrontados com +0x1504
```

Comparação registrada entre as duas leituras:

- **UDP:** unidirecional (somente leitura), taxa de atualização discreta, sujeita a buffer e latência de rede.
- **DR2Hook, memória direta:** bidirecional (leitura e escrita), latência zero (acesso síncrono no mesmo processo) e permite savestate, restore e alteração de física.

## Confirmed

- A emissão é `sendto` no fim do tick, porta padrão `20777`.
- O parser `dirt/rally2.rs` não aplica `scale` e não converte unidade.
- A ordem das quatro rodas no parser é RL, RR, FL, FR nos intervalos da tabela.
- A coluna do DiRT no offset `132` coincide com o código de marcha de `+0x1448`. O parser Rust não reproduz esse código.

## Probable / Hypotheses

Nenhuma identificação nova de canal UDP foi promovida além do que a captura de suspensão fechou. Os rótulos discordantes de `44`–`64` permanecem como discordância entre colunas, não como mapa do chassi.

## Unknown

- Unidade de `wheel_velocity` / `wheel_patch_speed` / “Velocity of Wheel”.
- Campo interno de `brake_temperature`, `suspension_velocity` e da rotação de pneu citada no pacote.
- Nenhum campo UDP tem o formato de `+0x10`, `+0x1660` ou de `T` / `V`.
- No momento em que o pacote foi comparado ao código, a ligação entre `+0x1504` e `suspension_position` não existia no código. A igualdade bit a bit veio depois, da captura, e está em [Suspension](suspension.md). Não há campo interno de rotação de roda confirmado para comparar com o canal `100`–`112`.

## Related Systems

- [Suspension](suspension.md) — prova empírica de `suspension_position`
- [Wheels](wheels.md) — o que o pacote não mapeia nos vetores da roda
- [Gearbox](gearbox.md) — marcha
- [Engine](engine.md) — o float de `rpms` no parser não é a conversão rpm/10
- [Network Guard](network_guard.md) — `sendto` não é filtrado
- [Tyres](tyres.md) — o pacote não traz o byte `+0x2cf0`
