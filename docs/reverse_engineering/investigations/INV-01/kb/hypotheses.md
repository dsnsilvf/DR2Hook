# ACTIVE HYPOTHESES

Source: /workspace/orchestrator/inventory-v1.md (RE Orchestrator, 2026-09-30); graded only on evidence recorded in /workspace/DR2ModLoader docs/reverse_engineering. Offsets are relative to the rig (PhysicsRig) unless stated.
A dash (—) means the field was not recorded in the source.

Grades PROBABLE and HYPOTHESIS are kept as written in inventory v1. Neither is a fact.

## Active

### H-001 — Rig chain points to the active player car
- Status: ACTIVE
- Grade: PROBABLE
- Address: See F-001
- Structure: —
- Function: —
- Meaning: —
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: Medium
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### H-002 — Position +0x2d0; velocities +0x320/+0x330
- Status: SPLIT 2026-09-30: velocity part REFUTED — two sources (static RE Analyst + runtime Memory Cartographer / Phase 1a; RE Orchestrator ruling; see F-013); position part superseded by H-015 → F-012
- Grade: PROBABLE
- Address: rig+0x2d0; rig+0x320 / +0x330
- Structure: —
- Function: —
- Meaning: —
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: Medium
- Related fields: +0x2b0 (copy source, F-008), +0x2e0 (F-004)
- Related functions: 0x140746150
- How validated: —
- What remains unknown: Velocity unit not measured. No isolated writer of +0x2d0 found (INV-01).
- Update 2026-09-30: Per INV-01 static report §3 (and RE Orchestrator's instruction), +0x320/+0x330 are previous-tick v/ω, output only (H-016); live velocity is +0x2b0 (v) / +0x2c0 (ω) (H-015). Position +0x2d0 graded CONFIRMED by the analyst as integrator input (H-015), single-source.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### H-003 — Names track/wheelbase for +0x2514/+0x2518
- Status: ACTIVE
- Grade: PROBABLE
- Address: rig+0x2514 / +0x2518
- Structure: —
- Function: —
- Meaning: —
- Evidence: inventory v1 (RE Orchestrator), repo evidence; arithmetic itself confirmed (F-006)
- Confidence: Medium
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### H-004 — Wheel suspension state integrator (wheel+0x80 / rig+0x1500)
- Status: ACTIVE
- Grade: PROBABLE
- Address: rig+0x1500
- Structure: —
- Function: —
- Meaning: Integrator, clamped to +/-20.
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: Medium
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: h (step) UNKNOWN.
- Update 2026-09-30: Static report §5.2 identifies rig+0x1500 as wheel +0x80 (RL) in the native state struct, clamped by SetFullState. Terminology note: 'integrator' here is not the rigid-body integrator 0x140746150 (H-014). Entry unchanged.
- Update 2026-09-30: Renamed by RE Orchestrator ruling (was 'Integrator +0x1500') to avoid colliding with the rigid-body integrator 0x140746150 (H-014). Content and grade unchanged.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### H-005 — Engine speed, rev limiter, gear
- Status: ACTIVE
- Grade: PROBABLE
- Address: rig+0x13d8 engine speed (rad/s); +0x140c rev limiter (785.398 = 7500 RPM); +0x1448 gear (10 = reverse)
- Structure: —
- Function: —
- Meaning: —
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: Medium
- Related fields: +0x370 (Q-004), +0x1400 (H-010)
- Related functions: —
- How validated: —
- What remains unknown: —
- Update 2026-09-30: Static report §4.2: SetFullState writes +0x13d8 ← s+0x40 and +0x1448 ← s+0x44 (resets +0x1460 on change). Consistent; grade unchanged.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### H-006 — DynamicsCarImpl size 0x3130
- Status: ACTIVE
- Grade: PROBABLE
- Address: —
- Structure: DynamicsCarImpl
- Function: —
- Meaning: —
- Evidence: inventory v1 (RE Orchestrator), repo evidence; no RTTI address recorded
- Confidence: Medium
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: —
- Update 2026-09-30: RE Orchestrator ruling: PhysicsRig = DynamicsCarImpl is NOT established; kept separate. See Q-021.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### H-007 — Damage channel table 0.10/0.05/0.05/0.90
- Status: ACTIVE
- Grade: PROBABLE
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: inventory v1 (RE Orchestrator), repo evidence. The repo doc labels it confirmed, but no xref is recorded; grade kept at PROBABLE.
- Confidence: Medium
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### H-008 — Native tabs with 24 rows; PE/toolchain; teleport/reset_vehicle strings
- Status: ACTIVE
- Grade: PROBABLE
- Address: —
- Structure: —
- Function: —
- Meaning: Native tabs have 24 rows (not validated in game). PE sections, MSVC/LTCG. Existence of the teleport/reset_vehicle strings.
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: Medium
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: Tabs not validated in game. Callers of the strings are part of INV-01 question 3.
- Update 2026-09-30: Static report §4.3 locates the reset_vehicle strings and finds the debug teleport consumer probably compiled out (H-030).
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### H-009 — Alternative pointers
- Status: ACTIVE
- Grade: HYPOTHESIS
- Address: exe+0x15a4b00, exe+0x15a9760 (fallbacks for car); exe+0x201b7c0, exe+0x20203a8 (fallbacks for container)
- Structure: —
- Function: —
- Meaning: —
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: Low
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: —
- Update 2026-09-30: Base and roles clarified by RE Orchestrator: all four are exe-relative (gameBase+); sources src/core/player.cpp:46,74 and physics_rig.md:49-60.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### H-010 — Idle, max power, gear count
- Status: ACTIVE
- Grade: HYPOTHESIS
- Address: +0x8e8 idle; +0x918 max power; +0x8f4 / +0x1400 gear count
- Structure: —
- Function: —
- Meaning: —
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: Low
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### H-011 — Visual anchor container+0xcd0
- Status: REFUTED IN NORMAL DRIVING 2026-09-30 — two runtime sources (Phase 1a H-040; Memory Cartographer rev. 2: con+0xcd0/+0xcc0 constant over 4 961 + 3 778 ticks, 91–560 m from the car, 0 equalities at any lag); RE Orchestrator ruling. Not tested across reset/placement.
- Grade: HYPOTHESIS
- Address: container+0xcd0
- Structure: —
- Function: —
- Meaning: Visual anchor; y about physics - 0.44 m.
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: Low
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: Whether it derives from +0x2d0 (INV-01 runtime question 2).
- Update 2026-09-30: Competing reading H-024 (placement target) from INV-01 static report; contradiction logged as C-004. H-011 unchanged.
- Update 2026-09-30: Phase 1a: container+0xcd0 constant for 45 s while p changed in 2432 ticks, 313–499 m from the car (H-040). H-024 supported.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### H-012 — The 13 questions in ui_limits.md
- Status: ACTIVE
- Grade: HYPOTHESIS
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: inventory v1 (RE Orchestrator), repo evidence; see ui_limits.md in the repo (not yet itemized in the KB)
- Confidence: Low
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

## INV-01 static findings (single-source, pending runtime cross-check)

Grades below are the RE Analyst's, as written. They are NOT KB confirmed facts until cross-checked.

### H-013 — Build identity of the analysed dirtrally2.exe
- Status: PROMOTED 2026-09-30 to F-011 (RE Orchestrator ruling: CONFIRMED by two sources)
- Grade: CONFIRMED FILE-ONLY (analyst build match on the on-disk exe); running image not yet checked (downgraded by RE Orchestrator ruling)
- Address: —
- Structure: —
- Function: —
- Meaning: SHA-256 c119f509fc315d8168aa7aaa5d0a1ee8e3570c7220bd6cbb2d337d6725d73442; size 24,668,160 B; PE TimeDateStamp 'Thu Mar 25 13:31:31 2021' (objdump, user machine local zone); ImageBase 0x140000000; .text 0x140001000 (0x109d22e), .rdata 0x14109f000, .data 0x141572000, .pdata 0x1420a3000. Path /mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0/dirtrally2.exe.
- Evidence: /workspace/analyst/INV-01-static-report.md §0; known addresses 0x140746150, 0x140746700, 0x14073e266, 0x14073e3ed disassemble as the docs describe.
- Confidence: High for the binary identity
- Related fields: Answers Q-008 pending cross-check (runtime brief Q4 also records hash/timestamp)
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: Runtime confirmation that the running process is this binary.
- Update 2026-09-30: Phase 1a G2 at runtime: con+0 = 0x141400c30 (consistent with H-031); running-image hash still not checked.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-014 — 0x140746150 is the rigid-body integrator
- Status: PROMOTED 2026-09-30 to F-012 (RE Orchestrator ruling); quaternion order / world-frame ω / units remain PROBABLE as noted in F-012
- Grade: CONFIRMED (structure); quaternion product order and world-frame ω PROBABLE; units m/s, rad/s PROBABLE
- Address: —
- Structure: —
- Function: 0x140746150(S = rig+0x2b0, xmm1 = dt)
- Meaning: Reads v +0x2b0, ω +0x2c0, p +0x2d0, q +0x2e0, F +0x340, τ +0x350, diagonal inertia +0x360, 1/mass +0x374. Writes in order: +0x320/+0x330 ← old v/ω; v += dt·invMass·F; ω += dt·(rotated I⁻¹τ); p += dt·v_new (semi-implicit Euler); q += dt·½(ω⊗q), normalized; basis +0x2f0..+0x310 rebuilt from q; F and τ cleared. The earlier 'copy +0x2b0 → +0x320/+0x330' reading is the first instructions of this routine.
- Evidence: /workspace/analyst/INV-01-static-report.md §1.1, §2.3 (excerpts 0x1407461ad..0x140746698; constants 0x1411f7ca4=0.5, 0x1411f7cb8=1.0, 0x1412b04c8=−9.81)
- Confidence: High for structure (static only)
- Related fields: F-004, F-008, H-015, H-016, H-028
- Related functions: 0x140edf760 (1/|q|), 0x14072f8e0 (rotate), 0x140745b60 (back to world, PROBABLE; not opened)
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: 0x140745b60 not opened (Q-012).
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-015 — Car-state source of truth: +0x2b0 v, +0x2c0 ω, +0x2d0 p, +0x2e0 q
- Status: PROMOTED 2026-09-30 to F-012 (RE Orchestrator ruling)
- Grade: CONFIRMED (as the integrator's inputs)
- Address: rig+0x2b0 (linear velocity), +0x2c0 (angular velocity), +0x2d0 (position), +0x2e0 (quaternion); S = rig+0x2b0
- Structure: —
- Function: —
- Meaning: These fields are what the next tick integrates from.
- Evidence: /workspace/analyst/INV-01-static-report.md §1.2, §6
- Confidence: High (static only)
- Related fields: H-002 (position part), F-004, H-016, H-017
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: Runtime tests E1, E3, E4, E5 (report §8). Whether the [rig+0x2550] hook edits S (Q-011).
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-016 — +0x320/+0x330 are the previous tick's v/ω, output only
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst); pending runtime cross-check by Memory Cartographer (INV-01)
- Grade: PROBABLE (downgraded by RE Orchestrator ruling; analyst had also written 'CONFIRMED for the integrator path')
- Address: rig+0x320 (prev v), rig+0x330 (prev ω)
- Structure: —
- Function: —
- Meaning: Written from S+0x00/S+0x10 before the update and never read in the integrator; only other physics-region reads are the look-ahead save/restore. Setters write them alongside v/ω.
- Evidence: /workspace/analyst/INV-01-static-report.md §3
- Confidence: Medium-high
- Related fields: H-002 (velocity part, now REFUTED), F-008
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: Readers outside 0x140720000–0x140760000 (Q-015); [rig+0x2550] hook could read S+0x70. Refuted by an instruction whose load of +0x320/+0x330 reaches S or F/τ in a later tick; runtime test E2.
- Update 2026-09-30: Phase 1a runtime (H-034) independently finds +0x320[k]==v[k-1], +0x330[k]==ω[k-1] in 100% of 2698 ticks. Two independent sources; grade stays PROBABLE.
- Update 2026-09-30: Addendum whole-.text scan (H-053) strengthens 'output-only'; grade stays PROBABLE.
- Update 2026-09-30: Lag-1 relation PROMOTED to F-013 (RE Orchestrator ruling). 'Output only' part stays PROBABLE.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-017 — Tick-start copies +0x170/+0x180 and mid-tick re-injection
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst); pending runtime cross-check by Memory Cartographer (INV-01)
- Grade: Copies +0x2b0→+0x170, +0x2c0→+0x180 and re-injection: CONFIRMED (analyst). Claim that writes between ticks survive / mid-tick writes are lost: HYPOTHESIS (downgraded by RE Orchestrator ruling)
- Address: rig+0x170 (v at tick start), rig+0x180 (ω at tick start)
- Structure: —
- Function: —
- Meaning: 0x14074b8f0 copies +0x2b0→+0x170 (0x14074ba92) and +0x2c0→+0x180 (0x14074ba84) at tick start; 0x14073e0c0 copies them back into S.v/S.ω (0x14073e232/0x14073e24c) before integrating. External writes to v/ω between ticks survive; writes landing between 0x14074ba92 and 0x14073e232 in the same tick are lost.
- Evidence: /workspace/analyst/INV-01-static-report.md §1.3, §2.4, §3
- Confidence: High (static only)
- Related fields: F-008 (+0x180 → +0x2c0 copy)
- Related functions: 0x14074b8f0, 0x14073e0c0, 0x1407308e0, 0x140746700
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: Runtime test E6.
- Update 2026-09-30: Phase 1a runtime (H-034) independently finds +0x170/+0x180 == v/ω[k-1] (100%) and observes the +0x170 copy before the integrate (H-033). Two independent sources for the copies. Physics burst < 1.5 ms per 16.7 ms tick (relevant to the survival HYPOTHESIS).
- Update 2026-09-30: Blanket survival statement replaced by per-point table L0–L8 (H-046). v/ω lost at L3, p/q survive at L3.
- Update 2026-09-30: Copy values +0x170/+0x180 PROMOTED to F-013. Write-survival: see H-046 (L1 stays PROBABLE per RE Orchestrator ruling).
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-018 — Look-ahead rollback 0x14073e0c0 (save → tentative integrate → restore)
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst); pending runtime cross-check by Memory Cartographer (INV-01)
- Grade: CONFIRMED
- Address: —
- Structure: —
- Function: 0x14073e0c0, called once per tick from 0x14073e089 in 0x14073d900 (← 0x14073f300 in 0x14073f220). Save at 0x14073e258..e2df, restore at 0x14073e3c5..e43e.
- Meaning: Runs only when |rig+0x2508| > 0.89408 (2 mph). Saves/restores 15 items: v, ω, p, q, 3 basis rows, prev v/ω (+0x320/+0x330), F, τ, inertia, mass, 1/mass, dword +0x378. Not restored: per-wheel +0x1560/+0x1690 (cone 0x14073a070), per-wheel +0x16c4/+0x16c8/+0x16d8 history, and the v/ω overwrite from +0x170/+0x180 (happens before save).
- Evidence: /workspace/analyst/INV-01-static-report.md §1.4, §4.1
- Confidence: High (static only)
- Related fields: H-R05, H-025, Q-001 (0x140739700 touches 4x4 table rig+0x12dc here)
- Related functions: 0x140748670, 0x14073a070, 0x140739700, 0x140731710
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: Runtime test E9.
- Update 2026-09-30: Phase 1a (H-037): look-ahead seen only with gate on (1750/1750, 0 with gate off). Two independent sources for the gate.
- Update 2026-09-30: Runtime (cartographer rev. 2, P5 PASS strong): gate-on ticks show trial values in +0x2b0..+0x310 and +0x320/+0x330 then bit-exact return (observed in 2 829 ticks); gate-off ticks 0 transients. Grade unchanged (no ruling).
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-019 — Native vehicle state API (rig functions and container thunks)
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst); pending runtime cross-check by Memory Cartographer (INV-01)
- Grade: CONFIRMED (code); names are the analyst's, from behaviour
- Address: —
- Structure: —
- Function: Reset 0x14074a110 (thunk 0x140731990); ResetToCommitted 0x14074a3a0 (0x1407319a0); SetFullState 0x14074a5e0 (0x1407319c0); GetSpawnState 0x14074b070 (0x140731c90), 0x90-byte struct; SetTransform 0x14074ad80 (0x140731c50/0x140731c70); SetLinVel 0x14074a910 (0x140731b20/0x140731b30); SetAngVel 0x14074a890 (0x140731a10/0x140731a20); PlaceWithSpeed 0x14074aa20 (0x140731c60); Commit 0x14074d190 (0x140731cb0); SetPose 0x140746770.
- Meaning: Container→rig thunks do mov rcx,[rcx+8]; jmp. Container-level users: 0x140db7740 (Reset), 0x140db7850 (ResetToCommitted, clears container+0x840), 0x140db95d0, 0x140db9490, 0x140db9410/0x140db95a0, 0x140999990 ('reset in place', flag [car+0x368]) and 0x140dbca20. Full behaviour table in report §4.2.
- Evidence: /workspace/analyst/INV-01-static-report.md §1.5, §4.2
- Confidence: High for code; names Medium
- Related fields: H-023, H-026, H-029
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: Commit after the commit stores (Q-014). Calling these is execution-altering and outside the static task.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-020 — Writers of +0x2d0/+0x2e0 use S-relative addressing
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst); pending runtime cross-check by Memory Cartographer (INV-01)
- Grade: CONFIRMED; 0x140742c67/0x140742c72 REFUTED as rig writers
- Address: —
- Structure: —
- Function: —
- Meaning: Writers: 0x140746626/0x14074663f (integrator), 0x140746781/0x140746791 (SetPose), 0x14073e3e3/0x14073e3e8 (look-ahead restore). Explains why a +0x2d0 displacement scan found none. 0x140742c67/c72 write a different object ([+0x428] → container; bounds/extents).
- Evidence: /workspace/analyst/INV-01-static-report.md §1.6, §2.4
- Confidence: High (static only)
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: ~80 +0x2d0/+0x2e0 stores outside the physics region not traced (Q-016). Other writers of +0x2b0/+0x2c0 with unknown target: Q-020.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-021 — Per-tick call chain and sub-iterations
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst); pending runtime cross-check by Memory Cartographer (INV-01)
- Grade: CONFIRMED by direct calls; top-level caller of thunk unresolved; 'one full-dt integration per tick' PROBABLE; 'sub-iterations write no persistent rig state' UNKNOWN
- Address: —
- Structure: —
- Function: 0x140dbc430 (container) → thunk 0x140731ca0 → 0x14074b8f0 (rig Update) → … 0x1407501a0 → [rig+0x2560]≠0 ? 0x14073f9c0 (kinematic/hold) : 0x14073f220 (solver) → n = max(1, int(dt·1000)) 1 ms sub-iterations of 0x14073e570 on local buffers → 0x140738cc0 → 0x1407395f5 calls 0x140746150(S, dt).
- Meaning: Gravity F.y += mass·(−9.81) at 0x14074bb81..bbca; freeze branch on byte rig+0x2548. Full chain in report §2.5.
- Evidence: /workspace/analyst/INV-01-static-report.md §2.5
- Confidence: Medium-high
- Related fields: F-003 (0x140748670 per-wheel geometry called per tick)
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: Q-013; runtime test E10.
- Update 2026-09-30: Phase 1a: timer delta is 2·dt, not dt (H-R06); Commit runs every tick (C-005); 60 Hz fixed step (H-032).
- Update 2026-09-30: Addendum extends the chain to the full frame (H-042); Commit via EndStep every F (H-044) resolves C-005.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-022 — Hook object at [rig+0x2550]
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst); pending runtime cross-check by Memory Cartographer (INV-01)
- Grade: CONFIRMED (call sites); implementations UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: 0x140738cc0 calls r13=[rig+0x2550] via 0x1407311b0: [vt+0x10] per wheel, [vt+0] before and [vt+8] after the integrate, rdx = S. A hook that edits S cannot be ruled out.
- Evidence: /workspace/analyst/INV-01-static-report.md §2.5
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: Q-011.
- Update 2026-09-30: Addendum: no installer; +0x2550 always null PROBABLE (H-052).
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-023 — Second physics representation at container+0x840 kept in sync by setters
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst); pending runtime cross-check by Memory Cartographer (INV-01)
- Grade: CONFIRMED (forwarding); object identity UNKNOWN; impact on raw savestate HYPOTHESIS
- Address: —
- Structure: —
- Function: —
- Meaning: Owner = [rig+0]. Container vtable 0x141400c30 (base 0x1412af930) slots +0x20 = 0x140db2190 (pose → 0x140e17ae0([this+0x840],m,1)), +0x28 = 0x140db20d0 (v → 0x140e174a0), +0x30 = 0x140db2090 (ω → 0x140e11db0); base slots are ret 0 (0x1406bfe70). A raw memory restore bypassing setters would leave the proxy stale (HYPOTHESIS).
- Evidence: /workspace/analyst/INV-01-static-report.md §4.2 notes
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: What the object is (Q-018); runtime test E12.
- Update 2026-09-30: Phase 1a (H-039): proxy re-synced from rig every tick in steady driving, which pressures the 'setters are the only sync path' part. Entry unchanged.
- Update 2026-09-30: Addendum lists all proxy sync paths (H-050), incl. per-F PostTick push and per-C committed push; 'setters only' no longer holds per static evidence. Entry unchanged.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-024 — container+0xcd0 is a placement target, not derived from +0x2d0
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst); pending runtime cross-check by Memory Cartographer (INV-01)
- Grade: HYPOTHESIS (role)
- Address: container+0xcd0 (x, y, z, yaw)
- Structure: —
- Function: —
- Meaning: Written by 0x140dbe180 (0x140dbe29a/0x140dbe2d1: lerp of two points + atan2 yaw); consumed by 0x140dbca20 (0x140dbca81/0x140dbcaf8), which, if [c+0xcc0] > 0 and cd0.xyz ≠ 0, zeroes v/ω and calls SetTransform. Called from 0x1404b1110.
- Evidence: /workspace/analyst/INV-01-static-report.md §4.2, §6
- Confidence: Low-medium
- Related fields: H-011 (competing reading), C-004
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: Runtime test E3; runtime brief Q2.
- Update 2026-09-30: Phase 1a (H-040) supports it: con+0xcd0 constant over 45 s of driving.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-025 — Per-wheel +0x1690 puzzle in wheels.md comes from the look-ahead cone
- Status: PROBABLE-REFUTED 2026-09-30 (formal REFUTED needs ≥3 phase-verified trials, I1/I5) — RUNTIME CORRELATION (Validation Specialist, INV-01 Phase 1a, read-only polling 2026-09-30 12:07:28–12:08:13 BRT, PID 4113076): one car, one location, one session; OPERATOR CONFIRMATION OF NO F5–F7 PENDING
- Grade: HYPOTHESIS
- Address: —
- Structure: —
- Function: —
- Meaning: 0x14073a070 runs on the predicted pose, is not rolled back, and stops below 2 mph, so +0x1690 freezes at rest.
- Evidence: /workspace/analyst/INV-01-static-report.md §4.1
- Confidence: Low
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: Refuted if +0x1690 updates at rest; runtime test E9.
- Update 2026-09-30: Phase 1a: +0x1690 changed in 125–134 gate-off moving ticks per wheel, written before the integrate (H-041). Refutes 'changes only when the gate is on'; the 'freezes at rest' explanation is weakened.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-026 — What the native Reset 0x14074a110 touches
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst); pending runtime cross-check by Memory Cartographer (INV-01)
- Grade: CONFIRMED (list)
- Address: —
- Structure: —
- Function: —
- Meaning: Zeroes v, prev v, +0x170, +0x200, ω, prev ω, +0x180, +0x210; resets timers, ring buffers +0x8c..+0xdb and index +0xdc, +0x2500/+0x2508, per-wheel state via 0x140730650, and more (report §5.1). Does NOT touch pose (+0x2d0/+0x2e0) or F/τ.
- Evidence: /workspace/analyst/INV-01-static-report.md §5.1
- Confidence: High (static only)
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-027 — State a raw savestate must capture or reset beyond S
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst); pending runtime cross-check by Memory Cartographer (INV-01)
- Grade: Per row as written in report §5.2 (mix of CONFIRMED, PROBABLE, HYPOTHESIS, UNKNOWN)
- Address: —
- Structure: —
- Function: —
- Meaning: Basis rows (read before next integrate), +0x170/+0x180, committed pose/vel +0x190..+0x1a8 and +0x200/+0x210, encoded tick-start pose +0x100..+0x118 and history +0x11c..+0x16f, F/τ (PROBABLE zero between ticks), +0x378, wheel spin wheel+0x104, suspension wheel+0x84/+0x88/+0x8c (difference use HYPOTHESIS), wheel +0x80/+0x6c/+0x70/+0x78, per-wheel history +0x16c4..+0x16ec/+0x15e0, per-wheel geometry, solver sub-iterations (PROBABLE no persistent state), flags rig+0x12c8/+0x1320, ring buffers/timers/+0x2508, control flags +0x2548/+0x2550/+0x2558/+0x2560, container proxy +0x840.
- Evidence: /workspace/analyst/INV-01-static-report.md §5.2 (authoritative table; not duplicated here)
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: Runtime tests E7, E11.
- Update 2026-09-30: Per RE Orchestrator ruling the restore recipe stays PROBABLE. Cartographer prediction (interpretation, weak until W-series): writing only +0x320/+0x330 or +0x170/+0x180 will not stick; fields that feed the next tick are +0x2b0/+0x2c0/+0x2d0/+0x2e0 with axes +0x2f0..+0x310; pxb previous transform and render interpolation blend from the pre-restore pose for one tick.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-028 — Rigid-body parameters in the state block
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst); pending runtime cross-check by Memory Cartographer (INV-01)
- Grade: +0x370 mass / +0x374 1/mass CONFIRMED; +0x360..+0x368 diagonal inertia PROBABLE; +0x340 F and +0x350 τ accumulators CONFIRMED; +0x378 UNKNOWN
- Address: —
- Structure: —
- Function: —
- Meaning: Setter 0x140746710 ([S+0xc0]=m, [S+0xc4]=1/m), called from 0x140749ce5. Live value 1083.53 in the docs matches a car mass. AddForceAtPoint 0x140745db0.
- Evidence: /workspace/analyst/INV-01-static-report.md §2.2, §6
- Confidence: —
- Related fields: Q-004 (+0x370), Q-017 (+0x378)
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: Setup writer of inertia not located; runtime test E8.
- Update 2026-09-30: Phase 1a (H-036) independently: +0x370 = 1083.53, +0x374 reciprocal exactly, constant. Two independent sources.
- Update 2026-09-30: Addendum: mass/1/mass set only on setup reload (H-055); inertia +0x360 rewritten every PreTick (H-043).
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-029 — Committed state and encoded tick-start pose
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst); pending runtime cross-check by Memory Cartographer (INV-01)
- Grade: CONFIRMED (writes/reads); purpose of +0x100..+0x118 UNKNOWN
- Address: +0x190..+0x1a8 committed pose (7 dwords, obfuscated by odd-constant multiply; modular inverses checked), +0x200/+0x210 committed v/ω, +0x100..+0x118 encoded tick-start pose
- Structure: —
- Function: —
- Meaning: Written by Commit 0x14074d190 and SetTransform(flag); read by ResetToCommitted, GetSpawnState, 0x14073d900. Possibly anti-tamper or teleport-distance check (unverified).
- Evidence: /workspace/analyst/INV-01-static-report.md §4.2, §5.2, §6
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: Q-019.
- Update 2026-09-30: Phase 1a (H-034): +0x200/+0x210 == v/ω[k] every tick (100%). Two independent sources for committed v/ω.
- Update 2026-09-30: Addendum: Commit fully read (H-045); 0x14075bd70 anti-tamper idea REFUTED (H-R09).
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-030 — String xrefs for reset/teleport
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst); pending runtime cross-check by Memory Cartographer (INV-01)
- Grade: Mixed: enum table CONFIRMED; handler UNKNOWN; candidate endpoints HYPOTHESIS; debug teleport consumer compiled out PROBABLE
- Address: —
- Structure: —
- Function: —
- Meaning: 'reset_vehicle' @0x141251618 and 'net_reset_vehicle' @0x141251628 used only in enum-to-name switch 0x140281a30. 'driving.reset_vehicle' @0x141271318 hashed into 0x141f58c18, consumed at 0x1402a7b0e (input-action check); candidates 0x1402a04cb, 0x1402a593c (→ 0x140db95d0), 0x1402a5c2d (→ 0x140db7740). 'debug.global.teleport.set/.pressed' hashed into 0x141f590a0/0x141f59080 with no other .text reference. Other registration strings not traced.
- Evidence: /workspace/analyst/INV-01-static-report.md §4.3
- Confidence: —
- Related fields: H-008
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: Q-010.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-031 — Container class construction and rig ownership
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst); pending runtime cross-check by Memory Cartographer (INV-01)
- Grade: CONFIRMED consistent
- Address: —
- Structure: —
- Function: —
- Meaning: Container vtable 0x141400c30 (= doc value exe+0x1400c30), constructor 0x140da13d0 → base constructor 0x140730ef0, which builds the rig via 0x1407469f0 and stores it at +8. Rig owner is [rig+0] (0x140746a3d).
- Evidence: /workspace/analyst/INV-01-static-report.md §4.2 notes, §7
- Confidence: —
- Related fields: F-001 (container+0x08 → rig)
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

## INV-01 Phase 1a runtime findings (Validation Specialist; independent of the static report)

Grades as written by the Validation Specialist. One car, one location, one session; operator confirmation of no F5–F7 (Practice Mode mod) during the capture is PENDING.

### H-032 — Physics tick cadence: fixed 60 Hz step
- Status: PROMOTED 2026-09-30 to F-014 (RE Orchestrator ruling)
- Grade: PROBABLE
- Address: —
- Structure: —
- Function: —
- Meaning: dt = 1/60 s (median dp/v_new 0.0166665; v_old fits worse, consistent with semi-implicit Euler). 60.00 ticks/s (2700 in 45.0 s), wall intervals jitter 11–24 ms: fixed-step accumulator driven from frames. Ring index rig+0xdc advances exactly once per tick 0→9→0 (2700/2700). Const .data 0x1415a9ce4 = 1000.0 in 45/45 reads. 0 ticks in 18 s while paused (gate run 12:04:46–12:05:04).
- Evidence: /workspace/validation/INV-01-phase1a.md §Tick segmentation, §R1
- Confidence: Medium-high (two independent measures: ring cadence and kinematics)
- Related fields: H-021 (sub-iterations; n=16 inferred only), H-R06
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: Frame-rate dependence (frame-cap arm not run, Q-024); sub-iteration count n not observed (Q-023); what +0x1334 counts (Q-022).
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### H-033 — Observed intra-tick order and timing
- Status: ACTIVE — RUNTIME CORRELATION (Validation Specialist, INV-01 Phase 1a, read-only polling 2026-09-30 12:07:28–12:08:13 BRT, PID 4113076): one car, one location, one session; OPERATOR CONFIRMATION OF NO F5–F7 PENDING
- Grade: PROBABLE (polling)
- Address: —
- Structure: —
- Function: —
- Meaning: In 2698/2698 ticks: 1) timer +0x1338 += 2·dt; 2) +0x170 ← v; 3) only when gated, look-ahead: v and p change, +0x320 ← v, all restored, +0x1334 rises ~3; 4) real integrate: ring index +1, v/p new, +0x320 ← v_old; 5) Commit: +0x200 ← v_new; 6) proxy p updated. Median tick start→integrate 0.35 ms, integrate→Commit 0.23 ms, integrate→proxy p 1.3 ms. Physics burst < ~1.5 ms of each 16.7 ms tick (~90% of wall time is between ticks).
- Evidence: /workspace/validation/INV-01-phase1a.md §Tick segmentation
- Confidence: Medium
- Related fields: H-017, H-018, H-021, H-029, H-039, C-005
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: Polling cannot see sub-iterations.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### H-034 — Settled-state lag relations (+0x320/+0x330, +0x170/+0x180, +0x200/+0x210)
- Status: ACTIVE — RUNTIME CORRELATION (Validation Specialist, INV-01 Phase 1a, read-only polling 2026-09-30 12:07:28–12:08:13 BRT, PID 4113076): one car, one location, one session; OPERATOR CONFIRMATION OF NO F5–F7 PENDING
- Grade: PROBABLE (correlation only)
- Address: —
- Structure: —
- Function: —
- Meaning: Bit-exact over 2698 settled ticks: +0x320[k]==v[k-1] and +0x330[k]==ω[k-1] (100%, lag 0: 0%); +0x170[k]==v[k-1], +0x180[k]==ω[k-1] (100%); +0x200[k]==v[k], +0x210[k]==ω[k] (100%, lag 1: 0%). Controls: +0x320 vs p 0%; shuffled v 0.037%. Commit copy runs every tick after the integrate.
- Evidence: /workspace/validation/INV-01-phase1a.md §R2; lag_equality.csv
- Confidence: Medium-high
- Related fields: TWO INDEPENDENT SOURCES now agree with H-016 (prev v/ω), H-017 (tick-start copies) and H-029 (committed v/ω): RE Analyst static + Validation Specialist runtime. Grade kept PROBABLE as graded. C-005 (Commit per tick).
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: Copy direction and actual consumption not established (W1/W5).
- Update 2026-09-30: +0x320/+0x330 and +0x170/+0x180 relations PROMOTED to F-013. +0x200/+0x210 lag-0 copy (post-commit, +191 µs) also bit-exact in cartographer runtime (4 960/4 960) — two runtime sources; not promoted (no ruling).
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### H-035 — F/τ accumulators are zero between ticks
- Status: ACTIVE — RUNTIME CORRELATION (Validation Specialist, INV-01 Phase 1a, read-only polling 2026-09-30 12:07:28–12:08:13 BRT, PID 4113076): one car, one location, one session; OPERATOR CONFIRMATION OF NO F5–F7 PENDING
- Grade: PROBABLE (CONFIRMED only at B2/B1 in I1)
- Address: rig+0x340..+0x35c
- Structure: —
- Function: —
- Meaning: 0 non-zero values in 2699/2699 settled samples; transients seen in 29,835 (F) / 19,813 (τ) records, e.g. F.y = −10623.9 = m·g.
- Evidence: /workspace/validation/INV-01-phase1a.md §R3
- Confidence: —
- Related fields: H-027 (F/τ row), H-028
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: A force added between B1 and the Commit loop would still look settled. No curb-strike or airborne arm.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### H-036 — Mass +0x370 and inverse +0x374 reciprocal and constant
- Status: ACTIVE — RUNTIME CORRELATION (Validation Specialist, INV-01 Phase 1a, read-only polling 2026-09-30 12:07:28–12:08:13 BRT, PID 4113076): one car, one location, one session; OPERATOR CONFIRMATION OF NO F5–F7 PENDING
- Grade: CONFIRMED (reciprocal and constancy) for this vehicle and session; 'mass in kg' PROBABLE
- Address: —
- Structure: —
- Function: —
- Meaning: +0x370 = 1083.53, +0x374 = 9.2291e-4, constant across 52,101 records; float32 1/m == inv exactly.
- Evidence: /workspace/validation/INV-01-phase1a.md §R4
- Confidence: —
- Related fields: H-028 (now two independent sources), Q-004
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: Second-vehicle control and damage event not done. As a single-session runtime result it is not promoted to confirmed-facts.md without a Chief ruling.
- Update 2026-09-30: Reciprocal/constancy reproduced by Memory Cartographer (product 1.0000000012, 0 changes). Per RE Orchestrator ruling 'mass in kg' stays PROBABLE; entry stays in hypotheses.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### H-037 — rig+0x2508 is signed ±|v|; car+0x310 is unsigned |v|; look-ahead only with gate on
- Status: ACTIVE — RUNTIME CORRELATION (Validation Specialist, INV-01 Phase 1a, read-only polling 2026-09-30 12:07:28–12:08:13 BRT, PID 4113076): one car, one location, one session; OPERATOR CONFIRMATION OF NO F5–F7 PENDING
- Grade: PROBABLE
- Address: —
- Structure: —
- Function: —
- Meaning: |+0x2508| == f32|v| in 100%, negative in 85/2699 settled ticks (slow reverse/creep). car+0x310 == f32|v| in 100%. Look-ahead signature in 1750 ticks, all with |+0x2508| > 0.89408 on the previous settled tick; 0 look-aheads with gate off; 25 gate-on ticks without a visible look-ahead (near threshold or missed).
- Evidence: /workspace/validation/INV-01-phase1a.md §R5(a)
- Confidence: —
- Related fields: H-018 (gate corroborated, two sources), H-R07
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: —
- Update 2026-09-30: Second runtime source (cartographer rev. 2): |+0x2508| == ‖v‖ 4 960/4 960, sign = sign(v·F) with F = +0x310 4 960/4 960; car+0x310 unsigned; gate-off ticks 0 transients. Near standstill (tick #2212) +0x2508 ≠ sign·‖v‖. Meaning of sign UNKNOWN. Grade unchanged (no ruling).
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### H-038 — Secondary copies of car state
- Status: ACTIVE — RUNTIME CORRELATION (Validation Specialist, INV-01 Phase 1a, read-only polling 2026-09-30 12:07:28–12:08:13 BRT, PID 4113076): one car, one location, one session; OPERATOR CONFIRMATION OF NO F5–F7 PENDING
- Grade: Per item: car+0x2b0/+0x2c0 PROBABLE; car+0x2e0 PROBABLE; car+0x2f0/+0x300 render transform HYPOTHESIS; rig+0x290/+0x2a0 filtered velocity HYPOTHESIS; car+0x310 PROBABLE
- Address: —
- Structure: —
- Function: —
- Meaning: car+0x2b0/+0x2c0 == v[k]/ω[k] bit-exact, lag 0. car+0x2e0 = p + R·c with body-frame c = (−0.00001, −0.3364, −0.3731) m, lag 0 (sd at float32 resolution). car+0x2f0 unit quaternion never equal to rig q (median 0.046°, max 0.54°); car+0x300 = (1,1,1,0), looks like scale → car+0x2e0/+0x2f0/+0x300 look like a render transform. rig+0x290 fits an EMA r += a·(v−r), a = 0.835 (approximate, rms 2.8e-3); +0x2a0 not a copy of ω.
- Evidence: /workspace/validation/INV-01-phase1a.md §R6
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: Readers of car copies; slerp fit for car+0x2f0 and characterisation of +0x2a0 not done.
- Update 2026-09-30: Addendum proposes rig+0x290/+0x2a0 = Commit lerp (H-045); competing with the EMA reading. Logged as C-006.
- Update 2026-09-30: rig+0x290/+0x2a0 EMA reading DISFAVOURED per RE Orchestrator ruling (runtime favours Commit/render lerp, H-045); not refuted; C-006 open until E-B10. car+0x2b0/+0x2c0 lag 0 and car+0x310 unsigned ‖v‖ reproduced bit-exact by cartographer (second runtime source); car+0x2e0 = p + R·(−0.00001, −0.33639, −0.37307) body offset (strong); car+0x2f0 ≈ q (weak). Grades unchanged (no ruling).
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### H-039 — Container proxy re-synced from rig every tick
- Status: ACTIVE — RUNTIME CORRELATION (Validation Specialist, INV-01 Phase 1a, read-only polling 2026-09-30 12:07:28–12:08:13 BRT, PID 4113076): one car, one location, one session; OPERATOR CONFIRMATION OF NO F5–F7 PENDING
- Grade: PROBABLE (correlation); early I4 result
- Address: [con+0x840] = 0x6b6f09c0; pxb = [[con+0x840]+8] = 0x6b6f04e0 (this session)
- Structure: —
- Function: —
- Meaning: pxb+0x1b0/+0x1f0 == rig p lag 0; pxb+0x230/+0x270 == p lag 1; pxb+0x2e0/+0x2f0 == v lag 0; pxb+0x2c0/+0x2d0 == ω lag 0 (2698 settled ticks). Proxy p updates ~1.3 ms after integrate, after Commit (2407 resolvable ticks). Supports I4 case (a) 'self-heals'.
- Evidence: /workspace/validation/INV-01-phase1a.md §Proxy
- Confidence: —
- Related fields: H-023 (puts pressure on 'setters are the only sync path'), Q-018
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: Whether the per-tick sync is itself a setter/forwarding call inside Commit (I3); whether a raw restore is re-synced (W11). Q-026.
- Update 2026-09-30: Static addendum (H-050) independently finds per-F PostTick and per-C committed pushes: two independent sources for per-tick proxy sync.
- Update 2026-09-30: Cartographer rev. 2 (third source): pxb v/ω lag 0 bit-exact, pos lag 0 (pxb+0x1b0) and lag 1 (pxb+0x230); see H-057.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### H-040 — container+0xcd0 does not track the car (placement inactive)
- Status: ACTIVE — RUNTIME CORRELATION (Validation Specialist, INV-01 Phase 1a, read-only polling 2026-09-30 12:07:28–12:08:13 BRT, PID 4113076): one car, one location, one session; OPERATOR CONFIRMATION OF NO F5–F7 PENDING
- Grade: PROBABLE
- Address: —
- Structure: —
- Function: —
- Meaning: Constant for 45 s at (−4578.00, 124.47, 39.78) while p changed in 2432 ticks, 313–499 m from the car; con+0xcc0 = 0 throughout.
- Evidence: /workspace/validation/INV-01-phase1a.md §R6
- Confidence: —
- Related fields: Supports H-024; refutes H-011 in this state; C-004
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: Whether anything reads it (W9, Q-027).
- Update 2026-09-30: Second runtime source: Memory Cartographer rev. 2 (P6 FAIL). H-011 now REFUTED in normal driving.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### H-041 — Second writer of per-wheel +0x1560/+0x1690 in gate-off ticks
- Status: ACTIVE — RUNTIME CORRELATION (Validation Specialist, INV-01 Phase 1a, read-only polling 2026-09-30 12:07:28–12:08:13 BRT, PID 4113076): one car, one location, one session; OPERATOR CONFIRMATION OF NO F5–F7 PENDING
- Grade: PROBABLE (observational)
- Address: —
- Structure: —
- Function: —
- Meaning: In 682 gate-off ticks with p changing (median |v| 0.16 m/s), +0x1690 changed in 125/134/128/126 (wheels 0–3); +0x1560 similar. Write lands ~1 record after tick start, before the integrate. Control: wheel+0x50 rear wheels change only with p (86/86); front wheels change in ~760 gate-off ticks incl. ~100 without p change, consistent with steering at rest.
- Evidence: /workspace/validation/INV-01-phase1a.md §R5(b)
- Confidence: —
- Related fields: H-025 (PROBABLE-refuted), H-027 per-wheel geometry row
- Related functions: Candidates 0x140748670 / 0x1407306b0 (unverified)
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: Which writer (Q-025); I1/I5 should count 0x14073a070 calls in gate-off ticks.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

## INV-01 static addendum findings (single-source static, pending runtime cross-check)

Grades as written by the RE Analyst in the addendum.

### H-042 — Per-frame physics call chain (F = PhysicsStep, C = per-container sync)
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: Edges CONFIRMED (direct calls, thunks, vtable contents); order of F vs C within a frame UNKNOWN (H-ORD: HYPOTHESIS)
- Address: —
- Structure: —
- Function: Task 0x1404b1040 ('PhysicsStepTask', vt slot .rdata 0x141276fd0) → F = 0x140dbc500(dt); task 0x1404b1100 ('PostPhysicsTask', 0x141277028) → C = 0x140dbca20. F: F.0 dt==0 → return; F.1/F.2 container prep (0x140dbe300); F.3 job batch mode 0 per container (job body 0x140da0d50: a. PreTick 0x140749a30, b. ContainerTick 0x140dbc430 → gate → 0x14074b8f0 rig Update, c. PostTick 0x140db2220 → 0x140749980); F.4/F.5 job batches mode 1/2; F.6 0x140dadc00; F.7 EndStep 0x1407511e0 via thunk 0x140731d50 at 0x140dbc988 → Commit; F.8 con+0xc930 = Δ/dt. C: placement branch (SetLinVel/SetAngVel 0 + SetTransform) if G-place, else 0x14074f600 pushes committed pose/v/ω to proxy.
- Meaning: —
- Evidence: /workspace/analyst/INV-01-static-addendum.md §1.1; task names are the analyst's
- Confidence: —
- Related fields: H-021 (extends), H-044, C-005
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: F vs C order and F calls per frame (Q-028); which container is the player's (Q-034). Test E-B1.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-043 — PreTick 0x140749a30: conditional pull, unconditional inertia rewrite
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: CONFIRMED (control flow)
- Address: —
- Structure: —
- Function: —
- Meaning: Runs every F per container before rig Update. Under G-pull (con+0x14 < 0.1, via 0x140db6bb0) overwrites S.p/q (SetPose 0x140749b70), S.v (0x140749b7c), S.ω (0x140749b88) from the proxy and sets pull deltas +0x3c0/+0x3d0 (zeroed every F). Unconditionally rewrites inertia +0x360 from encoded +0x590..+0x598 (0x140749bdf), so an external +0x360 write is lost at the next PreTick; pushes mass/inertia to proxy (owner vt+0x18). Calls adjuster vt+8(adj,0,owner) if rig+0x3b8.
- Evidence: /workspace/analyst/INV-01-static-addendum.md §1.2, §3.1, §5
- Confidence: —
- Related fields: H-028, H-049, H-051, Q-020 (answers 0x140749b7c/b88)
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: Test E-B6 (inertia overwrite).
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-044 — Commit runs every tick via EndStep (thunk 0x140731d50)
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: CONFIRMED (code)
- Address: —
- Structure: —
- Function: 0x140dbc988 → thunk 0x140731d50 → EndStep 0x1407511e0 → Commit 0x14074d190(rig, dl=1) at 0x140751244, unconditional; if rig+0x3b8, adjuster vt+0x10 may SetPose first (0x14075123a).
- Meaning: Resolves C-005 (static chain now includes a per-tick Commit, matching Phase 1a H-033/H-034). EndStep is outside the ContainerTick gate.
- Evidence: /workspace/analyst/INV-01-static-addendum.md §0.3, §1.1
- Confidence: —
- Related fields: C-005 (resolved), H-R08, H-034, H-029
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-045 — Commit body and render-interpolated block +0x220..+0x2af
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: Structure CONFIRMED (static); 'render interpolation' PROBABLE — favoured by runtime (C-006); 0x14075bd70 as anti-tamper REFUTED (H-R09)
- Address: —
- Structure: —
- Function: —
- Meaning: Commit: +0x190..+0x1a8 = encode(S.p, S.q); +0x200 = S.v; +0x210 = S.ω; alpha = [rig+0x1198]->vt+0x10(); if dl==0 or alpha==1.0 copy +0x190..+0x21f → +0x220..+0x2af, else +0x220 = enc(lerp(prev +0x100, cur +0x190, alpha)) via 0x14075bd70 → 0x14074a7d0, likewise +0x23c, +0x258..+0x270, +0x274 = 0x14073bc20(...), +0x290 = lerp(+0x170, +0x200, alpha), +0x2a0 = lerp(+0x180, +0x210, alpha). Getter 0x1407311a0 returns rig+0x220. 0x14075bd70 = pos lerp + quaternion interp 0x14075bba0, no other side effects.
- Evidence: /workspace/analyst/INV-01-static-addendum.md §1.2, §5
- Confidence: —
- Related fields: H-029, Q-014 (answered), Q-019, H-038 (rig+0x290/+0x2a0: competing EMA hypothesis, C-006)
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: Test E-B10.
- Update 2026-09-30: Runtime (cartographer rev. 2): rig+0x290/+0x2a0 = lerp(+0x320, +0x2b0, β) at the render rate, β≈α of pxb interpolation (median |β−α| 2.9e-3), corr(α, time since commit) 0.77 / 0.82. RE Orchestrator: favours the lerp reading (PROBABLE); C-006 stays OPEN until E-B10.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-046 — External-write landing points L0–L8
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: Per row as written in addendum §1.3 (L0, L2–L8 CONFIRMED control flow; L1 survival PROBABLE conditional on G-pull=0, G-adj=0, G-place=0 and no other thread writing S)
- Address: —
- Structure: —
- Function: —
- Meaning: Replaces the blanket H-017 survival statement. Key rows: L1 (after C, before next F) survives unless G-pull/G-adj; L3 (after 0x14074ba92, before 0x14073e232) v/ω LOST, p/q SURVIVE; L4 (inside look-ahead save/restore) all 15 fields lost; L5 integrated; L8 committed/proxy stale until next Commit/C. Writing S alone at L1 leaves committed block, interpolated block and proxy stale until the next Commit/C (self-heal PROBABLE).
- Evidence: /workspace/analyst/INV-01-static-addendum.md §1.3 (authoritative table; not duplicated)
- Confidence: —
- Related fields: H-017, H-050, H-049, H-051, H-040
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: Tests E-B3, E-B4, E-B5; gate values must be re-checked per trial.
- Update 2026-09-30: Per RE Orchestrator ruling, L1 survival stays PROBABLE.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-047 — Threading of the physics step
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: CONFIRMED structurally
- Address: —
- Structure: —
- Function: —
- Meaning: Containers 1..n run F.3 jobs on worker threads (dispatch 0x14084b690 at 0x140dbc785); container 0 runs inline on F's thread (0x140dbc7f5). A write from the Present thread can land at any of L1–L8 unless synchronised to F boundaries.
- Evidence: /workspace/analyst/INV-01-static-addendum.md §1.1, §1.4
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: Which thread runs F relative to Present (Q-028); which container is the player's (Q-034).
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-048 — ContainerTick gate on proxy translation y (H-GATE-FLOOR)
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: PROBABLE structure, HYPOTHESIS semantics (RE Orchestrator ruling 2026-09-30: conservative grade until tested; supersedes the addendum's §2.1 wording)
- Address: —
- Structure: —
- Function: 0x140dbc430: proxy world-matrix translation y (0x140df7b50, [[con+0x840]+0x10]+0x1f0) compared with G.y − 50.0 (G = vec4 at 0x1420202d0, only if [0x14201b788] ≠ 0); jbe skips con timers, rig Update and 0x140dbc100.
- Meaning: Reading: 'fell through the world' guard. If the flag is 0, G comes from an uninitialised stack slot (HYPOTHESIS: flag always set in game). Deadlock-recovery after a restore from below the floor via unconditional Commit: HYPOTHESIS.
- Evidence: /workspace/analyst/INV-01-static-addendum.md §2.1, §2.2, §6
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: Runtime value of G and [0x14201b788] (Q-029). Test E-B8.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-049 — Pull timer con+0x14 and pull-off in normal driving (H-PULL-OFF)
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: Writers/reader CONFIRMED; 'pull off in normal driving' PROBABLE; what sets it < 0.1 UNKNOWN
- Address: con+0x14
- Structure: —
- Function: —
- Meaning: += dt every tick (0x140dbccdf); set to FLT_MAX by 0x140db77c0 (Container::Reset), 0x140db7963, 0x140db961f, 0x140db9777; read by pull gate 0x140db6bd2 (< 0.1). No small-value writer found in 0x140da0000–0x140dc0000. Cartographer's frozen dump shows FLT_MAX (corroboration). Every reset path sets FLT_MAX.
- Evidence: /workspace/analyst/INV-01-static-addendum.md §2.2, §6
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: Q-030. Refuted if any tick shows con+0x14 < 0.1 (E-B2).
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-050 — Complete list of proxy sync paths; self-heal after a raw S write (H-SELFHEAL)
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: Edges CONFIRMED; 'only these' PROBABLE; H-SELFHEAL PROBABLE
- Address: —
- Structure: —
- Function: —
- Meaning: rig→proxy live v/ω at PostTick 0x140749980 every F; rig→proxy committed pose/v/ω at C 0x14074f600 when not G-place; rig→proxy mass/inertia at PreTick; setters; proxy→rig pull under G-pull; proxy→gate translation y every F. A raw savestate leaves the proxy stale for at most one F + one C (PROBABLE), unless G-pull or G-place.
- Evidence: /workspace/analyst/INV-01-static-addendum.md §2.3, §6
- Confidence: —
- Related fields: H-023 (revises 'setters only'), H-039 (runtime per-tick resync; now two independent sources for per-tick proxy sync), Q-026
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: Effect of one stale-proxy frame on contacts (Q-033). Test E-B3.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-051 — Adjuster pointer rig+0x3b8
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: Edges CONFIRMED; class UNKNOWN; installed by a special game mode HYPOTHESIS
- Address: —
- Structure: —
- Function: —
- Meaning: Written by constructor (0x140746cd7) and one setter thunk 0x140731a30, whose only caller 0x1402736c5 (in 0x140273560) is gated by [0x141f597a0]; object is r8+0x338 from game-mode table [0x141695150]->[+8][idx 0x141693ffc]. Called at PreTick vt+8(adj,0,owner), PostTick vt+8(adj,2,owner), EndStep vt+0x10(adj,owner,&pose) → SetPose if true. Same global gates a branch in the reset_vehicle input function.
- Evidence: /workspace/analyst/INV-01-static-addendum.md §3.5, §5
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: Q-031.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-052 — rig+0x2550 always null; freeze +0x2548 and kinematic +0x2560 dead in this build
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: PROBABLE
- Address: —
- Structure: —
- Function: —
- Meaning: Whole-.text scan: +0x2550 written only by the constructor (= 0); other hit 0x14046526d is a dword store in an unrelated class (PROBABLE). No hook installer, vtable or implementations exist. +0x2548/+0x2560 only zeroed by the constructor. Risk from H-022 downgraded to low; G2 should still poll.
- Evidence: /workspace/analyst/INV-01-static-addendum.md §3.4
- Confidence: —
- Related fields: H-022, Q-011 (proposed answer); Phase 1a G2 saw +0x2550=0, +0x2548=0, +0x2560=0 (one session)
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: memcpy or computed-address writes cannot be excluded statically. Refuted by any non-null runtime sample.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-053 — Rig encapsulation: outside code reaches the rig only through container thunks
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: CONFIRMED for the thunk range 0x140731000–0x140731dff
- Address: —
- Structure: —
- Function: —
- Meaning: Getters expose +0x2b0 (0x140731564), +0x2d0/+0x2e0 (0x1407318d0), +0x2f0 (0x140731544), +0x300 (0x1407317a4), +0x310 (0x1407313b4), +0x220 (0x1407311a0), +0x2550 (0x1407311b0). No getter exposes +0x320/+0x330. Whole-.text scan: 731 [reg+0x320/0x330] hits; only rig-based loads are the look-ahead save 0x14073e29c/2a4; S methods only store S+0x70/+0x80.
- Evidence: /workspace/analyst/INV-01-static-addendum.md §3.6
- Confidence: —
- Related fields: H-016 ('output-only' stays PROBABLE, strengthened), Q-015
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: 20 non-physics float loads and 88 non-physics X+0x2b0 sites not individually traced. Refuted by runtime W5 / E-B7.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-054 — reset_vehicle input path posts messages; H-RV
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: Input → message post CONFIRMED; final consumer UNKNOWN; H-RV (offline reset ends in 0x140999990 or Container::Reset 0x140db7740) HYPOTHESIS
- Address: —
- Structure: —
- Function: 0x1402a7990: action check 0x140d97470 on hash 0x141f58c18; posts event type 0x32 (vtable 0x141271230) and 0x53 (vtable 0x141271190) via 0x140515800 to manager [0x141695150]->[+8][idx 0x1416940a0]. Nearby strings 'ResetLinesManager', 'lng_reset_press_reset'.
- Meaning: Full caller lists of Reset, ResetToCommitted, SetFullState, Container::Reset, 0x140db95d0 and 0x140999990 in addendum §4.2. All reset paths set con+0x14 = FLT_MAX (push rig→proxy via setters, no pull).
- Evidence: /workspace/analyst/INV-01-static-addendum.md §4
- Confidence: —
- Related fields: H-030, Q-010, H-049
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: Message consumer. Test E-B11.
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-055 — Setup reload 0x140749c80 / 0x140750f20
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: Control flow CONFIRMED; 'does not run in normal driving' HYPOTHESIS
- Address: —
- Structure: —
- Function: —
- Meaning: Runs when rig+0x4c0 ≠ rig+0x10d0 and ≠ [[rig+0x1120]+0xe8]. 0x140750f20 writes per-wheel mount position wheel+0x0 (0x140750ff9, from setup +0x5a0/+0x5b0, x mirrored), wheel+0x6c..+0x78 = {1,0,1,0}, rig+0x2514/+0x2518, then 0x14074ee50. 0x140749c80 re-sets mass (0x140746710 from encoded +0x584) and inertia, copies +0x8e0.. → +0x4a8. Not an S writer (H-R10).
- Evidence: /workspace/analyst/INV-01-static-addendum.md §3.2
- Confidence: —
- Related fields: F-006 (+0x5a0/+0x5b0, +0x2514/+0x2518), H-010 (+0x8e8 region), Q-020
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-056 — 0x140745b60 is a pure quaternion rotation
- Status: ACTIVE — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: CONFIRMED
- Address: —
- Structure: —
- Function: 0x140745b60–0x140745ce7: out = q ⊗ v ⊗ q* (rotate *r8 by *rcx, write 3 floats to *rdx); no persistent side effects.
- Meaning: —
- Evidence: /workspace/analyst/INV-01-static-addendum.md §3.3
- Confidence: —
- Related fields: H-014, Q-012 (answered)
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

## INV-01 runtime report rev. 2 findings (Memory Cartographer)

### H-057 — Proxy body pxb = [[con+0x840]+0x8] field map
- Status: ACTIVE — RUNTIME CORRELATION (Memory Cartographer rev. 2, two captures). Scope: one car, one location, one session (pid 4113076, 2026-09-30); other cars, stages, replay not tested
- Grade: Per row as written in report §(c): v/ω and pos copies 'confirmed' (bit-exact), axes and interpolated transform 'strong'
- Address: pxb (= 0x6b6f04e0 in this session)
- Structure: —
- Function: —
- Meaning: pxb+0x2c0/+0x2e0 = ω,v lag 0 (commit record); pxb+0x2d0/+0x2f0 = ω,v lag 0 (+511 µs); pxb+0x1b0 (= +0x1f0) = pos lag 0; pxb+0x230 (= +0x270) = pos lag 1; pxb+0x180..0x1a0 = current axes recomputed (1–2 ULP); pxb+0x200..0x220 = previous-tick axes (1–2 ULP); pxb+0x280..0x2b0 = transform interpolated at render rate, pos = lerp(pxb+0x230, rig+0x2d0, α), α 0.38–0.995.
- Evidence: /workspace/captures/INV-01/REPORT.md (rev. 2) §(c), proxy section
- Confidence: —
- Related fields: H-039, H-050, H-023, Q-018
- Related functions: —
- How validated: —
- What remains unknown: pxb object's name/owner and its other bodies (Q-018).
- Recorded: 2026-09-30 by Knowledge Keeper from Memory Cartographer INV-01 report rev. 2   Last changed: 2026-09-30

### H-058 — Render-rate interpolation updates (~92–101 Hz)
- Status: ACTIVE — RUNTIME CORRELATION (Memory Cartographer rev. 2). Scope: one car, one location, one session (pid 4113076, 2026-09-30); other cars, stages, replay not tested
- Grade: strong (cartographer); interpretation 'render-side interpolation' strong
- Address: —
- Structure: —
- Function: —
- Meaning: Only pxb+0x280..0x2b0 changes at the other rate (92.2/s cap, 101.1/s cap2), followed ~100–200 µs later by rig+0x290/+0x2a0. 0, 1 or 2 commits between updates. The frozen '6.02 ms' offset is not constant (τ median −3.31 ms, sd 2.07 ms).
- Evidence: /workspace/captures/INV-01/REPORT.md (rev. 2) §(d) Q3, 'other rate'
- Confidence: —
- Related fields: H-045, C-006, H-057, F-014
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from Memory Cartographer INV-01 report rev. 2   Last changed: 2026-09-30

## Refuted / replaced (kept for history)

### H-R01 — +0x28 as suspension travel
- Status: REFUTED (before KB creation)
- Grade: REFUTED
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: —
- Related fields: Replaced by F-002
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### H-R02 — +0x3d8 and +0x220 as damage
- Status: REFUTED (before KB creation)
- Grade: REFUTED
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### H-R03 — wheel+0x10 as misalignment or steering response
- Status: REFUTED (before KB creation)
- Grade: REFUTED
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: Actual physics of wheel+0x10 (Q-003).
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### H-R04 — Suspension compression at wheel+0x00 (~0.35)
- Status: REFUTED (before KB creation)
- Grade: REFUTED
- Address: —
- Structure: —
- Function: —
- Meaning: —
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: —
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: Still lives in ADR-004 and in the code (C-003).
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### H-R05 — 0x14073e266/0x14073e3ed as a native reset/snapshot
- Status: REFUTED — SINGLE-SOURCE STATIC (RE Analyst); pending runtime cross-check by Memory Cartographer (INV-01)
- Grade: REFUTED
- Address: —
- Structure: —
- Function: —
- Meaning: The pair is the in-tick look-ahead save/restore (H-018): stack storage, runs every tick, not reachable from any reset path.
- Evidence: /workspace/analyst/INV-01-static-report.md §4.1
- Confidence: —
- Related fields: H-018, H-019 (real API)
- Related functions: —
- How validated: Static disassembly only (objdump, read-only on user's machine). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static report   Last changed: 2026-09-30

### H-R06 — rig+0x1338 timer advances by dt per tick
- Status: REFUTED — observational, one session; RUNTIME CORRELATION (Validation Specialist, INV-01 Phase 1a, read-only polling 2026-09-30 12:07:28–12:08:13 BRT, PID 4113076): one car, one location, one session; OPERATOR CONFIRMATION OF NO F5–F7 PENDING
- Grade: REFUTED
- Address: —
- Structure: —
- Function: —
- Meaning: +0x1338 (and +0x133c) advance 0.03333 = 2·dt per tick in 2698/2698 ticks; in ~5% of ticks as two 1/60 steps ~5 ms apart. Not a pure physics clock; async phase brackets using it must count half-steps.
- Evidence: /workspace/validation/INV-01-phase1a.md §R1
- Confidence: —
- Related fields: H-021 (static chain listed timers += dt), H-032
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### H-R07 — rig+0x2508 = abs(v) (literal)
- Status: REFUTED (minor) — observational, one session; RUNTIME CORRELATION (Validation Specialist, INV-01 Phase 1a, read-only polling 2026-09-30 12:07:28–12:08:13 BRT, PID 4113076): one car, one location, one session; OPERATOR CONFIRMATION OF NO F5–F7 PENDING
- Grade: REFUTED
- Address: —
- Structure: —
- Function: —
- Meaning: +0x2508 is signed ±|v|; the gate compares abs(+0x2508), so gate logic is unaffected. Unsigned copy is car+0x310.
- Evidence: /workspace/validation/INV-01-phase1a.md §R5(a)
- Confidence: —
- Related fields: H-037, H-018
- Related functions: —
- How validated: Passive read-only polling of /proc/<pid>/mem (pread), ~158k polls/s; no writes, no ptrace. Data /workspace/validation/phase1a/.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 Phase 1a   Last changed: 2026-09-30

### H-R08 — Validation Specialist N1 call path 0x140dbca20 → 0x140731d40 → 0x1407511e0
- Status: REFUTED — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: REFUTED
- Address: —
- Structure: —
- Function: —
- Meaning: 0x140731d40 jumps to 0x14074f600 (committed push), not 0x1407511e0; the thunk to 0x1407511e0 is 0x140731d50, called from F at 0x140dbc988. N1's conclusions 'Commit every tick' and 'between ticks is not a single safe point' still hold.
- Evidence: /workspace/analyst/INV-01-static-addendum.md §0.3
- Confidence: —
- Related fields: H-044
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-R09 — 0x14075bd70 (Commit tail) as anti-tamper / teleport detection
- Status: REFUTED — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: REFUTED
- Address: —
- Structure: —
- Function: —
- Meaning: It is a pos lerp + quaternion interpolation with no other side effects.
- Evidence: /workspace/analyst/INV-01-static-addendum.md §1.2
- Confidence: —
- Related fields: H-029, H-045
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-R10 — 0x140750f20 as an S writer
- Status: REFUTED — SINGLE-SOURCE STATIC (RE Analyst addendum); pending runtime cross-check (INV-01)
- Grade: REFUTED
- Address: —
- Structure: —
- Function: —
- Meaning: It writes wheel mount position wheel+0x0 via 0x1407308e0 on a wheel base, not S.
- Evidence: /workspace/analyst/INV-01-static-addendum.md §3.2
- Confidence: —
- Related fields: H-055, Q-020
- Related functions: —
- How validated: Static disassembly only (objdump/python, read-only, exe on disk). Not validated at runtime.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from INV-01 static addendum   Last changed: 2026-09-30

### H-R11 — Explicit Euler position update (p += v_old·dt)
- Status: REFUTED 2026-09-30 (runtime, two captures)
- Grade: REFUTED
- Address: —
- Structure: —
- Function: —
- Meaning: Explicit Euler reproduces p in only 295/4 959 and 172/3 776 ticks versus 4 959/4 959 and 3 776/3 776 for semi-implicit.
- Evidence: /workspace/captures/INV-01/REPORT.md (rev. 2) §(e) P3
- Confidence: —
- Related fields: F-012
- Related functions: —
- How validated: —
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from Memory Cartographer INV-01 report rev. 2   Last changed: 2026-09-30

