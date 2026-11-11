// sgtc_wpn_smokegrenade.cpp — CSmokeGrenade (ref cook-and-throw + SmokeThink cloud).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "sgtc_weapons.h"
#include "sgtc_gas_grenade.h"

LINK_ENTITY_TO_CLASS( weapon_smokegrenade, CSmokeGrenade );

void CSmokeGrenade::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_smokegrenade.mdl" );
	m_iId = SGTC_WPN_SMOKEGRENADE;
	m_iClip = -1;
	m_iDefaultAmmo = 1;
	pev->dmg = gSkillData.plrDmgHandGrenade;	// ref Spawn: gSkillData+0xfc (MP 120, not hardcoded 50)
	m_flGasStartThrow = 0.0f;
	m_flGasReleaseThrow = 0.0f;
	m_flGasDeployIdle = 0.0f;
	FallInit();
}

void CSmokeGrenade::Precache( void )
{
	PRECACHE_MODEL( "models/w_smokegrenade.mdl" );
	PRECACHE_MODEL( "models/v_smokegrenade.mdl" );
	PRECACHE_MODEL( "models/p_smokegrenade.mdl" );
	PRECACHE_SOUND( "weapons/gasflow.wav" );
	PRECACHE_SOUND( "player/cough1.wav" );
	PRECACHE_SOUND( "player/cough2.wav" );
	PRECACHE_SOUND( "player/cough3.wav" );
	PRECACHE_SOUND( "empty.wav" );	// ref CSmokeGrenade::Precache @0x174184
	SGTC_PrecacheGasSprites();
}

SGTC_GETITEM( CSmokeGrenade, SGTC_AMMO_SMOKE, 1, WEAPON_NOCLIP, 6, 2, SGTC_WPN_SMOKEGRENADE, 5, 5 )
SGTC_ADD_TO_PLAYER( CSmokeGrenade )

BOOL CSmokeGrenade::Deploy( void )
{
	return SGTC_GasDeploy( 2, "models/v_smokegrenade.mdl", "models/p_smokegrenade.mdl" );
}

void CSmokeGrenade::PrimaryAttack( void ) { SGTC_GasPinPull(); }

void CSmokeGrenade::WeaponIdle( void )
{
	SGTC_GasWeaponIdle( SGTC_WPN_SMOKEGRENADE, SGTC_GAS_SMOKE,
		"models/w_smokegrenade.mdl", pev->dmg );
}

SGTC_NO_SECONDARY( CSmokeGrenade )
SGTC_NO_RELOAD( CSmokeGrenade )
