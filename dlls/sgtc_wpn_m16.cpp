// sgtc_wpn_m16.cpp — StargateTC CM16 (split from sgtc_weapons.cpp; NEW file).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
#include "sgtc_weapons.h"

// ===========================================================================
// CM16 - "M16" autorifle. Analogue: mp5.cpp.
// Event: events/m16.sc.
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_m16, CM16 );

// Save/restore (#99). Not saved: m_usFire (event handle, rebuilt by Precache()).
TYPEDESCRIPTION CM16::m_SaveData[] =
{
	DEFINE_FIELD( CM16, m_iSgtcZoomActive, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE( CM16, CBasePlayerWeapon )

void CM16::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_m16.mdl" );
	m_iId = SGTC_WPN_M16;
	m_iDefaultAmmo = 210;
	m_iSgtcZoomActive = 0;
	FallInit();
}

void CM16::Precache( void )
{
	PRECACHE_MODEL( "models/v_m16.mdl" );
	PRECACHE_MODEL( "models/w_m16.mdl" );
	PRECACHE_MODEL( "models/p_m16.mdl" );
	PRECACHE_MODEL( "models/rshell.mdl" );
	PRECACHE_MODEL( "models/w_9mmARclip.mdl" );

	PRECACHE_SOUND( "items/9mmclip1.wav" );
	PRECACHE_SOUND( "items/clipinsert1.wav" );
	PRECACHE_SOUND( "items/cliprelease1.wav" );
	PRECACHE_SOUND( "weapons/M16-01.wav" );
	PRECACHE_SOUND( "weapons/357_cock1.wav" );

	m_usFire = PRECACHE_EVENT( 1, "events/m16.sc" );
}

SGTC_GETITEM( CM16, SGTC_AMMO_M16, 210, 30, 2, 1, SGTC_WPN_M16, 0, 15 )
SGTC_ADD_TO_PLAYER( CM16 )

BOOL CM16::Deploy( void )
{
	return SGTC_DEPLOY( 2, "models/v_m16.mdl", "models/p_m16.mdl", SGTC_DRAW, "mp5" );
}

void CM16::Holster( int skiplocal )	// ref @0x1073ae — reset zoom latch
{
	m_iSgtcZoomActive = 0;
	m_pPlayer->pev->fov = 0.0f;
	m_pPlayer->m_iFOV = 0;
	CBasePlayerWeapon::Holster( skiplocal );
}

void CM16::SecondaryAttack( void )	// ref @0x107020
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )
		return;

	if( m_pPlayer->pev->fov == 30.0f )
	{
		m_pPlayer->m_iFOV = 0;
		m_pPlayer->pev->fov = 0.0f;
		m_iSgtcZoomActive = 0;
	}
	else
	{
		m_pPlayer->m_iFOV = 0x1e;
		m_pPlayer->pev->fov = 30.0f;
		m_iSgtcZoomActive = 1;
	}

	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.5f;
}

void CM16::PrimaryAttack( void )
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )
		return;

	if( m_pPlayer->pev->waterlevel == 3 )
	{
		PlayEmptySound();
		m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.15f;
		return;
	}
	if( m_iClip <= 0 )
	{
		PlayEmptySound();
		m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.15f;
		return;
	}
	m_pPlayer->m_iWeaponVolume = NORMAL_GUN_VOLUME;
	m_pPlayer->m_iWeaponFlash = NORMAL_GUN_FLASH;
	m_iClip--;
	m_pPlayer->pev->effects = (int)( m_pPlayer->pev->effects ) | EF_MUZZLEFLASH;
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecAiming = m_pPlayer->GetAutoaimVector( AUTOAIM_5DEGREES );
	Vector vecDir = m_pPlayer->FireBulletsPlayer( 1, vecSrc, vecAiming,
		VECTOR_CONE_5DEGREES, 8192, SGTC_BULLET_M16, 2, 8, m_pPlayer->pev, m_pPlayer->random_seed );
	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFire,
		0.0, (float *)&g_vecZero, (float *)&g_vecZero, vecDir.x, vecDir.y,
		0, 0, 0, 0 );
	if( !m_iClip && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
		m_pPlayer->SetSuitUpdate( "!HEV_AMO0", FALSE, 0 );
	m_flNextPrimaryAttack = GetNextAttackDelay( 0.1f );
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
		UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );
}

void CM16::Reload( void )	// ref @0x10715a
{
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 || m_iClip == 30 )
		return;
	// Ref: no reload while scope active (m_iFOV 0x1e or weapon +0x208 latch).
	if( m_pPlayer->m_iFOV == 0x1e || m_iSgtcZoomActive )
		return;
	DefaultReload( 30, SGTC_RELOAD, 1.5f );
}

SGTC_WEAPON_IDLE( CM16 )
