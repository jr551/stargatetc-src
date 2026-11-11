// sgtc_wpn_shield.cpp — StargateTC CShieldGun (split from sgtc_weapons.cpp; NEW file).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
#include "sgtc_weapons.h"

// File-local event handle (the class declares no m_usFire member).
static unsigned short s_usShieldFire;

// ===========================================================================
// CShieldGun - "weapon_shield" personal shield wall. Analogue: gauss.cpp
// (charge device). Spawns the entities peer's effect_shield via Create().
// Event: events/shield.sc.
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_shield, CShieldGun );

void CShieldGun::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_shield.mdl" );
	m_iId = SGTC_WPN_SHIELD;
	m_iClip = -1;
	FallInit();
}

void CShieldGun::Precache( void )
{
	PRECACHE_MODEL( "models/shield.mdl" );
	PRECACHE_MODEL( "models/w_shield.mdl" );
	PRECACHE_MODEL( "models/v_shield.mdl" );
	PRECACHE_MODEL( "models/p_shield.mdl" );

	s_usShieldFire = PRECACHE_EVENT( 1, "events/shield.sc" );
}

int CShieldGun::GetItemInfo( ItemInfo *p )
{
	p->pszName = STRING( pev->classname );
	p->pszAmmo1 = NULL;
	p->iMaxAmmo1 = -1;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = WEAPON_NOCLIP;
	p->iSlot = 4;
	p->iPosition = 0;
	p->iFlags = 0;
	p->iId = m_iId = SGTC_WPN_SHIELD;
	p->iWeight = 0;
	return 1;
}

SGTC_ADD_TO_PLAYER( CShieldGun )

BOOL CShieldGun::Deploy( void )
{
	return SGTC_DEPLOY( 1, "models/v_shield.mdl", "models/p_shield.mdl", SGTC_DRAW, "gauss" );
}

void CShieldGun::PrimaryAttack( void )
{
	UTIL_MakeVectors( m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle );

	Vector vecSrc = m_pPlayer->pev->origin + m_pPlayer->pev->view_ofs;

#ifndef CLIENT_DLL
	m_pPlayer->m_bShieldWall = 1;
#endif

	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), s_usShieldFire,
		0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0, 0, 0, 0, 0, 0 );

	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );

	m_flNextPrimaryAttack = GetNextAttackDelay( 2.0 );
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.0;
}

SGTC_NO_SECONDARY( CShieldGun )
SGTC_NO_RELOAD( CShieldGun )
SGTC_WEAPON_IDLE( CShieldGun )
