# INV-01 (static half) - RE Analyst
Goal: work out what the integrator actually reads as the source of truth for the car state (position, orientation, velocities). The savestate depends on this answer.

Project copy: /workspace/DR2ModLoader (docs/reverse_engineering: vehicle_physics.md, vehicle_transform.md, physics_rig.md, wheels.md). Offsets are relative to the rig (PhysicsRig).
The game is running on the user's machine (machineId 79dac8ac-9d7c-4ae5-8caa-47c725ac2b3f). READ-ONLY: do not write to game memory.

## Do NOT re-investigate
- Rig chain: exe+0x1681ce8 -> car+0x30 -> container+0x08 -> rig
- Quaternion +0x2e0 and basis +0x2f0/+0x300/+0x310
- The +0x1660 formula and the wheel order (RL, RR, FL, FR)
- Copies: +0x2b0 -> +0x320/+0x330 (0x140746150), +0x180 -> +0x2c0/+0x210 (0x140746700)

## Current status
- PROBABLE: position +0x2d0, velocities +0x320/+0x330
- Snapshot/restore candidates: 0x14073e266 / 0x14073e3ed
- No isolated writer of +0x2d0/+0x2e0 found yet

## Questions
1. Which instructions write +0x2b0, +0x180, +0x2d0 and +0x2e0?
2. Do +0x320/+0x330 have a reader that feeds the next tick, or are they output only?
3. Is the 0x14073e266/0x14073e3ed pair a native reset/snapshot? Which fields does it save and restore, and who calls it (check the "teleport"/"reset_vehicle" strings)?
4. Is there sub-step or per-wheel state that a restore must also reset?

## Required evidence
Instruction addresses with disassembly excerpts and the xref chain. Grade every claim CONFIRMED/PROBABLE/HYPOTHESIS/UNKNOWN/REFUTED and say what would refute it.

## Output
Table: field | writers | readers | derived from | grade | evidence.
Then a list of controlled experiments for the Validation Specialist (write X, read N ticks later).
Report to RE Orchestrator. Memory Cartographer is doing the runtime half in parallel.
