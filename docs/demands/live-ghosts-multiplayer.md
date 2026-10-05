# Live ghosts: a multiplayer idea

> **Speculation.** Nobody has checked yet whether this can be done. This document explains why it looks possible and what is still unknown. It is not a plan the project has committed to.

## The idea

Racing games show a **ghost** of your earlier run so you can compare with it. The idea is to make that ghost come from somewhere else: instead of a recorded run, it is another player's car, updated live over the network. At first it would be a ghost only (you can see it, you cannot touch it). A later step could make the clone solid when it gets close, which removes the "ghost" effect.

Each player runs the stage on their own machine. Each machine sends its own car state and draws everyone else's cars as ghosts. This is the same shape as "racing against ghosts", with the ghost data arriving live.

## Why it might be possible

Each point below links to the note it comes from and is graded by what that note says.

| # | What we know | Why it helps | Grade | Source |
| --- | --- | --- | --- | --- |
| 1 | The car's rigid-body state is a small block on the rig: velocity `+0x2b0`, angular velocity `+0x2c0`, position `+0x2d0`, rotation quaternion `+0x2e0`. | That is nearly everything needed to place and move a car, and it is small enough to send many times per second. | `CONFIRMED` (one car, one location, one session) | [INV-01 F-012](../reverse_engineering/investigations/INV-01/kb/confirmed-facts.md) |
| 2 | The physics runs on a fixed 60 Hz tick, and the ring index at `+0xdc` advances once per tick. | A fixed tick gives a natural rhythm for sending and receiving state, and for smoothing between packets. | `CONFIRMED` (same scope) | [INV-01 F-014](../reverse_engineering/investigations/INV-01/kb/confirmed-facts.md) |
| 3 | The game has a native state API: `Reset`, `SetTransform`, `SetLinVel`, `SetAngVel`, `Commit`, `GetSpawnState`, `SetFullState`. | If a second car exposes the same API, moving it from outside may be possible. | `PROBABLE` (meaning of the calls is not proven) | [INV-01 HANDOFF](../reverse_engineering/investigations/INV-01/HANDOFF.md), [physics harness](../architecture/physics_tick_harness.md) |
| 4 | `DynamicsCar` is an abstract interface that other subsystems use, and the notes name replay among them. | A replay or a ghost likely reads car state through a shared interface, which would be a place to feed a different source. | `HYPOTHESIS` (the notes list replay as a user of the interface but have no offsets for it) | [PhysicsRig](../reverse_engineering/physics_rig.md) |
| 5 | The game already has a ghost feature in the pause menu (`ghost_select`). | A ghost exists as a concept the game can draw and move. | `CONFIRMED` that the menu entry exists; how the ghost works is `UNKNOWN` | [Menu](../reverse_engineering/menu.md) |
| 6 | The INV-01 plan expects other cars (AI or ghost) to call the same physics code, and tells the tester to filter by rig pointer. | It suggests several cars already share one physics path, one rig each. | `HYPOTHESIS` (written as a risk in the plan, not measured) | [INV-01 experiment plan](../reverse_engineering/investigations/INV-01/validation/INV-01-experiment-plan.md) |
| 7 | The loader can read car state every frame and has a tick-level harness that can run code on physics boundaries. | The tooling to read and later write car state already exists. | `CONFIRMED` that the code exists; writes are not validated in-game | [physics tick harness](../architecture/physics_tick_harness.md) |
| 8 | `dr2rec` records a session from outside the game, read-only. | The first experiment (watch what a ghost changes in memory) needs no write to the game. | `CONFIRMED` | [BLACKBOX](../tools/dr2rec.md) |
| 9 | The game's own telemetry leaves through `sendto`, which `NetworkGuard` does not intercept, and loopback traffic is allowed. | A local bridge process on `127.0.0.1` can run without changing the network rules. | `CONFIRMED` | [Telemetry](../reverse_engineering/telemetry.md), [Network Guard](../reverse_engineering/network_guard.md) |

## What is not known

- **How the game creates and moves a ghost.** Not investigated. This is the main unknown. If the ghost reads its pose from a recorded stream each tick, replacing that stream with network data may be easy. If it is a special object with fixed behaviour, it may be hard or impossible.
- **Whether a second car can be added to a stage** (or an existing ghost reused) from the loader.
- **Whether writes survive.** INV-01 has not yet validated that a write to the state block survives to the next tick. See the open items in the [INV-01 handoff](../reverse_engineering/investigations/INV-01/HANDOFF.md).
- **Collision.** A solid clone means two cars hitting each other. The physics may assume it fully controls every car it simulates. This is the hardest part and may not be possible.
- **Other game builds, cars, and stages.** All current findings are for one build and one scenario.
- **Replay and multi-car modes.** The INV-01 notes say the tick order may differ there and was not tested.

## Why the network layer needs a decision

`NetworkGuard` refuses non-local traffic on purpose, so no tool built on DR2Hook can reach RaceNet. A multiplayer feature would need a **narrow, explicit exception** for a ghost server, and it must never reach RaceNet or official leaderboards. That changes the [Fair play](../../README.md#fair-play) rule, so it needs a clear decision from the maintainer before any code is written. The early steps below use only localhost.

## Staged plan (each step can end the idea)

1. **Observe.** Record a session with `dr2rec` while the game shows a ghost, and note which memory changes. Read-only, no risk to the game.
2. **Locate.** Find the ghost's car object and where its pose comes from each tick.
3. **Home-made ghost.** Make a second car follow a file recorded by the player. If this works, it is already a useful feature on its own (for example, racing your best run).
4. **Live on loopback.** Replace the file with UDP on `127.0.0.1`, between two programs on one machine.
5. **Two machines.** Only after the decision on the network exception. Smooth the received positions; exact determinism is not needed for ghosts.
6. **Solid cars.** Study collision last, only if everything above works.

Stop rule: if step 2 shows the ghost cannot be driven from outside, mark this document `REFUTED` and keep the findings.

## Out of scope

Anything involving RaceNet, official leaderboards, or online competition. Cheating tools. Hiding that a session has outside cars in it.
