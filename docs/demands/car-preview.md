# Quick car preview

> **Speculation.** Not started. Nothing here has been run in the game.

## The idea

Open the game fast, only to look at one car, or render one car without the rest of the game. Useful for checking a car (or a future custom car, see [vehicle-editor.md](vehicle-editor.md)) without waiting for menus and a stage to load.

## Three possible routes

| Route | What it is | Cost | Fidelity |
| --- | --- | --- | --- |
| A. Jump to the garage screen | The hook skips the boot flow and opens the game's own car-selection screen, on a chosen car and livery. | Low, if the screen and its state can be reached from `states.bin` / `flow.bin`. | Exact, it is the game's own renderer. |
| B. Empty stage, free camera | Load the shortest stage, put the player car in it, pause physics, orbit the camera from an overlay. | Medium. Needs the stage-load path and a camera write. | Exact, but the stage still loads. |
| C. Standalone viewer | Extract the car model with `egodata` and render it in our own viewer. | High. Needs the model, texture and material formats. | Approximate. |

Recommended order: A, then B. C only if a viewer independent of the game is wanted.

## Why A and B might be possible

| What we know | Grade | Source |
| --- | --- | --- |
| The UI flow is data: `screens.bin`, `states.bin`, `flow.bin`, `links.bin`, patched at boot by the loader. | `CONFIRMED` | [ui_data.md](../reverse_engineering/ui_data.md) |
| The loader can add a state and a screen, and open a native screen from the pause menu. | `CONFIRMED` | [ui_data.md](../reverse_engineering/ui_data.md), [menu.md](../reverse_engineering/menu.md) |
| `egodata` extracts and converts those files to XML. | `CONFIRMED` | [`tools/egodata`](../../tools/egodata/) |
| The player, container and physics-rig pointers resolve, and position and velocity can be read. | `CONFIRMED`, one build | [reverse engineering index](../reverse_engineering/README.md) |

## What is not known

- Which screen and state show the car in the garage or car selection, and which data path holds the chosen car and livery.
- Whether the first state after boot can be redirected to that screen without the profile, event or stage state it may expect.
- Whether the car model is loaded by the screen alone, or needs a stage or session object.
- Where the camera lives, and whether it can be written outside the pause menu.
- Whether the splash and intro videos can be skipped without breaking the init order.

## First safe experiment (read only)

No game writes and no hooks. On a machine with the game, using `egodata`:

```
python3 -m tools.egodata extract game_1.dat system/screens.bin -o /tmp/ui/screens.bin
python3 -m tools.egodata extract game_1.dat system/states.bin  -o /tmp/ui/states.bin
python3 -m tools.egodata extract game_1.dat system/flow.bin    -o /tmp/ui/flow.bin
python3 -m tools.egodata xml /tmp/ui/screens.bin -o /tmp/ui/screens.xml
python3 -m tools.egodata xml /tmp/ui/states.bin  -o /tmp/ui/states.xml
python3 -m tools.egodata xml /tmp/ui/flow.bin    -o /tmp/ui/flow.xml
grep -n -i "garage\|car_select\|vehicle\|showroom\|livery" /tmp/ui/*.xml
```

Write down, for each candidate screen: its `Screen id`, the state that opens it, the links that lead to it, and the data paths it reads. That settles whether route A is plausible before any code is written.

## Second experiment (only if the first looks good)

Add a state, in the same way the loader adds `dr2hook_hub`, whose `screen_name` is the car-selection screen, and open it from the pause menu. If it opens, try the same from the first state after boot. Offline only.

## Out of scope

Redistributing the game's models or textures. Touching RaceNet or leaderboards.
