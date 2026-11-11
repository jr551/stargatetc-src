// sgtc_muzzlefx.cpp — visible muzzle flash light + nozzle smoke for bullet
// weapons.  Server-side temp entities only; no client changes.  Every player
// bullet path (humans and bots) funnels through CBaseEntity::FireBullets.
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "weapons.h"
#include "sgtc_diag.h"
#include "sgtc_muzzlefx.h"

cvar_t sgtc_cvar_muzzle_fx = { "sgtc_muzzle_fx", "1", FCVAR_SERVER };

static float s_flLastLight[33];
static float s_flLastSmoke[33];

void SGTC_MuzzleFxInit( void )
{
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_muzzle_fx );
	memset( s_flLastLight, 0, sizeof( s_flLastLight ) );
	memset( s_flLastSmoke, 0, sizeof( s_flLastSmoke ) );
}

// Called from CBaseEntity::FireBullets for every bullet shot (cShots loop is
// outside; one flash per FireBullets call, matching EF_MUZZLEFLASH).
void SGTC_MuzzleFx( entvars_t *pev, const Vector &vecSrc, int iBulletType )
{
	if( !pev || !( pev->flags & FL_CLIENT ) )
		return;
	if( sgtc_cvar_muzzle_fx.value <= 0.0f )
		return;
	const int iEnt = ENTINDEX( ENT( pev ) );
	if( iEnt <= 0 || iEnt >= 33 )
		return;

	const float flNow = gpGlobals->time;
	const Vector vecLight = vecSrc + gpGlobals->v_forward * 4.0f;
	const Vector vecSmoke = vecSrc + gpGlobals->v_forward * 8.0f;

	// Dynamic light: one per 50 ms per player so autos don't stack.
	if( flNow - s_flLastLight[iEnt] >= 0.05f )
	{
		s_flLastLight[iEnt] = flNow;
		MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, vecLight );
			WRITE_BYTE( TE_DLIGHT );
			WRITE_COORD( vecLight.x );
			WRITE_COORD( vecLight.y );
			WRITE_COORD( vecLight.z );
			WRITE_BYTE( 140 );	// radius
			WRITE_BYTE( 255 );
			WRITE_BYTE( 190 );
			WRITE_BYTE( 80 );
			WRITE_BYTE( 1 );	// 0.1 s
			WRITE_BYTE( 500 );	// decay
		MESSAGE_END();
	}

	// Muzzle smoke: one per 60 ms per player; TE_SMOKE carries model/scale/
	// framerate only, so the "velocity" is faked by a small random origin jitter.
	if( flNow - s_flLastSmoke[iEnt] >= 0.06f && g_sModelIndexSmoke > 0 )
	{
		s_flLastSmoke[iEnt] = flNow;
		Vector vecPuff = vecSmoke;
		vecPuff.x += RANDOM_FLOAT( -2.0f, 2.0f );
		vecPuff.y += RANDOM_FLOAT( -2.0f, 2.0f );
		vecPuff.z += RANDOM_FLOAT( 1.0f, 4.0f );
		MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, vecPuff );
			WRITE_BYTE( TE_SMOKE );
			WRITE_SHORT( g_sModelIndexSmoke );
			WRITE_COORD( vecPuff.x );
			WRITE_COORD( vecPuff.y );
			WRITE_COORD( vecPuff.z );
			WRITE_BYTE( (int)RANDOM_LONG( 2, 4 ) );	// scale
			WRITE_BYTE( (int)RANDOM_LONG( 30, 60 ) );	// framerate
		MESSAGE_END();
	}

	if( SGTC_DiagEnabled() )
		SGTC_DiagMuzzle( CBaseEntity::Instance( ENT( pev ) ), iBulletType );
}

// Cheap opt-in counter: logs every 32nd muzzle event.
void SGTC_DiagMuzzle( CBaseEntity *pPlayer, int iBulletType )
{
	static int s_iCount = 0;
	if( ( ++s_iCount % 32 ) != 0 )
		return;
	ALERT( at_console, "SGTC_MUZZLE {\"player\":%d,\"bullet\":%d,\"count\":%d}\n",
		pPlayer ? pPlayer->entindex() : 0, iBulletType, s_iCount );
}
