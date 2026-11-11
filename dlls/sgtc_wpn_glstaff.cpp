// sgtc_wpn_glstaff.cpp — StargateTC CBigLanceJaffa (split from sgtc_weapons.cpp; NEW file).

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
// CBigLanceJaffa - "glstaff" / deathglider cannon. Analogue: rpg.cpp.
// Event: events/biglance.sc.
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_glstaff, CBigLanceJaffa );

// Save/restore (#99). Not saved: m_usFire and m_iLaserBeam (precache handles, rebuilt by
// Precache()).
TYPEDESCRIPTION CBigLanceJaffa::m_SaveData[] =
{
	DEFINE_FIELD( CBigLanceJaffa, m_flSgtcRegenNext, FIELD_TIME ),
};

IMPLEMENT_SAVERESTORE( CBigLanceJaffa, CBasePlayerWeapon )

void CBigLanceJaffa::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_deathglider.mdl" );
	m_iId = SGTC_WPN_BIGLANCE;
	m_iDefaultAmmo = 22;
	m_flSgtcRegenNext = 0.0f;
	FallInit();
}

void CBigLanceJaffa::Precache( void )
{
	PRECACHE_MODEL( "models/v_deathglider.mdl" );
	PRECACHE_MODEL( "models/w_deathglider.mdl" );
	PRECACHE_MODEL( "models/p_deathglider.mdl" );

	PRECACHE_SOUND( "weapons/tirlancegoa.wav" );
	PRECACHE_SOUND( "weapons/lancegoavide.wav" );

	m_usFire = PRECACHE_EVENT( 1, "events/biglance.sc" );
	m_iLaserBeam = PRECACHE_MODEL( "sprites/laserbeam.spr" );	// ref +0x204
	PRECACHE_MODEL( "models/proj.mdl" );
	UTIL_PrecacheOther( "big_lance_proj" );	// build-21 audit: ref precaches the projectile here
	PRECACHE_SOUND( "weapons/rocket1.wav" );
}

SGTC_GETITEM( CBigLanceJaffa, SGTC_AMMO_ENERGIEGL, 22, WEAPON_NOCLIP, 3, 5, SGTC_WPN_BIGLANCE, 5, 20 )
SGTC_ADD_TO_PLAYER( CBigLanceJaffa )

BOOL CBigLanceJaffa::Deploy( void )
{
	// ref @0xc9a4e: DefaultDeploy( v_, p_, anim=2, "rpg" ) — anim index 2,
	// not SGTC_DRAW; enum collides with SGTC_FIRE1.
	return SGTC_DEPLOY( 1, "models/v_deathglider.mdl", "models/p_deathglider.mdl", SGTC_FIRE1, "rpg" );
}

// ref @0xc9aa8 — unconditional useable (needs virtual decl in sgtc_weapons.h).
BOOL CBigLanceJaffa::IsUseable( void )
{
	return TRUE;
}

void CBigLanceJaffa::Holster( int skiplocal )	// ref @0xc9c90
{
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] == 0 )
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]++;
	SendWeaponAnim( 4, 1, 0 );	// ref anim 4
}

void CBigLanceJaffa::Reload( void )	// ref @0xc9ab8
{
	SGTC_RegenAmmo( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType], m_flSgtcRegenNext,
		22, 1, 2.0f );
}

void CBigLanceJaffa::WeaponIdle( void )	// ref @0xc9b60
{
	ResetEmptySound();
	Reload();
	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;

	if( UTIL_SharedRandomFloat( m_pPlayer->random_seed, 0.0f, 1.0f ) <= 0.75f )
	{
		SendWeaponAnim( SGTC_IDLE, 1, 0 );
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
			UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
	}
	else
	{
		SendWeaponAnim( 3, 1, 0 );	// ref anim 3
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.5f;
	}
}

void CBigLanceJaffa::PrimaryAttack( void )	// ref @0xc9fea
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )	// ref +0x408
		return;

	if( m_pPlayer->m_bShieldLarge )
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = 1;	// ref +0x460 cheat ammo latch

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
		return;	// ref: silent dead-fire — no PlayEmptySound

	// A normal shot costs 5 cells (see below); refuse it at 1-4 so the count
	// cannot go negative. The cheat latch above (m_bShieldLarge) fires free.
	if( m_pPlayer->m_bShieldLarge != 1 && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] < 5 )
		return;

	UTIL_MakeVectors( m_pPlayer->pev->v_angle );	// ref +0x74 — no punchangle
	Vector vecDir = gpGlobals->v_forward;

	// ref: PLAYBACK_EVENT_FULL( 1, edict, m_usFire, 0, &g_vecZero, &g_vecZero, 0..0 )
	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFire,
		0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0.0f, 0.0f,
		0, 0, 0, 0 );

	m_pPlayer->SnagInvis();
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );

#ifndef CLIENT_DLL
	Vector vecSrc = m_pPlayer->GetGunPosition();
	TraceResult tr;
	UTIL_TraceLine( vecSrc, vecSrc + vecDir * 8192.0f, dont_ignore_monsters,
		m_pPlayer->edict(), &tr );

	CBigLanceProj *pBolt = CBigLanceProj::CreateBigLanceProj(
		vecSrc + vecDir * 16.0f, m_pPlayer->pev->velocity, tr.vecEndPos,
		m_pPlayer, this );

	if( pBolt )
	{
		// ref: if player+0xa2c (ally-shield stamp, m_flSgtcShieldTime) is
		// live, arm bolt+0xd5 (m_fAttendExplosion) so it still explodes.
		if( gpGlobals->time < m_pPlayer->m_flSgtcShieldTime )
			pBolt->m_fAttendExplosion = TRUE;
		pBolt->pev->v_angle = m_pPlayer->pev->v_angle;	// ref copies pev+0x74

		for( int i = 1; i < 3; i++ )
			SGTC_LaserBeamEnts( m_pPlayer, pBolt, m_iLaserBeam, 6 );
	}
#endif

	if( m_pPlayer->m_bShieldLarge != 1 )
	{
		if( !( m_pPlayer->pev->flags & FL_DUCKING ) )
		{
			const int iDrain = RANDOM_LONG( 0, 70 ) + 30;	// ref 0x46 + 0x1e
			m_pPlayer->m_iStamina -= iDrain;
		}
		else
		{
			const int iDrain = RANDOM_LONG( 0, 30 ) + 10;	// ref 0x1e + 10
			m_pPlayer->m_iStamina -= iDrain;
		}
		if( m_pPlayer->m_iStamina < 0 )
		{
			const float flDmg = (float)( m_pPlayer->m_iStamina * -2 );
			m_pPlayer->TakeDamage( m_pPlayer->pev, m_pPlayer->pev, flDmg, DMG_CLUB );
		}
		m_pPlayer->SGTC_UpdateStamina();
	}

	if( m_pPlayer->m_bShieldLarge == 1 )
	{
		m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.3f;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
			UTIL_SharedRandomFloat( m_pPlayer->random_seed, 7.0f, 10.0f );
		m_flSgtcRegenNext = gpGlobals->time + 0.3f;
	}
	else
	{
		m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 1.8f;	// ref @0xc9fea
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
			UTIL_SharedRandomFloat( m_pPlayer->random_seed, 7.0f, 10.0f );
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] -= 5;	// ref −5 cells
		m_flSgtcRegenNext = gpGlobals->time + 3.5f;
	}

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
		m_pPlayer->SetSuitUpdate( "!HEV_AMO0", FALSE, 0 );
}

SGTC_NO_SECONDARY( CBigLanceJaffa )
