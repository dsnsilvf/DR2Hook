# G3 prep: physics tick harness self-test (reconnaissance only)

Date: 2026-09-30, 12:41-12:50 BRT. Machine: cachyos-x8664 (`79dac8ac-...`).
Scope: read-only. Nothing was built, copied, installed or edited, and the game process (PID 4113076, still running) was not touched.
Sources:
- 4 read-only commands on the user machine: git, toolchain, `ls`/`sha256sum` of the game folder, Steam `localconfig.vdf` LaunchOptions, `grep -c` on the DLLs, and 12-byte reads of `dirtrally2.exe` on disk.
- The repository at commit `a35baa9`, read through the GitHub connector: `docs/physics_tick_harness.md`, `README.md`, `docs/INSTALL.md`, `scripts/package_release.sh`, `CMakeLists.txt`, `src/core/physics_tick_harness.cpp`, `src/core/core_module.cpp`, `include/dr2hook/physics_tick_harness_prologues.h`, `include/dr2hook/physics_harness_addresses.h`.

## TL;DR: G3 is NOT ready to run. There are two blockers in the harness code.

**B1. All 9 hook prologue constants are wrong for this exe.** Required hooks mismatch, so `TryInstall` aborts and the harness installs nothing. This is harmless, but G3 would FAIL with no data.

`include/dr2hook/physics_tick_harness_prologues.h` (12 bytes each) compared with `dirtrally2.exe` on disk (the same file verified in G0):

| Site | VA | Actual (disk) | Expected in header | |
|---|---|---|---|---|
| tick_start (required) | 0x14074b8f0 | `488bc4488958184889702055` | `48895c240848896c24104889` | MISMATCH |
| integrator (required) | 0x140746150 | `488bc44889581055488da838` | `48895c240848897424105748` | MISMATCH |
| commit (required) | 0x14074d190 | `488bc4488958184889702055` | `40534883ec20488bd9488b89` | MISMATCH |
| frame_loop (required) | 0x140dbca20 | `488bc4574881ecb000000033` | `48895c240848897424105741` | MISMATCH |
| physics_step | 0x140dbc500 | `488bc44889501041554883ec` | `405553565741544155488dac` | MISMATCH |
| pretick | 0x140749a30 | `488bc4488958104889781855` | `48895c241048896c24184889` | MISMATCH |
| end_step | 0x1407511e0 | `40534883ec50488b05b3b2e6` | `48895c240848897424105748` | MISMATCH |
| commit_log_73a070 | 0x14073a070 | `4c8bdc55535741554157498d` | `40534883ec30488bd9e80000` | MISMATCH |
| commit_log_73b620 | 0x14073b620 | `488bc45657415641574881ec` | `48895c240848897424105748` | MISMATCH |

The expected values look like generic placeholders. With the current code, `InstallHookSite("tick_start")` fails at `VerifyHookPrologue`. The log shows `prologue mismatch em tick_start … esperado=… actual=…` followed by `nao instalado`. No MinHook call is made, and there is no self-test report file (`LogSelfTestReport` runs only after a successful install).

Fix: replace the constants with the "Actual" column.
- end_step's bytes 6-11 contain a RIP-relative displacement (`mov rax,[rip+…]`). That is fine because the image is not relocated (base 0x140000000).

**B2. Once B1 is fixed, the detours would corrupt physics.** They clobber the float `dt` argument in xmm1. This matters because a "no-writes" self-test would still change game state.

All detours are declared `void __fastcall Detour(void *a1, void *a2, void *a3, void *a4)`. They forward only RCX/RDX/R8/R9. Before calling the original, they run `OnBoundary()`, which does:
- `LogBoundarySample`
- `std::ofstream <<` float formatting
- `flush`
- `GetAsyncKeyState` in tick_start

On MS-x64, xmm0-xmm5 are volatile, so xmm1 will almost certainly be overwritten.

Static evidence that the hooked functions take xmm1 as an argument (`/workspace/analyst/dis`):
- **tick_start** `0x14074b8f0`: `14074b93d: movaps xmm6,xmm1` before any write to xmm1, so xmm1 (dt) is an input.
- **integrator** `0x140746150`: callers do `1407395ee: movaps xmm1,xmm14` and `14073e304: movaps xmm1,xmm9` right before the call. The static report says: "rcx = S, xmm1 = dt" (CONFIRMED).
- **commit_log_73a070** (cone): callers do `movaps xmm1,xmm10` / `movaps xmm1,xmm9`.
- **commit_log_73b620**: its caller does `movaps xmm1,xmm6`.
- **pretick** `0x140749a30`: the caller preserves its own xmm1 in xmm6 around the call, which suggests a float argument (PROBABLE).

Consequence: the integrator and tick start would run with a garbage dt. Expect a NaN or exploding car, a teleport, or a crash, which trips the plan's abort criteria.

There is also a secondary risk. The post-call `OnBoundary` clobbers RAX/XMM0, and all detours return `void`. The call sites of commit and the integrator don't use the return value. frame_loop, physics_step and end_step were not checked.

Fix options (either one):
- (a) Detours with the true prototypes, e.g. `(void* rig, float dt)` for tick_start, the integrator and 73a070.
- (b) A small asm thunk per site that saves and restores RCX, RDX, R8, R9 and XMM0-XMM3 around the pre-call hook, and RAX/XMM0 around the post-call hook.

Verify after the build with `x86_64-w64-mingw32-objdump -d` on the `Detour*` symbols: xmm1 must reach the tail call to the original unchanged. The unit test cannot catch this, because on Linux the test is built with `DR2HOOK_PHYSICS_HARNESS_NO_HOOKS`.

Other code observations (not blockers):
- **Self-test window timing.** The 5 s self-test window starts when `Core_Initialize` runs, which is game start in the menus. There the player chain is invalid and `LogBoundarySample` returns early, so the report will likely show `fires=0` for everything. In-stage evidence has to come from the CSV. Alternatively: start the game without the ini, enter the stage, create the ini, press F8. The core then reloads, `TryInstall` runs, and the 5 s window falls inside the stage. That means installing hooks on a live physics thread; MinHook suspends threads while doing it.
- **Naming differs from the plan.** The harness calls tick-start entry "B1", but the plan calls it B2. Harness "B2" is after Commit. The harness integrator hook M1/M2 fires for **both** the look-ahead and the real integrate. Harness **H6** is the look-ahead return (`0x14073e314`), not the plan's M2 (`0x1407395fa`, the real integrate). The CSV analysis must map these.
- **What the self-test does not cover from the plan's G3.** It does not satisfy the plan's G3:
  - The 5 s window is about 300 ticks, and G3 requires 600 or more.
  - There is no re-entrancy counter.
  - There is no sham-write path (self-test forces writes off).
  - There is no check that the code bytes are restored after unhook.
  - These must come from CSV analysis plus an external read-only check of the 9 sites after the hooks are removed.
- **CSV load.** Every boundary does `flush()` on the physics thread. That is about 15 or more fires per tick at 60 Hz, which is timing perturbation to compare against R1.
- **Separate MinHook copies.** `dxgi.dll` and `dr2hook_core.dll` each link their own MinHook copy. The harness uses the core's copy, and the proxy already patches 5 other sites (G0). F8 unhooks with `MH_DisableHook/MH_RemoveHook` while the physics thread may be running.

---

## (1) Checkout
- Path: `/home/deivison/Projetos/DR2ModLoader`. Remote `git@github.com:dsnsilvf/DR2ModLoader.git`, branch `main`.
- HEAD = `a35baa9` "Merge pull request #1 from dsnsilvf/cursor/physics-tick-harness-739e", 2026-09-30 12:40:54 BRT. **HEAD contains a35baa9.**
- `git status`: `## main...origin/main` with no changes, so the **tree is clean**.
- Existing build dirs: `build/`, `build-release/` (CMakeCache from 2026-09-28, Release, `x86_64-w64-mingw32-g++`, Unix Makefiles), and `dist/`. The DLLs in `build-release/` are from 08:41, **before the harness**.

## (2) Build
Official (README "Linux, release DLLs only"): `bash scripts/package_release.sh`. It runs:
```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_SHARED_LINKER_FLAGS="-static -static-libgcc -static-libstdc++" .
cmake --build build-release --target dxgi --target dr2hook_core -j"$(nproc)"
```
It then strips the DLLs into `dist/DR2Hook-v0.1.0/` and zips them. `zip` is missing, so it falls back to `cmake -E tar`.

The full check including unit tests is `bash scripts/verify_release.sh`.

Recommended for G3: the same two cmake commands with `-B build-g3`, so `build-release/` stays as the known-good fallback.

Toolchain: **installed**.
- mingw-w64-gcc 16.2.0-2.1 (`x86_64-w64-mingw32-gcc/g++` 16.2.0)
- mingw-w64-binutils 2.47, crt/headers/winpthreads 14.0.0
- cmake 3.31.5 (`~/.local/bin`), GNU make 4.4.1
- ninja missing (not needed), zip missing (only needed for packaging)

**Both DLLs must be rebuilt, and the game restarted.** Commit a0dd164 at 12:34 BRT touched proxy/host files, and `dxgi.dll` cannot be hot-reloaded.

## (3) Deployment (current state)
- Method: a **`dxgi.dll` proxy** in the game folder, which loads `dr2hook_core.dll` from the same folder. There is no injector.
- Steam Launch Options (userdata 1434155510): `WINEDLLOVERRIDES="dxgi=n,b" %command%`. The running game's environment confirms `WINEDLLOVERRIDES=dxgi=n,b;…`.
- The prefix's `system32/dxgi.dll` (4,882,432 B) is Proton's.

Game folder `/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0/`:

| File | Size | mtime (BRT) | SHA-256 |
|---|---|---|---|
| dxgi.dll | 13,058,927 | 2026-09-30 08:41:50 | `359be0c89410b17572e8df225aa1c243b9a24efee41509cd5c50daaf7a774292` |
| dr2hook_core.dll | 17,971,484 | 2026-09-30 08:41:50 | `2f505ba2d240388c5eb3e5d5e13668b4ea1e35a81f7ff40765870d6dabcc68d4` |
| dr2hook_core.1.dll (F8/host copy) | 17,971,484 | 08:41:50 | same as core |
| dr2hook_core.dll.bak-20260929 | 2,419,214 | 09-28 14:36 | |
| dxgi.dll.bak-20260929 / .bak-pausemenu | 1,040,910 / 12,779,250 | 09-28 18:26 / 09-29 22:05 | |
| mods/practice_mode/ | | 09-28 | |
| dr2hook.log | 15,000 | 12:34:14 | |

- Neither deployed DLL contains the string `PhysicsTickHarness` (grep count 0), so **the deployed DLLs predate the harness**.
- There is no `dr2hook_physics_harness.ini` and no harness CSV in the folder.

## (4) Enabling self-test with no writes
- **INI (recommended; reversible, no Steam config edit):** create `<game dir>/dr2hook_physics_harness.ini` containing exactly:
  ```
  self_test=1
  ```
  - Use **LF line endings, no spaces, no BOM**. The parser does `line.substr(0,eq) == key` and `ParseTruthy(value)` with no trimming, so `self_test = 1` or a trailing `\r` is silently ignored.
  - Do not add `writes`/`experimental_native` lines. Self-test forces them off anyway (`s_writesEnabled=false`, `s_experimentalNativeEnabled=false`), and `ExecuteScheduled*IfDue` returns early when `s_selfTestMode`.
- **Env alternative:** `DR2HOOK_PHYSICS_HARNESS_SELF_TEST=1` added to the Steam Launch Options before `%command%`. Make sure `DR2HOOK_PHYSICS_HARNESS_WRITES` and `DR2HOOK_PHYSICS_HARNESS_EXPERIMENTAL_NATIVE` are unset.
- **Outputs**, all in the game folder (the exe directory):
  - `dr2hook.log`: `PhysicsTickHarness:` lines (addresses, `hook instalado em …`, `prologue mismatch …`, `self-test hook X installed=0/1`, `self-test boundary Y fires=N`).
  - `dr2hook_physics_harness_self_test.log`: written after install and again after 5 s (truncates each time).
  - `dr2hook_physics_tick_harness.csv`: **append mode**, so move any old one aside first. One row per boundary fire while the player chain is valid. Columns: `tick,step,boundary,thread_id,container,rig,key_f5,key_f6,key_f7`, vec4 at rig+0x170/180/200/210/2b0/2c0/2d0/2e0/320/330, scalars +0x2508/+0x1338, container+0xC930 vec4, rig+0x290 vec4.
- **PASS per docs:** the docs define **no explicit PASS criterion**. Derived from code and docs:
  1. `modo self-test (instrumentacao ligada, sem writes)` and `self-test writes=0 (forcado)` appear.
  2. All 4 required hooks (tick_start, integrator, commit, frame_loop) show `installed=1`, with no `prologue mismatch` / `MH_CreateHook falhou` / `nao instalado`.
  3. Boundary fires > 0 (from the CSV if the 5 s window fell in the menu).
  4. No `write tick=` / `native experimental` lines.
  - Optional hooks may be 0.
- **Plan G3 additionally requires** (Validation Specialist grading from the CSV plus external reads):
  - ≥600 ticks
  - phase order per plan §3.3, after mapping the naming
  - stable `thread_id`
  - 0 re-entrancy; there is no counter, so it is inferred from boundary nesting
  - physics sanity: dt / p continuity with the R1 baseline of 60 Hz and dt=1/60; no NaN
  - hooks removed on exit/F8 with code bytes back to disk (external read of the 9 sites)
  - The sham-write criterion is **not testable** with this harness, so it is PENDING.

## (5) Minimal reversible plan (execute only after B1+B2 are fixed and merged, and with the user's go-ahead)
With `G="/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0"` and `R=/home/deivison/Projetos/DR2ModLoader`:
0. **Preconditions:**
   - B1 fixed: prologues equal the "Actual" column.
   - B2 fixed: xmm1/args preserved, verified with objdump.
   - `git pull`; the tree is clean.
1. **Build** (no install):
   ```bash
   cd "$R"
   cmake -B build-g3 …   # same flags as in (2)
   cmake --build build-g3 --target dxgi --target dr2hook_core -j"$(nproc)"
   ```
   Then: `x86_64-w64-mingw32-objdump -d build-g3/dr2hook_core.dll` → check the `Detour*` functions, and check that the new DLL contains the harness strings.
2. **Exit the game completely.** Do not overwrite a DLL that the running process has mapped: `cp` over a mapped file changes its pages in place.
3. **Back up:**
   ```bash
   B="$G/_bak_g3_$(date +%Y%m%d-%H%M%S)"
   mkdir "$B"
   cp -p "$G/dxgi.dll" "$G/dr2hook_core.dll" "$G/dr2hook.log" "$B/"
   sha256sum "$B"/*.dll   # must equal 359be0c8… / 2f505ba2…
   ```
4. **Deploy** (new inode, atomic):
   ```bash
   install -m755 "$R/build-g3/dxgi.dll" "$G/dxgi.dll.new" && mv -f "$G/dxgi.dll.new" "$G/dxgi.dll"
   # same for dr2hook_core.dll
   rm -f "$G"/dr2hook_core.[0-9]*.dll   # stale host copies, optional
   ```
5. **Enable:**
   ```bash
   printf 'self_test=1\n' > "$G/dr2hook_physics_harness.ini"
   ```
   If an old `dr2hook_physics_tick_harness.csv` exists, move it aside. Keep the Launch Options unchanged.
6. **Run:**
   - The user launches the game from Steam and enters an offline stage (DirtFish / offline Time Trial).
   - Drive about 20 s, stop, and wait 10 s at rest. Do **not press F5-F7** (they are logged in the CSV anyway).
   - Re-check G1/G2 read-only before and after.
   - I then collect the log, the self-test log and the CSV, plus a read-only check of the 9 hook sites in `/proc/<pid>/mem`, which should show `e9` jumps while hooked.
7. **Unhook and verify.** Either exit the game, or delete the ini and press F8 (the core reloads without the harness, and `Shutdown` removes the hooks). After F8, check read-only that the 9 sites equal the disk bytes again.
8. **Roll back (any time, with the game closed):**
   ```bash
   cp -p "$B/dxgi.dll" "$B/dr2hook_core.dll" "$G/"
   rm -f "$G/dr2hook_physics_harness.ini"
   ```
   Keep the CSV and logs as evidence (copy them to the box). Verify the SHA-256s equal `359be0c8…` / `2f505ba2…`. `build-release/` stays untouched as a second fallback.
- **Abort** (the user exits the game) on: NaN/teleport/explosion, a crash, `prologue mismatch` on a required hook, or any `write tick=` line.
