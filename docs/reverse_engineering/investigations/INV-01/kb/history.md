# History (append-only)

Format: <date> | <ID> | <event: ADDED/PROMOTED/REFUTED/REPLACED/CONTRADICTION/FLAGGED> | <details, evidence, who>

2026-09-30 | — | KB created | Knowledge Keeper
2026-09-30 | F-001..F-010, H-001..H-012, H-R01..H-R04, Q-001..Q-009, C-001..C-003 | ADDED | Imported from /workspace/orchestrator/inventory-v1.md, grades kept as written | RE Orchestrator via Knowledge Keeper
2026-09-30 | H-R01..H-R04 | REFUTED | Refuted before KB creation per inventory v1 | RE Orchestrator
2026-09-30 | INV-01 | OPENED | Source of truth for car state; RE Analyst (static), Memory Cartographer (runtime) | RE Orchestrator
2026-09-30 | H-009 | CLARIFIED | Pointers are exe-relative; car vs container fallbacks (player.cpp:46,74; physics_rig.md:49-60) | RE Orchestrator
2026-09-30 | INV-01 | STATIC HALF REPORTED | /workspace/analyst/INV-01-static-report.md; recorded single-source, pending runtime cross-check; INV-01 stays OPEN | RE Analyst via RE Orchestrator
2026-09-30 | H-013..H-031, Q-010..Q-020 | ADDED | From INV-01 static report, analyst grades as written, single-source | RE Analyst
2026-09-30 | H-002 | SPLIT / PARTLY REFUTED | Velocity part (+0x320/+0x330 as velocities) REFUTED, single-source static; position part moved to H-015 | RE Analyst, per RE Orchestrator
2026-09-30 | H-R05 | REFUTED | 0x14073e266/0x14073e3ed native snapshot idea; it is the look-ahead rollback (H-018), single-source static | RE Analyst
2026-09-30 | Q-008 | PROPOSED ANSWER | Build identity H-013, pending runtime confirmation | RE Analyst
2026-09-30 | C-004 | CONTRADICTION | container+0xcd0 visual anchor (vehicle_transform.md/H-011) vs placement target (H-024) | RE Orchestrator
2026-09-30 | H-004 | RENAMED | 'Integrator +0x1500' → 'Wheel suspension state integrator (wheel+0x80 / rig+0x1500)' | RE Orchestrator ruling
2026-09-30 | Q-021 | ADDED | PhysicsRig = DynamicsCarImpl not established; RTTI question | RE Orchestrator ruling
2026-09-30 | H-016 | DOWNGRADED | → PROBABLE | RE Orchestrator ruling
2026-09-30 | H-017 | DOWNGRADED (part) | between/mid-tick survival → HYPOTHESIS | RE Orchestrator ruling
2026-09-30 | H-013 | DOWNGRADED | build match → file-only until running image checked | RE Orchestrator ruling
2026-09-30 | F-008 | RULING | annotation-only handling confirmed | RE Orchestrator
2026-09-30 | INV-01 | PHASE 1a REPORTED | /workspace/validation/INV-01-phase1a.md; one car/location/session; F5–F7 confirmation pending; INV-01 stays OPEN | Validation Specialist via RE Orchestrator
2026-09-30 | H-032..H-041, Q-022..Q-027 | ADDED | Phase 1a runtime, grades as written | Validation Specialist
2026-09-30 | H-R06 | REFUTED | +0x1338 timer delta == dt; it is 2·dt per tick (observational, one session) | Validation Specialist
2026-09-30 | H-R07 | REFUTED | +0x2508 = abs(v) literal; it is signed (observational, one session) | Validation Specialist
2026-09-30 | H-025 | PROBABLE-REFUTED | +0x1690 changes in gate-off ticks (formal REFUTED pending I1/I5) | Validation Specialist
2026-09-30 | H-011 | REFUTED (placement-inactive state) | con+0xcd0 constant over 45 s of driving | Validation Specialist
2026-09-30 | H-016, H-017, H-018, H-028, H-029 | CORROBORATED | Second independent source (runtime), grades unchanged | Validation Specialist
2026-09-30 | C-005 | CONTRADICTION | Commit absent from static per-tick chain vs observed every tick | Knowledge Keeper
2026-09-30 | F-011 | PROMOTED from H-013 | Build identity of running exe CONFIRMED by two sources (analyst hash + inode/dev; cartographer .text diff: equal except 5 detour runs) | RE Orchestrator ruling
2026-09-30 | Q-008 | ANSWERED | F-011 | RE Orchestrator ruling
2026-09-30 | INV-01 | STATIC ADDENDUM REPORTED | /workspace/analyst/INV-01-static-addendum.md; single-source static; INV-01 stays OPEN | RE Analyst via RE Orchestrator
2026-09-30 | H-042..H-056, Q-028..Q-034 | ADDED | From static addendum, grades as written | RE Analyst
2026-09-30 | C-005 | RESOLVED | Commit every tick via thunk 0x140731d50 / EndStep (H-044) | RE Analyst, per RE Orchestrator
2026-09-30 | H-R08 | REFUTED | Validation Specialist N1 call path via 0x140731d40 | RE Analyst
2026-09-30 | H-R09 | REFUTED | 0x14075bd70 as anti-tamper/teleport detection | RE Analyst
2026-09-30 | H-R10 | REFUTED | 0x140750f20 as S writer | RE Analyst
2026-09-30 | Q-011, Q-012, Q-014, Q-020 | ANSWERED/PROPOSED | Single-source static answers (H-052, H-056, H-045, H-043/H-R10) | RE Analyst
2026-09-30 | C-006 | CONTRADICTION | rig+0x290/+0x2a0 EMA (Phase 1a) vs Commit lerp (addendum) | Knowledge Keeper
2026-09-30 | INV-01 | RUNTIME HALF REPORTED | /workspace/captures/INV-01/REPORT.md (rev. 2) | Memory Cartographer via RE Orchestrator
2026-09-30 | F-012 | PROMOTED from H-014 + H-015 | State block origin + semi-implicit Euler, dt fp32(1/60); static + bit-exact runtime; scope one car/location/session | RE Orchestrator ruling
2026-09-30 | F-013 | PROMOTED from H-016/H-017/H-034 (value parts) | +0x320/+0x330 and +0x170/+0x180 lag-1 copies of v/ω | RE Orchestrator ruling
2026-09-30 | F-014 | PROMOTED from H-032 | Fixed 60 Hz tick; ring idx +0xdc once per tick | RE Orchestrator ruling
2026-09-30 | H-002 | VELOCITY PART REFUTED (two sources) | See F-013 | RE Orchestrator ruling
2026-09-30 | H-011 | REFUTED IN NORMAL DRIVING | Two runtime sources (H-040, cartographer P6) | RE Orchestrator ruling
2026-09-30 | H-036, H-027, H-046 | KEPT PROBABLE | mass kg, restore recipe, L1 survival | RE Orchestrator ruling
2026-09-30 | C-006 | UPDATED | Lerp (H-045) PROBABLE/favoured, EMA (H-038) disfavoured; OPEN until E-B10 | RE Orchestrator ruling
2026-09-30 | H-048 | GRADE SET | PROBABLE structure / HYPOTHESIS semantics | RE Orchestrator ruling
2026-09-30 | Q-024 | ANSWERED (this session) | Fixed step decoupled from render rate | Memory Cartographer
2026-09-30 | H-057, H-058, H-R11, Q-035, Q-036 | ADDED | From cartographer rev. 2 | Memory Cartographer
