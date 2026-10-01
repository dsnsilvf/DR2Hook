# Glossary

| Canonical term | Aliases used by agents | Definition |
|---|---|---|
| rig | PhysicsRig (DynamicsCarImpl is NOT an established alias, Q-021) | Object reached by F-001; offsets are rig-relative unless stated |
| car | — | Object at [exe+0x1681ce8] |
| container | — | Object at [car+0x30] |
| wheel+0x.. | — | Offsets relative to a per-wheel struct |
| Wheel order | RL, RR, FL, FR | Index order for per-wheel arrays (F-002) |
| S | state block | rig+0x2b0; S+0x20 = rig+0x2d0 (RE Analyst convention) |
| rigid-body integrator | integrator | 0x140746150 (H-014). Not the same as H-004 'wheel suspension state integrator' |
| look-ahead | look-ahead rollback | 0x14073e0c0 save → tentative integrate → restore (H-018) |
| tick-start copies | — | rig+0x170/+0x180 (H-017) |
| committed state | — | rig+0x190..+0x1a8, +0x200, +0x210 (H-029) |
| owner | [rig+0] | Object passed to rig constructor; container in the derived class (H-031) |
| con | container | Abbreviation used by Validation Specialist |
| pxb | proxy body | [[con+0x840]+8] (H-039) |
| settled sample | — | Last poll record before the next tick's timer increment (Phase 1a) |
| gate / look-ahead gate | — | |rig+0x2508| > 0.89408 (H-018, H-037) |
| F | PhysicsStep | 0x140dbc500(dt); task 0x1404b1040 'PhysicsStepTask' (analyst's name) |
| C | per-container sync | 0x140dbca20; task 0x1404b1100 'PostPhysicsTask' |
| PreTick / PostTick / EndStep | — | 0x140749a30 / 0x140db2220→0x140749980 / 0x1407511e0 (analyst's names) |
| L0..L8 | landing points | External-write landing points (H-046); 'between ticks' = L1 |
| G-pull / G-adj / G-place | — | con+0x14 < 0.1 / rig+0x3b8 ≠ 0 / con+0xcc0 > 0 and |con+0xcd0| > 0 |
| adjuster | — | Object at rig+0x3b8 (H-051) |
| ring / idx | ‖v‖ history ring | rig+0x8c..+0xb0 float32[10] (dup +0xb4), index rig+0xdc (F-014) |
| render rate / other rate | — | ~92–101 Hz interpolation updates (H-058); distinct from the 60 Hz physics tick |
| cap / cap2 | — | Memory Cartographer runtime captures (4 961 / 3 778 ticks) |
