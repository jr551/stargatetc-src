// sgtc_wpn_coujaf.cpp — StargateTC CCoujaf (split from sgtc_weapons.cpp; NEW file).

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
#include <cstddef>

#if defined( __GNUC__ ) && defined( __i386__ ) && !defined( CLIENT_DLL )
SGTC_STATIC_ASSERT( offsetof( CCoujaf, m_pSgtcLatchTarget ) == 0x200,
	"CCoujaf m_pSgtcLatchTarget must sit at ref +0x200" );
SGTC_STATIC_ASSERT( offsetof( CCoujaf, m_trSgtcRef204 ) == 0x204,
	"CCoujaf m_trSgtcRef204 must sit at ref +0x204" );
SGTC_STATIC_ASSERT( offsetof( CCoujaf, m_iSprShockwave ) == 0x240,
	"CCoujaf m_iSprShockwave must sit at ref +0x240" );
#endif

// rodata in CCoujaf::Swing (@0xd4962)
static const float kCoujafSwingRange = 32.0f;
static const float kCoujafSwingMissDelay = 0.5f;	// @0x1cd77c
static const float kCoujafSwingHitDelay = 0.28f;	// double @0x1cd798
static const float kCoujafSwingDmgHalf = 2.0f;	// crowbar skill / this when primary hot
static const float kCoujafSwingTalentDmg = 70.0f;
static const float kCoujafSwingVolAttn = 0.800000011920929f;

// rodata @0x1cd7a0 (GOT −0x37270)
static const float kCoujafLatchClearIdle = 0.5f;
// rodata @0x1cd7c0 / @0x1cd7c8 (GOT −0x37250 / −0x37248)
static const float kCoujafPaEpilogueBump = 0.3f;
static const float kCoujafSwingAgainBump = 0.1f;
// rodata @0x1cd7e8
static const float kCoujafIdlePlayerBump = 0.15f;
// rodata @0x1cd7d0 area
static const float kCoujafIdleThinkBump = 0.1f;
static const float kCoujafLatchTraceFar = 8192.0f;
static const float kCoujafLatchTraceNear = 120.0f;

#ifndef CLIENT_DLL
static void CoujafPaEpilogue( CCoujaf *wpn )
{
	wpn->m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + kCoujafPaEpilogueBump;
	wpn->m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + kCoujafPaEpilogueBump;
}

static void CoujafIdleLatchBody( CCoujaf *wpn, CBaseEntity *pTarget )
{
	if( !pTarget->IsPlayer() || !wpn->m_pPlayer )
		return;
	CBasePlayer *pVictim = static_cast<CBasePlayer *>( pTarget );

	CBasePlayer *pAtt = wpn->m_pPlayer;
	UTIL_MakeVectors( pAtt->pev->angles );	// ref WeaponIdle @0xd5c7e: pev+0x50 angles, not v_angle
	Vector vecSrc = pAtt->GetGunPosition();

	TraceResult trFar;
	UTIL_TraceLine( vecSrc, vecSrc + gpGlobals->v_forward * kCoujafLatchTraceFar,
		dont_ignore_monsters, ENT( pAtt->pev ), &trFar );

	pVictim->m_iStamina = 0;
	pVictim->SGTC_UpdateStamina();
	pVictim->SnagInvis();
	pVictim->m_flSnagArmorRamp = 0.0f;	// ref pev+0x1bc := 0
	pVictim->pev->maxspeed = 0.0f;		// ref pev+0x210 := 0

	TraceResult trNear;
	UTIL_TraceLine( vecSrc, vecSrc + gpGlobals->v_forward * kCoujafLatchTraceNear,
		ignore_monsters, ENT( pAtt->pev ), &trNear );

	pVictim->pev->origin = trNear.vecEndPos;
	pVictim->pev->velocity = trNear.vecPlaneNormal;
}

static void CoujafPaSameTeamStrike( CCoujaf *wpn, CBasePlayer *pVictim, const TraceResult &tr )
{
	pVictim->m_iStamina = 100;	// ref +0x43c
	pVictim->SGTC_UpdateStamina();
	pVictim->SnagInvis();
	pVictim->m_iSnagArmorCap = 100;	// ref +0x908
	pVictim->m_flSnagArmorRamp = 100.0f;	// ref pev+0x1bc
	pVictim->pev->maxspeed = 0.0f;		// ref pev+0x210
	pVictim->m_vecHurtInvertRgb = g_vecZero;	// ref +0x4c0
	pVictim->pev->velocity = g_vecZero;
	pVictim->pev->friction = 0.0f;
	pVictim->pev->impulse = 5;		// ref pev+0x148 := 5

	const Vector &vecHit = tr.vecEndPos;
	MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, vecHit );
		WRITE_BYTE( TE_BEAMCYLINDER );
		WRITE_COORD( vecHit.x );
		WRITE_COORD( vecHit.y );
		WRITE_COORD( vecHit.z );
		WRITE_COORD( vecHit.x );
		WRITE_COORD( vecHit.y );
		WRITE_COORD( vecHit.z + 250.0f );	// ref rodata 250 @0x1cd7bc
		WRITE_SHORT( wpn->m_iSprShockwave );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 5 );	// ref life byte
		WRITE_BYTE( 10 );	// ref width 0xa
		WRITE_BYTE( 0 );
		WRITE_BYTE( 255 );
		WRITE_BYTE( 255 );
		WRITE_BYTE( 255 );
		WRITE_BYTE( 0xff );
		WRITE_BYTE( 0 );
	MESSAGE_END();

	EMIT_SOUND( ENT( pVictim->pev ), CHAN_WEAPON, "weapons/coujafcorps.wav", 1.0f, ATTN_NORM );
}
#endif

// ===========================================================================
// CCoujaf - "Coujaf" melee. Analogue: crowbar.cpp.
// Event: events/coujaf.sc.
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_coujaf, CCoujaf );

// Save/restore (#99). Not saved: m_trHit/m_trSgtcRef204 (TraceResult), m_pSgtcLatchTarget (raw
// victim pointer; cleared on Deploy/Holster and revalidated before use),
// m_usFire/m_iSprShockwave (precache handles) and the layout padding.
TYPEDESCRIPTION CCoujaf::m_SaveData[] =
{
	DEFINE_FIELD( CCoujaf, m_iSwing, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE( CCoujaf, CBasePlayerWeapon )

void CCoujaf::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_coujaf.mdl" );
	m_iId = SGTC_WPN_COUJAF;
	m_iClip = -1;
	m_pSgtcLatchTarget = NULL;
	FallInit();
}

void CCoujaf::Precache( void )
{
	PRECACHE_MODEL( "models/v_coujaf.mdl" );
	PRECACHE_MODEL( "models/w_coujaf.mdl" );
	PRECACHE_MODEL( "models/p_coujaf.mdl" );
	m_iSprShockwave = PRECACHE_MODEL( "sprites/shockwave.spr" );

	SGTC_PrecacheSound( "weapons/nightstrike.wav" );
	PRECACHE_SOUND( "weapons/coujafcorps.wav" );
	PRECACHE_SOUND( "weapons/hitbody1.wav" );
	PRECACHE_SOUND( "weapons/hitbody2.wav" );
	PRECACHE_SOUND( "weapons/hitbody3.wav" );
	PRECACHE_SOUND( "weapons/swing1.wav" );
	PRECACHE_SOUND( "weapons/swing2.wav" );
	PRECACHE_SOUND( "weapons/swing3.wav" );
	PRECACHE_SOUND( "weapons/cbar_hit1.wav" );
	PRECACHE_SOUND( "weapons/cbar_hit2.wav" );

	m_usFire = PRECACHE_EVENT( 1, "events/coujaf.sc" );
}

SGTC_GETITEM( CCoujaf, NULL, -1, WEAPON_NOCLIP, 0, 0, SGTC_WPN_COUJAF, 0, 0 )
SGTC_ADD_TO_PLAYER( CCoujaf )

BOOL CCoujaf::Deploy( void )
{
	return SGTC_DEPLOY( 2, "models/v_coujaf.mdl", "models/p_coujaf.mdl", SGTC_DRAW, "crowbar" );
}

void CCoujaf::PrimaryAttack( void )
{
	// ref @0xd54e2
	if( m_pPlayer->IsObserver() )
		return;

	if( m_pSgtcLatchTarget )
	{
		m_pSgtcLatchTarget = NULL;
		m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + kCoujafLatchClearIdle;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + kCoujafLatchClearIdle;
		return;
	}

#ifndef CLIENT_DLL
	// ref: shield + hurtinvert both set → 8192 trace latch path; else crowbar Swing @0xd5a94.
	if( m_pPlayer->m_bShieldLarge && m_pPlayer->m_bHurtInvertFx )
	{
		UTIL_MakeVectors( m_pPlayer->pev->angles );	// ref PA latch @0xd55b0: pev+0x50 angles, not v_angle
		Vector vecSrc = m_pPlayer->GetGunPosition();

		TraceResult tr;
		UTIL_TraceLine( vecSrc, vecSrc + gpGlobals->v_forward * kCoujafLatchTraceFar,
			ignore_monsters, ENT( m_pPlayer->pev ), &tr );

		CBaseEntity *pHit = NULL;
		if( tr.flFraction < 1.0f )
			pHit = CBaseEntity::Instance( tr.pHit );

		if( pHit && pHit->IsPlayer() && pHit->IsAlive() )
		{
			CBasePlayer *pVictim = static_cast<CBasePlayer *>( pHit );
			if( pVictim->pev->team != m_pPlayer->pev->team )
			{
				m_pSgtcLatchTarget = pVictim;	// ref @0xd56ee
			}
			else if( pVictim->pev->maxspeed != 0.0f )
			{
				CoujafPaSameTeamStrike( this, pVictim, tr );
			}
		}

		CoujafPaEpilogue( this );
		return;
	}
#endif

	if( !Swing( 1 ) )
	{
#ifndef CLIENT_DLL
		if( m_flTimeWeaponIdle <= UTIL_WeaponTimeBase() )
		{
			SetThink( &CCoujaf::SwingAgain );
			pev->nextthink = gpGlobals->time + kCoujafIdleThinkBump;
			m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + kCoujafSwingAgainBump;
		}
#endif
	}
}

void CCoujaf::SwingAgain( void )
{
	Swing( 0 );
}

int CCoujaf::Swing( int fFirst )	// ref @0xd4962 (crowbar hull + SGTC latch swing)
{
	int fDidHit = FALSE;
	TraceResult tr;
	float flVol = 1.0f;
	int fHitWorld = TRUE;

	// ref @0xd498a: m_iSwing==1 && !m_bHurtInvertFx → clear m_iSwing
	if( m_iSwing == 1 && !m_pPlayer->m_bHurtInvertFx )
		m_iSwing = 0;

	UTIL_MakeVectors( m_pPlayer->pev->v_angle );
	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecEnd = vecSrc + gpGlobals->v_forward * kCoujafSwingRange;

	UTIL_TraceLine( vecSrc, vecEnd, dont_ignore_monsters, ENT( m_pPlayer->pev ), &tr );

#ifndef CLIENT_DLL
	SGTC_MeleeHullFallback( ENT( m_pPlayer->pev ), vecSrc, tr, vecEnd );
#endif

	if( tr.flFraction >= 1.0f )
	{
		if( fFirst )
		{
			m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + kCoujafSwingMissDelay;
			m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
		}
	}
	else
	{
		if( fFirst )
		{
			PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFire,
				0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0, 0, 0, 0, 0, 0 );
		}

		SendWeaponAnim( SGTC_FIRE2, 1, 0 );
		m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
		fDidHit = TRUE;

#ifndef CLIENT_DLL
		CBaseEntity *pEntity = CBaseEntity::Instance( tr.pHit );
		BOOL bTeamLatchSwing = FALSE;

		if( m_iSwing == 1 && pEntity && pEntity->IsPlayer() && pEntity->IsAlive() )
		{
			CBasePlayer *pVictim = static_cast<CBasePlayer *>( pEntity );
			if( pVictim->pev->team != m_pPlayer->pev->team )
			{
				bTeamLatchSwing = TRUE;
				SendWeaponAnim( SGTC_FIRE2, 1, 0 );
				EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_ITEM, "weapons/nightstrike.wav", // ref @0xd4e22 chan 3
					1.0f, kCoujafSwingVolAttn );
				m_pPlayer->m_iWeaponVolume = 128;
				m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + kCoujafSwingMissDelay;
			}
		}

		if( pEntity )
		{
			ClearMultiDamage();

			// ref @0xd4e7b: first-swing latch on a live enemy player queues a
			// flat 1054 hit (melee base + rodata 1000.0) before the normal hit.
			if( bTeamLatchSwing )
			{
				pEntity->TraceAttack( m_pPlayer->pev, kSgtcSkillMeleeDmg + 1000.0f,
					gpGlobals->v_forward, &tr, DMG_CLUB );
			}

			// ref @0xd4f52/@0xd4fb1: melee base 54, halved to 27 on SP
			// follow-ups inside 1.0s unless multiplayer (same gate as batsup).
			const float flDamage = ( ( m_flNextPrimaryAttack + 1.0f < UTIL_WeaponTimeBase() ) ||
				( g_pGameRules && g_pGameRules->IsMultiplayer() ) )
				? kSgtcSkillMeleeDmg : kSgtcSkillMeleeDmg * 0.5f;

			pEntity->TraceAttack( m_pPlayer->pev, flDamage,
				gpGlobals->v_forward, &tr, DMG_CLUB );
			ApplyMultiDamage( m_pPlayer->pev, m_pPlayer->pev );

			if( pEntity->Classify() != CLASS_NONE &&
				pEntity->Classify() != CLASS_MACHINE )
			{
				switch( RANDOM_LONG( 0, 2 ) )
				{
				case 0: EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM,
					"weapons/hitbody1.wav", 1, ATTN_NORM, 0, 98 + RANDOM_LONG( 0, 3 ) ); break;
				case 1: EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM,
					"weapons/hitbody2.wav", 1, ATTN_NORM, 0, 98 + RANDOM_LONG( 0, 3 ) ); break;
				default: EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM,
					"weapons/hitbody3.wav", 1, ATTN_NORM, 0, 98 + RANDOM_LONG( 0, 3 ) ); break;
				}
				m_pPlayer->m_iWeaponVolume = 128;
				if( !pEntity->IsAlive() )
				{
					m_iSwing = 0;
					m_trSgtcRef204 = tr;
					return TRUE;
				}
				flVol = 0.1f;
				fHitWorld = FALSE;
			}
		}

		if( fHitWorld )
		{
			TEXTURETYPE_PlaySound( &tr, vecSrc, vecEnd, BULLET_PLAYER_CROWBAR );
			if( g_pGameRules && g_pGameRules->IsMultiplayer() )
				flVol = 1.0f;
			switch( RANDOM_LONG( 0, 1 ) )
			{
			case 0:
				EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM,
					"weapons/cbar_hit1.wav", flVol, ATTN_NORM, 0,
					98 + RANDOM_LONG( 0, 3 ) );
				break;
			default:
				EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM,
					"weapons/cbar_hit2.wav", flVol, ATTN_NORM, 0,
					98 + RANDOM_LONG( 0, 3 ) );
				break;
			}
		}

		if( bTeamLatchSwing )
			m_iSwing = 0;
#endif
		m_trHit = tr;
		m_trSgtcRef204 = tr;

		if( m_iSwing != 1 )
		{
			m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + kCoujafSwingHitDelay;
			m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + kCoujafSwingHitDelay;
		}
	}

	m_pPlayer->m_iWeaponVolume = (int)( flVol * 512.0f );
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
		UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );

	return fDidHit;
}

void CCoujaf::WeaponIdle( void )
{
	// ref @0xd5b30 — not the SGTC_WEAPON_IDLE macro (no 10–15s idle anim timer bump here).
	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;

	if( m_iSwing == 1 )
	{
		if( !m_pPlayer->m_bHurtInvertFx )
			m_iSwing = 0;

		if( m_iSwing == 1 )
		{
			if( !Swing( 1 ) )
				SwingAgain();
#ifndef CLIENT_DLL
			pev->nextthink = gpGlobals->time + kCoujafIdleThinkBump;
#endif
		}
	}

	if( m_pSgtcLatchTarget )
	{
		if( FNullEnt( m_pSgtcLatchTarget->edict() ) || !m_pSgtcLatchTarget->IsAlive() )
			m_pSgtcLatchTarget = NULL;
	}

#ifndef CLIENT_DLL
	if( m_pSgtcLatchTarget )
		CoujafIdleLatchBody( this, m_pSgtcLatchTarget );
#endif

	ResetEmptySound();
	SendWeaponAnim( SGTC_IDLE );
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + kCoujafIdlePlayerBump;
}

// ref @0xd46e6 — NOT the stock holster: no viewmodel/weaponmodel clear, no
// m_fInReload reset. Body = vtable+0x15c ResetEmptySound then vtable+0x160
// SendWeaponAnim(this, 0, 0, <stale this>). SendWeaponAnim never reads its
// third arg (@0x14(%ebp) unreferenced in ref body), so the stale pointer is
// inert — pass 0 for determinism.
void CCoujaf::Holster( int skiplocal /* = 0 */ )
{
	ResetEmptySound();
	SendWeaponAnim( SGTC_IDLE, 0, 0 );
}

// ref @0xd495c — push ebp/mov ebp,esp/pop ebp/ret; literal empty body.
void CCoujaf::Smack( void )
{
}

SGTC_NO_SECONDARY( CCoujaf )
SGTC_NO_RELOAD( CCoujaf )
