// sgtc_armor_fx.h — which player hitgroups are metal armour, shared by the
// SGTC server (dlls/) and its fast-tier test. Plain header: no engine headers,
// so a host compiler can build a probe against it.
//
// The Goa'uld player models and Teal'c's Jaffa model wear metal chainmail on
// the torso and arms (see docs/ARMOR-HITS.md for the per-model evidence). A
// bullet that lands on one of those hitgroups shows sparks and plays a
// ricochet instead of blood; the two full-helmet classes (Anubis, Horus) also
// count the head. Damage is never changed here.
#pragma once
#ifndef SGTC_ARMOR_FX_H
#define SGTC_ARMOR_FX_H

#include <string.h>
#include "sgtc_enums.h"

// Hitgroup ids, mirroring dlls/monsters.h (GoldSrc ABI values; MUST NOT change).
enum SgtcHitGroup
{
	SGTC_HG_GENERIC  = 0,
	SGTC_HG_HEAD     = 1,
	SGTC_HG_CHEST    = 2,
	SGTC_HG_STOMACH  = 3,
	SGTC_HG_LEFTARM  = 4,
	SGTC_HG_RIGHTARM = 5,
	SGTC_HG_LEFTLEG  = 6,
	SGTC_HG_RIGHTLEG = 7
};

#define SGTC_ARMOR_HITGROUP_BIT( hg )	( 1 << (hg) )

// Chainmail covers the chest, stomach and both arms on every armoured class.
#define SGTC_ARMOR_LIMB_MASK ( SGTC_ARMOR_HITGROUP_BIT( SGTC_HG_CHEST ) | \
	SGTC_ARMOR_HITGROUP_BIT( SGTC_HG_STOMACH ) | \
	SGTC_ARMOR_HITGROUP_BIT( SGTC_HG_LEFTARM ) | \
	SGTC_ARMOR_HITGROUP_BIT( SGTC_HG_RIGHTARM ) )

// Bit mask of the metal-plated hitgroups for one player, or 0 for a class with
// no metal armour. The team NAME is checked (not pev->team) so the alternate
// modes ("Unas", "Marine", invis) never match even though they reuse the same
// team slots.
static inline int SGTC_ArmorMetalMask( const char *pszTeamName, int iClass )
{
	if( !pszTeamName || iClass < SGTC_CLASS_FIRST || iClass > SGTC_CLASS_LAST )
		return 0;

	// Goa'uld: systemlord, anubis, jaffa (goauld3), serpent (snakegoauld),
	// horus all wear chainmail. Anubis and Horus wear a full metal helmet.
	if( !strncmp( pszTeamName, "Goa'uld", 7 ) )
	{
		if( iClass == SGTC_CLASS_ANUBIS || iClass == SGTC_CLASS_HORUS )
			return SGTC_ARMOR_LIMB_MASK | SGTC_ARMOR_HITGROUP_BIT( SGTC_HG_HEAD );
		return SGTC_ARMOR_LIMB_MASK;
	}

	// Torri: only Teal'c is a Jaffa and wears the same chainmail.
	if( !strncmp( pszTeamName, "Torri", 5 ) && iClass == SGTC_CLASS_TEALC )
		return SGTC_ARMOR_LIMB_MASK;

	return 0;
}

// 1 when a bullet on iHitgroup hits metal armour on this player.
static inline int SGTC_ArmorMetalHit( const char *pszTeamName, int iClass, int iHitgroup )
{
	if( iHitgroup < SGTC_HG_GENERIC || iHitgroup > SGTC_HG_RIGHTLEG )
		return 0;
	return ( SGTC_ArmorMetalMask( pszTeamName, iClass ) & SGTC_ARMOR_HITGROUP_BIT( iHitgroup ) ) != 0;
}

// Rate limiter for the metal impact effect, shared with the fast-tier probe so
// its map-change behaviour is testable without an engine.
//
// The deadline is stored in gpGlobals->time seconds, and that clock restarts
// near zero on every map change. A deadline left over from the previous map can
// therefore sit minutes in the future on the new map and would mute the effect
// for that player slot until it elapsed. Any deadline further ahead than one
// interval plus slack is treated as stale and dropped before the comparison; a
// live deadline is never ahead by more than the interval, so normal play is
// unaffected.
#define SGTC_ARMOR_IMPACT_INTERVAL	0.1f
#define SGTC_ARMOR_IMPACT_STALE_SLACK	0.2f

// 1 when flNextImpact (in gpGlobals->time seconds) allows another impact now.
// On success the deadline advances by one impact interval.
static inline int SGTC_ArmorImpactReady( float &flNextImpact, float flNow )
{
	if( flNextImpact > flNow + SGTC_ARMOR_IMPACT_STALE_SLACK )
		flNextImpact = 0.0f;	// stale: last fired on a previous map

	if( flNow < flNextImpact )
		return 0;

	flNextImpact = flNow + SGTC_ARMOR_IMPACT_INTERVAL;
	return 1;
}

#endif // SGTC_ARMOR_FX_H
