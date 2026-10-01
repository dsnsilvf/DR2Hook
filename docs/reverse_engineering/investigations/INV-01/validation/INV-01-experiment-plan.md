# INV-01: experiment plan (Experimental Validation Specialist)

Date: 2026-09-30, rev 1 12:05 BRT; **rev 2 12:40 BRT** (America/Recife, UTC-3). Status: plan. Gates G0-G2 and Phase 1a R1-R6 were run later by the Validation Specialist (read-only; see §R2.1). Nothing in this revision touched the game. No machineId was used, nothing was attached to the game, and no game memory was read or written for this plan. Everything below was derived on the box from:
- `/workspace/analyst/INV-01-static-report.md` (the "static report", SR) and `/workspace/analyst/dis/*`
- `/workspace/orchestrator/briefs/INV-01-{static,runtime}.md`
- `/workspace/captures/INV-01/` (`static_search_frozen.txt`, `frozen_snapshot.csv`, `raw/*.bin`, `scripts/*.py`)
- `/workspace/DR2ModLoader` (`docs/adr_savestate_safety.md`, `src/core/safety.cpp`, `src/core/core_module.cpp`, `src/core/player.cpp`) and `/workspace/dr2hook-kb`

Offsets are rig-relative unless prefixed (`con+` = container = `car+0x30`, `car+` = car object, `wheel+` = per-wheel block at `rig+0x1480+i*0x420`). `S` = `rig+0x2b0`.

The design rule throughout: every experiment has to be able to fail. Where a test can only give a correlation (value A equals value B), it is labelled that way and graded no higher than PROBABLE.

---

## REVISION 2 (2026-09-30, 12:40 BRT): addendum and Phase 1a folded in. Read this section first.

Inputs added for this revision:
- `/workspace/analyst/INV-01-static-addendum.md` ("AD"): landing points L0-L8 and experiments E-B1..E-B11
- `/workspace/validation/INV-01-gates.md`: G0 PASS; G1 PASS on substitute criteria, graded PROBABLE-offline; G2 clean
- `/workspace/validation/INV-01-phase1a.md` plus `phase1a/` data (R1-R6)
- disassembly already on the box, re-checked for this revision

Still **plan only**. For this revision nothing ran on the user's machine and no game memory was touched. The only computation was an offline re-analysis of the existing `phase1a/settled_series.csv` on the box (R9 below).

Where rev 2 conflicts with rev 1 text further down, **rev 2 wins**. Each rev 1 experiment entry below carries a "Rev 2" block with the changes. The rev 1 file is kept as `INV-01-experiment-plan.rev1.md`.

### R2.0 Corrections and new facts

| # | Item | Evidence | Effect |
|---|---|---|---|
| C1 | **My N1 call path was wrong. AD §0.3 is right.** `0x140731d40` = `mov rcx,[rcx+8]; jmp 0x14074f600` (the committed-state push to the proxy). The thunk to `0x1407511e0` is `0x140731d50`, called from the physics step `0x140dbc500` at `0x140dbc988`. The conclusion "Commit every tick" stands, via F.7 EndStep. | `dis/phys_720000_760000.txt` lines 21196-21206; `dis/callers_db_9c_99.txt` 0x140dbc988 | N1 row in §0 corrected. The frame sync C (`0x140dbca20`) pushes committed pose/v/w to the proxy. It does not commit. |
| C2 | **Harness B2 is not the addendum's L1.** AD §1.3 labels L1 as "the plan's B2". That mapping is wrong: B2 is the entry of `0x14074b8f0` (rig Update), which lies *after* F.1 (`0x140dadc00`), F.2 (`0x140dbe300` proxy/collision prep), PreTick `0x140749a30` (pull, +0x360 overwrite, mass push), the proxy-y tick gate `0x140dbc46e`, and `0x140dbcc90` (timers). **B2 = the end of L2.** A write at B2 cannot see any L1 risk (pull, F.1/F.2), and it bypasses the one-F stale-proxy window that an L1 write creates. | AD §1.1 chain F.1-F.3b | New hook at the C return is needed for L1 (§R2.3). |
| C3 | **Naming collision.** AD calls L4 "M2". In this plan, **M2 = L5** (entry of `0x140746150` with return address `0x1407395fa`). AD's L4 (between look-ahead save `0x14073e266` and restore `0x14073e3ed`) is reached here through the same detour filtered on return address `0x14073e314`, named **M2LA**. The harness must use the plan's IDs. The CSV carries `landing_point` separately to avoid ambiguity. | AD §1.3 | Harness spec (§R2.3) |
| C4 | **The +0x1338 "2·dt per tick" is explained by code.** There are two writers per tick: `0x14074ddb1` (ContainerTick `0x140dbcc90` → thunk `0x140731cd0` → `0x14074dd90`) and `0x14074bb28` (inside rig Update). `+0x133c`, `+0xe0`, `+0xe4`, and `+0x144c` are also incremented twice. The first increment happens **before B2**, so a "half-step" marks being inside ContainerTick before the rig Update. The 5% of ticks where the two steps are about 5 ms apart is unexplained. The code between them is µs apart, so this suggests preemption of the job thread, or polling artefacts. | `dis` 0x14074dd90-0x14074ddec; 0x14074bb15-bb69; 0x140dbcca1 | Async phase bracket (W8) counts half-steps. The about-5 ms split means a Present-thread write can land inside L2. |
| C5 | **F.8 is an acceleration that AD left unresolved.** `con+0xc930 = ((rig+0x190)+0x70 − (rig+0x100)+0x70)/dt = (rig+0x200 − rig+0x170)/dt`, i.e. (committed v − tick-start v)/dt, every F after Commit (getters `0x1407313d0` → rig+0x190 and `0x1407315e0` → rig+0x100). So there is a consumer of `+0x170` and `+0x200` outside the rig, with an unknown reader of `con+0xc930` (g-force? camera? damage? telemetry?). | `dis/callers_db_9c_99.txt` 0x140dbc98d-0x140dbc9d9; `dis/phys` 0x1407313d0, 0x1407315e0 | Predicts an **acceleration spike** for v writes at L6/L7 (after the integrate, before Commit: +0x200 = written, +0x170 = old). There is **no spike** for writes at L1/L2 (the tick-start copy takes the written v) or at L8/L0 (committed is still old at F.8). `con+0xc930` goes into `field_set`, and its spike is a free landing-point discriminator. |
| C6 | **Commit render interpolation, checked offline (E-B10's read-only part).** In 1732 Phase 1a ticks with a usable denominator, a single per-tick α satisfies `+0x290 = +0x170 + α·(+0x200 − +0x170)` on all three axes. The within-tick xyz spread has median 9.4e-6 and p95 9.0e-5, which is decimal-CSV rounding. α varies from tick to tick: median 0.80, 5-95% 0.63-0.98. This fits the AD §1.2 lerp with a frame-interpolation α, and it **refutes Phase 1a's EMA (a = 0.835) hypothesis for +0x290**. | re-analysis of `phase1a/settled_series.csv` on the box, 12:3x BRT | +0x220..+0x2af is rebuilt by every Commit from tick-start and committed blocks, so it does not need restoring (PROBABLE). A bit-exact check needs α from the `[rig+0x1198]->vt+0x10` return (I6). |
| C7 | **Lead for the H-025 second writer (Phase 1a R5).** The cone `0x14073a070` (the writer of +0x1690 at `0x14073b2dd`) has a second caller, `0x14073b620` at `0x14073b68a`, with dt argument 0 (`xorps xmm10`). It is called from `0x1409cdbb7`, outside the physics module. This is a HYPOTHESIS for the gate-off +0x1560/+0x1690 updates. | `dis/phys` 0x14073b620-0x14073b6a5; `callers_db` 0x1409cdbb7 | Static follow-up for the analyst. I1 can count `0x14073b620` calls (log-only) against gate-off ticks. |
| C8 | **The proxy re-syncs every tick** (Phase 1a: pxb+0x1b0/+0x1f0 == p lag 0, pxb+0x2c0..+0x2f0 == v/ω lag 0, proxy p updated about 1.3 ms after the integrate, after Commit). This fits AD §2.3: PostTick pushes live v/w every F, and C pushes the committed pose every C. | Phase 1a, AD §2.3 | H-023's "setters are the only sync path" is **refuted observationally** (PROBABLE). The question becomes: **how long is the proxy stale after a raw write, and does anything read it in that window** (F.2 collision prep, the tick gate)? See W6/W11. |

### R2.1 Phase 1a outcomes (R1-R6 DONE, 12:07:28-12:08:13 BRT, one session, vehicle, and location)

| ID | Status | Outcome | Grade | Prediction changes |
|---|---|---|---|---|
| R1 | DONE | Constant 1000.0 (45/45). dt = 1/60 (semi-implicit Euler fit: v_new beats v_old). 60.00 ticks/s, fixed step, frame-driven, 11-24 ms jitter. **Timer +0x1338 = 2·dt/tick (refuted "== dt")**, explained by C4. Pause: 0 ticks in 18 s. | PROBABLE | n = 16 sub-iterations is still inferred. The frame-cap arm was not run. Write point P (pause) is usable, because pause stops ticks (F with dt==0 returns at F.0, per AD). |
| R2 | DONE | All lags 100% bit-exact: +0x320/+0x330/+0x170/+0x180 lag 1; +0x200/+0x210 lag 0. Controls 0%. | PROBABLE (correlation) | Commit every F is now two-source (code C1 + runtime). |
| R3 | DONE | Settled F/τ = 0 in 2699/2699; transients seen (m·g). | PROBABLE | No curb or airborne arm yet. I1 upgrades it at L1/L6. |
| R4 | DONE | m = 1083.53 constant, 1/m exact in f32. | CONFIRMED (this vehicle) | The second vehicle is still open. AD: mass is only rewritten by the setup reload `0x140749c80`. |
| R5 | DONE | (a) abs(+0x2508) == abs(v) 100%, but **+0x2508 is signed** (85 ticks negative). car+0x310 is the unsigned copy. Look-ahead seen only with the gate on (1750/1750, 0 gate-off). (b) **H-025 PROBABLE-refuted**: +0x1690/+0x1560 changed in about 19% of gate-off moving ticks, before the integrate. | PROBABLE / PROBABLE-refuted | Look-ahead gating uses abs(+0x2508) (unaffected). H-025 needs I1 (count `0x14073a070` / `0x14073b620` calls) to become formally REFUTED. |
| R6 | DONE | car+0x2b0/+0x2c0 == v/ω lag 0. car+0x2e0 = p + R·(0, −0.336, −0.373). car+0x2f0 is a unit quaternion within 0.54° of q but never equal (render transform?). rig+0x290/+0x2a0: **lerp(+0x170, +0x200, α)** (C6; replaces EMA). **con+0xcd0 constant for 45 s, 313-499 m from the car: H-011 refuted** (placement-inactive state), H-024 supported. | PROBABLE | W9 is kept, but reframed: it tests whether anything *reads* +0xcd0 while placement is inactive, i.e. whether the shipping ApplyState write is harmless or dangerous. |

**Gate status for write phases (unchanged decision point):**
- G0 PASS.
- G1 PASS only on substitute criteria (PROBABLE-offline). The plan's G1.1 network-namespace isolation is **not met**: 27 unconnected UDP sockets, and NetworkGuard does not cover `sendto`.
- By this plan's fail-closed rule, writes stay **INDETERMINATE → blocked** until the RE Orchestrator accepts the substitute explicitly for write phases, or G1.1 isolation is set up.
- Practice Mode (F5/F6/F7) is loaded. Its key presses are a confound, and it must be disabled (or its keys logged) during any trial.

### R2.2 Landing points: hook point and survival prediction

Tick = one call of F `0x140dbc500`. For each landing point, the predicted fate of a write to S.v/S.ω and to S.p/S.q, with the Phase 1a-updated expectations. Predictions assume G-pull = 0 (`con+0x14` ≥ 0.1), G-adj = 0 (`rig+0x3b8` = 0), G-place = 0 (`con+0xcc0` = 0), which were observed in every Phase 1a sample.

| L | Window | Harness hook (write point) | v/ω write predicted | p/q write predicted | Side effects predicted (logged) | Deciding instrs |
|---|---|---|---|---|---|---|
| **L0** | F returns → C entry (includes the AD "between Commit and proxy sync"; Phase 1a: about 1.3 ms after the integrate) | **NEW: `0x140dbca20` entry** | Survives in S. C pushes **old committed** v/w to the proxy; the proxy is wrong until the next PostTick (about 16.7 ms). | Survives in S. Proxy pose ← **old committed** pose; it stays wrong for a whole F (collision prep F.2 and the tick gate use it) until the next C. **Worst landing point for raw restores.** | con+0xc930 normal; +0x200/+0x190 stale until the next EndStep | 0x14074f608/6b5 |
| **L8** | After Commit (EndStep return) → F.8 → F return | **NEW (optional): `0x1407511e0` return** (filter rcx==rig) | as L0 | as L0 | as L0. Distinct from L0 only by F.8 (con+0xc930 uses the old +0x200 → normal). Kept as a snapshot point; as a write point it is optional. | 0x14074d1f9..d2b8 |
| **L1** | C return → next F entry (true "between ticks"; about 90% of wall time) | **NEW: `0x140dbca20` return** (earliest L1). Also **`0x140dbc500` entry** as the latest-L1 bracket. | **Survives** into the integration (tick-start copy); committed and proxy v stale until the next EndStep and PostTick (≤ 1 F). | **Survives**; the proxy pose stays stale through the next F (F.2 collision prep, tick gate), then C heals it. | con+0xc930 normal (no spike). If G-pull: **overwritten** at 0x140749b70/7c/88 (observable at the PreTick return). | 0x140db6bd2, 0x140749aed |
| **L2** | PreTick return → rig Update entry | **existing B2** (`0x14074b8f0` entry) = end of L2 | Survives | Survives | Proxy pose stale as in L1, but F.2 and the tick gate have already consumed the old pose for *this* F | 0x14074ba84/ba92 |
| **L3** | after `0x14074ba92` → before `0x14073e232` | **existing M1** (`0x14073e0c0` entry) | **Lost** (← +0x170/+0x180); leaks into per-wheel +0x16d0 of that tick | **Survives** (E-B5; no p/q copy-back) | — | 0x14073e232/24c |
| **L4** | look-ahead save → restore (only if abs(+0x2508) > 0.894) | **existing M2 detour, extra return-address filter `0x14073e314`** = "M2LA" (write before the *tentative* integrate) | **Lost** (restored); leaks into cone outputs +0x1560/+0x1690 (not restored) | **Lost** (restored); same leak | Gate-off ticks: M2LA never fires, which is the control | 0x14073e3e3/e8/ed |
| **L5** | after restore → before the real integrate | **existing M2** (ret `0x1407395fa`) | Survives, integrated | Survives, integrated | — | 0x140746150 |
| (L5b) | after the integrate → rig Update return | **existing M3** | Survives (next tick input) | Survives | con+0xc930 **spike** (as L6) | — |
| **L6** | rig Update return → end of job (PostTick pushes live v/w) | **existing B1** (`0x14074b8f0` return) | Survives; proxy v gets it at PostTick; committed at EndStep | Survives; proxy pose gets it at the next C (via Commit) | **con+0xc930 spike** = (written − v_tickstart)/dt | 0x140db23a1 |
| **L7** | after the jobs (F.4-F.6) → EndStep entry | **NEW (optional): `0x1407511e0` entry** | Survives; committed at EndStep; proxy v only at C (committed) | Survives, unless G-adj → SetPose | **con+0xc930 spike** | 0x14075122a/23a |

**Discriminators that make the table refutable per trial:**
1. Where the write went: S at the next M2.
2. When committed changed: +0x200/+0x190 at the EndStep return.
3. When the proxy changed: pxb+0x1b0/+0x2e0 at the C return and the next PreTick entry.
4. Whether con+0xc930 spiked.

A landing point whose observed 4-tuple does not match its row refutes that row.

### R2.3 Extra hooks the harness needs (beyond B1, B2, M1, M2, M3)

All detours must filter by the player's container/rig (`rcx`). They must record the thread id, because containers 1..n run F.3 on worker threads and the player's container may be one of them (AD §1.4). Queued writes execute inside the matching detour on that thread.

| # | Address | Entry/Return | Role | Why needed | What cannot be distinguished without it |
|---|---|---|---|---|---|
| H1 | `0x140dbca20` (C, per-container sync) | **return** | **L1 write point** (earliest "between ticks"); snapshot after the proxy pose push | B2 is the end of L2, not L1 (C2). This is the only way to write before PreTick, F.1, F.2, and the tick gate. | L1 vs L2 survival; whether PreTick, F.1 (`0x140dadc00`), or F.2 (`0x140dbe300`) overwrites S; the one-F stale-proxy window that a real savestate (mostly L1 by wall time) creates; W8 cannot mark its async writes BETWEEN-L1 vs inside L2 |
| H2 | `0x140dbca20` | **entry** | **L0 write point**; snapshot of committed and proxy before the push | This is the "between Commit and proxy sync" window the Orchestrator asked about. The worst case for raw restores is when the proxy gets the old committed pose. | whether C pushes committed (old) vs live state; a proxy stale for a full F after an L0/L8 write vs about 0 after an L6 write |
| H3 | `0x140dbc500` (F, physics step) | **entry and return**, log-only | defines `f_seq` (the tick counter replaces `tick_seq`: odd at F entry, even at F return); latest-L1 bracket; start of L0; E-B1 order (F…F, C, Present); F with dt==0 (pause) | The tick is F, not the rig Update: PreTick, the gate, and EndStep/Commit sit outside `0x14074b8f0`. | H-ORD (C after F; one or more F per frame); whether F and C ever **overlap** on different threads (then L1 is not a quiet window at all); pause behaviour |
| H4 | `0x140749a30` (PreTick) | **entry and return**, log-only, filter rcx==rig | attribution. Entry snapshot = S after F.1/F.2. Return snapshot = S after the pull and the +0x360 overwrite. Plus `con+0x14` at that moment. | Needed to attribute any L1 loss. Needed for E-B6 (+0x360 overwritten at the PreTick return). The gate skip is derived: PreTick ran but B2 did not within the same F. | an L1 write lost by B2 could come from F.1, F.2, PreTick pull, or ContainerTick; the E-B6 result; tick-skip detection (without H4, "B2 absent" can't be told apart from "container not stepped") |
| H5 | `0x1407511e0` (EndStep) | **entry and return**, filter rcx==rig (log-only; write points L7/L8 optional) | entry = snapshot after F.3c-F.6 and before the adjuster/Commit; return = committed values after Commit | Shows that nothing in F.4 (`0x140dbd740`), F.5 (`0x140dbd900`), or F.6 (`0x140dadc00`) writes S. Neither AD nor rev 1 examined these bodies. It also times Commit relative to the write. | L6 vs L7 fate; a hidden S writer between B1 and Commit; "committed = written" at the same F vs the next F |
| H6 | `0x140746150` detour (existing M2), **extra filter ret==`0x14073e314`** | entry and return | **L4 = M2LA** write point and snapshot | Not a new detour. Without it, E-B4's L4 arm (and "all 15 look-ahead fields restored") can't be run. | L4 "lost" vs L5 "kept"; the cone leak (+0x1560/+0x1690) |
| H7 (optional, log-only) | `0x14074d190` (Commit) entry; `0x14073a070` and `0x14073b620` entry | entry | I3 census; H-025 second-writer attribution (C7) | Low cost | whether Commit runs exactly once per F; which path writes +0x1690 in gate-off ticks |

**Not needed:**
- A separate hook on the ContainerTick gate (`0x140dbc430`). The skip is derived from H4 + B2 + H3.
- A hook "between Commit and proxy sync" other than H2 (+ H5 return). Only F.8 (con+0xc930) lies between them.

**Minimum set if the cloud agent must prioritise:** H1, H3, H4, H6 are required. H2 is required for the Priority 3 desync question. H5 is strongly recommended. H7 is optional.

### R2.4 Merged experiment list and run order (supersedes the §5 table)

E-B items are merged into rev 1 IDs; nothing is dropped silently. The harness hook sets needed are in brackets.

| Order | ID | Kind | Status | Merges | Needs | MC data? |
|---|---|---|---|---|---|---|
| 0 | G0-G2 | gates | **DONE** (G1 PROBABLE-offline substitute; write phases still blocked by fail-closed) | E-B9 folded into G2 (add `con+0x14`, abs(`con+0xcd0`)) | — | — |
| 1 | R1-R6 | external read | **DONE** (outcomes §R2.1) | E-B10 read-only part (C6) | — | yes |
| 2 | R7 | external read (**new**) | todo | **E-B2** pull gate: `con+0x14` every tick for 5 min plus one native reset (expect ≥ 0.1, FLT_MAX after the reset) | pread only | — |
| 3 | R8 | external read (**new**) | todo | **E-B8** tick gate: proxy T.y (`[[con+0x840]+0x10]+0x1f0`), `[0x1420202d0]`, `[0x14201b788]`, ring-index advance per F | pread only | — |
| 4 | R9 | offline + external read (**new**) | α part **DONE** (C6); c930 part todo | E-B10: +0x290/+0x2a0 lerp; **con+0xc930 == (+0x200 − +0x170)/dt** bit-exact on settled ticks (C5) | pread (add `con+0xc930` window) | — |
| 5 | G3 | harness self-test | todo | — | all hooks, log-only | — |
| 6 | I1 | log-only hooks | todo | **E-B1** (order F/C/Present, overlap, threads) | H3, H1/H2, H4, H5, B1, B2, M1, M2, M3, H6, Present | — |
| 7 | I2 | log-only | todo | — | M2/M3 | — |
| 8 | I3 | log-only | todo | **E-B11** (reset path: `0x140999990`, `0x140db7740`, `0x140db95d0`, `0x14074a110`, `0x14074a3a0`, `0x14074a5e0`, `0x1407319c0`, `0x140515800` filtered to types 0x32/0x53) | H7 + those entries | — |
| 9 | I4 | log-only + reads | partly answered by Phase 1a (C8) | E-B8 runtime cross-check with the in-process gate outcome | H3, H4, B2 | proxy dumps present |
| 10 | I5 | log-only | todo, narrowed | AD: `0x140749a30` = PreTick (every F; S writes only under G-pull); `0x140750f20` REFUTED as an S writer. Remaining: F.1/F.2/F.4-F.6 bodies via H1→H4 and B1→H5 snapshot diffs | H1, H4, H5 | — |
| 11 | I6 | log-only (**new**) | todo | E-B10 bit-exact: α from the `[rig+0x1198]->vt+0x10` return. The indirect call can't be detoured by address, so read the global object `0x1420209e0` state or compute α back from the snapshot at the H5 return (acceptable at ≤ 1 ulp) | H5 | — |
| 12 | W0 | write | todo | **E-B6** added as arm (e): +0x360 ×1.05 at **L1**, expected overwritten by the H4 return (positive "known-overwrite" control) | H1, H4 | — |
| 13 | W1 | write | todo, **re-scoped to a landing-point sweep for v** | E1, Priority 1a. Arms L1 (primary), L2 (=B2), L6 (=B1), L7, L0 | H1, H2, H5, B1, B2 | — |
| 14 | W2 | write | todo | E5; same sweep with L1 as primary | as W1 | — |
| 15 | W3 | write | todo | **E-B4** (L3 vs L5, + **L4 arm**), **E-B5** (p/q at L3 survives; p/q at L4 lost) | M1, H6, M2, M3 | — |
| 16 | W4 | write | todo | E6 (at L1 and at L2) | H1, B2 | — |
| 17 | W5 | write | todo | E2, **E-B7** (finite distinctive values, **not garbage/NaN**), Priority 2, shipping ApplyState (c) | H1, B2 | — |
| 18 | W6 | write | todo | E3, **E-B3** (p at L1; self-heal timing of committed, interp, proxy). **dp = +0.05 m, not +2 m** | H1, H2, H4, H5 | proxy offsets from Phase 1a |
| 19 | W7 | write | todo | E4 | H1, B2 | — |
| 20 | W8 | write | todo | async realism; the Phase 1a physics burst is ≤ 1.5 ms of 16.7 ms | H1-H5 for classification | — |
| 21 | W9 | write | todo, reframed (§R2.1 R6) | H-024 | H1 | R6 done |
| 22 | W10 | write | todo | E11 | H1 | — |
| 23 | W11 | write | todo, predictions revised | E12, Priority 3 raw arm; L1 vs **L0** arms | H1, H2, H3, H4, H5 | Phase 1a proxy offsets |
| 24 | A1-A4 | API | todo | Priority 3. **All calls at L1 (H1)**, not B2, so the setters' proxy push happens before F.2 collision prep | H1 (+H2/H5 snapshots) | — |

Counts: 31 experiments (R1-R9, I1-I6, W0-W11, A1-A4) plus 4 gates. 6 are done (R1-R6), and R9 is partly done. E-B1..E-B11 map as: B1→I1, B2→R7, B3→W6/W11, B4→W3, B5→W3, B6→W0(e), B7→W5, B8→R8/I4, B9→G2, B10→R9/I6, B11→I3. E-B12 (wording) is adopted: "between ticks" is replaced by explicit L-points.

### R2.5 Addendum claims that look like overclaims

1. **"L1 = the plan's B2"** (AD §1.3 table). This is wrong (C2), and the error matters: a harness built on that mapping would report "L1 survives" from B2 trials that never passed through PreTick, F.1, or F.2.
2. **L1 "survives unless G-pull or G-adj"** is presented as the complete list of conditions. F.1 (`0x140dadc00` → `0x140da94c0`/`0x140da6770`, for containers with byte +0xf2) and F.2 (`0x140dbe300`) run between L1 and PreTick, and their bodies were not shown to be free of S writes. The survival condition is PROBABLE at best, not exhaustive.
3. **L7 "Survives … CONFIRMED (control flow)".** F.4 (`0x140dbd740`), F.5 (`0x140dbd900`, uses con+0xc878), and F.6 (`0x140dadc00`) were not shown to be free of S writes. "CONFIRMED" covers only the EndStep adjuster branch. The same applies to L6: `0x140db2220` "not fully read" (AD §8).
4. **"Commit, now fully read"** (AD §1.2). The tail calls `0x14074f140`, `0x14073bc20`, `0x140736240`/`0x14073d840`/`0x14073be80`, and per-wheel `0x140730350`, and none of them was checked for persistent writes. "Anti-tamper REFUTED" is correctly scoped to `0x14075bd70` only.
5. **F.8 left as "(A+0x70 − B+0x70)/dt"** without resolving A and B. It is (committed v − tick-start v)/dt (C5), an unlisted consumer of +0x170/+0x200. That undercuts AD §1.3's "implication for a savestate", which lists only committed, interp, and proxy as stale state.
6. **Tick gate "semantics PROBABLE"** (AD §2.1) while the reference G (`0x1420202d0`) is UNKNOWN at runtime. Without G, the "fell through the world" reading is a HYPOTHESIS.
7. **"Every reset path writes con+0x14 = FLT_MAX … a game reset does not re-sync the rig from the proxy"** (AD §4.2). This holds only for the reset entry points found. The actual reset_vehicle consumer is UNKNOWN (H-RV is a HYPOTHESIS), so it can't be stated about "a game reset".
8. **"Pull is off in normal driving (PROBABLE)".** This rests on a writer search limited to 0x140da0000-0x140dc0000, plus one runtime sample. The grade is fine, but R7 is needed before any write trial treats G-pull = 0 as given. `con+0x14` must be in the per-trial G2 poll.
9. **E-B3 (+2 m) and E-B7 ("garbage")** break the minimal-write rule. They are replaced by +0.05 m and by finite distinctive values. The look-ahead save/restore propagates `+0x320` within the tick, and NaN in S would also reach the proxy.
10. **Proxy "stale for at most one F + one C"** (H-SELFHEAL). This is plausible and supported by C8, but "stale" only for the *visible* proxy fields. Internal collision-engine caches (contacts, broadphase) are not covered by that statement.

Minor: "H-023 setters only → REFUTED" is not stated by AD, but it is implied by its §2.3 table. Phase 1a (C8) supports the refutation observationally.

---

## 0. New facts found while planning (they change the design)

These came from re-reading the dumps and disassembly already on the box. They are reported here because several experiments depend on them.

| # | Observation | Source | Consequence for the plan |
|---|---|---|---|
| N1 | **Commit runs in normal driving.** `0x140dbca20` loops over all containers. When the placement branch is inactive (`con+0xcc0` <= threshold; the frozen dump has `con+0xcc0 = 0`), it takes `0x140dbcb87: call 0x140731d40` -> ~~`0x1407511e0(rig)`~~ **[rev 2: WRONG path, see C1. 0x140731d40 -> 0x14074f600 (committed push to proxy). `0x1407511e0` is reached from F at 0x140dbc988 via 0x140731d50.]** The `0x1407511e0` body is as described: That function (a) calls `[[rig+0x3b8]+0x10](owner, &{p,q})` if `rig+0x3b8` != 0, and if that returns true it calls **SetPose** (`0x14075123a`) with the returned pose; (b) **always** calls `Commit(rig,1)` (`0x140751244`). | `dis/callers_db_9c_99.txt` 0x140dbca70-0x140dbcb9b; `dis/phys_720000_760000.txt` 0x1407511e0-0x14075125b | The SR's per-tick chain (§2.5) leaves this out, and the SR describes `0x140dbca20` only as a placement routine. "Between ticks" is therefore not one point: code that commits state, and can rewrite the pose, runs there. Two between-tick write points have to be tested separately (B1/B2, §3). |
| N2 | Frozen dump: `+0x200` == `+0x2b0` and `+0x210` == `+0x2c0` bit for bit. | `static_search_frozen.txt` lines 83-89 | Consistent with N1: Commit ran after the last integrate. This is a single snapshot, so it is correlation only. |
| N3 | Frozen dump: `rig+0x3b8` = 0 (pose adjuster null), `rig+0x2550` = 0 (hook null), `rig+0x2548` = 0 (freeze), `rig+0x2560` = 0 (kinematic), `rig+0x12c0` = rig, `rig+0x12d0` = 4. | `raw/dump_rig_4b29bab0.bin` | For that session, the H-022 hook and the N1 SetPose path were both inactive. That has to be re-checked in every session and every trial (gate G2), because either one can edit `S`. |
| N4 | The container tick `0x140dbc430` first calls `0x140df7b50([con+0x840], &buf)`, compares a value derived from the proxy against a constant (`0x141227fe0`), and **skips the rig update** (`0x140731ca0`) when `jbe` is taken. | `callers_db_9c_99.txt` 0x140dbc430-0x140dbc4f0 | The `con+0x840` proxy gates whether the rig ticks at all. A raw pose write that leaves the proxy stale could change tick gating, not only collision. This matters for Priority 3 and is not in the SR. |
| N5 | Secondary copies the SR does not mention: `car+0x2b0` / `car+0x2c0` equal v/omega bit for bit; `car+0x2e0` ~ p plus a body-frame offset; `car+0x2f0` ~ q but **not equal** (-0.011050 vs -0.011524); `car+0x310` = `rig+0x2508` = abs(v) (10.2374); `rig+0x290` / `rig+0x2a0` ~ v/omega but not equal. | `static_search_frozen.txt`, dumps | A write can look as if it "stuck" because a copy changed, or look "lost" because a copy did not. All of these copies get logged (R6), and none of them counts as proof of physics effect. |
| N6 | **The offline gate in the current DR2Hook build is non-functional.** `core_module.cpp:101-102`: `SafetyGuard::Configure(&g_memoryScanner, 0); SafetyGuard::SetPermissiveMode(true);`, so `CanWriteState()` always returns true. KB Q-007: the `GameSessionMode` enum "is invented, not derived from the game". | DR2ModLoader source | Offline status **cannot** be verified through DR2Hook's own guard. Gate G1 defines an independent check. Until G1 passes, no writes. |
| N7 | The current restore code writes `+0x320` as "linear velocity" and writes `con+0xcd0` as the "visual anchor" (`player.cpp:155,219,225,229-235`; ADR-006). | DR2ModLoader source | If SR §3 is right, the shipping savestate restores the wrong velocity field and writes a placement target. W5/W9 test the shipping behaviour directly. |
| N8 | Build: the mapped PE header in `raw/dump_hdr_140000000.bin` has TimeDateStamp `0x605cbae3` = 2021-03-25 16:31:31 UTC = **13:31:31 BRT**. The SR's objdump string "Thu Mar 25 13:31:31 2021" (user machine's local zone) matches. The mass check passes in the dump: `1083.53 x 9.229094e-4 = 1.0000000012`. | dumps | The runtime PE timestamp matches. The disk SHA-256 has not yet been cross-checked against the running process (G0). |

---

## 1. Hard preconditions (every write, instrumentation-hook, and API experiment)

Passive external reads (Phase 1a) need only G0 and G2. Everything else needs G0, G1, G2, and G3. **If any gate fails or is indeterminate, abort. Fail-closed.**

### G0: Build identity gate (read-only)
1. SHA-256 of the exe on disk == `c119f509fc315d8168aa7aaa5d0a1ee8e3570c7220bd6cbb2d337d6725d73442`, size 24,668,160 B.
2. `/proc/<pid>/maps` shows `dirtrally2.exe` mapped at `0x140000000` from that same path, with the inode and mtime recorded at hash time. This rules out a different copy.
3. The mapped header's TimeDateStamp == `0x605cbae3` (2021-03-25 13:31:31 BRT / 16:31:31 UTC).
4. The code bytes read from process memory match the disk bytes at every address we hook or call: `0x14074b8f0`, `0x14073e0c0`, `0x140746150`, `0x1407395f5`, `0x14073e30f`, `0x14074d190`, `0x14074ad80`, `0x14074a910`, `0x14074a890`, `0x1407511e0`, `0x140dbc430`, `0x140dbca20` (32 bytes each). This catches runtime patching, DRM decryption differences, or another mod's hooks.
Log to the `RUN_META` rows of the CSV. Any mismatch means no writes and no hooks.

### G1: Offline gate (independent of DR2Hook's SafetyGuard; see N6)
All four of the following must hold, checked at session start **and** re-checked immediately before every trial:
1. **Network isolation at OS level.** The game runs with no route off loopback: preferably a network namespace with only `lo` (for example, launched under `unshare -n` / firejail `--net=none`), or an nftables rule dropping all non-loopback traffic for the game's cgroup/uid. Verify with `ss -tupn` filtered to the game PID: zero non-loopback sockets, and no non-loopback ESTABLISHED/SYN-SENT. Log the snapshot.
2. **DR2Hook NetworkGuard active.** `dr2hook.log` shows the Winsock detours installed at startup, and at least one refused `getaddrinfo`/`connect` for a non-loopback host. This is defence in depth only; on its own it is not sufficient.
3. **Game mode.** The operator (the user, not an agent) confirms from the screen that the session is DirtFish free roam, a Time Trial started while the game shows it is offline, or a Custom Championship. A screenshot is saved and referenced in `offline_evidence_ref`. Daily, Weekly, Clubs, and online career are forbidden, and so is any mode where the game shows RaceNet online status.
4. **Session identity unchanged since the check.** The rig pointer (`exe+0x1681ce8 -> car -> +0x30 -> +0x08`) equals the one recorded at the gate check. A rig change means a new session, so re-gate.
If any item cannot be confirmed, `offline_gate_status = FAIL` and **no writes happen**. A future memory-based session-mode check can be added only after Q-007 is answered by real RE. The enum in `safety.h` must not be trusted.

### G2: Rig sanity and confound gate (read-only, per trial)
`[rig+0x12c0] == rig`, `[rig+0x12d0] == 4`, `con+0 == 0x141400c30`, `con+8 == rig`, `rig+0 == con` (owner).
Confound flags that must hold for the "clean" condition:
- `rig+0x2550 == 0` (no H-022 hook)
- `rig+0x3b8 == 0` (no N1 pose adjuster)
- byte `rig+0x2548 == 0` (not frozen)
- `rig+0x2560 == 0` (not kinematic)
- `con+0xcc0 == 0` (placement branch inactive)
- **rev 2 (E-B9):** `con+0x14 >= 0.1` (pull gate off; FLT_MAX expected) and abs(`con+0xcd0`) logged. Bit5 of `confound_flags` = pull active.
If any of these is non-zero, the trial is tagged `CONFOUNDED` and excluded from grading. Optionally, run it as a separate arm.

### G3: Harness self-test (no state writes)
The hooks are installed with **logging only**. For >= 600 ticks, the harness must show: the phase counters advance in the expected order (§3.3); there are 0 re-entrancy errors; the physics thread ID is stable; and the sham-write path (it goes through snapshot -> "write" of 0 bytes -> restore-verify) logs `before == after` for all fields. Then remove the hooks and confirm the code bytes are back to the disk bytes (G0.4 again).

### Write protocol (all Phase 2/3 trials)
1. Snapshot the original bytes of **every field that will be written, plus the full observation set** (§4 `field_set`), at the write point.
2. Perform one minimal write: the smallest perturbation that is still detectable. Detection relies on bit-exact rules, not trajectory size, so perturbations can be small. Defaults: dv = +0.5 m/s on one world axis, domega = +0.2 rad/s, dp = +0.05 m (E3's +1 m only in the at-rest arm), and a dq of 5 deg about world Y (E4's 90 deg only at rest and only after 5 deg passes).
3. Read back immediately at the same phase (`event=READBACK`). The readback must equal `written_hex`, or the trial is invalid.
4. Observe N = 1..10 ticks (30 for position/fall tests), one row per field per tick.
5. **Restore.**
   - Output or dead fields (`+0x320/+0x330`, `+0x170/+0x180`, `con+0xcd0`, identity writes) are restored byte for byte at the next B2.
   - State fields (v, omega, p, q) cannot be byte-restored meaningfully once the car has moved. For those, the "restore" is a full Set-B snapshot restore (W10's set) taken at the pre-write tick. Its own verification rows are logged (`RESTORE`, `RESTORE_VERIFY`). If that fails the sanity checks, the operator uses the game's own offline "reset vehicle" / restart.
6. **Abort criteria** (stop the run, not just the trial): NaN or Inf in any S field; abs(v) > 60 m/s; abs(omega) > 20 rad/s; the rig pointer changes; a G1 re-check fails; the game hangs or crashes; a readback mismatch happens twice.
7. At most one write family per trial. Wait >= 3 s of undisturbed driving or rest between trials.

---

## 2. What each grade means for runtime results

- **CONFIRMED (runtime)**: the deterministic rule holds in 20/20 phase-verified trials per cell, across >= 2 car states (rest; about 20 m/s), >= 2 vehicles (different mass/inertia; e.g. a light FWD/RWD and a 4WD R5), and >= 2 locations (DirtFish plus one custom-championship stage). All negative controls must be clean. If this agrees with the SR, the claim becomes **CONFIRMED (two-source)**.
- **PROBABLE**: the rule holds in one configuration only; or there is <= 1 unexplained deviation per 20; or the phase was verified only post hoc (async writes); or the evidence is equality or correlation only.
- **REFUTED**: a phase-verified deterministic mismatch that reproduces in >= 3 trials while the controls are clean. The repeats exclude a harness bug. One mismatch triggers a harness audit, not a refutation.
- **Inconclusive**: the controls fail, or the confound flags are set. The grade stays unchanged.
- Bit-exact rules (for example, "`+0x320` at N=1 equals the written v") are preferred over trajectory statistics. They need no tolerance and a single verified counterexample is meaningful.

---

## 3. Tick-timing method (how "between ticks" and "mid-tick" are produced and proven)

### 3.1 Write points
The DR2Hook proxy already carries MinHook. The harness is a separate test build of `dr2hook_core.dll`, loaded only after G0 and G1 pass. It installs detours at the points below. Every write is performed **by the detour itself, on the calling (physics) thread**, so the phase holds by construction instead of by luck.

| ID | Location | What has already happened / what is still to come | SR model predicts for a v/omega write |
|---|---|---|---|
| **B2** | Entry of rig Update `0x14074b8f0` (**rev 2: = end of L2, NOT L1**; PreTick, F.1/F.2, the tick gate and timers already ran this F. L1 needs H1, §R2.3) | Everything from the previous tick, including EndStep/Commit (F.7) and C (`0x140dbca20`, proxy push), is done. The tick-start copy `+0x170 <- +0x2b0` (`0x14074ba92`) is still to come. | **Survives** (copied at tick start, re-injected, integrated) |
| **B1** | Return of `0x14074b8f0` (still inside `0x140dbc430`; rev 2: = L6) | The integrate is done. `0x140dbc100`, PostTick push, F.4-F.6, EndStep (adjuster + Commit) and C are still to come. | Survives. **Also predicts `+0x200` = written v after Commit** (N1). If `0x140dbc100` or the Commit path rewrites v, B1 and B2 will differ. |
| **M1** | Entry of `0x14073e0c0` | The tick-start copy at `0x14074ba92` is done. The re-injection `S.v <- +0x170` at `0x14073e232` is still to come. | **Lost** (overwritten at `0x14073e232`). Side effect: per-wheel `+0x16d0` in that tick is computed from the written v (`0x14073e150..e217`). |
| **M1+** | Entry of `0x14073e0c0`, writing **both** `+0x2b0` and `+0x170` (same value) | as M1 | Survives |
| **M2** | Entry of `0x140746150` **only when the return address == `0x1407395fa`** (the real integrate; the look-ahead call returns to `0x14073e314`) | Re-injection and look-ahead restore are done. The integrate is still to come. | Survives (integrated directly) |
| **M3** | Return of `0x140746150` from `0x1407395f5` | The integrate is done. The rest of the tick is still to come. | Survives (becomes next tick's input) |
| **A-EXT** | External `process_vm_writev` from a separate process, at a random time | unknown | Survival should depend on the phase it actually hit |
| **A-PRES** | Write from the DXGI `Present` hook (this is what the shipping `Player::ApplyState` does) | unknown | as A-EXT |
| **P** | While the game is paused (F-009 pause screen), if the tick counter is shown to be frozen | Between ticks, but pause/resume may run extra code | Survives, unless resume commits or resets |

The M1 vs M2 contrast is the most refutation-capable part of Priority 1. Both are mid-tick. The SR model says one loses the write and the other keeps it. If both are lost or both survive, the model of the in-tick data flow is wrong.

### 3.2 Proving which phase a write actually hit
- **Sequence lock (rev 2: defined on F, hook H3).** `f_seq` is incremented at F `0x140dbc500` entry (odd) and return (even); a separate `c_seq` counts C (`0x140dbca20`) entry and return. Rev 1 text: a shared-memory counter `tick_seq` is incremented at B2 entry (making it odd) and at B1 exit (making it even). The sub-phase detours write `phase_id` (1=B2, 2=M1, 3=M2, 4=M3, 5=B1) and `phase_seq` into the same block. Every write row records `tick_seq`, `phase_id`, the thread ID, and QPC/`rdtsc` before and after the write.
- **In-hook writes (B2/M1/M2/M3/B1).** The phase holds by construction. It is still verified on the data: at M1, `+0x170` must already equal the pre-write `+0x2b0` of the *current* tick (proving `0x14074ba92` ran). At M2, `S.v` must equal `+0x170` (proving the re-injection ran). At M3, `+0x320` must equal the M2 input v. If any check fails, the trial is `phase_verified = N`.
- **Async writes (A-EXT, A-PRES).** Bracket the write with reads of `tick_seq`/`phase_id` from shared memory: immediately before, after `process_vm_writev` returns, and 1 ms later. Classes:
  - even and unchanged -> `BETWEEN`
  - odd, with `phase_id` recorded -> `MID(phase)`
  - changed during the bracket -> `STRADDLE`, excluded
  If there is no in-process counter (an external-only run), use the game's own counters as a weaker bracket: `rig+0x1338` timer and ring index `rig+0xdc`, read in the same `process_vm_readv` batch as the write (before and after). Grade these at most PROBABLE.
- **Physics thread.** Log the thread ID at every detour. If `0x14074b8f0` is ever entered on more than one thread, or concurrently with `Present` on the same thread, the phase model is revisited before any Phase 2 run.
- **Ticks per frame.** Count B2 entries between consecutive `Present` calls. This measures whether the rig ticks 0, 1, or several times per frame (a fixed-timestep accumulator). That is needed to interpret A-PRES.

### 3.3 Expected phase order per tick (verified read-only in I1 before any write)
**Rev 2 order (AD §1.1 + Phase 1a):** `F entry -> F.1/F.2 -> PreTick 0x140749a30 -> gate 0x140dbc46e -> 0x140dbcc90 (timer +dt #1) -> B2 -> timer +dt #2 (0x14074bb28) -> +0x170/+0x180 copies -> M1 -> re-inject -> [M2LA look-ahead] -> M2 -> M3 -> B1 -> 0x140dbc100 -> PostTick push live v/w -> F.4-F.6 -> EndStep (adjuster, Commit) -> F.8 con+0xc930 -> F return -> C (push committed pose/v/w) -> ... next F`. Phase 1a observed timer -> +0x170 -> look-ahead -> integrate -> Commit -> proxy p (about 1.3 ms). Rev 1 wording kept for reference: `B2 -> [0x14074ba84/ba92 copies] -> M1 -> re-inject -> (look-ahead: 0x140746150 ret 0x14073e314, gated by abs(+0x2508) > 0.89408) -> M2 -> M3 -> B1 -> 0x140dbca20/Commit (order relative to the next B2 to be measured)`.

---

## 4. Observation set captured on every tick of every trial (`field_set`)

- `rig`: `+0x2b0, +0x2c0, +0x2d0, +0x2e0, +0x2f0, +0x300, +0x310, +0x320, +0x330, +0x340, +0x350, +0x360, +0x370/+0x374, +0x378, +0x170, +0x180, +0x190..+0x1a8` (raw), `+0x200, +0x210, +0x100..+0x118` (raw), `+0x290, +0x2a0, +0x2508, +0x2548, +0x2550, +0x2560, +0x3b8, +0x12c8, +0x1320, +0x1338, +0xdc`
- per wheel `i` (0..3 = RL, RR, FL, FR): `wheel+0x50, +0x84, +0x88, +0x8c, +0x104, +0x1560, +0x1690, +0x16c4, +0x16c8, +0x16d0, +0x16d8`
- `car`: `+0x2b0, +0x2c0, +0x2d0, +0x2e0, +0x2f0, +0x310`
- `con`: `+0xcc0, +0xcd0`, plus the proxy fields discovered in I4 (`[con+0x840]+...`)
- **rev 2 additions:** `con+0x14` (pull timer), `con+0xc930` (F.8 acceleration), `rig+0x220..+0x2af` (render interpolation, incl. +0x290/+0x2a0), `rig+0x3c0/+0x3d0` (pull deltas), `rig+0x1334`, `rig+0x360..+0x368` (inertia, rewritten each PreTick), proxy `pxb = [[con+0x840]+8]`: `+0x1b0, +0x1f0` (p lag 0), `+0x230, +0x270` (p lag 1), `+0x2c0..+0x2f0` (v/ω); proxy world matrix `[[con+0x840]+0x10]+0x1c0..+0x1ff` (gate reads the y of +0x1f0); globals `0x1420202d0` (gate G), `0x14201b788`
- harness: `f_seq`, `c_seq`, `landing_point`, `tick_seq`, `phase_id`, thread ID, QPC, the frame index at Present, and dt (the `xmm1` argument of `0x14074b8f0`)

---

## 5. Experiments, ordered by risk

**Rev 2: the merged run order in §R2.4 supersedes this table.** Rev 1 table kept for traceability.

Summary of run order (a star marks dependence on Memory Cartographer's (MC) read-only cross-check):

| Order | ID | Kind | Covers | MC? |
|---|---|---|---|---|
| 0 | G0-G3 | gates | preconditions | G0 partly (PE ts matches in dump) |
| 1 | R1 | external read | E10, tick cadence | ★ |
| 2 | R2 | external read | lag relations (P1/P2), N2 | ★ |
| 3 | R3 | external read | E7 | ★ |
| 4 | R4 | external read | E8 | ★ (1 sample already passes) |
| 5 | R5 | external read | E9, speed identity | ★ |
| 6 | R6 | external read | secondary copies, `con+0xcd0` (H-011 vs H-024), N5 | ★ |
| 7 | I1 | log-only hooks | phase map, SR §2.5 order, N1 order | |
| 8 | I2 | log-only hooks | integrator bit-exact replay (H-014) | |
| 9 | I3 | log-only hooks | Commit/setter call census (N1) | |
| 10 | I4 | log-only hooks + reads | `con+0x840` proxy mapping, N4 tick gate | ★ (proxy dump missing) |
| 11 | I5 | log-only hooks | unopened writers / hook / adjuster watch | |
| 12 | W0 | raw write | negative-control battery | |
| 13 | W1 | raw write | **Priority 1 (between)**, E1 | |
| 14 | W2 | raw write | E5 (omega) | |
| 15 | W3 | raw write | **Priority 1 (mid-tick matrix)** | |
| 16 | W4 | raw write | E6 | |
| 17 | W5 | raw write | **Priority 2**, E2, shipping ApplyState | |
| 18 | W6 | raw write | E3 (p) | |
| 19 | W7 | raw write | E4 (q/basis) | |
| 20 | W8 | raw write | async realism (A-EXT / A-PRES / P) | |
| 21 | W9 | raw write | `con+0xcd0` effect (H-011/H-024) | ★ (needs R6) |
| 22 | W10 | raw write | E11 minimal vs full restore | |
| 23 | W11 | raw write | E12 + **Priority 3 raw arm** | ★ (needs I4) |
| 24 | A1 | API call | **Priority 3** SetLinVel/SetAngVel | |
| 25 | A2 | API call | **Priority 3** SetTransform (+Commit) | |
| 26 | A3 | API call | Commit / ResetToCommitted semantics | |
| 27 | A4 | API call | **Priority 3** full native restore vs raw memcpy | ★ (needs I4) |

That is 27 experiments plus 4 gates. SR E1-E12 map as follows: E1->W1, E2->W5, E3->W6, E4->W7, E5->W2, E6->W4, E7->R3, E8->R4, E9->R5, E10->R1, E11->W10, E12->W11. E1 and the Priority 1 "between" case were merged into W1. E6 is kept separate from W3 because it tests precedence, not timing. The new items are R2, R6, I1-I5, W0, W3, W8, W9, and A1-A4.

Each entry below uses the same fields: **Hyp** (hypothesis), **Setup**, **Changed**, **Held**, **State modified**, **Capture**, **Control**, **Rule out**, **If true**, **Refutes**, **Grade if pass**, **Residual** (remaining uncertainty).

### Phase 1a: passive external reads (process_vm_readv; needs G0 and G2 only)

#### R1: Tick cadence, dt, sub-iteration count (E10)
- **Rev 2 status: DONE.** dt = 1/60 fixed, 60.00 Hz, constant 1000.0; timer +0x1338 = 2·dt (two writers, C4); 0 ticks while paused. PROBABLE. Open: frame-cap arm, n.
- **Hyp:** The rig ticks with a dt that we can measure. The solver runs `n = max(1, int(dt*[0x1415a9ce4]))` sub-iterations, and `[0x1415a9ce4]` = 1000.0.
- **Setup:** `sampler.py` for 60 s at rest and 60 s driving. Also read the float at `0x1415a9ce4` once per second.
- **Changed:** driving vs rest; frame rate cap (e.g. 60 vs uncapped) as a second arm.
- **Held:** vehicle, location, graphics settings.
- **State modified:** none.
- **Capture:** burst start times (inter-burst period histogram), `rig+0x1338` / `+0x144c` timer deltas per burst (dt estimate), and `.data 0x1415a9ce4`.
- **Control:** a paused-game window, which should show 0 bursts if pause stops physics. That also qualifies write point P.
- **Rule out:** burst != tick (polling aliasing: one burst can hold 2 ticks, and one tick can split across 2 bursts). Cross-check with the timer delta, where the delta should be an integer multiple of dt.
- **If true:** a stable dt, where timer delta == dt; the constant == 1000.0; the period does not depend on frame rate (fixed step) or does (variable step). Either result is informative.
- **Refutes:** the constant != 1000.0, or the timer delta is inconsistent with a single dt per burst.
- **Grade if pass:** PROBABLE (polling-based). It becomes CONFIRMED once I1 counts B2 entries directly.
- **Residual:** sub-iteration n is inferred, not observed. I1 can count `0x14073e570` calls if needed.

#### R2: Settled-state lag relations (SR §2.5, H-016/H-017, N2)
- **Rev 2 status: DONE.** All six relations 100% bit-exact at the predicted lag; controls 0%. PROBABLE (correlation).
- **Hyp:** On settled states, `+0x320[k] == +0x2b0[k-1]`, `+0x330[k] == +0x2c0[k-1]`, `+0x170[k] == +0x2b0[k-1]`, `+0x180[k] == +0x2c0[k-1]` (all lag 1, bit-exact). `+0x200[k] == +0x2b0[k]` and `+0x210[k] == +0x2c0[k]` (lag 0, via the per-tick Commit, N1).
- **Setup:** `predictions.py` P1/P2 on MC's sampler `.bin`, with the `+0x200/+0x210` pairs added. Needs >= 2000 ticks, driving.
- **Changed:** none (observational).
- **Held:** one session.
- **State modified:** none.
- **Capture:** `lag_equality.csv` and `settled_series.csv` (MC output).
- **Control:** a pair expected to be unrelated, e.g. `+0x320` vs `+0x2d0`, which should show 0 lag0/lag1 hits. Also a shuffled-tick null, which should give ~0 hits.
- **Rule out:** mis-segmented bursts (a mid-tick read labelled settled); equality due to the car being at rest (v constant) -> use driving ticks only, where v changed.
- **If true:** >= 99.9% bit-exact at the stated lag. Remaining misses must be explained by STRADDLE bursts.
- **Refutes:** a systematic lag-0 relation for `+0x320` vs `+0x2b0` (would mean it is written after the integrate, not before); or `+0x200 != +0x2b0` on most settled ticks (would mean Commit is not per tick, contradicting N1).
- **Grade if pass:** PROBABLE. **This is correlation only.** It shows copies and ordering, not that the game *reads* the copy. Causation is left to W1/W5.
- **Residual:** it cannot tell "A copied to B" from "B copied to A" or "both from C". The SR's direction rests on disassembly alone.

#### R3: F/tau accumulators between ticks (E7)
- **Rev 2 status: DONE.** 0 non-zero in 2699 settled; transients present. PROBABLE. Open: curb/airborne arm.
- **Hyp:** `+0x340..+0x35c` == 0 at every settled (between-tick) sample.
- **Setup:** MC sampler; 100+ settled ticks at rest, 100+ driving, and 100+ with a wheel off the ground or a curb strike.
- **Changed:** car state.
- **Held:** vehicle.
- **State modified:** none.
- **Capture:** `+0x340`, `+0x350` settled and transient.
- **Control:** transient mid-burst values of `+0x344` should be non-zero (gravity is added at `0x14074bbad` during the tick). This proves the sampler can see non-zero F when it exists.
- **Rule out:** sampling only between ticks by chance -> require observed transients.
- **If true:** settled F = tau = 0 in 100% of samples.
- **Refutes:** any non-zero settled value. Then someone adds forces between ticks, and a savestate must include F/tau.
- **Grade if pass:** PROBABLE (external). CONFIRMED when repeated at B2/B1 in I1.
- **Residual:** forces added between B1 and the Commit loop would appear "settled".

#### R4: Mass and inverse (E8)
- **Rev 2 status: DONE.** Reciprocal exact, constant. CONFIRMED (one vehicle). Open: second vehicle, damage.
- **Hyp:** `+0x374 == 1/+0x370` (float32) and both are constant per vehicle.
- **Setup:** read once per session for >= 2 vehicles. Also read during a damage event.
- **Changed:** vehicle.
- **Held:** -
- **State modified:** none.
- **Capture:** raw bits.
- **Control:** a vehicle with a different spec mass should give a different `+0x370`.
- **Rule out:** a coincidental reciprocal (unlikely).
- **If true:** product = 1 +/- 1e-6. The frozen dump already gives 1.0000000012.
- **Refutes:** the product != 1, or the mass changes during driving (fuel or damage), which would need a savestate.
- **Grade if pass:** CONFIRMED (reciprocal). The *meaning* "car mass in kg" stays PROBABLE, because agreeing with a plausible number is correlation.
- **Residual:** does not show the integrator uses `+0x374` (that is I2's job).

#### R5: Look-ahead gate and per-wheel `+0x1690` (E9, H-025)
- **Rev 2 status: DONE.** (a) holds on magnitude, but +0x2508 is signed. (b) **H-025 PROBABLE-refuted** (gate-off updates of +0x1690/+0x1560 in about 19% of moving gate-off ticks). Second-writer lead: C7. Formal refutation via I1 (count `0x14073a070`/`0x14073b620`).
- **Hyp:** (a) `+0x2508` == float32 abs(`+0x2b0`) (settled). (b) Per-wheel `+0x1690` changes only on ticks where abs(`+0x2508`) > 0.89408.
- **Setup:** MC sampler with the wheel window added; 60 s of stop-go driving and 30 s at full rest.
- **Changed:** speed crossing 2 mph.
- **Held:** vehicle, surface.
- **State modified:** none.
- **Capture:** `+0x2508`, wheel `+0x1690`, `+0x1560`.
- **Control:** `+0x1560` (also written by the cone) should follow the same gate; wheel `+0x50` (pose-derived) should change at rest only if p changes.
- **Rule out:** at rest nothing changes anyway (a trivial pass) -> include creeping at 0.5-0.8 m/s, where p changes but the gate is off.
- **If true:** (a) bit-exact in ~100%. (b) Zero `+0x1690` changes in gate-off ticks where p changed.
- **Refutes:** `+0x1690` changes in a gate-off tick where p changed.
- **Grade if pass:** PROBABLE (correlation between gate and update). CONFIRMED only if I1 shows that `0x14073a070` is not executed on gate-off ticks.
- **Residual:** `+0x2508` may be a filtered speed. Frozen dump: 10.237433 vs abs(v) = 10.237433 (exact to float32).

#### R6: Secondary copies and `con+0xcd0` derivation (H-011 vs H-024, N5)
- **Rev 2 status: DONE.** Car copies lag 0; car+0x2e0 body offset; car+0x2f0 near-q render transform (HYPOTHESIS); +0x290/+0x2a0 = Commit lerp (C6, replaces EMA); **con+0xcd0 constant: H-011 refuted (placement inactive), H-024 supported (PROBABLE).**
- **Hyp:** `car+0x2b0/+0x2c0` are lag-0 (or lag-1) copies of v/omega; `car+0x2e0` = p + R*c (a constant body offset c); `car+0x2f0` is q from a different moment (a render interpolation?); `rig+0x290/+0x2a0` are some average of v/omega; `con+0xcd0` does **not** follow p.
- **Setup:** `analyze.py` offset/lag test on the MC `.bin` (it already computes `car+0x2e0` and `con+0xcd0` offsets in the body frame at lag 0 and 1). Add `rig+0x290` vs `(v[k]+v[k-1])/2`, and `car+0x2f0` vs slerp(q[k-1], q[k], alpha).
- **Changed:** none.
- **Held:** -
- **State modified:** none.
- **Capture:** MC outputs.
- **Control:** a constant-velocity straight, where all interpolants coincide. Discrimination needs cornering and braking.
- **Rule out:** a fixed offset that only looks constant because the car barely rotated.
- **If true:** a body-frame offset sd < 1e-4 m for `car+0x2e0`; `con+0xcd0` changes on ticks where p is constant or vice versa (frozen: `con+0xcd0` is ~64 m from p, with `con+0xcc0` = 0).
- **Refutes (H-024):** `con+0xcd0` tracks p with a constant offset or lag.
- **Grade if pass:** PROBABLE for each derivation (correlation). Whether the game *reads* any of these copies needs a write test (W9 for `con+0xcd0`; the car copies are observed in every W trial).
- **Residual:** these copies are candidates for hidden state that a restore must also set. Their readers are unknown.

#### R7: Pull gate `con+0x14` (new in rev 2; merges E-B2; read-only)
- **Hyp (H-PULL-OFF):** `con+0x14` ≥ 0.1 on every tick of normal offline driving, and it is FLT_MAX right after a game reset. So PreTick never overwrites S from the proxy.
- **Setup:** pread sampler as in Phase 1a, adding `con+0x00..0x20`, `rig+0x3c0..0x3e0`. 5 min of driving plus one game reset.
- **Changed:** driving; reset event.
- **Held:** vehicle.
- **State modified:** none.
- **Capture:** `con+0x14` per settled tick; `rig+0x3c0/+0x3d0` (non-zero only if the pull ran).
- **Control:** after the reset, `con+0x14` == FLT_MAX exactly (writer `0x140db77c0` etc.), then it grows by dt per ticked F (it is only incremented in `0x140dbcc90`, i.e. when the gate passes).
- **Rule out:** a value near FLT_MAX that grows by dt is invisible in float32 (FLT_MAX + dt == FLT_MAX). Expect it to stay bit-constant; that is not evidence of "not incremented".
- **If true:** never < 0.1; `rig+0x3c0/+0x3d0` == 0 always.
- **Refutes:** any tick < 0.1, or non-zero pull deltas. Then S writes at L1 are overwritten in that window, and every write trial must gate on it.
- **Grade if pass:** PROBABLE (this session). **Residual:** what sets it small is unknown (AD §2.2), and it may happen only in modes not tested.

#### R8: Tick gate inputs (new in rev 2; merges E-B8 read-only part)
- **Hyp (H-GATE-FLOOR):** the rig ticks iff proxy T.y > G.y − 50, where G = `[0x1420202d0]`, used only when `[0x14201b788]` ≠ 0.
- **Setup:** pread `[0x1420202d0]` (16 B), `[0x14201b788]` (8 B), and the proxy matrix `[[con+0x840]+0x10]+0x1c0..0x200` per tick, with the ring index (+0xdc) as the "rig ticked" signal.
- **Changed:** driving over terrain with varied heights; stage change if possible.
- **Held:** —
- **State modified:** none.
- **Capture:** G, flag, T.y, and ticked Y/N.
- **Control:** paused (F dt == 0 → no tick regardless of the gate).
- **Rule out:** the ring index not advancing for other reasons (pause) → cross-check with timer half-steps (C4): gate-skipped Fs have neither timer increment, because `0x140dbcc90` is also inside the gate.
- **If true:** every observed F ticks, and T.y > G.y − 50 throughout. The margin is recorded, which gives the safety margin for any pose restore.
- **Refutes:** ticks with T.y ≤ G.y − 50, or missing ticks with T.y above the floor.
- **Grade if pass:** PROBABLE for the structure. The semantics ("floor guard") stay HYPOTHESIS until a below-floor case is seen, and that case is **not** to be provoked by writes.
- **Residual:** externally, a gate-skipped F is indistinguishable from "no F" unless F's own effects on other containers are visible. I1 (H3 + H4 + B2) resolves this.

#### R9: Commit-derived fields (new in rev 2; merges E-B10 read-only part, plus C5)
- **Hyp:** (a) `+0x290 = lerp(+0x170, +0x200, α_k)` and `+0x2a0 = lerp(+0x180, +0x210, α_k)` with one α per tick. (b) `con+0xc930 == (+0x200 − +0x170)/dt` (f32, bit-exact or 1 ulp) on every settled tick.
- **Status:** (a) for +0x290 was **done offline on the box** from `phase1a/settled_series.csv` (C6): 1732 ticks, one α per tick fits xyz within CSV rounding, α 0.63-0.98. (+0x2a0 is not in that CSV; it needs the raw `samples.bin`.) (b) needs a new pread capture that includes `con+0xc930`.
- **Control:** the rejected alternatives (EMA; v[k]; v[k−1]; mean) must fit worse. EMA already fits worse (rms 2.8e-3 vs about 1e-5).
- **Refutes:** (a) xyz needing different α in the same tick; (b) c930 ≠ Δv/dt.
- **Grade:** (a) PROBABLE (strong, decimal CSV); CONFIRMED needs raw bits plus α from I6. (b) PROBABLE if it holds on 2000+ ticks.
- **Residual:** the consumer of `con+0xc930` is unknown (static follow-up). If it feeds damage, g-force, or camera shake, the L6/L7 spike is user-visible.

### Phase 1b: in-process instrumentation, log-only (gated by G0-G3; patches game code but writes no game state)

#### I1: Phase map (verifies §3.3 and SR §2.5 order; completes R1/R3)
- **Rev 2 (merges E-B1):** add H3 (F entry/return), H1/H2 (C entry/return), H4 (PreTick), H5 (EndStep), the H6 filter and Present. New checks: (i) order F…F, C, Present per frame (H-ORD); (ii) **F and C never overlap in time across threads** (if they do, L1 is not a quiet window, and every L1 write design must move to H3 entry on F's thread); (iii) the number of F per Present; (iv) the tick-gate skip = H4 fired without B2 in the same F; (v) snapshot diffs H1→H4-entry→H4-return→B2 and B1→H5-entry, to find S writers in F.1/F.2/PreTick/F.4-F.6 (all expected zero under G-pull = 0); (vi) count `0x14073a070`/`0x14073b620` calls in gate-off ticks (H-025, C7). Refutes H-ORD if C runs before F or interleaved; refutes AD L1/L7 survival if any unexpected S change appears in these diffs.
- **Hyp:** Per tick, the order is exactly B2 -> M1 -> (look-ahead integrate if gated) -> M2 -> M3 -> B1, on one thread. `+0x170/+0x180` at M1 equal S.v/S.omega at B2. S.v at M2 equals `+0x170` (re-injection, plus the look-ahead restore). F/tau are 0 at B2 and at B1.
- **Setup:** the detours at §3.1 points (logging only), plus a detour on `0x140dbca20` / `0x1407511e0` entry, the `Present` hook, and `0x14073e570` (to count sub-iterations). Snapshot `field_set` at each point for 2000 ticks per state.
- **Changed:** rest vs about 20 m/s (look-ahead gate off vs on).
- **Held:** vehicle, location.
- **State modified:** none (code patched; reverted and verified after the run).
- **Capture:** the full per-phase snapshot rows (`event=OBSERVE`, `phase_id`).
- **Control:** the look-ahead integrate count must be 0 in gate-off ticks and 1 in gate-on ticks. Sub-iteration count vs n from dt.
- **Rule out:** the detour itself changing timing enough to alter behaviour -> compare the R1 cadence with and without the hooks.
- **If true:** the order holds in 100% of ticks. The `0x140dbca20` position relative to B1/B2 gets established. This decides whether "between ticks" contains a Commit.
- **Refutes:** any other phase order; more than 1 real integrate per tick; `S.v` at M2 != `+0x170` (someone else writes v between re-injection and the integrate: the hook, freeze, or an unopened writer).
- **Grade if pass:** CONFIRMED (runtime) for the order.
- **Residual:** the order may differ in other modes (replay, spectate, multiple cars).

#### I2: Integrator bit-exact replay (H-014, SR §2.3)
- **Hyp:** Given the M2 inputs (v, omega, p, q, F, tau, I, 1/m, dt), a float32 re-implementation of the SR's update (v += dt*invM*F; omega += dt*R*Iinv*R^T*tau; p += dt*v_new; q += dt*0.5*(omega (x) q), normalised; basis from q) reproduces the M3 outputs bit for bit, or to <= 1 ulp given SSE rounding.
- **Setup:** offline replay of the I1 logs.
- **Changed:** the model variants: omega(x)q vs q(x)omega; omega world vs body frame; semi-implicit vs explicit Euler.
- **Held:** -
- **State modified:** none.
- **Capture:** the M2/M3 snapshots.
- **Control:** deliberately wrong variants must fail (a discriminating check).
- **Rule out:** a pass by degeneracy (omega ~ 0) -> include high-yaw ticks.
- **If true:** exactly one variant fits in >= 99.9% of ticks within 1 ulp, and `+0x320/+0x330` at M3 == v/omega at M2.
- **Refutes:** no variant fits (hidden inputs such as `+0x378` or damping exist).
- **Grade if pass:** CONFIRMED (two-source) for the integrator equations, and it settles the quaternion order and the frame of omega (PROBABLE in the SR).
- **Residual:** the integrator equations do not show that nothing overwrites the inputs *before* M2 (that is W1/W3's job).

#### I3: Commit and setter call census (N1)
- **Rev 2 (merges E-B11):** add log-only entries `0x140999990`, `0x140db7740`, `0x140db95d0`, `0x1407319c0`, and `0x140515800` filtered to event types 0x32/0x53 (via the event vtable 0x141271230/0x141271190). Press the game's offline reset. H-RV holds if `0x140999990` or `0x140db7740` fires within a few F after the 0x32 post; refuted if none fire. Also expect Commit exactly once per F from ret `0x140751249` (C1); it must **not** be called from C.
- **Hyp:** In normal offline driving, `Commit 0x14074d190` is called exactly once per tick per car, from `0x140751244` (via `0x140dbca20`), after B1. SetPose, SetTransform, SetLinVel, SetAngVel, Reset, ResetToCommitted, and SetFullState are **not** called in steady driving.
- **Setup:** log-only detours on `0x14074d190, 0x140746770, 0x14074ad80, 0x14074a910, 0x14074a890, 0x14074a110, 0x14074a3a0, 0x14074a5e0, 0x14074b070, 0x1407511e0`, recording the return address, `tick_seq`, and `phase_id`. 5 min of driving, then trigger the game's own offline "reset vehicle" once.
- **Changed:** driving vs the native reset event.
- **Held:** -
- **State modified:** none.
- **Capture:** the call log.
- **Control:** the native reset must produce calls (proves the detours fire).
- **Rule out:** calls by other cars (AI or ghost) -> filter by rig pointer.
- **If true:** the census matches. Commit runs after B1 and before the next B2.
- **Refutes:** Commit is not per tick (then N2 had some other cause), or setters are called every tick (then the rig is being driven from elsewhere, and "raw write survives" becomes doubtful).
- **Grade if pass:** CONFIRMED (runtime).
- **Residual:** Commit's post-store work (`0x14075bd70`) is still unanalysed. It may be a teleport or anti-tamper check. The A-arms watch for its side effects.

#### I4: `con+0x840` proxy mapping and tick gate (SR §4.2 H-023, N4)
- **Rev 2:** Phase 1a already found the proxy fields (pxb+0x1b0/+0x1f0 = p lag 0; +0x230/+0x270 = p lag 1; +0x2c0..+0x2f0 = v/ω) and per-tick re-sync (C8). Case (a) "self-heals" is PROBABLE. What remains is causal: *which* call writes each proxy field (PostTick live v/w vs C committed pose, AD §2.3), timed with H1/H2/H5. Plus E-B8 in-process: the tick-gate outcome vs proxy T.y and G (R8 gives G read-only).
- **Hyp:** The object at `[con+0x840]` holds its own copy of pose and velocities. The container vtable slots `+0x20/+0x28/+0x30` (via `0x140e17ae0`, `0x140e174a0`, `0x140e11db0`) write it. It either (a) is re-synced from the rig every tick by some path (so a raw desync self-heals), or (b) is only updated by setters (so a raw restore leaves it stale). Separately, `0x140df7b50` returns data that gates the rig update in `0x140dbc430`.
- **Setup:** read-only. Dump 0x1000 bytes of `[con+0x840]` at B2 and at B1 for 500 ticks and search for bit patterns of v, omega, p, q, and the pose matrix (the `static_search.py` method, applied per tick). Log the `0x140dbc430` branch outcome (`jbe` taken or not) via a detour on `0x140731ca0` (count calls vs container-tick calls).
- **Changed:** driving; plus one native reset (setters fire).
- **Held:** -
- **State modified:** none.
- **Capture:** candidate proxy fields and their lag vs the rig; tick-skip count.
- **Control:** at the native reset, the proxy fields must jump together with the rig (proves they are the forwarded copy).
- **Rule out:** the proxy holds a render or interpolated transform unrelated to collision -> note it only; W11 tests function.
- **If true:** fields identified. Case (a) or (b) decided by whether they change every tick with no setter calls (per I3).
- **Refutes (H-023 "setters keep it in sync" as the only path):** the proxy tracks the rig every tick with no setter call.
- **Grade if pass:** PROBABLE for the field map; the impact is left to W11/A4.
- **Residual:** the proxy may be a Havok-like body with internal state (velocity caches, contact manifolds, broadphase AABB) that is not visible as plain copies.
- **Missing now:** MC has not dumped `[con+0x840]` (only `con+0..0x1ff0` is in `frozen_snapshot.csv`; the proxy pointer seen was `0x6b6f09c0`).

#### I5: Watch for unopened writers, the H-022 hook, and the N1 adjuster
- **Rev 2:** AD resolves `0x140749a30` = PreTick (runs every F; S writes only under G-pull; +0x360 unconditional) and `0x140750f20` = setup-reload wheel-mount writer (REFUTED as an S writer). `+0x2550`, `+0x2548`, `+0x2560` are ctor-only (PROBABLE dead). I5 narrows to: poll the confounds per F (G2 + `con+0x14`), count PreTick pull-branch executions (`0x140749aed` taken vs not, derived from `rig+0x3c0/+0x3d0` ≠ 0 at the H4 return), count setup reloads (`0x140749c80`), and run the snapshot diffs listed in I1(v).
- **Hyp:** In clean sessions (G2 flags 0), nothing writes S between the phase points except the SR-listed sites.
- **Setup:** log-only detours on the entries of `0x140749a30` (contains v/omega/p writers `0x140749b70/b7c/b88`, reached by the thunks `0x140731954` and `0x140731d0d`), `0x140750f20` (contains `0x140750ff9`), and `0x14073f9c0` (kinematic). Poll `rig+0x2550`, `+0x3b8`, `+0x2548`, `+0x2560` every tick.
- **Changed:** driving, native reset, a stage start, a DirtFish area change.
- **Held:** -
- **State modified:** none.
- **Capture:** call counts with `tick_seq`/`phase_id`.
- **Control:** a native reset (must fire the known paths).
- **Rule out:** none (census).
- **If true:** 0 calls during steady driving.
- **Refutes:** any of these runs per tick. Then a write can be overwritten by paths the SR did not trace.
- **Grade if pass:** PROBABLE (absence in the scenarios we tested).
- **Residual:** paths that only run in untested scenarios (replay, spectate, network).

#### I6: Commit interpolation α, bit-exact (new in rev 2; merges E-B10 in-process part)
- **Hyp:** the α that Commit gets from `[rig+0x1198]->vt+0x10` (global object `0x1420209e0` in the dump) makes `+0x220..+0x2af` bit-exact to the AD §1.2 formulas; when α == 1.0 or dl == 0, it is a plain copy of +0x190..+0x21f.
- **Setup:** log-only. At the H5 entry and return, snapshot `+0x100..+0x2af`. Recover α per tick from +0x290 (float64 solve, then verify f32 recomputation bit-exactly on all lanes and blocks). Optionally read the global object's fields to find where α lives.
- **Changed:** frame rate (60 cap vs uncapped); α should vary with the render/physics phase.
- **Held:** —
- **State modified:** none.
- **Capture:** snapshots and α.
- **Control:** ticks with α == 1.0 exactly must show a plain copy.
- **Refutes:** no single α reproduces all lanes; or the block changes outside Commit (H5 entry ≠ previous H5 return).
- **Grade if pass:** CONFIRMED (two-source). It formally closes "+0x220..+0x2af need no restore".
- **Residual:** who reads +0x220 (getter `0x1407311a0`; presumably the render). A restore that skips it could show one frame of visual interpolation glitch. That is cosmetic, but it should be noted in W10.

### Phase 2: raw state writes (gated G0-G3; hook-timed at B2 unless stated; write protocol §1)

Design used by every W trial:
- >= 20 trials per cell, in blocks with random order.
- Every block interleaves treatment, sham (the full pipeline with 0 bytes written), and identity writes (the current bytes written back).
- Cells: {rest, about 20 m/s straight} x {vehicle A, vehicle B} x {DirtFish, custom-championship stage}.
- The primary test is the bit-exact rule at N=1. Trajectory metrics are secondary.
- For each trial, the "expected" value is computed from **that trial's own logged M2 inputs** (F, tau, dt), not from a baseline run. This removes the need to compare against a chaotic unperturbed twin.

#### W0: Negative-control battery (must pass before W1-W11)
- **Rev 2:** controls run at **L1 (H1)** and at L2 (B2). New arm **(e) E-B6 known-overwrite positive control:** write `+0x360` (inertia) ×1.05 at L1. Predicted: at the H4 (PreTick) return, +0x360 == decode(+0x590..0x598) == the pre-write value (overwritten at `0x140749bdf`), and the integrate is bit-exact to the unwritten I2 prediction. **Refutes** AD §1.2 if the ×1.05 value survives to M2. Proves the harness can *detect* an overwrite (without a positive control, "lost" and "never written" look alike). Do **not** write +0x360 at B2: PreTick has already run, so the integrator would use it (unsafe, and not the test). Add arm (f): identity write at L0 (H2), expected no effect.
- **Hyp:** The harness itself does not perturb physics. Writes to dead fields have no effect.
- **Setup:** at B2: (a) sham; (b) identity write of v, omega, p, q (same bytes); (c) a non-identity write to `+0x170/+0x180` (delta = +0.5 / +0.2). These are dead at B2 per the SR because `0x14074ba84/ba92` overwrite them before any read. (d) Identity write to `+0x2bc` (the w lane of v, 0 -> 0).
- **Changed:** write type.
- **Held:** all else.
- **State modified:** (c) only, and it is transient.
- **Capture:** `field_set`, N = 0..10.
- **Control:** these trials *are* the controls, compared against each other and against the I1 no-write logs.
- **Rule out:** the detour latency changing dt -> dt logged, must equal the no-hook dt distribution.
- **If true:** (a), (b), (d): I2 replay residual unchanged (bit-exact); `+0x320[1]` == pre-write v. (c): at M1, `+0x170` == B2 v (the write was overwritten); trajectory bit-exact to the I2 prediction.
- **Refutes:** any deviation in (a), (b), or (d) means the harness is broken; stop. A deviation in (c) means `+0x170/+0x180` are read before `0x14074ba92`, which contradicts SR §2.5.
- **Grade if pass:** a precondition for everything else. For (c), CONFIRMED (runtime) that `+0x170/+0x180` are overwritten at tick start.
- **Residual:** none specific.

#### W1: v is the source; a between-tick write survives (Priority 1a, E1, H-015)
- **Rev 2: re-scoped to a v landing-point sweep (Priority 1a), primary arm L1 (H1).** Arms: L1, L2 (B2), L6 (B1), L7 (H5 entry), L0 (H2 entry), L8 (H5 return, optional). Predicted per §R2.2:
  - L1/L2: `+0x170`@M1 == written, integrated, `+0x200`@H5-return == v_new, **con+0xc930 normal**, proxy v stale until PostTick (L1: also during the next F's collision prep).
  - L6/L7: survives into the next F; `+0x200` = written at *this* F's EndStep; **con+0xc930 spike = (written − v_tickstart)/dt**.
  - L0/L8: survives in S; C pushes **old** committed v to the proxy; `+0x200` = old until the next EndStep; con+0xc930 normal this F.
  - **Refutes:** an L1 write lost before M2 (diff at H4 attributes it: pull vs F.1/F.2); L1 ≠ L2 outcome with G-pull = 0; a missing c930 spike at L6/L7 or a spike at L1 (then F.8 isn't what C5 says); the proxy getting the written v at an L0 write before the next PostTick.
  - Grade if all pass: CONFIRMED (two-source) per landing point. Rev 1 text below: read "B2" as the L2 arm.
- **Hyp:** v written at B2 is used as the integrator input of the coming tick.
- **Setup:** at B2, write `+0x2b0 = v0 + (0,0,delta)`, with delta = +0.5 m/s (+10 m/s only in the at-rest arm, as in E1). Repeat with the write at B1 (a separate cell).
- **Changed:** v.z; write point B2 vs B1.
- **Held:** omega, p, q, controls neutral (no throttle/steer: use a scripted input or have the operator hands-off), gear 0 at rest.
- **State modified:** `rig+0x2b0` (one 12-byte write).
- **Capture:** `field_set`, N = 0..10.
- **Control:** W0 sham/identity trials interleaved; a W5 (`+0x320`) trial in the same block.
- **Rule out:** (1) the readback is only our own write -> require `tick_seq` to advance and `+0x320[1]` == written (proves the integrator consumed it); (2) freeze/rest logic zeroing v -> check `+0x2548`, and use the moving arm; (3) Commit or `0x140dbca20` rewriting v at B1 -> compare B1 vs B2; (4) a car-side copy moving while physics does not -> the primary check is rig p, not car copies.
- **If true (bit-exact at N=1):** `+0x170` at M1 == written; `+0x320` at M3 == written; p[1] == p[0] + dt*(written + dt*invM*F) (using logged F); `+0x200` after the Commit == v_new. At N=2..10, p deviates consistently with the I2 model.
- **Refutes:** `+0x320[1]` != written, or p[1] matches the unwritten v0 -> the write was lost (someone overwrote it between B2 and M2: I1 identifies who). Or B1 lost while B2 kept -> post-Update code overwrites v.
- **Grade if pass:** CONFIRMED (two-source) for "v at +0x2b0 is the next-tick source when written at B2". For B1, a separate grade.
- **Residual:** only the rig integration is proven. Collision and proxy consistency are left to W11.

#### W2: omega is the source (E5)
- **Rev 2:** same landing-point sweep as W1 (L1 primary). The ω analogue of c930 does not exist (F.8 uses v only), so no spike is expected for ω; the proxy ω (pxb+0x2c0/+0x2d0) follows the same staleness as v.
- As W1, with `+0x2c0 = omega0 + (0, domega, 0)`, domega = +0.2 rad/s (+1 rad/s at rest).
- **Capture:** q and the basis.
- **Control:** a `+0x330` write in the same block (expected: no effect, mirroring W5).
- **If true:** `+0x330[1]` == written; q[1] == I2(q0, written omega). Yaw accrues about domega*dt per tick, damped by the tyres.
- **Refutes:** q[1] follows omega0.
- **Rule out:** tyre forces cancelling the yaw -> rely on the N=1 bit-exact rule, not on yaw growth.
- **Grade if pass:** CONFIRMED (two-source).
- **Residual:** the omega frame (world vs body) is decided in I2. A wrong-frame domega still "survives" but rotates differently.

#### W3: Mid-tick timing matrix (Priority 1b, SR §3 last paragraph, H-017)
- **Rev 2 (merges E-B4, E-B5):** landing points L3 (M1), **L4 (M2LA, H6)**, L5 (M2), L5b (M3). Arms × {v, p}.
  - v: L3 lost (== +0x170), L4 lost (restored), L5 kept, L5b kept.
  - **p (E-B5): L3 KEPT** (no copy-back of p/q; rev 1 did not test p here), L4 lost (restored), L5 kept.
  - Leaks to log: L3 v → per-wheel +0x16d0 of that tick; L4 v/p → cone outputs +0x1560/+0x1690 (not restored).
  - Control: the L4 arm in gate-off ticks never fires (M2LA absent); B2/L1 references in the same block.
  - **Refutes AD §1.3:** L3 p lost (an undiscovered p restore); L4 v kept (the look-ahead restore is incomplete); L3 v kept; L5 v lost. With the polling result (look-ahead only when gated, 1750/1750), the L4 arm is run at ≥ 5 m/s only.
- **Hyp:** A v/omega write is lost if made after `0x14074ba92` and before `0x14073e232` (M1). It survives at M2 and M3. It survives at M1 if `+0x170/+0x180` are written too (M1+).
- **Setup:** the same delta as W1, applied at M1, M1+, M2, M3. For M1, additionally split by look-ahead gate on/off (speed > or < 0.894 m/s).
- **Changed:** write phase.
- **Held:** delta, vehicle, state.
- **State modified:** `+0x2b0` (and `+0x170` for M1+).
- **Capture:** `field_set` at every phase of the write tick, plus N = 1..5.
- **Control:** a B2 write (W1) in the same block, as the positive reference.
- **Rule out:** a mid-tick write applied on the wrong tick -> the phase is verified with the §3.2 data checks. The look-ahead restore masking or unmasking the write: at M1, the look-ahead saves S *after* re-injection, so it should not matter -> check both gate states.
- **If true:**
  - M1: S.v at M2 == `+0x170` (the original), `+0x320[1]` == original v, so **lost**. But the per-wheel `+0x16d0` of that tick differs from the I1 prediction (it was computed from the written v at `0x14073e150..e217`). That is a measurable leak.
  - M1+: survives.
  - M2: survives.
  - M3: survives into the next tick, with `+0x320` of the *next* tick == written.
- **Refutes:** M1 survives (the re-injection doesn't happen or reads something else), or M2 is lost (something writes v between the re-injection and the integrate). Any outcome where M1 and M2 behave the same falsifies the SR's in-tick model.
- **Grade if pass:** CONFIRMED (two-source) for the loss window.
- **Residual:** only the v/omega window has been tested. p/q have no re-injection, so W6/W7 test them only at B2.

#### W4: Precedence of the tick-start copy (E6)
- **Rev 2:** run at L1 (H1) and L2 (B2). Prediction unchanged (A wins). Also check con+0xc930 == (v_new − A)/dt (the tick-start copy took A, so there is no spike). B must be invisible at H4, M1, M2, and M3.
- **Hyp:** At B2, if `+0x2b0 = A` and `+0x170 = B != A`, the tick integrates A, because `0x14074ba92` overwrites `+0x170` with A before the re-injection.
- **Setup:** at B2, write both fields (A = v0 + 0.5 z, B = v0 - 0.5 z).
- **Changed:** -
- **Held:** -
- **State modified:** `+0x2b0`, `+0x170`.
- **Capture:** N = 0..3.
- **Control:** W0(c) (B alone, no effect).
- **Rule out:** -
- **If true:** `+0x170` at M1 == A; `+0x320[1]` == A.
- **Refutes:** B is visible in any M1/M2/M3 value.
- **Grade if pass:** CONFIRMED (runtime).
- **Residual:** partly redundant with W0(c) and W3 M1+. It is kept because it is cheap and tests precedence directly.

#### W5: `+0x320/+0x330` are output only (Priority 2, E2, H-016; the shipping ApplyState)
- **Rev 2 (merges E-B7):** write at L1 (primary) and L2. Use **finite distinctive values** (e.g. v0 + (0, 0, 3.0), ω0 + (0, 1.0, 0)), never NaN or garbage. Observe 30 F (per E-B7), with a SHAM arm. AD's whole-.text scan found no rig-based consumer and no container getter for +0x320/+0x330 (output-only stays PROBABLE, strengthened). Downstream capture adds con+0xc930 and +0x290/+0x2a0 (not expected to change: they use +0x170/+0x200, not +0x320). The expected look-ahead behaviour (+0x320 holds the written value at M2 when gated) is unchanged. Grade targets unchanged.
- **Hyp:** Writing `+0x320/+0x330` at B2 has no effect on the next tick. Both are overwritten at `0x1407461b7/0x1407461bf` with v/omega before any reader uses them.
- **Setup:** at B2 write (a) `+0x320 = v0 + (0,0,delta)`; (b) `+0x330 = omega0 + (0,domega,0)`; (c) `+0x320 = 0`, `+0x330 = 0`. Case (c) is exactly what shipping `RestoreMode::Normal` writes (N7). Also repeat (a) at M2 (just before the integrate), where the SR predicts it is overwritten *inside* the integrator.
- **Changed:** the value written; B2 vs M2.
- **Held:** everything else.
- **State modified:** `+0x320` and/or `+0x330`.
- **Capture:** `field_set` N = 0..5, plus **every downstream output we can see**: car copies, wheel `+0x16d0/+0x16d8`, `con+0x840` proxy fields (I4), and the UDP telemetry packet if enabled (to catch readers outside the physics region).
- **Control:** W1 (v write) in the same block must show an effect. Identity write of `+0x320`.
- **Rule out:**
  - (1) a reader outside the physics region, such as camera, audio, telemetry, or a damage impulse from dv (the SR has not classified .text-wide loads). So, beyond "the trajectory is unchanged", check that *no* observable output differs from the I2/I1 prediction during the write tick.
  - (2) the look-ahead save/restore propagating the written value. It saves and restores `+0x320`, but the tentative integrate overwrites it first and the restore brings back the pre-look-ahead (written) value. So at M2, `+0x320` should still hold the written value: an expected, not refuting, observation.
  - (3) a hook at `rig+0x2550` reading `S+0x70` -> G2 requires the hook to be null.
- **If true:** p, q, v, omega at N = 1..5 bit-exact to the I2 prediction computed without the write. `+0x320` at M3 == v at M2 (overwritten). No difference in wheel or proxy outputs.
- **Refutes:** any deviation of S at N >= 1, or of any logged downstream output, beyond sham variance (which is 0 by W0).
- **Grade if pass:** CONFIRMED (two-source) for "not an integrator input". Only PROBABLE for "no reader anywhere", because the reader census is only as wide as the logged outputs.
- **Residual:** readers that only act on events (crash detection, a damage threshold on dv, replay recording) may not fire in 5 ticks. Add a 60-tick arm with a curb strike right after the write.

#### W6: p is the source (E3, H-015)
- **Rev 2 (merges E-B3, self-heal timing):** primary arm L1 (H1), dp = **+0.05 m** (not +2 m), plus arms L2 and **L0**. Read at H1 (write), H3-entry, H4-entry/return, B2, M2, M3, B1, H5-entry/return, H2, H1 of the next C:
  - S.p; committed `+0x190..` (raw, and decoded where the decode is known); interp `+0x220..`; proxy `pxb+0x1b0/+0x1f0`; proxy matrix `+0x1f0`; `con+0xc930`.
  - **H-SELFHEAL predicted (L1):** proxy and committed old through F.2 and the tick gate of the next F; committed = written + dt·v_new at the H5 return; proxy = committed at the H1 of the next C. **L0 predicted:** the proxy gets the **old** committed pose at this C, stays wrong through a whole F, and heals at the next C.
  - **Refutes H-SELFHEAL:** the proxy is still old after 2 C; S snaps back (then look for another sync); or the proxy updates earlier than predicted (then another push path exists).
  - Grade: CONFIRMED (runtime) for the timing if 20/20. The collision *effect* of the stale window is W11.
  - Rev 1 text below: read "B2" as the L2 arm.
- **Hyp:** p written at B2 is the next-tick integrator input. The car continues from the new p. Wheel points follow. `con+0xcd0` does not track p.
- **Setup:** at B2, `+0x2d0 += (0, +0.05, 0)`; the at-rest arm uses +1.0 m as in E3. Observe N = 1..30.
- **Changed:** p.y.
- **Held:** v, omega, q, basis.
- **State modified:** `+0x2d0`.
- **Capture:** `field_set` plus wheel `+0x50` and `+0x1560`, `con+0xcd0`, proxy fields, and the committed pose `+0x190..` (raw) after the Commit.
- **Control:** an identity write of p; a write at B1 (whose Commit captures it) vs B2 (the next tick's `0x14073d900` sees committed != current. **Watch** the `rig+0x12c8`-gated branch).
- **Rule out:** (1) N1's SetPose path snapping p back (requires `rig+0x3b8 == 0`); (2) the proxy or collision pushing the car back to the old location (this would look like "p lost" but is a proxy effect -> compare with I4 fields); (3) the encoded tick-start pose `+0x100..` or Commit's `0x14075bd70` detecting a jump.
- **If true:** p[1] == written + dt*v_new; the car falls or settles; wheel `+0x50` at N=1 is consistent with the new p; `con+0xcd0` unchanged.
- **Refutes:** p[1] reverts to about p0 + dt*v, or the car snaps back within a few ticks when the proxy fields are unchanged.
- **Grade if pass:** CONFIRMED (two-source) for the rig. The proxy/collision effect goes to W11.
- **Residual:** a +1 m jump at rest may trigger ground-penetration logic. Use the small delta for grading.

#### W7: q is the source; the basis is derived (E4)
- **Rev 2:** primary arm L1. Note that PreTick reads S.p/S.q (`0x140749a8b`) and passes them to the owner (pull check) even when the pull is off. There is no expected effect, but the H4 snapshots confirm it.
- **Hyp:** q written at B2 is the integrator input. The basis `+0x2f0..+0x310` is rebuilt from q at the integrate (`0x140746670..`), but it is read **before** the integrate by `0x14073a2f1` and `0x140730a10` (SR §5.2). So a stale basis perturbs the first tick.
- **Setup:** at B2, write q rotated 5 deg about world Y: arm (i) basis NOT updated; arm (ii) basis written consistently (rows computed from the new q). At rest only, repeat with 90 deg after 5 deg passes.
- **Changed:** q; basis consistency.
- **Held:** p, v, omega.
- **State modified:** `+0x2e0` (arm ii also `+0x2f0..+0x310`).
- **Capture:** N = 0..3; per-wheel `+0x1560`, `+0x16d0`, F/tau at M2 (from I1 snapshot points).
- **Control:** an identity write of q plus basis; arm (ii) vs arm (i).
- **Rule out:** a non-normalised q written by us -> normalise in float32 before writing, and log abs(q).
- **If true:** in both arms the basis at M3 == basis(q_new after integration). In arm (i), F/tau at the write tick differ from arm (ii) (the stale Up was used); in arm (ii) they don't.
- **Refutes:** q reverts; the basis is not rebuilt; or arm (i) == arm (ii) in F/tau. The last would mean the basis is not read before the integrate, which contradicts SR §5.2's CONFIRMED reads.
- **Grade if pass:** CONFIRMED (two-source).
- **Residual:** the magnitude of the stale-basis spike depends on contact state. Test both on the ground and airborne.

#### W8: Async timing realism (how the shipping DR2Hook actually writes)
- **Rev 2:** classification uses `f_seq`/`c_seq` (H3/H1/H2) plus sub-phase IDs. Classes: `L1` (between C return and F entry), `L0` (between F return and C entry), `IN_F:<sub-phase>`, `STRADDLE`.
  - Phase 1a timing: the physics burst is ≤ 1.5 ms per 16.7 ms tick, so about 90% of async writes are expected in L1/L0, and the M1 (L3) loss window is sub-ms. **Predicted loss rate for v writes ≈ fraction landing in L3 or L4, i.e. about 1-3%.** Because the proxy re-syncs, a raw async write's lasting damage is mostly the L0/L1 stale-proxy window, not the S loss.
  - The +0x1338 half-step (C4) is an external marker for "inside ContainerTick, before the rig Update".
  - About 5% of ticks show 5 ms splits (C4), which means the job thread can be preempted mid-F. Report the IN_F rate separately.
  - The Practice Mode F6/F7 path is exactly A-PRES. The same classification tells the project how often the shipping restore lands mid-F.
- **Hyp:** Writes from the Present thread (A-PRES) or from an external process (A-EXT) survive exactly when classified `BETWEEN`, and are lost at the M1-window rate predicted from I1 (the fraction of wall time spent between `0x14074ba92` and `0x14073e232`).
- **Setup:** 200 v writes (delta = +0.5 m/s) at random times via A-PRES, and 200 via A-EXT. Each is classified per §3.2. Also 20 writes in paused state P (only if R1/I1 shows no ticks during pause).
- **Changed:** timing source.
- **Held:** delta, vehicle, state.
- **State modified:** `+0x2b0`.
- **Capture:** `tick_seq`/`phase_id` bracket, and survival by the W1 rule.
- **Control:** B2 hook writes interleaved (100% survival expected).
- **Rule out:** A-PRES writes happening on the physics thread (if Present and the physics run serially on one thread, every A-PRES write is BETWEEN) -> thread IDs from I1.
- **If true:** survival = 100% for BETWEEN, about 0% for MID(M1), 100% for MID(M2/M3); the overall loss rate ~ the M1 time fraction. Pause writes survive with no side effects, or reveal pause/resume code.
- **Refutes:** BETWEEN writes lost (unknown overwriter), or loss not explained by phase.
- **Grade if pass:** CONFIRMED (runtime) for "the loss is a timing race". This directly tells the project whether the savestate must move onto the physics thread.
- **Residual:** at A-EXT, the ~us latency of the syscall bracket limits classification. STRADDLE trials are excluded, and their count must be reported.

#### W9: `con+0xcd0` write has no effect when placement is inactive (H-011 vs H-024; the shipping ApplyState)
- **Rev 2:** R6 found con+0xcd0 constant and 313-499 m from the car, and C only reads it when `con+0xcc0 > 0` (AD §1.1). Prediction strengthened: a write while +0xcc0 == 0 has **no** effect on the rig, proxy, or car copies. **Danger note:** the shipping ApplyState writes +0xcd0. If anything ever sets +0xcc0 > 0 (unknown writer), C would then call SetLinVel(0)/SetAngVel(0)/SetTransform to that point every C. W9 must also log +0xcc0 per F, and abort if it becomes > 0.
- **Hyp:** With `con+0xcc0 == 0`, writing `con+0xcd0` changes nothing in the rig, the proxy, or the car copies. The value is either kept or overwritten by `0x140dbe180`.
- **Setup:** at B2, `con+0xcd0.xyz += (0, 0.05, 0)`; then the shipping-style write (`con+0xcd0` = the "gfxPos" the ApplyState logic would compute).
- **Changed:** `con+0xcd0`.
- **Held:** `con+0xcc0` = 0 (checked every trial).
- **State modified:** `con+0xcd0` (byte-restored after N = 10).
- **Capture:** `field_set`, `con+0xcd0` over N, and the rendered position via car copies.
- **Control:** an identity write.
- **Rule out:** a placement branch firing because we changed `+0xcd0` (it is gated on `+0xcc0`, so this should not happen) -> log `0x140731c50` calls (I3 detour).
- **If true:** rig state bit-exact to the unwritten prediction; `+0xcd0` either persists or is rewritten by the next `0x140dbe180` pass.
- **Refutes (H-024):** the car or camera moves with `+0xcd0` (then it is a visual anchor, as H-011 says).
- **Grade if pass:** PROBABLE (null effects are hard to prove exhaustively).
- **Residual:** the effect of `+0xcd0` when `+0xcc0` > 0 is untested. That is dangerous: the game then calls SetTransform on the car. Out of scope.
- **Depends on:** R6.

#### W10: Minimal vs full raw restore (E11, SR §5.2)
- **Rev 2:** restores at L1 (H1). Set B changes, per AD:
  - **+0x360 does not need restoring** (rewritten every PreTick).
  - **+0x220..+0x2af does not need restoring** (rebuilt every Commit, C6).
  - **+0x190..+0x218 and the proxy heal within 1 F + 1 C** (H-SELFHEAL).
  - Add arm **B−** = Set A + tick-start/history blocks only. It tests whether the self-healing blocks can be dropped. **Refutes H-SELFHEAL** if B− diverges more than B in ticks 1-10.
  - Log con+0xc930 at ticks 0-2 for spikes.
- **Hyp:** Restoring Set A (`+0x2b0..+0x37c`) at B2 diverges from the recorded run more in the first 10 ticks than restoring Set B (A + `+0x170/+0x180`, `+0x190..+0x218`, `+0x100..+0x1ff`, wheel `+0x6c..+0x8c`, `+0x100/+0x104`, `+0x15e0`, `+0x16c4..+0x16ec`, ring buffers `+0x8c..+0xdc`, `+0x2508`, `+0x12c8`; plus the N5 car copies and `rig+0x290/+0x2a0` as an arm B+).
- **Setup:**
  1. Drive 5 s with a **scripted, deterministic input track**, recorded at the input layer and replayable. Without a replayable input, the divergence comparison is meaningless, so a manual-input version is only indicative.
  2. Snapshot at tick k0 (at B2).
  3. Continue for 300 ticks (the reference run).
  4. Restore Set A, B, or B+ at B2.
  5. Replay the same inputs for 300 ticks.
- **Changed:** the restore set.
- **Held:** inputs, vehicle, location, k0.
- **State modified:** the whole set.
- **Capture:** `field_set` every tick for both runs; divergence of p, q, v, omega; spikes in wheel `+0x16d8` and suspension velocity.
- **Control:** (i) a "restore then compare with no replay" determinism test: two reference runs from the same Set-B+ restore must be identical if the sim is deterministic. **Run this first**; if two B+ restores diverge from each other, the divergence metric only measures noise. (ii) An identity restore (snapshot and restore at the same tick).
- **Rule out:** nondeterminism (threads, float mode, input timing) -> control (i). Proxy desync dominating the divergence -> compare with I4 fields.
- **If true:** B and B+ diverge much less than A in ticks 1-10 (the A arm shows `+0x16d8` spikes). Long-horizon divergence is bounded by determinism noise.
- **Refutes (SR §5.2 "must also restore"):** A ~ B, meaning the extra fields don't matter; or B diverges as much as A, meaning hidden state is still missing (the proxy? engine/tyre state?).
- **Grade if pass:** PROBABLE (the set is sufficient for these scenarios). It cannot confirm necessity field by field without ablations. Add a leave-one-group-out ablation if time allows.
- **Residual:** engine, gearbox, tyre temperature, and damage state (`+0x1370`, `+0x1400`, `+0x1448`, `+0x2bd0`) are outside Set B. Expect them to dominate at longer horizons.

#### W11: Raw restore and proxy desync (E12, Priority 3 raw arm, H-023, N4)
- **Rev 2: predictions revised** from "stale for ≥ 1 tick" to a specific window.
  - **L1 restore:** the proxy pose is stale for exactly the next F's F.2 collision prep and tick gate, then healed by C. Proxy v is stale until PostTick of that F.
  - **L0 restore:** the proxy pose is stale for a full F plus the gate. This is the worst case, and it is the arm most likely to show wrong contact.
  - **Native restore at L1 (A4):** no stale window (the setters push immediately).
  - Obstacle test as in rev 1, with arms L1-raw, L0-raw, and L1-native.
  - Extra arm for the gate: restore from a point > 50 m below `G.y` is **not** allowed (unsafe, and G is unknown). Instead, R8 reads G and I1 logs the gate outcome, to confirm the gate never closes in the restores we do.
  - **Refutes H-SELFHEAL:** contact or gate anomalies lasting > 1 F after an L1 restore, or > 2 F after an L0 restore.
  - **Refutes the "proxy matters" claim:** L0-raw contact is indistinguishable from L1-native.
- **Hyp:** After a raw Set-B restore at B2 that bypasses the setters, the `con+0x840` proxy still holds the pre-restore pose and velocity. This yields >= 1 tick of wrong contact, collision, or tick-gate behaviour. The desync ends when some path (Commit? the per-tick update?) re-syncs it.
- **Setup:**
  1. Record a checkpoint 5 m in front of a fixed solid object (a DirtFish barrier or wall) at about 3 m/s.
  2. Drive 50 m away.
  3. Raw-restore at B2.
  4. Observe 60 ticks as the car contacts the object.
  5. Separately, restore at a location far from the current one with no obstacle.
- **Changed:** restore method (raw here; native in A4); distance between the current and restored location.
- **Held:** checkpoint, inputs (none).
- **State modified:** Set B.
- **Capture:** `field_set` plus the I4 proxy fields every tick; the `0x140dbc430` tick-gate outcome; F/tau at M2; `0x14075bd70` side effects if identifiable.
- **Control:** the same checkpoint restored through A4's native path; an identity raw restore (restore to the current state).
- **Rule out:** contact differences caused by missing wheel or suspension state rather than the proxy -> the proxy fields must be *observed* stale, not inferred.
- **If true:** proxy fields keep the old pose for >= 1 tick; contact forces at the object are delayed or absent, or the tick is skipped. The native restore shows neither.
- **Refutes:** the proxy fields jump to the restored pose at the next tick without setter calls (it self-heals), and contact is immediate. Then the "raw restore leaves the proxy stale" claim is wrong or harmless.
- **Grade if pass:** PROBABLE for the causal impact. It becomes CONFIRMED only if a proxy-only raw write (writing the found proxy fields in step with the rig) removes the effect.
- **Residual:** the proxy may be an opaque physics-engine body. Its internal caches can stay stale even if the visible fields match.
- **Depends on:** I4.

### Phase 3: native API calls (highest risk; gated G0-G3; **rev 2: call from the H1 (L1) detour**, formerly the B2 detour; never from Present or an external thread)

Every A-trial snapshots Set B+ and the proxy fields before the call, logs the call arguments and return, and records the Commit and owner-vtable forwarding (`0x140db2190/20d0/2090` detours, log-only).

#### A1: SetLinVel / SetAngVel vs a raw write (Priority 3)
- **Rev 2:** call at L1 (H1) instead of B2, so the setters' proxy push precedes F.2 collision prep. flag=1 sets +0x170 and +0x200 too, so for L1 calls con+0xc930 is normal. Predicted: the proxy v (pxb+0x2e0/+0x2f0) equals the new v at the next H4-entry (before PostTick), unlike the raw L1 write.
- **Hyp:** `SetLinVel(rig, v*, flag)` (`0x14074a910`, via container thunk `0x140731b20/0x140731b30`) sets S.v and forwards to `con+0x840` via `vt+0x28`. With flag=1 it also sets `+0x320`, `+0x170`, `+0x200`. SetAngVel is the same for omega. The result equals the raw write in the rig (W1/W2), except that the proxy is updated.
- **Setup:** at B2, 4 arms: SetLinVel flag 0 / flag 1; SetAngVel flag 0 / flag 1. Use the same delta as W1/W2. Plus the raw equivalents in the same block.
- **Changed:** method, flag.
- **Held:** delta, state.
- **State modified:** v/omega (and the flagged fields).
- **Capture:** N = 0..10; proxy fields; forwarding detour hits.
- **Control:** a call with the current value (identity via API); raw identity write.
- **Rule out:** calling on the wrong object (container vs rig): use the container thunk with `rcx = con`, verify `con+8 == rig`. Stack alignment and ABI errors -> an identity call must leave all fields bit-exact.
- **If true:** rig fields equal the raw-write outcome at N=1 (bit-exact); proxy velocity fields change only in the API arm; `+0x320`/`+0x170`/`+0x200` change only with flag=1.
- **Refutes:** the API result != the raw result in the rig; or no proxy change (the forwarding goes elsewhere); or a crash or instability.
- **Grade if pass:** CONFIRMED (two-source) for the setter semantics.
- **Residual:** `[owner vt+0x28]` side effects inside the proxy (wake-up, contact reset) are not visible.

#### A2: SetTransform (+Commit) vs a raw pose memcpy (Priority 3)
- **Rev 2:** call at L1. Predicted: the proxy pose (pxb+0x1b0/+0x1f0 and the matrix +0x1f0) is new at the next H4-entry, whereas the raw L1 write is still old there (the key contrast). commit=1 also rewrites +0x100 and +0x190. The Commit tail `0x14075bd70` is pure interpolation (AD), so no tamper side effect is expected.
- **Hyp:** `SetTransform(rig, pose*, commit)` (`0x14074ad80`) = SetPose (p, q, basis consistent) + `vt+0x20` forwarding + (if commit) encoding into `+0x100..0x118` and `+0x190..0x1a8`, then tail-calls `Commit(rig,0)`. The rig fields equal a raw memcpy of p/q plus a consistent basis. The proxy and the committed or encoded pose differ.
- **Setup:** at B2, dp = +0.05 m and a 5 deg yaw. Arms: commit 0; commit 1; raw (p, q, basis); raw (p, q, basis, committed and encoded blocks copied from an API-arm snapshot).
- **Changed:** method.
- **Held:** delta.
- **State modified:** pose fields.
- **Capture:** N = 0..30; proxy; `+0x100..0x1a8` raw; `0x14075bd70` hits.
- **Control:** an identity SetTransform (current pose).
- **Rule out:** the encoded-pose obfuscation (per-build constants) -> compare raw bytes, don't decode.
- **If true:** the rig S is bit-exact across arms; only the API arms update the proxy; commit=1 also updates `+0x100/+0x190` blocks.
- **Refutes:** the raw arm with the full blocks behaves differently from the API arm despite identical rig bytes (hidden state exists beyond the visible fields, i.e. in the proxy); or the API arm is unstable.
- **Grade if pass:** CONFIRMED (two-source).
- **Residual:** what `0x14075bd70` does (possibly teleport or anti-tamper bookkeeping) is unknown.

#### A3: Commit and ResetToCommitted semantics (N1, H-029)
- **Rev 2:** R2 shows +0x200 == v at every settled tick, and AD shows Commit is unconditional at EndStep. So the committed state is always the state at the end of the last F. **Prediction: ResetToCommitted at L1 is a no-op on S** (up to per-wheel resets); at L6/L7 it is a rollback of the current F's integrate. It can't serve as a checkpoint. Arm (iii) is kept as confirmation.
- **Hyp:** Commit copies the current pose/v/omega into the committed blocks. Because Commit runs every tick (N1), `ResetToCommitted` (`0x14074a3a0`) at B2 restores the state of the end of the previous tick (a no-op, or a 1-tick rollback), not an earlier checkpoint.
- **Setup:** at B2: (i) an extra `Commit(rig,1)` call; (ii) a `ResetToCommitted` call; (iii) Commit at B1 then ResetToCommitted at B2.
- **Changed:** call.
- **Held:** state.
- **State modified:** via the calls (ResetToCommitted also touches per-wheel state via `0x1407308c0/0x1407308a0`).
- **Capture:** N = 0..10; per-wheel fields.
- **Control:** no call (I1 baseline).
- **Rule out:** -
- **If true:** (i) no change beyond the committed blocks, which are already equal; (ii) S == the committed values, and the wheel fields are reset.
- **Refutes:** the committed state is older than one tick (Commit is not per tick) -> contradicts N1 and I3.
- **Grade if pass:** CONFIRMED (runtime).
- **Residual:** this matters mainly because the SR's "native savestate model" (§4.2) assumes the committed state is a checkpoint. If it is overwritten every tick, it can't serve as one.

#### A4: Full native restore vs raw memcpy (Priority 3 main)
- **Rev 2:** all arms restore at L1 (H1). If H-RV holds (I3), SetFullState via `0x140999990` is the game's own reset path. Also compare con+0xc930 at ticks 0-2 across arms. SetFullState sets +0x170/+0x200 (no spike). A raw Set B restore that includes +0x170 = +0x200 = v should also show no spike.
- **Hyp:** Restoring a checkpoint with the native sequence (SetFullState `0x14074a5e0` with a struct captured by GetSpawnState and patched with the current pose and v/omega at checkpoint time; or SetTransform(commit=1) + SetLinVel(1) + SetAngVel(1)) is stable. It keeps the `con+0x840` proxy consistent. Over 300 replayed ticks it diverges less than the raw Set-B memcpy.
- **Setup:** the W10 protocol, with 4 arms: raw Set B; raw Set B+; native trio; SetFullState. Plus the W11 obstacle scenario for each arm.
- **Changed:** method.
- **Held:** checkpoint, inputs, location, vehicle.
- **State modified:** the full vehicle state.
- **Capture:** `field_set`; proxy fields; the tick-gate outcome; divergence metrics; contact forces at the obstacle.
- **Control:** determinism control from W10(i); identity restores per arm.
- **Rule out:** SetFullState calls `Reset(rig,1)` first, which zeroes timers and ring buffers and sets `+0x2529`. Its divergence may come from that reset, not from better sync -> compare the trio against SetFullState separately.
- **If true:** the native arms show no proxy staleness, no tick skip, immediate contact, and divergence in ticks 1-10 <= raw Set B+.
- **Refutes:** the native arms are unstable (crash, NaN, ejection); or they do no better than raw B+ on both desync and divergence. Then the proxy is not the issue, and the missing state is elsewhere.
- **Grade if pass:** PROBABLE -> CONFIRMED after the full cell matrix (2 vehicles x 2 locations x rest/speed, 20 each).
- **Residual:** SetFullState also rewrites wheel state from the struct and resets engine-side state (`+0x1370` via `keepSome`). Its fidelity to "exact momentum" (ADR-006) may be worse even if it is more stable.
- **Depends on:** I4 (to observe the proxy).

---

## 6. Hypotheses the SR implies but its E-list misses (now covered)

| Gap | Covered by |
|---|---|
| The M1 (lost) vs M2 (kept) contrast inside a tick, the actual falsifier of the SR §3 re-injection claim | W3 |
| A mid-tick M1 write "lost" for S but leaking into per-wheel `+0x16d0` | W3 |
| The per-tick Commit via `0x140dbca20 -> 0x1407511e0` and the pose adjuster `rig+0x3b8` (N1): B1 != B2 | I1, I3, W1 (B1 arm), A3 |
| The proxy gating the rig tick (`0x140dbc430`, N4) | I4, W11 |
| Secondary copies (`car+0x2b0..0x310`, `rig+0x290/+0x2a0`) (N5) | R6 |
| Unopened writers `0x140749a30` / `0x140750f20`, the H-022 hook, freeze/kinematic flags | G2, I5 |
| A bit-exact integrator replay, which settles quaternion order and the omega frame (PROBABLE in the SR) | I2 |
| The shipping restore writes `+0x320` and `con+0xcd0` (N7) | W5(c), W9 |
| Async (Present-thread) timing of the shipping restore | W8 |
| Determinism of the sim, a precondition for any divergence metric (E11) | W10(i) |
| Readers of `+0x320/+0x330` outside the physics region (SR §9 lists this as not traced) | W5 downstream-output capture (runtime). A **static follow-up for the RE Analyst** is still needed: classify .text-wide loads of `[reg+0x320/+0x330]` and `[S+0x70/+0x80]`. |

---

## 7. Places where the static report treats correlation or absence as proof (overclaims)

**Rev 2:**
- The addendum's overclaims are in §R2.5.
- **Self-correction:** this plan's own N1 call path (`0x140731d40` → `0x1407511e0`) was a misread (C1). The "Commit every tick" conclusion was right, but it rested partly on correlation (N2). It is now two-source.
- Items 2, 3, 4 and 5 below were addressed by the addendum's downgrades (AD §0.2), by G0, and by the chain in AD §1.1.
- Item 5's "setters are the only sync path" is now refuted observationally (C8).


1. **§1.3 / §3 "A write ... between ticks survives; a write made inside that window is lost".** This is presented as a finding, but it is a control-flow inference that has not been tested. It also ignores post-Update code: the `0x140dbca20` Commit and a conditional SetPose (N1), plus `0x140dbc100`. "Between ticks" is not a single safe point. The report also ignores that the M1 window still leaks into `+0x16d0`. It should be graded HYPOTHESIS/PROBABLE until W1/W3.
2. **§1.2 "H-002 ... REFUTED" and the KB's "velocity part REFUTED".** What is actually shown is: "`+0x320/+0x330` are not integrator inputs" (CONFIRMED for the integrator path). "Output only" rests on *absence* of readers found in a 0x40000-byte window, and §3 itself grades it PROBABLE while listing the unclassified .text-wide readers and the unknown `[rig+0x2550]` hook. The headline REFUTED overstates this. The correct wording is "REFUTED as the integrator's velocity input; output-only PROBABLE".
3. **§0 "Build match: CONFIRMED".** This is established by four addresses disassembling "as the docs describe". The docs were derived from this binary, so that check is close to circular, and it verifies the file on disk, not the running process (the KB notes this). The SHA-256 is a valid file identity. Process identity needs G0.2-G0.4. The runtime PE timestamp does match (N8).
4. **§2.5 the per-tick call chain, "the one real integration per tick".** The chain is "CONFIRMED by direct calls", but the top-level caller was not resolved, and the chain misses `0x140dbca20`'s per-container Commit and SetPose path (N1) and the proxy-gated skip in `0x140dbc430` (N4). The frozen dump's `+0x200 == +0x2b0` (N2) is runtime evidence that the chain is incomplete.
5. **§4.2 "a second physics representation hangs off container+0x840 and the setters keep it in sync (CONFIRMED forwarding)"** and **§5.2 "raw savestate leaves that proxy stale".** The forwarding is code-confirmed. That the setters are the *only* sync path is inferred from not having seen another. The proxy's role is UNKNOWN. N4 shows it also *feeds* the rig tick gate, a direction the SR does not consider.
6. **§6 `+0x370` mass "CONFIRMED ... live value 1083.53 in the docs matches a car mass".** A plausible magnitude is correlation. The reciprocal relation is solid (N8). "Mass in kg" is PROBABLE. The same applies to "units m/s PROBABLE via -9.81": fine as graded, but it is not evidence of what the game *consumes*.
7. **§4.2 `con+0xcd0` "placement target, not an output derived from +0x2d0".** It is based on finding one writer (`0x140dbe180`). A second writer elsewhere is not excluded. The frozen dump (~64 m from p, `+0xcc0` = 0) supports "not a car-attached anchor" but is one sample. It stays HYPOTHESIS; R6 and W9 test it.
8. **§3 "Setters write them alongside v/omega ... so 'previous = current' after a teleport keeps finite-difference consumers quiet".** This is a motive attributed to the code without an identified consumer. It is circular as support for the output-only claim.
9. **§2.3 "0x140746150 is the rigid-body integrator ... CONFIRMED (structure)".** Fine as structure. The equations (product order, frame) are PROBABLE until I2 replays them bit for bit.

---

## 8. Missing preconditions and data (blocking or degrading)

**Rev 2 status:**
- (1) G1 ran on substitute criteria: PROBABLE-offline; strict G1.1 not met. Writes stay blocked until the Orchestrator accepts or G1.1 isolation is set up.
- (2) G0 is done (PASS; 5 foreign `e9` detours outside the physics code).
- (3) MC and Phase 1a data now exist for R1-R6. Still missing: `con+0x14` series (R7), gate globals (R8), `con+0xc930` (R9), a second vehicle and location, curb/airborne arms, and the frame-cap arm.
- (4) The harness is being built. It must add H1-H6 (§R2.3).
- (6) Static follow-ups now: bodies of F.1 `0x140dadc00`, F.2 `0x140dbe300`, F.4 `0x140dbd740`, F.5 `0x140dbd900`, `0x140db2220` (for S writes); the reader of `con+0xc930`; `0x1409cdbb7`→`0x14073b620` (H-025 second writer); the Commit-tail callees; the reset message consumer.
- (8) Practice Mode must be disabled or its keys logged during trials.

Rev 1 list:

1. **Offline verification: BLOCKING for all writes and hooks.** DR2Hook's SafetyGuard is hard-wired to permissive with session address 0 (N6), and the session-mode enum is invented (Q-007). G1's OS-level isolation plus operator attestation is the only gate currently available, and it has not been set up. Recommend to the RE Orchestrator: fix `core_module.cpp` (never ship `SetPermissiveMode(true)`) and open an RE task for the real session-mode field.
2. **Runtime build identity (G0.2-G0.4).** The disk SHA-256 is in the SR. The running process has not been checked against it. The PE timestamp from the memory header matches (13:31:31 BRT = 16:31:31 UTC).
3. **Memory Cartographer cross-check: mostly missing.** Present: one frozen dump (`raw/*.bin`, `static_search_frozen.txt`, `frozen_snapshot.csv`, 11:38-11:53 BRT) and the sampler, analysis, and prediction scripts. **Missing:** any sampler `.csv/.bin` series, `analysis.txt`, `predictions.txt`, `lag_equality.csv`, `settled_series.csv`; wheel-window captures; a `[con+0x840]` proxy dump; a dt/tick-period measurement; a pause-state tick check. R1-R6, I4, W9, W11, and A4 depend on these (the starred items).
4. **Harness does not exist.** The phase-point detours, `tick_seq` shared memory, the physics-thread writer, and the CSV logger all need to be built as a separate test core. Nothing in the current tree provides physics-thread timing: `Player::ApplyState` runs from Present.
5. **Deterministic input replay** for W10/A4. It is not known whether the game or DR2Hook can inject scripted inputs. Without it, the divergence comparisons are only indicative.
6. **Static follow-ups** (RE Analyst):
   - .text-wide classification of loads of `+0x320/+0x330`
   - `[rig+0x2550]` hook implementations
   - `[rig+0x3b8]` adjuster class (N1)
   - `0x140df7b50` and the tick-gate constant `0x141227fe0` (N4)
   - `0x14075bd70` (Commit tail)
   - `0x140749a30`, `0x140750f20`, `0x140745b60`
   - the order of `0x1404b1110 -> 0x140dbca20` relative to the container tick
7. **Second vehicle and location** for the cell matrix: the operator must choose them (at least two with different mass).

---

## 9. CSV logging

Template: `/workspace/validation/INV-01-log-template.csv`. Long format: **one row per (trial, event, relative tick, field)**. Run metadata rows use `event=RUN_META`. Column definitions:

| column | meaning |
|---|---|
| `run_id` | UUID per session run |
| `experiment_id` | G0..G3, R1..R6, I1..I5, W0..W11, A1..A4 |
| `arm` | the arm label within the experiment (e.g. `M1`, `M1plus`, `flag1`, `setA`) |
| `trial_idx` | trial number in the run |
| `block_idx`, `order_in_block`, `rand_seed` | randomisation audit |
| `condition` | `TREATMENT` / `SHAM` / `IDENTITY` / `DEAD_FIELD` / `REFERENCE` / `OBSERVE_ONLY` |
| `build_sha256` | disk hash (full) |
| `pe_timestamp_hex` | from the mapped header (`0x605cbae3`) |
| `code_sig_ok` | G0.4 result |
| `offline_gate_status` | `PASS` / `FAIL` / `INDETERMINATE` (writes only if PASS) |
| `offline_evidence_ref` | paths to the `ss` snapshot, log excerpt, screenshot |
| `game_mode` | `DIRTFISH_FREEROAM` / `TIMETRIAL_OFFLINE` / `CUSTOM_CHAMP` |
| `location`, `vehicle` | free text, exact in-game names |
| `confound_flags` | hex bitmask: bit0 hook `+0x2550`, bit1 adjuster `+0x3b8`, bit2 freeze `+0x2548`, bit3 kinematic `+0x2560`, bit4 placement `+0xcc0` |
| `car_state` | `REST` / `SPEED20` / `CREEP` / `AIRBORNE` |
| `speed_mps` | abs(v) at the write |
| `lookahead_gate` | 1 if abs(`+0x2508`) > 0.89408 |
| `timing_method` | `HOOK_B2` / `HOOK_B1` / `HOOK_M1` / `HOOK_M1PLUS` / `HOOK_M2` / `HOOK_M3` / `ASYNC_EXT` / `ASYNC_PRESENT` / `PAUSED` / `API_B2` / `NONE` |
| `phase_class` | `BETWEEN` / `MID_M1` / `MID_M2` / `MID_M3` / `STRADDLE` / `NA` |
| `phase_verified` | Y/N (§3.2 data checks) |
| `phase_evidence` | free text, e.g. "+0x170==B2.v; S.v@M2==+0x170" |
| `thread_id`, `physics_thread_id` | - |
| `tick_seq_before`, `tick_seq_after`, `phase_id` | seqlock bracket |
| `game_tick_counter` | `rig+0x1338` raw hex (and/or `+0xdc`) |
| `dt_s` | the `xmm1` argument of `0x14074b8f0` for this tick |
| `frame_idx` | Present count |
| `rel_tick` | 0 = the write tick (the phase given by `event_phase`); negative = before; N = N ticks after |
| `event` | `RUN_META` / `GATE` / `SNAPSHOT_ORIG` / `WRITE` / `READBACK` / `OBSERVE` / `API_CALL` / `RESTORE` / `RESTORE_VERIFY` / `ABORT` |
| `event_phase` | the phase point where this row was sampled (B2/M1/M2/M3/B1/EXT) |
| `host_mono_ns` | `CLOCK_MONOTONIC` ns |
| `wall_time_local` | ISO-8601 with offset, e.g. `2026-09-30T14:03:11.123-03:00` |
| `region` | `rig` / `con` / `car` / `wheel0..3` / `proxy` / `data` |
| `base_addr`, `offset`, `abs_addr` | hex |
| `field` | canonical name, e.g. `S.v`, `prev_v`, `v_tickstart`, `committed_v`, `con.cd0` |
| `width_bytes`, `dtype` | e.g. 12, `f32x3` |
| `before_hex` | original bytes at the write point |
| `written_hex` | bytes written (empty for OBSERVE) |
| `after_hex` | bytes observed at this row's `rel_tick`/`event_phase` |
| `after_f0..after_f3` | float decode of `after_hex` |
| `expected_hex` | model prediction (I2 replay or rule) |
| `expected_rule` | e.g. `prev_v@M3 == written` |
| `match` | `EXACT` / `ULP1` / `APPROX` / `MISMATCH` / `NA` |
| `abs_err`, `ulp_err` | - |
| `api_fn`, `api_args_hex`, `api_ret` | Phase 3 only |
| `restore_hex`, `restore_ok` | restore rows |
| `abort_reason` | - |
| `operator`, `notes` | - |
| **rev 2:** `landing_point` | `L0`..`L8`, `L5b`, `NA` (the addendum's IDs; always set together with `timing_method`) |
| `hook_addr`, `hook_kind`, `ret_filter` | e.g. `0x140dbca20`, `RETURN`, `0x14073e314` |
| `f_seq`, `c_seq`, `frame_seq` | F / C / Present counters at the row (they replace `tick_seq` as the primary tick) |
| `container_idx`, `on_worker_thread` | the player's container index in `[0x14201b7b0]`; Y if F.3 ran it on a worker |
| `g_pull_con14`, `g_adj`, `g_place` | `con+0x14` float, `rig+0x3b8` hex, `con+0xcc0` float at the write |
| `tick_gate_ran` | Y/N: B2 fired in this F after H4 |
| `c930_hex` | `con+0xc930` at the H3 return of the write F (spike discriminator) |
| `alpha` | Commit interpolation α, if recovered (I6) |
| `timer_halfstep` | Y if +0x1338 has advanced by exactly one dt in the current F |
