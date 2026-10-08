<p align="center"><img src="docs/images/dr2hook_logo.webp" alt="DR2Hook — DR2 Mod Loader" width="560"></p>

# DR2Hook

Reverse-engineering project for DiRT Rally 2.0 (EGO Engine, x64, DirectX 11): notes on the game's internals, a mod loader, and tools for its files.

Not affiliated with Codemasters or Electronic Arts.

## Contents

| Path | What |
| --- | --- |
| `docs/reverse_engineering/` | Notes on the game: memory, physics, UI, ghosts, stage loading, file formats |
| `src/` | Mod loader: a `dxgi.dll` proxy that loads `dr2hook_core.dll` (Lua mods, overlay, menu entry) |
| `mods/practice_mode/` | Example mod |
| `tools/` | Python tools for the game files, a web viewer for cars and tracks (`uiview`), and a track editor (`synthtrack`, `viewer3d`) |
| `scripts/` | Release, dev, and research scripts |

More in [docs/README.md](docs/README.md). Writing mods: [modding_guide.md](docs/guides/modding_guide.md).

## Install

Copy `dxgi.dll`, `dr2hook_core.dll`, and `mods/` next to `dirtrally2.exe`. See [install.md](docs/guides/install.md).

## Build

CMake 3.20+ with MinGW-w64 (Linux) or Visual Studio 2022.

```bash
bash scripts/release/verify_release.sh
```

Python tool tests: `bash scripts/dev/test_tools.sh`.

## Online play

While the loader is in the game, network traffic outside localhost is blocked. Remove the two DLLs to play online.

## Status

Only tested on Linux/Proton with one game build.

## Thanks

- **filipe411**, for the `raceload` path tip that led to the folder overlay.
- **Ssor**, for [ego-visibility-system](https://github.com/ssor0/ego-visibility-system).
- **Paths** ([ItsNotPaths](https://github.com/ItsNotPaths)), for [DiRTbench](https://github.com/ItsNotPaths/DiRTbench) (MIT).
- [Ego-Engine-Modding](https://github.com/EgoEngineModding/Ego-Engine-Modding) (MIT).

## License

[PolyForm Noncommercial 1.0.0](LICENSE). Free to use, copy, modify, and share; not for commercial use. The bundled Lua in `vendor/lua/` keeps its own MIT license.
