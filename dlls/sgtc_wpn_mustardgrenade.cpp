// sgtc_wpn_mustardgrenade.cpp — StargateTC CMustardGrenade (ref cook-and-throw + MustardThink cloud).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
#include "sgtc_weapons.h"
#include "sgtc_gas_grenade.h"

LINK_ENTITY_TO_CLASS( weapon_mustardgrenade, CMustardGrenade );

void CMustardGrenade::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_mustardgrenade.mdl" );
	m_iId = SGTC_WPN_MUSTARDGRENADE;
	m_iClip = -1;
	m_iDefaultAmmo = 1;
	pev->dmg = gSkillData.plrDmgHandGrenade;	// ref Spawn: gSkillData+0xfc (MP 120, not hardcoded 50)
	m_flGasStartThrow = 0.0f;
	m_flGasReleaseThrow = 0.0f;
	m_flGasDeployIdle = 0.0f;
	FallInit();
}

void CMustardGrenade::Precache( void )
{
	PRECACHE_MODEL( "models/w_mustardgrenade.mdl" );
	PRECACHE_MODEL( "models/v_mustardgrenade.mdl" );
	PRECACHE_MODEL( "models/p_mustardgrenade.mdl" );
	PRECACHE_SOUND( "weapons/gasflow.wav" );
	PRECACHE_SOUND( "player/cough1.wav" );
	PRECACHE_SOUND( "player/cough2.wav" );
	PRECACHE_SOUND( "player/cough3.wav" );
	PRECACHE_SOUND( "common/null.wav" );
	PRECACHE_SOUND( "debris/bustflesh1.wav" );
	PRECACHE_SOUND( "debris/bustflesh2.wav" );
	SGTC_PrecacheGasSprites();
}

SGTC_GETITEM( CMustardGrenade, SGTC_AMMO_MUSTARD, 1, WEAPON_NOCLIP, 6, 5, SGTC_WPN_MUSTARDGRENADE, 5, 5 )
SGTC_ADD_TO_PLAYER( CMustardGrenade )

BOOL CMustardGrenade::Deploy( void )
{
	return SGTC_GasDeploy( 2, "models/v_mustardgrenade.mdl", "models/p_mustardgrenade.mdl" );
}

void CMustardGrenade::PrimaryAttack( void )
{
	SGTC_GasPinPull();
}

void CMustardGrenade::WeaponIdle( void )
{
	SGTC_GasWeaponIdle( SGTC_WPN_MUSTARDGRENADE, SGTC_GAS_MUSTARD,
		"models/w_mustardgrenade.mdl", pev->dmg );
}

SGTC_NO_SECONDARY( CMustardGrenade )
SGTC_NO_RELOAD( CMustardGrenade )
