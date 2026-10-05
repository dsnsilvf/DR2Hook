# DR2Hook

**DR2Hook** is a reverse-engineering and instrumentation project for **DiRT Rally 2.0** (EGO Engine, x64, DirectX 11). It maps vehicle physics, tick boundaries, and UI data with evidence-graded notes, optional physics-tick hooks, and offline capture tools. On top of that work sits a **mod loader**: Lua mods, an in-game overlay, practice checkpoints, and a native pause-menu integration branded **DR2 ModLoader v0.1.0** in the UI.

This project is not affiliated with Codemasters or Electronic Arts.

## Reverse engineering (primary focus)

Findings are split by subsystem and keep the original confidence labels (`CONFIRMED`, `PROBABLE`, `HYPOTHESIS`, `REFUTED`, and Portuguese variants where the source used them).

| Resource | What it is |
| --- | --- |
| [docs/reverse_engineering/README.md](docs/reverse_engineering/README.md) | Index: PhysicsRig pointer chain, wheels, suspension, engine, gearbox, telemetry UDP, UI data, and related topics |
| [docs/reverse_engineering/investigations/INV-01/HANDOFF.md](docs/reverse_engineering/investigations/INV-01/HANDOFF.md) | **INV-01** — car-state investigation: origin block on the rig, tick order, native state API candidates, harness status, validation gates |
| [docs/reverse_engineering/investigations/INV-01/kb/](docs/reverse_engineering/investigations/INV-01/kb/) | Graded knowledge base (`confirmed-facts`, `hypotheses`, `open-questions`, `history`, `glossary`) |
| [docs/physics_tick_harness.md](docs/physics_tick_harness.md) | Opt-in hooks on the EGO physics tick path, CSV logging, queued writes, experimental native calls |
| [docs/BLACKBOX.md](docs/BLACKBOX.md) | **`scripts/dr2rec`** — offline session recorder and analyzer (read-only; no game writes, no RaceNet) |

Runtime reads used by the overlay and `Player` APIs are implemented in `src/core/player.cpp` against the documented rig offsets. After changing only native logic, rebuild `dr2hook_core.dll`, replace it next to `dirtrally2.exe`, and press **F8** — the DXGI proxy and network hooks stay loaded.

## Mod loader (built on the RE work)

Press **Insert** in-game to open the Dear ImGui overlay (English UI). You can also open it from the pause menu entry **DR2 ModLoader** when hooks match the expected game build (see [docs/reverse_engineering/menu.md](docs/reverse_engineering/menu.md)).

### Practice checkpoints

Long rally stages make corner repetition painful. Checkpoints capture pose and velocity and restore them without restarting the stage.

| Key | Action |
| --- | --- |
| **F5** | Save checkpoint (position, orientation, linear and angular velocity). |
| **F6** | Restore using the overlay’s selected mode (**Normal** or **With Momentum**). |
| **F7** | Restore **With Momentum** (linear and angular velocity from the save). |
| **F8** | Reload `dr2hook_core.dll` from disk. The game stays open; the in-memory C++ checkpoint is cleared and Lua restarts (`onInit` runs again). |
| **F9** | Toggle the free camera on a stage. Mouse looks. WASD moves, Space/Q go up and down. Hold Ctrl to freeze keyboard movement; the mouse keeps looking. + and − (main row or numpad) raise and lower the keyboard speed. Shift multiplies that speed by 4. The game pause menu gives the cursor back and freezes the fly camera until it closes. Insert still opens the overlay and pauses the fly camera. |
| **F11** | Insta crash: destroys the car on a stage (offline only) so the terminal-damage flow can be studied. Esc during the sequence opens the pause menu with Restart. See `docs/reverse_engineering/terminal_damage.md`. |

The **Practice Mode** tab and the shipped mod `mods/practice_mode/` expose the same shortcuts. The overlay tab uses the C++ `SavestateManager`. The mod keeps its own Lua checkpoint via `Player.getState` / `Player.setState` and adds options under **Pause → DR2 Hook → Mods** (`Menu` API).

**Normal** restore writes rig pose and zeros linear/angular velocity at the documented velocity offsets (`+0x320` / `+0x330`). **With Momentum** keeps the saved velocities. This is not a full native “reset vehicle” pipeline; stability depends on the physics solver.

### Overlay tabs

- **Diagnostics** — RPM (crank rad/s at `+0x13d8`), gear (`+0x1448`), redline, speed, position, linear and angular velocity, per-wheel suspension compression and ground contact, and whether player / container / physics-rig pointers resolve.
- **Mods** — Lua mods under `mods/`. **Reload Scripts** restarts Lua only. **Reload Native Core (F8)** reloads `dr2hook_core.dll`.
- **Practice Mode** — save, restore, and choose Normal vs With Momentum (C++ checkpoint).

## Install

Build or download `dxgi.dll` and `dr2hook_core.dll` (see [Build](#build)), then copy both plus the `mods/` folder into the game directory:

`[SteamLibrary]/steamapps/common/DiRT Rally 2.0/`

The game loads `dxgi.dll` at startup. The proxy forwards to the real `dxgi.dll` in `System32`, then loads `dr2hook_core.dll` from the same folder. No external injector is required.

Step-by-step notes for Windows and Linux / Steam Deck: [docs/INSTALL.md](docs/INSTALL.md).

## Physics tick harness (opt-in)

Reverse-engineering and instrumentation only. Hooks on the EGO physics tick path, CSV logging of rig samples, optional queued **memory writes**, and optional **experimental native API** calls (`SetTransform`, `SetLinVel`, `SetAngVel`, `Commit` at spec VAs). Everything is **off by default**.

| Enable | Env / INI |
| --- | --- |
| Instrumentation + CSV | `DR2HOOK_PHYSICS_HARNESS=1` or `instrumentation=1` |
| Queued rig writes | `DR2HOOK_PHYSICS_HARNESS_WRITES=1` or `writes=1` |
| Experimental native calls | `DR2HOOK_PHYSICS_HARNESS_EXPERIMENTAL_NATIVE=1` or `experimental_native=1` |
| Self-test (no real writes) | `DR2HOOK_PHYSICS_HARNESS_SELF_TEST=1` or `self_test=1` |

Native calls require instrumentation, the experimental flag, and `SafetyGuard::CanWriteState()`. They run only on chosen physics boundaries (never on `Present`). Details: [docs/physics_tick_harness.md](docs/physics_tick_harness.md).

## Build

Requirements: CMake 3.20+, and either MinGW-w64 (Linux cross-compile) or Visual Studio 2022 (MSVC, x64, C++20).

Linux, full check (unit tests, then the release package):

```bash
bash scripts/verify_release.sh
```

Linux, release DLLs only:

```bash
bash scripts/package_release.sh
```

The package contains `dxgi.dll`, `dr2hook_core.dll`, and `mods/`. Both binaries are statically linked (no `libstdc++` or `libgcc` next to the game).

Windows:

```bash
mkdir build && cd build
cmake .. -A x64
cmake --build . --config Release --target dxgi --target dr2hook_core
```

## Vehicle black box (`dr2rec`)

`scripts/dr2rec` records a local session and analyzes it afterward. The capture stores raw bytes; field names stay limited to what is already confirmed. It does not write game memory and does not contact RaceNet. Manual: [docs/BLACKBOX.md](docs/BLACKBOX.md).

## Writing a mod

Mods are Lua 5.4 scripts under `mods/<name>/` with a `mod.json` manifest. The shipped example is `mods/practice_mode/`.

Supported lifecycle hooks today: `onInit`, `onTick(dt)`, and `onKeyDown(keyCode)` (see [docs/MODDING_GUIDE.md](docs/MODDING_GUIDE.md)). `onStageStart` is registered in the loader but **not dispatched from gameplay yet** (only unit tests call it).

Lua bindings: `Player` (`getPosition`, `setPosition`, `getVelocity`, `setVelocity`, `getState`, `setState`), `Safety.isRestrictedMode()`, `UI.notify`, and `Menu` (native pause submenu). There is **no** `Player.getVehicleTelemetry()` in Lua; engine RPM and gear appear in the **Diagnostics** overlay via native code.

```lua
function onInit()
    print("[MyMod] ready")
end

function onTick(dt)
    -- once per rendered frame (Present)
end

function onKeyDown(keyCode)
    if keyCode == 0x74 then -- F5
        local saved = Player.getState()
        Player.setState(saved, "normal")
    end
end
```

`Player.setPosition`, `Player.setVelocity`, and `Player.setState` call `SafetyGuard::CanWriteState()` before writing. The guard can restrict writes by session mode when a session address is configured and permissive mode is off; **v0.1.0 currently starts the core with permissive mode enabled**, so restrictions are not enforced in-game until that wiring is finished.

Full API reference: [docs/MODDING_GUIDE.md](docs/MODDING_GUIDE.md). Pointer maps and offsets: [docs/reverse_engineering/README.md](docs/reverse_engineering/README.md).

## How it is put together

```
dirtrally2.exe
    └── dxgi.dll          stays loaded: DXGI proxy, Present, WndProc, NetworkGuard
            └── dr2hook_core.dll    overlay, telemetry, Lua, savestate (F8 reloads this file)
```

| Path | Role |
| --- | --- |
| `src/proxy/dxgi_proxy.cpp` | DXGI proxy |
| `src/core/hooks.cpp` | DirectX 11 `Present`, window procedure, and Winsock hooks (resident in proxy) |
| `src/core/host.cpp` | Loads `dr2hook_core.dll` and reloads it on F8 |
| `src/core/core_module.cpp` | Reloadable entry: frame, input, init, shutdown |
| `src/core/safety.cpp` | Session write gate (fail-closed when configured) |
| `src/core/player.cpp` | Vehicle telemetry reads and checkpoint restore |
| `src/core/physics_tick_harness.cpp` | Opt-in physics instrumentation |
| `src/script/` | Lua runtime and mod loading |
| `src/ui/overlay.cpp` | ImGui overlay |
| `docs/` | Install, modding, reverse engineering, harness, architecture notes |

**F8** (or **Reload Native Core** on the Mods tab) shuts the core down, unloads it, and loads a fresh copy. Replace `dr2hook_core.dll` next to `dirtrally2.exe` and press **F8**. The host copies that file to `dr2hook_core.N.dll` before loading so the build can overwrite `dr2hook_core.dll` while the game is running. The C++ checkpoint is cleared and Lua starts over. **F8** is handled in the proxy path and is not delivered to mods.

Changes to `dxgi.dll` still require a game restart. The proxy, MinHook, `Present`, `WndProc`, and `NetworkGuard` stay mapped.

## Status

- Evidence-graded RE docs, INV-01 investigation tree, and `dr2rec` capture tooling
- Opt-in physics tick harness (default off)
- Proxy DLL with dynamic forward to `System32`
- Reloadable `dr2hook_core.dll` (**F8**) for overlay, telemetry, savestate, and Lua
- `Present` and `WndProc` hooks, ImGui overlay
- `NetworkGuard` mandatory network refusal while the proxy is loaded
- Lua 5.4 sandbox with `Player`, `Safety`, `UI`, and `Menu` bindings
- Practice checkpoints (Normal / With Momentum) via C++ savestate and shipped Lua mod
- Automated tests, release scripts, and documentation

## Fair play

DR2Hook is aimed at offline practice, telemetry research, and community mods — not leaderboard or RaceNet competition.

**Network.** While `dxgi.dll` is loaded, `NetworkGuard` hooks `ws2_32.dll` and refuses remote traffic in the open:

- `getaddrinfo` / `GetAddrInfoW` for any name other than localhost returns “name not found” (logged).
- `connect` outside `127.0.0.0/8` and IPv6 loopback returns `WSAECONNREFUSED` (10061).
- Localhost is left alone for local tools (SimHub, motion rigs, telemetry on `127.0.0.1`).

There is no menu or script API to disable this. Online play returns only after you exit and remove `dxgi.dll` and `dr2hook_core.dll`.

**Memory writes.** `SafetyGuard` is designed to allow position, velocity, and full-state restores only in DirtFish, offline time trial, and custom offline championships when session detection is active and permissive mode is off. Unknown mode, failed read, or missing pointer is treated as not allowed. See the mod-loader note above for current v0.1.0 behavior.

## License

MIT. See `LICENSE`.
