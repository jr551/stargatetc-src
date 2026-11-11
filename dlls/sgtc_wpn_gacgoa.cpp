// sgtc_wpn_gacgoa.cpp — StargateTC CGrenadeChoc (split from sgtc_weapons.cpp; NEW file).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
#include "sgtc_weapons.h"
#include "sgtc_monsters.h"

enum gacgoa_e
{
	GACGOA_IDLE = 0,
	GACGOA_FIDGET,
	GACGOA_CHARGE,
	GACGOA_THROW1,
	GACGOA_THROW2,
	GACGOA_THROW3,
	GACGOA_HOLSTER,
	GACGOA_DRAW = 7,
	GACGOA_CHARGE_ALT = 8,
	GACGOA_THROW_ALT1 = 9,
	GACGOA_THROW_ALT2 = 10,
};

// ===========================================================================
// CGrenadeChoc - "Grenade Choc" (Goa'uld shock grenade). Analogue:
// handgrenade.cpp. No client event.
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_gacgoa, CGrenadeChoc );

// Save/restore (#99). Charge timers may hold the -1 "not charging" sentinel; FIELD_TIME
// round-trips it as a negative value, which is all the >= 0 / < 0 tests need.
TYPEDESCRIPTION CGrenadeChoc::m_SaveData[] =
{
	DEFINE_FIELD( CGrenadeChoc, m_flChargeBegin, FIELD_TIME ),
	DEFINE_FIELD( CGrenadeChoc, m_flChargeLatch, FIELD_TIME ),
	DEFINE_FIELD( CGrenadeChoc, m_iSgtcGacThrowAlt, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE( CGrenadeChoc, CBasePlayerWeapon )

void CGrenadeChoc::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_gacgoa.mdl" );
	m_iId = SGTC_WPN_GACGOA;
	m_iClip = -1;
	m_iDefaultAmmo = 4;
	FallInit();
	pev->dmg = 60.0f;	// ref @0xee394: gSkillData+0x114 (MP hardcode 60)
	m_flChargeBegin = -1.0f;
	m_flChargeLatch = -1.0f;
	m_iSgtcGacThrowAlt = 0;
}

void CGrenadeChoc::Precache( void )
{
	PRECACHE_MODEL( "models/w_gacgoa.mdl" );
	PRECACHE_MODEL( "models/v_gacgoa.mdl" );
	PRECACHE_MODEL( "models/v_gacgoaT.mdl" );
	PRECACHE_MODEL( "models/p_gacgoa.mdl" );
	PRECACHE_SOUND( "weapons/gacgoa.wav" );
	PRECACHE_SOUND( "weapons/gacencl.wav" );
	PRECACHE_SOUND( "common/null.wav" );
	UTIL_PrecacheOther( "monster_skull" );
}

SGTC_GETITEM( CGrenadeChoc, SGTC_AMMO_GACG, 4, WEAPON_NOCLIP, 5, 4, SGTC_WPN_GACGOA, 24, 10 )
SGTC_ADD_TO_PLAYER( CGrenadeChoc )

BOOL CGrenadeChoc::Deploy( void )
{
	m_flChargeBegin = -1.0f;
	m_flChargeLatch = -1.0f;
	m_iSgtcGacThrowAlt = 0;
	return SGTC_DEPLOY( 2, "models/v_gacgoa.mdl", "models/p_gacgoa.mdl", GACGOA_DRAW, "crowbar" );
}

void CGrenadeChoc::Holster( int skiplocal )
{
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;

	if( !m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] )
	{
		m_pPlayer->pev->weapons &= ~( 1 << SGTC_WPN_GACGOA );
		DestroyItem();
	}
	else
		SendWeaponAnim( GACGOA_HOLSTER );

	m_flChargeBegin = -1.0f;
	m_flChargeLatch = -1.0f;
	m_iSgtcGacThrowAlt = 0;

	EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "common/null.wav", 1.0, ATTN_NORM );
}

static BOOL SGTC_GacgoaSkullGate( CBasePlayer *pPlayer )
{
	return ( (int)pPlayer->pev->team == SGTC_ENTTEAM_GOAULD &&
		pPlayer->m_iSgtcPortraitShown == SGTC_CLASS_JAFFA &&
		pPlayer->m_iSgtcTalentDeploy != 0 );
}

void CGrenadeChoc::PrimaryAttack( void )	// ref @0xfe67c
{
	if( SGTC_GacgoaSkullGate( m_pPlayer ) )
		return;

	if( m_pPlayer->m_flGateTravel > gpGlobals->time )	// ref +0x408
		return;

	if( m_pPlayer->m_bHurtInvertFx )	// ref +0x4dd homing target paint
	{
		UTIL_MakeVectors( m_pPlayer->pev->angles );	// ref pev+0x50 (angles, not v_angle)
		Vector vecSrc = m_pPlayer->GetGunPosition();
		Vector vecEnd = vecSrc + gpGlobals->v_forward * 8192.0f;
		TraceResult tr;
		UTIL_TraceLine( vecSrc, vecEnd, dont_ignore_monsters, m_pPlayer->edict(), &tr );	// ref flag 0
		CBaseEntity *pTarget = CBaseEntity::Instance( tr.pHit );
		// ref vfuncs +0xa8 (IsAlive) +0x94 (IsPlayer) both required, team mismatch
		if( pTarget && pTarget->IsAlive() && pTarget->IsPlayer() &&
			pTarget->pev->team != m_pPlayer->pev->team )
		{
			CBaseEntity *pSkull = NULL;
			while( ( pSkull = UTIL_FindEntityByClassname( pSkull, "monster_skull" ) ) != NULL )
			{
				CSGTCSkull *pOwned = (CSGTCSkull *)pSkull;
				if( pOwned->m_pSkullOwner == m_pPlayer )
					pOwned->m_hEnemy = pTarget;
			}
		}
		m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.1f;
		m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.1f;
		return;
	}

	if( m_flChargeBegin >= 0.0f )
		return;

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] < 1 )
		return;

	m_flChargeBegin = gpGlobals->time;
	m_flChargeLatch = -1.0f;	// ref +0x188 = -1.0 at charge start
	SendWeaponAnim( GACGOA_CHARGE );
	m_pPlayer->m_iWeaponVolume = 200;	// ref +0xaac
	EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "weapons/gacencl.wav", 0.8, ATTN_NORM );
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.5f;
	m_iSgtcGacThrowAlt = 0;
}

void CGrenadeChoc::SecondaryAttack( void )	// ref @0xfe9fa
{
	if( !SGTC_GacgoaSkullGate( m_pPlayer ) )
	{
		if( m_pPlayer->m_flGateTravel > gpGlobals->time )	// ref +0x408
			return;

		if( m_pPlayer->m_bHurtInvertFx )
		{
			UTIL_MakeVectors( m_pPlayer->pev->angles );	// ref pev+0x50
			Vector vecSrc = m_pPlayer->GetGunPosition();
			Vector vecEnd = vecSrc + gpGlobals->v_forward * 8192.0f;
			TraceResult tr;
			UTIL_TraceLine( vecSrc, vecEnd, ignore_monsters, m_pPlayer->edict(), &tr );	// ref flag 1
			Vector vecSpot = tr.vecEndPos;
			CBaseEntity *pSkull = CBaseEntity::Create( "monster_skull", vecSpot,
				gpGlobals->v_forward, NULL );
			if( pSkull )
			{
				((CSGTCSkull *)pSkull)->m_pSkullOwner = m_pPlayer;	// ref entity+200
				pSkull->pev->takedamage = DAMAGE_NO;	// ref pev+0x16c @0xfe9fa
			}
			m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.5f;
			m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.5f;
			return;
		}

		if( m_flChargeBegin >= 0.0f )
			return;

		if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
			return;

		m_flChargeBegin = gpGlobals->time;
		m_flChargeLatch = -1.0f;	// ref +0x188 = -1.0
		SendWeaponAnim( GACGOA_CHARGE_ALT );
		m_pPlayer->m_iWeaponVolume = 200;
		EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "weapons/gacencl.wav", 0.8, ATTN_NORM );
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.5f;
		m_iSgtcGacThrowAlt = 1;
	}
}

void CGrenadeChoc::WeaponIdle( void )	// ref @0xfecd4
{
	// ref @0xfecd4: latch (+0x188) stamps once when armed while charging (+0x184 set)
	if( m_flChargeLatch < 0.0f && m_flChargeBegin >= 0.0f )
		m_flChargeLatch = gpGlobals->time;

	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;

	if( m_flChargeBegin < 0.0f && m_flChargeLatch > 0.0f )
	{
		// ref: begin==0 + latch>0 — charge aborted post-arm: RetireWeapon or draw reset
		m_flChargeBegin = 0.0f;
		if( !m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] )
			RetireWeapon();	// ref vptr+0x178
		else
		{
			SendWeaponAnim( GACGOA_DRAW );	// ref anim 7
			m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
				UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
			m_flChargeLatch = -1.0f;	// ref +0x188 = -1.0
		}
		return;
	}

	if( m_flChargeBegin < 0.0f )
	{
		if( !m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] )
			return;

		int iAnim;
		if( UTIL_SharedRandomFloat( m_pPlayer->random_seed, 0.0f, 1.0f ) <= 0.75f )
		{
			iAnim = GACGOA_IDLE;
			m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
				UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
		}
		else
		{
			iAnim = GACGOA_FIDGET;
			m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.5f;
		}
		SendWeaponAnim( iAnim );
		return;
	}

	// Release charged throw (ref else-branch: +0x184 armed).
	Vector angThrow = m_pPlayer->pev->v_angle;	// ref pev+0x74 — v_angle only, no punchangle
	if( angThrow.x < 0.0f )
		angThrow.x = -10.0f + angThrow.x * ( ( 90.0f - 10.0f ) / 90.0f );
	else
		angThrow.x = -10.0f + angThrow.x * ( ( 90.0f + 10.0f ) / 90.0f );

	float flVel = ( 90.0f - angThrow.x ) * 4.0f;	// ref (90-pitch)*4, cap 500
	if( flVel > 500.0f )
		flVel = 500.0f;

	UTIL_MakeVectors( angThrow );
	Vector vecSrc = m_pPlayer->pev->origin + m_pPlayer->pev->view_ofs + gpGlobals->v_forward * 16.0f;
	Vector vecThrow = gpGlobals->v_forward * flVel + m_pPlayer->pev->velocity;
	float flFuse = ( m_flChargeBegin - gpGlobals->time ) + 3.0f;	// ref local_24
	if( flFuse < 0.0f )
		flFuse = 0.0f;

	m_pPlayer->SnagInvis();	// ref calls SnagInvis BEFORE ShootFlash

#ifndef CLIENT_DLL
	CGrenade *pNade = CGrenade::ShootFlash( m_pPlayer->pev, vecSrc, vecThrow, flFuse, 10 );
	if( pNade )
	{
		pNade->pev->owner = m_pPlayer->edict();
		pNade->pev->team = m_pPlayer->pev->team;
		if( m_iSgtcGacThrowAlt )
		{
			pNade->m_pSgtcThrower = m_pPlayer;
			pNade->pev->movetype = MOVETYPE_FLY;	// ref pev+0x108=5 @0xfecd4 alt branch
			pNade->pev->velocity = vecThrow;
			pNade->pev->origin = m_pPlayer->pev->origin;
			pNade->pev->angles = g_vecZero;
			pNade->pev->avelocity.y = 400.0f;
		}
	}
#endif

	const BOOL bAltThrow = m_iSgtcGacThrowAlt;
	if( bAltThrow )
	{
		if( flVel < 750.0f )
			SendWeaponAnim( GACGOA_THROW_ALT1 );
		else
			SendWeaponAnim( GACGOA_THROW_ALT2 );
	}
	else if( flVel < 500.0f )
		SendWeaponAnim( GACGOA_THROW1 );
	else if( flVel < 1000.0f )
		SendWeaponAnim( GACGOA_THROW2 );
	else
		SendWeaponAnim( GACGOA_THROW3 );

	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );	// ref SetAnimation(player,6)
	// ref emits NO gacgoa.wav here (sound is precached but not emitted in WeaponIdle)

	m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]--;
	m_flChargeBegin = -1.0f;	// ref +0x184 = 0
	m_flChargeLatch = -1.0f;	// ref +0x188 = 0
	m_iSgtcGacThrowAlt = 0;

	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.5f;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.5f;

	if( !m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] )
		m_flTimeWeaponIdle = m_flNextSecondaryAttack = m_flNextPrimaryAttack;
}

SGTC_NO_RELOAD( CGrenadeChoc )
