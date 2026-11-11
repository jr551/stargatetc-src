// sgtc_wpn_zat.cpp — StargateTC CZat (split from sgtc_weapons.cpp; NEW file).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
#include "effects.h"
#include "sgtc_weapons.h"
#include "sgtc_misc.h"
#include "sgtc_util.h"
#include "sgtc_sniper.h"	// sway / fire-note hooks

#ifndef CLIENT_DLL
static void SGTC_ZatBeamTEs( CBaseEntity *pProj, int iSprite )
{
	for( int i = 1; i < 5; i++ )
	{
		MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
		WRITE_BYTE( 22 );	// ref TE 0x16
		WRITE_SHORT( pProj->entindex() );
		WRITE_SHORT( iSprite );
		WRITE_BYTE( 1 );
		WRITE_BYTE( 8 );
		WRITE_BYTE( 0x19 );
		WRITE_BYTE( 10 );
		WRITE_BYTE( 255 );
		WRITE_BYTE( 255 );
		MESSAGE_END();
	}
}

static BOOL SGTC_ZatGoaDestroy( CZat *pZat )
{
	CBasePlayer *pl = pZat->m_pPlayer;
	if( pl->pev->team != SGTC_ENTTEAM_TORRI || pl->m_iSgtcPortraitShown != SGTC_CLASS_CARTER ||
		pl->m_iSgtcTalentDeploy != 1 )
		return FALSE;

	pZat->RetireWeapon();	// ref vcall +0x178 RetireWeapon @0x1c5b20
	pl->m_rgAmmo[pZat->m_iPrimaryAmmoType] = 0;
	pl->pev->weapons &= ~( 1 << SGTC_WPN_ZAT );
	pZat->SetThink( &CBasePlayerItem::DestroyItem );
	pZat->pev->nextthink = gpGlobals->time + 0.1f;
	pl->m_iSgtcTalentDeploy = 2;
	return TRUE;
}
#endif

// ===========================================================================
// CZat - "Zat" energy gun. Analogue: gauss.cpp (energy hitscan).
// Events: events/zata.sc (primary) / events/zatb.sc (repeat secondary).
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_zat, CZat );

// Save/restore (#99). Not saved: m_usFireA/m_usFireB/m_iZatWaveSprite (precache handles,
// rebuilt by Precache()).
TYPEDESCRIPTION CZat::m_SaveData[] =
{
	DEFINE_FIELD( CZat, m_iZatCharge, FIELD_INTEGER ),
	DEFINE_FIELD( CZat, m_flZatRegenNext, FIELD_TIME ),
};

IMPLEMENT_SAVERESTORE( CZat, CBasePlayerWeapon )

void CZat::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_zat.mdl" );
	m_iId = SGTC_WPN_ZAT;
	m_iDefaultAmmo = 100;
	m_iZatCharge = 0;
	m_flZatRegenNext = 0.0f;
	FallInit();
}

void CZat::Precache( void )
{
	PRECACHE_MODEL( "models/v_zat.mdl" );
	PRECACHE_MODEL( "models/w_zat.mdl" );
	PRECACHE_MODEL( "models/p_zat.mdl" );
	PRECACHE_MODEL( "sprites/zatimpact.spr" );
	PRECACHE_MODEL( "sprites/zatwave.spr" );

	PRECACHE_SOUND( "weapons/zatvide.wav" );
	PRECACHE_SOUND( "weapons/zatimpact.wav" );

	m_usFireA = PRECACHE_EVENT( 1, "events/zata.sc" );
	m_usFireB = PRECACHE_EVENT( 1, "events/zatb.sc" );
	m_iZatWaveSprite = PRECACHE_MODEL( "sprites/zatwave.spr" );
}

SGTC_GETITEM( CZat, SGTC_AMMO_ENERGIE2, 100, WEAPON_NOCLIP, 1, 2, SGTC_WPN_ZAT, 5, 15 )
SGTC_ADD_TO_PLAYER( CZat )

BOOL CZat::Deploy( void )
{
	m_pPlayer->SGTC_WeapDispSet( 1 );
	m_pPlayer->m_iSgtcAmmoDispLatch = 1;	// ref +0xa1c @0x1c5a8a
	m_pPlayer->m_iSgtcAmmoDispMax = 100;	// ref +0xa20
	ResetEmptySound();			// ref vfunc +0x15c before DefaultDeploy
	return DefaultDeploy( "models/v_zat.mdl", "models/p_zat.mdl", 2, "onehanded" );
}

void CZat::Reload( void )
{
#ifndef CLIENT_DLL
	if( SGTC_ZatGoaDestroy( this ) )
		return;
#endif

	int &pool = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
	if( pool < 100 )
	{
		if( m_iZatCharge > 0 && m_flZatRegenNext < gpGlobals->time )
			m_iZatCharge--;

		// ref +0.4s per +2 cells; SGTC_RegenAmmo clamps to the cap (an odd count
		// plus 2 would overshoot it)
		SGTC_RegenAmmo( pool, m_flZatRegenNext, 100, 2, 0.4f );
	}
}

void CZat::WeaponIdle( void )
{
#ifndef CLIENT_DLL
	if( SGTC_ZatGoaDestroy( this ) )
		return;
#endif

	Reload();
	ResetEmptySound();

	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] == 0 )
		return;

	if( UTIL_SharedRandomFloat( m_pPlayer->random_seed, 0.0f, 1.0f ) <= 0.75f )
	{
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
			UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
		SendWeaponAnim( SGTC_IDLE, 0 );
	}
	else
	{
		m_pPlayer->m_iWeaponVolume = 200;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.5f;
		SendWeaponAnim( 4, 0 );	// ref anim 4
	}
}

void CZat::PrimaryAttack( void )
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )
		return;
	if( UTIL_WeaponTimeBase() < m_flNextPrimaryAttack )
		return;

#ifndef CLIENT_DLL
	if( SGTC_ZatGoaDestroy( this ) )
		return;
#endif

	int &pool = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];

	if( m_pPlayer->m_bHurtInvertFx != 0 )
	{
#ifndef CLIENT_DLL
		for( int i = 0; i < 20; i++ )
		{
			Vector vecOrg = m_pPlayer->pev->origin;
			vecOrg.z += (float)( i * 20 + 100 );
			CBaseEntity *pLarve = CBaseEntity::Create( "monster_larve", vecOrg,
				m_pPlayer->pev->v_angle, m_pPlayer->edict() );
			if( pLarve )
			{
				pLarve->pev->fixangle = 300.0f;	// ref pev+0x28 @0x1c60de cheat loop
				pLarve->pev->velocity.x = RANDOM_FLOAT( 0.0f, 300.0f );
				pLarve->pev->velocity.z = RANDOM_FLOAT( 0.0f, 300.0f );
			}
		}
#endif
		m_flZatRegenNext = gpGlobals->time + 0.8f;
		m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.5f;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
			UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
		return;
	}

	if( pool < 5 )
	{
		m_pPlayer->m_iWeaponVolume = 200;
		EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "weapons/zatvide.wav",
			0.8f, 0.8f );
		m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.5f;
		return;
	}

	Vector angAim = m_pPlayer->pev->v_angle;	// ref pev+0x74 only @0x1c60de
	UTIL_MakeVectors( angAim );
	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecAiming = gpGlobals->v_forward;
	// Bots only: keep the hitscan, the beam trace and the projectile on the
	// same (swayed) vector so they cannot disagree about where the shot went.
	SGTC_Sniper_SwayAimVector( m_pPlayer, &vecAiming );
	SGTC_Sniper_NoteFire( m_pPlayer );
	Vector vecSpread( 0.01f, 0.01f, 0.01f );
	Vector vecDir = m_pPlayer->FireBulletsPlayer( 1, vecSrc, vecAiming,
		vecSpread, 8192.0f, SGTC_BULLET_ZAT, 0, 0,	// ref 0x46000000 dist @0x1c60de
		m_pPlayer->pev, m_pPlayer->random_seed );

	unsigned short usEvent = ( m_iZatCharge < 1 ) ? m_usFireA : m_usFireB;
	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS | FEV_RELIABLE, m_pPlayer->edict(), usEvent,
		0.01f, (float *)&m_pPlayer->pev->origin, (float *)&m_pPlayer->pev->angles,
		vecDir.x, vecDir.y, 0, 0, 0, 0 );	// ref flag 3, before SetAnimation

	m_iZatCharge = 5;		// ref +0x1fc
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
	pool -= 5;

#ifndef CLIENT_DLL
	// ref pev+0x1a4 bit 4 (mapped to pev->flags in port, see sgtc_wpn_larve.cpp)
	if( ( m_pPlayer->pev->flags >> 4 ) & 1 )
	{
		MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
			WRITE_BYTE( 0x1b );	// ref TE byte @0x1c60de (not stock TE_ELIGHT id)
			WRITE_COORD( m_pPlayer->pev->origin.x );
			WRITE_COORD( m_pPlayer->pev->origin.y );
			WRITE_COORD( m_pPlayer->pev->origin.z );
			WRITE_COORD( 10.0f );
			WRITE_BYTE( 0x19 );
			WRITE_BYTE( 10 );
			WRITE_BYTE( 255 );
			WRITE_BYTE( 12 );
			WRITE_BYTE( 0 );
		MESSAGE_END();
		m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.5f;
		m_pPlayer->TakeDamage( m_pPlayer->pev, m_pPlayer->pev, 30.0f,
			DMG_SHOCK );	// ref @0x1b68fc: push $0x100, dmg 30.0
	}

	TraceResult tr;
	UTIL_TraceLine( vecSrc, vecSrc + vecAiming * 8192.0f,
		dont_ignore_monsters, m_pPlayer->edict(), &tr );

	Vector vecSpawn = tr.vecEndPos;
	Vector vecDelta = vecSpawn - vecSrc;
	CZatProj *pProj = CZatProj::CreateZatProj( vecSpawn, vecDelta,
		m_pPlayer, this );
	if( pProj )
	{
		if( gpGlobals->time < m_pPlayer->m_flSgtcShieldTime )
			pProj->m_bAllyShield = 1;	// ref proj+0xd5, player+0xa2c @0x1c60de
		extern cvar_t sgtc_cvar_oldzat;	// ref oldzat @0x21359c
		if( sgtc_cvar_oldzat.value != 0.0f )
			pProj->m_bBeamMode = 1;		// ref proj+0x40c
		pProj->pev->angles = m_pPlayer->pev->v_angle;
		pProj->pev->v_angle = m_pPlayer->pev->v_angle;	// ref pev+0x74 copy @0x1c60de
		SGTC_ZatBeamTEs( pProj, m_iZatWaveSprite );
	}
	else
	{
		// The projectile emits the zatimpact sprite on contact (see
		// sgtc_misc.cpp SGTC_ZatImpactFx); only when it failed to spawn does
		// the server trace have to show the impact itself.
		CSprite *pImpact = CSprite::SpriteCreate( "sprites/zatimpact.spr", tr.vecEndPos, TRUE );
		if( pImpact )
		{
			pImpact->AnimateAndDie( 15.0f );
			pImpact->SetTransparency( kRenderTransAdd, 255, 255, 255, 200, 14 );
		}
	}
#endif

	m_pPlayer->SnagInvis();
	m_flZatRegenNext = gpGlobals->time + 0.8f;
	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.52f;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
		UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );

}

// ref: CZat has NO SecondaryAttack (inherits CBasePlayerWeapon no-op @0xc863a).
SGTC_NO_SECONDARY( CZat )
