// sgtc_wpn_narcogrenade.cpp — CNarcoGrenade (ref cook-and-throw + NarcoThink cloud).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "sgtc_weapons.h"
#include "sgtc_gas_grenade.h"

LINK_ENTITY_TO_CLASS( weapon_narcogrenade, CNarcoGrenade );

void CNarcoGrenade::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_narcogrenade.mdl" );
	m_iId = SGTC_WPN_NARCOGRENADE;
	m_iClip = -1;
	m_iDefaultAmmo = 1;
	pev->dmg = gSkillData.plrDmgHandGrenade;	// ref Spawn: gSkillData+0xfc (MP 120, not hardcoded 50)
	m_flGasStartThrow = 0.0f;
	m_flGasReleaseThrow = 0.0f;
	m_flGasDeployIdle = 0.0f;
	FallInit();
}

void CNarcoGrenade::Precache( void )
{
	PRECACHE_MODEL( "models/w_narcogrenade.mdl" );
	PRECACHE_MODEL( "models/v_narcogrenade.mdl" );
	PRECACHE_MODEL( "models/p_narcogrenade.mdl" );
	PRECACHE_SOUND( "weapons/gasflow.wav" );
	PRECACHE_SOUND( "player/cough1.wav" );
	PRECACHE_SOUND( "player/cough2.wav" );
	PRECACHE_SOUND( "player/cough3.wav" );
	PRECACHE_SOUND( "common/null.wav" );
	SGTC_PrecacheGasSprites();
}

SGTC_GETITEM( CNarcoGrenade, SGTC_AMMO_NARCO, 1, WEAPON_NOCLIP, 6, 4, SGTC_WPN_NARCOGRENADE, 5, 5 )
SGTC_ADD_TO_PLAYER( CNarcoGrenade )

BOOL CNarcoGrenade::Deploy( void )
{
	return SGTC_GasDeploy( 2, "models/v_narcogrenade.mdl", "models/p_narcogrenade.mdl" );
}

void CNarcoGrenade::PrimaryAttack( void ) { SGTC_GasPinPull(); }

void CNarcoGrenade::WeaponIdle( void )
{
	SGTC_GasWeaponIdle( SGTC_WPN_NARCOGRENADE, SGTC_GAS_NARCO,
		"models/w_narcogrenade.mdl", pev->dmg );
}

SGTC_NO_SECONDARY( CNarcoGrenade )
SGTC_NO_RELOAD( CNarcoGrenade )
