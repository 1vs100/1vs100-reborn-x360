# 1 vs 100 loader (Xbox 360)

A system DLL that lets the Xbox 360 game *1 vs 100* run against a custom revival backend instead of
the retired Xbox Live service. It patches the running title and XAM in memory: server lookups
point at the backend, the game session runs without Live, multiplayer traffic is relayed through
the backend's matchmaking service, and Marketplace content is found on loose installs.

| Component | What it is |
|-----------|------------|
| `1vs100Reborn.xex` | the system DLL (`sysdll/`), loaded by DashLaunch |
| `launcher.xex` | The game launcher (`launcher/`) that checks that the console is ready (profile signed in as user 0, avatar data installed), loads the DLL through DashLaunch, sets the launch data the game requires and starts the game |

**Supported title builds:**

- v2.0.1149.0 (retail, title id 584A07D1)
- v2.0.1317.0 (debug, title id 584A07E4)

## Quick start

Check the [Releases](https://github.com/1vs100/1vs100-reborn-x360) page and download the latest archive.

## How to compile?

Requirements: 
- Xbox 360 XDK (under Wine on Linux, or native on Windows)
- CMake ≥ 3.21
- Python 3

```
export XEDK=~/xdk                 # XDK root
cmake --preset xdk                # production build -> build/
cmake --build --preset xdk
```

`./build.sh` does the same.

| Preset | Build | Output |
|--------|-------|--------|
| `xdk` | production: functional patches only | `build/1vs100Reborn.xex` |
| `xdk-diagnostics` | adds the investigation-only hooks (log mirrors, probes, network trace) | `build-diagnostics/1vs100Reborn.xex` |

Every link also runs the project's checks: format strings, include layering, no build branches
outside `title/`, patch reference up to date, packaging, and the CRT-call audit. Host-side unit
tests:

```
cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
```

## Configuration

The launcher looks up each of these files, and `1vs100Reborn.xex` itself, **next to the launcher
first** (its `game:\` directory), then on the HDD root:

* `1vs100Reborn.ini`: the backend host and ports. Start from `1vs100Reborn.ini.example`.
* `1vs100Reborn.disable`: patches to switch off, by name. See `PATCHES.md`.

## Documentation

| Document | Contents |
|----------|----------|
| [`verification/README.md`](verification/README.md) | patch manifests and how to record one on hardware |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | conventions and the pull-request checklist |

## Support

If you are running into issues

- Check the issues / discussions sections of the Github repositories
- Visit our Discord: https://discord.gg/tV3ajhwFTf

## Disclaimer

1 vs 100, Xbox, Xbox 360 and Xbox LIVE are trademarks of their owners. This is an unofficial fan project, not affiliated with or endorsed by Microsoft or the game's developers.
