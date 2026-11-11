// sgtc_util.h - small helpers shared by the SGTC server sources (#96).
//
// Server-only: nothing here is used by cl_dll. Every helper is an inline
// function so the header can be included from any number of translation units.
#pragma once
#ifndef SGTC_UTIL_H
#define SGTC_UTIL_H

#include "extdll.h"
#include "util.h"
#include "cbase.h"

// Manhattan 3d distance as int (ref Endo_distance3d @0x1abc1c / @0x1bbc1c).
// Each axis delta is truncated to int before the three are summed, so the
// result is never larger than (int)( |dx| + |dy| + |dz| ) and can be up to 2
// smaller. This is the lanceproj/player/token/sgtc_player behaviour; the
// other copies this replaces truncated the sum or the raw coordinates instead.
inline int SGTC_Distance3D( const Vector &a, const Vector &b )
{
	return (int)fabs( a.x - b.x ) + (int)fabs( a.y - b.y ) +
		(int)fabs( a.z - b.z );
}

// The stock HL crowbar hull projection (defined in crowbar.cpp, which has no
// header declaration of its own).
void FindHullIntersection( const Vector &vecSrc, TraceResult &tr, float *mins, float *maxs, edict_t *pEntity );

// Hull-trace tail of the melee Swing() paths (lance, coujaf, batsup). Call it
// after the 32-unit aim trace: when that trace missed (tr.flFraction >= 1),
// retry with the head hull; a BSP/world hull hit is projected onto the face
// with the stock FindHullIntersection, and vecEnd becomes the new trace end.
// vecSrc/vecEnd are the aim trace's endpoints, pentPlayer the swinging player.
inline void SGTC_MeleeHullFallback( edict_t *pentPlayer, const Vector &vecSrc, TraceResult &tr, Vector &vecEnd )
{
	if( tr.flFraction >= 1.0f )
	{
		UTIL_TraceHull( vecSrc, vecEnd, dont_ignore_monsters, head_hull,
			pentPlayer, &tr );
		if( tr.flFraction < 1.0f )
		{
			CBaseEntity *pHit = CBaseEntity::Instance( tr.pHit );
			if( !pHit || pHit->IsBSPModel() )
				FindHullIntersection( vecSrc, tr, VEC_DUCK_HULL_MIN,
					VEC_DUCK_HULL_MAX, pentPlayer );
			vecEnd = tr.vecEndPos;
		}
	}
}

// Time-driven ammo regeneration shared by the staff/zat/main weapons' Reload().
// While iAmmo is below iCap and the latch flNext is due, add iStep (never past
// iCap, so a step of 2 from an odd count cannot overshoot) and push the latch
// out by flInterval. Catches up several steps if the latch is far behind. A
// non-positive step or interval would never terminate, so it is a no-op.
inline void SGTC_RegenAmmo( int &iAmmo, float &flNext, int iCap, int iStep, float flInterval )
{
	if( iStep <= 0 || flInterval <= 0.0f )
		return;

	while( iAmmo < iCap && flNext < gpGlobals->time )
	{
		iAmmo += iStep;
		if( iAmmo > iCap )
			iAmmo = iCap;
		flNext += flInterval;
	}
}

#endif // SGTC_UTIL_H
