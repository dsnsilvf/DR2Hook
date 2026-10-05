# Vehicle editor and custom cars

> **Speculation.** Not started. Nobody has checked whether the game can load a car it does not ship with.

## The idea

Install custom cars, for example a Beetle, if someone builds one. This is mainly about installing and loading cars made by the community, and it may grow into an editor later.

## Why it might be possible

| What we know | Grade | Source |
| --- | --- | --- |
| The physics rig, wheels, suspension, tyres, engine, and gearbox are mapped, so the pieces a car is made of are partly understood. | `CONFIRMED` for the mapped fields, one car and one build | [reverse engineering index](../reverse_engineering/README.md) |
| The vehicle setup is documented. | Graded inside the note | [Vehicle Setup](../reverse_engineering/vehicle_setup.md) |
| `egodata` reads the game's archives and binary XML. | `CONFIRMED` that the tool exists | [`tools/egodata`](../../tools/egodata/) |

## What is not known

- The formats for the car model, the physics data, the setup, and the audio.
- How the game registers a car and puts it in the garage and in events.
- Whether the game would load an extra car without breaking other content.
- How to share a custom car without redistributing the game's own assets.
- Whether many parts depend on code that cannot be changed from a loader.

## First safe experiment

Use `egodata` (read-only) to find where one existing car's data lives, and write down the files, the formats, and how they refer to each other. No game files change.

## Out of scope

Redistributing the game's original assets.
