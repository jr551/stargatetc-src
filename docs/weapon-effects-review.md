# Weapon effects and feel review

Branch `goal/weapon-effects-feel`, on top of `716dc00`. Scope: every weapon class registered in the game, 38 in all: the 31 MP arsenal weapons, `weapon_9mmAR` (an alias of the MP5 class), and six single-player shims. Nothing here is merged to `master`.

## How it was checked

- **Server, all MP arsenal weapons.** `wepfireall` on a dedicated server (`sg_nox`) gives each weapon, fires its primary, and fires its secondary where the class has one, logging each step. Sweep build: 38 classes registered, 31 given, 31 primaries fired, 11 secondaries fired, 0 retries.
- **Client, one GUI run per weapon.** Each run loads `sg_nox`, refills stamina, gives the weapon, sets its ammo to 5, fires the primary (or holds the secondary), and takes a screenshot 3 frames after the trigger. `sgtc_fxlog 1` prints one `SGTCFX <event> idx= origin= f1= i1=` line when each client event arrives. A receipt means the client handler ran.
- **Extra runs** for cases the sweep does not reach: the silenced P90, the charged Zat shot, medic self-heal, the larve detonation, the six shims, the gas release (three runs), and glstaff and Batsup after the r17 changes.
- **Builds.** The sweep and the extra runs used r16 (`hl.so` md5 `1027e860…`, `client.so` md5 `3482aee5…`). The gas, glstaff and Batsup-second-swing runs used r17 (`hl.so` md5 `cc9efb1f…`), which adds the gas throw cue and the lance light change.
- **Sound was not heard.** The runs used `-nosound`. Where the code plays a sound, the file was checked on disk. Nothing below claims a sound was heard.
- **Visible effects** come from single screenshot frames. "Not in frame" means the frame did not show the effect; it does not mean the effect is absent.
- **Evidence files** are in the test scratch folder, not the repo: run logs `one-<tag>.clean`, screenshots `one-shots/<tag>.png`, and the server log `dedi-final3.log`.

## Corrections to earlier notes

- **Gas grenades do release.** An earlier note said the canister never left the hand. That was a misreading: the canister still in hand is the next one (ammo 5 becomes 4). The release sends `events/throw.sc` with `i1=29`. The gas cloud itself was not captured in any frame.
- **The glstaff wash is not fixed.** The big lance light was reduced from radius 180 to 72 in r17, but the glstaff frame is still washed out to white and pink. The client BigLance handler adds no light, so the cause is still open.

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
| `weapon_m16` (CM16) | `m16.sc` ×3 | M16 held with scope; no flash in frame. The earlier verify run showed no wash on the near rock | Zoom; no event by design | Zoomed view, scope visible | Muzzle light radius 32 (see commit 37e9a4f) |
| `weapon_p90` (CP90) | `p90.sc` ×5 | Held; no flash in frame | Silencer toggle and FOV; no event by design | Held | Silenced shot: `p90l.sc` ×1 (extra run). The silencer toggle is silent |
| `weapon_psg1` (CPsg1) | `psg1.sc` ×1 | Held; no flash in frame | Zoom stages; no event by design | Not checked | |
| `weapon_m92s` (CM92s, Eagle) | `eagle.sc` ×3 | Held; no flash in frame | None | — | |
| `weapon_usas` (CUsas) | `usas.sc` ×3 | Held; no flash in frame | None | — | Client draws 8 pellets at 1024 units, matching the server (commit 880a52c) |
| `weapon_spas` (CShotgun, stock) | `spas.sc` and `spas2.sc` ×1 | Held; no pellet decals in frame | Stock secondary is empty | — | Client draws 8 pellets at 2048 units (commit 880a52c). Stock class |
| `weapon_mp5` (CMP5, stock) | None; stock `mp5.sc` is not logged | Held | Stock cooldown-only stub; no event (`mp5-sec`) | Held | Needs a decision; see Decisions |
| `weapon_reetou` (CReetou) | `reetou.sc` ×1 | Held; no projectile in frame | `reetou2.sc` ×1 | Held; no effect in frame | |
| `weapon_zat` (CZat) | `zata.sc` ×1; charged `zatb.sc` ×1 (extra run) | Held; no beam in frame | None | — | Zat wave sprite missing |
| `weapon_zatarc` (CZatarc) | `zatarc.sc` ×1 | Whole view tinted yellow-green; source not identified | Zoom; no event by design | Not checked | |
| `weapon_tacgun` (CTacgun) | `tac.sc` ×1 | Held; no laser dot in frame | HUD info cycle and `tacact.wav`; no event by design | Held; HUD change not confirmed | |
| `weapon_main` (CMainDieu, Energie) | `main.sc` ×1, `mains.sc` ×1 (stop) | Held; beam streak visible from the device | `main2.sc` ×1 | Hand only; no beam in frame | Live beam from an earlier commit (29e11d8) |
| `weapon_glstaff` (CBigLanceJaffa) | `biglance.sc` ×1 | **Whole view washed white and pink on r16 and r17. Open** | None | — | Projectile light reduced to 72 in r17; the wash remains |
| `weapon_lance` (CLanceJaffa) | `lance.sc` ×1 | Held; bolt trail visible | `lance2.sc` ×1 | Held; no bolt in frame | |
| `weapon_larve` (CLarve) | `larve.sc` ×1 | Larve held | Detonate owned larves; no event by design | Wall close-up; detonation not in frame | |
| `weapon_invis` (CInvis) | `invis.sc` ×1 | Device held | Shield wall: no event in the harness | Held; no wall | Did not fire in the harness; cause not checked |
| `weapon_shield` (CShieldGun) | `shield.sc` ×1 | Viewmodel missing (asset); small coloured glow at crosshair; green wall overlay not in frame | None | — | Raise sound and glow from source. Missing assets |
| `weapon_batsup` (CBatonSupplice) | `batsup.sc` ×1 | Melee flare on the rock ahead | None | — | Follow-up swing `batsup2.sc` not captured in a two-swing run |
| `weapon_coujaf` (CCoujaf) | `coujaf.sc` ×1 (swing and miss) | Blade mid-swing; no hit in frame | None | — | Miss now sends the event (commit 7323428). `nightstrike.wav` missing |
| `weapon_couteau` (CCouteau) | `couteau.sc` ×1 and `throw.sc` ×1 (`i1=3`) | Knife held | `throw.sc` ×1 (`i1=3`) | Knife held; throw not in frame | |
| `weapon_gacgoa` (CGrenadeChoc) | `throw.sc` ×1 (`i1=10`) | Device held; throw not in frame | `throw.sc` ×1 (`i1=10`) | Held | |
| `weapon_flashbang` (CFlashBang) | `throw.sc` ×1 (`i1=0`) | Held; detonation not in frame | None | — | `i1=0` is the flashbang's id by design. `flashbang.wav` missing: detonation silent |
| `weapon_handgrenade` (CHandGrenade, stock) | `throw.sc` ×1 (`i1=12`) from the ammo watch | Grenade held | None | — | Stock class, not edited |
| `weapon_c4` (CSgtcC4) | `throw.sc` ×1 (`i1=14`) | Two C4 units; a charge is on the ground ahead | Stock detonate; not run | — | Subclass of the stock satchel |
| `weapon_mine` (CTripmine, stock) | None; stock `tripfire.sc` is not logged | Mine held; placement not captured | None | — | Stock class, not edited |
| `weapon_mustardgrenade` | r16 sweep: none. r17: `throw.sc` ×1 (`i1=29`) | Canister held (the next one); cloud not in frame | None | — | Release confirmed on r17 |
| `weapon_narcogrenade` | r16 sweep: none. Same release path; not run on r17 | Canister held | None | — | |
| `weapon_nervegrenade` | r16 sweep: none. Same release path; not run on r17 | Canister held | None | — | |
| `weapon_smokegrenade` | r16 sweep: none. Same release path; not run on r17 | Canister held | None | — | |
| `weapon_medic` (CMedicx) | None; heal and poison need a player target in the crosshair | Medic held with the red hurt overlay from the test | `medic.sc` ×1 (`i1=3`, self heal) | Red overlay; no green puff in frame | Self-heal puff drawn in front of the view (source) |
| `weapon_goaregen` (CMedicxgoa) | `goaregen.sc` ×1 (`i1=1`, shooter; no target) | Device held; no puff in frame | None | — | Target heal (`i1=0`) not captured |
| `weapon_9mmAR` (alias of CMP5) | As `weapon_mp5` | — | — | — | Same class as the MP5 row |
| `weapon_9mmhandgun` (SP shim → CM92s) | `eagle.sc` ×3 (`sp-9mmhandgun`) | Not checked | None | — | Single-player only |
| `weapon_crossbow` (SP shim → CPsg1) | `psg1.sc` ×1 | Not checked | None | — | Single-player only |
| `weapon_crowbar` (SP shim → CBatonSupplice) | `batsup.sc` ×1 | Not checked | None | — | Single-player only |
| `weapon_rpg` (SP shim → CLanceJaffa) | `lance.sc` ×1 | Not checked | None | — | Single-player only |
| `weapon_satchel` (SP shim → stock CSatchel) | None | Not checked | — | — | No throw cue on this shim; see Decisions |
| `weapon_shotgun` (SP shim → CShotgun) | `spas.sc` and `spas2.sc` ×1 | Not checked | — | — | Single-player only |

## Stock-based classes (no stock file edited)

- **`weapon_handgrenade`** (`dlls/handgrenade.cpp`). The throw cue comes from `dlls/sgtc_throwfx.cpp`, which watches each player's handgrenade ammo once per frame and sends `events/throw.sc` when it drops.
- **`weapon_c4`** (`CSgtcC4`, `dlls/sgtc_wpn_c4.cpp`). A subclass of the stock satchel, linked in `dlls/sgtc_links.cpp`. The throw cue fires on drop; detonation is stock.
- **`weapon_mp5`, `weapon_9mmAR`** (`dlls/mp5.cpp`). The stock `mp5.sc` and `mp52.sc` events play; the SGTC client cannot log them. The secondary is a cooldown-only stub.
- **`weapon_spas`** (`dlls/shotgun.cpp`). The client draws the server's 8 pellets. The stock secondary is empty.
- **`weapon_mine`** (`dlls/tripmine.cpp`). Stock `tripfire.sc`, which the SGTC client cannot log.

## Single-player shims

Five of the six single-player shims map onto MP classes (`dlls/sp_shims.cpp`), so they carry that class's effects. `weapon_satchel` maps onto the stock `CSatchel`, which has no throw cue. The shims were given and fired in their own GUI runs; only the receipts were checked for them.

## Decisions for you

1. **MP5 secondary.** The stock `mp5.cpp` secondary only starts a cooldown and fires nothing. Fixing it means replacing the class, as C4 does. But the stock file also links `weapon_mp5`, so the SGTC link cannot be added without editing `dlls/mp5.cpp`. This needs your go-ahead to edit a stock file.
2. **Missing assets** (table above). These need the content files, or a decision to substitute existing ones.
3. **Glstaff wash.** The whole-view wash is still open. The cause is not the client handler, and the server projectile light change did not remove it. It needs a closer look at the projectile's effects.
4. **Single-player satchel** has no throw cue. Deriving the shim from `CSgtcC4` would give it one, in a single line in `dlls/sp_shims.cpp`. Not done.
5. **P90 silencer toggle** is silent. A click would be a small addition.
6. **Gas cloud and detonation** were not captured in any frame. The detonation sound `gasflow.wav` is present on disk.
7. **Batsup follow-up swing** (`batsup2.sc`) was not captured in the two-swing run.

## Commits on this branch since `716dc00`

- `37e9a4f` Shrink the muzzle light so it stops washing out close walls
- `7323428` Play the coujaf swing when it misses
- `7d954e1` Send the weapon id with throw events
- `754c2ec` Add test tokens for hurt, ammo and stamina
- `880a52c` Fire the Spas and USAS pellets the server fires
- `e51ec45` Draw the shield raise cue and self-heal puffs in view
- `1508eb7` Reduce the big lance bolt light to radius 72
- `f30cc2a` Play the throw cue when a gas grenade is released
