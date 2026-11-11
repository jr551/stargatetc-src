# StargateTC — source port (public source mirror)

C/C++ game-logic source for **StargateTC**, a Stargate SG-1 total conversion for
Half-Life / GoldSrc (and Xash3D FWGS). This tree is the server (`dlls/`) and
client (`cl_dll/`) game code, the shared game modules, the vendored bot code and
the CMake build — enough to rebuild `hl.so` / `client.so` / `hl.dll` yourself.

This is a **one-way source mirror**: it is generated automatically from the
upstream development tree and only ever contains buildable source plus the
licence/notice files needed to redistribute it. Development, CI, tests and
release packaging do not live here, and issues on this mirror are not monitored.

No game assets are included or distributed by this repository — no maps, models,
sounds, textures or compiled binaries. You need your own Half-Life installation
(and the StargateTC game data package) to run anything built from this source.

## Build

32-bit Linux (same shape the game loads):

```bash
sudo apt-get install cmake ninja-build gcc-multilib g++-multilib
git clone --recursive https://github.com/jr551/stargatetc-src
cd stargatetc-src
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -D64BIT=OFF -B build -S .
cmake --build build --target all
```

Output: `hl.so` (server) and `client.so` (client) under `build/`. For Windows,
build the same targets with a 32-bit MinGW/GCC toolchain.

`-DBUILD_JK_BOTTI=ON` (default) links the bundled jk_botti bots into the server
library instead of loading them as a Metamod plugin.

## Layout

| Path | What |
|---|---|
| `dlls/` | Server game logic (`hl`) |
| `cl_dll/` | Client game logic (`client`) |
| `game_shared/`, `pm_shared/` | Shared game / player-movement code |
| `common/`, `public/`, `engine/` | Engine glue and shared headers |
| `external/` | Vendored headers (SDL2, OpenBSD strlcpy/strlcat) |
| `bots/jk_botti/` | Vendored jk_botti bot source (linked into `hl`) |
| `freevgui/` | Public submodule — VGUI1 replacement (FWGS) |
| `cmake/`, `CMakeLists.txt`, `mod_options.txt` | Build system |
| `tests/` | C/C++ test harnesses (stub engine, HUD/director probes) |

## Licensing and attribution

- **Half-Life 1 SDK** — `LICENSE` (Valve Corp.). Redistribution in source and
  object-code form is permitted for free, non-commercial use only, and this
  licence file must ship with any distribution. This repository is free to use.
- **jk_botti** — BSD licence, vendored from Bots-United/jk_botti; see
  `release/notices/jk_botti.txt` and `UPSTREAM.md`.
- **Metamod** — notice required and included: `release/notices/metamod.txt`.
- **zlib** — Gailly/Adler notice: `release/notices/zlib.txt`.
- **Xash3D FWGS / hlsdk-xash3d** — MIT (Alibek Omarov and contributors).
- **freevgui** — public submodule (FWGS).
- **StargateTC** — the original total conversion and its assets remain the
  property of their authors; Stargate SG-1 is MGM/Amazon IP. This tree contains
  no StargateTC assets. The `sgtc_*` game-logic sources are a compatibility
  reimplementation of the mod's behaviour, reconstructed from analysis of the
  shipped Linux game library; their inline comments record the reference points
  used. Nothing here is an official release, and no rights in the original mod
  or the franchise are claimed or granted.
- **StargateTC credits** (from the mod itself): Axel, Keyven, Kegeruneku,
  Endolaylith, Brophis, Darkkyp, Sinock, ApOpHls, Or3L, Bad Lt, NemexIS,
  LeGendS Seagal, Trygee, Little, e-t172, Zerterf, Im_not_human, Waldoo, ZePrOuT
  — plus beta testers, and the Half-Life/Xash3D communities.

If you are a rights-holder and want something removed, open an issue here and it
will be handled.
