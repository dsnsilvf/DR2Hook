# PR 3 review: cursor/minor-fixes-physics-harness-739e (base a35baa9)

Reviewed 2026-09-30, 12:57-13:15 BRT, by the Experimental Validation Specialist. This was read-only with respect to the PR: no merge, no comment, no push. The user's machine was not touched.

- Source: `git clone` of the branch onto the box at `/workspace/pr3`. HEAD is `c8eab24`, with parents `47ce206` → `f5b72b7` → `a35baa9`.
- Build: done on the box with the `package_release.sh` flags (Release, `x86_64-w64-mingw32-g++`, `-static -static-libgcc -static-libstdc++`), target `dr2hook_core`, into `/workspace/build-pr3`.
  - The build succeeds. It produces 9 warnings: `g_ops_* initialized and declared 'extern'`.
  - The box compiler is GCC 14-posix; the user has GCC 16.2. The thunk bytes are hand-written asm and do not depend on the compiler, but the C callbacks' code generation can differ.
- Objdump: `/tmp/core.dis`. The DLL loads at image base 0x38d240000, with `PhysicsHarness_DetourCommon` at 0x38d34d400.
- Reference prologue bytes: 12-byte reads of `dirtrally2.exe` on disk, recorded in `/workspace/validation/G3-prep.md`.

## OVERALL: **FAIL, do not deploy for G3**

### Blocking issues (in order)
1. **B-1: the prologue "fix" did not change a single byte.** Commit f5b72b7 only adds comments. All 9 arrays are still the old placeholders, i.e. the "expected" column in G3-prep. The comments claim those are disk values, which is wrong. Result: `tick_start` fails `VerifyHookPrologue`, and nothing is installed.
2. **B-2: the thunk dereferences R11 after calls that are allowed to clobber it.**
   - R11 is volatile under the Win64 ABI.
   - Crash on the first `tick_start` once B-1 is fixed (near-certain):
     - `physics_harness_detour_x64.S:35` (`mov rax,[r11+0x10]` at 0x38d34d46a) runs after the `before` callback (`call rax` at 0x38d34d440).
     - `HarnessBeforeTickStart` calls `GetAsyncKeyState` directly (0x38d253eeb, via `__imp_GetAsyncKeyState`). That ends in a win32u syscall, and `syscall` sets R11 to RFLAGS.
     - It also runs libstdc++ stream, mutex and WriteFile code.
     - This is a load from a tiny address, i.e. an access violation, on the physics thread.
   - Wrong `after` pointer for `commit_log_73a070` (certain):
     - `.S:45` (`mov rax,[r11+8]` at 0x38d34d485) runs after the original.
     - `0x14073a070` begins `mov r11,rsp` (disassembly line 29541). R11 therefore returns as the original's entry RSP, so `[r11+8]` is the thunk's own shadow slot 0, which is garbage.
     - The `after` pointer that gets called is thus arbitrary. It is `nullptr` for this site in the table, but the thunk never reads the table value.
   - Fix: keep the ops pointer in a callee-saved register (e.g. push rbx and use `mov rbx,r11`, keeping alignment) or in a stack slot, and reload it after every call.
3. **B-3: the thunk has no SEH/unwind info.**
   - `.S:5-57` has no `.seh_proc`, `.seh_pushreg rbp`, `.seh_setframe`, `.seh_stackalloc` or `.seh_endprologue`.
   - Parsing the built DLL's `.pdata` confirms that no RUNTIME_FUNCTION covers 0x38d34d400-0x38d34d4a7. For contrast, `HarnessAfterIntegrator` is covered at RVA 0x100c0-0x10190.
   - The Win64 unwinder therefore treats the thunk as a leaf and takes `[rsp]` as the return address. Its RSP is 0xC8 below that, so the unwind is garbage.
   - Consequences:
     - Any C++ exception from a callback crashes; mingw x64 C++ EH uses SEH unwinding.
     - Any SEH dispatch from game code that walks past the thunk looking for a handler crashes.
     - Stack walks from crash reporters are garbage.
   - The prompt requires that exceptions do not crash, so this blocks.
4. **B-4: G3 evidence is broken by design.**
   - `HarnessAfterIntegrator` (`physics_tick_harness.cpp:745-759`) uses `__builtin_return_address(0)`. In the build that is `mov rbx,[rsp+0x38]` at 0x38d2500cd, which is the thunk's own return address (0x38d34d495), not the game caller's.
   - `M2` and `H6` therefore **never fire**. The actual caller return address sits at `[rbp+8]` in the thunk and is not passed to C.
   - The G3 phase order (M1→M2→M3) cannot be verified. This is a functional issue, not a safety one, but it blocks G3.

### Non-blocking issues (fix in the same pass)
- **N-1: the `Frame` layout does not match the asm.**
  - The thunk passes `rcx = rsp+8`. The C `struct alignas(16) Frame` (`physics_harness_detour_abi.h:11-22`) puts `xmm0` at +0x20, `rax` at +0x60 and `xmm0_ret` at +0x70. The asm stores them at +0x28, +0x68 and +0x78 relative to the frame pointer.
  - The pointer itself is only 8-mod-16 aligned, while the type claims 16.
  - It is latent today, because no callback reads `Frame`. The first callback that reads `frame->xmm1` (dt) will read the wrong slot, or fault on `movaps`.
- **N-2: the saved argument registers sit inside the callee's shadow space.**
  - The callee's home area is thunk `[rsp+0x00..0x1F]`. Saved rcx, rdx and r8 are at +0x08, +0x10 and +0x18, so a callee that spills its home area overwrites them before they are restored for the original.
  - In the GCC 14 build, no `Harness*` callback spills (I checked the first 12 instructions of each). That is compiler-dependent, and the user builds with GCC 16.
  - Fix: `sub rsp,0xE0` and save at +0x20 and above.
- **N-3: no re-entrancy guard in the thunk.** There is only a thread-local counter in `OnBoundary` (`.cpp:609-620`), which counts but does not block. The normal nesting (tick_start → integrator → commit) is sequential and fine.
- **N-4: stack arguments are not forwarded.**
  - The original runs in a fresh frame, so its `[rsp+0x28..]` points at the thunk's locals.
  - None of the call sites I inspected passes more than 4 arguments: integrator (rcx, xmm1), 73a070 (rcx, xmm1, r8), commit, pretick.
  - Unverified: `tick_start` (called indirectly), `frame_loop` 0x140dbca20, `physics_step` 0x140dbc500 (outside the dumped range), and 73b620 / end_step (no direct callers in the dump). This should be documented as a hard limitation.
- **N-5: harmless `extern "C" ... = {...}` warnings** (`.cpp:817-834`). There is also a dynamic initializer, `_GLOBAL__sub_I_g_ops_tick_start`. It runs at DLL load, before the hooks, so this is fine.

---

## 1. Prologue table: **FAIL**
The table compares `physics_tick_harness_prologues.h` in PR 3 with disk. The PR's values are identical to a35baa9 and are the placeholders.

| Site (header line) | VA | Disk (G3-prep) | PR 3 | Result |
|---|---|---|---|---|
| kTickStart (:12) | 0x14074b8f0 | 488bc4488958184889702055 | 48895c240848896c24104889 | ✗ |
| kIntegrator (:16) | 0x140746150 | 488bc44889581055488da838 | 48895c240848897424105748 | ✗ |
| kCommit (:20) | 0x14074d190 | 488bc4488958184889702055 | 40534883ec20488bd9488b89 | ✗ |
| kFrameLoop (:24) | 0x140dbca20 | 488bc4574881ecb000000033 | 48895c240848897424105741 | ✗ |
| kPhysicsStep (:28) | 0x140dbc500 | 488bc44889501041554883ec | 405553565741544155488dac | ✗ |
| kPreTick (:32) | 0x140749a30 | 488bc4488958104889781855 | 48895c241048896c24184889 | ✗ |
| kEndStep (:36) | 0x1407511e0 | 40534883ec50488b05b3b2e6 | 48895c240848897424105748 | ✗ |
| kCommitAuxA (:40) | 0x14073a070 | 4c8bdc55535741554157498d | 40534883ec30488bd9e80000 | ✗ |
| kCommitAuxB (:44) | 0x14073b620 | 488bc45657415641574881ec | 48895c240848897424105748 | ✗ |

The unit test cannot catch this, because it is built with NO_HOOKS and does not compare against disk.

**Stolen-byte / RIP-relative analysis**, using the real bytes and MinHook's minimum of 5 bytes, whole instructions only:
- tick_start, commit: `mov rax,rsp`(3) + `mov [rax+0x18],rbx`(4) = 7 bytes. No RIP-relative. OK.
- integrator: `mov rax,rsp` + `mov [rax+0x10],rbx` = 7. OK.
- pretick: `mov rax,rsp` + `mov [rax+0x10],rbx` = 7. OK.
- physics_step: `mov rax,rsp` + `mov [rax+0x10],rdx` = 7. OK.
- frame_loop: `mov rax,rsp`(3) + `push rdi`(1) + `sub rsp,0xb0`(7) = 11. OK.
- end_step: `push rbx`(2) + `sub rsp,0x50`(4) = 6. The next instruction, `mov rax,[rip+0xe6b2b3]` at +6, is RIP-relative but is **not** stolen. The 12-byte compare includes its disp32, which is fine because the image is not relocated (base 0x140000000). Even if it were stolen, MinHook relocates RIP-relative instructions. OK.
- 73a070: `mov r11,rsp`(3) + `push rbp` + `push rbx` = 5. OK.
- 73b620: `mov rax,rsp` + `push rsi` + `push rdi` = 5. OK.
- No site has a RIP-relative instruction split badly. Not verified: that no in-function branch targets the first 5-11 bytes. That is unlikely for these prologues.
- **Fix:** paste the "Disk" column into the header, and add a test that pins these exact hex strings.

## 2. Thunks: **FAIL**
The checks below were done on both the source (`src/core/physics_harness_detour_x64.S`) and the objdump of the built DLL. The two are byte-for-byte equivalent.

| Check | Result | Detail |
|---|---|---|
| RCX/RDX/R8/R9 + XMM0-3 saved before the pre-call C callback | PASS* | `.S:10-17` (0x38d34d40b-0x38d34d42e). *They are saved inside the callee's shadow space; see N-2. |
| Restored before calling the original | PASS | `.S:26-33` (0x38d34d442-0x38d34d465). |
| RAX + XMM0 preserved across the post-call callback | PASS | `.S:42-43` / `52-53` (0x38d34d478, 0x38d34d495). XMM0 saved as a full 128 bits. |
| RSP 16-aligned at every call | PASS | Entry RSP≡8, `push rbp`≡0, `sub 0xC0`≡0. The `call rax` instructions at 0x38d34d440, 0x38d34d476 and 0x38d34d493 all run with RSP≡0 mod 16. `movaps` to [rsp+0x30..0x80] is aligned. |
| 32-byte shadow space | PASS (with conflict) | [rsp+0..0x1F] is available, but it overlaps saved registers (N-2). |
| Callee-saved registers untouched/restored | PASS | Only RBP is used, and it is pushed/popped. RBX, RSI, RDI, R12-R15 and XMM6-15 are untouched. |
| Ops pointer (R11) valid after the calls | **FAIL (B-2)** | `.S:35`, `.S:45` read `[r11]` after the calls. |
| SEH/unwind | **FAIL (B-3)** | No `.seh_*` directives. No `.pdata` entry covers the thunk. |
| Re-entrancy guard | FAIL (N-3) | None in the thunk; there is only a counter in C. |
| Stack arguments (5th and later) | Not forwarded (N-4) | |
| Trampoline null means the original is skipped silently | Risk | `.S:37-38`: `je skip_orig`. If `*orig_trampoline==0` (Shutdown nulls it, `.cpp:1138-1146`), the game function is simply **not executed**. It should never skip; it should abort or spin. |

## 3. Real signatures and return values: **PARTIAL PASS**
The thunk forwards RCX, RDX, R8, R9 and XMM0-3, and returns RAX and XMM0. That covers everything seen, provided B-2 is fixed.

| Function | Arguments (evidence) | Return value used? |
|---|---|---|
| tick_start 0x14074b8f0 | rcx = rig/this; **xmm1 = dt** (`movaps xmm6,xmm1` at 0x14074b93d before any write to xmm1). Its home spills only touch [rax+0x18/0x20]. | Indirect call site, not checked. The thunk preserves RAX/XMM0 anyway. |
| integrator 0x140746150 | rcx = S, **xmm1 = dt** (callers: `movaps xmm1,xmm14` at 0x1407395ee, `movaps xmm1,xmm9` at 0x14073e304). | No. After 0x1407395fa comes `movss xmm0,[rbx]`; after 0x14073e314 comes `call 0x140748670`. |
| commit 0x14074d190 | rcx (callers 0x14074afd3 and 0x140751244; nothing float set right before). | No (the next instruction is a call). |
| 73a070 (cone) | rcx, **xmm1** (`movaps xmm1,xmm10` at 0x14073b683 / `xmm9` at 0x14073e323), **r8** = &local (`lea r8,[rsp+0x30]` at 0x14073b67e). | Not checked. Preserved. |
| pretick 0x140749a30 | rcx, **xmm1 probable** (caller 0x140731d0a: `movaps xmm6,xmm1`, call, `movaps xmm1,xmm6`). | Not checked. |
| 73b620, end_step, frame_loop, physics_step | rcx (physics_step also spills rdx, so ≥2 arguments). The rest are unverified (indirect calls, or outside the dump). | Not checked. |

No 5th or stack argument was found at any inspected site (N-4 caveat).

## 4. No-writes in self-test, plus the minor fixes: **PASS for no-write safety; the rest is PARTIAL**
- **Real writes are blocked in self-test: PASS.**
  - `LoadConfiguration` (`.cpp:982-988`) forces `writes=false` and `native=false`.
  - `AreWritesEnabled()` includes `!s_selfTestMode` (`.cpp:998`).
  - `ExecuteScheduledWriteIfDue` returns in the sham branch (`.cpp:500-510`) **before** the `memcpy` at `.cpp:538`.
  - `ExecuteScheduledNativeIfDue` returns at `.cpp:559`.
- **Writes are blocked when forced off without self-test: PASS.** `.cpp:512-515` checks, and `ScheduleWrite` refuses at `.cpp:1186`.
- **Sham-write path: never exercised in-game.** `ScheduleWrite` has no production caller; the only callers are in tests. `sham_writes` will always be 0, so the plan's "sham write before==after" criterion stays untestable.
- **INI trim: PASS.** `TrimIniToken` (`.cpp:165-189`) strips CR, LF, space and tab at the end, and space and tab at the start, for both key and value. A UTF-8 BOM on line 1 is still not handled (minor).
- **New timestamped CSV each run: PASS.**
  - The file is `dr2hook_physics_tick_harness_YYYYMMDD_HHMMSS.csv`, opened in `trunc` mode (`.cpp:370-393`).
  - It is opened lazily on the first valid sample, and closed on Shutdown so the next session gets a new file.
  - Two sessions in the same second would clobber each other (negligible).
- **600 ticks: PARTIAL.**
  - The threshold is `kSelfTestPassInStageTicks=600` (`.cpp:58`), and the report fires at ≥600 (`.cpp:622-635`).
  - However, `s_tickCounter` increments on every `tick_start` call regardless of player-chain validity, so the "in_stage" label is not enforced.
  - Suggested fix: count only ticks where `ResolvePlayerChain` succeeds.
- **PASS criteria documented: PARTIAL.**
  - `docs/physics_tick_harness.md:107-112` gives two criteria: the 4 required hooks installed, and ticks ≥600. `EvaluateSelfTestPassCriteria` (`.cpp:637-662`) implements exactly that.
  - It ignores re-entrancy, fire counts (M2/H6 are always 0, per B-4), thread-ID stability, sham writes and byte restoration after unhook.
  - My G3 grading will keep the stricter plan criteria and compute them from the CSV plus external memory reads.

## 5. Other risks
- **Writes from non-physics threads.** `allowWritePoints=true` on H1/H2 (frame_loop) and H5. If frame_loop runs on a different thread from physics, a future real write would race with physics. Restrict writes to B2/M1/M3/H6 and assert the thread ID.
- **Unhook and F8.**
  - `MH_DisableHook` restores the original bytes, which is good. The external check after F8 is still required.
  - Hazards:
    - (a) A thread inside a thunk, or inside an original called from the thunk, when the core DLL is unloaded will return into freed code.
    - (b) Shutdown nulls `g_orig*` after disabling. A thread already past entry but before the load at `.S:35` skips the game function silently.
    - (c) `MH_RemoveHook` frees the trampoline while a thread may still be executing it.
  - Recommendation: for G3, end with **game exit**, not F8, and do the byte check on a fresh launch without the harness. Otherwise add a quiesce step (an in-flight counter plus waiting for it to reach zero).
- **Unguarded reads of game memory.** `ResolvePlayerChain` / `LogBoundarySample` use raw `memcpy` through car→container→rig with no SEH guard. During stage load or unload, a stale pointer is an access violation on the physics thread. That is made worse by B-3, since there is no unwind through the thunk.
- **Logging thread safety: OK.** The CSV is under `s_csvMutex`, and `Logger` uses `s_mutex` (`logger.cpp:74,97`). The self-test report file is written from the physics thread.
- **Timing perturbation.** There is a `flush()` per boundary row (about 15 rows per tick) plus mutex and file I/O on the physics thread. Compare against the Phase 1a baseline, where the burst was under 1.5 ms.
- **Other hook sites.** The proxy (dxgi) holds its own MinHook instance and the 5 G0 UI detours. None overlaps the 9 physics sites.

## Required changes before G3 (summary)
1. Prologue bytes = disk values, plus a pinned test.
2. Thunk:
   - keep the ops pointer in a non-volatile location;
   - add `.seh_proc`/`.seh_*` unwind directives;
   - move the save area above the 0x20 shadow space;
   - make the `Frame` offsets match (or `static_assert` the offsets used in the asm);
   - never skip the original;
   - pass the caller's return address (`[rbp+8]`) to the callbacks for the M2/H6 filter.
3. Count "in-stage" ticks only while the chain is valid.
4. Restrict write points to physics-thread boundaries.
5. Rebuild and re-review the objdump. I can repeat this on the box in about 2 minutes.

---

# Re-review at head ccc9ff2 (2026-09-30 ~13:12-13:30 BRT)

- Source: fetched into `/workspace/pr3` and checked out at `ccc9ff2`. The new commits on top of c8eab24 are dbe8896, 2aad36e, 114a0bd, 71e2e65, 0658af0, c56e36e and ccc9ff2.
- Build: clean rebuild into `/workspace/build-pr3` with the `package_release.sh` flags. It succeeds with the same 9 `extern` warnings as before.
- Addresses in the built DLL: `PhysicsHarness_DetourCommon` at 0x38d34e240-0x38d34e303; the entry stubs follow at 0x38d34e304-0x38d34e393.
- Tests: `test_physics_tick_harness` built natively (Linux, NO_HOOKS) passes **51/51**. The other native test targets fail to compile because of box Wine headers (`rpcasync.h -Wchanges-meaning`). That is an environment problem, not the PR.

## OVERALL: **FAIL, still not deployable.** One old blocker remains and there are two new ABI regressions. Each one alone would corrupt or crash the game.

### Blocking issues
1. **R-1 (unchanged from B-1): the prologue bytes are still the placeholders.**
   - The concatenated 108 array bytes have identical MD5s at a35baa9 and at ccc9ff2 (`9547fcbe…`). Commit c56e36e only reformatted the header (`physics_tick_harness_prologues.h:14-63`).
   - The new test `TestBug1CalibratedPrologueHexPinned` (`tests/test_physics_tick_harness.cpp:~75-91`) pins **the placeholder values**, e.g. `"48895c240848896c24104889"` for tick_start where disk has `488bc4488958184889702055`. The test now *locks in* the bug.
   - None of the 9 sites matches the disk table in G3-prep.md, so nothing installs.
2. **R-2 (NEW regression): the thunk destroys the caller's callee-saved r12 and r13.**
   - Every entry stub (`.S:74-75`; e.g. 0x38d34e304 `lea r12,[rip+…]`, 0x38d34e30b `mov r13,[rsp]`) overwrites r12 and r13 **before** saving them.
   - `DetourCommon` stores those already-clobbered values at `[rsp+0x18]` (0x38d34e24b) and `[rsp+0x80]` (0x38d34e278) and **never restores** r12 or r13.
   - There is no push/pop of r12/r13 and no `.seh_pushreg r12/r13`. The claim that "r12/r13 are pushed/popped" is false.
   - Every hooked call returns to the game with r12 = &g_ops_* and r13 = the return address. That silently corrupts game state or crashes in the caller.
3. **R-3 (NEW regression): the ops pointer lives in shadow space and the hooked function overwrites it.**
   - The slot is `[rsp+0x18]`, and `physics_harness_detour_abi.h:35` even *asserts* it lies in the home area.
   - The callee called at 0x38d34e2c8 (MinHook trampoline, then the game original) owns thunk `[rsp+0x00..0x1F]` as its home area.
   - tick_start and commit both begin `mov rax,rsp; mov [rax+0x18],rbx; mov [rax+0x20],rsi` (disassembly line 45171-45173). With rax = thunk_rsp-8, `[rax+0x20]` is thunk `[rsp+0x18]`, the ops slot.
   - After the original returns, 0x38d34e2da `mov rax,[rsp+0x18]` loads the game's rsi. 0x38d34e2df `mov rax,[rax+8]` dereferences it, and 0x38d34e2ed `call rax` calls an arbitrary pointer. The result is a crash, or execution of an arbitrary address, on the first tick.
   - Callbacks may spill r9 home to the same slot as well (compiler-dependent).
   - Fix: keep ops and caller_return in the **local** area (≥ rsp+0x20, e.g. inside Frame), or in callee-saved registers that are properly pushed, popped and `.seh_pushreg`-ed.

### Item-by-item
| Item | Result | Evidence |
|---|---|---|
| (a) Arrays match disk + test pins them | **FAIL** | R-1. The test pins the wrong values. |
| (b) No volatile register across calls; ops/caller_return survive 3 calls | **FAIL** | r10/r11 are no longer used (PASS on that part). The ops slot is clobbered by the original (R-3). caller_return (frame+0x60 = rsp+0x80) is above the shadow space and survives, but r12/r13 are destroyed (R-2). |
| (c) Callee-saved push/pop + SEH | **FAIL** | Only rbp is pushed: `.seh_pushreg rbp` then `.seh_stackalloc 0xC0`, matching `push rbp` @0 and `sub rsp,0xc0` @4-10. `.seh_endprologue` is present. `.pdata`: RUNTIME_FUNCTION 0x38d34e240-0x38d34e304 covers the whole common thunk. `.xdata` decodes to ver 1, prolog 11, codes `@+11 ALLOC_LARGE 0xc0`, `@+1 PUSH_NONVOL rbp`, which is correct for what is actually pushed. The entry stubs have no pdata but change nothing on the stack (leaf-like, acceptable). r12/r13 are used without push, pop or pushreg (R-2). |
| (d) Alignment, shadow, Frame, restores | **PASS** | RSP≡0 mod 16 at 0x38d34e292/2c8/2ed. The 0x20 shadow space is free except for the ops slot (R-3). Frame is at rsp+0x20 (16-aligned). Offsets: rcx/rdx/r8/r9 at +0x20..0x38, xmm0-3 at +0x40..0x70, caller_return +0x80, rax +0x88, xmm0_ret +0x90. These equal the `static_assert`s (header :40-52, sizeof 0x80). Args are restored at 0x38d34e294-2b7 before the original. rax/xmm0 are saved at 0x38d34e2ca/2d2 and restored at 2ef/2f7. |
| (e) caller_return → M2/H6 filters | **PASS (logic)** | `HarnessAfterIntegrator` uses `frame->caller_return` (`.cpp:876-884`). The stub reads `[rsp]` at detour entry, which is the game return address because MinHook enters via jmp. The RVAs are `0x7395FA` / `0x73E314`. These are the correct **return** addresses: the calls are at 0x1407395f5 and 0x14073e30f, and 0x1407395f5 would be wrong. `s_gameBase +` handles relocation; the base is 0x140000000 anyway. |
| (f) Null original | **Changed: no skip, but a crash** | The `test/je` was removed. 0x38d34e2c5-2c8 is `mov rax,[rax]; call rax`, so null means `call 0`, an access violation. `InstallHookSite` rejects a null trampoline after enable (`.cpp:995-1002`). `Shutdown` still nulls `g_orig*` after `MH_DisableHook` (`.cpp:1265-1271`), so a thread already inside the thunk crashes. Better: never null them while hooks may be in flight. |
| (g1) In-stage-only ticks | **PASS** | `s_inStageTickCounter` increments only when `ResolvePlayerChain` is true (`.cpp:870-873`). PASS uses it (`:747, :760`). |
| (g2) Sham path callable in-game | **PASS, but weak** | `InvokeSelfTestShamWritePath` is called from `OnBoundary` (`.cpp:735`). It only **logs** (`PerformShamWrite`, `.cpp:373-383`) and does not read before/after bytes, so it proves no before==after invariant. |
| (g3) Self-test cannot write | **PASS** | The sham path returns before `memcpy` (`.cpp:619-622` vs `:653`). Native returns (`:677`). `AreWritesEnabled` includes `!selfTest`. |
| (h) Tests | **PASS (51/51)** | But the prologue test pins the wrong bytes, and NO_HOOKS means no thunk coverage. |

### Fixed since c8eab24
- SEH/pdata/xdata correct for what is pushed.
- r11 no longer used.
- Frame layout equals the asm, and the save area is above the shadow space.
- caller_return plumbing and the M2/H6 filter.
- In-stage counting.
- Guarded reads (`VirtualQuery`, `.cpp:301-359`). This costs a syscall per read on the physics thread, so recheck timing.
- CSV batched flush every 32 rows (`.cpp:87, :597`).

### Regressions since c8eab24
- R-2 (r12/r13 clobber) and R-3 (ops slot in home area) are **new** and worse than the old r11 bug.
- A null original now crashes instead of being skipped. That is acceptable only if Shutdown never nulls pointers in flight.

### Minimal correct thunk shape (suggested)
```
push rbp            ; .seh_pushreg rbp
push r12            ; .seh_pushreg r12
push r13            ; .seh_pushreg r13
sub  rsp, 0xC0      ; .seh_stackalloc 0xC0   (entry≡8; after 3 pushes ≡0; 0xC0 keeps ≡0)
.seh_endprologue
; stubs pass ops in r11 (volatile, fine before any call) and must NOT touch r12/r13
mov r12, r11        ; ops, now in a saved callee-saved reg
mov r13, [rsp+0xC0+0x18] ; caller return = original [rsp] at entry
... save args at rsp+0x20.., calls ..., restore ...
add rsp, 0xC0 ; pop r13 ; pop r12 ; pop rbp ; ret
```
Alignment must be recomputed: entry≡8, then three pushes give ≡0, so the allocation must be ≡0 mod 16 (0xC0 works). The `.seh_*` directives must match each push exactly.

---

# Re-review at head 6f088f0 (2026-09-30 ~13:23-13:45 BRT)

- Source: fetched the latest tip into `/workspace/pr3` (HEAD = `6f088f0`; new since ccc9ff2: 4aecc0e, 6f088f0).
- Build: clean rebuild into `/workspace/build-pr3` with the `package_release.sh` flags. It succeeds with the same 9 `extern` warnings. `DetourCommon` is at 0x38d34e240-0x38d34e323; the stubs follow.
- Tests:
  - Native `test_physics_tick_harness`: **53/53**.
  - The repo's `scripts/run_physics_detour_wine_test.sh` under box Wine: **PASS**.
  - My stronger Wine test (`/tmp/wt2/t.cpp`, not in the repo): **PASS**, with one finding (N-A below).

## OVERALL: **PASS for G3 (self-test, no writes)**, under the conditions below
No blockers remain. It is **safe to proceed to G3 in self-test mode (no writes)**, provided:
- The DLLs are built from the PR branch tip `6f088f0` (or from main after merge). The user's local checkout is at a35baa9, which does **not** contain these fixes.
- The run ends by **exiting the game, not F8** (see N-C).
- `self_test=1` is the only harness key; no `writes=` or `experimental_native=` lines.
- After the user's GCC 16 build, re-run `scripts/verify_physics_harness_dll.sh` / objdump on *that* DLL. The thunk is hand-written asm and should be byte-identical, but confirm it.

## (a) Prologue arrays + pinning test: **PASS**
- Each of the 9 header arrays (`physics_tick_harness_prologues.h:16-60`) was compared programmatically with the G3-prep disk table: all equal.
- The pinning test (`tests/test_physics_tick_harness.cpp:75-91`) uses the same 9 disk hex strings: all equal.
- `kEndStepPrologueLength = 12`, the same as the others.

## (b) Register audit: **PASS** (one non-blocking finding, N-A)
Objdump of `DetourCommon` (0x38d34e240):
```
push rbp / push r12 / push r13 / mov rbp,rsp / sub rsp,0xc0
[rsp+0xa8]=r11 (ops) ; r13=[rbp+0x18] (caller ret) ; save rcx,rdx,r8,r9 @+0x20..0x38, xmm0-3 @+0x40..0x70 ; [rsp+0x80]=r13
call before(&frame)                         @0x38d34e2a0
restore rcx..r9, xmm0-3 ; r12=[rbp+8] ; r13=[rbp+0x10] ; rax=[[rsp+0xa8]+0x10] ; call [rax]   @0x38d34e2e1
[rsp+0x88]=rax ; [rsp+0x90]=xmm0 ; call after(&frame)   @0x38d34e309
rax=[rsp+0x88] ; xmm0=[rsp+0x90] ; mov rsp,rbp ; pop r13 ; pop r12 ; pop rbp ; ret
```
- **The stubs set only r11** (e.g. 0x38d34e324 `lea r11,[rip+…]`, then `jmp`). r11 is consumed before the first call. PASS.
- **Nothing the thunk needs after a call is volatile or in the callee's shadow space.** After each call it reads only `[rsp+0x20..0xa8]` and `[rbp+…]`. rbp is callee-saved and preserved by every callee, and all slots are ≥ rsp+0x20. PASS.
- **Args restored before the original:** rcx, rdx, r8, r9 and xmm0-3 at 0x38d34e2a2-2c5. **rax and xmm0 preserved after it:** 0x38d34e2e3/2eb, then 0x38d34e30b/313. PASS.
- **Game-visible callee-saved registers on return:**
  - rbp, r12 and r13 are pushed and popped in the correct order.
  - rbx, rsi, rdi, r14, r15 and xmm6-15 are never touched by the thunk, and the callbacks and original preserve them under the ABI.
  - Dynamic confirmation: the repo test and my test check rbx, rsi, rdi, r12-r15, xmm6 and xmm15 on return, plus rax=0x1234567890ABCDEF and xmm0=1.0. All pass, **even with callbacks that trash rcx, rdx, r8-r11, xmm0-5 and the home area, and call `GetAsyncKeyState`/`Sleep` (syscalls)**.
- **N-A (non-blocking): the second restore uses the wrong slot.** `mov r13,[rbp+0x10]` at 0x38d34e2ce loads the **saved rbp**, not the saved r13; the saved r13 is at `[rbp+0x0]`. My test confirms that inside the original r13 = 0x7ffffe2ffe90 (a stack address), not 0xB…0D. The repo test checks only rbx and r12 inside the dummy (`test_physics_harness_detour_wine.cpp:98`), which is why it doesn't catch this.
  - It is harmless under the Win64 ABI: the incoming values of callee-saved registers are not inputs, MSVC code never reads r13 before saving it, and the final `pop r13` restores the correct value for the game.
  - SEH unwinding is also fine: the thunk's own unwind codes restore r13 from `[rbp+0]`.
  - Fix at leisure: use `[rbp+0x0]`, or drop both restores, since they're unnecessary.

## (c) Stack layout: **PASS**
- At entry (reached by jmp from the MinHook relay) E≡8 mod 16 and `[E]` is the game return address. After three pushes, rbp = E-0x18, so `[rbp+0x18]` = `[E]`, **the game's return address**.
  - Dynamic check: `caller_return = 0x14000176b`, which lies inside `WineDetourRunHookedCall` (0x140001610), right after its `call rax`.
  - For the real sites, the M2/H6 filters compare against 0x1407395fa / 0x14073e314 (base + RVA). These are the correct return addresses.
- The original is **called** from the thunk (0x38d34e2e1), so its return address is inside the thunk. Its home area is thunk `[rsp+0x00..0x1F]`, which is pure scratch: the thunk keeps nothing there.
- Stack arguments (5th and later) are **not forwarded**. The original's `[entry+0x28..]` would read the thunk's saved rcx and so on. I checked every hooked function:
  - Static scan of the 7 functions in the dump, tracking entry-relative rsp, rbp and rax/r11 through each prologue and body up to the `.pdata` end: **0 accesses at entry+≥0x28** for tick_start, integrator, commit, pretick, end_step, 73a070 (first chunk) and 73b620.
  - Call-site evidence: integrator (rcx, xmm1), 73a070 (rcx, xmm1, r8), commit and pretick (rcx; pretick probably also xmm1).
  - physics_step 0x140dbc500 is called at 0x1404b1092 with xmm0=dt and rdx=[this+0x40] (static addendum), i.e. 2 register args.
  - frame_loop 0x140dbca20 is called at 0x1404b1110 with no stack args noted.
  - **No hooked function takes more than 4 args.** Caveat: frame_loop and physics_step bodies are outside the box dump, so they are confirmed from the static report only.

## (d) Alignment and shadow space: **PASS**
- E-0x18-0xC0 = E-0xD8 ≡ 0 mod 16, so RSP is aligned at all three calls (0x38d34e2a0, 2e1, 309).
- `[rsp+0..0x1F]` is left free as shadow space. The save area starts at +0x20 (Frame, 16-aligned) and ends at +0xA0, with the ops slot at +0xA8, all within 0xC0.
- `static_assert`s in `physics_harness_detour_abi.h:34-52` match the asm offsets, including `kDetourOpsStackSlotOffset(0xA8) ≥ 0x20+sizeof(Frame)`.

## (e) SEH: **PASS** (one nit)
- `.pdata`: RUNTIME_FUNCTION 0x38d34e240-0x38d34e324 (len 0xe4) covers the whole common thunk. The stubs don't touch RSP (lea+jmp), so leaving them without pdata is fine.
- `.xdata` (unwind RVA 0x24ecc4): ver 1, prolog 15, 5 slots:
  - `@+15 ALLOC_LARGE 0xC0` matches `sub rsp,0xc0` at +8..+15.
  - `@+5 PUSH_NONVOL r13` matches `push r13` at +3..+5.
  - `@+3 PUSH_NONVOL r12` matches +1..+3.
  - `@+1 PUSH_NONVOL rbp` matches +0..+1.
  - The order and offsets are exact. `mov rbp,rsp` is not a frame register (no `.seh_setframe`), which is fine because the body is RSP-based.
- **Nit:** the epilogue `mov rsp,rbp` is not a canonical x64 epilogue form. Only an *asynchronous* unwind landing exactly on the last 4 instructions (a profiler or suspend-and-walk) would mis-unwind. Normal exceptions and MinHook thread fix-ups are unaffected. Suggest `add rsp,0xC0`.

## (f) MinHook stolen bytes: **PASS**
The vendored MinHook writes a 5-byte `JMP_REL` (`hook.c:397-413`). hde64 copies whole instructions, and only ModR/M-RIP-relative ones are relocated (`trampoline.c:150`).

| Site | Stolen instructions | Bytes | RIP-relative stolen? |
|---|---|---|---|
| tick_start 0x14074b8f0 | mov rax,rsp; mov [rax+0x18],rbx | 7 | no |
| integrator 0x140746150 | mov rax,rsp; mov [rax+0x10],rbx | 7 | no |
| commit 0x14074d190 | mov rax,rsp; mov [rax+0x18],rbx | 7 | no |
| frame_loop 0x140dbca20 | mov rax,rsp; push rdi; sub rsp,0xb0 | 11 | no |
| physics_step 0x140dbc500 | mov rax,rsp; mov [rax+0x10],rdx | 7 | no |
| pretick 0x140749a30 | mov rax,rsp; mov [rax+0x10],rbx | 7 | no |
| end_step 0x1407511e0 | push rbx (40 53); sub rsp,0x50 | 6 | no. `mov rax,[rip+0xe6b2b3]` at +6 is **not** stolen and runs in place. |
| 73a070 | mov r11,rsp; push rbp; push rbx | 5 | no |
| 73b620 | mov rax,rsp; push rsi; push rdi | 5 | no |

- No branch in the dumped functions targets the interior of a stolen range.
- `mov rax,rsp` / `mov r11,rsp` in the trampoline capture thunk_rsp-8. Everything after is relative to that (homes at thunk `[rsp+0x00..0x1F]`, `lea rbp,[rax-X]`), so the original's frame is self-consistent on the thunk's stack.
- The repo Wine test reproduces exactly the tick_start prologue through MinHook and passes.

## (g) Wine test: **PASS; the repo test's assertions are narrow**
- The repo test checks, after return: rbx, rsi, rdi, r12-r15, xmm6, xmm15, rax and xmm0.
- Inside the dummy it checks rcx≠0, rdx, r8, r9, rbx and r12.
- It does **not** check xmm1 (dt), xmm7-14, or r13 inside the original.
- It runs with **both callbacks null**, so the callback path is not exercised.
- Its caller `WineDetourRunHookedCall` itself clobbers main's non-volatile registers (a test-harness nit).
- My extended test fills those gaps: non-null callbacks that trash volatile registers and call syscalls. It confirms:
  - xmm1 = 0x3FE8… (0.75) arrives in the original, as do xmm0/2/3 and rdx/r8/r9;
  - `after` sees `frame->rax` = the original's return value;
  - `caller_return` is correct;
  - only r13-in-original is off (N-A).

## (h) Previous items, status
- Shutdown nulling: **fixed** (`.cpp:1271`, comment only).
- Null trampoline: rejected at install (`.cpp:995-1002`). The thunk has no skip path, and pointers now stay valid.
- In-stage ticks, guarded reads and the self-test no-write path: unchanged, PASS.
- Still open (non-blocking for self-test):
  - **N-B:** the sham path only logs; it proves no before==after.
  - **N-C:** F8 unload while a thread is inside the thunk or trampoline returns into freed code, and `MH_RemoveHook` frees the trampoline. **For G3, end by exiting the game, not F8.** The byte-restore check then becomes "disk == memory at a fresh launch without the harness", which is trivially true, or is done before the exit with the hooks still enabled (expect `e9` at the 9 sites).
  - **N-D:** write points on frame_loop/H5 may be off the physics thread. Irrelevant with writes off.
  - **N-E:** timing perturbation. There are about 19 `VirtualQuery` calls per boundary row, about 15 rows per tick, and a flush every 32 rows. Measure against the Phase 1a tick-burst baseline (<1.5 ms per 16.7 ms tick).
  - **N-F:** 0x140dbca20 is labelled "frame_loop" in the harness but is the per-container sync / PostPhysicsTask (KB glossary C). Naming only.
- Regressions vs ccc9ff2: **none found.** R-1, R-2 and R-3 are all fixed.
