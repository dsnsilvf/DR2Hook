# DR2 ModLoader

A mod loader and hook framework for **DiRT Rally 2.0**. It is meant for offline practice, telemetry, and community mods: save a point on a long stage, put the car back there, and repeat the corner without restarting the whole special.

Press **Insert** in-game to open the overlay. The interface is in English.

This project is not affiliated with Codemasters or Electronic Arts.

## Practice Mode

Rally stages are long. A corner you want to drill can sit ten minutes into the stage. Practice Mode stores the car and puts it back.

| Key | Action |
| --- | --- |
| **F5** | Save a checkpoint: position, orientation, linear velocity, and angular velocity. |
| **F6** | Restore **Normal**. The car returns to the saved pose, speeds are cleared, and the suspension is settled so it starts stable. |
| **F7** | Restore **With Momentum**. Position and orientation come back, and so do the linear and angular velocity from the moment you saved. |

The same controls are on the **Practice Mode** tab of the overlay.

Writes only run in offline practice sessions: DirtFish, offline time trial, and custom offline championships. See [Fair play](#fair-play) at the end of this file.

## Overlay

**Insert** toggles the menu.

- **Diagnostics** — engine and drivetrain (RPM, gear, speed), position, linear and angular velocity, suspension contact on all four wheels, and whether the player, container, and physics rig pointers are live.
- **Mods** — scripts loaded from `mods/`, with version, running state, and hot reload.
- **Practice Mode** — save, restore, and choose Normal or With Momentum.

## Install

Build `dxgi.dll` (see below) and copy it, together with the `mods/` folder, into the game directory:

`[SteamLibrary]/steamapps/common/DiRT Rally 2.0/`

The game loads `dxgi.dll` on startup. Calls are forwarded to the real `dxgi.dll` in `System32`. No external injector is required.

Step-by-step notes for Windows and Linux / Steam Deck are in [docs/INSTALL.md](docs/INSTALL.md).

## Build

Requirements: CMake 3.20 or newer, and either MinGW-w64 (Linux cross-compile) or Visual Studio 2022 (MSVC, x64, C++20).

Linux (runs the unit tests, then builds the release DLL):

```bash
bash build_release.sh
```

Windows:

```bash
mkdir build && cd build
cmake .. -A x64
cmake --build . --config Release
```

## Writing a mod

Mods are Lua 5.4 scripts under `mods/<name>/`, with a `mod.json` manifest. The shipped example is `mods/practice_mode/`.

```lua
function onInit()
    print("[MyMod] ready")
end

function onStageStart(stage)
    print("Stage: " .. stage.name)
end

function onTick(dt)
    -- once per rendered frame
end

function onKeyDown(keyCode)
    -- WndProc hook; 0x74 is F5
end

local telem = Player.getVehicleTelemetry()
-- telem.rpm, telem.gear, telem.speedKmh, telem.torque, telem.throttle

local saved = Player.getState()
Player.setState(saved, "normal")     -- stopped, suspension settled
Player.setState(saved, "momentum")   -- keeps linear and angular velocity
```

`Player.setPosition`, `Player.setVelocity`, and `Player.setState` are checked in native C++ before any write. If the session is not a known offline practice mode, or if the session cannot be read, the write is rejected.

The full API is in [docs/MODDING_GUIDE.md](docs/MODDING_GUIDE.md). Pointer maps and offsets are in [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md).

## How it is put together

```
dirtrally2.exe
    └── dxgi.dll          proxy; forwards to System32, starts the loader
            ├── Present + WndProc hooks   overlay and per-frame tick
            ├── Lua 5.4                   mods/ scripts
            ├── SafetyGuard               rejects memory writes outside offline practice
            └── NetworkGuard              refuses remote sockets; see Fair play
```

| Path | Role |
| --- | --- |
| `src/proxy/dxgi_proxy.cpp` | DXGI proxy |
| `src/core/hooks.cpp` | DirectX 11 `Present`, window procedure, and Winsock hooks |
| `src/core/safety.cpp` | Fail-closed session check |
| `src/core/player.cpp` | Vehicle telemetry and checkpoint restore |
| `src/script/` | Lua runtime and mod loading |
| `src/ui/overlay.cpp` | ImGui overlay |
| `docs/` | Install guide, modding guide, reverse-engineering notes, architecture decisions |

## Status

- Proxy DLL with dynamic forward to `System32` (no `.def` dependency)
- `Present` and `WndProc` hooks, ImGui overlay
- Fail-closed safety gate and mandatory network refusal
- Lua 5.4 sandbox with `Player`, `Safety`, and `UI` bindings
- Practice Mode: Normal and With Momentum
- Automated test suite, release check, and docs

## Fair play

This loader is for offline practice, telemetry, and mods. It is not a tool for leaderboards, daily or weekly challenges, clubs, or any ranked RaceNet session. Using it to alter a competitive result is out of scope, and the loader is built so that path does not work.

Two checks enforce that, and both are documented here on purpose. Nothing in this project pretends to be a normal online client, spoofs a server, or hides the fact that the hooks are installed.

**Network.** While `dxgi.dll` is loaded, `NetworkGuard` hooks `ws2_32.dll` and refuses remote traffic in the open:

- `getaddrinfo` / `GetAddrInfoW` for any name other than localhost returns “name not found”. The refused name is written to the log.
- `connect` to any address outside `127.0.0.0/8` and IPv6 loopback returns `WSAECONNREFUSED` (10061). The game sees a refused connection. The call is not dropped silently and the destination is not rewritten.
- Localhost is left alone, so local tools (SimHub, motion rigs, telemetry on `127.0.0.1`) still work.

There is no menu option and no script API to turn this off. Online play returns only after you exit the game and remove `dxgi.dll`.

**Memory writes.** `SafetyGuard` re-checks the session on every write. Position, velocity, and full-state restores are allowed only in DirtFish, offline time trial, and custom offline championships. An unknown mode, a failed read, or a missing pointer is treated as “not allowed”.

## License

MIT. See `LICENSE`.
