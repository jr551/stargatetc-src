// sgtc_diag.cpp — opt-in diagnostic logging (build-5 regression hunt).
//
// Logs projectile/entity spawns with owner and model state, weapon grants and
// the per-respawn WeaponList burst, and the TE_BEAMENTS tempentity calls.  Each
// line is one JSON object with "SGTC_DIAG" as the tag so the headless runs can
// be counted with grep.  Off by default (sgtc_diag_log 0) and only used by the
// test harness; no gameplay effect.

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "sgtc_diag.h"

cvar_t sgtc_diag_log = { "sgtc_diag_log", "0", FCVAR_SERVER };

void SGTC_DiagInit( void )
{
	SGTC_RegisterOrAdoptCvar( &sgtc_diag_log );
}

static BOOL DiagOn( void )
{
	// Read the live engine cvar value (the pointer the console `set` command
	// updates); the local cvar_t copy can stay at its default when a config or
	// command line created the cvar before the DLL registered it.
	cvar_t *pCvar = CVAR_GET_POINTER( "sgtc_diag_log" );
	return ( pCvar && pCvar->value > 0.0f ) ? TRUE : FALSE;
}

BOOL SGTC_DiagEnabled( void )
{
	return DiagOn();
}

static const char *EntName( CBaseEntity *pEntity )
{
	if( !pEntity || !pEntity->pev )
		return "";
	if( pEntity->IsPlayer() )
		return STRING( pEntity->pev->netname );
	return STRING( pEntity->pev->classname );
}

static const char *EntClass( CBaseEntity *pEntity )
{
	if( !pEntity || !pEntity->pev )
		return "";
	return STRING( pEntity->pev->classname );
}

void SGTC_DiagSpawn( const char *pszClass, CBaseEntity *pOwner, const Vector &vecOrigin,
	float flScale, int iBody, int iSkin, int iSeq )
{
	if( !DiagOn() )
		return;

	UTIL_LogPrintf(
		"SGTC_DIAG {\"cmd\":\"spawn\",\"class\":\"%s\",\"owner\":\"%s\",\"owner_class\":\"%s\","
		"\"origin\":[%.1f,%.1f,%.1f],\"scale\":%.2f,\"body\":%d,\"skin\":%d,\"seq\":%d}\n",
		pszClass, EntName( pOwner ), EntClass( pOwner ),
		vecOrigin.x, vecOrigin.y, vecOrigin.z, flScale, iBody, iSkin, iSeq );
}

void SGTC_DiagGrant( CBaseEntity *pPlayer, const char *pszWeapon, int iWeaponBits )
{
	if( !DiagOn() )
		return;

	UTIL_LogPrintf( "SGTC_DIAG {\"cmd\":\"grant\",\"player\":\"%s\",\"weapon\":\"%s\",\"weapons\":%d}\n",
		EntName( pPlayer ), pszWeapon, iWeaponBits );
}

void SGTC_DiagWeaponList( CBaseEntity *pPlayer, int iRecords )
{
	if( !DiagOn() )
		return;

	UTIL_LogPrintf( "SGTC_DIAG {\"cmd\":\"weaponlist\",\"player\":\"%s\",\"records\":%d}\n",
		EntName( pPlayer ), iRecords );
}

void SGTC_DiagBeam( CBaseEntity *pStart, CBaseEntity *pEnd, int iSprite, int iNoise )
{
	if( !DiagOn() )
		return;

	UTIL_LogPrintf(
		"SGTC_DIAG {\"cmd\":\"beam\",\"sprite\":%d,\"noise\":%d,\"start\":%d,\"end\":%d,"
		"\"start_class\":\"%s\",\"end_class\":\"%s\"}\n",
		iSprite, iNoise, pStart ? pStart->entindex() : 0, pEnd ? pEnd->entindex() : 0,
		EntClass( pStart ), EntClass( pEnd ) );
}
