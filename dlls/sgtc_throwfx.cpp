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

// The stock handgrenade (handgrenade.cpp, left unedited) plays no event on a
// throw. Watch each player instead: while weapon_handgrenade is the active item,
// a drop in its ammo is a throw. If the last grenade removes the weapon in the
// same frame, the ammo index kept from the previous frame still catches the drop.
#define SGTC_THROWFX_SLOTS 33
static int s_iHgAmmo[SGTC_THROWFX_SLOTS];	// last seen grenade count, -1 when not holding one
static int s_iHgAmmoIdx[SGTC_THROWFX_SLOTS];	// that grenade's primary ammo index

void SGTC_ThrowFx_Frame( void )
{
	int i;
	static BOOL s_bWatchInit = FALSE;

	if( !s_bThrowPrecached )
		return;
	if( !s_bWatchInit )
	{
		for( i = 0; i < SGTC_THROWFX_SLOTS; i++ )
		{
			s_iHgAmmo[i] = -1;
			s_iHgAmmoIdx[i] = 0;
		}
		s_bWatchInit = TRUE;
	}

	for( i = 1; i <= gpGlobals->maxClients && i < SGTC_THROWFX_SLOTS; i++ )
	{
		CBasePlayer *pPlayer = (CBasePlayer *)UTIL_PlayerByIndex( i );
		CBasePlayerItem *pItem = ( pPlayer && pPlayer->IsAlive() ) ? pPlayer->m_pActiveItem : NULL;
		const BOOL bHolding = ( pItem && FClassnameIs( pItem->pev, "weapon_handgrenade" ) ) ? TRUE : FALSE;

		if( bHolding )
		{
			CBasePlayerWeapon *pWeapon = (CBasePlayerWeapon *)pItem;
			const int iIdx = pWeapon->m_iPrimaryAmmoType;
			const int iAmmo = pPlayer->m_rgAmmo[iIdx];
			if( s_iHgAmmo[i] >= 0 && iAmmo < s_iHgAmmo[i] )
				SGTC_PlayThrowFx( pPlayer, pPlayer->GetGunPosition(), 0.5f );
			s_iHgAmmo[i] = iAmmo;
			s_iHgAmmoIdx[i] = iIdx;
		}
		else if( s_iHgAmmo[i] >= 0 )
		{
			// The grenade left our hands this frame: its ammo may have dropped on
			// the way out, so compare once more with the index kept from last frame.
			if( pPlayer && pPlayer->m_rgAmmo[s_iHgAmmoIdx[i]] < s_iHgAmmo[i] )
				SGTC_PlayThrowFx( pPlayer, pPlayer->GetGunPosition(), 0.5f );
			s_iHgAmmo[i] = -1;
		}
	}
}
