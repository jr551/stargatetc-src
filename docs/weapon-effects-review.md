# Weapon effects and feel review

Branch `goal/weapon-effects-feel`, on top of `716dc00`. Scope: every weapon class registered in the game, 38 in all: the 31 MP arsenal weapons, `weapon_9mmAR` (an alias of the MP5 class), and six single-player shims. Nothing here is merged to `master`.

## How it was checked

- **Server, all MP arsenal weapons.** `wepfireall` on a dedicated server (`sg_nox`) gives each weapon, fires its primary, and fires its secondary where the class has one, logging each step. Sweep build: 38 classes registered, 31 given, 31 primaries fired, 11 secondaries fired, 0 retries.
- **Client, one GUI run per weapon.** Each run loads `sg_nox`, refills stamina, gives the weapon, sets its ammo to 5, fires the primary (or holds the secondary), and takes a screenshot 3 frames after the trigger. `sgtc_fxlog 1` prints one `SGTCFX <event> idx= origin= f1= i1=` line when each client event arrives. A receipt means the client handler ran.
- **Extra runs** for cases the sweep does not reach: the silenced P90, the charged Zat shot, medic self-heal, the larve detonation, the six shims, the gas release (three runs), and glstaff, Batsup and the satchel shim after the later changes.
- **Builds.** The sweep and the extra runs used r16 (`hl.so` md5 `1027e860…`, `client.so` md5 `3482aee5…`). The gas, glstaff and Batsup-second-swing runs used r17 (`hl.so` md5 `cc9efb1f…`). The satchel shim and the bolt-light removal were checked on r18 and r19 builds, on glstaff and the satchel shim only. The zatarc flash gate and the colormap test token are on r20. The MP5 click and the entity-count checks are on the later build.
- **Sound was not heard.** The runs used `-nosound`. Where the code plays a sound, the file was checked on disk. Nothing below claims a sound was heard.
- **Visible effects** come from single screenshot frames. "Not in frame" means the frame did not show the effect; it does not mean the effect is absent.
- **Evidence files** are in the test scratch folder, not the repo: run logs `one-<tag>.clean`, screenshots `one-shots/<tag>.png`, and the server log `dedi-final3.log`.

## Corrections to earlier notes

- **Gas grenades do release.** An earlier note said the canister never left the hand. That was a misreading: the canister still in hand is the next one (ammo 5 becomes 4). The release sends `events/throw.sc` with `i1=29`. The gas cloud itself was not captured in any frame.
- **The glstaff wash came from the bolt's light.** The big lance bolt's dynamic light was orange (255,180,60). At radius 72 it washed the rock near the bolt yellow and white, and at radius 40 it still did. The impact explosion and the laser beams were tested and were not the cause. The light is removed (`aa3e9da`); the glow sprite stays, and the glstaff frame is normal.

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
| `weapon_m16` (CM16) | `m16.sc` ×3; ×5 in the flash run | M16 held with scope; a warm tint on the near rock in the first frame (`m16flash-0`), no distinct flash sprite | Zoom; no event by design | Zoomed view, scope visible | Muzzle light radius 32 (see commit 37e9a4f) |
| `weapon_p90` (CP90) | `p90.sc` ×5 | Held; no flash in frame | Silencer toggle and FOV; no event by design | Held | Silenced shot: `p90l.sc` ×1 (extra run). The silencer toggle is silent |
| `weapon_psg1` (CPsg1) | `psg1.sc` ×1 | Held; no flash in frame | Zoom stages; no event by design | Not checked | |
| `weapon_m92s` (CM92s, Eagle) | `eagle.sc` ×3 | Held; no flash in frame | None | — | |
| `weapon_usas` (CUsas) | `usas.sc` ×3; ×5 in the flash run | Muzzle flash visible at the first frame after the shot (`usasflash-0`); the flash shows on the far rock in `usasdecal-1`. Pellet decals not clearly visible | None | — | Client draws 8 pellets at 1024 units, matching the server (commit 880a52c) |
| `weapon_spas` (CShotgun, stock) | `spas.sc` and `spas2.sc` ×1 | Held; no pellet decals in frame | Stock secondary is empty | — | Client draws 8 pellets at 2048 units (commit 880a52c). Stock class |
| `weapon_mp5` (CMP5, stock) | None; stock `mp5.sc` is not logged | Held | Dry-fire click added (approved stock edit, `4596d74`); no projectile or ammo, as the reference | Held | Click not heard (`-nosound`) |
| `weapon_reetou` (CReetou) | `reetou.sc` ×1 | The projectile is in flight as a cyan ball ahead of the view (`reetouflight-0`) | `reetou2.sc` ×1 | Held; no effect in frame | |
| `weapon_zat` (CZat) | `zata.sc` ×1; charged `zatb.sc` ×1 (extra run) | Held; no beam in frame | None | — | Zat wave sprite missing |
| `weapon_zatarc` (CZatarc) | `zatarc.sc` ×1 | Normal on the r20 build; the impact flare still shows | Zoom; no event by design | Not checked | The tint was the full-alpha `Muz1` sprite at close hits (bisected). Now drawn only for hits beyond 300 units (`ff34e5c`). Before `zatarc.png` (r16), after `zatarc-fix.png` (r20) |
| `weapon_tacgun` (CTacgun) | `tac.sc` ×1, ×2 in the dot run | A small red dot at the aim point on the hit (`tacdot-1`); the laser beam itself is not in frame | HUD info cycle and `tacact.wav`; no event by design | Held; HUD change not confirmed | |
| `weapon_main` (CMainDieu, Energie) | `main.sc` ×1, `mains.sc` ×1 (stop) | Held; beam streak visible from the device | `main2.sc` ×1 | Hand only; no beam in frame | Live beam from an earlier commit (29e11d8) |
| `weapon_glstaff` (CBigLanceJaffa) | `biglance.sc` ×1 | Washed white and pink on r16 and r17 from the bolt's light. Normal on r19 (frame checked) | None | — | Light removed in `aa3e9da`; glow sprite kept. Before `glstaff.png` (r16), after `glstaff-nodl-0.png` (r19) |
| `weapon_lance` (CLanceJaffa) | `lance.sc` ×1 | Held; bolt trail visible | `lance2.sc` ×1 | Held; no bolt in frame | |
| `weapon_larve` (CLarve) | `larve.sc` ×1 | Larve held | Detonate owned larves; no event by design | The detonation shows as a grey concussion fade on a player within 128 units (`larvevis-3`) | The blast itself has sound and damage only |
| `weapon_invis` (CInvis) | `invis.sc` ×1 | Device held | Shield wall: no event by design; with `colormap9_1` it adds one entity and a whole-view yellow-green shield tint | Tint in `invisinfo-0` | Gate needs colormap bit 9 (test token `colormap9_1`, `e16e05c`) |
| `weapon_shield` (CShieldGun) | `shield.sc` ×1 | Viewmodel missing (asset); small coloured glow at crosshair; green wall overlay not in frame | None | — | Raise sound and glow from source. Missing assets |
| `weapon_batsup` (CBatonSupplice) | `batsup.sc` ×1; follow-up `batsup2.sc` ×1 with 36- and 40-frame gaps | Melee flare on the rock ahead | None | — | The follow-up needs the second swing 0.5–0.75 s after the first; 6, 25 and 50 frames miss it |
| `weapon_coujaf` (CCoujaf) | `coujaf.sc` ×1 (swing and miss) | Blade mid-swing; no hit in frame | None | — | Miss now sends the event (commit 7323428). `nightstrike.wav` missing |
| `weapon_couteau` (CCouteau) | `couteau.sc` ×1 and `throw.sc` ×1 (`i1=3`) | Knife held | `throw.sc` ×1 (`i1=3`) | Knife held; throw not in frame | |
| `weapon_gacgoa` (CGrenadeChoc) | `throw.sc` ×1 (`i1=10`) | Device held; throw not in frame | `throw.sc` ×1 (`i1=10`) | Held | |
| `weapon_flashbang` (CFlashBang) | `throw.sc` ×1 (`i1=0`) | Held; the detonation shows as a grey-white flash that fades (`flashvis-4`, `flashvis-5`) | None | — | `i1=0` is the flashbang's id by design. Detonation is the stock flash path (`CGrenade::ShootFlash`); `flashbang.wav` missing, so the detonation sound is silent |
| `weapon_handgrenade` (CHandGrenade, stock) | `throw.sc` ×1 (`i1=12`) from the ammo watch | Grenade held | None | — | Stock class, not edited |
| `weapon_c4` (CSgtcC4) | `throw.sc` ×1 (`i1=14`) | Two C4 units; a charge is on the ground ahead | Stock detonate; not run | — | Subclass of the stock satchel |
| `weapon_mine` (CTripmine, stock) | None; stock `tripfire.sc` is not logged | Mine held; placement not captured | None | — | Stock class, not edited |
| `weapon_mustardgrenade` | r16 sweep: none. r17: `throw.sc` ×1 (`i1=29`) | Canister held (the next one); cloud not in frame | None | — | Release confirmed on r17 |
| `weapon_narcogrenade` | r16 sweep: none. Same release path; not run on r17 | Canister held | None | — | |
| `weapon_nervegrenade` | r16 sweep: none. Same release path; not run on r17 | Canister held | None | — | |
| `weapon_smokegrenade` | r16 sweep: none. Same release path; not run on r17 | Canister held | None | — | |
| `weapon_medic` (CMedicx) | None; heal and poison need a player target in the crosshair | Medic held with the red hurt overlay from the test | `medic.sc` ×1 (`i1=3`, self heal) | Red overlay; the device is held and no green puff is in frame (`medicpuff-1`) | Self-heal puff drawn in front of the view (source) |
| `weapon_goaregen` (CMedicxgoa) | `goaregen.sc` ×1 (`i1=1`, shooter; no target) | Device held with a small orange glow at the palm (`goapuff-0`); no green puff in frame | None | — | Target heal (`i1=0`) not captured |
| `weapon_9mmAR` (alias of CMP5) | As `weapon_mp5` | — | — | — | Same class as the MP5 row |
| `weapon_9mmhandgun` (SP shim → CM92s) | `eagle.sc` ×3 (`sp-9mmhandgun`) | Not checked | None | — | Single-player only |
| `weapon_crossbow` (SP shim → CPsg1) | `psg1.sc` ×1 | Not checked | None | — | Single-player only |
| `weapon_crowbar` (SP shim → CBatonSupplice) | `batsup.sc` ×1 | Not checked | None | — | Single-player only |
| `weapon_rpg` (SP shim → CLanceJaffa) | `lance.sc` ×1 | Not checked | None | — | Single-player only |
| `weapon_satchel` (SP shim → CSgtcC4) | `throw.sc` ×1 (`i1=14`, r18) | Not checked | — | — | Now derives from CSgtcC4 (`fd44f29`) |
| `weapon_shotgun` (SP shim → CShotgun) | `spas.sc` and `spas2.sc` ×1 | Not checked | — | — | Single-player only |

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
5. **Gas cloud and detonation.** Not confirmed. Looking down before the throw, the canister is in flight in frames 1–2 and gone by frame 3. A no-throw control with the same look-down looks the same as the throw run (`gasbase-3`, `gasvis-3`), so the green cast is the normal look-down view and no cloud is visible. The detonation sound `gasflow.wav` is present on disk. The entity count does not change at detonation, so the cloud may reuse the grenade's entity, and the green frames look like the normal look-down view.
6. **Batsup follow-up swing** (`batsup2.sc`) confirmed with 36- and 40-frame gaps. The window is about 0.5–0.75 s after the first swing; 6, 25 and 50 frames miss it.
7. **Invis shield wall.** Confirmed. The gate needs colormap bit 9, which the test-only token `colormap9_1` sets (`e16e05c`). The secondary then adds one entity (the wall) and the view takes a whole-view yellow-green shield tint (`invisinfo-0`). The load sound was not heard.
8. **Larve detonation.** The blast itself has sound and damage only. The visible effect is the concussion fade: the view goes grey for several frames when the blast hits a player within 128 units (`larvevis-3`). In the harness the player is on team 0, so the larve's enemy search does not exclude its owner; with teams that check applies.
9. **P90 silencer toggle** is silent. A click would be a small addition.

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
