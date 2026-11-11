// sgtc_shield.cpp — effect_shield / CShield (ref/stc_i386.so @0x162388 Spawn, @0x16221a Think).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "sgtc_shield.h"
#include "sgtc_monsters.h"

// ref rodata @0x1d9de0 / hull constants @0x1d9df4..0x1d9e04 / think @0x1d9dc4..
static const char *const kShieldModel = "models/shield.mdl";

LINK_ENTITY_TO_CLASS( effect_shield, CShield );

void CShield::Precache( void )
{
	PRECACHE_MODEL( kShieldModel );
}

void CShield::Spawn( void )
{
	Precache();

	pev->movetype = MOVETYPE_NONE;	// ref pev+0x108 = 0
	pev->solid = SOLID_NOT;		// ref pev+0x10c = 0

	SET_MODEL( ENT( pev ), kShieldModel );

	UTIL_SetSize( pev, Vector( 0, 0, 0 ), Vector( 0, 0, 0 ) );

	pev->health = 100.0f;	// ref pev+0x160 @0x162452 (not renderamt)
	pev->view_ofs = g_vecZero;	// ref pev+0x174 @0x16247f
	pev->origin = pev->origin - Vector( 0, 0, 40.0f );	// ref fsub 40 @0x1624a7

	pev->renderamt = 130.0f;	// ref pev+0x14c @0x1d9e00
	pev->rendermode = 5;		// ref pev+0x148 @0x1624c4 (engine rendermode 5)

	m_flExpire = gpGlobals->time + 1.0f;	// ref +0x19c @0x1d9e04 (written twice in ref)

	SetThink( &CShield::Think );
	pev->nextthink = 0;	// ref pev+0x138 cleared before inline Think @0x1624f9
	Think();	// ref: immediate virtual call via vslot +0xc4
}

int CShield::Classify( void )
{
	return 0;	// ref @0x162210
}

void CShield::Think( void )
{
	// ref @0x16221a — health-driven renderamt thresholds:
	// pev->health <= 100 -> renderamt 130; pev->health <= 80 -> renderamt 200.
	if( pev->health <= 100.0f )
		pev->renderamt = 130.0f;
	if( pev->health <= 80.0f )
		pev->renderamt = 200.0f;

	if( !pev->owner )
		UTIL_Remove( this );

	if( m_flExpire < gpGlobals->time )
	{
		// ref @0x1622d6: UTIL_FindEntityByClassname( …, "player" ) vs pev->owner.
		BOOL bOwnerAlive = FALSE;
		for( CBaseEntity *pPlayer = NULL; ( pPlayer = UTIL_FindEntityByClassname( pPlayer, "player" ) ) != NULL; )
		{
			if( pPlayer->edict() == pev->owner )
				bOwnerAlive = TRUE;
		}
		if( !bOwnerAlive )
			UTIL_Remove( this );
		m_flExpire = gpGlobals->time + 1.0f;
	}
	pev->nextthink = gpGlobals->time + 0.1f;
}

void SGTC_ShieldEmitArmImpactFlash( CBaseEntity *pShieldEmit )
{
	if( !pShieldEmit || !FClassnameIs( pShieldEmit->pev, "monster_shieldemit" ) )
		return;
	((CSGTCShieldEmit *)pShieldEmit)->m_iSgtcMode = 1;
}
