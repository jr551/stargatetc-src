# Making the bots play like players

What changed in the bot layer on 2026-11-28, why, and how each part was checked.
Source: `bots/jk_botti/` (vendored jk_botti plus the SGTC additions) and `dlls/sgtc_bots*.cpp`.

## What a baseline match looked like

Six bots on `sg_nox`, 130 s, default settings: no bot left a 100-300 unit box, nobody
scored, 127 "stuck, jump" events, and the bots were named `hgrunt`, `barney`,
`scientist` (their model names) and never said a word. Three separate causes.

## 1. Navigation: use the maps' authored waypoints

Every payload map ships `maps/<map>.HPB_wpt`, an HPB_bot waypoint file laid out by the
map makers (55-468 nodes; 22 maps). Nothing read it. The jk_botti file the install
generates from spawn points holds about ten nodes, so most of a map was outside any
bot's reach.

* `waypoint_hpb.cpp` parses the HPB file (engine free; `tests/host/hpb_waypoint_harness.cpp`
  runs 65 checks, including every shipped file).
* `waypoint.cpp` (`WaypointImportHPB`) appends its nodes and authored paths when the
  jk_botti graph is the smaller one, drops nodes that are now inside geometry (the map
  may have been recompiled), links the nodes the jk file already had (spawn and item
  nodes) in by the usual reachability test, and rebuilds the route matrix. Only crouch
  and ladder flags are imported; item flags stay with the spawn-object scan because
  jk_botti checks them against live entities.
* File format, for the next person: 52 byte header (`HPB_bot\0`, version 4, flags, node
  count, map name[32]); nodes `{int flags; float x,y,z}`; then per node a **short** path
  count and that many short indices. Flags: crouch `1<<3`, ladder `1<<4`, health `1<<6`,
  deleted `1<<31`.
* A bot that has waypoints on the map but cannot reach any (a sparse graph, or one knocked
  off it) now roams on its own after 3 s instead of standing and jumping at its spawn.
  The roam picks the least-walked of a few candidate directions (`bot_learn.cpp`), so a
  bot on a map with no authored graph spreads out of its room.

Cvar: `sgtc_bot_hpb` (default 1).

## 2. Stamina

Moving costs 10 stamina every half second (`SGTC_TickMoveLatchAndStaminaDrain`), and
below 20 the game clamps `maxspeed` to 10 u/s (`SGTC_TickShieldEnergyAndMaxspeed`). A
person stops to get their breath back; a bot that keeps pushing forward stays pinned at a
crawl for the rest of the match. That, not navigation, was why the first fix alone did
not make them move.

* `dlls/sgtc_bots_stamina.h` is the pure rule: a calm bot stops and crouches below 30
  stamina and moves on at 70% of its health; a bot in a fight keeps moving once it is
  above 24; a bot below 40 health does not rest (stamina is capped near health, so a rest
  cannot help; it should be looking for a medic). A host simulation
  (`tests/host/bot_stamina_harness.cpp`) shows a bot following the rule moving about 45%
  of the time and never pinned, and the same bot without it pinned 97% of the time.
* `dlls/sgtc_bots_stamina.cpp` applies it before the move is sent. Aim and fire bits are
  left alone.

Cvar: `sgtc_bot_stamina` (default 1).

### Core bug found on the way (not fixed here, affects people too)

The game's stamina **regen** (`SGTC_TickSgtcThinkTail`, +2 to +10 per half second) never
fires. `m_flSgtcTailTick = flNow + 0.5` is executed on every call, outside the
`if( bTailTick )` block, so the 0.5 s gate is pushed forward every frame and never
reopens after the first one. Traced in a live run: the deadline reads `now + 0.5` on every
think. Nothing else restores stamina except a medic, `goaregen` or a talent, so a player
who has run it down stays at the 10 u/s clamp.

It was left alone on purpose. Moving the line inside the block would make the whole
0.5 s tail run for the first time, and that includes a branch that, as ported, starts a
random grey `UTIL_ScreenFade` every tick (`!SGTC_FloatEq( m_flSgtcLatch3fc, flNow )` is
true for the default latch). The reverse-engineered source needs a look before the tail is
turned on.

**Update (goal/playtest-fixes, d9c3e12):** the gate is fixed. The re-arm now sits at the end of
the gated block, so the tail runs every half second and stamina regenerates (a host with stamina
set to 20 and standing still went 20 -> 105 in 11 s; it stayed 20 before). Moving the line alone
did bring up the grey fade you describe, so the drug effect now runs only while the +0x3fc
deadline is in the future (narco gas and `drugs4me` set now + 15; the medic, goaregen, Teal'c and
zat-touch writes leave it in the past). `tests/playtest/gui_scenarios.py stamina` and
`check_dedicated.py stamina` cover it. The bots' own regen copy below retires itself as designed.

So the bots copy the regen themselves, using the game's own numbers
(`SGTC_Stamina_RegenPerTick`), and retire the copy for a bot once they see its stamina
rising by itself three ticks in a row, which means the game's tick has been fixed.

## 3. Names and chat

The install ships neither `addons/jk_botti/jk_botti_names.txt` nor `jk_botti_chat.txt`.

* `bot_persona.cpp` carries 64 gamer handles and about 130 chat lines as a fallback; a
  file, when present, still wins (`tests/host/bot_persona_harness.cpp`, 50 checks).
* Messages from different bots are paced at least 2.5 s apart and typed at a person's
  speed; idle chat comes every 2-6 minutes per bot instead of every 30 s.
* Bots answer a person: a hello gets a hello, a gg a gg, thanks "np", and a bot called by
  name says something back. One reply per message at most. A bot that is already in the
  game says hello when a person joins.
* Bots created without an explicit skill land one level either side of `sgtc_bot_skill`
  (`sgtc_bot_skill_spread`, default 1).

Test aids: `sgtc_bot_hear <text>` feeds a line to the bots as if a person had said it,
and the `sgtc_test` token `srv <server command>` lets a timed script drive any server
command.

## 4. What the bots learn from people

`bot_learn.cpp` (heat map in `bot_learn_core.cpp`, `tests/host/bot_learn_harness.cpp`, 45
checks):

* once a second it records where each living person is (more for a person moving than
  one standing still), and every death a person is part of, into 160-unit cells;
* the cells are saved per map as `waypoints/<map>.heat` next to the waypoint file, every
  two minutes and at map end, and loaded (aged by 10%) at the next start;
* a bot that has nothing to do (no enemy, no item to fetch) heads for a waypoint chosen by
  how busy the cells around it are, 70% of the time, and picks at random as before the
  rest. Bots with no data behave exactly as before.

How hard the bots are is **not** done here: the bot director (`dlls/sgtc_director.cpp`,
running every frame even with `sv_botdirector 0`) already rates each person and steers
every bot's skill toward a target. A first version of this work had a second skill
controller; it fought the director and was removed.

Cvar `sgtc_bot_learn` (default 1). `sgtc_bot_learn_test 1` makes bots stand in for people
so a headless run can exercise it; `sgtc_learn status|save|clear` prints or manages it.

## Checked

* Host harnesses: HPB reader 65, stamina rule 31, persona 50, heat map 45 checks.
* Stub-engine bot suites on an export build: 116/116 (`botlogic`, `gatedial`,
  `gatedialwrapper`, `botedict`, `botclass`, `botevent`, `bottac`).
* Live 120 s matches with 6-8 bots: all bots now cross the map (sampled path 3,700 to
  15,600 units against 100-300 before) and fight (up to 195 zat impacts and 63 deaths on
  `sg_temple`).
* Heat map: a run wrote 137 cells, the next run loaded 132 and picked goals from them.

## Things seen and not changed

* On `sg_temple` one Goa'uld bot spawn-camps the Tau'ri spawn with the Larve talent for
  dozens of kills. That is the talent and the spawn layout, not navigation.
* `sg2dm_*` maps have no authored waypoint file; bots there rely on the roam fallback.
