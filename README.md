# DR2Hook

**DR2Hook** is a project to fully reverse-engineer **DiRT Rally 2.0** (EGO Engine, x64, DirectX 11).

The project is built in three layers, each one resting on the one before it:

1. **Reverse engineering** — the goal. Map the game's memory, physics, tick order, and UI, with every finding graded by evidence.
2. **Mod loader** — the consequence. What the RE work makes possible: a DLL that loads into the game, runs Lua mods, and adds an overlay and a native menu.
3. **Example mod** — the demonstration. `mods/practice_mode/` shows a mod built on the loader: save a checkpoint on a stage and return to it.

This project is not affiliated with Codemasters or Electronic Arts.

## 1. Reverse engineering

This is the core of DR2Hook. Findings are split by subsystem and carry confidence labels: `CONFIRMED`, `PROBABLE`, `HYPOTHESIS`, and `REFUTED` (some documents use the Portuguese equivalents).

| Resource | What it is |
| --- | --- |
| [docs/reverse_engineering/README.md](docs/reverse_engineering/README.md) | Index of all findings: PhysicsRig pointer chain, wheels, suspension, tyres, engine, gearbox, damage, telemetry UDP, executable, UI data, pause menu |
| [INV-01 handoff](docs/reverse_engineering/investigations/INV-01/HANDOFF.md) | Current car-state investigation: origin block on the rig, tick order, native state API candidates, validation gates |
| [INV-01 knowledge base](docs/reverse_engineering/investigations/INV-01/kb/) | Graded facts, hypotheses, open questions, history, glossary |

### Research tools

| Tool | What it does |
| --- | --- |
| **Physics tick harness** ([docs](docs/physics_tick_harness.md)) | Opt-in hooks on the EGO physics tick path. Logs rig samples to CSV and can test queued writes and native calls on real tick boundaries. Off by default. |
| **`dr2rec`** ([docs](docs/BLACKBOX.md)) | Offline session recorder and analyzer. Read-only: no game writes, no RaceNet. Run with `scripts/dr2rec`. |
| **`egodata`** (`tools/egodata/`) | Reads EGO game data: NEFS archives, AES, binary XML. |
| `scripts/` | Capture and analysis helpers (suspension pairs, pause-menu observation). |

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
- **Native pause menu.** The pause menu gets a **DR2 Hook** entry that opens a game-native screen (built by patching the game's UI data at boot): open the overlay, reload Lua mods, reload the native core, and a **Mods** tab where each mod's `Menu` options appear. How it works: [menu.md](docs/reverse_engineering/menu.md) and [ui_data.md](docs/reverse_engineering/ui_data.md).
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
| Hooks | `onInit`, `onTick(dt)`, `onKeyDown(keyCode)`, `onRenderUI` (while the mod's overlay tab is open). `onStageStart` is registered but **not dispatched from gameplay yet**. |
| `Player` | `getPosition`, `setPosition`, `getVelocity`, `setVelocity`, `getState`, `setState` |
| `Safety` | `isRestrictedMode()` |
| `UI` | `notify(text, seconds)` |
| `Menu` | `button`, `toggle`, `choice`, `get` — options shown under **Pause → DR2 Hook → Mods** |

Engine RPM and gear are not exposed to Lua yet; they are only in the **Diagnostics** overlay. Full reference: [docs/MODDING_GUIDE.md](docs/MODDING_GUIDE.md).

## 3. Example mod: Practice Mode

`mods/practice_mode/` ships with the loader as a demonstration of what a mod can do. Long rally stages make it hard to repeat one corner; this mod saves the car's state and puts it back without restarting the stage.

| Key | Action |
| --- | --- |
| **F5** | Save checkpoint (position, orientation, linear and angular velocity) |
| **F6** | Restore **Normal** — saved pose, velocities zeroed |
| **F7** | Restore **With Momentum** — saved pose and saved velocities |

The same actions, plus a restore-mode choice and a notifications toggle, are under **Pause → DR2 Hook → Mods → Practice Mode**. The mod keeps its own checkpoint in Lua through `Player.getState` / `Player.setState`.

Restoring writes the rig pose and velocities at the offsets used since the first version (`+0x320` / `+0x330`). **Known limitation:** INV-01 has since marked `+0x320` as a velocity field `REFUTED` (it is a one-tick-delayed copy; the origin state block is at `+0x2b0..+0x2e0`), so velocity restore, and therefore **With Momentum**, may not behave as described. Moving the restore to the origin block is planned in INV-01 and is not validated in-game yet. It is also not the game's own "reset vehicle" path.

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
| `tools/dr2rec/`, `tools/egodata/` | Offline research tools (Python) |
| `src/proxy/dxgi_proxy.cpp` | DXGI proxy |
| `src/core/hooks.cpp` | `Present`, window procedure, and Winsock hooks (resident in proxy) |
| `src/core/host.cpp` | Loads `dr2hook_core.dll` and reloads it on F8 |
| `src/core/core_module.cpp` | Reloadable core entry: frame, input, init, shutdown |
| `src/core/player.cpp` | Vehicle reads and checkpoint restore, using the documented rig offsets |
| `src/core/physics_tick_harness.cpp` | Opt-in physics instrumentation |
| `src/core/pause_menu.cpp`, `ui_patch.cpp`, `native_screen.cpp` | Native pause-menu entry and screens |
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

## Status (v0.1.0)

Validated on Linux with Proton, against one `dirtrally2.exe` build (`c119f509…3442`). Windows native and other game builds are untested.

### ✅ Working

| Area | What works |
| --- | --- |
| Research | Evidence-graded RE docs, INV-01 investigation, `dr2rec` and `egodata` tools |
| Loader | DXGI proxy that loads `dr2hook_core.dll`; hot reload with **F8** |
| Overlay | Dear ImGui overlay on **Insert**: Diagnostics tab and one tab per mod |
| Native menu | **DR2 Hook** pause-menu entry that opens a game-native screen, with a Mods page |
| Lua mods | Lua 5.4 with `onInit`, `onTick`, `onKeyDown`, and `Player`, `Safety`, `UI`, `Menu` |
| Network | `NetworkGuard` always on: non-local traffic is refused, localhost still works |
| Example mod | Practice Mode: save a checkpoint (F5) and restore the **position and orientation** (F6) |

### ⚠️ Not working yet

| Area | Problem |
| --- | --- |
| Practice Mode | **Velocity restore is unreliable.** INV-01 `REFUTED` `+0x320` as a velocity field, so **With Momentum** (F7) may not carry the real speed, and **Normal** may not zero it correctly. |
| Practice Mode | "Clear checkpoint on new stage" does nothing: `onStageStart` is never called from gameplay. |
| Practice Mode | "Indestructible tyres" and "Indestructible car" are placeholders. |
| Safety | `SafetyGuard` offline-only check is **not enforced**; the core starts in permissive mode. |
| Lua API | RPM and gear are not exposed to Lua; they appear only in the Diagnostics overlay. |
| Compatibility | Only one game build and only Linux/Proton were tested. |
| Research harness | Physics tick harness is off by default and its in-game self-test (gate G3) has not run yet. |

## Roadmap

### Near term (concrete work)

- Run the harness gate G3, then the INV-01 write experiments.
- Move the Practice Mode restore to the origin state block (`+0x2b0..+0x2e0`) and validate it in-game.
- Dispatch `onStageStart` from gameplay.
- Enforce `SafetyGuard` with a real offline/online signal (proposed INV-02).
- Expose engine RPM and gear to Lua.

### Ideas under study (speculation, no promises)

These are ambitions. Nobody knows yet whether they are possible, and nothing here is started. They live in [docs/demands](docs/demands/README.md), where each idea explains why it might be possible, what is unknown, and what the first safe experiment is.

| Idea | What it would mean | Document |
| --- | --- | --- |
| **Multiplayer through live ghosts** | Other players' cars drawn as ghosts, updated over the network, and maybe made solid up close. | [live-ghosts-multiplayer.md](docs/demands/live-ghosts-multiplayer.md) |
| **Map editor** | Create or change stages. | [map-editor.md](docs/demands/map-editor.md) |
| **Vehicle editor / custom cars** | Install custom cars (for example, a Beetle, if someone builds one). | [vehicle-editor.md](docs/demands/vehicle-editor.md) |
| **Level editor and modeling** | Build level content and make 3D models. Scope not decided yet. | [level-editor-and-modeling.md](docs/demands/level-editor-and-modeling.md) |

`NetworkGuard` blocks the network by design, so any multiplayer would need a separate, narrow exception that never reaches RaceNet or the official leaderboards.

## License

[PolyForm Noncommercial 1.0.0](LICENSE). Anyone can use, copy, modify, and share DR2Hook for free. Selling it, or using it in a commercial product or service, is not allowed. This license makes the project source-available rather than OSI-approved open source. The bundled Lua in `vendor/lua/` keeps its own MIT license.
