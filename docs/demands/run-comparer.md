# Run comparer and in-game HTML

> **Speculation.** Not started. Nobody has loaded two real runs and checked that they can be aligned.

## The idea

Pick two runs of the **same stage** and see where time is gained or lost. Any pairing works: you vs you, you vs another player, player A vs player B. The output is a report or a HUD with a cumulative time delta, overlaid traces and a verdict per corner.

Two sub-ideas live here:

1. **Run comparer.** Offline first, from recorded sessions.
2. **HTML or similar rendered inside the game.** Only a question for now: can the game show a rich page, or must the HUD be drawn another way?

Offline and research only. No RaceNet and no official leaderboards (see the ground rules in the [README](README.md)).

## Why it might be possible

| What we know | Grade | Source |
| --- | --- | --- |
| `dr2rec` records sessions with a monotonic clock, raw rig bytes and the raw UDP datagram per sample. | `CONFIRMED` that the tool exists | [BLACKBOX](../BLACKBOX.md) |
| The game emits UDP telemetry at the end of each simulation tick. | `CONFIRMED` | [Telemetry](../reverse_engineering/telemetry.md) |
| Suspension travel, gear and engine value have known packet offsets. | `CONFIRMED` for offsets `68`–`80`, `132`, `148` | [Telemetry](../reverse_engineering/telemetry.md) |
| `total_time`, `lap_time`, `lap_distance`, speed, position and G forces are said to be in the packet. | `PROBABLE`: cited, but **no offsets are documented here** | [Telemetry](../reverse_engineering/telemetry.md) |
| `dr2rec compare` already compares two sessions, but by memory (constant vs dynamic fields), not by performance. | `CONFIRMED`; a different job | [BLACKBOX](../BLACKBOX.md) |
| The overlay is Dear ImGui and already runs inside the game. | `CONFIRMED` | [README](../../README.md) |

## What is not known

- **Offsets of the fields the comparer needs** (lap distance, time, position, speed, throttle, brake, steering). `UNKNOWN` until confirmed against a real capture.
- **Telemetry must be on.** The process only emits UDP after reading `hardware_settings_config.xml` with `udp enabled="true"` and `extradata="3"`, and an open session does not re-read it. Other players' runs would need to be recorded the same way.
- **Resolution and quality of lap distance.** Coarse values or jumps would make the alignment noisy. `UNKNOWN`.
- **A shared run format.** Whether to reuse `.dr2cap` or extract a smaller normalized run. Open.
- **Bad runs.** Crashes, rollovers, resets and different starts break the alignment. Open.
- **Same stage check.** The tool must refuse or warn on different stages or cars. How to identify the stage is `UNKNOWN`.
- **Other players' runs.** How runs get shared, and whether a run from someone else is trustworthy. Open.

## Method (planned)

- **Align by distance, not time.** At each distance along the stage, compare time and speed of the two runs. The cumulative time delta is the main chart.
- **Overlay traces** of speed, throttle, brake, steering and gear against distance.
- **Split into sections** (braking, apex, exit) and give a short verdict per section, for example "braked earlier, exited slower".
- **Track map** coloured by gain or loss, if position is available.

## First safe experiment

Read-only, no game writes:

1. Record two real runs of the same stage with `dr2rec record --udp ...`, telemetry enabled.
2. Confirm in the captured packets which offsets carry lap distance, time, speed and driver inputs.
3. Check the quality of lap distance (resolution, monotonic, jumps).
4. Align the two runs by distance and plot the delta.

If distance is unusable, fall back to position (x, y, z) projected along a reference path, or mark the idea as not feasible.

## HTML inside the game: what the repo says so far

- The game UI is **not Scaleform or loose XML**. The frontend screens are three binary files inside `game/game_1.dat`, with a native menu already patched for the DR2 Hook pause entry ([UI data](../reverse_engineering/ui_data.md), [Menu](../reverse_engineering/menu.md)).
- Nothing in the notes shows the game rendering HTML. `HYPOTHESIS`: it does not.
- The overlay in the loader is ImGui, so a HUD can be drawn without HTML: lines, bars and text.
- Rendering real HTML would mean embedding a browser engine in the overlay or showing a window next to the game. This is **not** analyzed, not tested, and may be heavy or fragile on Linux with Proton. `UNKNOWN`.

Order of preference, to be confirmed: ImGui HUD, then an external viewer that opens the HTML report beside the game, then an embedded engine only if the first two are not enough.

## Out of scope

- Submitting runs or comparisons to RaceNet or any official leaderboard.
- Collecting or distributing other players' runs without their consent.
