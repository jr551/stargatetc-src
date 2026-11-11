// sgtc_cvar.cpp — build-11: register game-DLL cvars without losing config or
// launcher (+flag) values.
//
// The engine creates a cvar for a launcher `+flag`/early cfg before the game
// DLL registers it.  CVAR_REGISTER() then hits "already defined as a var" and
// leaves the cvar_t in this DLL at its compiled default, so features like
// sv_botdirector/sgtc_bot_quota silently ignore the configured value.
//
// SGTC_RegisterOrAdoptCvar() adopts an existing engine cvar instead of
// registering a duplicate; SGTC_CvarAdoptSync() runs once per frame and keeps
// the adopted structs in step with runtime `set`/config changes.

#include "extdll.h"
#include "util.h"
#include "cbase.h"

typedef struct
{
	cvar_t *pGame;
	cvar_t *pEngine;
} sgtc_adopted_cvar_t;

static sgtc_adopted_cvar_t s_adopted[512];
static int s_adoptedCount = 0;

static void SGTC_AdoptCvarValue( cvar_t *pGame, cvar_t *pEngine )
{
	if( !pGame || !pEngine )
		return;
	pGame->value = pEngine->value;
	if( pEngine->string )
		pGame->string = pEngine->string;
}

void SGTC_RegisterOrAdoptCvar( cvar_t *pCvar )
{
	if( !pCvar || !pCvar->name )
		return;

	cvar_t *pExisting = CVAR_GET_POINTER( pCvar->name );
	if( pExisting && pExisting != pCvar )
	{
		// Already created by the engine/cfg/launcher (or another DLL module):
		// adopt it and skip registration so the engine keeps its single cvar.
		SGTC_AdoptCvarValue( pCvar, pExisting );
		for( int i = 0; i < s_adoptedCount; ++i )
		{
			if( s_adopted[i].pGame == pCvar )
			{
				s_adopted[i].pEngine = pExisting;
				return;
			}
		}
		if( s_adoptedCount < (int)( sizeof( s_adopted ) / sizeof( s_adopted[0] ) ) )
		{
			s_adopted[s_adoptedCount].pGame = pCvar;
			s_adopted[s_adoptedCount].pEngine = pExisting;
			++s_adoptedCount;
		}
		return;
	}

	CVAR_REGISTER( pCvar );
}

void SGTC_CvarAdoptSync( void )
{
	for( int i = 0; i < s_adoptedCount; ++i )
	{
		cvar_t *pGame = s_adopted[i].pGame;
		if( !pGame || !pGame->name )
			continue;
		cvar_t *pEngine = CVAR_GET_POINTER( pGame->name );
		if( !pEngine )
			continue;
		if( pGame->value != pEngine->value || pGame->string != pEngine->string )
			SGTC_AdoptCvarValue( pGame, pEngine );
	}
}
