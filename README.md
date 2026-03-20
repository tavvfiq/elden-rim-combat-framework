# Elden Rim Combat Framework (ERCF)

ERCF is an SKSE plugin that implements an Elden Ring-inspired combat model for Skyrim:

- typed damage with layered mitigation (Defense -> Absorption)
- status buildup and proc events
- data-driven tuning through ESP-authored effects and TOML config

## Project Layout

- `src/` plugin runtime code (SKSE, combat math, routing, messaging, PrismaUI bridge)
- `tests/` standalone C++ test harness for math behavior
- `view/ERCF/` PrismaUI HTML/CSS/JS HUD
- `concept.md` design intent
- `implementation.md` implementation roadmap
- `ercf_esp_runtime_contract.md` ESP/runtime contract
- `ercf.toml.example` runtime tuning example

## Prerequisites

- Windows + Visual Studio C++ build tools
- `xmake` (2.8.2 or newer)
- local checkout of [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG)

## Configure

Set the path to your local CommonLibSSE-NG checkout:

```powershell
xmake f --commonlibsse_dir="D:/Modding/CommonLibSSE-NG"
```

If your repo is next to this one as `../CommonLibSSE-NG`, the default works without extra flags.

## Build

Build all targets:

```powershell
xmake
```

Build plugin only:

```powershell
xmake build ercf
```

Build tests only:

```powershell
xmake build combat_math_tests
```

## Run Tests

```powershell
xmake run combat_math_tests
```

Expected output:

```text
CombatMath tests passed.
```
