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

## Endereços (spec parte 2, image base `0x140000000`)

Constantes em `include/dr2hook/physics_harness_addresses.h`. Resolução em runtime: `GetModuleHandle(nullptr) + RVA`.

| Símbolo | VA spec | RVA | Hook spec parte 2 |
| :--- | :--- | :--- | :--- |
| Integrator | `0x140746150` | `0x746150` | M1 entrada, M2 retorno |
| Tick start | `0x14074b8f0` | `0x74B8F0` | B1 entrada |
| Commit | `0x14074d190` | `0x74D190` | M3 entrada, B2 retorno |
| Per-tick caller | `0x1407511e0` | `0x7511E0` | Referência (log na instalação) |
| SetPose | `0x140746770` | `0x746770` | Referência |
| Frame loop | `0x140dbca20` | `0xDBCA20` | Referência; hook opcional (extensões) |

### Pontos B1 / M1–M3 / B2

| ID | Momento | Endereço |
| :--- | :--- | :--- |
| **B1** | Antes do corpo de tick start | `0x14074b8f0` |
| **M1** | Antes do integrator | `0x140746150` |
| **M2** | Depois do integrator (pós-trampoline) | `0x140746150` |
| **M3** | Antes do Commit | `0x14074d190` |
| **B2** | Depois do Commit (pós-trampoline) | `0x14074d190` |

## Endereços adicionais (extensões)

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
