# INV-01 Phase 1a: passive external reads R1-R6 (READ-ONLY)

Capture: 2026-09-30 **12:07:28-12:08:13 BRT** (45.0 s), machine cachyos-x8664, PID 4113076.
Method: one approved script. `/proc/4113076/mem` was opened O_RDONLY and read with `pread` only. There was no ptrace attach, no writes, no hooks and no signals. G3 and all writes remain on hold.
Sampler: `/workspace/validation/phase1a/p1a_sampler.sh`.

Per poll it read these windows:
- rig+0x80..0x400, rig+0x1320..0x1340, rig+0x2500..0x2570
- con+0xc80..0xd40, car+0x280..0x340
- pxb+0x180..0x300, where pxb = `[[con+0x840]+8]` = 0x6b6f04e0
- the wheel block rig+0x1480..0x2500

A record was written whenever the core windows changed. `.data 0x1415a9ce4` was read once per second. The 0x1000-byte proxy objects `[con+0x840]` = 0x6b6f09c0 and pxb were dumped at start, at about 22 s, and at the end.

Data and outputs are in `/workspace/validation/phase1a/`:
- `inv01_p1a/` (raw `samples.bin`, `meta.json`, `proxy_*.bin`, `pxb_*.bin`)
- `settled_series.csv` (one row per tick)
- `lag_equality.csv`
- `analysis_out.txt`, `followups_out.txt`, `order_out.txt`
- the scripts `load.py`, `analyze_p1a.py`, `order.py`

A temporary copy of the archive was placed at `/home/deivison/inv01_tmp/inv01_p1a.tgz` and `/tmp/inv01_p1a*` on the user machine for the transfer. Nothing else was written there.

## Session checks
- Live: the timer at +0x1338 and the ring index at +0xdc took 16 distinct values in 200 ms at start. Over the run there were 2700 ring advances.
- G2 at start and at end:
  - `[rig+0x12c0]`==rig, `+0x12d0`=4, con+0=0x141400c30, con+8==rig, rig+0==con
  - +0x2550=0, +0x3b8=0, +0x2548=0, +0x2560=0, con+0xcc0=0
  - `confound_flags = 0x00` (clean)
- Rig chain unchanged: car 0x4b254b80, con 0x4b28f100, rig 0x4b29bab0.
- Driving profile from +0x2508: about 10 m/s, braking to rest from about 5 s to 20 s, then 6-10 m/s, then about 2 m/s at the end. This gives 1750 ticks with the look-ahead active and 948 without, including 682 slow creep ticks.
- Poll rate: 158k polls/s (6.3 µs). About 2600 polls and about 20 change-records per tick.
- One vehicle, one location, one session. The G1 grade stays PROBABLE-offline.

## Tick segmentation
The ring index +0xdc advances exactly once per tick. It moves 0→1→…→9→0 with step 1 in all 2700 cases, and it changes in the same record as the real integrate. A tick's "settled" sample is the last record before the next tick's timer increment. In 2699/2699 settled samples, +0x200 == v, so the settled sample is always taken after Commit.

Intra-tick order was observed by polling, in 2698/2698 ticks:
1. timer +0x1338 += 2·dt
2. +0x170 ← v (tick-start copy)
3. only when gated: look-ahead. v and p change, +0x320 ← v, then all three are restored and +0x1334 rises by about 3
4. real integrate: ring index +1, v and p new, +0x320 ← v_old
5. Commit: +0x200 ← v_new
6. proxy p updated

Timing (median, with 5-95% range):
- tick start → integrate: 0.35 ms (0.21-0.96 ms)
- integrate → Commit: 0.23 ms (0.10-0.80 ms)
- integrate → proxy p: 1.3 ms

The physics burst takes under about 1.5 ms of each 16.7 ms tick, so the time between ticks is roughly 90% of wall time. This matters for W8.

---

## R1: tick cadence, dt, sub-iterations
| Item | Expected | Actual |
|---|---|---|
| const `0x1415a9ce4` | 1000.0 | `0x447a0000` = **1000.0** in 45/45 reads |
| dt | stable, measurable | **dt = 1/60 s**: median of dp/v_new is 0.0166665 (5-95%: 0.016632-0.016702). Using v_old gives a worse fit (0.016648), which fits semi-implicit Euler (p += dt·v_new). |
| tick rate | fixed or variable step | **60.00 ticks/s** (2700 in 45.0 s). Tick-to-tick wall intervals jitter from 11 to 24 ms (1-99%) but average 16.67 ms. This is a fixed-step accumulator driven from frames. |
| timer delta == dt | yes | **No: +0x1338 (and +0x133c) advance 0.03333 = 2·dt per tick** in 2698/2698 ticks. In about 5% of ticks the increase arrives as two separate 1/60 steps about 5 ms apart. |
| sub-iterations n = max(1, int(dt·1000)) | inferred | n = 16 is **inferred only**; it was not observed. The counter at +0x1334 rises by 0-6 per tick (3 is the most common). Its meaning is unknown. |
| pause control | 0 bursts while paused | From the gate run at 12:04:46-12:05:04: 0 ticks in 18 s while frozen/paused (state bit-identical to 11:38) |

- Interpretation: the fixed 60 Hz step is confirmed by two independent measures: ring-index cadence and kinematics.
- **Refuted prediction:** "the `rig+0x1338` timer delta equals dt". The timer advances 2·dt per tick, in two increments of dt. It is therefore not a pure physics clock, or it is advanced twice per tick. Any async phase bracket that uses +0x1338 must count half-steps.
- Grade: **PROBABLE** (polling). The frame-rate-cap arm was not run, so whether the tick rate depends on frame rate is still open.
- Remaining uncertainty: n, and what +0x1334 counts.

## R2: settled-state lag relations (2698 consecutive settled ticks; v changed in all of them)
| Relation | Expected | Actual (bit-exact) |
|---|---|---|
| +0x320[k] == v[k-1] | lag 1 | **100.000%** (lag 0: 0%) |
| +0x330[k] == ω[k-1] | lag 1 | **100.000%** (lag 0: 0%) |
| +0x170[k] == v[k-1] | lag 1 | **100.000%** |
| +0x180[k] == ω[k-1] | lag 1 | **100.000%** |
| +0x200[k] == v[k] | lag 0 (N1 Commit) | **100.000%** (lag 1: 0%) |
| +0x210[k] == ω[k] | lag 0 | **100.000%** |
| control: +0x320 vs p lag 0/1 | 0 | 0% / 0% |
| control: shuffled v | ~0 | 0.037% (1 hit) |

- Interpretation: every predicted copy and lag holds with no exceptions. The Commit copy (+0x200/+0x210 ← v/ω) runs **every tick**, after the integrate. This backs N1 and contradicts the SR §2.5 chain, which leaves Commit out of the per-tick path.
- The intra-tick sequence above also shows the look-ahead save/restore (+0x320 is restored) and the order +0x170 copy → integrate.
- Grade: **PROBABLE** (correlation only). Copy direction and actual consumption are not established; that is for W1/W5.
- Refuted: nothing.

## R3: F/τ accumulators between ticks
- Expected: +0x340..+0x35c == 0 at every settled sample, and non-zero transients during the tick.
- Actual: **0 non-zero values in 2699/2699 settled samples** for both F and τ.
- Control: transients are present in 29,835 records (F) and 19,813 records (τ). For example F = (-24.9, **-10623.9**, -11.0), which is m·g = 1083.53 × 9.805. So the sampler does see F when it exists.
- The settled samples cover rest, creep, braking and about 10 m/s. There was no deliberate curb strike or airborne arm.
- Grade: **PROBABLE**. It becomes CONFIRMED at B2/B1 in I1.
- Residual: a force added between B1 and the Commit loop would still look "settled".

## R4: mass and inverse
- +0x370 = **1083.53** (constant across all 52,101 records). +0x374 = 9.2291e-4 (constant).
- In float32, `1/m == inv` exactly; the float64 product is 1.0000000.
- Grade: **CONFIRMED** (reciprocal and constancy) for this vehicle and this session. "Mass in kg" stays PROBABLE.
- Not done: the second-vehicle control and a damage event.

## R5: look-ahead gate and per-wheel cone outputs
(a) Expected +0x2508 == float32 |v|.
- Actual: |+0x2508| == f32|v| in **100%** of samples.
- But +0x2508 is **signed**: it is negative in 85/2699 settled ticks (slow reverse/creep, e.g. -0.0229 vs |v| 0.0229).
- **car+0x310 == f32|v| in 100%** (this is the unsigned copy).
- Minor **refutation** of the literal claim "+0x2508 = abs(v)". The gate threshold compares abs(+0x2508), so the gate logic is unaffected.

Look-ahead vs gate:
- The look-ahead signature (p moves, then is restored before the integrate) was seen in 1750 ticks. All 1750 had |+0x2508| > 0.89408 on the previous settled tick.
- **0 look-aheads with the gate off.**
- 25 gate-on ticks showed no look-ahead. These are near the threshold, or polling missed the brief excursion.

(b) Expected: per-wheel +0x1690 (rig+0x1690+i·0x420) changes **only** in look-ahead ticks.
- Look-ahead ticks: all 4 wheels changed in 1750/1750.
- Ticks without a look-ahead but where p changed (682, median |v| 0.16 m/s): **+0x1690 changed in 125, 134, 128 and 126** of them (wheels 0-3). +0x1560 behaves the same (126-135).
- Example: tick 844 at 0.108 m/s. +0x1690[w0] went from (0.072074, 0.997094, 0.024678) to (0.072030, 0.997096, 0.024707). The write was 1 record after tick start and **before** the integrate. Only 27 of the 125 are next to a look-ahead tick.

**The R5 hypothesis H-025 is REFUTED as stated**: "+0x1690 changes only when |+0x2508| > 0.89408 / only the look-ahead cone writes it". A second writer of +0x1560/+0x1690 runs early in some gate-off ticks, before the integrate, possibly the per-tick recompute from pose that SR line 305 mentions. The SR puzzle explanation (values freeze at rest) is weakened.

- Grade: the refutation is observational. It was seen 125+ times in one session, and the controls are fine (G2 clean, look-ahead detection agrees with the gate in 1750/1750). By the plan's rules, the formal REFUTED grade needs 3 or more phase-verified trials (I1/I5), so this is **PROBABLE-refuted**.
- Control: wheel+0x50 (rig+0x1480+i·0x420+0x50). Wheels 0-1 change only when p changes (86/86). Wheels 2-3 (front) change in about 760 gate-off ticks, including about 100 where p did not change. This is consistent with steering input at rest, not a pose-only derivation.
- Residual: which writer this is (`0x140748670` / `0x1407306b0`?) is a static task. I1 or I5 should count `0x14073a070` calls against gate-off ticks.

## R6: secondary copies
| Field | Expected (hypothesis) | Actual | Grade |
|---|---|---|---|
| car+0x2b0 / car+0x2c0 | lag-0 or lag-1 copy of v/ω | **== v[k] / ω[k], 100.000% bit-exact, lag 0** (lag 1: 0%) | PROBABLE (copy; reader unknown) |
| car+0x2e0 | p + R·c | Body-frame offset c = (-0.00001, **-0.3364, -0.3731**) m using rows R=(+0x2f0,+0x300,+0x310)·d, lag 0. sd = (1.9e-4, 1.0e-4, 2.5e-4) m, which is at the float32 resolution for \|p\| ≈ 4300 m (ulp 4.9e-4). Lag 1 and R^T are much worse. | PROBABLE. The plan's sd < 1e-4 m threshold is not met on x and z, which is explained by f32 quantisation. |
| car+0x2f0 | q at a different moment (interpolation?) | A unit quaternion, **never equal** to rig q at lag 0 or 1 (0.037%). The angle to rig q has median 0.046°, 99% at 0.36°, max 0.54°. car+0x300 = (1,1,1,0), which looks like scale. So car+0x2e0/+0x2f0/+0x300 look like a render transform (pos/quat/scale). | HYPOTHESIS (the slerp fit was not done) |
| car+0x310 | == rig+0x2508 | **== f32\|v\| in 100%**; it differs from +0x2508 only by sign (85 ticks) | PROBABLE |
| rig+0x290 / +0x2a0 | some average of v/ω | Not equal to v[k], v[k-1], (v[k]+v[k-1])/2, or the look-ahead v. An exponential moving average `r += a·(v_k - r)` fits with **a = 0.835**, rms 2.8e-3 (max 0.03), so it is approximate, not bit-exact. +0x2a0 is likewise not a copy of ω. | HYPOTHESIS (filtered velocity) |
| con+0xcd0 | does not follow p (H-024) | **Constant for the whole 45 s** at (-4578.00, 124.47, 39.78), while p changed in 2432 ticks. It is 313-499 m from the car. con+0xcc0 stayed 0 throughout. | PROBABLE. **H-011 ("visual anchor that tracks the car") is refuted for the placement-inactive state; H-024 is supported.** Whether anything reads it needs W9. |

## Proxy `[con+0x840]` = 0x6b6f09c0, and `[[con+0x840]+8]` = pxb 0x6b6f04e0
Snapshots at start, mid and end all differ, so the proxy is live. In the per-tick pxb window, bit-exact over 2698 settled ticks:
- pxb+0x1b0 and pxb+0x1f0 == rig p, **lag 0**
- pxb+0x230 and pxb+0x270 == rig p, **lag 1** (the previous position)
- pxb+0x2e0 and pxb+0x2f0 == v, lag 0
- pxb+0x2c0 and pxb+0x2d0 == ω, lag 0

The proxy p updates about 1.3 ms after the integrate, **after** Commit, in 2407 of the resolvable ticks.

Interpretation: the proxy is re-synced from the rig **every tick** in steady driving. This backs I4 case (a), "self-heals", and puts pressure on H-023's "setters are the only sync path". It remains open whether that per-tick sync is itself a setter or forwarding call inside Commit (I3). So this is PROBABLE (correlation), and it is an early I4 result, not a Phase-1a deliverable. It still needs a raw-write test (W11) to show that a raw restore is re-synced.

## Planned predictions refuted (all observational, one session)
1. **R1:** "+0x1338 timer delta == dt". It is **2·dt per tick** (0.0333 s with dt = 1/60).
2. **R5(b) / H-025:** "+0x1690 (and +0x1560) change only when the look-ahead gate is on". They changed in about 19% of gate-off moving ticks, written before the integrate. PROBABLE-refuted pending I1/I5.
3. **R5(a), literal:** "+0x2508 = abs(v)". It is **signed** ±|v|; the unsigned copy is car+0x310.
4. **R6 / H-011:** con+0xcd0 as a car-tracking visual anchor. It did not move in 45 s of driving (placement inactive).

Everything else held: 1000.0 constant, dt fixed, all R2 lags bit-exact, settled F/τ = 0, mass reciprocal, car v/ω lag-0 copies, car+0x2e0 body offset.

## Remaining uncertainty and next steps
- Single vehicle, location and session. No frame-cap arm, no curb or airborne arm, no damage event, no second vehicle.
- Polling cannot see sub-iterations. The meaning of +0x1334 is unknown.
- The +0x1560/+0x1690 second writer: a static trace is needed (SR line 305: `0x140748670`, `0x1407306b0`).
- The car+0x2f0 slerp/interpolation fit and a characterisation of +0x2a0 were not done.
- The Practice Mode mod (F5/F6/F7) was loaded. No evidence of its writes was seen (no discontinuities in v or p were checked explicitly). The operator should confirm they did not press F5-F7 between 12:07:28 and 12:08:13.
