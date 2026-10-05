# DR2Hook

**DR2Hook** is a project to fully reverse-engineer **DiRT Rally 2.0** (EGO Engine, x64, DirectX 11).

The project is built in four layers, each one resting on the one before it:

1. **Reverse engineering** — the goal. Map the game's memory, physics, tick order, UI, ghost cars, save files, and asset formats, with every finding graded by evidence.
2. **Mod loader** — the consequence. What the RE work makes possible: a DLL that loads into the game, runs Lua mods, and adds an overlay and native menus.
3. **Example mod** — the demonstration. `mods/practice_mode/` shows a mod built on the loader: checkpoints, race-start modes, and ghost-car tools.
4. **Asset tools** — the same reverse engineering applied to the game's files, outside the game: a browser-based **Car Model Explorer** and **Track Explorer / editor** (`tools/uiview/`).

### What is new since v0.1.0

- **Ghost cars.** Save format (`GHST`) and cipher decoded, a live gap to the ghost, extra ghost copies, a solid (opaque) ghost, ghost collision research, and **up to 15 ghost cars plus the player** on one stage (the 16-car render limit was measured). See [ghosts.md](docs/reverse_engineering/ghosts.md).
- **Stage lifecycle events in Lua.** `onStageLoad`, `onCountdown`, and `onStageStart` now fire from gameplay, plus `Race.setStartMode` (normal, no countdown, automatic, on throttle).
- **Main-menu tab and richer native menu.** A **DR2 Hook** tab in the main menu, a right-hand panel per option, rich text, and per-mod saved settings (`settings.ini`).
- **Remote command channel.** `dr2hook_cmd.txt` lets a tool or an AI drive the game without anyone at the keyboard ([docs](docs/REMOTE_COMMANDS.md)).
- **Free camera (F9) and terminal damage (F11)** with their reverse-engineering notes.
- **Physics tick harness gate G3 passed.** The harness self-test ran in-game with no mismatches, and the rig velocities were moved to the origin block (`+0x2b0` / `+0x2c0`).
- **Asset tools.** Save decryption (`dr2save`), ghost parsing (`dr2ghost`), NEFS writing, a PSSG parser and serializer, and the browser explorers for **cars** and **tracks**, which can move, rotate, delete, and duplicate stage objects and write the result into a *new* `.nefs`.

This project is not affiliated with Codemasters or Electronic Arts.

## 1. Reverse engineering

This is the core of DR2Hook. Findings are split by subsystem and carry confidence labels: `CONFIRMED`, `PROBABLE`, `HYPOTHESIS`, and `REFUTED` (some documents use the Portuguese equivalents).

| Resource | What it is |
| --- | --- |
| [docs/reverse_engineering/README.md](docs/reverse_engineering/README.md) | Index of all findings: PhysicsRig pointer chain, wheels, suspension, tyres, engine, gearbox, damage, telemetry UDP, executable, UI data, pause menu, camera, terminal damage, ghosts, stage loading, track file formats |
| [ghosts.md](docs/reverse_engineering/ghosts.md) | Ghost cars: encrypted saves, `GHST` format, slots, copies, opacity, collision, and the 15-car limit |
| [track_formats.md](docs/reverse_engineering/track_formats.md) | What is decoded in `locations/*.nefs`: terrain, objects, trees, ornaments, AI line, collision archive |
| [INV-01 handoff](docs/reverse_engineering/investigations/INV-01/HANDOFF.md) | Current car-state investigation: origin block on the rig, tick order, native state API candidates, validation gates |
| [INV-01 knowledge base](docs/reverse_engineering/investigations/INV-01/kb/) | Graded facts, hypotheses, open questions, history, glossary |

### Research tools

| Tool | What it does |
| --- | --- |
| **Physics tick harness** ([docs](docs/physics_tick_harness.md)) | Opt-in hooks on the EGO physics tick path. Logs rig samples to CSV and can test queued writes and native calls on real tick boundaries. Off by default. |
| **`dr2rec`** ([docs](docs/BLACKBOX.md)) | Offline session recorder and analyzer. Read-only: no game writes, no RaceNet. Run with `scripts/dr2rec`. |
| **`egodata`** (`tools/egodata/`) | Reads and writes EGO game data: NEFS archives (including writing a modified copy), AES, binary XML, text files. |
| **`dr2save`**, **`dr2ghost`** (`tools/`) | Decrypt the game's save files (AES-256-ECB, fixed key) and parse ghost recordings (`GHST`). |
| **`pssg`** (`tools/pssg.py`) | Parser and serializer for the PSSG model/texture format. |
| **DR2 UI Viewer** (`tools/uiview/`, [docs](docs/UIVIEW.md)) | Browser tools: UI screens, **Car Model Explorer**, **Track Explorer / editor**. Exports the game's files to a local folder and shows them with WebGL. |
| `scripts/` | Capture and analysis helpers (suspension pairs, pause-menu observation), `restart_game.sh`. |

Physics tick harness switches (environment variable or INI key):

| Enable | Env / INI |
| --- | --- |
| Instrumentation + CSV | `DR2HOOK_PHYSICS_HARNESS=1` or `instrumentation=1` |
| Queued rig writes | `DR2HOOK_PHYSICS_HARNESS_WRITES=1` or `writes=1` |
| Experimental native calls | `DR2HOOK_PHYSICS_HARNESS_EXPERIMENTAL_NATIVE=1` or `experimental_native=1` |
| Self-test (no real writes) | `DR2HOOK_PHYSICS_HARNESS_SELF_TEST=1` or `self_test=1` |

Native calls need instrumentation, the experimental flag, and `SafetyGuard::CanWriteState()`. They run only on chosen physics boundaries, never on `Present`.

## 2. Mod loader

Once the game's memory and UI are mapped, the same knowledge can be used to load code into the game. That is the mod loader: two DLLs placed next to `dirtrally2.exe`, with no external injector.

```
dirtrally2.exe
    └── dxgi.dll              stays loaded: DXGI proxy, Present, WndProc, NetworkGuard
            └── dr2hook_core.dll    overlay, telemetry reads, Lua, savestate (F8 reloads this file)
```

What it provides:

- **Lua 5.4 mods** loaded from `mods/<name>/`, with `Player`, `Safety`, `UI`, and `Menu` bindings.
- **In-game overlay** (Dear ImGui, **Insert**):
  - **Diagnostics** — RPM, gear, redline, speed, position, linear and angular velocity, per-wheel suspension and ground contact, and whether the player / container / physics-rig pointers resolve.
  - **Mods** — loaded mods. **Reload Scripts** restarts Lua; **Reload Native Core (F8)** reloads `dr2hook_core.dll`.
  - **One tab per loaded mod.** The Practice Mode tab adds a C++ checkpoint panel (`SavestateManager`) with save/restore buttons and the default restore mode.
- **Native menus.** The pause menu and the main menu get a **DR2 Hook** entry that opens a game-native screen (built by patching the game's UI data at boot): open the overlay, reload Lua mods, reload the native core, and a **Mods** tab where each mod's `Menu` options appear, with a description panel per option, rich text, and saved settings. How it works: [menu.md](docs/reverse_engineering/menu.md), [ui_data.md](docs/reverse_engineering/ui_data.md), [ui_tabs.md](docs/reverse_engineering/ui_tabs.md), and [ui_limits.md](docs/reverse_engineering/ui_limits.md).
- **Stage lifecycle.** The core watches the stage load, the countdown, and the start (including restarts) and passes them to Lua. An optional `AutoStage` hook and `LoadTrace` help with loading research ([stage_loading.md](docs/reverse_engineering/stage_loading.md)).
- **GhostLab.** Native support for ghost cars: live time gap, copies of the ghost lap, opaque ghost, pausing the ghosts, and more ghost cars than the game normally allows.
- **Remote commands.** A file-based command channel for testing without a person at the keyboard ([docs/REMOTE_COMMANDS.md](docs/REMOTE_COMMANDS.md)).
- **Hot reload.** **F8** unloads `dr2hook_core.dll` and loads a fresh copy while the game keeps running. Changes to `dxgi.dll` still need a game restart.
- **NetworkGuard.** Remote network traffic is blocked while the loader is in the game (see [Fair play](#fair-play)).

### Writing a mod

A mod is a folder under `mods/` with a `mod.json` manifest and a Lua entry script.

```
mods/
└── my_mod/
    ├── mod.json
    └── main.lua
```

```lua
Menu.button("hello", "Say hello", function()
    UI.notify("Hello from my mod", 2.0)
end)

function onInit()
    print("[MyMod] ready")
end

function onTick(dt)
    -- once per rendered frame (Present)
end

function onKeyDown(keyCode)
    if keyCode == 0x74 then -- F5
        local pos = Player.getPosition()
        print(string.format("%.2f %.2f %.2f", pos.x, pos.y, pos.z))
    end
end
```

| API | Contents |
| --- | --- |
| Hooks | `onInit`, `onTick(dt)`, `onKeyDown(keyCode)`, `onRenderUI` (while the mod's overlay tab is open), `onStageLoad(stage)`, `onCountdown(light)`, `onStageStart(stage)` (`stage.name`, `stage.restart`) |
| `Player` | `getPosition`, `setPosition`, `getVelocity`, `setVelocity`, `getState`, `setState`, `getVehicleTelemetry` (RPM, gear, speed, torque, throttle) |
| `Safety` | `isRestrictedMode()` |
| `Race` | `setStartMode("normal" \| "no_countdown" \| "automatic" \| "on_throttle")` |
| `Ghost` | `status()`, `clone(count, spacing)`, `setOpaque(bool)`, `setTimeOffset(seconds)`, `setHud(bool)` |
| `UI` | `notify(text, seconds)` |
| `Menu` | `button`, `toggle`, `choice`, `get`, `set`, `describe` — options shown under **Pause → DR2 Hook → Mods** |

Full reference: [docs/MODDING_GUIDE.md](docs/MODDING_GUIDE.md).

## 3. Example mod: Practice Mode

`mods/practice_mode/` ships with the loader as a demonstration of what a mod can do. Long rally stages make it hard to repeat one corner; this mod saves the car's state and puts it back without restarting the stage. It also holds the race-start and ghost-car options.

| Key | Action |
| --- | --- |
| **F5** | Save checkpoint (position, orientation, linear and angular velocity) |
| **F6** | Restore **Normal** — saved pose, velocities zeroed |
| **F7** | Restore **With Momentum** — saved pose and saved velocities |

Core hotkeys (not part of any mod):

| Key | Action |
| --- | --- |
| **F8** | Reload `dr2hook_core.dll` from disk. The game stays open; the in-memory C++ checkpoint is cleared and Lua restarts (`onInit` runs again). |
| **F9** | Toggle the free camera on a stage. Mouse looks. WASD moves, Space/Q go up and down. Hold Ctrl to freeze keyboard movement; the mouse keeps looking. + and − raise and lower the keyboard speed. Shift multiplies that speed by 4. The game pause menu gives the cursor back and freezes the fly camera until it closes. Insert still opens the overlay and pauses the fly camera. |
| **F11** | Insta crash: destroys the car on a stage (offline only) so the terminal-damage flow can be studied. Esc during the sequence opens the pause menu with Restart. See [terminal_damage.md](docs/reverse_engineering/terminal_damage.md). |

Ghost-car mode (developer feature): while `dr2hook_ghost_cars.txt` exists next to `dirtrally2.exe`, the number inside it sets how many ghost cars the game creates on the next full stage load (**15 is the maximum**: larger values are clamped, because 16 ghosts crash the load), and the core fills them with copies of your ghost lap automatically. In that mode **F7** adds one more test copy and **F6** pauses and resumes all ghosts, instead of the checkpoint actions. Without the file, F6 and F7 keep the Practice Mode behavior. See [ghosts.md](docs/reverse_engineering/ghosts.md) §6.

The same actions are under **Pause → DR2 Hook → Mods → Practice Mode**, together with: restore mode, race-start mode (Normal, No countdown, Automatic, On throttle), live gap to the ghost, extra ghost copies and their spacing, a head start for the ghost, a solid ghost car, and a notifications toggle. The mod keeps its own checkpoint in Lua through `Player.getState` / `Player.setState`.

The core now reads and writes the rig velocities at the origin state block (`+0x2b0` linear, `+0x2c0` angular), after INV-01 `REFUTED` `+0x320` as a velocity field (it is a one-tick-delayed copy). **Known limitation:** an in-game validation of **With Momentum** on the new offsets is not recorded in the docs yet, and the restore is still not the game's own "reset vehicle" path.

## 4. Asset tools: Car and Track Explorer

`tools/uiview/` reads the game's own files (NEFS archives, PSSG models, XML) and shows them in a local web page. It never changes the game folder. Details and commands: [docs/UIVIEW.md](docs/UIVIEW.md).

| Tab | What it does |
| --- | --- |
| **Screens** | The game's UI screens and images, with search and localization (pt/en). |
| **Cars** | **Car Model Explorer.** Real node tree of each car (LOD, nodes, slices, materials), 3D viewport with textures, move/rotate parts with undo/redo, game cameras, and wheel and disc patches. |
| **Tracks** | **Track Explorer and editor.** Terrain, physics objects, ornaments, trees, track limits, and the AI line of a stage, drawn with instancing. Move, rotate, delete, and **duplicate** objects (Ctrl+Z / Ctrl+Y). **Save .nefs** writes a *new* package under `build/uiview/saves/`; the game folder is never written. |

```bash
python -m tools.uiview.track --tracks montalegre -o build/uiview   # export one stage (about 15 s)
python -m tools.uiview.serve                                       # serve build/uiview and enable Save .nefs
```

**Limits.** A modified `.nefs` has **not been tested in the game yet**. Stage collision (`track.jpk`) is not decoded and does not change, so a deleted object can still be solid. Only the Montalegre rallycross was exported and viewed; the rally stages (millions of vertices) are exported but not rendered in the test environment. Duplicating works only for objects in `objects.ens`.

## Install

Copy `dxgi.dll`, `dr2hook_core.dll`, and the `mods/` folder into the game directory:

`[SteamLibrary]/steamapps/common/DiRT Rally 2.0/`

The game loads `dxgi.dll` at startup. The proxy forwards to the real `dxgi.dll` in `System32` and loads `dr2hook_core.dll` from the same folder.

Windows and Linux / Steam Deck (Proton) steps: [docs/INSTALL.md](docs/INSTALL.md).

## Build

Requirements: CMake 3.20+, and either MinGW-w64 (Linux cross-compile) or Visual Studio 2022 (MSVC, x64, C++20).

| Goal | Command |
| --- | --- |
| Linux: unit tests + release package | `bash scripts/verify_release.sh` |
| Linux: release package only | `bash scripts/package_release.sh` |

The package (`dist/DR2Hook-v0.1.0.zip`) contains `dxgi.dll`, `dr2hook_core.dll`, and `mods/`. Both DLLs are statically linked.

Windows:

```bash
mkdir build && cd build
cmake .. -A x64
cmake --build . --config Release --target dxgi --target dr2hook_core
```

During development, rebuild only `dr2hook_core.dll`, replace it next to `dirtrally2.exe`, and press **F8**. The host copies the file to `dr2hook_core.N.dll` before loading it, so the build can overwrite the original while the game runs. **F8** is handled by the proxy and is not passed to mods.

## Repository layout

| Path | Role |
| --- | --- |
| `docs/reverse_engineering/` | RE findings by subsystem, investigations (INV-01) |
| `docs/` | Install, modding guide, harness, black box, architecture notes |
| `docs/demands/` | Speculative ideas (multiplayer, map, vehicle, level editor and modeling) |
| `tools/dr2rec/`, `tools/egodata/`, `tools/dr2save.py`, `tools/dr2ghost.py`, `tools/pssg.py` | Offline research tools (Python) |
| `tools/uiview/` | DR2 UI Viewer: Car Model Explorer, Track Explorer / editor, exporter and track edit tool |
| `src/proxy/dxgi_proxy.cpp` | DXGI proxy |
| `src/core/hooks.cpp` | `Present`, window procedure, and Winsock hooks (resident in proxy) |
| `src/core/host.cpp` | Loads `dr2hook_core.dll` and reloads it on F8 |
| `src/core/core_module.cpp` | Reloadable core entry: frame, input, init, shutdown |
| `src/core/player.cpp` | Vehicle reads and checkpoint restore, using the documented rig offsets |
| `src/core/physics_tick_harness.cpp` | Opt-in physics instrumentation |
| `src/core/pause_menu.cpp`, `ui_patch.cpp`, `native_screen.cpp` | Native pause-menu entry and screens |
| `src/core/ghost_lab.cpp`, `ghost_trace.cpp` | Ghost cars: gap, copies, opacity, pause, limits |
| `src/core/race_events.cpp`, `load_trace.cpp`, `auto_stage.cpp` | Stage lifecycle events and loading research |
| `src/core/free_camera.cpp`, `terminal_damage.cpp`, `remote_commands.cpp` | Free camera (F9), insta crash (F11), remote command channel |
| `src/core/safety.cpp` | Session write gate |
| `src/script/` | Lua runtime, mod loading, `Menu` API |
| `src/ui/overlay.cpp` | ImGui overlay |
| `mods/practice_mode/` | Example mod |
| `tests/` | Unit and Wine tests |

## Fair play

DR2Hook is for offline practice, research, and community mods. It is not for leaderboards or RaceNet.

**Network.** While `dxgi.dll` is loaded, `NetworkGuard` hooks `ws2_32.dll`:

- `getaddrinfo` / `GetAddrInfoW` for anything other than localhost returns "name not found".
- `connect` outside `127.0.0.0/8` and IPv6 loopback returns `WSAECONNREFUSED`.
- Localhost is untouched, so local tools (SimHub, motion rigs, UDP telemetry on `127.0.0.1`) still work.

No menu or script can turn this off. To play online again, exit the game and remove `dxgi.dll` and `dr2hook_core.dll`.

**Memory writes.** `SafetyGuard` is designed to allow position, velocity, and state writes only in DirtFish, offline time trial, and custom offline championships, and to refuse when the session mode is unknown. In v0.1.0 the core still starts in permissive mode, so this check is **not enforced in-game yet**.

## Status

Developed and validated on Linux with Proton, against one `dirtrally2.exe` build (`c119f509…3442`). Windows native and other game builds are untested. The packaged release is still labelled **v0.1.0**; the list above ("What is new since v0.1.0") has not been through a new release yet.

### ✅ Working

| Area | What works |
| --- | --- |
| Research | Evidence-graded RE docs, INV-01 investigation (gate G3 passed), `dr2rec`, `egodata`, `dr2save`, `dr2ghost`, `pssg` |
| Loader | DXGI proxy that loads `dr2hook_core.dll`; hot reload with **F8** |
| Overlay | Dear ImGui overlay on **Insert**: Diagnostics tab and one tab per mod |
| Native menus | **DR2 Hook** entry in the pause menu and in the main menu, with a Mods page, per-option description panel, and saved settings |
| Lua mods | Lua 5.4 with `onInit`, `onTick`, `onKeyDown`, `onStageLoad`, `onCountdown`, `onStageStart`, and `Player`, `Safety`, `Race`, `Ghost`, `UI`, `Menu` |
| Network | `NetworkGuard` always on: non-local traffic is refused, localhost still works |
| Practice Mode | Save a checkpoint (F5) and restore the **position and orientation** (F6); race-start modes; clear checkpoint on a new stage |
| Ghost cars | Live gap, extra copies, solid ghost, pause (F6 in ghost mode), up to **15 ghost cars + player** |
| Free camera / insta crash | F9 free camera; F11 terminal damage with pause and restart |
| Remote testing | `dr2hook_cmd.txt` command channel (status, pause, link, key, opt) |
| Asset tools | Car Model Explorer and Track Explorer, with edits written to a **new** `.nefs` (tested by re-reading the package; 27 + 16 Python tests pass) |

### ⚠️ Not working yet

| Area | Problem |
| --- | --- |
| Practice Mode | **Velocity restore is not validated.** The core now uses the origin block (`+0x2b0` / `+0x2c0`), but **With Momentum** has not been re-checked in-game. |
| Practice Mode | "Indestructible tyres" and "Indestructible car" are placeholders. |
| Safety | `SafetyGuard` offline-only check is **not enforced**; the core starts in permissive mode. |
| Lua API | `Race.setStartMode` returns `false` outside the Windows build. |
| Ghost cars | More than 15 ghosts crash the stage load (17th car render object reads garbage). F8 while the pause menu is open freezes the game (cause unknown). |
| Terminal damage | Repairing sound and steering after a restart has not been tested. |
| Track editor | A modified `.nefs` has **not been tried in the game**. Collision is not edited. Duplicated objects get new `instanceID` values; whether the game accepts them is unknown. Rally stages (millions of vertices) are not rendered in the test environment. |
| Track formats | Collision tiles (`.vcqtc`), `track.vis`, `grass.grs`, and the texture of the dense road blocks are not decoded. |
| Compatibility | Only one game build and only Linux/Proton were tested. |
| Research harness | The physics tick harness is off by default; the INV-01 write experiments (after G3) have not run. |

## Roadmap

### Near term (concrete work)

- Test one small, safe edited `.nefs` in the game (only with the owner's go-ahead) to validate the save path of the track editor.
- Decode stage collision (`.vcqtc`) and the remaining track formats, then export and render the rally stages on a real GPU.
- Re-validate **With Momentum** on the origin block, and run the INV-01 write experiments.
- Enforce `SafetyGuard` with a real offline/online signal (proposed INV-02).
- Native desktop viewer for the asset tools (SDL3 + OpenGL), reading the files the Python exporter already writes, so large stages do not depend on the browser.

### Ideas under study (speculation, no promises)

These are ambitions. Nobody knows yet whether they are possible. They live in [docs/demands](docs/demands/README.md), where each idea explains why it might be possible, what is unknown, and what the first safe experiment is.

| Idea | What it would mean | Status | Document |
| --- | --- | --- | --- |
| **Multiplayer through live ghosts** | Other players' cars drawn as ghosts, updated over the network, and maybe made solid up close. | Ghost cars, copies, solid ghost, and the 15-car limit are now understood; networking is not started. | [live-ghosts-multiplayer.md](docs/demands/live-ghosts-multiplayer.md) |
| **Map editor** | Create or change stages. | **Started:** the Track Explorer reads and edits object placement offline. Game acceptance untested. | [map-editor.md](docs/demands/map-editor.md) |
| **Vehicle editor / custom cars** | Install custom cars (for example, a Beetle, if someone builds one). | Car Model Explorer reads car models; custom cars not started. | [vehicle-editor.md](docs/demands/vehicle-editor.md) |
| **Level editor and modeling** | Build level content and make 3D models. Scope not decided yet. | Not started. | [level-editor-and-modeling.md](docs/demands/level-editor-and-modeling.md) |

`NetworkGuard` blocks the network by design, so any multiplayer would need a separate, narrow exception that never reaches RaceNet or the official leaderboards.

## License

[PolyForm Noncommercial 1.0.0](LICENSE). Anyone can use, copy, modify, and share DR2Hook for free. Selling it, or using it in a commercial product or service, is not allowed. This license makes the project source-available rather than OSI-approved open source. The bundled Lua in `vendor/lua/` keeps its own MIT license.
