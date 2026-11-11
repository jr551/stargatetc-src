// sgtc_bots_budget.h - pure edict-budget decision for jk_botti spawns.
//
// The engine has a hard MAX_EDICTS (900 in the 32-bit xash3d-fwgs build), and
// hitting it is fatal: Host_Error: SV_AllocEdict: no free edicts. sg_escape
// already sits at ~882/900 with no bots (#292), so a bot spawn must not be
// allowed to consume the last few slots.
//
// The decision is a pure function over plain integers so tests/stub_engine.c
// and the host-side feature test can link it without an engine. The engine
// wrappers (dlls/sgtc_bots.cpp) read pfnNumberOfEntities()/gpGlobals->maxEntities
// and do the logging; BotCreate and sgtc_bot_add pass the loadout reserve.
#pragma once
#ifndef SGTC_BOTS_BUDGET_H
#define SGTC_BOTS_BUDGET_H

#include "../game_shared/sgtc_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

// Headroom required before a spawn: a pre-spawn reserve, not free space left
// afterwards. The reserve is loadout-aware; one uniform value cannot cover the
// freeguns worst case without over-blocking a normal class kit.
//
//   * mp_freeguns > 1 (the CP spawn in sgtc_rules_spawn.cpp) grants every
//     weapon slot: 26 of the 28 slots allocate an item, invis/shield are
//     granted through the state machine, the post-loop invis item and the team
//     melee add one each, and the fake client itself is one more. That is up to
//     29 edicts; SGTC_NUM_WEAPON_SLOTS + 3 (31) covers it with two spare.
//   * otherwise a normal class kit is about 15 entities (12 kit items + melee +
//     possible invis + fake client), so 16 leaves a one-edict cushion.
//
// Policy: choose SGTC_BOT_EDICT_MARGIN_FREEGUNS while mp_freeguns > 1, and
// SGTC_BOT_EDICT_MARGIN_NORMAL otherwise. At used=883 the freeguns spawn is
// refused (883 + 29 = 912 > 900) while the 15-edict normal spawn is allowed
// (883 + 15 = 898 <= 900).
//
// On sg_escape's measured 882/900 baseline the normal reserve still allows one
// 15-edict bot (882 + 15 = 897), while a freeguns server refuses it
// (882 + 29 = 911 can reach SV_AllocEdict). The 4-6 bot quota from #193 does
// not fit on that map either way.
//
// Documented exception: this is a reserve taken at check time. Edicts allocated
// between the check and the spawn by another client, a map game_player_equip
// grant or engine code are not covered; those allocations can still consume the
// reserve. The guard also cannot make a multi-bot `sgtc_bot_add` atomic -- a
// request that passes the count floor can be refused by the per-spawn gate
// mid-loop (the residual is documented in docs/TEST-COMMANDS.md).
#define SGTC_BOT_EDICT_MARGIN_NORMAL 16
#define SGTC_BOT_EDICT_MARGIN_FREEGUNS ( SGTC_NUM_WEAPON_SLOTS + 3 )

// Pick the reserve for a loadout. Nonzero `freeguns` (the live mp_freeguns > 1
// test) selects the CP worst case; zero selects the normal class-kit reserve.
// BotCreate passes the result to the per-spawn gate and sgtc_bot_add passes the
// same result to the count-aware form, so both make the same choice.
int SGTC_Bots_EdictBudgetSpawnMargin( int freeguns );

// 1 = allow one more bot, 0 = refuse.
//
// Refuses when any input is unusable (used < 0, max_edicts <= 0, margin < 0,
// margin >= max_edicts) or when `used + margin` reaches the engine maximum:
// at exactly the margin, at exactly max, and over max all refuse.
int SGTC_Bots_EdictBudgetAllows( int used, int max_edicts, int margin );

// 1 = allow a request for `count` bots, 0 = refuse.
//
// Count-aware form used by `sgtc_bot_add`: it reserves one edict per requested
// bot above the normal margin (`margin + count - 1`), so a request that cannot
// fit even that floor is refused once, before any bot is created. This does
// not change the per-spawn decision above. Because a bot costs more than one
// edict (fake client + spawn inventory), a request that passes this floor can
// still be refused by the per-spawn gate mid-loop.
int SGTC_Bots_EdictBudgetAllowsRequest( int used, int max_edicts, int margin, int count );

#ifdef __cplusplus
}
#endif

#endif // SGTC_BOTS_BUDGET_H
