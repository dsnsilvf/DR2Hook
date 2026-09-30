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

## Spec parte 3 — logging CSV

- **Contador:** `s_tickCounter` (`uint64_t`, monotônico), incrementado em **B1** (tick start); todas as linhas CSV do mesmo ciclo partilham o mesmo `tick`.
- **Cada boundary** (B1, M1, M2, M3, B2, …) chama `LogBoundarySample` → uma linha em `dr2hook_physics_tick_harness.csv`.
- **Cadeia do rig:** `[exe + 0x1681ce8]` → `car`; `car + 0x30` → `container`; `container + 0x08` → `rig`.
- **Validação:** `*(uint64_t*)(rig + 0x12c0) == rig` e `*(uint32_t*)(rig + 0x12d0) == 4`. Se falhar, **não** escreve linha CSV.

### Colunas (rig)

| Campo | Offsets |
| :--- | :--- |
| `vec4` (4× float) | `0x170`, `0x180`, `0x200`, `0x210`, `0x2b0`, `0x2c0`, `0x2d0`, `0x2e0`, `0x320`, `0x330` |
| `scalar` (float) | `0x2508`, `0x1338` |

Cabeçalho: `tick,boundary,rig`, depois `vec4_0x…_{x,y,z,w}` e `scalar_0x…`.

## Spec parte 4 — escritas enfileiradas

- **API:** `ScheduleWrite(tick, boundary, rigOffset, bytes, len)` — uma escrita na fila.
- **Gating:** flag `writes` **e** `SafetyGuard::CanWriteState()` (agendamento e execução). `SetPermissiveMode(true)` em `main.cpp` inalterado.
- **Execução:** só nos detours de física (`ExecuteScheduledWriteIfDue`), nunca no `Present`.
- **Log:** `before=` / `after=` em hex no `dr2hook.log`.

## Spec parte 5 — API nativa experimental

Desligada por defeito (`experimental_native=0`). Endereços (base `0x140000000`):

| API | VA |
| :--- | :--- |
| SetTransform | `0x14074ad80` |
| SetLinVel | `0x14074a910` |
| SetAngVel | `0x14074a890` |
| Commit | `0x14074d190` |

Tipos e convenções inferidas (`__fastcall`, `DynamicsCarImpl*` em RCX): `include/dr2hook/physics_native_api.h`.

- **Agendar:** `ScheduleExperimentalNative(tick, boundary, kind, CallParams)` — uma chamada na fila; exige flag experimental **e** `SafetyGuard`.
- **Executar:** `ExecuteScheduledNativeIfDue` nos detours de física (mesmo boundary/tick que a fila).

## Endereços adicionais (extensões)

## Prólogo e instalação

Cada hook chama `VerifyHookPrologue` antes de `MH_CreateHook`. Hooks obrigatórios (tick start, integrator, commit) abortam `TryInstall` se o prólogo falhar.

## API

- `ScheduleExperimentalNative(..., CallParams)` — spec parte 5.

## Calibrar prólogos

Se a instalação falhar, o log lista bytes esperados vs. lidos. Atualize `include/dr2hook/physics_tick_harness_prologues.h` para o build do seu `dirtrally2.exe`.
