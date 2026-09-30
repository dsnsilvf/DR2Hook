# Physics Tick Harness (instrumentação opt-in)

Harness de **somente instrumentação** para o tick de física do `dirtrally2.exe`. Desligado por padrão; não altera o comportamento do jogo quando desabilitado.

## Ativação (spec: desligado por padrão)

Sem `DR2HOOK_PHYSICS_HARNESS=1` (ou `instrumentation=1` no INI), **nenhum** hook de física é instalado e o jogo comporta-se como antes.

| Mecanismo | Chave / variável |
| :--- | :--- |
| Instrumentação | `DR2HOOK_PHYSICS_HARNESS=1` ou `dr2hook_physics_harness.ini` → `instrumentation=1` |
| Escritas na memória do rig | `DR2HOOK_PHYSICS_HARNESS_WRITES=1` ou `writes=1` (exige instrumentação) |
| Self-test (log de hooks disparados, sem writes) | `DR2HOOK_PHYSICS_HARNESS_SELF_TEST=1` ou `self_test=1` |
| Chamadas nativas experimentais | `DR2HOOK_PHYSICS_HARNESS_EXPERIMENTAL_NATIVE=1` ou `experimental_native=1` |

Toda escrita ou chamada nativa também passa por `SafetyGuard::CanWriteState()`. O padrão permissivo do `SafetyGuard` **não é alterado** neste módulo.

## Endereços (image base `0x140000000`)

| Símbolo | VA |
| :--- | :--- |
| Integrator | `0x140746150` |
| Tick start | `0x14074b8f0` |
| Commit | `0x14074d190` |
| Frame loop (H1/H2) | `0x140dbca20` |
| Physics step (H3) | `0x140dbc500` |
| PreTick (H4) | `0x140749a30` |
| End step (H5) | `0x1407511e0` |
| SetPose | `0x140746770` |
| SetTransform (experimental) | `0x14074ad80` |
| SetLinVel / SetAngVel | `0x14074a910` / `0x14074a890` |

## Pontos de hook

| ID | Momento | Escritas enfileiradas |
| :--- | :--- | :--- |
| B1 | Antes do tick start | Sim |
| M1 / M2 | Antes / depois do integrator | Sim |
| M3 / B2 | Antes / depois do Commit | Sim |
| H2 / H1 | Entrada / retorno do frame loop | Sim (ponto “between tick”) |
| H3 / H4 | Physics step / PreTick | Log only |
| H5 | End step | Sim |
| H6 | Integrator quando `_ReturnAddress()` == `0x14073e314` | Sim |
| LOG_COMMIT_A/B | `0x14073a070`, `0x14073b620` | Log only (opcional) |

Cada instalação chama `VerifyHookPrologue` (`include/dr2hook/hook_prologue.h`, mesma política dos hooks de UI/game) **antes** de `MH_CreateHook`. Em mismatch o hook em causa **não** é instalado; hooks obrigatórios (tick start, integrator, commit, frame loop) abortam `TryInstall` inteiro.

## Cadeia do rig (filtro jogador)

```
[exe + 0x1681ce8] -> car -> +0x30 container -> +0x08 rig
```

Validação: `*(rig + 0x12c0) == rig` e `*(uint32_t*)(rig + 0x12d0) == 4`. CSV e writes só quando a cadeia é válida.

## CSV (`dr2hook_physics_tick_harness.csv`)

Colunas: tick, boundary, `thread_id`, rig, F5/F6/F7, offsets rig `0x170`…`0x330` (vec4), escalares `0x2508`/`0x1338`, vec4 rig `0x290`, vec4 container `0xc930`.

## API

- `ScheduleWrite(tick, boundary, rigOffset, bytes, len)` — uma operação por vez, executada na thread de física.
- `ScheduleExperimentalNative(tick, boundary, kind)` — `SetTransform`, `SetLinVel`, `SetAngVel`, `Commit` (experimental, desligado por padrão).

Logs before/after de writes em `dr2hook.log`.

## Calibrar prólogos

Se a instalação falhar, o log lista bytes esperados vs. lidos. Atualize `include/dr2hook/physics_tick_harness_prologues.h` para o build do seu `dirtrally2.exe`.
