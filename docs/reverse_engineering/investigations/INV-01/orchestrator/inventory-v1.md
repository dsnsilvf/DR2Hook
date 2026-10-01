# DR2Hook - Graded inventory v1 (RE Orchestrator, 2026-09-30)
Grades are based ONLY on evidence recorded in the repo /workspace/DR2ModLoader (docs/reverse_engineering), not on the docs' own labels. Offsets are relative to the rig.

## CONFIRMED
- Rig chain exe+0x1681ce8 -> car+0x30 -> container+0x08 -> rig, in this build (105953 samples, [rig+0x12c0]==rig, [rig+0x12d0]==4). "Active player car" is only PROBABLE.
- Suspension travel +0x1504/+0x1924/+0x1d44/+0x2164, order RL, RR, FL, FR. Re-verified: 105947/105953 match UDP bit for bit.
- +0x1660 = base + [+0x1504]*Up; +0x1670 = base. Writer 0x140748670.
- Quaternion +0x2e0; basis Right/Up/Forward +0x2f0/+0x300/+0x310.
- Tyre byte +0x2cf0+i (0 intact, 1 punctured, 2 rim, 3 wheel off). Controlled write plus writer 0x1407636b0.
- Axle point +0x5a0/+0x5b0 -> +0x1480; scalar +0x14e8. Track/wheelbase arithmetic +0x2514/+0x2518 (names PROBABLE).
- Wheel vectors +0x00 (Up), +0x20, +0x30. 4x4 table +0x12dc: contents confirmed, consumer UNKNOWN.
- Copies +0x2b0 -> +0x320/+0x330 (0x140746150); +0x180 -> +0x2c0/+0x210 (0x140746700). Meanings UNKNOWN.
- StatePauseScreen 0x140031b60, vtable 0x141250b50, dispatcher 0x140285640. The pause menu item works in game. Title via 0x1403a8820.
- NeFS v2 + AES-256-ECB + deflate; BXML format; injection at 0x140c54270.

## PROBABLE
- Position +0x2d0; velocities +0x320/+0x330 (unit not measured).
- Integrator +0x1500 clamped to +/-20 (h UNKNOWN).
- Engine speed +0x13d8 (rad/s); rev limiter +0x140c (785.398 = 7500 RPM); gear +0x1448 (10 = reverse).
- DynamicsCarImpl size 0x3130 (no RTTI address recorded).
- Damage channel table 0.10/0.05/0.05/0.90 (the doc says confirmed, but there is no xref).
- Native tabs with 24 rows (not validated in game). PE sections, MSVC/LTCG. Existence of the teleport/reset_vehicle strings.

## HYPOTHESIS
- Alternative pointers +0x15a4b00, +0x15a9760, +0x201b7c0, +0x20203a8.
- Idle +0x8e8, max power +0x918, gear count +0x8f4/+0x1400.
- Visual anchor container+0xcd0 (y about physics - 0.44 m).
- The 13 questions in ui_limits.md.

## UNKNOWN
- +0x370 (RPM mapping reverted in c520018). Accumulator +0x2d00+4i and its thresholds. Physics of wheel +0x10.
- car+0x130/+0x160, exe+0x15a3607. Session mode (the enum in safety.h is invented). Game version/hash. Anti-cheat.

## REFUTED
- +0x28 as suspension travel; +0x3d8 and +0x220 as damage; wheel +0x10 as misalignment or steering response.
- Suspension compression at wheel +0x00 (~0.35). Still lives in ADR-004 and in the code.

## Open contradictions
- suspension.md describes a different series than the CSV on disk (2065 datagrams/99.9 s vs 105953 rows/580.8 s).
- SafetyGuard is permissive (core_module.cpp:101). getVehicleTelemetry/onStageStart do not exist.

## Open investigations
- INV-01: source of truth for the car state (RE Analyst: static; Memory Cartographer: runtime). Briefs in /workspace/orchestrator/briefs/.
