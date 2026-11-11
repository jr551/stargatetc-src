// sgtc_wpn_goaregen.cpp — StargateTC CMedicxgoa (split from sgtc_weapons.cpp; NEW file).

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
// CMedicxgoa - Goa'uld regeneration device. Analogue: healthkit.cpp.
// No client event.
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_goaregen, CMedicxgoa );

void CMedicxgoa::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_goaregen.mdl" );
	m_iId = SGTC_WPN_GOAREGEN;
	m_iClip = -1;
	m_iDefaultAmmo = 150;
	FallInit();
}

void CMedicxgoa::Precache( void )
{
	PRECACHE_MODEL( "models/v_goaregen.mdl" );
	PRECACHE_MODEL( "models/w_goaregen.mdl" );
	PRECACHE_MODEL( "models/p_goaregen.mdl" );
	PRECACHE_SOUND( "weapons/goaregen.wav" );
}

SGTC_GETITEM( CMedicxgoa, SGTC_AMMO_GOAREGEN, 150, WEAPON_NOCLIP, 3, 4, SGTC_WPN_GOAREGEN, 0, 0 )
SGTC_ADD_TO_PLAYER( CMedicxgoa )

BOOL CMedicxgoa::IsUseable( void )
{
	return TRUE;	// ref @0x1067c8
}

BOOL CMedicxgoa::Deploy( void )
{
	return SGTC_DEPLOY( 1, "models/v_goaregen.mdl", "models/p_goaregen.mdl", 2, "hive" );	// ref @0x10699a
}

void CMedicxgoa::PrimaryAttack( void )	// ref @0x1069f4
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )	// ref player +0x408
		return;

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] < 1 )
		return;

	if( m_flNextPrimaryAttack > UTIL_WeaponTimeBase() )
		return;

	int iHeal = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
	if( iHeal > 30 )	// ref cap 0x1e
		iHeal = 30;

	SendWeaponAnim( 1 );
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
	m_pPlayer->SnagInvis();

	UTIL_MakeVectors( m_pPlayer->pev->angles );	// ref pev+0x50 (angles)
	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecEnd = vecSrc + gpGlobals->v_forward * 8192.0f;

	TraceResult tr;
	UTIL_TraceLine( vecSrc, vecEnd, dont_ignore_monsters, m_pPlayer->edict(), &tr );	// ref flag 0

	MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, vecSrc );
		WRITE_BYTE( TE_DLIGHT );
		WRITE_COORD( vecSrc.x );
		WRITE_COORD( vecSrc.y );
		WRITE_COORD( vecSrc.z );
		WRITE_BYTE( 8 );
		WRITE_BYTE( 255 );
		WRITE_BYTE( 50 );
		WRITE_BYTE( 50 );
		WRITE_BYTE( 7 );
		WRITE_BYTE( 0 );
	MESSAGE_END();

	EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "weapons/goaregen.wav", 1.0, ATTN_NORM );

	CBaseEntity *pHit = CBaseEntity::Instance( tr.pHit );
	if( pHit && pHit->IsPlayer() )
	{
		CBasePlayer *pTarget = (CBasePlayer *)pHit;
		if( pTarget->IsAlive() )
	{
		BOOL bApplied = FALSE;
		if( pTarget->pev->team == m_pPlayer->pev->team )
		{
			bApplied = ( pTarget->pev->health < 100.0f );	// ref local_10 gate
			pTarget->m_iStamina = 100;		// ref +0x43c = 100 (unconditional)
			pTarget->SGTC_UpdateStamina();
			pTarget->TakeHealth( (float)iHeal, DMG_DROWNRECOVER );	// ref vfunc +0x3c, bits 0x80000
			pTarget->m_flSgtcLatch3fc = 0;	// ref +0x3fc = 0
		}
		else
		{
			bApplied = ( pTarget->pev->health > 0.0f );	// ref local_10 gate
			pTarget->m_iStamina = 0;			// ref +0x43c = 0
			pTarget->SGTC_UpdateStamina();
			pTarget->TakeDamage( pTarget->pev, m_pPlayer->pev, (float)iHeal, DMG_POISON );	// ref bits 0x20000
		}

		if( bApplied )	// ref local_10 != 0 → spend ammo + self stamina
		{
			m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] -= iHeal;
			m_pPlayer->m_iStamina -= iHeal / 2;	// ref +0x43c -= iHeal/2
			if( m_pPlayer->m_iStamina < 1 )
				m_pPlayer->m_iStamina = 0;
			m_pPlayer->SGTC_UpdateStamina();
		}
		}
	}

	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.7f;	// ref pev+0x104
	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.7f;
}

void CMedicxgoa::WeaponIdle( void )	// ref @0x106fcc
{
	if( m_flNextPrimaryAttack > UTIL_WeaponTimeBase() )
		return;

	SendWeaponAnim( 0 );
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.1f;
}

SGTC_NO_SECONDARY( CMedicxgoa )
SGTC_NO_RELOAD( CMedicxgoa )
