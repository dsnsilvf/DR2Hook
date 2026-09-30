# Physics Tick Harness (instrumentação opt-in)

O harness vive em **`dr2hook_core.dll`** (inicializado em `Core_Initialize`); a proxy **`dxgi.dll`** só carrega o core. Desligado por padrão; não altera o comportamento do jogo quando desabilitado.

## Ativação (spec: desligado por padrão)

Sem `DR2HOOK_PHYSICS_HARNESS=1` (ou `instrumentation=1` no INI), **nenhum** hook de física é instalado e o jogo comporta-se como antes.

Chaves no INI (`dr2hook_physics_harness.ini`) têm espaços e CRLF aparados em chave e valor.

| Mecanismo | Chave / variável |
| :--- | :--- |
| Instrumentação | `DR2HOOK_PHYSICS_HARNESS=1` ou `dr2hook_physics_harness.ini` → `instrumentation=1` |
| Escritas na memória do rig | `DR2HOOK_PHYSICS_HARNESS_WRITES=1` ou `writes=1` (exige instrumentação) |
| Self-test | `DR2HOOK_PHYSICS_HARNESS_SELF_TEST=1` ou `self_test=1` — liga instrumentação, **sem writes reais** (caminho **sham-write**); tenta todos os hooks; CSV até critério de ticks; relatório em log + `dr2hook_physics_harness_self_test.log` |
| Chamadas nativas experimentais | `DR2HOOK_PHYSICS_HARNESS_EXPERIMENTAL_NATIVE=1` ou `experimental_native=1` |

Toda escrita real ou chamada nativa também passa por `SafetyGuard::CanWriteState()`. O padrão permissivo do `SafetyGuard` **não é alterado** neste módulo.

## Endereços (spec parte 2, image base `0x140000000`)

Constantes em `include/dr2hook/physics_harness_addresses.h`. Resolução em runtime: `GetModuleHandle(nullptr) + RVA`.

| Símbolo | VA spec | RVA | Hook spec parte 2 |
| :--- | :--- | :--- | :--- |
| Integrator | `0x140746150` | `0x746150` | M1 entrada; M2/H6 no retorno (filtros) |
| Tick start | `0x14074b8f0` | `0x74B8F0` | **B2** entrada |
| Commit | `0x14074d190` | `0x74D190` | M3 entrada, B3 retorno |
| Per-tick caller / EndStep (H5) | `0x1407511e0` | `0x7511E0` | H5 entrada/retorno; writes opcionais |
| Physics step (H3) | `0x140dbc500` | `0xDBC500` | H3 entrada/retorno; contador `step` |
| PreTick (H4) | `0x140749a30` | `0x749A30` | H4 entrada/retorno; só log |
| SetPose | `0x140746770` | `0x746770` | Referência |
| Post-physics task (H1/H2) | `0x140dbca20` | `0xDBCA20` | **H2** entrada, **H1** retorno; hook `post_physics_task`; obrigatório |

### Nomes de hooks obrigatórios (plano)

| Nome no log / self-test | Função | Endereço de referência |
| :--- | :--- | :--- |
| **B2 (tick_start)** | Entrada tick start | `0x14074b8f0` |
| **M2 (0x1407395fa) / H6 (0x14073e314)** | Detour do integrator `@ 0x140746150` | M2 quando `return == 0x1407395fa`; H6 quando `return == 0x14073e314` |

### Spec correction 1 — H1 / H2 (frame loop `@ 0x140dbca20`)

| ID | Momento | Endereço | Writes / native |
| :--- | :--- | :--- | :--- |
| **H2** | Entrada do frame loop | `0x140dbca20` | `ScheduleWrite` / `ScheduleExperimentalNative` |
| **H1** | Retorno do frame loop (between-tick) | `0x140dbca20` | Idem |

Instalação **obrigatória** com tick start, integrator e commit: falha de prólogo em `post_physics_task` aborta `TryInstall`.

### Pontos B2 / M1–M3 / B3

| ID (CSV) | Momento | Endereço | Writes |
| :--- | :--- | :--- | :--- |
| **B2** | Antes do corpo de tick start | `0x14074b8f0` | Sim (sham no self-test) |
| **M1** | Antes do integrator | `0x140746150` | Sim |
| **M2** | Retorno integrator filtrado (`return == 0x1407395fa`) | `0x140746150` | Não (só CSV) |
| **H6** | Retorno integrator filtrado (`return == 0x14073e314`) | `0x140746150` | Sim (sham no self-test) |
| **M3** | Antes do Commit | `0x14074d190` | Sim |
| **B3** | Depois do Commit (pós-trampoline) | `0x14074d190` | Sim |

## Spec parte 3 — logging CSV

- **Ficheiro:** `dr2hook_physics_tick_harness_YYYYMMDD_HHMMSS.csv` (novo ficheiro por sessão, **sem append**).
- **Contador `tick`:** `s_tickCounter` (`uint64_t`, monotônico), incrementado em **B2** (tick start).
- **Self-test `in_stage_ticks`:** `s_inStageTickCounter` — só incrementa em **B2** quando a cadeia `car → container → rig` é válida (mesmo critério do CSV); **PASS ≥ 600** usa este contador, não `s_tickCounter`.
- **Contador `step`:** `s_stepCounter`, incrementado na **entrada de H3** (`0x140dbc500`, physics step). Linhas de H3/H4/H5 e dos boundaries B/M partilham o `tick` do ciclo actual; o `step` reflecte o último physics step visto na thread.
- **Cada boundary** chama `LogBoundarySample` → linhas bufferizadas (`kCsvFlushRowBatch` = 32) e flush em shutdown / fim do self-test (sem `flush` por linha).
- **Filtro:** só regista se a cadeia `car → container → rig` do jogador for válida (`+0x12c0 == rig`, `+0x12d0 == 4`).
- **Cadeia:** `[exe + 0x1681ce8]` → `car`; `car + 0x30` → `container`; `container + 0x08` → `rig`.

Cabeçalho: `tick,step,boundary,thread_id,container,rig,key_f5,key_f6,key_f7`, depois colunas `vec4` / `scalar` da tabela abaixo.

- **Teclas F5/F6/F7 (spec correction 3):** amostradas uma vez por tick em **B2** (`GetAsyncKeyState`); colunas `key_f5`, `key_f6`, `key_f7` são `0`/`1` (mods de prática que gravam estado do carro).

### Spec correction 3 — H6, commit log hooks, teclas

| Item | Detalhe |
| :--- | :--- |
| **M2** | No detour do integrator, se o endereço de retorno for **`0x1407395fa`**, dispara boundary **M2** (só CSV). |
| **H6** | No mesmo detour, se o retorno for **`0x14073e314`**, dispara boundary **H6** com fila de writes/native (ponto separado de M2). |
| **LOG_COMMIT_74D190** | Entrada do hook obrigatório em `@ 0x14074d190` — só log (M3/B3 mantêm writes). |
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

**Self-test:** com `self_test=1`, `LoadConfiguration` força instrumentação ligada e desliga `writes` / `experimental_native`. `TryInstall` regista cada site (obrigatório + opcional); após instalar imprime hooks `installed=0/1`. O relatório final dispara quando **`s_tickCounter` ≥ 600** ticks in-stage (incrementos em **B2**), não após tempo fixo.

**Critério PASS (self-test):**

1. Hooks obrigatórios instalados: **B2 (tick_start)**, **M2/H6 (integrator)**, **commit**, **post_physics_task** (trampoline MinHook **não nulo** — instalação falha se `*orig_trampoline == nullptr`).
2. **`in_stage_ticks` ≥ 600** (`s_inStageTickCounter`, só com cadeia rig válida em B2).

O log e `dr2hook_physics_harness_self_test.log` incluem `result=PASS` ou `result=FAIL`, contagem de **re-entrancy** (nested `OnBoundary`) e **sham_writes** (escritas enfileiradas **e** exercício sham in-game em cada boundary com fila de write durante self-test).

**Sham-write:** com self-test activo, `ExecuteScheduledWriteIfDue` consome a fila via `PerformShamWrite` (sem `memcpy`). Em cada boundary com writes permitidos, `InvokeSelfTestShamWritePath` chama o mesmo caminho in-game (cadeia rig válida).

**CSV extra:** após os escalares do rig, `container_vec4_0xC930_{x,y,z,w}` e `rig_vec4_0x290_{x,y,z,w}`.

## Spec parte 4 — escritas enfileiradas

- **API:** `ScheduleWrite(tick, boundary, rigOffset, bytes, len)` — uma escrita na fila.
- **Gating:** flag `writes` **e** `SafetyGuard::CanWriteState()` (agendamento e execução), excepto enfileiramento em self-test (execução sham). `SetPermissiveMode(true)` em `main.cpp` inalterado.
- **Execução:** só nos detours de física (`ExecuteScheduledWriteIfDue`), nunca no `Present`. Boundaries com fila: B2, M1, M3, B3, **H6**, **H1**, **H2**, H5_ENTRY, H5_RETURN, …
- **Log:** `before=` / `after=` em hex no `dr2hook.log` (ou `sham-write` no self-test).

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

Os primeiros **12 bytes** em disco (hex verificado) vivem em `include/dr2hook/physics_tick_harness_prologues.h` e são fixados por `TestBug1CalibratedPrologueHexPinned`. Padrão típico: **`mov rax,rsp`** (`48 8b c4 …`) ou **`mov r11,rsp`** (`4c 8b dc …` em `0x73A070`); o MinHook relocates essas instruções no trampoline. **`end_step`:** verificação com 12 B; roubo mínimo **13 B** (não partir `mov rax,[rip+…]`).

| Site | Hex (12 B) |
| :--- | :--- |
| tick_start | `488bc4488958184889702055` |
| integrator | `488bc44889581055488da838` |
| commit | `488bc4488958184889702055` |
| post_physics_task | `488bc4574881ecb000000033` |
| physics_step | `488bc44889501041554883ec` |
| pretick | `488bc4488958104889781855` |
| end_step | `40534883ec50488b05b3b2e6` |
| 73A070 | `4c8bdc55535741554157498d` |
| 73B620 | `488bc45657415641574881ec` |

Teste Wine (thunk + MinHook, dummy `mov rax,rsp`): `bash scripts/run_physics_detour_wine_test.sh` — valida **rbx/rbp/rsi/rdi/r12–r15**, **xmm6–15**, args **rcx–r9/xmm0–3**, retorno **rax/xmm0**.

Cada hook chama `VerifyHookPrologue` antes de `MH_CreateHook`.

### Win64 ABI (BUG 2)

Os detours expostos ao MinHook são **thunks em assembly** (`physics_harness_detour_x64.S`): guardam **RCX/RDX/R8/R9** e **XMM0–XMM3** antes do logging C++, chamam o trampoline original com o mesmo estado de argumentos (preservando **`dt` em XMM1** no tick start / integrator), e depois do retorno restauram **RAX** e **XMM0** antes de devolver ao caller do jogo.

- **Stub:** `lea r11, [g_ops_*+rip]` + `jmp` — **não** altera **r12/r13**; **r11** só até ao prologue comum.
- **Prologue:** `push rbp` / `push r12` / `push r13` com **`.seh_pushreg`**; **r12** = ops, **r13** = caller return (`[rbp+24]`); cópia em **`[rsp+0xA8]`** (acima do `Frame` @ `+0x20`, fora da shadow que tick_start/commit escrevem via `mov rax,rsp`); sem **r11** após `call`.
- **`Shutdown`** desactiva/remove hooks MinHook mas **não anula** `g_orig*` — evita corrida com threads ainda dentro do thunk.
- **`Frame`** começa em **`rsp+0x20`**, acima dos **32 bytes** de shadow/home space Win64 (`rsp+0x00..0x1F`) usados pelos `call` C++; offsets em `physics_harness_detour_abi.h` (`static_assert` alinhados com o `.S`).
- **`Frame::caller_return`** recebe o endereço de retorno do caller original (`[rsp]` na entrada do thunk), usado pelos filtros **M2/H6** no `after` do integrator (não usar `__builtin_return_address` no handler).
- O corpo comum do thunk declara **`.seh_proc` / `.seh_pushreg` / `.seh_stackalloc` / `.seh_endprologue`** para gerar unwind info (`.pdata`).
- Leituras da cadeia `car → rig` e amostras CSV usam **`VirtualQuery`** (fail-closed) antes de `memcpy`.
- O trampoline original **nunca é omitido**: se MinHook devolver ponteiro nulo, **`TryInstall` falha**.

## API

- `ScheduleExperimentalNative(..., CallParams)` — spec parte 5.

## Calibrar prólogos

Se a instalação falhar, o log lista bytes esperados vs. lidos. Atualize `include/dr2hook/physics_tick_harness_prologues.h` para o build do seu `dirtrally2.exe`.
