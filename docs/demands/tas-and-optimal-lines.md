# TAS, replay and optimal lines

> **Speculation.** Not started. Nobody has checked whether the physics is deterministic, which is the condition for everything below.

## The idea

A tool-assisted run (TAS) for DiRT Rally 2.0: a file of per-tick inputs that the game replays, with rewind and retry, so the community can look for the theoretically best line on a stage. Related ideas that share the same base are listed at the end.

This is for **offline research and reference lines only**. It never touches RaceNet or official leaderboards (see the ground rules in the [README](README.md)).

## Why it might be possible

| What we know | Grade | Source |
| --- | --- | --- |
| Opt-in hooks on the physics tick path can queue writes on real tick boundaries. This is the point where a TAS would inject input. | `CONFIRMED` that the harness exists; not tested for input | [Physics tick harness](../physics_tick_harness.md) |
| The Practice Mode mod saves and restores a checkpoint. This is the base for retry and rewind. | `CONFIRMED` that it exists; restore is not bit-exact (velocity at `+0x320` has a known limitation) | [INV-01 handoff](../reverse_engineering/investigations/INV-01/HANDOFF.md) |
| `dr2rec` records sessions offline and can compare them. | `CONFIRMED` that the tool exists | [BLACKBOX](../BLACKBOX.md) |

## What is not known

- **Determinism.** Whether the same inputs on the same tick always give the same state. Fixed timestep, random sources, weather, surface and damage all matter. `UNKNOWN`.
- **Input path.** Where the game reads steering, throttle, brake, handbrake and gear, and whether the values can be replaced on a tick boundary instead of writing rig state directly. `UNKNOWN`.
- **Exact restore.** Whether a savestate can bring back the whole physics state so a retry starts identical. Today it probably cannot. `PROBABLE`.
- **Tick rate and ordering.** How the physics tick relates to the rendered frame and to input sampling. Partly mapped in INV-01.
- **Ghosts and timing.** How the game's own stage timer and ghost data could be fed from a replay. `UNKNOWN`.

## First safe experiment

Determinism test, read-only on the game:

1. With `dr2rec` and the physics harness CSV, record a short stretch of a stage with real input.
2. Replay the same input from the same start, then compare the two CSVs tick by tick.
3. If the states match, determinism is `PROBABLE` and the rest is engineering. If they drift, find the source of the drift; if it cannot be removed, the idea is marked not feasible here.

Only after that: find the input read path (observation first), then try replacing it.

## Related ideas on the same base

- **Replay file format.** A per-tick input file, versioned and tied to game build, car, stage and setup, so runs can be shared and checked.
- **Rewind and retry.** Save, try a section, go back. Needs exact restore.
- **Automatic line search.** An optimizer that tries input sequences per section (for example by sampling or hill climbing from a savestate) to find a fast line. Needs fast, deterministic retries; possibly faster than real time.
- **Reference lines and ghosts.** Export the best found line as a ghost or as a path overlay to study corners, braking points and gear choice.
- **Stage and setup analysis.** Use many retries to compare setups or show how much time a corner gives or loses.
- **Input assist for humans.** Slow motion, frame advance, and input display for practice, without recorded replays.

## Out of scope

- Submitting TAS or assisted runs to RaceNet or any official leaderboard.
- Anything that works online or against other players.
