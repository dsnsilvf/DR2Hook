# INV-01 static analysis: addendum (follow-up to the Validation Specialist's challenges)

Analyst: RE Analyst (static). Date: 2026-09-30, about 12:00-13:10 BRT (UTC-3).
Companion to `/workspace/analyst/INV-01-static-report.md` ("SR"). Answers the "static follow-ups" in `/workspace/validation/INV-01-experiment-plan.md` §8.6 and the RE Orchestrator's four trace items.

Method: same as the SR. Everything was read-only. `objdump -d -M intel` and small Python byte readers ran against the exe on disk on machine 79dac8ac. Excerpts were copied to the box under `/workspace/analyst/dis/`:
- `scan2.txt`: whole-.text call and xref scan
- `sigoffs.txt`: whole-.text accesses to `+0x2b0/2c0/2d0/2e0/320/330/370/374`
- `cls320.txt`: classification of the `+0x320/+0x330` hits

I wrote nothing to game memory, the game folder, or the project. No debugger was used and process memory was not read.
All addresses assume image base `0x140000000`. Grades: CONFIRMED / PROBABLE / HYPOTHESIS / UNKNOWN / REFUTED. Names in quotes are mine, not proof.

---

## 0. Build identity and grade changes (applied everywhere in the SR)

### 0.1 Process vs file (new check)
- The running process is `pid 4113076`, `S:\steamapps\common\DiRT Rally 2.0\dirtrally2.exe -novr` under Proton, started 2026-09-30 11:33:57 BRT. Its `/proc/4113076/maps` maps `0x140000000-0x140001000` (and `0x142229000-…`) from **dev 103:02, inode 42601200**, path `/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0/dirtrally2.exe`. I read only the maps metadata, not memory contents.
- `stat` on that path gives inode 42601200, dev 0x10302 (= 103:02), size 24,668,160 B, and mtime 2026-09-07 10:25:52 BRT, which is before process start. `sha256sum` = `c119f509fc315d8168aa7aaa5d0a1ee8e3570c7220bd6cbb2d337d6725d73442`, the same as in the SR.
- A Windows `Get-Process` query does not apply: the machine is CachyOS Linux running the game under Proton, so I used `/proc` instead.
- **Grade:** "the running process maps the same file I disassembled" is **CONFIRMED**.
  - "The in-memory code equals the disk code at our addresses" is **UNKNOWN from my own checks**, because I did not read process memory.
  - Supporting data from another agent: the Memory Cartographer's `/workspace/captures/INV-01/text_hash.txt` and `text_diff.txt` report 5 patched 5-byte runs in .text: `0x140285640, 0x1403a8820, 0x140c54270, 0x140d040c0, 0x140d09380` (detour-style `e9` jumps). None of them is in the physics chain traced here.
- SR §0 "Build match: CONFIRMED" is replaced by: **file identity CONFIRMED (SHA-256); process maps that file CONFIRMED (inode/dev); code-byte equality at hook points UNKNOWN (my checks) / supported by the Cartographer's diff.** The "four known addresses disassemble as documented" check is circular, as the Validation Specialist noted, and is kept only as a sanity check.

### 0.2 Mandated downgrades
| SR claim | New grade | Reason |
|---|---|---|
| "+0x320/+0x330 are output only" | **PROBABLE** (unchanged after the whole-.text scan, §3.3) | The scan found no other reader, but absence stays absence. REFUTED stays scoped to "they are not the integrator's velocity input". |
| "A write between ticks survives; a mid-tick write is lost" | **HYPOTHESIS** as a blanket statement. Replaced by the per-point table in §1.3. | Each point's overwrite or survival is now tied to a deciding instruction (control-flow CONFIRMED). End-to-end survival stays PROBABLE and conditional on runtime gates (G-pull, G-adj, G-place). |
| "Build match CONFIRMED" | See 0.1 | |

### 0.3 Correction to the Validation Specialist's N1 (call path)
- N1 says `0x140dbca20 → 0x140dbcb8a: call 0x140731d40 → 0x1407511e0`. **The call path is REFUTED.**
  - `0x140731d40` is `mov rcx,[rcx+8]; jmp 0x14074f600`.
  - The thunk to `0x1407511e0` is the next one, `0x140731d50`.
  - `0x1407511e0` is actually called from the physics step `0x140dbc500` at `0x140dbc988` (§1.1).
- **The conclusion "Commit runs every tick" still holds (CONFIRMED by code)**, via the other path. N1's point that "between ticks" is not a single safe point also holds.
```
140731d40: mov rcx,[rcx+0x8] ; jmp 0x14074f600   <- called by 0x140dbca20 at 0x140dbcb8a
140731d50: mov rcx,[rcx+0x8] ; jmp 0x1407511e0   <- called by 0x140dbc500 at 0x140dbc988
```

---

## 1. Q1: exact per-tick order and external-write landing points

### 1.1 Call chain (all edges are direct calls or thunks I observed; vtable edges give the vtable address)
```
[task object, vtable slot at .rdata 0x141276fd0]  0x1404b1040(this, double* dt)       "PhysicsStepTask"
   0x1404b1076  [this+0x48]+0x2b0 ->vt+8 (unrelated object, not a rig)
   0x1404b1092  call 0x140dbc500(xmm0=(float)*dt, rdx=[this+0x40])                    "PhysicsStep F"
[task object, vtable slot at .rdata 0x141277028]  0x1404b1100(this)                    "PostPhysicsTask"
   0x1404b1110  call 0x140dbca20                                                       "per-container sync"
```
The order of `0x1404b1040` relative to `0x1404b1100` within a frame, and how many F calls happen per frame, is **UNKNOWN**: both are task vtable slots and the scheduler decides at runtime. Hypothesis H-ORD: F then C, one or more F per frame. Experiment E-B1 tests it.

**F = `0x140dbc500(dt)`**, for all containers `[0x14201b7b0 + 8*i]`, `i < [0x14201b930]`:
```
F.0  0x140dbc51e  if dt == 0 -> return (whole step skipped)
F.1  0x140dbc560..  containers with byte +0xf2: 0x140dadc00, state switch (+0xf0/+0xf1) -> 0x140da94c0 / 0x140da6770
F.2  0x140dbc6fe..  per container 0x140dbe300          (proxy/collision-world prep; calls 0x140df59xx/0x140dfb4xx)
F.3  JOB BATCH mode 0, one job per container (job objects at 0x142020340 + 0x30*i, vtable 0x141400b30,
     job+0x8=container, job+0x10=dt, job+0x28=mode). Jobs 1..n go to workers via 0x14084b690
     (0x140dbc785); job 0 runs inline via [vt+0x10] (0x140dbc7f5); wait at 0x14086bca0 (0x140dbc811).
     Job body 0x140da0d50, mode 0:
       a. 0x140da0d9e  0x140db31f0(con) -> 0x140db31f9 thunk 0x140731950 -> 0x140749a30(rig)      "PreTick"
       b. 0x140da0db4  if job.dt > 0: 0x140dbc430(con, dt)                                       "ContainerTick"
            0x140dbc46e  0x140df7b50([con+0x840]) -> proxy matrix; GATE (see §2) ; jbe -> skip b.*
            0x140dbc4a6  0x140dbcc90  (con timers +0x14/+0x18 += dt; rig timers via 0x14074dd90)
            0x140dbc4b1  thunk 0x140731ca0 -> 0x14074b8f0(rig, dt)                                "rig Update"
                 (SR §2.5 internals: tick-start copies 0x14074b9b3..ba92 ; solver 0x14073f220 ;
                  look-ahead save 0x14073e266 .. restore 0x14073e3ed ; copy-back 0x14073e232/0x14073e24c ;
                  integrate 0x1407395f5 -> 0x140746150)
            0x140dbc4d9  0x140dbc100(con+0x144, rig+0x1330, dt, |rig+0x2508|)  (container-side only)
       c. 0x140da0dc5  0x140db2220(con, dt, 1)                                                     "PostTick"
            0x140db23a1  thunk 0x140731940 -> 0x140749980(rig): push LIVE S.v, S.w to container->proxy
F.4  JOB BATCH mode 1 (0x140dbd740, global) x max(1, 0x140c696e0()) ; wait
F.5  JOB BATCH mode 2 per container (0x140dbd900, uses con+0xc878) ; wait
F.6  0x140dbc963..  per container 0x140dadc00
F.7  0x140dbc988  per container thunk 0x140731d50 -> 0x1407511e0(rig)                                "EndStep"
       0x140751202  if [rig+0x3b8] (adjuster): [vt+0x10](adj, owner, &{p,q}) ; true -> SetPose 0x14075123a
       0x140751244  Commit(rig, dl=1)  0x14074d190   (unconditional)
F.8  0x140dbc9d9  con+0xc930 = (A+0x70 - B+0x70)/dt  (A,B from rig getters 0x1407313d0/0x1407315e0)

C = 0x140dbca20 (per container):
   0x140dbca73  if con+0xcc0 > 0 and |con+0xcd0|^2 > 0:  "placement branch"
        0x140dbcb38 SetLinVel(0, also +0x170/+0x200)  [thunk 0x140731b30 -> 0x14074a910, r8b=1]
        0x140dbcb4d SetAngVel(0, also +0x180/+0x210)  [thunk 0x140731a20 -> 0x14074a890, r8b=1]
        0x140dbcb80 SetTransform(pos from con+0xcd0/+0xcc0) [thunk 0x140731c50 -> 0x14074ad80]
   else 0x140dbcb8a thunk 0x140731d40 -> 0x14074f600(rig): push COMMITTED pose/v/w to container->proxy
```
Grade: the edges are **CONFIRMED** (direct calls and thunks). The job/vtable dispatch edges are CONFIRMED by the vtable contents:
- `.rdata 0x141400b40 = 0x140da0d50`
- `0x141400c30` is the container vtable. The Cartographer's dump shows `con+0 = 0x141400c30`.

Worker-thread execution of containers 1..n is CONFIRMED structurally (the dispatch call). Which container is the player's is UNKNOWN.

### 1.2 New functions on the path (excerpts)

**PreTick `0x140749a30(rig)`**, every F, per container, before the rig Update:
```
140749a5c: mov rcx,[rcx+0x3b8] ; test ; je        ; adjuster ->vt+8(adj, 0, owner)
140749a73: mov rax,[rbx+0x4c0] ; cmp [rbx+0x10d0],rax ; je -> skip 0x140749c80 (setup reload)
140749a8b: local.p = +0x2d0 ; local.q = +0x2e0 ; local.v = +0x2b0 ; local.w = +0x2c0
140749ac5: +0x3c0..+0x3d8 = 0
140749adf: mov rcx,[rbx] ; call [vt+0x10](owner, &pose, &v, &w)      ; owner = container, vt 0x141400c30 -> 0x140db6bb0
140749aed: test al,al ; je 0x140749b8d
140749b0a: +0x3c0 = new.v - S.v ; +0x3d0 = new.w - S.w
140749b70: call 0x140746770  SetPose(S, new pose)         <-- WRITES +0x2d0/+0x2e0 (+basis)
140749b7c: call 0x1407308e0  S.v = new.v                  <-- WRITES +0x2b0
140749b88: call 0x140746700  S.w = new.w                  <-- WRITES +0x2c0
140749b8d: inertia = decode(+0x590,+0x594,+0x598) ; 0x140746730(S, &inertia)   <-- WRITES +0x360 EVERY TICK
140749bf1: owner->vt+0x18(mass=+0x370, &inertia)          ; push mass/inertia to proxy (0x140db2110)
140749bfb: 0x140743680(rig+0x2bd0) -> +0x13b8,+0x13bc,+0x13d4,+0x139c,+0x13c0 ; per wheel 0x1407305f0
```
**Pull source `0x140db6bb0(con, &pose, &v, &w)`** (container vtable 0x141400c30, slot +0x10):
```
140db6bc7: movss xmm0,[0x1411f7c90] (=0.1) ; comiss xmm0,[rcx+0x14] ; jbe -> return false
140db6bf1: 0x140df7b50([con+0x840]) -> proxy 4x4 ; 0x140daada0 -> pose ; store to *pose
140db6c1d: 0x140df5bb0([con+0x840]) -> v (w lane zeroed) -> *v
140db6c65: 0x140df4480([con+0x840]) -> w (w lane zeroed) -> *w ; return true
```
**Push `0x140749980(rig)`** (PostTick) and **push `0x14074f600(rig)`** (C, non-placement):
```
1407499a5: adjuster ->vt+8(adj, 2, owner)
1407499ca: owner->vt+0x28(&S.v) ; owner->vt+0x30(&S.w)          ; 0x140db20d0 -> proxy 0x140e174a0 ; 0x140db2090 -> 0x140e11db0
14074f608: decode committed pose +0x190..+0x1a8 -> owner->vt+0x20(&pose)   ; 0x140db2190 -> proxy 0x140e17ae0
14074f6b5: owner->vt+0x28(&rig+0x200) ; owner->vt+0x30(&rig+0x210)
14074f6d5: 0x14074da50(rig) (per-wheel [vt+8] calls; no S offsets seen) ; [rig+0x440]->vt+8(rig+0x3e0)
```
**Commit `0x14074d190(rig, dl)`**, now fully read:
```
14074d1c2..d2a1: +0x190..+0x1a8 = encode(S.p(+0x2d0..2d8), S.q(+0x2e0..2ec))
14074d2a7: +0x200 = S.v ; 14074d2b1: +0x210 = S.w
14074d34d: 0x14074f140(rig, decoded pose)
14074d352: alpha = [rig+0x1198]->vt+0x10()            (rig+0x1198 = 0x1420209e0 in the Cartographer's dump: global object)
14074d362: if dl==0 or alpha==1.0 (0x1411f7cb8): copy +0x190..+0x21f -> +0x220..+0x2af (0x14074d909..d980)
          else: +0x220 = enc(lerp(prev pose +0x100.., cur pose +0x190.., alpha))   0x14075bd70 -> 0x14074a7d0
                +0x23c = same for pose history +0x11c.. / +0x1ac..
                +0x258..+0x270 = same for +0x138.. / +0x1c8.. (encoded inline)
                +0x274 = 0x14073bc20(...)
                +0x290 = lerp(+0x170, +0x200, alpha) ; +0x2a0 = lerp(+0x180, +0x210, alpha)
14074d98b..d9c2: 0x140736240 / 0x14073d840(rig+0x11a0) / 0x14073be80(rig+0x11a0, ..., local buf)
14074d9da: per wheel 0x140730350(wheel, dl)
```
**`0x14075bd70(a, out, b, t)`** (the "Commit tail" the plan flagged as possible anti-tamper): `out.pos = a.pos + (b.pos - a.pos)*t` (0x14075bd9a..bde4); `out.q = 0x14075bba0(a.q, b.q, t)`. It has no other memory side effects.
- "Interpolation between tick-start state and committed state for rendering": **PROBABLE**. Structure CONFIRMED.
- The anti-tamper / teleport-detection hypothesis for 0x14075bd70 is **REFUTED**.

### 1.3 External-write landing points (single-threaded view; §1.4 covers threads)
Gates used below: **G-pull** = `con+0x14 < 0.1` (pull active). **G-adj** = `rig+0x3b8 != 0`. **G-place** = `con+0xcc0 > 0 && |con+0xcd0| > 0`.

| # | Landing point (between) | Write to S.v/S.w (+0x2b0/+0x2c0) | Write to S.p/S.q (+0x2d0/+0x2e0) | Deciding instruction(s) | Grade |
|---|---|---|---|---|---|
| L0 | After F returns, before C | Survives F-boundary copies, but C pushes the **committed** (old) v/w to the proxy. If G-place: **zeroed** by C. | Proxy gets the **old committed** pose from C. If G-place: **overwritten** by SetTransform. | 0x14074f6b5/0x14074f608; 0x140dbcb38/b4d/b80 | CONFIRMED (control flow) |
| L1 | After C, before next F (the SR's "between ticks", the plan's B2) | Survives into the integration **unless** G-pull (0x140749b7c/b88 overwrite from proxy) or G-adj (adjuster vt+8 may act; body UNKNOWN). | Survives **unless** G-pull (0x140749b70 SetPose from proxy). | 0x140db6bd2 (gate), 0x140749aed | Survival PROBABLE, conditional on G-pull=0, G-adj=0 |
| L2 | PreTick end → rig Update start (0x140749c4d..0x14074b8f0) | Survives (tick-start copy takes it into +0x170/+0x180) | Survives (tick-start encode into +0x100) | 0x14074ba84/ba92 | CONFIRMED |
| L3 | Inside rig Update, after 0x14074ba92, before 0x14073e232 ("M1") | **Lost**: S.v ← +0x170, S.w ← +0x180 | Survives this copy-back (p/q are not restored from +0x100) | 0x14073e232 / 0x14073e24c | CONFIRMED (control flow) |
| L4 | Between look-ahead save 0x14073e266 and restore 0x14073e3ed ("M2") | Lost (restored from the save) | Lost (restored) | 0x14073e3e3/e8/ed | CONFIRMED (when the look-ahead runs: \|+0x2508\| > 0.894) |
| L5 | After the restore, before the integrate (0x1407395f5) | Survives and is integrated | Survives and is integrated | 0x140746150 | CONFIRMED |
| L6 | After the rig Update returns, still in the job (plan's B1) | Survives. PostTick 0x140749980 pushes it (live) to the proxy. | Survives. The proxy pose is *not* pushed here. | 0x140db23a1 | CONFIRMED |
| L7 | After all jobs, before EndStep (F.4–F.6) | Survives | Survives, unless G-adj and adjuster vt+0x10 returns true → SetPose overwrites | 0x14075122a/0x14075123a | CONFIRMED (control flow) |
| L8 | After Commit, same F (F.8) | Survives in S, but committed +0x200/+0x210 and +0x190.. hold the **pre-write** values until the next Commit. C then pushes the stale committed state to the proxy. | Same | 0x14074d1f9..d2b8, 0x14074f600 | CONFIRMED |

- **Revised blanket statement:** "a write to S at L1 survives into the next integration" is **PROBABLE**, conditional on G-pull=0, G-adj=0, G-place=0 and on no other thread writing S.
  - In the Cartographer's frozen dump, the gates had these values:
    - `con+0x14` = FLT_MAX, so G-pull=0;
    - `rig+0x3b8` = 0;
    - `con+0xcc0` = 0.
  - That is one sample, so the values must be re-checked per trial.
- "A mid-tick write is lost" is true only for v/w in L3, and for all 15 look-ahead fields in L4 (CONFIRMED control flow). It is **false for p/q in L3** (they survive).
- **Implication for a savestate:** writing S alone at L1 leaves three things stale until the next Commit:
  - the committed block (+0x190.., +0x200, +0x210);
  - the interpolated render block (+0x220..+0x2af);
  - the proxy (pose until the next C, v/w until the next PostTick).

  The integration itself uses S (CONFIRMED). The stale copies self-heal after one F plus one C (PROBABLE, from the control flow; see §2.3 for what could break that).

### 1.4 Threading (new)
- Containers 1..n run F.3 on worker threads; container 0 runs on F's thread (0x140dbc785 vs 0x140dbc7f5).
- Any DR2Hook write from the Present thread can land at any of L1–L8 for a given container, unless it is synchronised to F's boundaries. The thread that runs F relative to Present is **UNKNOWN**.

---

## 2. Q2: the con+0x840 proxy and the rig-tick gate

### 2.1 The gate at 0x140dbc430 (CONFIRMED structure)
```
140dbc439: cmp qword [0x14201b788],0 ; je 140dbc45d
140dbc453: movdqa xmm6,[0x1420202d0]          ; global vec4 G (bss; runtime value UNKNOWN)
140dbc462: rcx = [con+0x840] ; 140dbc46e: call 0x140df7b50(proxy, &buf)
   140df7b50: rax=[rcx+0x10] ; buf[0..3] = [rax+0x1c0/0x1d0/0x1e0/0x1f0]    ; proxy world matrix, row 3 = translation
140dbc473: mask = pcmpgtd({0,0,0,1},{0,0,0,0}) = {0,0,0,~0} ; xmm2 = andnps(mask, buf+0x30) ; .y lane
140dbc48a: xmm6.y - [0x141227fe0] (= 50.0f)
140dbc49b: comiss proxy_T.y, G.y - 50 ; jbe 0x140dbc4de   -> skips 0x140dbcc90, the rig Update, 0x140dbc100
```
- The gate reads **only the proxy translation's y component** (y-up), compared against `G.y − 50 m`.
- If `[0x14201b788] == 0`, xmm6 comes from an uninitialised stack slot `[rsp+0x20]`. That is odd code; presumably the flag is always set in-game (HYPOTHESIS).
- Reading: a "fell through the world" guard. The rig only ticks while the proxy is above a floor 50 m below a world reference height. The semantics are **PROBABLE**; what G is remains UNKNOWN.

### 2.2 What happens when the proxy is stale after a raw rig write
Answers to the Orchestrator's options:
- **"Is the tick skipped?"** Only if the stale proxy y is ≤ G.y − 50. A savestate restore that puts the car back from below that floor while the proxy still holds the low pose would skip the tick. That skip lasts until something refreshes the proxy pose, and the refresh comes from C 0x14074f600 using the *committed* pose, which Commit only updates when the tick runs.
  - **Deadlock hypothesis (HYPOTHESIS):** if the rig does not tick, F.7 Commit still runs, because EndStep is not gated by the tick. Commit then copies S (the restored pose) into committed, and C pushes it to the proxy, so the gate recovers after about one frame.
  - Supporting: EndStep 0x140dbc988 is outside 0x140dbc430 and unconditional.
  - Missing: runtime check.
  - Refuted if: the gate stays closed for more than 2 frames after a restore from below the floor.
- **"Is the rig state overwritten from the proxy?"** Only when G-pull holds (`con+0x14 < 0.1`). Then PreTick overwrites S.p/q/v/w from the proxy every F (§1.2). Otherwise it is **not** overwritten (CONFIRMED control flow).
  - `con+0x14` is incremented by dt every tick (0x140dbccca/0x140dbccdf).
  - It is set to **FLT_MAX** (`[0x1415bb5ac]` = 3.4028235e38 on disk) by Container::Reset `0x140db7740` (0x140db77c0), by `0x140db7850` (0x140db7963) and by `0x140db95d0` (0x140db961f, 0x140db9777).
  - With FLT_MAX, pull stays off. **I found no writer that sets `con+0x14` below 0.1** in `0x140da0000–0x140dc0000`; the other `[reg+0x14]` stores there belong to other objects.
  - So: the pull is off in normal driving (**PROBABLE**; the Cartographer's dump shows `con+0x14 = FLT_MAX`). What turns it on is **UNKNOWN**. It could be a writer outside that range, a memcpy-style init, or a network/replay path.
- **"Something else":** the proxy drives collision (`0x140dbe300` in F.2, `0x140db2220` reads proxy v at 0x140db28a9). The effect of one frame of stale proxy pose on contacts is **UNKNOWN**.

### 2.3 Proxy sync paths, complete list found (CONFIRMED edges; "only these" is PROBABLE)
| Direction | Where | What | Condition |
|---|---|---|---|
| rig → proxy v/w (live S) | PostTick 0x140749980 (F.3c) | owner vt+0x28/+0x30 | every F, per container |
| rig → proxy pose, v, w (committed) | C 0x14074f600 | owner vt+0x20/+0x28/+0x30 | every C, when not G-place |
| rig → proxy mass/inertia | PreTick 0x140749bf1 | owner vt+0x18 | every F |
| rig → proxy (setters) | SetTransform 0x14074adb6, SetLinVel 0x14074a943, SetAngVel, Reset etc. (SR §4) | vt+0x20/+0x28/+0x30 | on call |
| proxy → rig (pull) | PreTick 0x140749ae7 → 0x140db6bb0 | pose, v, w overwrite S | G-pull |
| proxy → gate | 0x140dbc46e | translation y | every F |

SR §5.2 said a raw savestate "leaves the proxy stale". The revised grade: **stale for at most one F + one C, then self-healing (PROBABLE)**, unless G-pull or G-place hold.

---

## 3. Q3: untraced paths, the hook object, and .text-wide +0x320/+0x330 readers

### 3.1 0x140749a30
This is PreTick, above. It is **not** a rare path: it runs every F for every container. Its S writes (0x140749b70/7c/88) happen only under G-pull. Its +0x360 write (0x140749bdf) is unconditional. Grade: **CONFIRMED** (control flow).

### 3.2 0x140750f20
- Reached only from `0x140749c80` ("setup reload"), which runs when `rig+0x4c0 != rig+0x10d0` and `rig+0x4c0 != [[rig+0x1120]+0xe8]` (0x140749a7a, 0x140749c97).
- It writes the per-wheel mount position `wheel+0x0` (wheel = rig+0x1480+0x420*i; `0x140750ff9: call 0x1407308e0`, from setup +0x5a0/+0x5b0 with x mirrored by `[0x1411f7d10]`). It also sets `wheel+0x6c..+0x78 = {1,0,1,0}`, rig+0x2514/+0x2518, then calls 0x14074ee50.
- **It does not write S (REFUTED as an S writer).**
- 0x140749c80 also re-sets mass (0x140746710 from enc +0x584) and inertia, and copies +0x8e0.. → +0x4a8...
- A restore that changes car setup could trigger this. **HYPOTHESIS**: it does not run in normal driving; the dump's +0x4c0/+0x10d0 differ, so the second compare decides.

### 3.3 0x140745b60
A pure function: `out = q ⊗ v ⊗ q*` (rotation of `*r8` by quaternion `*rcx`; writes 3 floats to `*rdx`; 0x140745b60–0x140745ce7). It has no persistent side effects (**CONFIRMED**).

### 3.4 The rig+0x2550 "hook" (H-022)
- Whole-.text scan for any operand containing `0x2550` found exactly these:
  - `0x140746c3e mov [rbx+0x2550],r15`: the rig constructor, with `r15 = 0` from `0x140746b9e xor r15d,r15d`;
  - `0x1407311b4 mov rax,[rax+0x2550]`: the container getter `0x1407311b0`, used by 0x140738cc0;
  - `0x14046526d mov dword [rcx+0x2550],r14d`: a 4-byte store in an unrelated function, not a qword pointer store, so a different class (PROBABLE).
- There is **no code that installs a hook object**, so there is no vtable and there are no implementations to trace.
- Grade: **"+0x2550 is always null in this build" PROBABLE**. It is refuted if any runtime sample shows it non-null; writes through memcpy or computed addressing cannot be excluded statically.
- The same holds for `+0x2548` (freeze byte) and `+0x2560` (kinematic target): the only rig-side writes are the constructor zeroing them (0x140746c37, 0x140746c4c). Freeze and kinematic branches are therefore **PROBABLE dead in this build**.
- The SR's warning "a hook that edits S cannot be ruled out" is downgraded to **low risk; G2 should still poll it**.

### 3.5 The rig+0x3b8 "adjuster" (N1)
- Writers: the constructor (`0x140746cd7`) and **one** setter, the container thunk `0x140731a30: mov rax,[rcx+8]; mov [rax+0x3b8],rdx`.
- Its only caller is `0x1402736c5` in `0x140273560`, gated by `cmp [0x141f597a0],0` (0x140273672). The adjuster object is `r8+0x338`, where `r8` = an entry from the game-mode table `[0x141695150]->[+8][idx 0x141693ffc]`.
- It is called at:
  - PreTick vt+8(adj, 0, owner);
  - PostTick vt+8(adj, 2, owner);
  - EndStep vt+0x10(adj, owner, &pose) → SetPose when it returns true.
- Class and vtable: **UNKNOWN** (the object is embedded; its constructor was not found).
- The same global `0x141f597a0` gates a branch in the reset_vehicle input function (§4). HYPOTHESIS: a special game mode (replay/spectate/tutorial) installs it.

### 3.6 Readers of +0x320/+0x330 across the whole .text
Scan: `objdump` over all of .text, then every memory operand `[reg+0x320]` / `[reg+0x330]`, excluding `rsp+`/`rbp-`/`rip+` → **731 hits**: 328 stores, 266 loads, 118 lea, 19 indirect calls (vtable slots). The full list is in `dis/cls320.txt`.
- **Float/vector loads (44)**, classified by base:

| Hit(s) | Function | Base traced to | Rig? |
|---|---|---|---|
| 0x14073e29c, 0x14073e2a4 | 0x14073e0c0 (look-ahead save) | rcx = rig (SR) | **Yes**: the only rig reader; it saves and restores, it does not feed the integration |
| 0x14073cd12 | 0x14073be80 | `rax = [rbp-0x28] = r8 arg` (0x14073bf0b); from Commit the r8 is a local stack buffer (`rbp-0x60`, built by 0x140736240) | No (PROBABLE) |
| 0x140744fed..0x14074516e (6) | 0x140744c10 | `rbx = [rcx+0x440]` (0x140744c1f), a separately allocated object | No (PROBABLE) |
| 0x1406c5f15 | 0x1406c5660 | `r14 = r8` arg; the same base has fields to +0x750, byte flag +0x1f0 and vtable calls, which do not match the rig layout | No (PROBABLE) |
| 0x1406f1637 | 0x1406f14f0 | same base does integer `cmp/mov dword [rsi+0x2c0]`, but the rig's +0x2c0 is a float vector | No (PROBABLE) |
| 0x140dd6dbb/eb6/fce, 0x140dfa759/773, 0x140dfab47/b6c, 0x140e0fb72, 0x140e212c0/2f4, 0x140dd72a7..0x140dd9840 | proxy/collision engine region 0x140dd0000–0x140e22000 (home of 0x140df7b50, 0x140df5bb0, 0x140e174a0, 0x140e17ae0) | proxy bodies / world | No (HYPOTHESIS: proxy's own body layout. Several show vec fields at +0x2b0..+0x2e0 and +0x370/+0x374, so the proxy may reuse the same rigid-body base class) |
| remaining 20 (0x14047111c, 0x1404bcef9, 0x1406d525e, 0x1407130b9, 0x1407192a0, 0x140a5648f, 0x140a9dc91/9c, 0x140ac1271, 0x140b1aeee, 0x140b9ec19, 0x140b9faf9, 0x140bb498e, …) | outside the physics module | base not traced individually | UNKNOWN; no rig-signature co-access, and none reached via a container thunk |

- **The rig pointer is encapsulated:** code outside the physics module reaches it only through container thunks `0x140731000–0x140731dff` (`mov rax,[rcx+8]`). Those getters expose +0x2b0 (0x140731564), +0x2d0/+0x2e0 (0x1407318d0), +0x2f0 (0x140731544), +0x300 (0x1407317a4), +0x310 (0x1407313b4), +0x220 (0x1407311a0) and +0x2550 (0x1407311b0). **No getter exposes +0x320 or +0x330** (CONFIRMED for that thunk range).
- **S-relative readers ([S+0x70]/[S+0x80]):** every whole-.text site computing `X+0x2b0` inside the physics module (40 sites) was checked. The S methods `0x140745cf0–0x140746800` only **store** S+0x70/+0x80 (0x140745d21/d29 constructor, 0x1407461b7/bf integrator, 0x140746753/763 setters).
  - The other S receivers (0x14074b3d0 reads S+0..8; 0x140745db0 = AddForceAtPoint) don't read them.
  - 0x14073c50e and 0x14075ee6f are `rbp`-based locals.
  - The 88 `X+0x2b0` sites outside the module were not individually traced (UNKNOWN), with the same encapsulation argument as above.
- **Grade: "+0x320/+0x330 are output-only" stays PROBABLE (strengthened).** It is refuted by a runtime capture (W5) showing a consumer's value change when only +0x320/+0x330 are altered.

---

## 4. Q4: the reset_vehicle handler

### 4.1 From the hash check to what it calls (CONFIRMED)
Function `0x1402a7990` (pdata chain 0x1402a7990–0x1402a7c33) loops over player/vehicle entries (`0x1409713e0(i)`):
```
1402a7a58: cmp [0x141f597a0],0 ; ...          (same global that gates the adjuster setter)
1402a7b00: 0x140c52610(actionmap, id=[veh+0x894])
1402a7b0e: lea rdx,[0x141f58c18] ; 1402a7b18: call 0x140d97470   ; "is action driving.reset_vehicle triggered"
1402a7b40: if r14b: 0x1403d8a60(&ev)  ; ev type 0x32, vtable 0x141271230
1402a7b69:          0x140515800(mgr, &ev, target) -> [mgr]->vt+0x30   (post/dispatch message)
1402a7ba9: if dil:  0x1403d8a90(&ev)  ; ev type 0x53, vtable 0x141271190 ; 0x140515800(...)
```
So the input check **does not call the physics directly**. It posts message objects (types 0x32 and 0x53) to a message manager `[0x141695150]->[+8][idx 0x1416940a0]`. `.rdata` near those vtables contains the strings "driving.reset_vehicle" (0x141271318), "ResetLinesManager" and "lng_reset_press_reset", which suggests a HUD/UI reset flow.

### 4.2 Does it reach Reset / ResetToCommitted / SetFullState?
- **UNKNOWN.** The consumer of message 0x32/0x53 was not found statically: it is a vtable dispatch with no static target.
- All callers of the reset entry points are listed below. The reset_vehicle path must go through one of them, but which one is not determined.
  - rig Reset `0x14074a110`:
    - via thunk 0x140731990, from Container::Reset `0x140db7740` (0x140db7755), from `0x140db95d0` (0x140db960b) and from 0x1409c8110;
    - directly, from SetFullState (0x14074a612) and from `0x14074af20` (0x14074af7f).
  - ResetToCommitted `0x14074a3a0`: via thunk 0x1407319a0 from `0x140db7850` (0x140db7859; only caller 0x140da1834 = the container constructor), and directly from `0x1407493f0` (0x14074940a).
  - SetFullState `0x14074a5e0`: via thunk 0x1407319c0 from `0x140999990` (0x140999a1a), after GetSpawnState (0x1409999d9) and `0x140db95d0` (0x1409999e9).
  - Container::Reset `0x140db7740` callers: 0x1402a5c2d, 0x14043a56f, 0x14045311c, 0x14046c5de (in `0x14046c4f0`, a vehicle (re)setup routine, called from 0x1402a5911), 0x140478055, 0x1404aeda0, 0x14098fe80, 0x140996f61, 0x140998e29.
  - `0x140db95d0` callers: 0x1402a04cb, 0x1402a593c, 0x1404a3d91 (jmp), 0x1409999e9.
  - `0x140999990` ("reset in place": GetSpawnState → 0x140db95d0 → SetFullState) callers: 0x14047f809, 0x140531a38, 0x140569221, 0x14066c8dc, 0x140934cfd, 0x140934f7d, 0x14093516b, 0x1409391ea, 0x140998e11, 0x140ad3a97.
- **HYPOTHESIS H-RV:** the offline "reset vehicle" ends in `0x140999990` (GetSpawnState + SetFullState), or in Container::Reset.
  - Supporting: these are the only routines that combine rig Reset with a spawn state, and each Container::Reset variant sets `con+0x14 = FLT_MAX` and zeroes history.
  - Missing: the message consumer.
  - Refuted by: a runtime log-only detour on these entry points (plan's A1) showing none of them hit when the player presses reset.
- Every reset path writes `con+0x14 = FLT_MAX`, i.e. it **disables** the pull (§2.2). A game reset does not re-sync the rig from the proxy; it pushes rig → proxy through the setters (CONFIRMED for the setters, SR §4).

---

## 5. Updated field table (only rows that changed or are new)

| field | writers | readers | derived from | grade | evidence |
|---|---|---|---|---|---|
| `+0x2b0` S.v | SR list **+** 0x140749b7c (PreTick pull, G-pull) **+** SetLinVel via placement 0x140dbcb38 (G-place) | SR list **+** 0x1407499ca (PostTick push → proxy), 0x14074f79c.. (0x14074f700), getter 0x140731564 | itself (integrator) | CONFIRMED (state variable) | §1.2, §1.3 |
| `+0x2c0` S.w | SR list **+** 0x140749b88 (pull), SetAngVel via placement 0x140dbcb4d | **+** 0x1407499d1 (push) | itself | CONFIRMED | §1.2 |
| `+0x2d0/+0x2e0` S.p/S.q | SR list **+** 0x140749b70 SetPose (pull), 0x14075123a SetPose (adjuster, G-adj), SetTransform via placement 0x140dbcb80 | **+** Commit 0x14074d1c2..d28b, PreTick 0x140749a8b, getter 0x1407318d4/0x1407318de | itself | CONFIRMED | §1.2 |
| `+0x320/+0x330` v_prev/w_prev | integrator 0x1407461b7/bf; setters 0x140746763/753; ctor 0x140745d21/d29; look-ahead restore | **only** 0x14073e29c/2a4 (look-ahead save) in all of .text among rig-based loads; no container getter | copy of v/w before the update | output-only **PROBABLE** | §3.6 |
| `+0x360` inertia | 0x140746730 via PreTick **0x140749bdf every F** (from encoded +0x590..598); 0x140749d41 (setup reload); look-ahead restore | integrator; look-ahead save; pushed to proxy 0x140749bf1 | encoded setup | CONFIRMED (**an external write is overwritten at the next PreTick**) | §1.2 |
| `+0x370/+0x374` mass, 1/mass | 0x140746710 from 0x140749ce5 (setup reload only) | integrator; PreTick push 0x140749b9b | encoded +0x584 | CONFIRMED; "kg" PROBABLE | §3.2 |
| `+0x190..+0x1a8` committed pose (enc) | **Commit 0x14074d1f9..d2a1 every F (EndStep)**; setters (SR) | C push 0x14074f608..; Commit interpolation 0x14074d37a..; ResetToCommitted | encode(S.p,S.q) at EndStep | CONFIRMED | §1.2 |
| `+0x200/+0x210` committed v/w | **Commit 0x14074d2ae/d2b8 every F**; SetLinVel/SetAngVel with flag (0x14074a966) | C push 0x14074f6b5/c5; Commit lerp 0x14074d846.. | S.v/S.w at EndStep | CONFIRMED (explains N2: `+0x200 == +0x2b0` in the frozen dump) | §1.2 |
| `+0x220..+0x2af` render-interpolated state (new) | Commit only (0x14074d4c7.., 0x14074d917..d980) | getter 0x1407311a0 (returns rig+0x220) | lerp(prev(+0x100/+0x170/+0x180), committed, alpha) | structure CONFIRMED; "render interpolation" PROBABLE | §1.2 |
| `+0x3c0/+0x3d0` pull deltas (new) | PreTick: zeroed 0x140749ac5..ad9 every F; set 0x140749b0a..b68 under G-pull | UNKNOWN | new − old v/w from the pull | CONFIRMED (writers) | §1.2 |
| `+0x2550` hook ptr | ctor only (=0) 0x140746c3e | getter 0x1407311b0 → 0x140738cc0 | – | always null **PROBABLE** | §3.4 |
| `+0x2548` freeze, `+0x2560` kinematic | ctor only (=0) | 0x140731904; 0x14073c1b6, 0x1407501fd | – | dead in this build **PROBABLE** | §3.4 |
| `+0x3b8` adjuster ptr | ctor 0x140746cd7; setter 0x140731a34 (only caller 0x1402736c5, gated by [0x141f597a0]) | 0x140749a5c, 0x1407499a5, 0x1407511f8 | – | CONFIRMED (edges); class UNKNOWN | §3.5 |
| `con+0x14` pull timer (new) | += dt 0x140dbccdf every tick; = FLT_MAX by 0x140db77c0, 0x140db7963, 0x140db961f, 0x140db9777 | 0x140db6bd2 (pull gate, < 0.1) | time since event X | CONFIRMED (writers/reader); what sets it small UNKNOWN | §2.2 |
| `[[con+0x840]+0x10]+0x1c0..0x1ff` proxy world matrix | proxy engine (0x140e17ae0 via container vt+0x20) | tick gate 0x140df7b50 (y of +0x1f0); pull 0x140db6bf1 | rig committed pose (C) or setters | CONFIRMED (readers) | §2 |
| `con+0xcc0/+0xcd0` placement | 0x140dbe180 (SR) | C 0x140dbca73..: SetLinVel(0)/SetAngVel(0)/SetTransform every C while active | – | CONFIRMED control flow; semantics HYPOTHESIS | §1.1 |

---

## 6. Hypotheses introduced or changed here

- **H-PULL-OFF** (the pull is off in normal driving): **PROBABLE**.
  - Supporting: every reset writes FLT_MAX; +dt keeps it huge; no small-value writer found in the container range; dump shows FLT_MAX.
  - Contradicting: none.
  - Missing: a whole-.text search for `con+0x14` writers via other addressing; runtime series.
  - Refuted if: any tick shows `con+0x14 < 0.1`.
- **H-SELFHEAL** (after a raw S write at L1, committed/interp/proxy re-sync within one F + one C): **PROBABLE**.
  - Supporting: Commit unconditional at EndStep; PostTick push; C push.
  - Contradicting: collisions against a stale proxy for up to one frame (effect UNKNOWN).
  - Refuted if: the proxy translation differs from the committed p after 2 frames.
- **H-GATE-FLOOR** (the tick gate is a "below world floor − 50 m" guard): **PROBABLE** structure, HYPOTHESIS semantics.
  - Missing: the runtime value of G (`0x1420202d0`).
  - Refuted if: G.y − 50 is above the drivable surface (the car would never tick).
- **H-ORD** (C runs after F in each frame): **HYPOTHESIS**. It rests only on the task names being mine.
  - Refuted if: log-only detours show C before F or interleaved otherwise.
- **H-RV** (the offline reset uses 0x140999990 or Container::Reset): **HYPOTHESIS** (see §4.2).
- **N1 call path** (0x140dbca20 → 0x1407511e0): **REFUTED** (see §0.3). "Commit every tick": CONFIRMED by code via 0x140dbc988.

---

## 7. New and revised experiments for the Validation Specialist
All are read-only unless marked WRITE; WRITE experiments need the plan's offline gate G1. "Tick" = one F call. Detour points are log-only.

1. **E-B1 (order):** log-only detours on `0x1404b1040`, `0x140dbc500`, `0x1404b1100`, `0x140dbca20`, `0x1407511e0` and on Present. Record the sequence and thread id per frame.
   - If H-ORD is true: F…F, C, Present.
   - If false: C before F, or C on another thread → L0/L1 must be redefined.
2. **E-B2 (pull gate):** read `con+0x14` and `[con+0x840]` each tick for 5 min of driving plus one game reset.
   - If H-PULL-OFF is true: always ≥ 0.1, FLT_MAX after the reset.
   - If false: values < 0.1 → identify the window; the S-overwrite risk is real.
3. **E-B3 (WRITE, S.p at L1):** after C returns and before the next F, add +2 m in x to `+0x2d0`. Read S.p, `+0x190` (decoded), `+0x220` (decoded), and the proxy translation `[[con+0x840]+0x10]+0x1f0` at N=0 (before F), after F, and after C.
   - If H-SELFHEAL is true:
     - before F: proxy and committed are old;
     - after F: committed = written+v·dt;
     - after C: proxy = committed.
   - If false: the proxy stays old for more than one C, or S snaps back → look for another sync.
4. **E-B4 (WRITE, S.v at L3 vs L5):** two arms with phase detours (after 0x14074ba92; after 0x14073e3ed).
   - Expected per §1.3: L3 → v lost (== +0x170), p unaffected; L5 → v integrated.
   - False if L3 keeps v or L5 loses it.
5. **E-B5 (WRITE, p/q at L3):** write S.p during L3.
   - Expected (§1.3): it survives (no restore from +0x100).
   - False if p reverts → an undiscovered p restore exists.
6. **E-B6 (WRITE, +0x360):** write 2× inertia at L1, read `+0x360` right after PreTick returns.
   - Expected: equals decode(+0x590..598), i.e. the write is gone.
   - False if it persists → PreTick 0x140749bdf is not reached.
7. **E-B7 (dead-field, WRITE +0x320/+0x330):** write garbage at L1; compare the next 30 ticks of S, per-wheel +0x16d0 and the tyre/HUD outputs against a SHAM arm.
   - If output-only: identical trajectories; +0x320 == v_prev after one tick.
   - If false: divergence → find the consumer.
8. **E-B8 (gate):** log proxy T.y, `[0x1420202d0]`, `[0x14201b788]` and whether 0x14074b8f0 was entered, each tick.
   - If H-GATE-FLOOR is true: the tick is entered iff T.y > G.y − 50.
   - False if ticks are skipped with T.y above the floor, or run below it.
9. **E-B9 (confound poll, G2 extension):** each tick poll `rig+0x3b8`, `rig+0x2550`, `rig+0x2548`, `rig+0x2560`, `con+0x14`, `con+0xcc0` and `|con+0xcd0|`; abort WRITE trials if any is non-default.
   - Expected in normal offline driving: 0, 0, 0, 0, FLT_MAX, 0, any.
10. **E-B10 (Commit interpolation, read-only):** after EndStep, check `+0x290 == +0x170 + (+0x200 − +0x170)·α`, where α is obtained by logging the return of `[rig+0x1198]->vt+0x10` inside Commit.
    - If true: +0x220..+0x2af is render interpolation and does not need restoring (Commit rebuilds it every F).
    - False → the block has other writers.
11. **E-B11 (reset path):** log-only detours on `0x140999990`, `0x140db7740`, `0x140db95d0`, `0x14074a110`, `0x14074a3a0`, `0x14074a5e0`, `0x1407319c0`, plus `0x140515800` filtered to event type 0x32/0x53. Press the game's reset.
    - If H-RV is true: 0x140999990 or 0x140db7740 fires within a few frames after the 0x32 post.
    - False if none fire → another path exists.
12. **Replaces SR E-series timing wording:** the SR's "write between ticks" becomes "write at L1 (after C returns, before the next F)", with E-B1 establishing where L1 is in wall-clock/thread terms.

---

## 8. Blockers and what is still not done
- **CopyToBox of the exe is still refused** (outside the allowed local-exec root). Analysis continued with read-only `objdump`/Python on the user's machine. Large outputs were auto-saved under `/home/deivison/agent-tools/`; I copied them to the box in `/workspace/analyst/dis/`.
- **Not traced:**
  - the consumer of message types 0x32/0x53, so the reset_vehicle final handler is UNKNOWN;
  - the adjuster class at `[mgr+0x338]`;
  - the runtime values of `G` (0x1420202d0) and `[0x14201b788]`;
  - the full bodies of `0x140db2220` (only calls and offsets grepped; no S-writing call seen) and `0x140dbe300`;
  - `0x14073e570` (still UNKNOWN for persistent writes);
  - individual bases of 20 non-physics `+0x320/+0x330` float loads and 88 non-physics `X+0x2b0` sites (encapsulation argument only);
  - writers of `con+0x14` outside `0x140da0000–0x140dc0000`.
- Some supporting data came from the Memory Cartographer's captures (`/workspace/captures/INV-01/`: container dump `con+0 = 0x141400c30`, `con+8 = rig`, `rig+0 = container`, `con+0x14 = FLT_MAX`; .text diff). I used it only as corroboration. Every structural claim above rests on my own disassembly.
