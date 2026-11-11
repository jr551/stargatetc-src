// sgtc_bots_rules.cpp — build-13 bot rules API.  Provisional implementation;
// values marked TODO are wired to Victor's ref extraction in the next patch.
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "sgtc_weapons.h"
#include "sgtc_bots_rules.h"
#include "sgtc_mech_state.h"

static int s_bSgtcWeaponRulesReady = 0;

// Weapon ids that are melee/crush shield breakers (ref DMG_CLUB damage bits).
static int SGTC_Bots_WeaponIsMelee( int iWeaponId )
{
	return ( iWeaponId == SGTC_WPN_COUTEAU || iWeaponId == SGTC_WPN_COUJAF ||
		iWeaponId == SGTC_WPN_BATSUP || iWeaponId == SGTC_WPN_MAIN );
}

void SGTC_Bots_WeaponRulesInit( void )
{
	s_bSgtcWeaponRulesReady = 1;
}

int SGTC_Bots_WeaponBypassesShield( int iWeaponId )
{
	return SGTC_Bots_WeaponIsMelee( iWeaponId ) ? 1 : 0;
}

sgtc_vis_t SGTC_Bots_TargetVisible( const CBasePlayer *observer, const CBasePlayer *target )
{
	if( !observer || !target || !target->pev )
		return SGTC_VIS_UNKNOWN;
	if( target->pev->health <= 0.0f )
		return SGTC_VIS_VISIBLE;
	if( target->m_bShieldSprint )	// cloaked (invis weapon / sprint shield)
	{
		if( !SGTC_Bots_ObserverHasTER( observer ) )
			return SGTC_VIS_INVISIBLE;
	}
	return SGTC_VIS_VISIBLE;
}

float SGTC_Bots_TargetRevealAge( const CBasePlayer *target )
{
	if( !target || target->m_flSgtcInvisReveal <= 0.0f )
		return -1.0f;
	return gpGlobals->time - target->m_flSgtcInvisReveal;
}

int SGTC_Bots_ObserverHasTER( const CBasePlayer *observer )
{
	if( !observer || !observer->m_pActiveItem )
		return 0;
	CBasePlayerItem *pItem = observer->m_pActiveItem;
	if( !pItem->pev || !FClassnameIs( pItem->pev, "weapon_reetou" ) )
		return 0;
	return ((CReetou *)pItem)->SGTC_IsCloakScanActive() ? 1 : 0;
}

int SGTC_Bots_EnemyShielded( const CBasePlayer *victim )
{
	return ( victim && victim->m_bShieldWall ) ? 1 : 0;
}

int SGTC_Bots_ShieldState( const CBasePlayer *victim, int *pShieldType, float *pEnergy01 )
{
	if( pShieldType )
		*pShieldType = SGTC_SHIELD_NONE;
	if( pEnergy01 )
		*pEnergy01 = 0.0f;
	if( !victim )
		return 0;
	if( victim->m_bShieldWall )
	{
		if( pShieldType )
			*pShieldType = SGTC_SHIELD_PERSONAL;
	}
	else if( victim->m_bShieldSprint )
	{
		if( pShieldType )
			*pShieldType = SGTC_SHIELD_SPRINT;
	}
	else
		return 0;
	if( pEnergy01 )
		*pEnergy01 = (float)victim->m_iShieldCharge / 9.0f;
	return 1;
}

int SGTC_Bots_WeaponInfo( int iWeaponId, sgtc_bot_weapon_info_t *pOut )
{
	if( !pOut )
		return 0;
	memset( pOut, 0, sizeof( *pOut ) );
	pOut->id = iWeaponId;
	pOut->damage = -1.0f;
	pOut->range = -1.0f;
	pOut->worksVsShield = SGTC_Bots_WeaponBypassesShield( iWeaponId );
	pOut->seesInvis = ( iWeaponId == SGTC_WPN_REETOU ) ? 1 : 0;
	if( iWeaponId == SGTC_WPN_COUTEAU || iWeaponId == SGTC_WPN_COUJAF ||
		iWeaponId == SGTC_WPN_BATSUP || iWeaponId == SGTC_WPN_MAIN )
		pOut->category = SGTC_WCAT_MELEE;
	else if( iWeaponId == SGTC_WPN_REETOU )
		pOut->category = SGTC_WCAT_ENERGY;
	else if( iWeaponId == SGTC_WPN_ZAT || iWeaponId == SGTC_WPN_ZATARC )
		pOut->category = SGTC_WCAT_ZAT;
	else if( iWeaponId == SGTC_WPN_LANCE || iWeaponId == SGTC_WPN_BIGLANCE ||
		iWeaponId == SGTC_WPN_GACGOA )
		pOut->category = SGTC_WCAT_ENERGY;
	else if( iWeaponId == SGTC_WPN_HANDGRENADE || iWeaponId == SGTC_WPN_MUSTARDGRENADE ||
		iWeaponId == SGTC_WPN_NARCOGRENADE || iWeaponId == SGTC_WPN_NERVEGRENADE ||
		iWeaponId == SGTC_WPN_FLASHBANG || iWeaponId == SGTC_WPN_SMOKEGRENADE ||
		iWeaponId == SGTC_WPN_TRIPMINE || iWeaponId == SGTC_WPN_SATCHEL )
		pOut->category = SGTC_WCAT_BLAST;
	else if( iWeaponId == SGTC_WPN_MEDIC || iWeaponId == SGTC_WPN_INVIS ||
		iWeaponId == SGTC_WPN_SHIELD || iWeaponId == SGTC_WPN_GOAREGEN )
		pOut->category = SGTC_WCAT_UTILITY;
	else
		pOut->category = SGTC_WCAT_BULLET;

	/* Damage/range from Victor's Addendum F reference decode (combat.cpp
	 * switch + weapon defs).  -1 stays for values still TBD in the ref (MP5/M16
	 * FireBulletsPlayer iDamage semantics, larve/tacgun damage, grenade fuse,
	 * lance radius, mine alpha, zat projectile speed).  Range is the port's
	 * effective engagement distance used for weapon-choice hysteresis. */
	switch( iWeaponId )
	{
	case SGTC_WPN_COUTEAU:   pOut->damage = 54.0f;  pOut->range = 80.0f;   break;
	case SGTC_WPN_COUJAF:    pOut->damage = 54.0f;  pOut->range = 80.0f;   break;
	case SGTC_WPN_BATSUP:    pOut->damage = 54.0f;  pOut->range = 80.0f;   break;
	case SGTC_WPN_MAIN:      pOut->damage = 54.0f;  pOut->range = 80.0f;   break;
	case SGTC_WPN_M92S:      pOut->damage = 40.0f;  pOut->range = 1200.0f; break;
	case SGTC_WPN_MP5:       pOut->damage = 16.0f;  pOut->range = 1400.0f; break;
	case SGTC_WPN_P90:       pOut->damage = 14.0f;  pOut->range = 1400.0f; break;
	case SGTC_WPN_M16:       pOut->damage = 17.0f;  pOut->range = 1800.0f; break;
	case SGTC_WPN_PSG1:      pOut->damage = 120.0f; pOut->range = 2600.0f; break;
	case SGTC_WPN_SHOTGUN_STOCK: pOut->damage = 24.0f; pOut->range = 500.0f;  break; // per pellet
	case SGTC_WPN_USAS:      pOut->damage = 24.0f;  pOut->range = 500.0f;  break; // per pellet
	case SGTC_WPN_ZAT:       pOut->damage = 0.0f;   pOut->range = 900.0f;  break; // stun
	case SGTC_WPN_ZATARC:    pOut->damage = 80.0f;  pOut->range = 2000.0f; break;
	case SGTC_WPN_LANCE:     pOut->damage = 60.0f;  pOut->range = 2000.0f; break;
	case SGTC_WPN_BIGLANCE:  pOut->damage = 240.0f; pOut->range = 2000.0f; break; // port build-21 scale (John's 10x design)
	case SGTC_WPN_REETOU:    pOut->damage = 80.0f;  pOut->range = 1600.0f; break;
	case SGTC_WPN_HANDGRENADE: pOut->damage = 120.0f; pOut->range = 400.0f; break;
	case SGTC_WPN_SATCHEL:   pOut->damage = 180.0f; pOut->range = 200.0f;  break;
	case SGTC_WPN_TRIPMINE:  pOut->damage = 150.0f; pOut->range = 200.0f;  break;
	default: break;
	}
	return 1;
}

