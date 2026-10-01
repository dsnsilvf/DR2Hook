# INV-01 (data-flow/runtime half) - Memory Cartographer
The game is running NOW on the user's machine (machineId 79dac8ac-9d7c-4ae5-8caa-47c725ac2b3f); use Shell with that machineId. READ-ONLY: no memory writes.
Project: /workspace/DR2ModLoader on the box, and /home/deivison/Projetos/DR2ModLoader on the user's machine. See docs/reverse_engineering/vehicle_physics.md, vehicle_transform.md, physics_rig.md, tools/dr2rec (process_vm_readv) and scripts/capture_suspension_pair.py.

Goal: map the per-tick data flow of the car state in the PhysicsRig, to find where a restore has to write for the state to stick.

## Do NOT re-investigate
- Rig chain (validate with [rig+0x12c0]==rig and [rig+0x12d0]==4)
- Orientation +0x2e0/+0x2f0-0x310; suspension +0x1504 and siblings
- Known copies: +0x2b0 -> +0x320/+0x330 (0x140746150), +0x180 -> +0x2c0/+0x210 (0x140746700)

## Questions
1. Per tick, in what order are +0x180, +0x2b0, +0x2c0, +0x2d0, +0x2e0 and +0x320/+0x330 updated? Which field is the origin and which are copies?
2. Does container+0xcd0 (visual anchor, HYPOTHESIS) derive from +0x2d0?
3. Is there a secondary copy of the state (previous or interpolated)?
4. Record the build: exe hash and PE timestamp.

## Evidence
A CSV series with bit-for-bit equality and the 0/1-tick lag between fields. Save to /workspace/captures/INV-01/.

## Output
A flow diagram (origin -> copies, with tick lag); a table: field | origin/copy | evidence | grade; the open gaps. Report to RE Orchestrator.
