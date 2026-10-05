# DR2 UI Viewer (`tools/uiview/`)

Browser tools that read the game's own files and show them locally. They run **outside the game**, read from the game folder, and write only under `build/uiview/` (the output folder). Nothing here changes the game folder.

Requirements: Python 3, the game installed (the path comes from `tools.egodata.cli.DEFAULT_GAME`; override with `--game`), and a browser with WebGL.

## Tabs

| Tab | Source files | What it does |
| --- | --- | --- |
| **Screens** | UI data (`game_1.dat`), images, `.lng` text | The game's UI screens and images, search, pt/en labels |
| **Cars** | `cars/*.nefs` (PSSG) | **Car Model Explorer**: real node tree (LOD, nodes, slices, materials), textures, move/rotate with undo/redo, game cameras (`cameras.xml`), wheel and disc patches |
| **Tracks** | `locations/*.nefs` | **Track Explorer / editor**: terrain, objects, trees, ornaments, track limits, AI line; move, rotate, delete, duplicate; write a new `.nefs` |

## Commands

```bash
# UI screens, images and car models (default output: build/uiview)
python -m tools.uiview [--game PASTA] [-o SAIDA] [--models 037] [--all-models] [--open]

# export one or more stages (empty --tracks only re-indexes)
python -m tools.uiview.track --tracks montalegre,poland_rally_01 -o build/uiview

# serve build/uiview and enable the "Save .nefs" button
python -m tools.uiview.serve [--port 8790] [--root build/uiview]

# apply an exported edits file to a NEW .nefs (never inside the game folder)
python -m tools.uiview.track.edit montalegre.edits.json -o build/uiview/saves/montalegre.nefs
```

Tests: `bash scripts/dev/test_tools.sh` (or `python3 -m unittest discover -s tools/uiview/tests -t .`).

## Layout

```
tools/uiview/
  export.py content.py mesh.py   shared: UI screens, textures, DR2M geometry
  car/                           Car Model Explorer (carmodel.py, models.py: package catalog and export)
  track/                         Track Explorer (export.py: stage export, edit.py: write the edited .nefs)
  serve.py                       local server with POST /api/save
  web/                           index.html, css/, js/ (copied as is into the output folder)
  tests/
```

## Track Explorer controls

| Action | Control |
| --- | --- |
| Orbit / pan / zoom | Drag / right button or Shift / wheel; **WASD** moves; **F** frames; **Esc** deselects |
| Tools | **1** Navigate, **2** Move (drag on the ground, Shift raises and lowers), **3** Rotate |
| Edit | Type X/Y/Z in the panel, ±15° / ±90° buttons, **Del** or **H** deletes, **Restore** brings the original back |
| Duplicate | **Ctrl+D** or the button (only for objects from `objects.ens`) |
| History | **Ctrl+Z** / **Ctrl+Y**, kept per route (300 steps) |
| Save | **Save .nefs** (needs `tools.uiview.serve`) writes `build/uiview/saves/<stage>.nefs`; **Export edits** downloads `<stage>.edits.json` |

The layers (terrain, objects, trees, distant terrain, track limits, AI line) and the draw distance (700 m default) are in the top bar.

## How an edit reaches a `.nefs`

The viewer keeps, for each object that changed, its new 3×3 matrix and position, or a *deleted* mark, or (for a copy) the index of the object it came from. `track/edit.py` turns that into changes to the source file of the object:

| Object source | Edit |
| --- | --- |
| `objects.ens` (text XML) | The `TEMPLATETRANSFORM` changes. A deleted instance leaves the text; the file is padded with spaces so it keeps its size. A **duplicate** is a new `TEMPLATEENTITYINSTANCE` with a new `id` (`…_dupN`), `instanceID`, and `instance_tag`; it must fit in the free space of the file's last 64 KiB block (about 80 copies). |
| `ornaments.bin`, `trees.bin` | The record's matrix and position change. A deleted object has a zero matrix and `y = -10000` (the record count is fixed). Duplicates are refused. |

`egodata.nefs_write.replace_files` copies the volume once and appends the new files at the end, updating the directory tables; a new file must have the same number of 64 KiB blocks as the old one, and the signed intro (128 bytes) is kept. On the Montalegre rallycross a package with four edits was written in 0.6 s (884 MB), re-read correctly, and every other file matched by hash.

## Limits

- **Not tested in the game.** The game may reject a changed package or a duplicate with a new `instanceID`.
- **Collision does not change.** It lives in `track.jpk` (480 quadtree tiles `qt_*.vcqtc`), whose vertices are not decoded. A deleted ornament can still be solid.
- **Rally stages are heavy**: Poland has ~9 million terrain vertices and ~305,000 instances. The export takes ~35–60 s and works; the viewer needs a real GPU and probably LOD or tiles on demand.
- The dense road blocks (`batched_track.fx`) declare no texture and are drawn in a fixed dirt colour. Blended terrain blocks (`terrain_wsm_*`) use only their first diffuse texture.
- Exporting all 40 stages would take tens of GB; only the stages you ask for are exported.

File formats are in [reverse_engineering/track_formats.md](../reverse_engineering/track_formats.md).
