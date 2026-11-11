// sgtc_wpn_medic.cpp — StargateTC CMedicx (ref dump medicx.c @0x10bd00).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
#include "sgtc_weapons.h"

LINK_ENTITY_TO_CLASS( weapon_medic, CMedicx );

void CMedicx::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_medkit.mdl" );	// build-11: normal-size stock medkit pack (w_medic.mdl is a tiny pen)
	m_iId = SGTC_WPN_MEDIC;
	m_iClip = -1;
	m_iDefaultAmmo = 200;	// ref +0x1f8
	FallInit();
}

void CMedicx::Precache( void )
{
	PRECACHE_MODEL( "models/v_medic.mdl" );
	PRECACHE_MODEL( "models/w_medkit.mdl" );
	PRECACHE_MODEL( "models/p_medic.mdl" );
	PRECACHE_SOUND( "weapons/medic.wav" );
}

SGTC_GETITEM( CMedicx, SGTC_AMMO_MEDGOO, 200, WEAPON_NOCLIP, 6, 0, SGTC_WPN_MEDIC, 0, 1 )
SGTC_ADD_TO_PLAYER( CMedicx )

BOOL CMedicx::IsUseable( void )	// ref @0x10bad4 — always 1
{
	return TRUE;
}

BOOL CMedicx::Deploy( void )	// ref @0x10bca6
{
	return SGTC_DEPLOY( 2, "models/v_medic.mdl", "models/p_medic.mdl", 1, "onehanded" );
}

// ref float @0x1d3920 (dump label DAT_001e3920; Ghidra data addrs run +0x10000) — verified 0.0 via objdump
static const float kMedicHealLatch3fc = 0.0f;

void CMedicx::PrimaryAttack( void )	// ref @0x10bd00
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

	UTIL_MakeVectors( m_pPlayer->pev->angles );	// ref pev+0x50 — angles, not v_angle
	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecEnd = vecSrc + gpGlobals->v_forward * 8192.0f;

	TraceResult tr;
	UTIL_TraceLine( vecSrc, vecEnd, dont_ignore_monsters, m_pPlayer->edict(), &tr );

	CBasePlayer *pTarget = (CBasePlayer *)CBaseEntity::Instance( tr.pHit );
	if( pTarget && pTarget->IsPlayer() && pTarget->IsAlive() )
	{
		BOOL bApplied = FALSE;
		if( pTarget->pev->team == m_pPlayer->pev->team )
		{
			if( pTarget->pev->health < 100.0f )	// ref DAT @0x1d391c
			{
				pTarget->m_iStamina = 100;
				pTarget->SGTC_UpdateStamina();
				pTarget->TakeHealth( (float)iHeal, DMG_GENERIC );	// ref 0x80000
				pTarget->m_flSgtcLatch3fc = kMedicHealLatch3fc;	// ref +0x3fc = DAT_001e3920
				bApplied = TRUE;
			}
		}
		else if( kMedicHealLatch3fc < pTarget->pev->health )	// ref DAT_001e3920 @0x1e3920
		{
			pTarget->m_iStamina = 0;
			pTarget->SGTC_UpdateStamina();
			pTarget->TakeDamage( pTarget->pev, m_pPlayer->pev, (float)iHeal, DMG_POISON );	// ref 0x20000
			bApplied = TRUE;
		}

		if( bApplied )
		{
			EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "weapons/medic.wav", 1.0f, 0.8f );
			m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] -= iHeal;
			if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] < 0 )
				m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = 0;
			m_pPlayer->m_iStamina -= iHeal / 2;
			if( m_pPlayer->m_iStamina < 1 )
				m_pPlayer->m_iStamina = 0;
			m_pPlayer->SGTC_UpdateStamina();
		}
	}

	m_pPlayer->SnagInvis();
	SendWeaponAnim( 2, 1, 0 );	// ref vcall+0x160 anim 2 only @0x10bd00
	pev->nextthink = gpGlobals->time + 0.7f;	// ref weapon pev+0x104 @0x10bd00
	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.7f;	// ref +0x1d4, DAT 0.7
}

void CMedicx::SecondaryAttack( void )	// ref @0x10c17a
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )	// ref +0x408 early-out inverted
		return;

	if( m_pPlayer->pev->health >= 100.0f )	// ref gate @0x1d3938
		return;

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] < 1 )
		return;

	if( m_flNextPrimaryAttack > UTIL_WeaponTimeBase() )
		return;

	int iHeal = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
	if( iHeal > 30 )
		iHeal = 30;

	EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "weapons/medic.wav", 1.0f, 0.8f );
	m_pPlayer->TakeHealth( (float)iHeal, DMG_GENERIC );
	m_pPlayer->m_iStamina = 100;
	m_pPlayer->SGTC_UpdateStamina();
	m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] -= iHeal;
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] < 0 )
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = 0;
	m_pPlayer->m_bSgtcPadA3D = 1;	// ref player +0xa3d
	SendWeaponAnim( 3, 1, 0 );
	pev->nextthink = gpGlobals->time + 1.2f;	// ref weapon pev+0x104 @0x10c17a
	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 1.2f;	// ref DAT 1.2
}

void CMedicx::WeaponIdle( void )	// ref @0x10c3b4
{
	if( m_flNextPrimaryAttack > UTIL_WeaponTimeBase() )
		return;

	SendWeaponAnim( 0, 1, 0 );
	pev->nextthink = gpGlobals->time + 0.1f;	// ref weapon pev+0x104 @0x10c3b4
}

void CMedicx::Reload( void )	// ref: no-op body
{
}
