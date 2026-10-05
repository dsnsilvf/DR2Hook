# Map editor

> **Partly started (2026-10-05).** The files are now read and the objects can be edited offline, but **nobody has checked whether the game accepts changed stage data.** The rest of this page is the original plan.

## What exists now

The **Track Explorer** ([docs/UIVIEW.md](../UIVIEW.md)) exports a stage from `locations/*.nefs` and shows terrain, objects, trees, ornaments, track limits, and the AI line. It can move, rotate, delete, and duplicate objects and write the result into a **new** `.nefs`. The formats are in [track_formats.md](../reverse_engineering/track_formats.md). Known gaps: stage collision (`track.jpk`) is not decoded and does not change; the edited package has not been tried in the game; creating new routes or editing terrain is not started.

## The idea

Create or change stages: move objects, change a route, or build a new one.

## Why it might be possible

| What we know | Grade | Source |
| --- | --- | --- |
| The `egodata` tool already lists and extracts the game's archives (NEFS) and converts its binary XML (`.bin`) to text. | `CONFIRMED` that the tool exists and reads those formats | [`tools/egodata`](../../tools/egodata/) |
| The native pause-menu screen works by changing the game's UI data at boot, and the game accepted it. | `CONFIRMED` for UI data | [UI Data](../reverse_engineering/ui_data.md) |

That shows the game can load changed data in at least one area. It does not say anything about stage or track data.

## What is not known

- Where stage geometry, surfaces, objects, and routes are stored, and in which formats.
- Whether those formats can be read, written back, and loaded by the game.
- Whether the game checks the integrity of stage files.
- How a changed stage would be shipped to other players without redistributing the game's own files.

## First safe experiment

Use `egodata` (read-only) to list the archives and look for stage-related files and their formats. Write down what exists. No game files change.

## Out of scope

Redistributing the game's original assets.
