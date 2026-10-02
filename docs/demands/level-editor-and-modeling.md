# Level editor and modeling

> **Speculation, and not yet scoped.** The maintainer has not decided how to treat this one. It is recorded here so the idea is not lost. Nothing is started, and nobody has checked whether it is possible.

## The idea

Two related wishes, kept together until it is clear whether they are separate:

- **Level editor.** Build or change a stage's content: placement of objects, scenery, and layout.
- **Modeling.** Make 3D models (for scenery, props, or cars) that could be used in the game.

## How it relates to the other demands

- It overlaps with the [map editor](map-editor.md). "Level editor" may turn out to be the same work, or the part of it that deals with objects and scenery rather than the route.
- It overlaps with the [vehicle editor](vehicle-editor.md) when the models are cars.
- Both depend on the same unknown: the formats the game uses for models and levels.

## Open questions about scope

These need an answer before this becomes a real plan:

- Is this a tool inside the game (through the loader), an outside tool that writes game-ready files, or both?
- Does "modeling" mean an exporter from a common 3D program to the game's format, or only reading the game's models?
- Is the goal new content, or editing the existing stages and cars?
- Should it be merged into the map editor and vehicle editor documents once the formats are known?

## What we know

| What we know | Grade | Source |
| --- | --- | --- |
| `egodata` lists and extracts the game's archives and converts binary XML to text. | `CONFIRMED` that the tool exists | [`tools/egodata`](../../tools/egodata/) |
| Nothing is known yet about the game's model, texture, or level formats. | `UNKNOWN` | none |

## First safe experiment

Use `egodata` (read-only) to list the archives and sort the files by type: models, textures, level data, and anything else. Write down the file kinds and how they point to each other. This is also the first step of the map editor and vehicle editor, so one survey can serve all three. No game files change.

## Out of scope

Redistributing the game's original assets.
