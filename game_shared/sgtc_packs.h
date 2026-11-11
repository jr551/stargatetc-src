// sgtc_packs.h — fixed pack-menu presets (mp_wepselmode 2), shared by client and server.
#pragma once
#ifndef SGTC_PACKS_H
#define SGTC_PACKS_H

#include "sgtc_enums.h"

#define SGTC_PACKSEL_COUNT  4
#define SGTC_PACK_MAX_SLOTS 6

// ref FUN_1002b15b: team index 0 = Torri, 1 = Goa'uld; class 0..4; pack 0..3.
static const int s_SgtcPackSlots[2][SGTC_NUM_CLASSES][SGTC_PACKSEL_COUNT][SGTC_PACK_MAX_SLOTS] =
{
	{
		{ { 6, 10, 15, 20, 22, -1 }, { 3, 7, 11, 15, 23, -1 }, { -1 }, { -1 } },
		{ { 8, 12, 22, 24, -1 },     { 4, 10, 20, 23, -1 },     { -1 }, { -1 } },
		{ { 3, 5, 15, 20, 25, -1 },  { 6, 10, 16, 24, 26, -1 }, { -1 }, { -1 } },
		{ { 8, 11, 16, 23, 24, -1 }, { 7, 10, 12, 15, 25, -1 }, { -1 }, { -1 } },
		{ { 8, 15, 16, 23, 25, -1 }, { 3, 4, 20, 22, 26, -1 },
		  { 3, 6, 15, 16, -1 },      { 5, 10, 24, 26, -1 } },
	},
	{
		{ { 9, 10, 12, 18, -1 },     { 10, 14, 17, 27, -1 },    { -1 }, { -1 } },
		{ { 10, 11, 14, 21, 27, -1 },{ 2, 10, 12, 13, 19, -1 }, { -1 }, { -1 } },
		{ { 2, 10, 19, -1 },         { 11, 21, 27, -1 },
		  { 12, 13, 14, -1 },        { 10, 14, 21, -1 } },
		{ { 11, 19, 21, 27, -1 },    { 9, 11, 21, 19, -1 },     { -1 }, { -1 } },
		{ { 2, 13, 14, 17, -1 },     { 2, 11, 17, 21, -1 },     { -1 }, { -1 } },
	}
};

static inline int SGTC_PackGetSlots( int iTeam, int iClassIdx, int iPack, int *piSlots )
{
	if( !piSlots || iTeam < SGTC_TEAM_TORRI || iTeam > SGTC_TEAM_GOAULD ||
		iClassIdx < 0 || iClassIdx >= SGTC_NUM_CLASSES ||
		iPack < 1 || iPack > SGTC_PACKSEL_COUNT )
		return 0;

	const int *pSrc = s_SgtcPackSlots[iTeam - SGTC_TEAM_TORRI][iClassIdx][iPack - 1];
	int iCount = 0;
	while( iCount < SGTC_PACK_MAX_SLOTS && pSrc[iCount] >= 0 )
	{
		piSlots[iCount] = pSrc[iCount];
		iCount++;
	}
	return iCount;
}

// Returns TRUE when gond[i] is exactly 1 on preset slot indices and 0 elsewhere.
static inline int SGTC_GondMatchesPackPreset( int iTeam, int iClassIdx, int iPack, const int *gond )
{
	if( !gond || iClassIdx < 0 || iClassIdx >= SGTC_NUM_CLASSES )
		return 0;

	int preset[SGTC_PACK_MAX_SLOTS];
	const int n = SGTC_PackGetSlots( iTeam, iClassIdx, iPack, preset );
	if( n <= 0 )
		return 0;

	for( int slot = 0; slot < SGTC_NUM_WEAPON_SLOTS; slot++ )
	{
		int want = 0;
		for( int j = 0; j < n; j++ )
		{
			if( preset[j] == slot )
			{
				want = 1;
				break;
			}
		}
		if( ( gond[slot] != 0 ? 1 : 0 ) != want )
			return 0;
	}
	return 1;
}

static inline int SGTC_GondMatchesAnyClassPack( int iTeam, int iClassShown, const int *gond )
{
	if( iClassShown < SGTC_CLASS_FIRST || iClassShown > SGTC_CLASS_LAST )
		return 0;
	const int iClassIdx = iClassShown - SGTC_CLASS_FIRST;
	for( int pack = 1; pack <= SGTC_PACKSEL_COUNT; pack++ )
	{
		if( SGTC_GondMatchesPackPreset( iTeam, iClassIdx, pack, gond ) )
			return 1;
	}
	return 0;
}

#endif
