// sgtc_wpn_psg1.cpp — StargateTC CPsg1 (split from sgtc_weapons.cpp; NEW file).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
#include "sgtc_weapons.h"
#include "sgtc_sniper.h"	// sway / fire-note hooks

// ===========================================================================
// CPsg1 - "PSG1" sniper. Analogue: python.cpp (slow, high-damage hitscan).
// Event: events/psg1.sc.
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_psg1, CPsg1 );

void CPsg1::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_psg1.mdl" );
	m_iId = SGTC_WPN_PSG1;
	m_iDefaultAmmo = 50;
	m_iSgtcZoomStage = 0;
	FallInit();
}

void CPsg1::Precache( void )
{
	PRECACHE_MODEL( "models/v_psg1.mdl" );
	PRECACHE_MODEL( "models/w_psg1.mdl" );
	PRECACHE_MODEL( "models/p_psg1.mdl" );
	PRECACHE_MODEL( "models/shell.mdl" );

	PRECACHE_SOUND( "weapons/psg1_reload.wav" );
	PRECACHE_SOUND( "weapons/psg1_fire.wav" );
	PRECACHE_SOUND( "weapons/psg1_empty.wav" );

	m_usFire = PRECACHE_EVENT( 1, "events/psg1.sc" );
}

SGTC_GETITEM( CPsg1, SGTC_AMMO_PSG1, 50, 10, 2, 4, SGTC_WPN_PSG1, 0, 15 )
SGTC_ADD_TO_PLAYER( CPsg1 )

BOOL CPsg1::Deploy( void )
{
	return SGTC_DEPLOY( 2, "models/v_psg1.mdl", "models/p_psg1.mdl", SGTC_DRAW, "bow" );
}

void CPsg1::Holster( int skiplocal )	// ref @0x14a62c
{
	m_iSgtcZoomStage = 0;
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;
	SendWeaponAnim( SGTC_HOLSTER );
}

void CPsg1::SecondaryAttack( void )	// ref @0x14aa0c
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )
		return;

	if( m_pPlayer->pev->fov == 20.0f )
	{
		m_pPlayer->m_iFOV = 0;
		m_pPlayer->pev->fov = 0.0f;
		m_iSgtcZoomStage = 0;
	}
	else if( m_pPlayer->pev->fov == 40.0f )
	{
		m_pPlayer->m_iFOV = 0x14;
		m_pPlayer->pev->fov = 20.0f;
		m_iSgtcZoomStage = 1;
	}
	else if( m_pPlayer->pev->fov == 0.0f )
	{
		m_pPlayer->m_iFOV = 0x28;
		m_pPlayer->pev->fov = 40.0f;
		m_iSgtcZoomStage = 1;
	}

	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.1f;
	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.2f;
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 1.0f;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
		UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );
}

void CPsg1::PrimaryAttack( void )
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
	// Bots only: drift the server-side aim so bot shots wander like a sniper's.
	SGTC_Sniper_SwayAimVector( m_pPlayer, &vecAiming );
	SGTC_Sniper_NoteFire( m_pPlayer );
	Vector vecDir = m_pPlayer->FireBulletsPlayer( 1, vecSrc, vecAiming,
		VECTOR_CONE_1DEGREES, 8192, SGTC_BULLET_PSG1, 0, 40, m_pPlayer->pev, m_pPlayer->random_seed );
	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFire,
		0.0, (float *)&g_vecZero, (float *)&g_vecZero, vecDir.x, vecDir.y,
		0, 0, 0, 0 );
	// Bolt cycle: a separate cue ~0.45 s after the shot (the client event owns
	// the report/tail/tracer/punch). One lightweight think on the weapon, no
	// extra entity and no new save fields (the pending cue is not persisted).
	SetThink( &CPsg1::SgtcBoltThink );
	pev->nextthink = gpGlobals->time + 0.45f;
	if( !m_iClip && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
		m_pPlayer->SetSuitUpdate( "!HEV_AMO0", FALSE, 0 );
	m_flNextPrimaryAttack = GetNextAttackDelay( 1.0f );
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
		UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );
}

void CPsg1::Reload( void )	// ref @0x14ac06
{
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 || m_iClip == 10 )
		return;
	if( m_flNextPrimaryAttack > UTIL_WeaponTimeBase() )
		return;
	// Ref: drop zoom before DefaultReload when pev->fov != 0.
	if( m_pPlayer->pev->fov != 0.0f )
	{
		m_pPlayer->m_iFOV = 0;
		m_pPlayer->pev->fov = 0.0f;
		m_iSgtcZoomStage = 0;
	}
	DefaultReload( 10, SGTC_RELOAD, 1.5f );
}

// Post-shot bolt-cycle cue, using the mod's own PSG1 mechanism sample. Runs
// once ~0.45 s after PrimaryAttack, then clears the think and nextthink so the
// weapon is idle again (secondary fire and idle do not rely on pev->nextthink).
void CPsg1::SgtcBoltThink( void )
{
	SetThink( NULL );
	pev->nextthink = 0.0f;
	if( m_pPlayer )
		EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_ITEM, "weapons/psg1_reload.wav", 0.8f, ATTN_NORM );
}

SGTC_WEAPON_IDLE( CPsg1 )
