/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
/*

===== h_export.cpp ========================================================

  Entity classes exported by Halflife.

*/

#include "extdll.h"
#include "util.h"

#include "cbase.h"

// Holds engine functionality callbacks
enginefuncs_t	 g_engfuncs;
globalvars_t	*gpGlobals;

#if XASH_WIN32

// Required DLL entry point
BOOL WINAPI DllMain( HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved )
{
	if( fdwReason == DLL_PROCESS_ATTACH )
	{
	}
	else if( fdwReason == DLL_PROCESS_DETACH )
	{
	}
	return TRUE;
}

// stdcall for win32
#define EXPORT2 WINAPI
#else
#define EXPORT2
#endif

extern "C" void DLLEXPORT EXPORT2 GiveFnptrsToDll( enginefuncs_t *pengfuncsFromEngine, globalvars_t *pGlobals )
{
	memcpy( &g_engfuncs, pengfuncsFromEngine, sizeof(enginefuncs_t) );
	gpGlobals = pGlobals;
}

// build-21: team no-collide.  sgtc_team_nocollide 1 (default) lets players on
// the same SGTC team pass through each other; enemies and other entities keep
// stock collision.  Bots are players too (FL_CLIENT), observers are team 0 and
// are not filtered.
cvar_t sgtc_cvar_team_nocollide = { "sgtc_team_nocollide", "1", FCVAR_SERVER };
// build-22: bots never collide with each other (avoids bot traffic jams / body
// blocking).  Bots are fake clients; human-vs-bot still follows the team rule
// above.  Separate cvar so either behaviour can be turned off independently.
cvar_t sgtc_cvar_bot_nocollide = { "sgtc_bot_nocollide", "1", FCVAR_SERVER };

// The engine runs ShouldCollide for traces as well as for movement, so with team
// no-collide on, a trace fired by a player never reports a player on the same team.
// The medic and the Goa'uld regen weapon heal the player they hit, so their trace
// must see teammates: SGTC_TraceLineTeammates raises this around the trace.
static int s_iTraceTeammates = 0;

void SGTC_TraceLineTeammates( const Vector &vecStart, const Vector &vecEnd, edict_t *pentIgnore, TraceResult *ptr )
{
	s_iTraceTeammates++;
	UTIL_TraceLine( vecStart, vecEnd, dont_ignore_monsters, pentIgnore, ptr );
	s_iTraceTeammates--;
}

extern "C" int SGTC_ShouldCollide( edict_t *pentTouched, edict_t *pentOther )
{
	if( s_iTraceTeammates > 0 )
		return 1;
	cvar_t *pCvar = CVAR_GET_POINTER( "sgtc_team_nocollide" );
	if( !pCvar || pCvar->value <= 0.0f )
		return 1;
	if( !pentTouched || !pentOther || pentTouched == pentOther )
		return 1;
	if( !FBitSet( pentTouched->v.flags, FL_CLIENT ) || !FBitSet( pentOther->v.flags, FL_CLIENT ) )
		return 1;
	// build-22: bots pass through each other regardless of team, so they do not
	// block or push one another (human-vs-bot keeps the team rule below).
	cvar_t *pBotCvar = CVAR_GET_POINTER( "sgtc_bot_nocollide" );
	if( pBotCvar && pBotCvar->value > 0.0f
		&& FBitSet( pentTouched->v.flags, FL_FAKECLIENT )
		&& FBitSet( pentOther->v.flags, FL_FAKECLIENT ) )
		return 0;
	const int iTeamA = (int)pentTouched->v.team;
	const int iTeamB = (int)pentOther->v.team;
	if( iTeamA == 0 || iTeamB == 0 )
		return 1;	// observers/unassigned
	return ( iTeamA == iTeamB ) ? 0 : 1;
}

static NEW_DLL_FUNCTIONS gNewDLLFunctionsTable =
{
	NULL, NULL, SGTC_ShouldCollide, NULL, NULL
};

extern "C" EXPORT int GetNewDLLFunctions( NEW_DLL_FUNCTIONS *pFunctionTable, int *interfaceVersion )
{
	if( !pFunctionTable )
		return 0;
	memcpy( pFunctionTable, &gNewDLLFunctionsTable, sizeof( NEW_DLL_FUNCTIONS ) );
	*interfaceVersion = NEW_DLL_FUNCTIONS_VERSION;
	return 1;
}
