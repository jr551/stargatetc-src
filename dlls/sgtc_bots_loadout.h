// sgtc_bots_loadout.h - pure Stargate team/class decisions for jk_botti bots.
//
// Everything here is a pure function over plain integers: no engine calls, no
// globals, no allocation. dlls/sgtc_bots.cpp gathers the live player counts and
// the per-class caps, calls these, then does the engine work (setting
// CBasePlayer fields). tests/stub_engine.c links them directly, which is why
// they carry C linkage and mirror the SgtcTeam / SgtcClass numbering from
// game_shared/sgtc_enums.h.
#pragma once
#ifndef SGTC_BOTS_LOADOUT_H
#define SGTC_BOTS_LOADOUT_H

#include "sgtc_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

// One snapshot of the round state used to pick a bot's team and class.
typedef struct
{
	int team_counts[3];        // [SgtcTeam] live players, index 1..2
	int class_caps[3][6];      // [team][class], 0 = unlimited
	int class_counts[3][6];    // [team][class] live players
} sgtc_bot_loadout_state_t;

void SGTC_Bots_LoadoutStateInit( sgtc_bot_loadout_state_t *st );

// Deterministic per-bot seed derived from (cvar seed, bot slot).
unsigned int SGTC_Bots_MixSeed( unsigned int seed, int index );

// Small reproducible PRNG for per-bot choices; advances *state.
unsigned int SGTC_Bots_NextRandom( unsigned int *state );

// Smaller team wins (SgtcTeam numbering); a tie is broken by tiebreak bit 0.
// override_team (0 auto, SGTC_TEAM_TORRI/GOAULD) wins over the counts.
int SGTC_Bots_PickTeam( const sgtc_bot_loadout_state_t *st, int override_team,
	unsigned int tiebreak );

// Pick a class 1..5 that is not capped on that team. forced_class (0 auto,
// 1..5) is honoured when that class is not capped; otherwise the bot falls back
// to the uncapped pick (the engine re-rolls capped picks at respawn anyway).
// pick selects deterministically among the uncapped classes. Returns 0 when
// every class is capped.
int SGTC_Bots_PickClass( int team, const int *caps, const int *counts,
	int forced_class, unsigned int pick );

// "Torri" / "Goa'uld" for an SgtcTeam. Returns 0 on success, -1 on bad input.
int SGTC_Bots_TeamName( int team, char *dst, int dst_size );

// Player model key for an SgtcTeam + SgtcClass (the same table PlayerSpawn and
// SGTC_ApplyUserInfoTeam use). Returns 0 on success, -1 on bad input.
int SGTC_Bots_ClassModelName( int team, int cls, char *dst, int dst_size );

#ifdef __cplusplus
}
#endif

#endif // SGTC_BOTS_LOADOUT_H
