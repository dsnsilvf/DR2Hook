# Task: INV-01 instrumentation harness for DR2Hook

Build an opt-in, instrumentation-only harness for DR2Hook (DiRT Rally 2.0 modding framework: C++20, dxgi.dll proxy + hot-reloadable dr2hook_core.dll, MinHook, CMake with MinGW cross-compile). It is for controlled reverse-engineering experiments on the physics tick. Work on a new branch and open a PR. The user's local main has unpushed work, so keep the change self-contained and easy to rebase (a new module or files plus minimal wiring). Don't refactor existing code.

Attached files:
- INV-01-experiment-plan.md: the experiment plan (hook points B1/B2/M1-M3, experiments I1-I5, W0-W11, A1-A4, log format).
- INV-01-static-report.md: disassembly-based analysis with addresses.
- INV-01-phase1a.md: runtime observations of the tick order.

## Requirements
Check the details against the plan. Where the plan disagrees with this task, the plan wins.
1. Off by default. It turns on only through an explicit config flag or environment variable, and never changes normal behavior.
2. MinHook detours at the plan's tick boundaries. Known addresses (image base 0x140000000, PE timestamp 0x605cbae3): integrator 0x140746150, tick start 0x14074b8f0, Commit 0x14074d190, per-tick caller 0x1407511e0, frame loop 0x140dbca20. Verify the prologue bytes before installing each hook and refuse to install on a mismatch, the same way the existing UI hooks do.
3. A monotonic tick counter, plus CSV logging of the named rig fields at each boundary, following the plan's log template where feasible. Rig chain: [exe+0x1681ce8] -> car; [car+0x30] -> container; [container+0x08] -> rig. Validate that qword [rig+0x12c0] == rig and dword [rig+0x12d0] == 4.
4. Scheduling of a single queued write (field offset, value, boundary, tick). The write executes ON the physics thread at that boundary, never from Present. Log the value before and after every write.
5. Optional, experimental calls to the native state API addresses from the static report (SetTransform, SetLinVel, SetAngVel, Commit, ...), executed only at a chosen boundary.
6. Gated by the SafetyGuard. Do NOT change the SafetyGuard's permissive default in this PR (that is a separate decision). Independently of that, the harness must refuse writes unless its own explicit enable flag is set.
7. Unit tests in the existing simulated-memory style, plus a short README section on how to enable the harness and run one experiment.

## Done when
- It builds with the existing CMake/MinGW toolchain.
- The existing tests and the new tests pass.
- The PR describes the hook points, how each hook is validated, and the safety gating.
- Default runtime behavior is unchanged.

Investigate the codebase yourself. The addresses above come from analysis; check how the plan uses them before you hard-code them.
