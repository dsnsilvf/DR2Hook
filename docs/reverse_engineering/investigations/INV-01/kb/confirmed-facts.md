# CONFIRMED FACTS

Source: /workspace/orchestrator/inventory-v1.md (RE Orchestrator, 2026-09-30); graded only on evidence recorded in /workspace/DR2ModLoader docs/reverse_engineering. Offsets are relative to the rig (PhysicsRig) unless stated.
A dash (—) means the field was not recorded in the source.

### F-001 — Rig pointer chain
- Status: CONFIRMED
- Grade: CONFIRMED (inventory v1)
- Address: exe+0x1681ce8 -> car+0x30 -> container+0x08 -> rig
- Structure: PhysicsRig (rig)
- Function: —
- Meaning: Pointer chain from the executable to the rig, in this build.
- Evidence: inventory v1 (RE Orchestrator), repo evidence: 105953 samples with [rig+0x12c0]==rig and [rig+0x12d0]==4.
- Confidence: High
- Related fields: rig+0x12c0 (self-pointer), rig+0x12d0 (==4)
- Related functions: —
- How validated: Sampled invariants over 105953 samples.
- What remains unknown: Whether this is the active player car (H-001, PROBABLE). Game version/hash not recorded (Q-008).
- Update 2026-09-30: Static report: container+8 = rig, consistent (H-031).
- Update 2026-09-30: Phase 1a G2 at start and end: [rig+0x12c0]==rig, +0x12d0 == 4, con+8 == rig, rig+0 == con; chain unchanged (car 0x4b254b80, con 0x4b28f100, rig 0x4b29bab0). Consistent.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### F-002 — Suspension travel per wheel
- Status: CONFIRMED
- Grade: CONFIRMED (inventory v1)
- Address: rig+0x1504 / +0x1924 / +0x1d44 / +0x2164
- Structure: PhysicsRig, per-wheel
- Function: —
- Meaning: Suspension travel; wheel order RL, RR, FL, FR.
- Evidence: inventory v1 (RE Orchestrator), repo evidence: re-verified 105947/105953 samples match UDP bit for bit.
- Confidence: High
- Related fields: +0x1660, +0x1670 (F-003)
- Related functions: —
- How validated: Bit-for-bit comparison against UDP telemetry.
- What remains unknown: 6 of 105953 samples did not match (cause not recorded). See also C-001 about which series the doc describes.
- Update 2026-09-30: Static report: +0x1504 is wheel+0x84, wheel base rig+0x1480, stride 0x420; Reset also sets +0x88/+0x8c (H-027). Consistent; fact not changed.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### F-003 — Wheel point formula +0x1660 / base +0x1670
- Status: CONFIRMED
- Grade: CONFIRMED (inventory v1)
- Address: rig+0x1660, rig+0x1670
- Structure: PhysicsRig
- Function: —
- Meaning: +0x1660 = base + [+0x1504]*Up; +0x1670 = base.
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: High
- Related fields: +0x1504 (F-002); Up vector
- Related functions: Writer 0x140748670
- How validated: —
- What remains unknown: —
- Update 2026-09-30: Static report: 0x140748670 is per-wheel geometry, called at tick start and inside the look-ahead (H-018, H-021). Consistent.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### F-004 — Orientation quaternion and basis
- Status: CONFIRMED
- Grade: CONFIRMED (inventory v1)
- Address: rig+0x2e0 (quaternion); rig+0x2f0 / +0x300 / +0x310 (Right / Up / Forward)
- Structure: PhysicsRig
- Function: —
- Meaning: Car orientation as quaternion and basis vectors.
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: High
- Related fields: +0x2d0 position (H-002)
- Related functions: —
- How validated: —
- What remains unknown: Writer of +0x2e0 not isolated (INV-01).
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### F-005 — Tyre state byte
- Status: CONFIRMED
- Grade: CONFIRMED (inventory v1)
- Address: rig+0x2cf0+i (per wheel i)
- Structure: PhysicsRig, byte array
- Function: —
- Meaning: 0 intact, 1 punctured, 2 rim, 3 wheel off.
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: High
- Related fields: —
- Related functions: Writer 0x1407636b0
- How validated: Controlled write plus identified writer 0x1407636b0.
- What remains unknown: —
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### F-006 — Axle point, scalar, track/wheelbase arithmetic
- Status: CONFIRMED
- Grade: CONFIRMED (inventory v1); names PROBABLE (H-003)
- Address: rig+0x5a0 / +0x5b0 -> +0x1480; scalar +0x14e8; +0x2514 / +0x2518
- Structure: PhysicsRig
- Function: —
- Meaning: Axle point (+0x5a0/+0x5b0 feeding +0x1480), scalar +0x14e8, track/wheelbase arithmetic at +0x2514/+0x2518.
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: High for the arithmetic; names only PROBABLE
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: Whether the names track/wheelbase are correct (H-003).
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### F-007 — Wheel vectors and 4x4 table
- Status: CONFIRMED
- Grade: CONFIRMED (inventory v1)
- Address: wheel+0x00 (Up), wheel+0x20, wheel+0x30; rig+0x12dc 4x4 table
- Structure: Wheel struct; PhysicsRig
- Function: —
- Meaning: Wheel vectors (+0x00 is Up); 4x4 table contents confirmed.
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: High
- Related fields: wheel+0x10 (Q-003)
- Related functions: —
- How validated: —
- What remains unknown: Consumer of the 4x4 table (Q-001); meaning of wheel+0x20/+0x30 not recorded.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### F-008 — Known state copies
- Status: CONFIRMED
- Grade: CONFIRMED (inventory v1); meanings UNKNOWN
- Address: rig+0x2b0 -> +0x320/+0x330; rig+0x180 -> +0x2c0/+0x210
- Structure: PhysicsRig
- Function: —
- Meaning: Copy operations confirmed; what the fields mean is UNKNOWN.
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: High for the copies
- Related fields: +0x320/+0x330 velocities (H-002)
- Related functions: 0x140746150 (+0x2b0 copy), 0x140746700 (+0x180 copy)
- How validated: —
- What remains unknown: Meaning of the source/destination fields (Q-002); tick order (INV-01).
- Update 2026-09-30: INV-01 static report (single-source) characterizes 0x140746150 as the rigid-body integrator; the +0x2b0 → +0x320/+0x330 copy is its first instructions (H-014). +0x180 → +0x2c0 via 0x140746700 at 0x14073e24c (H-017). The copies stand; proposed meanings are in H-015/H-016/H-017, pending cross-check. Fact not changed.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### F-009 — Pause screen state
- Status: CONFIRMED
- Grade: CONFIRMED (inventory v1)
- Address: —
- Structure: —
- Function: StatePauseScreen 0x140031b60; vtable 0x141250b50; dispatcher 0x140285640; title via 0x1403a8820
- Meaning: Pause screen state machine entry; a custom pause menu item works in game.
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: High
- Related fields: —
- Related functions: —
- How validated: The pause menu item works in game.
- What remains unknown: —
- Update 2026-09-30: F-011 evidence: 0x140285640 (dispatcher) and 0x1403a8820 (title) are detoured (e9 jump) in the running image.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### F-010 — Asset archive format and injection
- Status: CONFIRMED
- Grade: CONFIRMED (inventory v1)
- Address: —
- Structure: —
- Function: Injection at 0x140c54270
- Meaning: NeFS v2 + AES-256-ECB + deflate; BXML format; injection point.
- Evidence: inventory v1 (RE Orchestrator), repo evidence
- Confidence: High
- Related fields: —
- Related functions: —
- How validated: —
- What remains unknown: —
- Update 2026-09-30: F-011 evidence: 0x140c54270 (injection) is detoured (e9 jump) in the running image.
- Recorded: 2026-09-30 by Knowledge Keeper from inventory v1   Last changed: 2026-09-30

### F-011 — Build identity of the running dirtrally2.exe
- Status: CONFIRMED — promoted 2026-09-30 by RE Orchestrator ruling (two sources); was H-013
- Grade: CONFIRMED
- Address: —
- Structure: —
- Function: —
- Meaning: File: SHA-256 c119f509fc315d8168aa7aaa5d0a1ee8e3570c7220bd6cbb2d337d6725d73442, 24,668,160 B, PE TimeDateStamp 0x605cbae3 ('Thu Mar 25 13:31:31 2021' as printed on the user machine), ImageBase 0x140000000 (loaded at 0x140000000). Running process (pid 4113076, Proton, started 2026-09-30 11:33:57 BRT) maps that file (dev 103:02, inode 42601200). In-memory .text equals disk except 5 five-byte detour runs (e9 jumps) at 0x140285640, 0x1403a8820, 0x140c54270, 0x140d040c0, 0x140d09380; .pdata/.tls/.gfids/_RDATA/.rsrc/.reloc match; .rdata/.data differ (relocated pointers and runtime data).
- Evidence: /workspace/analyst/INV-01-static-addendum.md §0.1 (file hash, inode/dev mapping); Memory Cartographer /workspace/captures/INV-01/text_hash.txt and text_diff.txt (section hashes, differing runs)
- Confidence: High
- Related fields: H-013 (source entry), Q-008 (answered); F-009/F-010 (their addresses 0x140285640, 0x1403a8820, 0x140c54270 are among the detoured runs)
- Related functions: —
- How validated: Two independent sources: RE Analyst file hash + /proc maps inode/dev; Memory Cartographer disk-vs-memory section diff.
- What remains unknown: The 5 detours lead through FF 25 stubs in page 0x13fff0000 into the project's own dxgi.dll proxy image (cartographer rev. 2). All 15 checked physics addresses match disk; .rdata differs in the IAT and two qwords at 0x141256f18/0x141256f20 redirected into dxgi.dll.
- Recorded: 2026-09-30 by Knowledge Keeper from RE Orchestrator ruling + addendum §0.1 + cartographer text diff   Last changed: 2026-09-30

### F-012 — Rigid-body state block origin and integrator (rig+0x2b0 v, +0x2c0 ω, +0x2d0 p, +0x2e0 q; 0x140746150 semi-implicit Euler, dt = float32(1/60))
- Status: CONFIRMED for this build — promoted 2026-09-30 by RE Orchestrator ruling (independent static + runtime evidence); was H-014 + H-015. Scope: one car, one location, one session (pid 4113076, 2026-09-30); other cars, stages, replay not tested
- Grade: CONFIRMED
- Address: rig+0x2b0 (v), rig+0x2c0 (ω), rig+0x2d0 (p), rig+0x2e0 (q); axes +0x2f0..+0x310 co-written
- Structure: Vector3 v, Vector3 ω, Vector3 p, Vector4 q (16-byte lanes)
- Function: 0x140746150 integrates: p_k = p_{k−1} + v_k·dt (semi-implicit Euler), dt = float32(1/60) = 0x3c888889
- Meaning: Origin state of each physics tick within the sampled memory; every other sampled v/ω/p field is a lag-0 copy, lag-1 copy or interpolation of it.
- Evidence: Static: RE Analyst INV-01 report (H-014, H-015). Runtime: /workspace/captures/INV-01/REPORT.md (rev. 2) §(c), §(e) P3 — fp32 recompute of p bit-exact 4 959/4 959 (cap) and 3 776/3 776 (cap2); explicit Euler 295/4 959 and 172/3 776; final-write order 0x2b0 before 0x320 449/0.
- Confidence: High (for this build and scope)
- Related fields: F-013, F-014, H-016, H-017, H-046, H-R11 (explicit Euler)
- Related functions: 0x140746150, 0x14074b8f0, 0x14073e0c0
- How validated: Two independent sources: static disassembly (RE Analyst) and bit-exact runtime reproduction over two captures (Memory Cartographer).
- What remains unknown: Upstream writer identity by runtime (sampling cannot identify writers; cartographer grades 'origin' as strong within sampled windows). Quaternion product order and world-frame ω stay PROBABLE (from H-014). Generalisation beyond scope.
- Recorded: 2026-09-30 by Knowledge Keeper from RE Orchestrator ruling + Memory Cartographer INV-01 runtime report rev. 2   Last changed: 2026-09-30

### F-013 — rig+0x320/+0x330 and rig+0x170/+0x180 are lag-1 copies of v/ω
- Status: CONFIRMED for this build — promoted 2026-09-30 by RE Orchestrator ruling (independent static + runtime evidence); value parts of H-016, H-017, H-034. Scope: one car, one location, one session (pid 4113076, 2026-09-30); other cars, stages, replay not tested
- Grade: CONFIRMED (value relation)
- Address: rig+0x320/+0x330, rig+0x170/+0x180
- Structure: Vector3 ×2 each (16 B incl. w lane for +0x320/+0x330)
- Function: —
- Meaning: At end of tick k: +0x320/+0x330 == +0x2b0/+0x2c0 of tick k−1, and +0x170/+0x180 (written at tick start from +0x2b0/+0x2c0) equal +0x320/+0x330. Consequence: H-002's reading of +0x320/+0x330 as the velocity is REFUTED (two sources).
- Evidence: Static: RE Analyst (H-016, H-017). Runtime: /workspace/captures/INV-01/REPORT.md (rev. 2) §(c), §(e) P1/P2 — lag 1 4 959/4 959 (cap), 3 776/3 776 (cap2), lag 0 = 0; +0x170 == +0x320 4 960/4 960; Validation Specialist Phase 1a 2 698/2 698 (H-034).
- Confidence: High (for this build and scope)
- Related fields: F-012, H-002, H-016 ('output only' stays PROBABLE), H-017 (write-survival stays HYPOTHESIS/L1 PROBABLE), H-034
- Related functions: —
- How validated: Static + two runtime captures (cartographer) + Phase 1a (Validation Specialist).
- What remains unknown: Which function writes each copy; source of +0x320's value (written after +0x2b0 is overwritten: from +0x170 or a register). 'Output only' (no reader) not promoted.
- Recorded: 2026-09-30 by Knowledge Keeper from RE Orchestrator ruling + Memory Cartographer INV-01 runtime report rev. 2   Last changed: 2026-09-30

### F-014 — Physics tick is a fixed 60 Hz step; ring index rig+0xdc increments once per tick
- Status: CONFIRMED for this build — promoted 2026-09-30 by RE Orchestrator ruling (independent static + runtime evidence); was H-032. Scope: one car, one location, one session (pid 4113076, 2026-09-30); other cars, stages, replay not tested
- Grade: CONFIRMED
- Address: rig+0xdc (int32 idx, mod 10); ring rig+0x8c..+0xb0 float32[10] (duplicate +0xb4..+0xd8)
- Structure: —
- Function: —
- Meaning: Exactly one tick per idx increment; ring[idx−1] = ‖v_k‖ written in the commit record. Integration dt constant float32(1/60). Ticks are decoupled from the render rate (~92–101 Hz updates; wall-time commit intervals bimodal 12–13 / 18–19 ms, mean 16.66 ms).
- Evidence: /workspace/captures/INV-01/REPORT.md (rev. 2) Method §1, §(c), dt table — 4 961/4 961 (cap) and 3 778/3 778 (cap2) increments of exactly +1, 60.011 / 59.996 ticks/s; ring and duplicate 4 960/4 960. Phase 1a H-032. Static dt constant.
- Confidence: High (for this build and scope)
- Related fields: F-012, H-032, H-033, Q-024
- Related functions: —
- How validated: Static + two runtime captures + Phase 1a.
- What remains unknown: Accumulator mechanism (cartographer: 'consistent with a fixed-step accumulator; cause not proven'). Behaviour at other frame rates not tested.
- Recorded: 2026-09-30 by Knowledge Keeper from RE Orchestrator ruling + Memory Cartographer INV-01 runtime report rev. 2   Last changed: 2026-09-30

