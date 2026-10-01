# OPEN QUESTIONS

Source: /workspace/orchestrator/inventory-v1.md (RE Orchestrator, 2026-09-30); graded only on evidence recorded in /workspace/DR2ModLoader docs/reverse_engineering. Offsets are relative to the rig (PhysicsRig) unless stated.
A dash (—) means the field was not recorded in the source.

## Open investigations

### INV-01 — Source of truth for the car state
- Status: OPEN (registered 2026-09-30 at RE Orchestrator's request)
- Goal: determine what the integrator reads as the source of truth for position, orientation and velocities, and where a restore must write for the state to stick. The savestate depends on this.
- Static half: RE Analyst, brief /workspace/orchestrator/briefs/INV-01-static.md. Questions: writers of +0x2b0, +0x180, +0x2d0, +0x2e0; whether +0x320/+0x330 feed the next tick; whether 0x14073e266 / 0x14073e3ed are a native reset/snapshot (fields, callers, teleport/reset_vehicle strings); sub-step or per-wheel state a restore must reset.
- Runtime half: Memory Cartographer, brief /workspace/orchestrator/briefs/INV-01-runtime.md. Questions: per-tick update order of +0x180, +0x2b0, +0x2c0, +0x2d0, +0x2e0, +0x320/+0x330 (origin vs copies); whether container+0xcd0 derives from +0x2d0; secondary (previous/interpolated) state copy; exe hash and PE timestamp. Captures go to /workspace/captures/INV-01/.
- Constraint: read-only on the user's machine; no game memory writes.
- Known going in: F-001, F-003, F-004, F-008 (do not re-investigate). Candidates: snapshot/restore 0x14073e266 / 0x14073e3ed; no isolated writer of +0x2d0/+0x2e0 found yet.
- Related: H-002, H-008, H-011, Q-002, Q-008.
- Static half: REPORTED 2026-09-30 — /workspace/analyst/INV-01-static-report.md. Recorded as H-013..H-031, H-R05, Q-010..Q-020, C-004; H-002 split. All single-source static.
- Static blocker: CopyToBox refused the exe (outside allowed local-exec root); analysis ran read-only via objdump on the user's machine. Disassembly copies in /workspace/analyst/dis/.
- Proposed experiments E1..E12 for the Validation Specialist: report §8 (not duplicated here).
- Runtime Phase 1a (Validation Specialist): REPORTED 2026-09-30 — /workspace/validation/INV-01-phase1a.md; data /workspace/validation/phase1a/. Recorded as H-032..H-041, H-R06, H-R07, Q-022..Q-027, C-005; H-025 PROBABLE-refuted; H-011 refuted in placement-inactive state. One car/location/session; operator F5–F7 confirmation PENDING.
- Static addendum (RE Analyst): REPORTED 2026-09-30 — /workspace/analyst/INV-01-static-addendum.md. Recorded as H-042..H-056, H-R08..H-R10, Q-028..Q-034, C-006; resolves C-005; Q-011/Q-012/Q-014/Q-020 answered or proposed. Revised experiments E-B1..E-B11 in addendum §7; 'write between ticks' now means 'write at L1'.
- Build identity: CONFIRMED by two sources (F-011).
- Runtime half (Memory Cartographer full analysis): REPORTED 2026-09-30 — /workspace/captures/INV-01/REPORT.md (rev. 2). Promotions F-012..F-014 per RE Orchestrator ruling; H-057, H-058, H-R11, Q-035, Q-036 added. INV-01 stays OPEN.
- Result: OPEN (not closed; awaiting runtime cross-check).

## Unknowns

### Q-001 — Consumer of the 4x4 table rig+0x12dc
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: F-007
- Related functions: —
- How validated: —
- What remains unknown: —
- Update 2026-09-30: 0x140739700, called at 0x14073e458 inside the look-ahead, touches the 4x4 table rig+0x12dc (H-018). Consumer still not established; stays OPEN.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### Q-002 — Meaning of copied fields +0x2b0/+0x320/+0x330 and +0x180/+0x2c0/+0x210
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: F-008, H-002
- Related functions: —
- How validated: —
- What remains unknown: —
- Update 2026-09-30: Static report proposes meanings (H-015/H-016/H-017, H-029). Stays OPEN pending runtime cross-check.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### Q-003 — Physics of wheel+0x10
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-R03
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### Q-004 — rig+0x370
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: RPM mapping reverted in commit c520018
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: —
- Update 2026-09-30: Static report grades +0x370 = mass, CONFIRMED by analyst (H-028). Stays OPEN pending cross-check.
- Update 2026-09-30: Phase 1a: +0x370 = 1083.53 constant, reciprocal of +0x374 (H-036). Stays OPEN pending Chief ruling.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### Q-005 — Accumulator +0x2d00+4i and its thresholds
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### Q-006 — car+0x130/+0x160 and exe+0x15a3607
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### Q-007 — Session mode
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: The enum in safety.h is invented, not derived from the game.
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### Q-008 — Game version / exe hash
- Status: ANSWERED 2026-09-30 — see F-011
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: Not recorded; INV-01 runtime question 4 asks for exe hash and PE timestamp. All addresses in the KB are 'this build' until it is recorded.
- Update 2026-09-30: Build identity recorded in H-013: SHA-256 c119f509…3442, PE timestamp 2021-03-25. Runtime brief Q4 should confirm.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### Q-009 — Anti-cheat
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### Q-010 — Final handler of driving.reset_vehicle
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: /workspace/analyst/INV-01-static-report.md §4.3
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: —
- Update 2026-09-30: Addendum: input check posts message types 0x32/0x53; consumer still UNKNOWN (H-054). Stays OPEN.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### Q-011 — Implementations behind the [rig+0x2550] hook
- Status: PROPOSED ANSWER 2026-09-30: no hook installer exists; +0x2550 always null PROBABLE (H-052), single-source static
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-022
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### Q-012 — Function 0x140745b60 (back-to-world transform, PROBABLE)
- Status: ANSWERED 2026-09-30 (single-source static): pure rotation q⊗v⊗q*, H-056
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### Q-013 — Full body of 0x14073e570: do sub-iterations write persistent rig state?
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-021
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: —
- Update 2026-09-30: Addendum: 0x14073e570 still UNKNOWN for persistent writes. Stays OPEN.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### Q-014 — Commit 0x14074d190 after the commit stores (0x14075bd70 etc.)
- Status: ANSWERED 2026-09-30 (single-source static): Commit body read, H-045
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### Q-015 — .text-wide readers of +0x320/+0x330 outside the physics region
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-016
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: —
- Update 2026-09-30: Addendum whole-.text scan (H-053): only rig-based loads are the look-ahead save; no getter exposes +0x320/+0x330. Residual: 20 non-physics float loads untraced. Stays OPEN.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### Q-016 — ~80 +0x2d0/+0x2e0 stores outside the physics region (e.g. 0x1406c6288)
- Status: OPEN
- Grade: UNKNOWN (rig ownership judged unlikely, not refuted)
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### Q-017 — Meaning of dword +0x378
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: Saved/restored by look-ahead (0x14073e25f/0x14073e403)
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### Q-018 — Identity of the object at container+0x840
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-023
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: —
- Update 2026-09-30: Addendum: proxy lives in engine region 0x140dd0000–0x140e22000; several functions there show rigid-body-like fields (HYPOTHESIS: same base class). Stays OPEN.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### Q-019 — Purpose of encoded tick-start pose +0x100..+0x118 and history +0x11c..+0x16f
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-029
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: —
- Update 2026-09-30: Addendum: Commit lerps prev pose +0x100.. with committed +0x190.. into +0x220 (H-045), suggesting +0x100 is the previous pose for render interpolation. Single-source; stays OPEN until cross-checked.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### Q-020 — Unopened writers 0x140749b7c, 0x140750ff9 (+0x2b0) and 0x140749b88 (+0x2c0)
- Status: ANSWERED 2026-09-30 (single-source static): 0x140749b7c/0x140749b88 = PreTick pull writes under G-pull (H-043); 0x140750ff9 writes wheel mount pos, not S (H-R10)
- Grade: UNKNOWN (target)
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### Q-021 — Is the object at the rig address RTTI DynamicsCarImpl? (PhysicsRig = DynamicsCarImpl not established)
- Status: OPEN (RE Orchestrator ruling 2026-09-30)
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-006, glossary 'rig'
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from RE Orchestrator ruling   Last changed: 2026-09-30

### Q-022 — What rig+0x1334 counts (rises 0–6 per tick, mostly 3)
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: /workspace/validation/INV-01-phase1a.md §R1
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### Q-023 — Solver sub-iteration count n (16 inferred, not observed)
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-021, H-032
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### Q-024 — Does the physics tick rate depend on frame rate?
- Status: ANSWERED 2026-09-30 for this session: no — fixed 60 Hz step decoupled from ~92–101 Hz render updates (F-014); other frame rates not tested
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: Frame-cap arm not run (/workspace/validation/INV-01-phase1a.md §R1)
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### Q-025 — Identity of the second writer of per-wheel +0x1560/+0x1690
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-041
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### Q-026 — Is the per-tick proxy sync a call inside Commit (I3), and does a raw restore get re-synced (W11)?
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-023, H-039
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### Q-027 — Does anything read container+0xcd0? (W9)
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-024, H-040
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: —
- Update 2026-09-30: Cartographer rev. 2: cd0/cc0 constant during driving; to close, sample across reset/restart/stage start.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### Q-028 — Order of F vs C within a frame, F calls per frame, and F's thread relative to Present
- Status: OPEN
- Grade: UNKNOWN (H-ORD HYPOTHESIS)
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-042, H-047; test E-B1
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### Q-029 — Runtime value of G (0x1420202d0) and flag [0x14201b788]
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-048; test E-B8
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### Q-030 — What sets con+0x14 below 0.1 (turns the pull on)?
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-049; writers outside 0x140da0000–0x140dc0000 not searched
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### Q-031 — Class/vtable of the adjuster object at rig+0x3b8 ([mgr+0x338])
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-051
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### Q-032 — Full bodies of 0x140db2220 (PostTick) and 0x140dbe300 (proxy prep)
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### Q-033 — Effect on contacts of one frame of stale proxy pose
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-050, H-023; E12
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### Q-034 — Which container index is the player's car?
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: H-042, H-047, H-001
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### Q-035 — Meaning of rig+0x80, +0xe0, +0x100, +0x110, +0x190, +0x1a0, +0x340/+0x350 (transient per tick), car+0x2a0, car+0x2d0, car+0x2f0
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: Q-019, H-038; report §(f).4
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from Memory Cartographer INV-01 report rev. 2   Last changed: 2026-09-30

### Q-036 — Runtime writer identity of each field (e.g. is +0x320 written by 0x140746150; does +0x170 come from 0x14074b8f0?)
- Status: OPEN
- Grade: UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: —
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: Sampling proves order/equality only. Closing needs hardware write watchpoints (not read-only: attaches and stops threads — needs user approval) or log-only hooks I1/I3.
- Recorded: 2026-09-30 by Knowledge Keeper from Memory Cartographer INV-01 report rev. 2   Last changed: 2026-09-30

## Open contradictions

### C-001 — suspension.md vs CSV on disk
- Status: OPEN CONTRADICTION
- Grade: —
- Address: —
- Structure: —
- Function: —
- Meaning: suspension.md describes 2065 datagrams / 99.9 s; the CSV on disk has 105953 rows / 580.8 s.
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: —
- Related fields: F-002
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### C-002 — SafetyGuard and missing APIs
- Status: OPEN CONTRADICTION
- Grade: —
- Address: —
- Structure: —
- Function: —
- Meaning: SafetyGuard is permissive (core_module.cpp:101). getVehicleTelemetry/onStageStart do not exist.
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### C-003 — Refuted wheel+0x00 compression still in ADR-004 and code
- Status: OPEN CONTRADICTION
- Grade: —
- Address: —
- Structure: —
- Function: —
- Meaning: H-R04 is refuted but ADR-004 and the code still use it.
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### C-004 — container+0xcd0: visual anchor (vehicle_transform.md, H-011) vs placement target (H-024)
- Status: OPEN CONTRADICTION (both sides HYPOTHESIS)
- Grade: —
- Address: —
- Structure: —
- Function: —
- Meaning: vehicle_transform.md reads container+0xcd0 as a visual anchor derived from physics; the static report finds it written by 0x140dbe180 (lerp + atan2) and consumed as a SetTransform target.
- Evidence: Repo vehicle_transform.md; /workspace/analyst/INV-01-static-report.md §4.2
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: Runtime E3 / runtime brief Q2 should decide.
- Update 2026-09-30: Phase 1a: H-011 refuted in the placement-inactive state; H-024 supported (H-040). Stays OPEN for the placement-active state and for readers (Q-027).
- Update 2026-09-30: H-011 (visual anchor) REFUTED in normal driving (two runtime sources). The cartographer notes cd0 matched, in x and z, a second object reachable from [con+0x840] in the frozen snapshot (placement/reset target possible, unproven). Contradiction left OPEN (no ruling to resolve).
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### C-005 — Commit in the per-tick path: static chain (H-021, report §2.5) vs runtime (H-033/H-034)
- Status: RESOLVED 2026-09-30 (static addendum H-044, single-source static, agrees with Phase 1a runtime; per RE Orchestrator)
- Grade: —
- Address: —
- Structure: —
- Function: —
- Meaning: The static per-tick chain does not include Commit; Phase 1a observes +0x200/+0x210 ← v/ω every tick right after the integrate (100%).
- Evidence: /workspace/analyst/INV-01-static-report.md §2.5; /workspace/validation/INV-01-phase1a.md §R2
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: Where Commit is called from each tick (I3).
- Update 2026-09-30: Commit runs every tick via 0x140dbc988 → thunk 0x140731d50 → EndStep 0x1407511e0 → Commit. The earlier static chain (H-021) did not cover EndStep. N1's call path via 0x140731d40 is REFUTED (H-R08).
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### C-006 — rig+0x290/+0x2a0: filtered-velocity EMA (Phase 1a, H-038) vs Commit lerp(+0x170, +0x200, alpha) (addendum, H-045)
- Status: OPEN — lerp reading (H-045) PROBABLE and favoured; EMA reading (H-038) disfavoured; E-B10 decides
- Grade: —
- Address: —
- Structure: —
- Function: —
- Meaning: Phase 1a fits +0x290 approximately with an EMA a = 0.835 (rms 2.8e-3); the addendum reads Commit writing +0x290 = lerp(+0x170, +0x200, alpha) and +0x2a0 = lerp(+0x180, +0x210, alpha).
- Evidence: /workspace/validation/INV-01-phase1a.md §R6; /workspace/analyst/INV-01-static-addendum.md §1.2
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: Test E-B10 (log alpha).
- Update 2026-09-30: Runtime (cartographer rev. 2): blend fraction correlates with time since commit (r 0.77 / 0.82) at render rate. RE Orchestrator: lerp reading PROBABLE, EMA reading disfavoured; stays OPEN until E-B10.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

