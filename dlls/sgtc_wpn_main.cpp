// sgtc_wpn_main.cpp — StargateTC CMainDieu (split from sgtc_weapons.cpp; NEW file).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
#include "sgtc_weapons.h"
#include "sgtc_util.h"

#ifndef CLIENT_DLL
// True for a non-player entity that belongs to the shooter or to the shooter's
// team: same owner, same (non-zero) team, or owned by a teammate. The secondary
// blast must neither push nor damage these (tacs, mines, larves, skulls ...).
static BOOL SGTC_MainIsFriendly( CBasePlayer *pShooter, CBaseEntity *pEnt )
{
	if( pEnt->pev->owner == pShooter->edict() )
		return TRUE;

	const int iTeam = pShooter->pev->team;
	if( iTeam == 0 )
		return FALSE;	// no team: nothing counts as a teammate

	if( pEnt->pev->team == iTeam )
		return TRUE;

	if( pEnt->pev->owner )
	{
		CBaseEntity *pOwner = CBaseEntity::Instance( pEnt->pev->owner );
		if( pOwner && pOwner->pev->team == iTeam )
			return TRUE;
	}
	return FALSE;
}
#endif

// ===========================================================================
// CMainDieu - "Main Dieu" hand device. Analogue: egon.cpp (energy device).
// Events: main.sc / mainc.sc / main2.sc / mains.sc.
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_main, CMainDieu );

// Save/restore (#99). Not saved: m_usFire*/m_iSprShockwave/m_iSprHandTir (precache handles,
// rebuilt by Precache()).
TYPEDESCRIPTION CMainDieu::m_SaveData[] =
{
	DEFINE_FIELD( CMainDieu, m_flSgtcBlastTimer, FIELD_TIME ),
	DEFINE_FIELD( CMainDieu, m_flSgtcMainAmmoGate, FIELD_TIME ),
	DEFINE_FIELD( CMainDieu, m_bSgtcMainBeam, FIELD_INTEGER ),
	DEFINE_FIELD( CMainDieu, m_bSgtcMainCont, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE( CMainDieu, CBasePlayerWeapon )

void CMainDieu::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_main.mdl" );
	m_iId = SGTC_WPN_MAIN;
	m_iClip = -1;
	m_iDefaultAmmo = 100;
	FallInit();
	m_bSgtcMainBeam = FALSE;
	m_bSgtcMainCont = FALSE;
}

void CMainDieu::Precache( void )
{
	PRECACHE_MODEL( "models/v_main.mdl" );
	PRECACHE_MODEL( "models/w_main.mdl" );
	PRECACHE_MODEL( "models/p_main.mdl" );
	m_iSprHandTir = PRECACHE_MODEL( "sprites/handtir.spr" );
	m_iSprShockwave = PRECACHE_MODEL( "sprites/shockwave.spr" );	// ref stores @+0x204

	PRECACHE_SOUND( "weapons/tirmain.wav" );
	PRECACHE_SOUND( "weapons/tirmaincont.wav" );
	PRECACHE_SOUND( "weapons/mainvide.wav" );
	PRECACHE_SOUND( "empty.wav" );

	m_usFire = PRECACHE_EVENT( 1, "events/main.sc" );
	m_usFireCont = PRECACHE_EVENT( 1, "events/mainc.sc" );
	m_usFire2 = PRECACHE_EVENT( 1, "events/main2.sc" );
	m_usFireStop = PRECACHE_EVENT( 1, "events/mains.sc" );
}

SGTC_GETITEM( CMainDieu, SGTC_AMMO_ENERGIE, 100, WEAPON_NOCLIP, 3, 1, SGTC_WPN_MAIN, 0, 25 )
SGTC_ADD_TO_PLAYER( CMainDieu )

BOOL CMainDieu::Deploy( void )
{
	m_bSgtcMainBeam = FALSE;
	m_bSgtcMainCont = FALSE;
	return SGTC_DEPLOY( 1, "models/v_main.mdl", "models/p_main.mdl", 5, "hive" );	// ref @0x117808
}

static void SGTC_MainDlight( const Vector &vecSrc )
{
	MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, vecSrc );
		WRITE_BYTE( TE_DLIGHT );
		WRITE_COORD( vecSrc.x );
		WRITE_COORD( vecSrc.y );
		WRITE_COORD( vecSrc.z );
		WRITE_BYTE( 10 );
		WRITE_BYTE( 255 );
		WRITE_BYTE( 50 );
		WRITE_BYTE( 50 );
		WRITE_BYTE( 7 );
		WRITE_BYTE( 0 );
	MESSAGE_END();
}

void CMainDieu::PrimaryAttack( void )	// ref @0x117880
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )	// ref +0x408
		return;

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] < 1 )
	{
		PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFireStop,
			0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0, 0, 0, 0, 0, 0 );
		EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "empty.wav", 1.0, ATTN_NORM );
		SendWeaponAnim( 0 );
		return;
	}

	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.3f;	// ref +0x1d4
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 1.0f;	// ref +0x1d8
	m_flSgtcBlastTimer = gpGlobals->time + 0.3f;		// ref +0x1fc
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.5f;	// ref +0x1dc
	if( m_flSgtcMainAmmoGate <= 0.0f )	// ref +0x200: arms once when <= 0 (first shot / after idle reset)
		m_flSgtcMainAmmoGate = gpGlobals->time + 0.5f;

	if( m_bSgtcMainBeam )
	{
		if( !m_bSgtcMainCont )
		{
			PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFireCont,
				0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0, 0, 0, 0, 0, 0 );
			EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "weapons/tirmaincont.wav", 1.0, ATTN_NORM );
			SendWeaponAnim( 2 );
			m_bSgtcMainCont = TRUE;
		}
	}
	else
	{
		PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFireStop,
			0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0, 0, 0, 0, 0, 0 );
		if( m_flSgtcMainAmmoGate < gpGlobals->time )
			EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "empty.wav", 1.0, ATTN_NORM );
		PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFire,
			0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0, 0, 0, 0, 0, 0 );
		m_bSgtcMainBeam = TRUE;
	}

	m_pPlayer->SnagInvis();
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );

	UTIL_MakeVectors( m_pPlayer->pev->v_angle );	// ref pev+0x74 (v_angle only, no punchangle)
	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecEnd = vecSrc + gpGlobals->v_forward * 8192.0f;

	TraceResult tr;
	UTIL_TraceLine( vecSrc, vecEnd, dont_ignore_monsters, m_pPlayer->edict(), &tr );

	if( m_flSgtcMainAmmoGate < gpGlobals->time )
		SGTC_MainDlight( tr.vecEndPos );	// ref: TE_DLIGHT at trace endpos

	CBaseEntity *pHit = CBaseEntity::Instance( tr.pHit );
	if( pHit && pHit->IsPlayer() &&		// ref vfunc+0xa8 == player check
		pHit->pev->team != m_pPlayer->pev->team )	// ref pev+0x1ac team compare
	{
		if( m_flSgtcMainAmmoGate < gpGlobals->time )
		{
			CBasePlayer *pVictim = (CBasePlayer *)pHit;
			pVictim->pev->velocity = g_vecZero;
			pVictim->TakeDamage( pVictim->pev, m_pPlayer->pev, 1.0f, DMG_ENERGYBEAM );	// ref 0x3f800000, 0x8000
			pVictim->TakeDamage( pVictim->pev, m_pPlayer->pev, 18.0f, DMG_RADIATION );	// ref 0x41900000, 0x100
			pVictim->m_iStamina = 0;
			pVictim->SGTC_UpdateStamina();
		UTIL_ScreenFade( pVictim, Vector( 255, 50, 50 ), 3.0f, 3.0f, 0xf5, 0 );

		// ref @0x117880: handtir beam TE, muzzle→victim origin+30 (skipped when
		// the victim's custom effects bit14 (0x4000) is set).
		if( !( (int)pVictim->pev->effects & 0x4000 ) )
		{
			MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
				WRITE_BYTE( TE_BEAMPOINTS );	// ref TE 0
				WRITE_COORD( vecSrc.x );
				WRITE_COORD( vecSrc.y );
				WRITE_COORD( vecSrc.z - 5.0f );
				WRITE_COORD( pVictim->pev->origin.x );
				WRITE_COORD( pVictim->pev->origin.y );
				WRITE_COORD( pVictim->pev->origin.z + 30.0f );
				WRITE_SHORT( m_iSprHandTir );	// ref this+0x208 sprites/handtir.spr
				WRITE_BYTE( 0 );
				WRITE_BYTE( 0 );
				WRITE_BYTE( 1 );
				WRITE_BYTE( 0x1e );
				WRITE_BYTE( 9 );
				WRITE_BYTE( 0xff );
				WRITE_BYTE( 0x32 );
				WRITE_BYTE( 0x32 );
				WRITE_BYTE( 0x80 );
				WRITE_BYTE( 0 );
			MESSAGE_END();
		}
		}
	}

	if( m_flSgtcMainAmmoGate < gpGlobals->time )
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]--;	// ref: plain decrement, no suit update

	if( m_flSgtcMainAmmoGate < gpGlobals->time )
	{
		Vector vecBeam = vecSrc + gpGlobals->v_forward * 10.0f;
		SGTC_MainDlight( vecBeam );
	}
}

void CMainDieu::SecondaryAttack( void )	// ref CMainDieu::SecondaryAttack @0x11849c
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )	// ref +0x408
		return;

	m_flSgtcMainAmmoGate = 0.0f;	// ref +0x200
	m_bSgtcMainBeam = FALSE;	// ref +0x214
	m_bSgtcMainCont = FALSE;	// ref +0x215

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 4 )
		return;

	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFire2,
		0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0, 0, 0, 0, 0, 0 );
	m_pPlayer->SetAnimation( (PLAYER_ANIM)6 );

	UTIL_MakeVectors( m_pPlayer->pev->v_angle );
	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecDir = gpGlobals->v_forward;
	m_pPlayer->FireBulletsPlayer( 1, vecSrc, vecDir, Vector( 0.01f, 0.01f, 0.01f ),
		100.0f, 10, 0, 0, m_pPlayer->pev, m_pPlayer->random_seed );
	m_flSgtcBlastTimer = gpGlobals->time + 0.5f;	// ref this+0x1fc

	SGTC_MainDlight( vecSrc );
	m_pPlayer->SnagInvis();
	m_pPlayer->m_bShieldSprint = 0;	// ref player +0x4e9 — after SnagInvis in dump

#ifndef CLIENT_DLL
	// ref @0x107dac-0x107dcf: center = vecSrc + gpGlobals->v_forward * 85.0
	//   (rodata @B-0x3141c). Port added a bare unit forward instead.
	Vector vecSphereCenter = vecSrc + gpGlobals->v_forward * 85.0f;

	CBaseEntity *pScan = NULL;
	while( ( pScan = UTIL_FindEntityInSphere( pScan, vecSphereCenter, 100.0f ) ) != NULL )
	{
		BOOL bSkip = FALSE;

		if( pScan->IsPlayer() )
		{
			CBasePlayer *pPlr = (CBasePlayer *)pScan;
			if( pPlr == m_pPlayer || pPlr->IsObserver() ||
				pPlr->pev->team == m_pPlayer->pev->team )
				bSkip = TRUE;
			else
			{
				const int iDist = SGTC_Distance3D( pPlr->pev->origin,
					m_pPlayer->pev->origin );
				pPlr->m_iStamina = pPlr->m_iStamina + iDist - 200;
				if( pPlr->m_iStamina < 0 )
					pPlr->m_iStamina = 0;
				pPlr->SGTC_UpdateStamina();
				pPlr->m_fSgtcPushLatch = 0x25;	// ref vfunc +0x18c arg 0x25
			}
		}
		else if( SGTC_MainIsFriendly( m_pPlayer, pScan ) )
			bSkip = TRUE;

		if( !bSkip )
		{
			// Only push things that obey physics. Without the movetype gate the
			// "non-monster" arm wrote velocity onto doors, trains and other
			// brush/mover entities.
			const int iMove = pScan->pev->movetype;
			const BOOL bPushable = ( iMove == MOVETYPE_WALK || iMove == MOVETYPE_STEP ||
				iMove == MOVETYPE_TOSS || iMove == MOVETYPE_BOUNCE );

			if( bPushable &&
				( ( !pScan->IsPlayer() && !pScan->MyMonsterPointer() ) ||
				pScan->IsAlive() ) )
			{
				const int iDist = SGTC_Distance3D( pScan->pev->origin,
					m_pPlayer->pev->origin );
				const float flPush = (float)( ( 200 - iDist ) * 10 );
				pScan->pev->velocity.x = gpGlobals->v_forward.x * flPush;
				pScan->pev->velocity.y = gpGlobals->v_forward.y * flPush;
				const float flPrevZ = pScan->pev->velocity.z;
				const int iDistZ = SGTC_Distance3D( pScan->pev->origin,
					m_pPlayer->pev->origin );
				pScan->pev->velocity.z = (float)( ( 200 - iDistZ ) * 3 ) + flPrevZ;
				SGTC_MainDlight( pScan->pev->origin );
			}

			if( pScan->pev->takedamage != DAMAGE_NO )
				pScan->TakeDamage( pScan->pev, m_pPlayer->pev, 35.0f, DMG_CRUSH );
		}
	}
#endif

	m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] -= 5;
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.7f;	// ref +0x1d8
	m_flNextPrimaryAttack = m_flNextSecondaryAttack;	// ref copies +0x1d8 → +0x1d4
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.5f;		// ref +0x1dc
}

void CMainDieu::Reload( void )	// ref @0x118d9c
{
	// ref cap 100, +0.6s per cell (nothing to do at the cap)
	SGTC_RegenAmmo( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType], m_flSgtcBlastTimer,
		100, 1, 0.6f );
}

void CMainDieu::WeaponIdle( void )	// ref @0x118e50
{
	Reload();	// ref vfunc+0x170

	m_flSgtcMainAmmoGate = 0;	// ref +0x200 — cleared BEFORE the idle-time gate
	if( m_bSgtcMainCont )	// ref +0x215: stop mains.sc + empty.wav while still firing
	{
		EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "empty.wav", 1.0, ATTN_NORM );
		PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFireStop,
			0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0, 0, 0, 0, 0, 0 );
	}

	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;

	m_bSgtcMainBeam = FALSE;	// ref +0x214
	m_bSgtcMainCont = FALSE;	// ref +0x215
	int iAnim;
	if( UTIL_SharedRandomFloat( m_pPlayer->random_seed, 0.0f, 1.0f ) <= 0.75f )
	{
		iAnim = 0;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
			UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
	}
	else
	{
		iAnim = 4;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.5f;
	}
	SendWeaponAnim( iAnim );
}

// ref @0x109074 — NOT the stock holster:
//  player+0x39c (m_flNextAttack) = WTB+0.5; ammo floored to >=1;
//  PLAYBACK mains.sc (m_usFireStop) + EMIT empty.wav. No viewmodel clear.
void CMainDieu::Holster( int skiplocal )
{
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] == 0 )
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = 1;

	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFireStop,
		0.0f, (float *)&g_vecZero, (float *)&g_vecZero, 0.8f, 1.0f, 0, 0, 0, 0 );

	// ref CMainDieu::Holster @0x118e50: chan 1 (WEAPON), vol 1.0, attn 0.8.
	EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "empty.wav", 1.0f, 0.8f );
}
