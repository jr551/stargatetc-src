// sgtc_wpn_p90.cpp — StargateTC CP90 (split from sgtc_weapons.cpp; NEW file).

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
// CP90 - "P90" SMG. Analogue: mp5.cpp.
// Events: events/p90.sc (fire) / events/p90l.sc (silencer secondary).
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_p90, CP90 );

// Save/restore (#99). Not saved: m_usFire/m_usFireL (event handles, rebuilt by Precache()),
// m_flP90Latch204/208 (only ever 0.0) and m_iP90SilencerLatch (written, never read).
TYPEDESCRIPTION CP90::m_SaveData[] =
{
	DEFINE_FIELD( CP90, m_bP90SilencerOn, FIELD_CHARACTER ),
};

IMPLEMENT_SAVERESTORE( CP90, CBasePlayerWeapon )

void CP90::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_p90.mdl" );
	m_iId = SGTC_WPN_P90;
	m_iDefaultAmmo = 200;
	m_flP90Latch204 = 0.0f;
	m_flP90Latch208 = 0.0f;
	m_bP90SilencerOn = 0;
	m_iP90SilencerLatch = 0;
	FallInit();
}

void CP90::Precache( void )
{
	PRECACHE_MODEL( "models/v_p90.mdl" );
	PRECACHE_MODEL( "models/w_p90.mdl" );
	PRECACHE_MODEL( "models/p_p90.mdl" );
	PRECACHE_MODEL( "models/rshell.mdl" );
	PRECACHE_MODEL( "models/w_9mmARclip.mdl" );

	PRECACHE_SOUND( "items/9mmclip1.wav" );
	PRECACHE_SOUND( "items/clipinsert1.wav" );
	PRECACHE_SOUND( "items/cliprelease1.wav" );
	PRECACHE_SOUND( "weapons/P90-1.wav" );
	PRECACHE_SOUND( "weapons/P90recharge.wav" );
	PRECACHE_SOUND( "weapons/357_cock1.wav" );

	m_usFire = PRECACHE_EVENT( 1, "events/p90.sc" );
	m_usFireL = PRECACHE_EVENT( 1, "events/p90l.sc" );
}

SGTC_GETITEM( CP90, SGTC_AMMO_P90, 250, 50, 2, 0, SGTC_WPN_P90, 0, 15 )
SGTC_ADD_TO_PLAYER( CP90 )

BOOL CP90::Deploy( void )
{
	return SGTC_DEPLOY( 2, "models/v_p90.mdl", "models/p_p90.mdl", SGTC_DRAW, "mp5" );
}

void CP90::Holster( int skiplocal )	// ref @0x129fb6
{
	m_bP90SilencerOn = 0;
	m_iP90SilencerLatch = 0;
	m_pPlayer->pev->fov = 0.0f;
	m_pPlayer->m_iFOV = 0;
	CBasePlayerWeapon::Holster( skiplocal );
}

void CP90::SecondaryAttack( void )	// ref @0x129c3c
{
	if( m_pPlayer->pev->fov == 65.0f )
	{
		SendWeaponAnim( SGTC_DRAW, 1, 0 );
		m_pPlayer->m_iFOV = 0;
		m_pPlayer->pev->fov = 0.0f;
		m_bP90SilencerOn = 0;
		m_iP90SilencerLatch = 0;
	}
	else
	{
		SendWeaponAnim( 6, 1, 0 );
		m_pPlayer->m_iFOV = 0x41;
		m_pPlayer->pev->fov = 65.0f;
		m_bP90SilencerOn = 1;
		m_iP90SilencerLatch = 1;
	}

	m_flP90Latch204 = 0.0f;
	m_flP90Latch208 = 0.0f;
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 1.0f;
	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.07f;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
		UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );
}

void CP90::PrimaryAttack( void )	// ref @0x129530
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
		VECTOR_CONE_6DEGREES, 8192, SGTC_BULLET_P90, 1, 6, m_pPlayer->pev, m_pPlayer->random_seed );

	const unsigned short usEvent = m_bP90SilencerOn ? m_usFireL : m_usFire;
	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), usEvent,
		0.0, (float *)&g_vecZero, (float *)&g_vecZero, vecDir.x, vecDir.y,
		0, 0, 0, 0 );

	if( !m_iClip && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
		m_pPlayer->SetSuitUpdate( "!HEV_AMO0", FALSE, 0 );

	m_flNextPrimaryAttack = GetNextAttackDelay( 0.07f );
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
		UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );
}

void CP90::Reload( void )	// ref @0x129dee
{
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 || m_iClip == 50 )
		return;

	const int iStage = (int)m_flP90Latch204;
	if( iStage > 0 && iStage <= 2 && m_bP90SilencerOn )
		m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.3f;

	m_flP90Latch204 = 0.0f;
	m_flP90Latch208 = 0.0f;

	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.5f;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
		UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );

	if( m_iClip <= 49 )
		EMIT_SOUND_DYN( m_pPlayer->edict(), CHAN_ITEM, "items/clipinsert1.wav",
			1.0f, ATTN_NORM, 0, PITCH_NORM );

	const int iAnim = m_bP90SilencerOn ? 8 : 2;
	DefaultReload( 50, iAnim, 1.5f );
}

SGTC_WEAPON_IDLE( CP90 )
