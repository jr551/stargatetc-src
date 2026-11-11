// sgtc_wpn_zatarc.cpp — StargateTC CZatarc (split from sgtc_weapons.cpp; NEW file).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
#include "effects.h"
#include "sgtc_weapons.h"
#include "sgtc_lanceproj.h"
#include "sgtc_util.h"
#include "sgtc_sniper.h"	// sway / fire-note hooks

// ===========================================================================
// CZatarc - "Zat'arc" heavy energy gun. Analogue: gauss.cpp.
// Event: events/zatarc.sc.
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_zatarc, CZatarc );

// Save/restore (#99). Not saved: m_usFire (rebuilt by Precache()) and m_iSgtcZoomStage (written,
// never read; the zoom itself lives in the player's fov/m_iFOV).
TYPEDESCRIPTION CZatarc::m_SaveData[] =
{
	DEFINE_FIELD( CZatarc, m_flZatarcRegenNext, FIELD_TIME ),
	DEFINE_FIELD( CZatarc, m_bSgtcZarSplash, FIELD_INTEGER ),
	DEFINE_FIELD( CZatarc, m_flSgtcRechargeFx, FIELD_TIME ),
};

IMPLEMENT_SAVERESTORE( CZatarc, CBasePlayerWeapon )

void CZatarc::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_zar.mdl" );
	m_iId = SGTC_WPN_ZATARC;
	m_iDefaultAmmo = 40;
	m_flZatarcRegenNext = 0.0f;
	m_flSgtcRechargeFx = 0.0f;
	FallInit();
}

void CZatarc::Precache( void )
{
	PRECACHE_MODEL( "models/v_zar.mdl" );
	PRECACHE_MODEL( "models/w_zar.mdl" );
	PRECACHE_MODEL( "models/p_zar.mdl" );
	PRECACHE_MODEL( "sprites/laserbeam.spr" );
	PRECACHE_MODEL( "sprites/Muz1.spr" );

	// Trace-end splash: zarimpact is the heavy energy burst; zatimpact keeps the
	// effect visible when a server is missing the newer sprite (combat.cpp uses
	// it for the world-hit impact too).
	// Model indices are per-level, so re-evaluate the fallback on every
	// precache instead of caching a string_t across a changelevel.
	int iSplash = PRECACHE_MODEL( "sprites/zarimpact.spr" );
	PRECACHE_MODEL( "sprites/zatimpact.spr" );
	m_bSgtcZarSplash = iSplash ? 1 : 0;

	PRECACHE_SOUND( "weapons/zar.wav" );

	m_usFire = PRECACHE_EVENT( 1, "events/zatarc.sc" );
}

SGTC_GETITEM( CZatarc, SGTC_AMMO_ENERGIE5, 40, WEAPON_NOCLIP, 3, 0, SGTC_WPN_ZATARC, 0, 15 )
SGTC_ADD_TO_PLAYER( CZatarc )

BOOL CZatarc::Deploy( void )
{
	m_pPlayer->SGTC_WeapDispSet( 1 );
	return DefaultDeploy( "models/v_zar.mdl", "models/p_zar.mdl", 2, "hive" );
}

void CZatarc::PrimaryAttack( void )
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )
		return;

	int &pool = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
	if( pool <= 0 )
		return;

	m_pPlayer->SnagInvis();

	// ref @0x1c7128: delay 0x3c23d70a (0.01), origin = pev+8 (pev->origin),
	// angles = pev+0x50 (pev->angles), all params 0, last bool 1.
	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFire,
		0.01f, (float *)&m_pPlayer->pev->origin, (float *)&m_pPlayer->pev->angles,
		0, 0, 0, 0, 0, 1 );

	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );

	UTIL_MakeVectors( m_pPlayer->pev->v_angle );	// ref +0x74 — no punchangle
	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecAiming = gpGlobals->v_forward;
	// Bots only: the hitscan and the server trace below must use the same
	// (swayed) vector, otherwise the splash and the damage disagree.
	SGTC_Sniper_SwayAimVector( m_pPlayer, &vecAiming );
	SGTC_Sniper_NoteFire( m_pPlayer );
	Vector vecSpread( 0.00873f, 0.00873f, 0.00873f );
	// ref: dist imm 0x46000000 (8192), bullettype 0x12 (SGTC_BULLET_ZATARC)
	(void)m_pPlayer->FireBulletsPlayer( 1, vecSrc, vecAiming,
		vecSpread, 8192.0f, SGTC_BULLET_ZATARC, 0, 0,
		m_pPlayer->pev, m_pPlayer->random_seed );
#ifndef CLIENT_DLL
	TraceResult tr;
	UTIL_TraceLine( vecSrc, vecSrc + vecAiming * 8192.0f,
		dont_ignore_monsters, m_pPlayer->edict(), &tr );

	CBaseEntity *pHit = CBaseEntity::Instance( tr.pHit );
	if( pHit && pHit->IsPlayer() )
		((CBasePlayer *)pHit)->m_bLanceStunFlag = 1;

	CSprite *pMuz = CSprite::SpriteCreate( "sprites/Muz1.spr", tr.vecEndPos, TRUE );
	if( pMuz )
	{
		pMuz->AnimateAndDie( 20.0f );
		pMuz->SetTransparency( kRenderTransAdd, 255, 255, 255, 255, 14 );
	}

	// Energy splash where the bolt lands. Entity hits are covered here; world
	// hits get their zatimpact from combat.cpp (the two traces use different
	// random spread, so each endpoint shows exactly one burst).
	if( tr.pHit != NULL && ENTINDEX( tr.pHit ) != 0 )
	{
		const char *pszSplash = m_bSgtcZarSplash ? "sprites/zarimpact.spr" : "sprites/zatimpact.spr";
		CSprite *pSplash = CSprite::SpriteCreate( pszSplash, tr.vecEndPos, TRUE );
		if( pSplash )
		{
			// AnimateAndDie lifetime is frames / framerate: 30 fps ~= 0.3 s.
			pSplash->AnimateAndDie( 30.0f );
			pSplash->SetTransparency( kRenderTransAdd, 255, 255, 255, 220, 14 );
		}
	}

#endif

	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 1.2f;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
		UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
	pool -= 1;
	m_flZatarcRegenNext = gpGlobals->time + 1.2f;
}

void CZatarc::SecondaryAttack( void )	// ref CZatarc::SecondaryAttack @0x1c75d4 (zoom toggle)
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )	// ref +0x408
		return;

	const float fov = m_pPlayer->pev->fov;
	// Order matters: test the widest stage first (as CPsg1::SecondaryAttack
	// does), otherwise the >= 35 test swallows 50 and 35 is never reached.
	if( fov >= 50.0f )
	{
		m_pPlayer->m_iFOV = 0x23;
		m_pPlayer->pev->fov = 35.0f;
		m_iSgtcZoomStage = 1;
	}
	else if( fov >= 35.0f )
	{
		m_pPlayer->m_iFOV = 0;
		m_pPlayer->pev->fov = 0.0f;
		m_iSgtcZoomStage = 0;
	}
	else if( fov == 0.0f )
	{
		m_pPlayer->m_iFOV = 0x32;
		m_pPlayer->pev->fov = 50.0f;
		m_iSgtcZoomStage = 1;
	}
	pev->nextthink = gpGlobals->time + 0.1f;
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.5f;	// ref +0x1d8
}

void CZatarc::Holster( int skiplocal )	// ref CZatarc::Holster @0x1c79aa
{
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;
	int &pool = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
	if( pool == 0 )
		pool += 1;	// ref: leave 1 cell on holster
	m_pPlayer->pev->fov = 0.0f;	// ref pev+0x214 — reset zoom on holster
}

void CZatarc::Reload( void )	// ref CZatarc::Reload @0x1c7774
{
	int &pool = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
	const int iBefore = pool;
	const BOOL bBelowCap = ( pool < 0x28 );

	// ref cap 0x28 (40), +1 cell per 2.0s
	SGTC_RegenAmmo( pool, m_flZatarcRegenNext, 0x28, 1, 2.0f );

	// Recharge cue: a short blue cell-glow pulse at the gun, drawn only when a
	// cell was actually regained from below the cap (the cadence is 1 cell per
	// 2 s, so this cannot machine-gun the effect).
	if( bBelowCap && pool > iBefore )
	{
		if( gpGlobals->time > m_flSgtcRechargeFx )
		{
			m_flSgtcRechargeFx = gpGlobals->time + 1.5f;

			Vector vecGlow = m_pPlayer->GetGunPosition();

			MESSAGE_BEGIN( MSG_PAS, SVC_TEMPENTITY, vecGlow );
				WRITE_BYTE( 27 );	// TE_DLIGHT
				WRITE_COORD( vecGlow.x );
				WRITE_COORD( vecGlow.y );
				WRITE_COORD( vecGlow.z );
				WRITE_BYTE( 14 );	// radius (x10)
				WRITE_BYTE( 60 );	// r
				WRITE_BYTE( 140 );	// g
				WRITE_BYTE( 255 );	// b
				WRITE_BYTE( 2 );	// life 0.2s
				WRITE_BYTE( 0 );	// decay
			MESSAGE_END();
		}
	}
}

void CZatarc::WeaponIdle( void )	// ref CZatarc::WeaponIdle @0x1c781c
{
	Reload();		// ref vcall+0x170
	ResetEmptySound();	// ref vcall+0x158

	if( m_pPlayer->m_flGateTravel > gpGlobals->time )
	{
		m_pPlayer->m_iFOV = 0;			// ref player+0xcb0
		m_pPlayer->pev->fov = 0.0f;		// ref pev+0x214
		m_iSgtcZoomStage = 0;			// ref this+0x204
	}

	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;

	int iAnim;
	if( UTIL_SharedRandomFloat( m_pPlayer->random_seed, 0.0f, 1.0f ) <= 0.75f )
	{
		iAnim = 0;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
			UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
	}
	else
	{
		iAnim = 3;	// ref anim 3 (zat uses anim4)
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.5f;
	}
	SendWeaponAnim( iAnim, 1, 0 );	// ref vcall+0x160 (anim,1,0)
}
