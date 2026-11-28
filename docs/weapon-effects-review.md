# Weapon effects and feel review

Branch `goal/weapon-effects-feel`, on top of `716dc00`. Scope: every weapon class registered in the game, 38 in all: the 31 MP arsenal weapons, `weapon_9mmAR` (an alias of the MP5 class), and six single-player shims. Nothing here is merged to `master`.

## How it was checked

- **Server, all MP arsenal weapons.** `wepfireall` on a dedicated server (`sg_nox`) gives each weapon, fires its primary, and fires its secondary where the class has one, logging each step. Sweep build: 38 classes registered, 31 given, 31 primaries fired, 11 secondaries fired, 0 retries.
- **Client, one GUI run per weapon.** Each run loads `sg_nox`, refills stamina, gives the weapon, sets its ammo to 5, fires the primary (or holds the secondary), and takes a screenshot 3 frames after the trigger. `sgtc_fxlog 1` prints one `SGTCFX <event> idx= origin= f1= i1=` line when each client event arrives. A receipt means the client handler ran.
- **Multiplayer runs with a real local player.** `maxplayers 4`, `sgtc_bot_team 1` and `motdfile nomotd.txt` (the server MOTD window is modal and blocks `+attack`), then `autopick` and `sgtc_loadout_done`, put the local player in the round as a class. `sgtc_test "unarmed 1 2"` then `kill` makes the player a teammate of the bots. A bot is held 100 units ahead with `sgtc_test movebot_2_-1600_1280_100` every frame, because bots keep walking. With `maxplayers 2` the round ends at once and firing is blocked. The frames are from the build at `adb4bed` with the repaired font WADs; the merged tree `70f94f8` was re-run for the events (`mp-medic16`: poison `i1=2`, heal `i1=1`; the regen shot missed that time).
- **Extra runs** for cases the sweep does not reach: the silenced P90, the charged Zat shot, medic self-heal, the larve detonation, the six shims, the gas release (three runs), and glstaff, Batsup and the satchel shim after the later changes.
- **Builds.** The sweep and the extra runs used r16 (`hl.so` md5 `1027e860…`, `client.so` md5 `3482aee5…`). The gas, glstaff and Batsup-second-swing runs used r17 (`hl.so` md5 `cc9efb1f…`). The satchel shim and the bolt-light removal were checked on r18 and r19 builds, on glstaff and the satchel shim only. The zatarc flash gate and the colormap test token are on r20. The MP5 click and the entity-count checks are on the later build.
- **Sound was measured, not heard.** The picture runs used `-nosound`. A separate set of runs recorded the engine's audio output (see "Sound, measured" below). Rows outside that section make no claim about sound beyond the file existing on disk.
- **Visible effects** come from single screenshot frames. "Not in frame" means the frame did not show the effect; it does not mean the effect is absent.
- **Evidence files** are in the test scratch folder, not the repo: run logs `one-<tag>.clean`, screenshots `one-shots/<tag>.png`, and the server log `dedi-final3.log`.

## Corrections to earlier notes

- **Client-only sprites were never precached (`ba3e7fe`).** The client looks up effect sprites with `EV_FindModelIndex`, which only sees models the server has precached. `lazer.spr`, `zattir.spr`, `lazerdot.spr` and `shieldglow.spr` were not precached, so the Zat and Tac beams, the Tac dot, and the medic, goaregen and shield glows were silently skipped. This is why the Zat beam, the medic and goaregen puffs and the shield glow never showed. The Tac dot note below was wrong: the red dot in those frames is the HUD crosshair.
- **Pellet decals and the muzzle light are visible from a fixed position.** Looking down at the floor (`sgtc_test tp -1700 1280 100 0 45`) shows the Spas and USAS pellet decals and the M16 muzzle light on the floor. In the default view at range they are not visible.
- **Gas grenades do release.** An earlier note said the canister never left the hand. That was a misreading: the canister still in hand is the next one (ammo 5 becomes 4). The release sends `events/throw.sc` with `i1=29`. The gas cloud itself was not captured in any frame.
- **The glstaff wash came from the bolt's light.** The big lance bolt's dynamic light was orange (255,180,60). At radius 72 it washed the rock near the bolt yellow and white, and at radius 40 it still did. The impact explosion and the laser beams were tested and were not the cause. The light is removed (`aa3e9da`); the glow sprite stays, and the glstaff frame is normal.

- **Teammates could not be healed (`ffdb3a9`).** Team no-collide (`SGTC_ShouldCollide`, `sgtc_team_nocollide 1` by default) also applies to traces. The medic and the regen weapon find their target with a trace, so a teammate was never hit and the heal branch could not run. A debug trace showed the ray passing the teammate 100 units ahead and ending at the world 1,220 units away; with `sgtc_team_nocollide 0` the same shot healed. A counter now makes `SGTC_ShouldCollide` answer 1 during these two traces only; movement collision is unchanged.
- **Target puffs were inside the target (`adb4bed`).** The server sends the target's origin, the middle of its body, so the puffs were mostly hidden, and a poison puff (a 32x48 sprite at scale 0.2) was a few pixels. They now sit 22 units toward the shooter at chest height, are scaled for the sprite, and grow with distance up to 3x.
- **The Reetou shot played twice for the shooter (`1f8888a`).** `v_reetou.mdl` already plays `weapons/reetou.wav` from its `tir1` and `tir1a` sequences (event 5004), so the primary was never silent for the shooter, and the client handler added in `6939f4f` played the same wav again. On the null-sink recording the shot was 1.5 times louder than on the build before the weapon work (rms 12958 against 8754) and clipped at full scale. The handler now plays it only for other players, who have no viewmodel event; the shooter's shot measures 0.93 and rms 8754, the same as before. Found by comparing against a baseline build (below).
- **The throw whoosh cut the Goa'uld grenade charge sound (`1d826a6`).** A new sound on an entity's channel cuts the one playing there. The device holds `weapons/gacencl.wav` (2.8 s) on `CHAN_WEAPON`, and the whoosh from `d942e81` used the same channel, so the charge sound stopped at the throw: it lasted 4 s on the baseline build and 2 s here, and matched its wav at 0.98 and 0.5. The whoosh now plays on `CHAN_AUTO`; the charge sound matches at 0.95 and lasts 4 s again, and the whoosh is still in the output.
- **Client hang in the menu chain (`1b22090`).** Found while spawning a local player for these runs. `CMenuPanel::SetNextMenu` appends at the tail of the chain, so the three unlink sites never removed their node, and `SgtcPanels_ShowMenu` looped forever when a briefing sat behind the team/class panel and a loadout menu arrived. Without the change 3 of 3 autopick runs hung; with it 8 of 8 passed. This is not a weapon effect, but it blocked every multiplayer run with a player.

## Missing game assets

These are referenced by the code but are in neither the repo nor the test game folders (`stargatetc/` and `valve/`). The engine logs them as "Could not load" at precache. They need to come from the game's asset set.

| Asset | Used by | What the player misses |
|---|---|---|
| `models/v_shield.mdl`, `models/p_shield.mdl`, `models/w_shield.mdl` | `weapon_shield` (`dlls/sgtc_wpn_shield.cpp`, client table `cl_dll/sgtc_weapons_client.cpp:49`) | The shield gun has no viewmodel or worldmodel. Its raise sound and glow still play. |
| `sprites/zatwave.spr` | `weapon_zat` (`dlls/sgtc_wpn_zat.cpp:96`, beam TEs at `:267`; projectile `dlls/sgtc_misc.cpp:73`) | The zat wave sprite is not drawn. |
| `sound/weapons/nightstrike.wav` | `weapon_coujaf` (`dlls/sgtc_wpn_coujaf.cpp:152`, `:294`) | The coujaf night-strike sound is silent. |
| `sound/weapons/flashbang.wav` | `weapon_flashbang` (`dlls/sgtc_wpn_flashbang.cpp:51`) | Precached but absent. The detonation sound is `common/null.wav` (`:88`), so the flashbang is silent at detonation. |
| `sound/weapons/timer.wav` | HUD (`cl_dll/status_icons.cpp:148`) | The HUD timer beep is silent. |

Every other `.wav` the SGTC code plays is present. Missing vehicle, radio and other non-weapon sounds are stock content and outside this review.

## Per-weapon table

"Receipt" is the client events in the run log. "On screen" is what the screenshot frame shows. "By design" means the source sends no client event for that action. Runs without a suffix are from the r16 sweep.

| Weapon (class) | Primary: receipt | Primary: on screen | Secondary: receipt | Secondary: on screen | Notes |
|---|---|---|---|---|---|
| `weapon_m16` (CM16) | `m16.sc` ×3; ×5 in the flash run | M16 held with scope; a warm tint on the near rock in the first frame (`m16flash-0`). Fired at the floor from a fixed position, the muzzle light warms the whole floor (`m16floor-0`). The flash sprite is there: ten consecutive frames of sustained fire all show it (`burst-m16`); the single frame 3 frames after the trigger had simply missed it | Zoom; no event by design | Zoomed view, scope visible | Muzzle light radius 32 (see commit 37e9a4f). The scope toggle now plays a click (`ca3e921`) |
| `weapon_p90` (CP90) | `p90.sc` ×5 | Flash sprite in every frame of sustained fire (`burst-p90`) | Silencer toggle and FOV; no event by design | Held | Silenced shot: `p90l.sc` ×1 (extra run). The silencer toggle now plays a click (`ca3e921`) |
| `weapon_psg1` (CPsg1) | `psg1.sc` ×1 | Before: no flash sprite in ten consecutive frames (`burst-psg1`), because `v_psg1.mdl` has no muzzle flash event. After: a flash star at the barrel (`burst-psg1b`), drawn by the handler except behind the scope (`25f8500`) | Zoom stages; no event by design | Not checked | Each zoom step now plays a click (`ca3e921`) |
| `weapon_m92s` (CM92s, Eagle) | `eagle.sc` ×3 | Flash sprite on each shot (`burst-m92s`) | None | — | |
| `weapon_usas` (CUsas) | `usas.sc` ×3; ×5 in the flash run | Muzzle flash visible at the first frame after the shot (`usasflash-0`). Pellet decals are visible when fired down at the floor from a fixed position (`usasfloor-0`) | None | — | Client draws 8 pellets at 1024 units, matching the server (commit 880a52c) |
| `weapon_spas` (CShotgun, stock) | `spas.sc` and `spas2.sc` ×1 | Pellet decals are visible when fired down at the floor from a fixed position (`spasfloor-0`); not visible in the default view at range | Stock secondary is empty | — | Client draws 8 pellets at 2048 units (commit 880a52c). Stock class |
| `weapon_mp5` (CMP5, stock) | None; stock `mp5.sc` is not logged | Held | Dry-fire click added (approved stock edit, `4596d74`); no projectile or ammo, as the reference | Held | Click not heard (`-nosound`) |
| `weapon_reetou` (CReetou) | `reetou.sc` ×1 | The projectile is in flight as a cyan ball ahead of the view (`reetouflight-0`) | `reetou2.sc` ×1 | Held; no effect in frame | |
| `weapon_zat` (CZat) | `zata.sc` ×1; charged `zatb.sc` ×1 (extra run) | Lightning beam from the muzzle to the target (`zatafter.png`). Before the sprite precache there was no beam (`zat.png`) | None | — | Zat wave sprite `zatwave.spr` still missing. Beam fixed by `ba3e7fe` |
| `weapon_zatarc` (CZatarc) | `zatarc.sc` ×1 | Normal on the r20 build; the impact flare still shows | Zoom; no event by design | Not checked | The zoom steps now play a click (`ace710a`). The tint was the full-alpha `Muz1` sprite at close hits (bisected). Now drawn only for hits beyond 300 units (`ff34e5c`). Before `zatarc.png` (r16), after `zatarc-fix.png` (r20) |
| `weapon_tacgun` (CTacgun) | `tac.sc` ×1, ×2 in the dot run | A red beam from the device to the rock on the shot, visible in the first frame: a thin streak at 1 unit (`tacafter-0`) and a clear band at 3 units, the change in `6ba79dd` (`tacwide-0`). The beam lasts 0.1 s, so frame 3 is past it. The red dot in earlier frames is the HUD crosshair; the dot itself is not clearly visible | HUD info cycle and `tacact.wav`; no event by design | Held; HUD change not confirmed | |
| `weapon_main` (CMainDieu, Energie) | `main.sc` ×1, `mains.sc` ×1 (stop) | Held; beam streak visible from the device | `main2.sc` ×1 | Hand only; no beam in frame | Live beam from an earlier commit (29e11d8) |
| `weapon_glstaff` (CBigLanceJaffa) | `biglance.sc` ×1 | Washed white and pink on r16 and r17 from the bolt's light. Normal on r19 (frame checked) | None | — | Light removed in `aa3e9da`; glow sprite kept. Before `glstaff.png` (r16), after `glstaff-nodl-0.png` (r19); against the baseline build in god mode, `god-base-glstaff` and `god-fin-glstaff`. A frame with no scene at all is the dead view: the staff's blast can kill the team 0 harness player |
| `weapon_lance` (CLanceJaffa) | `lance.sc` ×1 | Held; bolt trail visible | `lance2.sc` ×1 | Held; no bolt in frame | |
| `weapon_larve` (CLarve) | `larve.sc` ×1 | Larve held | Detonate owned larves; no event by design | The detonation shows as a grey concussion fade on a player within 128 units (`larvevis-3`) | The blast itself has sound and damage only |
| `weapon_invis` (CInvis) | `invis.sc` ×1 | Device held | Shield wall: no event by design; with `colormap9_1` it adds one entity and a whole-view yellow-green shield tint | Tint in `invisinfo-0` | Gate needs colormap bit 9 (test token `colormap9_1`, `e16e05c`) |
| `weapon_shield` (CShieldGun) | `shield.sc` ×1 | A gold glow at the view centre on the raise (`shieldafter.png`). Before the sprite precache only the team tint was there (`shield.png`). Viewmodel missing (asset) | None | — | Raise sound and glow from source. Missing assets |
| `weapon_batsup` (CBatonSupplice) | `batsup.sc` ×1; follow-up `batsup2.sc` ×1 with 36- and 40-frame gaps | Melee flare on the rock ahead | None | — | The follow-up needs the second swing 0.5–0.75 s after the first; 6, 25 and 50 frames miss it |
| `weapon_coujaf` (CCoujaf) | `coujaf.sc` ×1 (swing and miss) | Blade mid-swing; no hit in frame | None | — | Miss now sends the event (commit 7323428). `nightstrike.wav` missing |
| `weapon_couteau` (CCouteau) | `couteau.sc` ×1 and `throw.sc` ×1 (`i1=3`) | Knife held | `throw.sc` ×1 (`i1=3`) | Knife held; throw not in frame | |
| `weapon_gacgoa` (CGrenadeChoc) | `throw.sc` ×1 (`i1=10`) | Device held; throw not in frame | `throw.sc` ×1 (`i1=10`) | Held | |
| `weapon_flashbang` (CFlashBang) | `throw.sc` ×1 (`i1=0`) | Held; the detonation shows as a grey-white flash that fades (`flashvis-4`, `flashvis-5`) | None | — | `i1=0` is the flashbang's id by design. Detonation is the stock flash path (`CGrenade::ShootFlash`); `flashbang.wav` missing, so the detonation sound is silent |
| `weapon_handgrenade` (CHandGrenade, stock) | `throw.sc` ×1 (`i1=12`) from the ammo watch | Grenade held | None | — | Stock class, not edited |
| `weapon_c4` (CSgtcC4) | `throw.sc` ×1 (`i1=14`) | Two C4 units; a charge is on the ground ahead | Stock detonate; not run | — | Subclass of the stock satchel |
| `weapon_mine` (CTripmine, stock) | None; stock `tripfire.sc` is not logged | Mine held; placement not captured | None | — | Stock class, not edited |
| `weapon_mustardgrenade` | `throw.sc` ×1 (`i1=29`) on release | The cloud lands about 1,200 units ahead; before the change there was no mark there (`gasdbg-3`). After, a white burst is on the path (`gasburst-3`) | None | — | The cloud is now a radius-scaled sprite burst (`9228678`). It is small at range and fills more of the view up close; close-range frames not captured |
| `weapon_narcogrenade` | `throw.sc` ×1 (`i1=28`) on release | Same landing point, white burst (`narcoburst-2`). Before frames not captured for this gas | None | — | Same change as mustard |
| `weapon_nervegrenade` | `throw.sc` on release | Same landing point, white burst (`nerveburst-2`). Before frames not captured | None | — | Same change as mustard |
| `weapon_smokegrenade` | `throw.sc` on release | Same landing point, blue burst from the steam sprite (`smokeburst-2`). Before frames not captured | None | — | Same change as mustard |
| `weapon_medic` (CMedicx) | On a player in the crosshair, in the multiplayer runs: `medic.sc` `i1=2` (poison on an enemy) and `i1=1` (heal on a teammate) | Poison: blue puffs around the enemy's torso (`mp-medic10-1` to `-3`). Heal: a green aura column on the teammate (`mp-medic14-3` to `-5`). Before: a few pixels at the waist, mostly behind the arm (`mp-medic4-1`), and a heal on a teammate never applied | `medic.sc` ×1 (`i1=3`, self heal) | A large green puff in front of the view (`medicsec-0`). Before the sprite precache there was no puff (`medicpuff-0`) | The teammate heal could not run while `sgtc_team_nocollide` was 1, its default: the trace passed through teammates (`ffdb3a9`). The target puffs were hidden inside the body (`adb4bed`). Heal health 50 to 55 and poison health 100 to 99 were read from the player list |
| `weapon_goaregen` (CMedicxgoa) | `goaregen.sc` ×1 (`i1=1`, shooter, no target); in the multiplayer runs `i1=0` on a teammate | Shooter: a green puff in front of the view (`goaafter-0`); before the sprite precache only an orange glow at the palm (`goapuff-0`). Target: a green aura around the teammate (`mp-medic15-6`, `-7`); before the change a thin sliver at the hip (`mp-medic14-6`, `-7`) | None | — | Same teammate-trace bug as the medic, fixed in `ffdb3a9`. The regen on a teammate was read from the player list: health 50 to 55 |
| `weapon_9mmAR` (alias of CMP5) | As `weapon_mp5` | — | — | — | Same class as the MP5 row |
| `weapon_9mmhandgun` (SP shim → CM92s) | `eagle.sc` ×3 (`sp-9mmhandgun`) | Not checked | None | — | Single-player only |
| `weapon_crossbow` (SP shim → CPsg1) | `psg1.sc` ×1 | Not checked | None | — | Single-player only |
| `weapon_crowbar` (SP shim → CBatonSupplice) | `batsup.sc` ×1 | Not checked | None | — | Single-player only |
| `weapon_rpg` (SP shim → CLanceJaffa) | `lance.sc` ×1 | Not checked | None | — | Single-player only |
| `weapon_satchel` (SP shim → CSgtcC4) | `throw.sc` ×1 (`i1=14`, r18) | Not checked | — | — | Now derives from CSgtcC4 (`fd44f29`) |
| `weapon_shotgun` (SP shim → CShotgun) | `spas.sc` and `spas2.sc` ×1 | Not checked | — | — | Single-player only |

## Sound, measured

The engine's bundled SDL has only the alsa, disk and dummy audio drivers, so these runs use `SDL_AUDIODRIVER=alsa AUDIODEV=pulse PULSE_SINK=<null sink>`, and `parec` on the sink's monitor records what the engine outputs. Each case plays a marker (three `buttons/blip1.wav` plays, 12 frames apart), waits 90 frames, then does the action. Each wav is then located in the recording by normalized cross-correlation: 1.00 is an exact copy, and mixing with other sounds lowers it. Run `snd-snd2` on the merged tree `70f94f8`, single-player `sg_nox`.

| Case | Sound found | Match | After the marker's last blip |
|---|---|---|---|
| MP5 secondary | `weapons/357_cock1.wav` | 0.98 | +1.7 s; the only match in the run |
| Mustard gas pin pull | `weapons/gren_cock1.wav` | 0.92 | +1.6 s |
| Mustard release | a `swing` whoosh | 0.56 | +2.3 s, mixed with the grenade's own sounds |
| Reetou primary, shooter | `weapons/reetou.wav` | 0.93 (`snd6`) | +1.5 s. Same as before the weapon work: it comes from the viewmodel (see the corrections) |
| USAS shot, then reload | `weapons/reload1.wav` | 0.98 | +3.7 s (the shot has no template here) |
| Shield raise | `weapons/P90recharge.wav` | 0.98 | +1.6 s |
| Flashbang throw | a `swing` whoosh | 0.47 | +2.2 s |
| Hand grenade throw | `weapons/swing3.wav` | 1.00 | +2.2 s |
| M16 scope toggle (`snd4`) | `weapons/357_cock1.wav` | 0.95 | +1.5 s |
| P90 silencer toggle (`snd4`) | `weapons/357_cock1.wav` | 0.95 | +1.5 s |
| PSG1 zoom, two steps (`snd5`) | `weapons/357_cock1.wav` | 0.95, 0.95 | +1.8 s and 1.6 s later |
| Zatarc zoom, three steps (`sndzarc`) | `weapons/357_cock1.wav` | 0.95 each | +1.6 s, then every 1.6 s |
| Control: marker, no action | none of the above | none | none |

A second `reetou.wav` match (0.98) comes 3.4 s after the shot, on the baseline build too: the viewmodel's `tir1a` sequence plays it again.

The coujaf miss swing is audible now (a swing whoosh, peak rms 7308 in `allfin1`; silent in `allbase1`). Not measured: the coujaf `nightstrike.wav` (missing), the flashbang detonation (`flashbang.wav` is missing), the HUD timer (`timer.wav` is missing).

## Final sweep on the merged tree

43 single-player GUI runs on the final head `64b3da8` (tags `fin2-*`, `-nosound`; the same runs on `70f94f8` earlier, tags `fin-*`, gave the same receipts): the 31 primaries and 12 secondaries. All exited 0 and no log shows a signal. The primaries without a receipt are the ones the table already lists: the medic (no target in single player), the mine and the MP5 (stock events are not logged). The secondaries without a receipt are by design: the M16, PSG1 and zatarc zoom, the MP5 click, the invis wall, the P90 silencer and the Tac HUD cycle. `reetou2.sc`, `lance2.sc`, `main2.sc`, the gacgoa throw and the medic self heal each logged one receipt. The primary frames (contact sheet `sheet-fin2-primary.png` in the scratch folder) show no whole-view wash: zatarc is normal and the shield shows its gold glow. The glstaff frame depends on whether the harness player survives the staff's own blast: it is the dead view (flat grey) when the player dies, as in the baseline, and the wash fix is shown with `god` in the before and after table below. The Torri hand grenade viewmodel crash that the playtest session found (`v_grenadeT.mdl`, fixed on its branch) is not reachable from these runs, because the single-player player is on team 0.

The dedicated-server `wepfireall` sweep on the same head (`dedi-final5`): 38 classes registered, 31 given, 31 primaries and 11 secondaries fired, 0 self-kills, exit 0.

## Before and after, every weapon

The baseline is the commit before the weapon work (`1e15751`), built clean, plus only the test-only stamina, ammo and hurt tokens (`754c2ec`) so both builds get the same refills. The same 43 single-player runs were repeated on it (tags `base-*`, frames in `one-shots/`), and the audio cases were recorded on it. "SSIM" compares the two frames at 640x400: 1.00 is identical, and a lower value means the frames differ, which animation timing alone also causes, so each pair was also looked at. The baseline client has no `sgtc_fxlog`, so there are no receipts for it.

| Run | SSIM | Frame, before and after | Sound, before and after |
|---|---|---|---|
| batsup | 0.89 | Same flare | Same |
| coujaf | 1.00 | Same | The miss swing was silent; now a swing plays (`allbase1`, `allfin1`: silent, then peak rms 7308) |
| couteau | 1.00 | Same | Same |
| m92s | 0.96 | The shot warms the rock and lights the viewmodel (muzzle light) | Same |
| flashbang | 1.00 | Same (the flash is the detonation) | Throw whoosh added (silent, then audible) |
| gacgoa | 1.00 | Same | Whoosh added; the charge sound is intact after `1d826a6` |
| glstaff | 0.37 | **In god mode, so the player survives: before, the whole scene is blown out white, pink and orange; after, a normal scene** (`god-base-glstaff`, `god-fin-glstaff`). Without god mode the staff's own blast kills the single-player harness player (team 0), and the frame is then the dead view, flat grey with only the HUD, in both builds (`base-glstaff`, `fin2-glstaff`; health is negative in the log). It is not a rendering fault, and the older `fin-glstaff` was a normal frame only because that run survived with 24 health | Same |
| goaregen | 0.95 | **Before: nothing. After: a green puff in front of the view** | Same |
| handgrenade | 0.53 | Animation timing differs; the thrown grenade is in the air after | Throw whoosh added (swing match 1.00, none before) |
| invis | 0.99 | Same | Same |
| lance | 0.36 | Same composition | Same |
| larve | 1.00 | Same | Same |
| m16 | 0.47 | Viewmodel lit by the muzzle light | Same |
| main | 0.98 | Same | Same |
| medic | 0.77 | Same: no target in single player | Same |
| mine | 1.00 | Same | Same |
| mp5 | 0.67 | Viewmodel lit by the muzzle light | Same |
| mustardgrenade | 1.00 | Same at this timing; the cloud lands later (gas rows above) | Pin pull (`gren_cock1.wav`, 0.12 before, 0.92 after) and whoosh added |
| narcogrenade | 0.28 | The baseline camera sits at a different yaw; no effect in either | As mustard |
| nervegrenade, smokegrenade | 1.00 | Same | As mustard |
| p90 | 0.28 | The baseline camera sits at a different yaw | Same |
| psg1 | 0.55 | Viewmodel lit by the muzzle light | Same |
| reetou | 0.40 | Same pose | The shooter hears the same shot (0.93 both); other players now hear it too |
| shield | 0.93 | **Before: only the team tint. After: a gold glow column** | Raise cue added (`P90recharge.wav`, 0.07 before, 0.98 after) |
| spas | 0.99 | Viewmodel lit by the muzzle light | Same |
| tacgun | 0.99 | **Before: no beam. After: a red beam to the ground** | Same |
| usas | 0.98 | Muzzle flash and a lit viewmodel | Reload sound added (`reload1.wav`, 0.13 before, 0.98 after) |
| zatarc | 0.88 | **Before: no arc. After: a lightning arc (and a blue cast on the hand)** | Same |
| zat | 0.96 | **Before: no beam. After: a lightning beam** | Same |
| c4 | 1.00 | Same | Throw whoosh added |
| lance, main, reetou, gacgoa secondaries | 0.96, 0.43, 1.00, 1.00 | Same | Same (the gacgoa secondary also adds the throw whoosh) |
| p90 secondary | 1.00 | Same | Silencer click added (0.25 before, 0.95 after) |
| m16 secondary | 1.00 | Same | Scope click added (0.25 before, 0.95 after) |
| psg1 secondary | 0.88 | Same | Zoom click added (0.25 before, 0.95 after) |
| mp5 secondary | 0.96 | Same | Dry-fire click added (0.26 before, 0.98 after) |
| zatarc secondary | 0.70 | Zoom ring; the hand is tinted as in the primary | Same |
| medic secondary | 0.63 | **Before: nothing. After: a green self-heal puff** | Same |
| invis, tacgun secondaries | 1.00, 1.00 | Same | Same |

Heal, poison and regen on a target have no single-player frame; their before and after are the multiplayer frames in the medic and goaregen rows (`mp-medic4-1` against `mp-medic10-1` for poison, and the teammate heal that never applied before).

## Stock-based classes (no stock file edited)

- **`weapon_handgrenade`** (`dlls/handgrenade.cpp`). The throw cue comes from `dlls/sgtc_throwfx.cpp`, which watches each player's handgrenade ammo once per frame and sends `events/throw.sc` when it drops.
- **`weapon_c4`** (`CSgtcC4`, `dlls/sgtc_wpn_c4.cpp`). A subclass of the stock satchel, linked in `dlls/sgtc_links.cpp`. The throw cue fires on drop; detonation is stock.
- **`weapon_mp5`, `weapon_9mmAR`** (`dlls/mp5.cpp`). The stock `mp5.sc` and `mp52.sc` events play; the SGTC client cannot log them. The secondary is a cooldown-only stub.
- **`weapon_spas`** (`dlls/shotgun.cpp`). The client draws the server's 8 pellets. The stock secondary is empty.
- **`weapon_mine`** (`dlls/tripmine.cpp`). Stock `tripfire.sc`, which the SGTC client cannot log.

## Single-player shims

Five of the six single-player shims map onto MP classes (`dlls/sp_shims.cpp`), so they carry that class's effects. `weapon_satchel` now derives from `CSgtcC4` (`fd44f29`), so its drop has the same throw cue (receipt `i1=14`). The shims were given and fired in their own GUI runs; only the receipts were checked for them.

## Decisions and open items

1. **MP5 secondary.** Approved: the stock edit is `4596d74`. The reference secondary is a cooldown-only stub, so it gets the dry-fire click only, with no projectile and no ammo.
2. **Missing assets** (table above). Decided: wait for the asset set. The shield viewmodel and worldmodel, the zat wave sprite, and the coujaf, flashbang and HUD timer sounds stay blocked until then.
3. **Zatarc whole-view tint.** Fixed on r20 (see the table). The source was the full-alpha `Muz1` sprite at close hits.
4. **Flashbang detonation.** Confirmed. The flashbang leaves the entity list about 3.4 s after the throw, and the view goes grey-white and fades (`flashvis-4`, `flashvis-5`).
5. **Gas cloud.** Now visible at range as a radius-scaled sprite burst (`9228678`). Detonation at t=16.89, the cloud 1,212 units from the thrower, with the before and after frames as above. The earlier beam strip, 16 units tall and about 42 wide, was a few pixels at that range. Close-range frames are not captured.
6. **Batsup follow-up swing** (`batsup2.sc`) confirmed with 36- and 40-frame gaps. The window is about 0.5–0.75 s after the first swing; 6, 25 and 50 frames miss it.
7. **Invis shield wall.** Confirmed. The gate needs colormap bit 9, which the test-only token `colormap9_1` sets (`e16e05c`). The secondary then adds one entity (the wall) and the view takes a whole-view yellow-green shield tint (`invisinfo-0`). The load sound was not heard.
8. **Larve detonation.** The blast itself has sound and damage only. The visible effect is the concussion fade: the view goes grey for several frames when the blast hits a player within 128 units (`larvevis-3`). In the harness the player is on team 0, so the larve's enemy search does not exclude its owner; with teams that check applies.
9. **P90 silencer toggle, M16 scope and PSG1 zoom** were silent. All three now play the dry-fire click (`ca3e921`), measured in the audio output. The zatarc zoom steps were silent too and now click as well (`ace710a`).
10. **Heal and poison on a target.** Captured in the multiplayer runs (see the medic and goaregen rows). Earlier attempts used `jointeam`, `menuselect` and key presses and stayed an observer; `autopick` after the briefing step spawns the local player as a class, and the pending team and class are applied on the next dead tick.
11. **Sound.** Measured on a PipeWire null sink (section above): the MP5 click, the gas pin pull, the Reetou shot, the USAS reload, the shield raise cue and the throw whoosh are in the audio output. Nobody has listened to them. Whether they sound right, and at what volume, is still a human check.

## Commits on this branch since `716dc00`

- `37e9a4f` Shrink the muzzle light so it stops washing out close walls
- `7323428` Play the coujaf swing when it misses
- `7d954e1` Send the weapon id with throw events
- `754c2ec` Add test tokens for hurt, ammo and stamina
- `880a52c` Fire the Spas and USAS pellets the server fires
- `e51ec45` Draw the shield raise cue and self-heal puffs in view
- `1508eb7` Reduce the big lance bolt light to radius 72 (superseded by `aa3e9da`)
- `f30cc2a` Play the throw cue when a gas grenade is released
- `aa3e9da` Remove the big bolt's dynamic light, which washed the view
- `fd44f29` Give the single-player satchel shim the C4 throw cue
- `ff34e5c` Draw the zatarc hit flash only for far hits
- `e16e05c` Add a test token that sets colormap bit 9 for the invis shield gate
- `4596d74` Give the MP5 secondary a dry-fire click (approved stock edit)
- `1b22090` Unlink queued menus by relinking, not appending
- `ffdb3a9` Let the medic and Goa'uld regen weapons trace teammates
- `adb4bed` Draw the medic and regen target puffs where the target can be seen
- `ca3e921` Add a click to the M16 scope, PSG1 zoom and P90 silencer toggles
- `1f8888a` Do not play the Reetou shot twice for the shooter
- `1d826a6` Play the throw whoosh on its own channel so it stops cutting the charge sound
- `25f8500` Draw a muzzle flash for the PSG1
- `ace710a` Add a click to the zatarc zoom steps
