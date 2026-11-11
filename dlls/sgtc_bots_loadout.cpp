// sgtc_bots_loadout.cpp - pure team/class decisions, no engine calls.
// See sgtc_bots_loadout.h.

#include <string.h>

#include "sgtc_bots_loadout.h"

// Model keys copied from dlls/sgtc_rules_spawn.cpp (s_SgtcGoauldClass /
// s_SgtcTorriClass), indexed by SgtcClass 1..5.
static const char *const s_sgtc_torri_models[6] =
{
	NULL, "oneill", "carter", "jackson", "tealc", "commander"
};

static const char *const s_sgtc_goauld_models[6] =
{
	NULL, "systemlord", "anubis", "goauld3", "snakegoauld", "horus"
};

static void copy_cstr( char *dst, int dst_size, const char *src )
{
	int i = 0;

	if( !dst || dst_size <= 0 )
		return;

	if( src )
	{
		while( i < dst_size - 1 && src[i] )
		{
			dst[i] = src[i];
			i++;
		}
	}

	dst[i] = '\0';
}

void SGTC_Bots_LoadoutStateInit( sgtc_bot_loadout_state_t *st )
{
	if( st )
		memset( st, 0, sizeof( *st ) );
}

unsigned int SGTC_Bots_MixSeed( unsigned int seed, int index )
{
	// splitmix32-style finalizer: cheap, no libc, stable across platforms.
	unsigned int x = seed ^ ( (unsigned int)index * 0x9e3779b9u );

	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;

	return x ? x : 0x9e3779b9u;
}

unsigned int SGTC_Bots_NextRandom( unsigned int *state )
{
	unsigned int x = ( state && *state ) ? *state : 0x9e3779b9u;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;

	if( state )
		*state = x;

	return x;
}

int SGTC_Bots_PickTeam( const sgtc_bot_loadout_state_t *st, int override_team,
	unsigned int tiebreak )
{
	if( override_team == SGTC_TEAM_TORRI || override_team == SGTC_TEAM_GOAULD )
		return override_team;

	if( !st )
		return SGTC_TEAM_TORRI;

	if( st->team_counts[SGTC_TEAM_TORRI] < st->team_counts[SGTC_TEAM_GOAULD] )
		return SGTC_TEAM_TORRI;
	if( st->team_counts[SGTC_TEAM_GOAULD] < st->team_counts[SGTC_TEAM_TORRI] )
		return SGTC_TEAM_GOAULD;

	// Perfectly balanced: alternate deterministically instead of always
	// stacking every new bot on the same side.
	return ( tiebreak & 1u ) ? SGTC_TEAM_GOAULD : SGTC_TEAM_TORRI;
}

int SGTC_Bots_PickClass( int team, const int *caps, const int *counts,
	int forced_class, unsigned int pick )
{
	int candidates[SGTC_NUM_CLASSES];
	int num_candidates = 0;
	int cls;

	if( team != SGTC_TEAM_TORRI && team != SGTC_TEAM_GOAULD )
		return SGTC_CLASS_NONE;
	if( !caps || !counts )
		return SGTC_CLASS_NONE;

	for( cls = SGTC_CLASS_FIRST; cls <= SGTC_CLASS_LAST; cls++ )
	{
		const int cap = caps[cls];
		const int used = counts[cls];

		// cap == 0 is "unlimited" in the rules code (SGTC_ClassCapped).
		if( cap == 0 || used < cap )
			candidates[num_candidates++] = cls;
	}

	if( num_candidates == 0 )
		return SGTC_CLASS_NONE;

	if( forced_class >= SGTC_CLASS_FIRST && forced_class <= SGTC_CLASS_LAST )
	{
		const int cap = caps[forced_class];
		const int used = counts[forced_class];

		if( cap == 0 || used < cap )
			return forced_class;
	}

	return candidates[pick % (unsigned int)num_candidates];
}

int SGTC_Bots_TeamName( int team, char *dst, int dst_size )
{
	if( !dst || dst_size <= 0 )
		return -1;

	if( team == SGTC_TEAM_TORRI )
	{
		copy_cstr( dst, dst_size, "Torri" );
		return 0;
	}
	if( team == SGTC_TEAM_GOAULD )
	{
		copy_cstr( dst, dst_size, "Goa'uld" );
		return 0;
	}

	dst[0] = '\0';
	return -1;
}

int SGTC_Bots_ClassModelName( int team, int cls, char *dst, int dst_size )
{
	if( !dst || dst_size <= 0 )
		return -1;
	if( cls < SGTC_CLASS_FIRST || cls > SGTC_CLASS_LAST )
	{
		dst[0] = '\0';
		return -1;
	}

	if( team == SGTC_TEAM_TORRI )
	{
		copy_cstr( dst, dst_size, s_sgtc_torri_models[cls] );
		return 0;
	}
	if( team == SGTC_TEAM_GOAULD )
	{
		copy_cstr( dst, dst_size, s_sgtc_goauld_models[cls] );
		return 0;
	}

	dst[0] = '\0';
	return -1;
}
