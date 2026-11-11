// sgtc_token.cpp — CToken / game_nuke.
// REA-verified against ref/stc_i386.so (Ghidra addrs):
//   Spawn 0x1a4244, Precache 0x1a440c, KeyValue 0x1a4626,
//   Activator 0x1a446c, Deconax 0x1a46b2, Think 0x1a4708.
// pev field map (ref entvars): +0x104 nextthink, +0x108 movetype, +0x10c solid,
//   +0x148 rendermode, +0x14c renderamt, +0x150..0x158 rendercolor,
//   +0x15c renderfx, +0x160 health (bomb state marker), +0x16c dmg,
//   +0x174 view_ofs (vec3).
// Spawn writes: m_pfnThink @this+0x134=Think fn? (ref sets +0x134/0x138 to
//   function/0: standard pev-style state, represented by m_iBombState here).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "gamerules.h"
#include "weapons.h"
#include "sgtc_rules.h"
#include "sgtc_util.h"
#include "sgtc_token.h"

// SGTC endo helpers (ref @0x1abc72 / 0x1abc1c / 0x1ab90e); 0x1abc1c is
// SGTC_Distance3D in sgtc_util.h.
static int SGTC_Endo_intconstrain( int lo, int hi, int v )
{
	return v < lo ? lo : ( v > hi ? hi : v );
}
static int SGTC_Endo_takefromlarger( int a, int b )
{
	return a > b ? a - b : b - a;
}

int sgtc_type = 0;		// ref BSS "type" @0x236588 (2 = CP-style capture)
// ref BSS globals (stc_i386.so): firstplace @0x2368d4, LastKeyPlace @0x2368e0,
// GlobalBombState @0x23710c, bomby @0x2368c4.
Vector sgtc_firstplace;
Vector sgtc_LastKeyPlace;
char sgtc_KeyInPlay = 0;	// ref BSS KeyInPlay @0x2368c6 (indirect in .so)
int sgtc_GlobalBombState = 0;
int sgtc_bomby = 0;
void *sgtc_superguns = NULL;	// ref GLOB_DAT superguns @0x2134d4 (fld DWORD @+0xc)

LINK_ENTITY_TO_CLASS( game_nuke, CToken );

void CToken::Precache( void )
{
	// ref Precache: bomb.mdl + nukepower.wav + cliprelease1.wav ONLY.
	PRECACHE_MODEL( "models/bomb.mdl" );
	PRECACHE_SOUND( "misc/nukepower.wav" );
	PRECACHE_SOUND( "items/cliprelease1.wav" );
}

void CToken::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "countdown" ) )
	{
		m_iCountdown = (int)atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseDelay::KeyValue( pkvd );
}

void CToken::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/bomb.mdl" );
	pev->classname = MAKE_STRING( "game_nuke" );
	pev->dmg = 0;				// +0x16c
	pev->health = 100.0f;			// +0x160 = 0x42c80000
	pev->view_ofs = Vector( 0, 0, 0 );	// +0x174
	pev->movetype = MOVETYPE_NONE;		// +0x108 = 0
	pev->solid = SOLID_BBOX;		// +0x10c = 2
	pev->renderamt = 255.0f;		// +0x14c
	UTIL_SetOrigin( pev, pev->origin );
	UTIL_SetSize( pev, Vector( -18, -2, 0 ), Vector( 18, 2, 12 ) );
	pev->nextthink = gpGlobals->time + 0.1f; // +0x104
	m_iBombState = 0xc5;
	m_iArmed = 0;
	if( m_iCountdown <= 0 )
		m_iCountdown = 15;
}

void CToken::Deconax( void )
{
	// ref @0x1a46b2.
	pev->renderfx = 0;
	pev->rendermode = 0;
	pev->renderamt = 255.0f;
	pev->health = 100.0f;
}

void CToken::ResetForNewRound( void )
{
	// ref CStargateTeamDM::NewRound @0x194f42 game_nuke walk.
	pev->health = 30.0f;	// 0x41f00000 — arms Deconax path, avoids detonation
	Think();
}

// ref @0x1a446c: returns 1 when a qualifying player finished arming.
int CToken::GetObjectiveState( void ) const
{
	// low byte = bomb state, bit8 = armed, upper bits = countdown seconds.
	return ( m_iBombState & 0xff ) | ( ( m_iArmed ? 1 : 0 ) << 8 ) | ( m_iCountdown << 16 );
}

int SGTC_Token_ObjectiveState( CBaseEntity *pEnt )
{
	if( !pEnt || !pEnt->pev || !FClassnameIs( pEnt->pev, "game_nuke" ) )
		return -1;
	return ( (CToken *)pEnt )->GetObjectiveState();
}

int CToken::Activator( CBaseEntity *pPlayer )
{
	if( !pPlayer )
		return 0;

	// ref vtable+0xa8 on player — IsAlive()-style gate.
	if( !pPlayer->IsAlive() )
		return 0;

	CBasePlayer *pl = (CBasePlayer *)pPlayer;
	if( SGTC_Distance3D( pl->pev->origin, pev->origin ) < 0x3d &&
	    SGTC_Endo_takefromlarger( (int)pl->pev->origin.z,
		(int)pev->origin.z ) <= 60 )
	{
		if( pl->m_iInBombZone >= 1 )
		{
			if( pl->m_iNukeCharge <= 99 )
			{
				pl->m_iNukeCharge++;
				pl->m_flNukeChargeTime = gpGlobals->time + 0.15f;
				return 0;
			}
			pl->m_iInBombZone = 0;
			pl->m_iNukeBonus += 4;
			// ref @0x1945c2: SUB_UseTargets(USE_TOGGLE=3, value rodata 1.625f).
			SUB_UseTargets( this, USE_TOGGLE, 1.625f );
			return 1;
		}
	}
	return 0;
}

void CToken::Think( void )
{
	int local_24 = SGTC_Endo_intconstrain( 5, 0x78, m_iCountdown );

	// health <= 30 -> defused.
	if( pev->health <= 30.0f )
		Deconax();

	// health <= 50 -> detonation pass (ref @0x194795: once per tick, not per player).
	if( pev->health <= 50.0f )
	{
		pev->renderfx = 0;
		pev->rendermode = 0;
		pev->renderamt = 255.0f;

		CBaseEntity *pScan = NULL;
		while( ( pScan = UTIL_FindEntityByClassname( pScan, "player" ) ) != NULL )
			((CBasePlayer *)pScan)->m_iInBombZone = 0;

		CBaseEntity *pObj = NULL;
		while( ( pObj = UTIL_FindEntityByClassname( pObj, "item_objective" ) ) != NULL )
			UTIL_Remove( pObj );

		CBaseEntity::Create( "item_objective", sgtc_firstplace,
			Vector( 0, 0, 0 ), NULL );
		sgtc_LastKeyPlace = sgtc_firstplace;
		sgtc_bomby = 0;

		CBaseEntity *pPlayer = NULL;
		while( ( pPlayer = UTIL_FindEntityByClassname( pPlayer, "player" ) ) != NULL )
		{
			CBasePlayer *pl = (CBasePlayer *)pPlayer;
			if( pl->m_flNukeGrace < gpGlobals->time )
			{
				UTIL_ScreenFade( pPlayer, Vector( 255, 255, 255 ),
					0.5f, 4.7f, 0xff, 0 );
				// ref @0x19492a: damage type 0x80 (DMG_CLUB bit), not DMG_BLAST.
				pl->TakeDamage( pl->pev, pev, 10000.0f, (int)0x80 );
			}
		}
		pev->health = 100.0f;
		if( CStargateTeamDM *pRules = SGTC_GetTeamDMRules() )
			pRules->Goal( 2 ); // ref g_pGameRules vtbl+0x30
	}

	pev->nextthink = gpGlobals->time + 0.1f;

	// arming pass: players in proximity charge the bomb.
	CBaseEntity *pPlayer = NULL;
	while( ( pPlayer = UTIL_FindEntityByClassname( pPlayer, "player" ) ) != NULL )
	{
		if( Activator( pPlayer ) )
			pev->health = 50.0f;
	}

	// health <= 50 again (just armed) -> start countdown FX.
	if( pev->health <= 50.0f )
	{
		pev->nextthink = gpGlobals->time + (float)local_24;

		// ref message block: MSG_BROADCAST SVC_TEMPENTITY TE_DLIGHT.
		MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY, pev->origin );
			WRITE_BYTE( TE_DLIGHT );
			WRITE_COORD( pev->origin.x );
			WRITE_COORD( pev->origin.y );
			WRITE_COORD( pev->origin.z );
			WRITE_BYTE( 0x46 );	// radius 70
			WRITE_BYTE( 0xff );
			WRITE_BYTE( 0x68 );
			WRITE_BYTE( 0x28 );
			WRITE_BYTE( local_24 * 0x14 );	// life
			WRITE_BYTE( 0 );		// decay
		MESSAGE_END();

		pev->renderfx = 0x13;			// +0x15c = 19
		pev->renderamt = 10.0f;			// +0x14c
		pev->rendercolor = Vector( 255.0f, 104.0f, 40.0f ); // +0x150..158
		EMIT_SOUND( ENT( pev ), CHAN_VOICE, "misc/nukepower.wav", 1.0, 0.8 );
		// ref CToken::Think @0x194c48: chan 4 (BODY), not ITEM.
		EMIT_SOUND( ENT( pev ), CHAN_BODY, "items/cliprelease1.wav", 1.0, 0.8 );
		UTIL_ScreenShake( pev->origin, 10.0f, (float)( local_24 * 0x14 ),
			(float)local_24, 2000.0f );
		sgtc_LastKeyPlace = sgtc_firstplace;
		sgtc_GlobalBombState = 2;
		sgtc_bomby = 1;
	}
}
