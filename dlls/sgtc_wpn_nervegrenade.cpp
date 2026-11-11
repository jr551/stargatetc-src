// sgtc_wpn_nervegrenade.cpp — CNerveGrenade (ref cook-and-throw + NerveThink cloud).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "sgtc_weapons.h"
#include "sgtc_gas_grenade.h"

LINK_ENTITY_TO_CLASS( weapon_nervegrenade, CNerveGrenade );

void CNerveGrenade::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_nervegrenade.mdl" );
	m_iId = SGTC_WPN_NERVEGRENADE;
	m_iClip = -1;
	m_iDefaultAmmo = 1;
	pev->dmg = gSkillData.plrDmgHandGrenade;	// ref Spawn: gSkillData+0xfc (MP 120, not hardcoded 50)
	m_flGasStartThrow = 0.0f;
	m_flGasReleaseThrow = 0.0f;
	m_flGasDeployIdle = 0.0f;
	FallInit();
}

void CNerveGrenade::Precache( void )
{
	PRECACHE_MODEL( "models/w_nervegrenade.mdl" );
	PRECACHE_MODEL( "models/v_nervegrenade.mdl" );
	PRECACHE_MODEL( "models/p_nervegrenade.mdl" );
	PRECACHE_SOUND( "weapons/gasflow.wav" );
	PRECACHE_SOUND( "player/cough1.wav" );
	PRECACHE_SOUND( "player/cough2.wav" );
	PRECACHE_SOUND( "player/cough3.wav" );
	PRECACHE_SOUND( "common/null.wav" );
	PRECACHE_SOUND( "debris/bustflesh1.wav" );
	PRECACHE_SOUND( "debris/bustflesh2.wav" );
	SGTC_PrecacheGasSprites();
}

SGTC_GETITEM( CNerveGrenade, SGTC_AMMO_NERVE, 1, WEAPON_NOCLIP, 6, 3, SGTC_WPN_NERVEGRENADE, 5, 5 )
SGTC_ADD_TO_PLAYER( CNerveGrenade )

BOOL CNerveGrenade::Deploy( void )
{
	return SGTC_GasDeploy( 2, "models/v_nervegrenade.mdl", "models/p_nervegrenade.mdl" );
}

void CNerveGrenade::PrimaryAttack( void ) { SGTC_GasPinPull(); }

void CNerveGrenade::WeaponIdle( void )
{
	SGTC_GasWeaponIdle( SGTC_WPN_NERVEGRENADE, SGTC_GAS_NERVE,
		"models/w_nervegrenade.mdl", pev->dmg );
}

SGTC_NO_SECONDARY( CNerveGrenade )
SGTC_NO_RELOAD( CNerveGrenade )
