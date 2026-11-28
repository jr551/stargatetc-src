# Playtest scripts

Scripted checks that run without a display: host harnesses, the stub-engine logic suites,
headless dedicated-server scenarios and an offscreen GUI client. They were used for the
2026-11-28 integration pass. Paths default to the playtest install
(`/home/deck/Projects/stargatetc-playtest/stargatetc-linux-x86`); set `SGTC_INSTALL` to change it.

## Build

The game libraries must be 32-bit and match the engine (Xash3D FWGS 0.21):

    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -D64BIT=OFF -B build -S .
    cmake --build build

A second tree with `-DSGTC_EXPORT_ALL_SYMBOLS=ON` is needed only for the stub-engine logic
suites (`botlogic`, `gatedial`, `botedict`, `botclass`, `botevent`, `bottac`), which look up
internal symbols in `hl.so`:

    gcc -m32 -O1 -o stub_engine tests/stub_engine.c -ldl
    ./stub_engine build-exports/dlls/hl.so botlogic      # 116 checks across the six suites

## Host harnesses (no engine)

    tests/playtest/host_harnesses.sh [install dir]

Fire-patch cap, gate timing, bot edict budget, bot persona, bot stamina, bot learning, HPB waypoint reader, director core, font fallback, the scheme fonts
at six VGUI tiers, third-person camera, l10n parser, the command menu data and the console
and HUD font lumps (`tests/qfont_wad_regression.py`).

## Private install copy

Several engines on one install fight over `config.cfg`, screenshots and the game libraries.
Make a hard-linked private copy per run (real copies of everything the engine writes):

    tests/playtest/private_copy.sh /home/deck/Projects/stargatetc-playtest-mine build

## Dedicated-server scenarios

    tests/playtest/run_dedicated.sh <install copy> <port> <scenario> <map> <timeout> <log>
    tests/playtest/check_dedicated.py wepfire|grenades|botkill|clean <log>

| scenario | map | what it covers |
| --- | --- | --- |
| `wepfire_late` | `sg_nox` | `wepfireall`: every weapon class given to a bot and both attacks fired (31 given, 31 primary, 11 secondary). Runs after 45 s because the knife deploy pad is an absolute time of 40.07 s. |
| `grenadeall` | `sg_nox` | `grenadethrowall`: the six grenade classes pin on press and throw on release. |
| `botkill` | `sg_nox` | `botkill`: a lethal bullet from one bot to the next at 40 s and 90 s, respawn in between. |
| `mapsweep` | any of the 44 maps | four bots, 30 s, `info` at 12 s and 24 s; a load and clean-shutdown check per map (`check_dedicated.py clean`). |
| `flash` | `sg_nox` | `flashtest`: an `info_flash` spawned 300, 500 and 700 units above a bot must survive its own Spawn (it used to be removed when no floor was within 256 units, so that flashbang never flashed). |
| `long` | `sg_pyramide` | 275 s soak with Zat duels and a C4 drop and detonation. |

`check_dedicated.py quiet <log>` fails a dedicated log that has unknown-command, late-precache or fell-out-of-level warnings.

`ALERT( at_console )` output does not reach the dedicated log. The harness lines come from
`test_out` in `dlls/sgtc_testcmd.cpp`, which writes to stdout.

## GUI client (offscreen)

    tests/playtest/gui_scenarios.py map|menus|chain|weapons <userconfig.cfg> [args]
    DEV=2 tests/playtest/run_gui.sh <install copy> <w> <h> <port> <timeout> <log>
    tests/playtest/gui_map_sweep.sh <install copy> <port> <outdir> <map> ...

The engine ignores `+exec` and `+map` in GUI mode, so a scenario goes in
`<install>/stargatetc/userconfig.cfg`, which `gui_scenarios.py` writes. Screenshots land in
`<install>/stargatetc/scrshots`.

- `menus`: briefing, team and class, MOTD, help, loadout and pack screens (`vguimenuyoyo <id>`).
  Run it at 640x480, 800x600, 1024x768, 1280x800 and 1920x1080.
- `chain`: opens menus 5, 11 and 10, then `autopick`. Before the menu chain fix this hung the
  client at 100% CPU. A healthy run ends with `EXIT=0` and both `STEP_` markers in the log.
- `weapons`: gives, selects and fires every weapon in a list with `sgtc_fxlog 1`, so the log
  shows one `SGTCFX` line per client effect.
- `flash`: `flashair` lifts the host into open air and spawns an `info_flash` at the eyes; take the two screenshots it
  writes and compare their luma (`ffmpeg -vf signalstats`): about 90 before and above 200 after on the fixed code, 76 after on
  the old one. `check_dedicated.py flashair` checks the log line.
- `stamina`: sets the host's stamina to 20 and samples it while standing still; `check_dedicated.py stamina`
  fails when it does not come back (the half-second think tail never ran before d9c3e12).
- `map`: used by `gui_map_sweep.sh` for the per-map client check.

Tips: `sgtc_timer_dump` prints the HUD clock mode (1 = player HUD, 2 = spectator clock only),
`sys_timescale 12` speeds up the round and pack timers, `classe 2` or `autopick` joins from the
console, and `sgtc_test_enable 1` turns on the developer JSON lines (`SGTC_PACKAUTO`,
`SGTC_BOT_LOADOUT`, `SGTC_SP_ENEMIES`).

Do not use `pkill -f xash3d`: it kills other sessions' engines. Stop a run by its pid or let
the timeout end it.
