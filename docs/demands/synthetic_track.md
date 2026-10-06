# Synthetic track in the game

> **Speculation.** A synthetic track exists offline (`tools/synthtrack`, see [docs/tools/synthtrack.md](../tools/synthtrack.md)). Nothing has been tried in the game. Every step below that writes a `.nefs` needs the owner's go-ahead before the result is loaded by the game.

> **Analysis of 2026-10-06:** the order of experiments, the open unknowns and the host-track choice are in [../plans/mapa_no_jogo.md](../plans/mapa_no_jogo.md) (Portuguese).

## The idea

Drive a track that DR2Hook generated, starting with the procedural "DR2Hook Ring".

## What exists

| What | Grade | Source |
| --- | --- | --- |
| The generator writes the track in the Track Explorer export format (DR2M, DR2I, `track.json`) and both viewers open it. | `CONFIRMED` offline | [synthtrack.md](../tools/synthtrack.md) |
| It also writes `objects.ens`, `trees.bin` and `ornaments.bin` in the layout the exporter reads and `track/edit.py` writes, and 16 power-of-two PNG textures. | `CONFIRMED` offline (tests) | `tools/synthtrack/tests/` |
| `nefs_write.replace_files` can replace files in an existing `.nefs` if each new file keeps the same number of 64 KiB blocks. | `CONFIRMED` by re-reading the package; not loaded by the game | [track_formats.md](../reverse_engineering/track_formats.md#escrita-num-nefs) |
| `egodata/pssg.py` can serialise a PSSG tree. | `CONFIRMED` for round trips in tests | `tools/egodata/tests/test_pssg_write.py` |

## Why a brand-new track is out of reach today

- **Registration.** Nobody knows how the game lists a location in menus and events. A new `locations/*.nefs` would not be offered.
- **Collision.** `track.jpk` holds 480 `.vcqtc` quadtree tiles whose vertices are not decoded. Without it the car has nothing to drive on.
- **Package size rule.** `replace_files` keeps the block count of each file. A different `tracksplit.pssg` almost never fits the original block count.
- **AI and timing.** `progress_track.xml` and `ai_track.xml` are binary XML that the repo can read but not yet write.

## The plausible route: dress an existing track

Use the smallest stage, Montalegre rallycross (~1 GB), as a host, and change it step by step. Each step is tested in the game separately and only with the owner's go-ahead:

1. **Objects.** Move, delete and copy objects already works through `edits.json` → `track/edit.py`. A first in-game test of an edited Montalegre would validate the whole write path. The synthetic barriers and trees could be placed as copies of existing Montalegre object types.
2. **Textures.** Replace the pixels of existing textures (asphalt, grass, barriers) with the synthetic ones. The image keeps the original size and DXT format, so the PSSG and the package keep their size. This needs a DXT1/DXT5 encoder; there is none in the repo yet.
3. **Terrain shape.** Move the vertices of `tracksplit.pssg` blocks (same vertex count, same size). Collision would not follow, so this only makes sense after the `.vcqtc` format is decoded.
4. **Route.** Write `progress_track.xml` and `ai_track.xml` from `source/layout.json`. This needs a binary XML writer and a matching collision.

## First safe experiment

Steps 1 and 2 offline, with no game run:

1. Pick a Montalegre texture of the same size as a synthetic one. Encode the synthetic PNG to the same DXT format.
2. Write the new PSSG with `pssg.py` and check that it has the same size and the same number of 64 KiB blocks.
3. Write a new `.nefs` in `build/uiview/saves/` with `replace_files`, re-read it, and compare every other file by hash.
4. Open it in the Track Explorer and check that the texture changed.

Loading that `.nefs` in the game is the next step and is the owner's decision.

## Out of scope

Redistributing the game's assets. The synthetic textures and geometry are generated and can be shared; an edited game package cannot.
