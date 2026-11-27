// sgtc_throwfx.cpp — the shared "throw" playback for SGTC throwables (flashbang,
// gacgoa, couteau and C4). One event, events/throw.sc, carries the release point
// as its origin and the release strength (0..1) as fparam1. The client plays a
// short whoosh there and gives the thrower a small kick (EV_SGTC_Throw).
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "sgtc_weapons.h"
#include "sgtc_throwfx.h"

static BOOL s_bThrowPrecached = FALSE;
static unsigned short s_usThrow = 0;

// Call from each thrower's Precache(). Event indexes are per map, so take it every time.
void SGTC_ThrowFxPrecache( void )
{
	s_usThrow = PRECACHE_EVENT( 1, "events/throw.sc" );
	s_bThrowPrecached = TRUE;
}

void SGTC_PlayThrowFx( CBasePlayer *pPlayer, const Vector &vecOrigin, float flStrength )
{
	if( !pPlayer || !s_bThrowPrecached )
		return;
	if( flStrength < 0.0f )
		flStrength = 0.0f;
	if( flStrength > 1.0f )
		flStrength = 1.0f;

	float origin[3] = { vecOrigin.x, vecOrigin.y, vecOrigin.z };
	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, pPlayer->edict(), s_usThrow,
		0.0, origin, (float *)&g_vecZero, flStrength, 0.0f, 0, 0, 0, 0 );
}
