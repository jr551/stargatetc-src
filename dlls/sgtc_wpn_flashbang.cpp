// sgtc_wpn_flashbang.cpp — StargateTC CFlashBang (split from sgtc_weapons.cpp; NEW file).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
#include "sgtc_weapons.h"

enum flashbang_e
{
	FLASHBANG_IDLE = 0,
	FLASHBANG_FIDGET,
	FLASHBANG_PINPULL,
	FLASHBANG_THROW1,
	FLASHBANG_THROW2,
	FLASHBANG_THROW3,
	FLASHBANG_HOLSTER,
	FLASHBANG_DRAW = 7,
};

// ===========================================================================
// CFlashBang — ref @0xe6078..0xe64d8; cook/throw via CGrenade::ShootFlash @0xf5d42.
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_flashbang, CFlashBang );

void CFlashBang::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_grenade.mdl" );
	m_iId = SGTC_WPN_FLASHBANG;
#if !CLIENT_DLL
	pev->dmg = 60.0f;	// ref Spawn @0xe60e4: gSkillData+0x110 (MP hardcode 60, not plrDmgHandGrenade)
#endif
	m_iDefaultAmmo = 4;
	m_iClip = 0;	// ref +0x1fc slide-flash path flag (0 = normal throw)
	FallInit();
}

void CFlashBang::Precache( void )
{
	PRECACHE_MODEL( "models/w_grenade.mdl" );
	PRECACHE_MODEL( "models/v_grenade.mdl" );
	PRECACHE_MODEL( "models/p_grenade.mdl" );
	PRECACHE_SOUND( "common/null.wav" );
	SGTC_PrecacheSound( "weapons/flashbang.wav" );
}

SGTC_GETITEM( CFlashBang, SGTC_AMMO_FLASH, 10, WEAPON_NOCLIP, 5, 5, SGTC_WPN_FLASHBANG, 24, 10 )
SGTC_ADD_TO_PLAYER( CFlashBang )

BOOL CFlashBang::Deploy( void )
{
	m_flReleaseThrow = -1.0f;
	m_flStartThrow = 0.0f;
	return SGTC_DEPLOY( 2, "models/v_grenade.mdl", "models/p_grenade.mdl", FLASHBANG_DRAW, "crowbar" );
}

BOOL CFlashBang::CanHolster( void )
{
	return ( m_flStartThrow == 0.0f );
}

void CFlashBang::Holster( int skiplocal )
{
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] )
		SendWeaponAnim( FLASHBANG_HOLSTER );
	else
	{
		m_pPlayer->pev->weapons &= ~( 1 << SGTC_WPN_FLASHBANG );
		DestroyItem();
	}

	if( m_flStartThrow )
	{
		m_flStartThrow = 0.0f;
		m_flReleaseThrow = 0.0f;
	}

	EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "common/null.wav", 1.0f, ATTN_NORM );
}

void CFlashBang::PrimaryAttack( void )
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )
		return;

	if( !m_flStartThrow && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0 )
	{
		m_flStartThrow = gpGlobals->time;
		m_flReleaseThrow = 0.0f;
		SendWeaponAnim( FLASHBANG_PINPULL );
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.5f;
	}
}

void CFlashBang::WeaponIdle( void )
{
	if( m_flReleaseThrow == 0.0f && m_flStartThrow )
		m_flReleaseThrow = gpGlobals->time;

	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;

	if( m_flStartThrow )
	{
		Vector angThrow = m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle;
		if( angThrow.x < 0.0f )
			angThrow.x = -10.0f + angThrow.x * ( ( 90.0f - 10.0f ) / 90.0f );
		else
			angThrow.x = -10.0f + angThrow.x * ( ( 90.0f + 10.0f ) / 90.0f );

		float flVel = ( 90.0f - angThrow.x ) * 4.0f;	// ref (90-pitch)*4 cap 500
		if( flVel > 500.0f )
			flVel = 500.0f;

		UTIL_MakeVectors( angThrow );
		Vector vecSrc = m_pPlayer->GetGunPosition() + gpGlobals->v_forward * 40.0f;
		Vector vecThrow = gpGlobals->v_forward * flVel + m_pPlayer->pev->velocity;

		float flFuse = m_flStartThrow - gpGlobals->time + 3.0f;
		if( flFuse < 0.0f )
			flFuse = 0.0f;

		m_pPlayer->SnagInvis();

#ifndef CLIENT_DLL
		CGrenade *pNade = NULL;
		if( m_iClip )
			pNade = CGrenade::LaunchSlideFlash( m_pPlayer->pev, vecSrc, vecThrow, flFuse, 0 );
		else
			pNade = CGrenade::ShootFlash( m_pPlayer->pev, vecSrc, vecThrow, flFuse, 0 );
		if( pNade )
		{
			pNade->pev->owner = m_pPlayer->edict();
			pNade->pev->team = m_pPlayer->pev->team;
		}
#endif

		if( flVel < 500.0f )
			SendWeaponAnim( FLASHBANG_THROW1 );
		else if( flVel < 1000.0f )
			SendWeaponAnim( FLASHBANG_THROW2 );
		else
			SendWeaponAnim( FLASHBANG_THROW3 );

		m_pPlayer->SetAnimation( PLAYER_ATTACK1 );

		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]--;
		m_flStartThrow = 0.0f;
		m_flReleaseThrow = 0.0f;

		m_flNextPrimaryAttack = GetNextAttackDelay( 0.5f );
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.5f;

		if( !m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] )
			m_flTimeWeaponIdle = m_flNextSecondaryAttack = m_flNextPrimaryAttack = GetNextAttackDelay( 0.5f );
		return;
	}
	else if( m_flReleaseThrow > 0.0f )
	{
		m_flStartThrow = 0.0f;
		if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] )
			SendWeaponAnim( FLASHBANG_DRAW );
		else
		{
			RetireWeapon();
			return;
		}
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
		m_flReleaseThrow = -1.0f;
		return;
	}

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] )
	{
		SendWeaponAnim( FLASHBANG_IDLE );
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
	}
}

SGTC_NO_SECONDARY( CFlashBang )
SGTC_NO_RELOAD( CFlashBang )
