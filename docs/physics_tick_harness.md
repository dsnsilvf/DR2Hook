# Physics Tick Harness (instrumentação opt-in)

Harness de **somente instrumentação** para o tick de física do `dirtrally2.exe`. Desligado por padrão; não altera o comportamento do jogo quando desabilitado.

## Ativação

Use **uma** das opções abaixo (OR lógico):

| Mecanismo | Valor |
| :--- | :--- |
| Variável de ambiente `DR2HOOK_PHYSICS_HARNESS` | `1`, `true`, `yes`, `on` |
| Arquivo `dr2hook_physics_harness.ini` (pasta do executável) | `instrumentation=1` |

Escritas experimentais na memória do rig exigem **instrumentação + flag dedicada**:

| Mecanismo | Valor |
| :--- | :--- |
| `DR2HOOK_PHYSICS_HARNESS_WRITES` | `1` / `true` / … |
| `dr2hook_physics_harness.ini` | `writes=1` |

Toda escrita também passa pelo `SafetyGuard::CanWriteState()`. O padrão permissivo do `SafetyGuard` **não é alterado** neste módulo.

## Endereços (image base `0x140000000`)

| Símbolo | VA |
| :--- | :--- |
| Integrator | `0x140746150` |
| Tick start | `0x14074b8f0` |
| Commit | `0x14074d190` |
| Per-tick caller | `0x1407511e0` |
| SetPose | `0x140746770` |
| Frame loop | `0x140dbca20` |

## Pontos de hook

| ID | Momento |
| :--- | :--- |
| **B1** | Antes do tick start |
| **M1** | Antes do integrator |
| **M2** | Depois do integrator |
| **M3** | Antes do Commit |
| **B2** | Depois do Commit |

Cada hook valida o **prólogo** em `include/dr2hook/physics_tick_harness_prologues.h`. Em mismatch a instalação é recusada (mesma política dos hooks de UI).

## Cadeia do rig

```
[exe + 0x1681ce8] -> car
car + 0x30 -> container
container + 0x08 -> rig
```

Validação: `*(rig + 0x12c0) == rig` e `*(uint32_t*)(rig + 0x12d0) == 4`.

## CSV

Arquivo: `dr2hook_physics_tick_harness.csv` (pasta do jogo).

Por boundary: contador monotônico de tick, nome do boundary, ponteiro do rig, offsets `0x170` … `0x330` como `vec4` float, escalares `0x2508` e `0x1338`.

## Escrita enfileirada

API: `PhysicsTickHarness::ScheduleWrite(tick, boundary, rigOffset, bytes, len)`.

- Uma escrita por vez.
- Executada na thread de física no boundary/tick indicados (nunca no `Present`).
- Valores **before/after** vão para `dr2hook.log`.

## Calibrar prólogos

Se a instalação falhar, o log lista bytes esperados vs. lidos. Atualize `physics_tick_harness_prologues.h` para o build do seu `dirtrally2.exe`.
