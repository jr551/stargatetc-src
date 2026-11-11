// sgtc_wpn_reetou.cpp — StargateTC CReetou (split from sgtc_weapons.cpp; NEW file).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
#include "sgtc_weapons.h"
#include "sgtc_lanceproj.h"
#include "sgtc_util.h"

// ===========================================================================
// CReetou - Reetou disruptor. Analogue: hornetgun.cpp (energy projectile).
// Events: events/reetou.sc (primary) / events/reetou2.sc (secondary).
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_reetou, CReetou );

// Save/restore (#99). Not saved: m_usFire/m_usFire2 (event handles, rebuilt by Precache()).
TYPEDESCRIPTION CReetou::m_SaveData[] =
{
	DEFINE_FIELD( CReetou, m_flSgtcRegenNext, FIELD_TIME ),
	DEFINE_FIELD( CReetou, m_bSgtcCloakOn, FIELD_INTEGER ),
	DEFINE_FIELD( CReetou, m_flSgtcCloakTimer, FIELD_TIME ),
};

IMPLEMENT_SAVERESTORE( CReetou, CBasePlayerWeapon )

void CReetou::Spawn( void )	// ref @0x162300
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_reetou.mdl" );
	m_iId = SGTC_WPN_REETOU;
	m_iDefaultAmmo = 10;
	m_bSgtcCloakOn = FALSE;		// ref +0x204
	m_flSgtcRegenNext = 0.0f;
	SetThink( &CReetou::ThinkGuardStub );	// ref +0x134 = 0x175
	pev->nextthink = 0.0f;
	FallInit();
}

void CReetou::Precache( void )	// ref @0x1623b4
{
	PRECACHE_MODEL( "models/v_reetou.mdl" );
	PRECACHE_MODEL( "models/w_reetou.mdl" );
	PRECACHE_MODEL( "models/p_reetou.mdl" );

	PRECACHE_SOUND( "weapons/invis.wav" );
	PRECACHE_SOUND( "weapons/invisdec.wav" );

	UTIL_PrecacheOther( "reetou_proj" );	// ref: proj model in entity Precache

	m_usFire = PRECACHE_EVENT( 1, "events/reetou.sc" );
	m_usFire2 = PRECACHE_EVENT( 1, "events/reetou2.sc" );
}

SGTC_GETITEM( CReetou, SGTC_AMMO_TER, 10, WEAPON_NOCLIP, 4, 4, SGTC_WPN_REETOU, 5, -10 )
SGTC_ADD_TO_PLAYER( CReetou )

BOOL CReetou::Deploy( void )	// ref @0x162552
{
	m_pPlayer->SGTC_WeapDispSet( 1 );
	m_bSgtcCloakOn = FALSE;	// ref +0x204
	SetThink( &CReetou::ThinkGuardStub );	// ref +0x134 = 0x175
	pev->nextthink = 0.0f;
	return DefaultDeploy( "models/v_reetou.mdl", "models/p_reetou.mdl", 4,
		"gauss" );	// ref anim 4, not SGTC_DRAW
}

void CReetou::Holster( int skiplocal )	// ref @0x1625e6
{
	if( m_bSgtcCloakOn )
		DisarmCloakScan();	// ref vcall weapon+0x16c when +0x204 set
	m_bSgtcCloakOn = FALSE;
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.3f;	// ref player +0x39c
	pev->nextthink = gpGlobals->time + 0.1f;
}

void CReetou::DisarmCloakScan( void )
{
	// build-21: restore exactly the render state saved when the scan armed.
	if( m_bSgtcPinkSaved && m_pPlayer )
	{
		m_pPlayer->pev->renderfx = (float)m_iSgtcSavedRenderFx;
		m_pPlayer->pev->renderamt = m_flSgtcSavedRenderAmt;
		m_pPlayer->pev->rendercolor = m_vecSgtcSavedRenderColor;
	}
	m_bSgtcPinkSaved = FALSE;
	// ref Holster cloak path: tear down SearchInvisible think (vtable +0x16c).
	SetThink( &CReetou::ThinkGuardStub );
	ThinkGuardStub();
}

void CReetou::PrimaryAttack( void )	// ref @0x162f4c
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )	// ref +0x408
		return;

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] < 1 )
	{
		m_flNextPrimaryAttack = GetNextAttackDelay( 0.6f );	// ref 0x3e19999a
		return;
	}

	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFire,
		0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0, 0, 0, 0, 0, 0 );

	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );	// ref anim 6
	m_pPlayer->SnagInvis();

#ifndef CLIENT_DLL
	UTIL_MakeVectors( m_pPlayer->pev->v_angle );	// ref +0x74
	Vector vecDir = gpGlobals->v_forward;
	Vector vecSrc = m_pPlayer->GetGunPosition();
	TraceResult tr;
	UTIL_TraceLine( vecSrc, vecSrc + vecDir * 8192.0f, dont_ignore_monsters,
		m_pPlayer->edict(), &tr );

	Vector vecBoltStart = vecSrc + vecDir * 16.0f;
	CReetouProj *pBolt = CReetouProj::CreateReetouProj( vecBoltStart,
		tr.vecEndPos, m_pPlayer, (CReetou *)this );

	if( pBolt && gpGlobals->time < m_pPlayer->m_flSgtcShieldTime )	// ref +0xa2c
		pBolt->m_fAttendExplosion = TRUE;	// ref bolt+0xd5
#endif

	m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]--;
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.3f;	// ref +0x1d8
	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 1.0f;	// ref +0x1d4

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
		m_pPlayer->SetSuitUpdate( "!HEV_AMO0", FALSE, 0 );
}

void CReetou::SecondaryAttack( void )	// ref @0x162680
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )	// ref +0x408
		return;

	m_bSgtcCloakOn = !m_bSgtcCloakOn;	// ref this+0x204 toggle

	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFire2,
		0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0, 0, 0, 0, 0, 0 );
	EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "weapons/invisdec.wav",
		1.0f, 0.8f );

	m_flSgtcCloakTimer = gpGlobals->time + 1.0f;	// ref this+0x1fc
	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 1.0f;	// ref +0x1d4
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 3.0f;	// ref +0x1d8

	if( m_bSgtcCloakOn )
	{
		// build-21: scanner glows pink while the cloak scan is active.
		if( !m_bSgtcPinkSaved )
		{
			m_iSgtcSavedRenderFx = (int)m_pPlayer->pev->renderfx;
			m_flSgtcSavedRenderAmt = m_pPlayer->pev->renderamt;
			m_vecSgtcSavedRenderColor = m_pPlayer->pev->rendercolor;
			m_bSgtcPinkSaved = TRUE;
		}
		m_pPlayer->pev->renderfx = kRenderFxGlowShell;
		m_pPlayer->pev->rendercolor = Vector( 255.0f, 105.0f, 180.0f );
		m_pPlayer->pev->renderamt = 130.0f;
		SetThink( &CReetou::ThinkSearchInvisible );	// ref SearchInvisible @0x162846
	}
	else
	{
		DisarmCloakScan();
		SetThink( &CReetou::ThinkGuardStub );	// ref stub @0x175
	}
	pev->nextthink = gpGlobals->time + 0.3f;
}

void CReetou::ThinkGuardStub( void )	// ref stub @0x175
{
	SetThink( NULL );
}

void CReetou::ThinkSearchInvisible( void )	// ref SearchInvisible @0x162846
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )	// ref player +0x408
		return;

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] < 10 &&
		m_flSgtcRegenNext < gpGlobals->time )
	{
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]++;
		m_flSgtcRegenNext = gpGlobals->time + 2.25f;	// ref +2.25 in think path
	}

	// build-21: pink scanner pulse so the active scan is readable at night.
	{
		Vector vecPos = m_pPlayer->GetGunPosition() + gpGlobals->v_forward * 16.0f;
		MESSAGE_BEGIN( MSG_PAS, SVC_TEMPENTITY, vecPos );
			WRITE_BYTE( TE_DLIGHT );
			WRITE_COORD( vecPos.x );
			WRITE_COORD( vecPos.y );
			WRITE_COORD( vecPos.z );
			WRITE_BYTE( 140 );
			WRITE_BYTE( 255 );
			WRITE_BYTE( 105 );
			WRITE_BYTE( 180 );
			WRITE_BYTE( 2 );
			WRITE_BYTE( 300 );
		MESSAGE_END();
	}

	const int iMaxDist = 600;
	int iConeStep = 60;
	UTIL_MakeVectors( m_pPlayer->pev->v_angle );
	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecFwd = gpGlobals->v_forward;

	if( m_flSgtcCloakTimer < gpGlobals->time )
	{
		EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "weapons/invisdec.wav",
			1.0f, 0.8f );
		m_flSgtcCloakTimer = gpGlobals->time + 1.0f;	// ref this+0x1fc
	}

	for( int iStep = 0; iStep < iMaxDist / iConeStep; iStep++ )
	{
		TraceResult tr;
		Vector vecEnd = vecSrc + vecFwd * (float)( iStep * iConeStep );
		UTIL_TraceLine( vecSrc, vecEnd, dont_ignore_monsters,
			m_pPlayer->edict(), &tr );

		iConeStep = (int)roundf( iConeStep * 1.06f );
		float flRadius = (float)iConeStep;

		RadiusDamage( tr.vecEndPos, m_pPlayer->pev, m_pPlayer->pev, 3.0f,
			flRadius, 0, DMG_RADIATION );	// ref 0x40400000 dmg, bits 0x40000

		// build-13 TER scan: reveal cloaked players inside the pulse radius so
		// bots/observers can see them (ref SearchInvisible 0x152846 clears the
		// invis latch on targets it finds).
		for( int iClients = 1; iClients <= gpGlobals->maxClients; ++iClients )
		{
			CBasePlayer *pTgt = (CBasePlayer *)UTIL_PlayerByIndex( iClients );
			if( !pTgt || pTgt == m_pPlayer || !pTgt->IsAlive() || !pTgt->m_bShieldSprint )
				continue;
			if( SGTC_Distance3D( tr.vecEndPos, pTgt->pev->origin ) > iConeStep )
				continue;
			pTgt->m_bShieldSprint = 0;
			pTgt->pev->rendermode = 0;
			pTgt->pev->renderamt = 255.0f;
			pTgt->m_flSgtcInvisReveal = gpGlobals->time;
		}

		static const char *sRevealClasses[2] = { "monster_mine", "monster_tac" };
		for( int iClass = 0; iClass < 2; iClass++ )
		{
			CBaseEntity *pEnt = NULL;
			while( ( pEnt = UTIL_FindEntityByClassname( pEnt,
				sRevealClasses[iClass] ) ) != NULL )
			{
				if( pEnt->pev->renderamt >= 200.0f )
					continue;
				if( SGTC_Distance3D( tr.vecEndPos, pEnt->pev->origin )
					> iConeStep )
					continue;
				pEnt->pev->renderamt = 200.0f;	// ref +0x14c = 0x43480000
			}
		}
	}

	pev->nextthink = gpGlobals->time + 0.15f;	// ref pev+0x104
}

void CReetou::WeaponIdle( void )	// ref @0x162c7a
{
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] < 10 &&
		m_flSgtcRegenNext < gpGlobals->time )
	{
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]++;
		m_flSgtcRegenNext = gpGlobals->time + 2.0f;	// ref +0x200 latch
	}

	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;

	int iAnim;
	if( UTIL_SharedRandomFloat( m_pPlayer->random_seed, 0.0f, 1.0f ) <= 0.75f )
	{
		iAnim = 0;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
			UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
	}
	else
	{
		iAnim = 5;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.5f;
	}

	SendWeaponAnim( iAnim );
	pev->nextthink = gpGlobals->time + 0.15f;
}

SGTC_NO_RELOAD( CReetou )
