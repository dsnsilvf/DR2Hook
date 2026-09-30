# Physics Tick Harness (instrumentação opt-in)

O harness vive em **`dr2hook_core.dll`** (inicializado em `Core_Initialize`); a proxy **`dxgi.dll`** só carrega o core. Desligado por padrão; não altera o comportamento do jogo quando desabilitado.

## Ativação (spec: desligado por padrão)

Sem `DR2HOOK_PHYSICS_HARNESS=1` (ou `instrumentation=1` no INI), **nenhum** hook de física é instalado e o jogo comporta-se como antes.

| Mecanismo | Chave / variável |
| :--- | :--- |
| Instrumentação | `DR2HOOK_PHYSICS_HARNESS=1` ou `dr2hook_physics_harness.ini` → `instrumentation=1` |
| Escritas na memória do rig | `DR2HOOK_PHYSICS_HARNESS_WRITES=1` ou `writes=1` (exige instrumentação) |
| Self-test | `DR2HOOK_PHYSICS_HARNESS_SELF_TEST=1` ou `self_test=1` — liga instrumentação, **sem writes**; tenta todos os hooks; CSV ~5 s; relatório em log + `dr2hook_physics_harness_self_test.log` |
| Chamadas nativas experimentais | `DR2HOOK_PHYSICS_HARNESS_EXPERIMENTAL_NATIVE=1` ou `experimental_native=1` |

Toda escrita ou chamada nativa também passa por `SafetyGuard::CanWriteState()`. O padrão permissivo do `SafetyGuard` **não é alterado** neste módulo.

## Endereços (spec parte 2, image base `0x140000000`)

Constantes em `include/dr2hook/physics_harness_addresses.h`. Resolução em runtime: `GetModuleHandle(nullptr) + RVA`.

| Símbolo | VA spec | RVA | Hook spec parte 2 |
| :--- | :--- | :--- | :--- |
| Integrator | `0x140746150` | `0x746150` | M1 entrada, M2 retorno |
| Tick start | `0x14074b8f0` | `0x74B8F0` | B1 entrada |
| Commit | `0x14074d190` | `0x74D190` | M3 entrada, B2 retorno |
| Per-tick caller / EndStep (H5) | `0x1407511e0` | `0x7511E0` | H5 entrada/retorno; writes opcionais |
| Physics step (H3) | `0x140dbc500` | `0xDBC500` | H3 entrada/retorno; contador `step` |
| PreTick (H4) | `0x140749a30` | `0x749A30` | H4 entrada/retorno; só log |
| SetPose | `0x140746770` | `0x746770` | Referência |
| Frame loop | `0x140dbca20` | `0xDBCA20` | **H2** entrada, **H1** retorno (between-tick); obrigatórios; writes enfileirados |

### Spec correction 1 — H1 / H2 (frame loop `@ 0x140dbca20`)

| ID | Momento | Endereço | Writes / native |
| :--- | :--- | :--- | :--- |
| **H2** | Entrada do frame loop | `0x140dbca20` | `ScheduleWrite` / `ScheduleExperimentalNative` |
| **H1** | Retorno do frame loop (between-tick) | `0x140dbca20` | Idem |

Instalação **obrigatória** com tick start, integrator e commit: falha de prólogo em `frame_loop` aborta `TryInstall`.

### Pontos B1 / M1–M3 / B2

| ID | Momento | Endereço | Writes |
| :--- | :--- | :--- | :--- |
| **B1** | Antes do corpo de tick start | `0x14074b8f0` | Sim |
| **M1** | Antes do integrator | `0x140746150` | Sim |
| **M2** | Depois do integrator (pós-trampoline) | `0x140746150` | Não (só CSV) |
| **H6** | Retorno integrator filtrado (`return == 0x14073e314`) | `0x140746150` | Sim |
| **M3** | Antes do Commit | `0x14074d190` | Sim |
| **B2** | Depois do Commit (pós-trampoline) | `0x14074d190` | Sim |

## Spec parte 3 — logging CSV

- **Contador `tick`:** `s_tickCounter` (`uint64_t`, monotônico), incrementado em **B1** (tick start).
- **Contador `step`:** `s_stepCounter`, incrementado na **entrada de H3** (`0x140dbc500`, physics step). Linhas de H3/H4/H5 e dos boundaries B/M partilham o `tick` do ciclo actual; o `step` reflecte o último physics step visto na thread.
- **Cada boundary** chama `LogBoundarySample` → uma linha em `dr2hook_physics_tick_harness.csv` (após validação da cadeia do jogador).
- **Filtro:** só regista se a cadeia `car → container → rig` do jogador for válida (`+0x12c0 == rig`, `+0x12d0 == 4`).
- **Cadeia:** `[exe + 0x1681ce8]` → `car`; `car + 0x30` → `container`; `container + 0x08` → `rig`.

Cabeçalho: `tick,step,boundary,thread_id,container,rig,key_f5,key_f6,key_f7`, depois colunas `vec4` / `scalar` da tabela abaixo.

- **Teclas F5/F6/F7 (spec correction 3):** amostradas uma vez por tick em **B1** (`GetAsyncKeyState`); colunas `key_f5`, `key_f6`, `key_f7` são `0`/`1` (mods de prática que gravam estado do carro).

### Spec correction 3 — H6, commit log hooks, teclas

| Item | Detalhe |
| :--- | :--- |
| **H6** | No detour do integrator `@ 0x140746150`, se o endereço de retorno for **`0x14073e314`**, dispara boundary **H6** com fila de writes/native (ponto separado de M2, que só regista CSV). |
| **LOG_COMMIT_74D190** | Entrada do hook obrigatório em `@ 0x14074d190` — só log (M3/B2 mantêm writes). |
| **LOG_COMMIT_73A070 / LOG_COMMIT_73B620** | Hooks **opcionais** só log em `@ 0x14073a070` e `@ 0x14073b620`. |

### Hooks H3–H5 (spec correction 2)

| ID | VA | Comportamento |
| :--- | :--- | :--- |
| **H3_ENTRY / H3_RETURN** | `0x140dbc500` | Só log (sem writes); `step++` na entrada |
| **H4_ENTRY / H4_RETURN** | `0x140749a30` | Só log |
| **H5_ENTRY / H5_RETURN** | `0x1407511e0` | Log + `ScheduleWrite` / `ScheduleExperimentalNative` opcionais em entrada e retorno |

Todas as lin CSV incluem `thread_id` (`GetCurrentThreadId`) e ponteiros `container` / `rig` do jogador.

### Colunas (rig)

| Campo | Offsets |
| :--- | :--- |
| `vec4` (4× float) | `0x170`, `0x180`, `0x200`, `0x210`, `0x2b0`, `0x2c0`, `0x2d0`, `0x2e0`, `0x320`, `0x330` |
| `scalar` (float) | `0x2508`, `0x1338` |
| `container` vec4 | `0xc930` (spec correction 4) |
| `rig` vec4 extra | `0x290` (spec correction 4) |

### Spec correction 4 — self-test e colunas extra

**Self-test:** com `self_test=1`, `LoadConfiguration` força instrumentação ligada e desliga `writes` / `experimental_native`. `TryInstall` regista cada site (obrigatório + opcional); após instalar imprime hooks `installed=0/1`; uma thread espera **5 s** de jogo (CSV normal) e imprime contagem de **fires** por boundary. Nenhuma escrita ou native experimental é executada.

**CSV extra:** após os escalares do rig, `container_vec4_0xC930_{x,y,z,w}` e `rig_vec4_0x290_{x,y,z,w}`.

## Spec parte 4 — escritas enfileiradas

- **API:** `ScheduleWrite(tick, boundary, rigOffset, bytes, len)` — uma escrita na fila.
- **Gating:** flag `writes` **e** `SafetyGuard::CanWriteState()` (agendamento e execução). `SetPermissiveMode(true)` em `main.cpp` inalterado.
- **Execução:** só nos detours de física (`ExecuteScheduledWriteIfDue`), nunca no `Present`. Boundaries com fila: B1, M1, M3, B2, **H6**, **H1**, **H2**, H5_ENTRY, H5_RETURN, …
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

Cada hook chama `VerifyHookPrologue` antes de `MH_CreateHook`. Hooks obrigatórios (tick start, integrator, commit, **frame loop H1/H2**) abortam `TryInstall` se o prólogo falhar.

### Win64 ABI (BUG 2)

Os detours expostos ao MinHook são **thunks em assembly** (`physics_harness_detour_x64.S`): guardam **RCX/RDX/R8/R9** e **XMM0–XMM3** antes do logging C++, chamam o trampoline original com o mesmo estado de argumentos (preservando **`dt` em XMM1** no tick start / integrator), e depois do retorno restauram **RAX** e **XMM0** antes de devolver ao caller do jogo.

## API

- `ScheduleExperimentalNative(..., CallParams)` — spec parte 5.

## Calibrar prólogos

Se a instalação falhar, o log lista bytes esperados vs. lidos. Atualize `include/dr2hook/physics_tick_harness_prologues.h` para o build do seu `dirtrally2.exe`.
