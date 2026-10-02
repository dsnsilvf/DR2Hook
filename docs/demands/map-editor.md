# Map editor

> **Speculation.** Not started. Nobody has checked whether the game accepts changed stage data.

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
