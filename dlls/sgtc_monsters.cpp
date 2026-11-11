// hlsdk-portable/dlls/sgtc_monsters.cpp
//
// StargateTC custom server monsters — see sgtc_monsters.h for the
// factory list and disassembly references. All behavior below is a clean-room
// re-implementation of the decoded think/touch shapes using stock SDK calls.

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "player.h"
#include "weapons.h"
#include "soundent.h"
#include "skill.h"
#include "schedule.h"
#include "sgtc_monsters.h"
#include "sgtc_diag.h"
#include "effects.h"
#include "gamerules.h"
#include "sgtc_util.h"
#include "sgtc_weapons.h"

extern cvar_t sgtc_tacdist;
extern cvar_t sgtc_svcrazy;
extern cvar_t sgtc_tac_bubble;
int SGTC_Round_intconstrain( int lo, int hi, int v );

static int SGTC_TacSeparation( void )
{
	return SGTC_Round_intconstrain( 50, 8000, (int)sgtc_tacdist.value );
}

#ifndef CLIENT_DLL

// ref Endo_takefromlarger / Endo_distance1d @0x1ab9de
static int SGTC_Endo_takefromlarger( int a, int b )
{
	return ( abs( a ) >= abs( b ) ) ? a : b;
}

static float SGTC_Endo_distance1d( int a, int b )
{
	return (float)SGTC_Endo_takefromlarger( a - b, b - a );
}

static int g_iSkullNargasSpr = 0;

static const float kSkullNudge = 5.0f;		// rodata -0x2c4a0 @ebx=0x204a10
static const float kSkullFrateHigh = 160.0f;	// -0x2c490
static const float kSkullFrateLow = 16.0f;	// -0x2c48c
static const float kSkullBeamInterval = 0.3f;	// fldl -0x2c498
static const float kSkullThinkInterval = 0.1f;	// fldl -0x2c460
static const int kSkullAcquireDist = 0x2328;	// initial best Manhattan cap (9000)

static void SGTC_SkullNudgePeaks( entvars_t *pev, int iAxis, int iPlane, int iLane )
{
	if( iAxis == 1 )
	{
		pev->angles.x += kSkullNudge;
		pev->v_angle.x += kSkullNudge;
	}
	else if( iAxis == 2 )
	{
		pev->angles.x -= kSkullNudge;
		pev->v_angle.x -= kSkullNudge;
	}
	if( iPlane == 1 )
	{
		pev->angles.y += kSkullNudge;
		pev->v_angle.y += kSkullNudge;
	}
	else if( iPlane == 2 )
	{
		pev->angles.y -= kSkullNudge;
		pev->v_angle.y -= kSkullNudge;
	}
	if( iLane == 1 )
	{
		pev->angles.z += kSkullNudge;
		pev->v_angle.z += kSkullNudge;
	}
	else if( iLane == 2 )
	{
		pev->angles.z -= kSkullNudge;
		pev->v_angle.z -= kSkullNudge;
	}
}

static void SGTC_SkullBeamPoints( CBaseEntity *pEnt )
{
	const Vector &o = pEnt->pev->origin;
	MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, o );
		WRITE_BYTE( TE_BEAMPOINTS );
		WRITE_COORD( o.x );
		WRITE_COORD( o.y );
		WRITE_COORD( o.z );
		WRITE_COORD( o.x );
		WRITE_COORD( o.y );
		WRITE_COORD( o.z );
		WRITE_SHORT( g_iSkullNargasSpr );
		WRITE_BYTE( 35 );
		WRITE_BYTE( 12 );
		WRITE_BYTE( 5 );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 204 );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 157 );
		WRITE_BYTE( 255 );
		WRITE_BYTE( 0 );
	MESSAGE_END();
}

static void SGTC_SkullRampFramerate( entvars_t *pev, byte &rampFlag )
{
	if( rampFlag == 1 )
	{
		if( pev->framerate > kSkullFrateHigh )
			return;
		if( pev->framerate <= kSkullFrateHigh )
			pev->framerate -= kSkullNudge;
	}
	else if( !rampFlag )
	{
		if( pev->framerate < kSkullFrateLow )
			return;
		if( pev->framerate >= kSkullFrateLow )
			pev->framerate += kSkullNudge;
		else
			rampFlag = 1;
	}
}

static void SGTC_SkullSetChaseVelAxis( entvars_t *pev, float &velComp,
	float skullCoord, float targCoord )
{
	const int iSkull = (int)skullCoord;
	const int iTarg = (int)targCoord;
	const float dist = SGTC_Endo_distance1d( iSkull, iTarg );
	int iVel;
	if( pev->maxspeed == dist )
		iVel = (int)pev->maxspeed;
	else
		iVel = (int)dist;
	if( targCoord >= skullCoord )
		velComp = (float)iVel;
	else
		velComp = (float)( -iVel );
}

static void SGTC_SkullChaseVelocity( entvars_t *pev, CBaseEntity *pTarget )
{
	entvars_t *tpev = pTarget->pev;
	SGTC_SkullSetChaseVelAxis( pev, pev->velocity.x, pev->origin.x, tpev->origin.x );
	SGTC_SkullSetChaseVelAxis( pev, pev->velocity.y, pev->origin.y, tpev->origin.y );
	SGTC_SkullSetChaseVelAxis( pev, pev->velocity.z, pev->origin.z, tpev->origin.z );
}

#define SGTC_REPLIC_DETONATE_DELAY 1000000.0f	// ref @0x153757 rodata 1e6 (not 15s snark timer)
#define SGTC_LARVE_DETONATE_DELAY 15.0f
#define SGTC_REPLIC_SPEED 250.0f
#define SGTC_REPLIC_HOP 120.0f
#define SGTC_REPLICATEUR_SPEED 50.0f // decoded rodata float @ 0x1d9cc0
// BestVisibleEnemy() only walks m_pLink, which Look() builds. Think-driven
// monsters that never run RunAI must call Look() first (stock squeakgrenade.cpp:
// Look( 512 ) before BestVisibleEnemy()).
#define SGTC_REPLIC_LOOK_DIST 512
#define SGTC_LOOK_DIST 1024
#define SGTC_TAC_RANGE 1024.0f
#define SGTC_TAC_REVEAL_RANGE 256.0f

// CBaseMonster::Killed only removes an entity that gibs, and several of the
// custom-think spawns below never get FL_MONSTER or MonsterInit, so a lethal
// hit that does not gib would leave them thinking. Shared tail of the Killed()
// overrides: tell a MonsterMaker owner (as CBaseMonster::Killed does), stop
// taking damage and queue the removal. Callers clear think/touch first.
static void SGTC_KilledRemove( CBaseMonster *pMonster )
{
	if( pMonster->pev->owner != NULL )
	{
		CBaseEntity *pOwner = CBaseEntity::Instance( pMonster->pev->owner );
		if( pOwner )
			pOwner->DeathNotice( pMonster->pev );
	}
	pMonster->pev->takedamage = DAMAGE_NO;
	pMonster->pev->solid = SOLID_NOT;
	UTIL_Remove( pMonster );
}

// =============================================================
// CSGTCReplic — monster_replic (models/replic.mdl)
// Snark-hunt clone of CReplicMonster::HuntThink/SuperBounceTouch.
// =============================================================

TYPEDESCRIPTION CSGTCReplic::m_SaveData[] =
{
	DEFINE_FIELD( CSGTCReplic, m_flDie, FIELD_TIME ),
	DEFINE_FIELD( CSGTCReplic, m_flNextHunt, FIELD_TIME ),
	DEFINE_FIELD( CSGTCReplic, m_flNextSound, FIELD_TIME ),
};
IMPLEMENT_SAVERESTORE( CSGTCReplic, CGrenade );
LINK_ENTITY_TO_CLASS( monster_replic, CSGTCReplic );

void CSGTCReplic::Precache( void )
{
	PRECACHE_MODEL( "models/replic.mdl" );
	// ref CReplicMonster::Precache @0x15387a — model only; no squeek sounds.
}

void CSGTCReplic::Spawn( void )
{
	Precache();

	pev->movetype = MOVETYPE_BOUNCE;
	pev->solid = SOLID_BBOX;
	SET_MODEL( ENT( pev ), "models/replic.mdl" );
	UTIL_SetSize( pev, Vector( -4, -4, 0 ), Vector( 4, 4, 8 ) );
	UTIL_SetOrigin( pev, pev->origin );

	SetTouch( &CSGTCReplic::SuperBounceTouch );
	SetThink( &CSGTCReplic::HuntThink );
	pev->nextthink = gpGlobals->time + 0.1;
	m_flNextHunt = gpGlobals->time + 1E6;
	m_flNextSound = gpGlobals->time;

	pev->flags |= FL_MONSTER;
	pev->takedamage = DAMAGE_AIM;
	pev->health = 30.0f;	// ref @0x153790 rodata 30 (NOT gSkillData.snarkHealth)
	pev->gravity = 0.5;
	pev->friction = 0.5;
	pev->maxspeed = 40.0f;	// ref @0x1537c6 rodata 40
	pev->dmg = 75.0f;	// ref @0x1537d8 rodata 75 (NOT snarkDmgPop)

	m_flDie = gpGlobals->time + SGTC_REPLIC_DETONATE_DELAY;
	m_flFieldOfView = 0; // 180 degrees, like the snark

	pev->sequence = 0;
	ResetSequenceInfo();
}

void CSGTCReplic::HuntThink( void )
{
	pev->nextthink = gpGlobals->time + 0.1;

	// ref @0x153a49-0x153a86: !IsAlive → clear touch/think, UTIL_Remove (no sound/dmg).
	if( !IsAlive() )
	{
		SetTouch( NULL );
		SetThink( NULL );
		UTIL_Remove( this );
		return;
	}

	// ref @0x153b3e-0x153b57: time >= m_flDie → return (no Detonate/EMIT in ref).
	if( gpGlobals->time >= m_flDie )
		return;

	if ( m_flNextHunt <= gpGlobals->time )
	{
		Look( SGTC_REPLIC_LOOK_DIST );	// rebuild m_pLink; BestVisibleEnemy walks it
		m_hEnemy = BestVisibleEnemy();
		m_flNextHunt = gpGlobals->time + 0.5;
	}

	if ( m_hEnemy == 0 )
	{
		// Idle hop so the block pile looks alive.
		if ( pev->flags & FL_ONGROUND )
			pev->velocity = Vector( RANDOM_FLOAT( -80, 80 ), RANDOM_FLOAT( -80, 80 ), 150 );
		return;
	}

	Vector vecDelta = m_hEnemy->pev->origin - pev->origin;
	float flLen = vecDelta.Length();
	if ( flLen > 1.0f )
		vecDelta = vecDelta * ( 1.0f / flLen );

	pev->velocity.x = vecDelta.x * SGTC_REPLIC_SPEED;
	pev->velocity.y = vecDelta.y * SGTC_REPLIC_SPEED;
	if ( pev->flags & FL_ONGROUND )
		pev->velocity.z = SGTC_REPLIC_HOP;

	// ref HuntThink @0x153a2c-0x1541b0: no EMIT_SOUND / CSoundEnt (stock snark carryover).
}

void CSGTCReplic::SuperBounceTouch( CBaseEntity *pOther )
{
	if ( pOther->pev->takedamage && pOther != this )
	{
		// ref CReplicMonster::SuperBounceTouch @0x1543a0:
		//   ClearMultiDamage; TraceAttack(60.0, dir, ptr, 0x4); ApplyMultiDamage.
		//   No TakeDamage, no velocity write here — bounce stays in HuntThink.
		ClearMultiDamage();
		TraceResult tr;
		UTIL_TraceLine( pev->origin, pOther->pev->origin, dont_ignore_monsters,
			pev->owner ? pev->owner : ENT( pev ), &tr );
		pOther->TraceAttack( pev, 60.0f,
			( pOther->pev->origin - pev->origin ).Normalize(), &tr, DMG_SLASH );
		ApplyMultiDamage( pev, pev );

		// ref @0x154416: second ApplyMultiDamage (multi dmg accumulates per-bounce),
		// then pOther->pev->dmg += gSkillData->snarkDmgPop @0x154461.
		ApplyMultiDamage( pev, pev );
		pOther->pev->dmg += gSkillData.snarkDmgPop;
	}

	// ref SuperBounceTouch @0x1542a4-0x1545a0: no EMIT_SOUND.

	// Spawn parks m_flNextHunt at +1E6 like the stock snark, whose bounce touch
	// arms the hunt (m_flNextHunt = time). Without this the replic never looks.
	m_flNextHunt = gpGlobals->time;
}

void CSGTCReplic::Detonate( void )
{
	// ref: no Detonate symbol — silent remove only (stock snark blast/sound removed).
	SetTouch( NULL );
	SetThink( NULL );
	UTIL_Remove( this );
}

void CSGTCReplic::Killed( entvars_t *pevAttacker, int iGib )
{
	// CGrenade::Killed calls the non-virtual CGrenade::Detonate (full HE blast
	// with fireball and decals), so Detonate() above never runs. Remove
	// silently, same as HuntThink's !IsAlive() path.
	SetTouch( NULL );
	SetThink( NULL );
	SGTC_KilledRemove( this );
}

// =============================================================
// CSGTCLarve — monster_larve (models/w_larve.mdl)
// Snark-hunt clone of CLarveMonster::HuntThink/SuperBounceTouch.
// =============================================================

TYPEDESCRIPTION CSGTCLarve::m_SaveData[] =
{
	DEFINE_FIELD( CSGTCLarve, m_flDie, FIELD_TIME ),
	DEFINE_FIELD( CSGTCLarve, m_vecTarget, FIELD_VECTOR ),
	DEFINE_FIELD( CSGTCLarve, m_flNextHunt, FIELD_TIME ),
	DEFINE_FIELD( CSGTCLarve, m_flNextHit, FIELD_TIME ),
	DEFINE_FIELD( CSGTCLarve, m_posPrev, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( CSGTCLarve, m_hOwner, FIELD_EHANDLE ),
	DEFINE_FIELD( CSGTCLarve, m_iSgtcFlag, FIELD_INTEGER ),
	DEFINE_FIELD( CSGTCLarve, m_flSgtcTouchGate, FIELD_TIME ),
	DEFINE_FIELD( CSGTCLarve, m_bSgtcLarveDetLatch, FIELD_CHARACTER ),
	DEFINE_FIELD( CSGTCLarve, m_bSgtcLarveHuntLatch, FIELD_CHARACTER ),
};
IMPLEMENT_SAVERESTORE( CSGTCLarve, CGrenade );
float CSGTCLarve::m_flNextBounceSoundTime = 0.0f;
LINK_ENTITY_TO_CLASS( monster_larve, CSGTCLarve );

void CSGTCLarve::Precache( void )
{
	// ref CLarveMonster::Precache @0x100e42
	PRECACHE_MODEL( "models/w_larve.mdl" );
	PRECACHE_MODEL( "models/w_squeak.mdl" );
	PRECACHE_SOUND( "weapons/electro6.wav" );	// build-11: placeholder for missing player/gloweyes.wav
	PRECACHE_SOUND( "weapons/larve1.wav" );
	PRECACHE_SOUND( "weapons/larve2.wav" );
	PRECACHE_SOUND( "squeek/sqk_deploy1.wav" );
}

void CSGTCLarve::Spawn( void )
{
	Precache();

	pev->movetype = MOVETYPE_BOUNCE;
	pev->solid = SOLID_BBOX;
	SET_MODEL( ENT( pev ), "models/w_larve.mdl" );
	// ref Spawn @0x100b5e: UTIL_SetSize (-4,-4,-6),(4,4,6) — half the old snark-clone hull.
	UTIL_SetSize( pev, Vector( -4, -4, -6 ), Vector( 4, 4, 6 ) );
	UTIL_SetOrigin( pev, pev->origin );

	SetTouch( &CSGTCLarve::SuperBounceTouch );
	SetThink( &CSGTCLarve::HuntThink );
	pev->nextthink = gpGlobals->time + 0.1;
	m_flNextHunt = gpGlobals->time + 1E6;
	m_bSgtcLarveDetLatch = 0;
	m_bSgtcLarveHuntLatch = 0;

	pev->flags |= FL_MONSTER;
	pev->takedamage = DAMAGE_AIM;
	pev->health = gSkillData.snarkHealth;	// ref @0x100d1e gSkillData+0xa4 MATCH
	pev->gravity = 0.5;
	pev->friction = 0.5;
	pev->dmg = 90.0f;	// ref @0x100d86 rodata 90 (NOT snarkDmgPop)
	pev->maxspeed = (float)RANDOM_LONG( 200, 300 );	// ref @0x100d63-0x100d7a

	m_flDie = gpGlobals->time + SGTC_LARVE_DETONATE_DELAY;
	m_flNextHit = 0.0f;
	m_flSgtcTouchGate = 0.0f;
	m_hOwner = NULL;
	m_vecTarget = g_vecZero;
	m_posPrev = g_vecZero;
	m_flFieldOfView = 0;

	pev->sequence = 0;
	ResetSequenceInfo();
}

// ref CLarveMonster::BestVisibleEnemy @0x1019fa
CBaseEntity *CSGTCLarve::BestVisibleEnemy( void )
{
	float flBest = 8192.0f;	// ref local init 0x2000
	CBaseEntity *pBest = NULL;

	if( m_bSgtcLarveDetLatch && !m_bSgtcLarveHuntLatch && pev->owner != NULL )
	{
		CBaseEntity *pOwner = CBaseEntity::Instance( pev->owner );
		if( pOwner && pOwner->IsPlayer() && pOwner->IsAlive() )
			return pOwner;
	}

	CBaseEntity *pScan = NULL;
	while( ( pScan = UTIL_FindEntityByClassname( pScan, "player" ) ) != NULL )
	{
		CBasePlayer *pPlayer = (CBasePlayer *)pScan;
		if( !pPlayer->IsAlive() )
			continue;
		if( pPlayer->m_bSnagInvisLatch )	// ref +0x4de skip cloaked
			continue;

		CBaseEntity *pOwner = pev->owner ? CBaseEntity::Instance( pev->owner ) : NULL;
		if( pOwner && pPlayer->pev->team != 0 && pPlayer->pev->team == pOwner->pev->team )
			continue;
		if( pPlayer->m_iSgtcPortraitShown == 4 )	// ref +0xa40 == 4 skip
			continue;
		if( !pPlayer->FVisible( this ) )
			continue;

		const float flDist = ( pPlayer->pev->origin - pev->origin ).Length();
		if( flDist < flBest )
		{
			flBest = flDist;
			pBest = pPlayer;
		}
	}
	return pBest;
}

// ref CLarveMonster::HuntThink @0x10103e (not snark clone)
void CSGTCLarve::HuntThink( void )
{
	if( !IsAlive() )
	{
		SetTouch( NULL );
		SetThink( NULL );
		UTIL_Remove( this );
		return;
	}

	if( m_iSgtcFlag )
	{
		pev->rendermode = kRenderTransTexture;
		pev->renderamt = 255.0f;
		pev->renderfx = kRenderFxNone;
		pev->rendercolor = Vector( 255, 255, 255 );
		pev->health = 19.0f;
	}

	StudioFrameAdvance( 0.1f );
	pev->nextthink = gpGlobals->time + 0.1f;

	if( gpGlobals->time >= m_flDie )
	{
		pev->velocity = pev->velocity.Normalize();
		pev->dmg = 90.0f;	// ref @0x1011d9 pev+0x160
		Detonate();
		return;
	}

	if( pev->waterlevel != 0 )
	{
		if( pev->movetype == MOVETYPE_BOUNCE )
			pev->movetype = MOVETYPE_FLY;
		pev->velocity = pev->velocity * 0.9f;
		pev->velocity.z += 8.0f;
	}
	else if( pev->movetype == MOVETYPE_FLY )
	{
		pev->movetype = MOVETYPE_BOUNCE;
	}

	if( m_flNextHunt <= gpGlobals->time )
	{
		m_hEnemy = BestVisibleEnemy();
		m_flNextHunt = gpGlobals->time + 0.5f;	// ref @0x1012cc rodata 0.5
	}

	const float flLeft = m_flDie - gpGlobals->time;
	if( flLeft <= 0.3f )
	{
		// ref @0x10149d: larve1 CHAN_VOICE (2), pitch 100, InsertSound 256
		EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "weapons/larve1.wav", 1.0f, ATTN_NORM, 0, 100 );
		CSoundEnt::InsertSound( bits_SOUND_COMBAT, pev->origin, 256, 0.25f );
	}

	if( m_hEnemy != NULL && m_hEnemy->IsAlive() && FVisible( m_hEnemy ) )
	{
		Vector vecDir = m_hEnemy->EyePosition() - pev->origin;
		m_vecTarget = vecDir.Normalize();

		const float flVel = pev->velocity.Length();
		float flAdj = pev->maxspeed / ( flVel + 10.0f );
		if( flAdj > 1.2f )
			flAdj = 1.2f;
		pev->velocity = pev->velocity * flAdj + m_vecTarget * 300.0f;
	}

	if( pev->flags & FL_ONGROUND )
		pev->avelocity = g_vecZero;
	else if( pev->avelocity == g_vecZero )
	{
		pev->avelocity.x = RANDOM_FLOAT( -100.0f, 100.0f );
		pev->avelocity.z = RANDOM_FLOAT( -100.0f, 100.0f );
	}

	if( ( pev->origin - m_posPrev ).Length() < 1.0f )
	{
		pev->velocity.x = RANDOM_FLOAT( -100.0f, 100.0f );
		pev->velocity.y = RANDOM_FLOAT( -100.0f, 100.0f );
	}
	m_posPrev = pev->origin;

	pev->angles = UTIL_VecToAngles( pev->velocity );
	pev->angles.z = 0.0f;
	pev->angles.x = 0.0f;
	pev->v_angle.x = 0.0f;
	pev->v_angle.z = 0.0f;

	if( m_bSgtcLarveHuntLatch )
	{
		m_bSgtcLarveDetLatch = 0;
		if( m_iSgtcFlag )
			UTIL_Remove( this );
		return;
	}

	if( !m_bSgtcLarveDetLatch )
		return;
	if( m_bSgtcLarveHuntLatch )
		return;	// ref @0x10191b — +0x41d set skips proximity walk

	CBaseEntity *pScan = NULL;
	while( ( pScan = UTIL_FindEntityByClassname( pScan, "player" ) ) != NULL )
	{
		if( !pScan->IsPlayer() || !pScan->IsAlive() )
			continue;
		if( pev->owner == NULL || pScan->edict() != pev->owner )
			continue;
		Vector vecA = pev->origin;
		Vector vecB = pScan->pev->origin;
		const int iDist = (int)( fabs( vecA.x - vecB.x ) + fabs( vecA.y - vecB.y ) + fabs( vecA.z - vecB.z ) );
		if( iDist > 59 )
			continue;
		((CBasePlayer *)pScan)->m_iSgtcAmmoLatch904++;
		UTIL_Remove( this );
		return;
	}
}

// ref CLarveMonster::SuperBounceTouch @0x101bc0
void CSGTCLarve::SuperBounceTouch( CBaseEntity *pOther )
{
	TraceResult tr = UTIL_GetGlobalTrace();

	if ( pev->owner != NULL )
	{
		if ( pOther->edict() == pev->owner )
			return;

		CBaseEntity *pThrowerEnt = CBaseEntity::Instance( pev->owner );
		if ( pThrowerEnt != NULL && pThrowerEnt->IsPlayer() && pOther->IsPlayer() &&
			g_pGameRules->PlayerRelationship( pThrowerEnt, pOther ) == GR_TEAMMATE )
			return;
	}

	pev->angles.x = 0.0f;
	pev->angles.z = 0.0f;

	if ( m_flNextHit > gpGlobals->time )
		return;

	const float flLatchPitch = 155.0f - 60.0f * ( ( m_flDie - gpGlobals->time ) / 20.0f );
	BOOL bInfected = FALSE;

	if ( pOther->pev->takedamage != DAMAGE_NO &&
		m_flSgtcTouchGate <= gpGlobals->time &&
		tr.pHit == pOther->edict() &&
		tr.pHit->v.modelindex != pev->modelindex &&
		pev->owner != NULL && m_iSgtcFlag &&
		pOther->IsPlayer() )
	{
		CBasePlayer *pThrower = static_cast<CBasePlayer *>( CBaseEntity::Instance( pev->owner ) );
		CBasePlayer *pVictim = static_cast<CBasePlayer *>( pOther );

		if ( pThrower != NULL && pVictim != NULL &&
			pThrower->IsAlive() && !pThrower->IsObserver() &&
			pVictim->IsAlive() && pVictim->pev->health >= 30.0f )
		{
			bInfected = TRUE;

			pThrower->SetAnimation( PLAYER_ATTACK1 );	// ref push 4 @0x101e78
			pThrower->m_iSgtcInfoLock = 1;			// ref +0xdc

			pThrower->pev->origin = pVictim->pev->origin;
			pThrower->pev->v_angle = pVictim->pev->v_angle;
			pThrower->pev->angles = pVictim->pev->angles;

			pThrower->TakeHealth( 100.0f, DMG_DROWNRECOVER );	// ref 0x80000 @0x101eea

			if ( FBitSet( pVictim->pev->flags, ( 1 << 14 ) ) )	// ref pev+0x1a4 bit 14
				pThrower->pev->origin.z += 20.0f;

			pThrower->pev->armorvalue = pVictim->pev->armorvalue;
			pThrower->m_iSnagArmorCap = pVictim->m_iSnagArmorCap;

			static const char *s_pszLarvePortraitModel[] =
			{
				NULL, "oneill", "carter", "jackson", "tealc", "commander",
			};
			const int iPortrait = pVictim->m_iSgtcPortraitShown;
			if ( iPortrait >= SGTC_CLASS_FIRST && iPortrait <= SGTC_CLASS_LAST )
			{
				edict_t *pEd = pThrower->edict();
				g_engfuncs.pfnSetClientKeyValue(
					ENTINDEX( pEd ),
					g_engfuncs.pfnGetInfoKeyBuffer( pEd ),
					"model",
					s_pszLarvePortraitModel[iPortrait] );
			}

			pThrower->RemoveAllItems( FALSE );

			const char *pszWeapon = "weapon_crowbar";
			if ( pVictim->m_pActiveItem != NULL )
				pszWeapon = STRING( pVictim->m_pActiveItem->pev->classname );
			pThrower->GiveNamedItem( pszWeapon );	// ref victim +0xcd4 -> classname @0x102187

			if ( !( pThrower->pev->weapons & ( 1 << 3 ) ) )
				pThrower->GiveNamedItem( "weapon_couteau" );
			if ( !( pThrower->pev->weapons & ( 1 << 17 ) ) )
				pThrower->GiveNamedItem( "weapon_zat" );

			pThrower->m_iSgtcTalentDeploy = 0;	// ref +0x918
			pThrower->m_flTalentCooldown = gpGlobals->time + 10.0f;	// ref +0x494

			pVictim->pev->effects |= EF_BRIGHTLIGHT;	// ref pev+0x118 = 0x80
			pVictim->Killed( pThrower->pev, 1 );	// ref @0x10222d

			const int iGlowPitch = (int)( flLatchPitch + 0.5f );
			EMIT_SOUND_DYN( ENT( pThrower->pev ), CHAN_VOICE, "weapons/electro6.wav",
				1.0f, 0.8f, 0, iGlowPitch );	// ref @0x1022c3 CHAN_VOICE(2)

			ClearMultiDamage();
			Vector vecDir = gpGlobals->v_forward;
			vecDir.z = 0.0f;
			float flDir = vecDir.Length();
			if ( flDir > 0.0f )
				vecDir = vecDir * ( 1.0f / flDir );
			pVictim->TraceAttack( pev, 60.0f, vecDir, &tr, DMG_POISON );	// ref 0x20000
			if ( m_hOwner != 0 )
				ApplyMultiDamage( pev, m_hOwner->pev );
			else
				ApplyMultiDamage( pev, pev );

			pVictim->pev->dmg += gSkillData.snarkDmgPop;	// ref gSkillData+0xac @0x1023f3

			Vector vecVel = pev->velocity;
			vecVel.z = 0.0f;
			float flVel = vecVel.Length();
			if ( flVel > 0.0f )
				g_vecAttackDir = vecVel * ( 1.0f / flVel );
		}
	}

	if ( g_pGameRules->IsMultiplayer() && gpGlobals->time < m_flNextBounceSoundTime )
	{
		m_flNextHit = gpGlobals->time + 0.1f;
		m_flNextHunt = gpGlobals->time;
		m_flSgtcTouchGate = gpGlobals->time + 0.1f;
		return;
	}

	if ( !( pev->flags & FL_ONGROUND ) )
	{
		if ( !bInfected && !( pOther->pev->flags & FL_WORLDBRUSH ) )
			CSoundEnt::InsertSound( bits_SOUND_COMBAT, pev->origin, 100, 0.1f );
	}
	else
	{
		const int iPitch = (int)( flLatchPitch + 0.5f );
		if ( RANDOM_FLOAT( 0.0f, 1.0f ) <= 0.5f )
			EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "weapons/larve1.wav", 1.0f, ATTN_NORM, 0, iPitch );
		else
			EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "weapons/larve2.wav", 1.0f, ATTN_NORM, 0, iPitch );
		CSoundEnt::InsertSound( bits_SOUND_COMBAT, pev->origin, 256, 0.25f );
	}

	m_flNextHit = gpGlobals->time + 0.1f;
	m_flNextHunt = gpGlobals->time;
	m_flSgtcTouchGate = gpGlobals->time + 0.1f;
	m_flNextBounceSoundTime = gpGlobals->time + 0.1f;
}

void CSGTCLarve::Detonate( void )
{
	if ( m_hEnemy != 0 )
	{
		Vector vecDelta = m_hEnemy->pev->origin - pev->origin;
		if ( vecDelta.Length() < 128.0f )
			m_hEnemy->TakeDamage( pev, pev, pev->dmg, DMG_BLAST );
	}
	// ref @0x102662 EMIT: "weapons/larve1.wav", CHAN_VOICE(2), vol 1.0, attn 0.8.
	EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "weapons/larve1.wav", 1.0, ATTN_NORM, 0, PITCH_NORM );
	SetTouch( NULL );
	SetThink( NULL );
	UTIL_Remove( this );
}

void CSGTCLarve::Killed( entvars_t *pevAttacker, int iGib )
{
	// CGrenade::Killed calls the non-virtual CGrenade::Detonate (full HE blast),
	// not the Detonate() above. Remove silently, same as HuntThink's
	// !IsAlive() path.
	SetTouch( NULL );
	SetThink( NULL );
	SGTC_KilledRemove( this );
}

// =============================================================
// CSGTCReplicateur — monster_replicateur + monster_replicator
// Headcrab-like hunter driven by ReplicateurThink (0x161be4).
// =============================================================

LINK_ENTITY_TO_CLASS( monster_replicateur, CSGTCReplicateur );
LINK_ENTITY_TO_CLASS( monster_replicator, CSGTCReplicateur );

void CSGTCReplicateur::Precache( void )
{
	PRECACHE_MODEL( "models/headcrab.mdl" );
	// ref CReplicateurMonster::Precache @0x1620ec: headcrab.mdl only —
	// no hc_* sound precaches (ReplicateurThink has no EMIT_SOUND).
}

void CSGTCReplicateur::Spawn( void )
{
	Precache();

	SET_MODEL( ENT( pev ), "models/headcrab.mdl" );
	UTIL_SetSize( pev, Vector( -12, -12, 0 ), Vector( 12, 12, 24 ) );
	UTIL_SetOrigin( pev, pev->origin );

	pev->solid = SOLID_SLIDEBOX;
	pev->movetype = MOVETYPE_STEP;
	m_bloodColor = BLOOD_COLOR_YELLOW;
	pev->health = gSkillData.headcrabHealth;	// ref @0x161eaa gSkillData+0x48 MATCH; no heavy variant in ref
	pev->view_ofs = Vector( 0, 0, 20 );
	m_flFieldOfView = 0.5f;
	m_MonsterState = MONSTERSTATE_NONE;
	pev->takedamage = DAMAGE_AIM;

	pev->sequence = 0;
	pev->frame = 0;
	pev->framerate = 1.0;

	SetThink( &CSGTCReplicateur::ReplicateurThink );
	pev->nextthink = gpGlobals->time + 0.1;
}

void CSGTCReplicateur::Killed( entvars_t *pevAttacker, int iGib )
{
	// No FL_MONSTER/MonsterInit: CBaseMonster::Killed would leave it thinking
	// (chasing, biting) unless it gibbed.
	SetTouch( NULL );
	SetThink( NULL );
	SGTC_KilledRemove( this );
}

Vector CSGTCReplicateur::Center( void )
{
	// Decoded Center() adds +6 z over the origin.
	return pev->origin + Vector( 0, 0, 6 );
}

Vector CSGTCReplicateur::BodyTarget( const Vector &posSrc )
{
	return Center();
}

void CSGTCReplicateur::ReplicateurThink( void )
{
	// Decoded repath interval @ 0x1d9cc8.
	pev->nextthink = gpGlobals->time + 0.1;

	Look( SGTC_LOOK_DIST );	// rebuild m_pLink; BestVisibleEnemy walks it
	m_hEnemy = BestVisibleEnemy();

	CBaseEntity *pGoal = NULL;
	if ( m_hEnemy != 0 )
		pGoal = m_hEnemy;
	else
		pGoal = UTIL_FindEntityByClassname( NULL, "info_player_deathmatch" );

	if ( pGoal == NULL )
		return;

	Vector vecDelta = pGoal->pev->origin - pev->origin;
	vecDelta.z = 0;
	float flLen = vecDelta.Length();

	if ( flLen > 1.0f )
	{
		vecDelta = vecDelta * ( 1.0f / flLen );
		pev->velocity.x = vecDelta.x * SGTC_REPLICATEUR_SPEED;
		pev->velocity.y = vecDelta.y * SGTC_REPLICATEUR_SPEED;
		if ( ( pev->flags & FL_ONGROUND ) && m_hEnemy != 0 )
			pev->velocity.z = 180.0f; // hop at prey, headcrab-style
	}

	// Melee bite once in range.
	if ( m_hEnemy != 0 && flLen < 72.0f )
	{
		m_hEnemy->TakeDamage( pev, pev, gSkillData.headcrabDmgBite, DMG_SLASH );
		// ref ReplicateurThink @0x161be4..0x162115: no EMIT_SOUND on bite.
		pev->nextthink = gpGlobals->time + 0.5; // bite recovery
	}
}

// =============================================================
// CSGTCImplanted — monster_implanted (CZombie alias)
// Zombie bruiser in a commander skin (0x1b7ffe).
// =============================================================

LINK_ENTITY_TO_CLASS( monster_implanted, CSGTCImplanted );

void CSGTCImplanted::Precache( void )
{
	CZombie::Precache();
	PRECACHE_MODEL( "models/player/commander/commander.mdl" );
}

void CSGTCImplanted::Spawn( void )
{
	Precache();

	SET_MODEL( ENT( pev ), "models/player/commander/commander.mdl" );
	UTIL_SetSize( pev, VEC_HUMAN_HULL_MIN, VEC_HUMAN_HULL_MAX );

	pev->solid = SOLID_SLIDEBOX;
	pev->movetype = MOVETYPE_STEP;
	m_bloodColor = BLOOD_COLOR_RED;
	pev->health = 80.0f;	// ref CZombie::Spawn @0x1b8106 rodata 80 (NOT gSkillData.zombieHealth)
	pev->view_ofs = VEC_VIEW;
	m_flFieldOfView = 0.5f;
	m_MonsterState = MONSTERSTATE_NONE;
	m_afCapability = bits_CAP_DOORS_GROUP;

	MonsterInit();
}

// =============================================================
// CSGTCSkull — monster_skull (CRat, models/crane_rose.mdl)
// =============================================================

TYPEDESCRIPTION CSGTCSkull::m_SaveData[] =
{
	DEFINE_FIELD( CSGTCSkull, m_flNextSound, FIELD_TIME ),
	DEFINE_FIELD( CSGTCSkull, m_pSkullOwner, FIELD_CLASSPTR ),
	DEFINE_FIELD( CSGTCSkull, m_iSkullAxis, FIELD_INTEGER ),
	DEFINE_FIELD( CSGTCSkull, m_iSkullPlane, FIELD_INTEGER ),
	DEFINE_FIELD( CSGTCSkull, m_iSkullLane, FIELD_INTEGER ),
	DEFINE_FIELD( CSGTCSkull, m_iSkullMode, FIELD_INTEGER ),
	DEFINE_FIELD( CSGTCSkull, m_flSkullBeamGate, FIELD_TIME ),
	DEFINE_FIELD( CSGTCSkull, m_flSkullThinkGate, FIELD_TIME ),
	DEFINE_FIELD( CSGTCSkull, m_bSkullFrateRamp, FIELD_CHARACTER ),
};
IMPLEMENT_SAVERESTORE( CSGTCSkull, CRat );
LINK_ENTITY_TO_CLASS( monster_skull, CSGTCSkull );

void CSGTCSkull::Precache( void )
{
	PRECACHE_MODEL( "models/crane_rose.mdl" );
	PRECACHE_SOUND( "misc/skull_attack1.wav" );
	PRECACHE_SOUND( "misc/skull_attack2.wav" );
	PRECACHE_SOUND( "misc/skull_idle1.wav" );
	PRECACHE_SOUND( "misc/skull_idle2.wav" );
	PRECACHE_SOUND( "misc/skull_idle3.wav" );
	g_iSkullNargasSpr = PRECACHE_MODEL( "sprites/nargas.spr" );
}

void CSGTCSkull::Spawn( void )
{
	Precache();

	SET_MODEL( ENT( pev ), "models/crane_rose.mdl" );
	UTIL_SetSize( pev, Vector( -8, -8, 0 ), Vector( 8, 8, 16 ) );
	UTIL_SetOrigin( pev, pev->origin );

	pev->solid = SOLID_SLIDEBOX;
	pev->movetype = MOVETYPE_FLY;	// ref CRat::Think sets movetype 8
	m_bloodColor = BLOOD_COLOR_RED;
	pev->health = 50.0f;	// ref CRat::Spawn @0x14b33c rodata 50
	pev->view_ofs = Vector( 0, 0, 6 );
	m_flFieldOfView = 0.5f;
	m_MonsterState = MONSTERSTATE_NONE;
	pev->takedamage = DAMAGE_AIM;

	m_flNextSound = gpGlobals->time;
	m_flSkullBeamGate = 0.0f;
	m_flSkullThinkGate = 0.0f;
	m_bSkullFrateRamp = 0;
	m_pSkullOwner = NULL;
	m_iSkullAxis = m_iSkullPlane = m_iSkullLane = 0;
	m_iSkullMode = 3;

	SetTouch( NULL );	// ref CRat — no Touch; damage via Think @0x14b50a only
	SetThink( &CSGTCSkull::SkullThink );
	pev->nextthink = gpGlobals->time + 0.1f;
}

void CSGTCSkull::Killed( entvars_t *pevAttacker, int iGib )
{
	// No FL_MONSTER/MonsterInit: CBaseMonster::Killed would leave it chasing.
	SetTouch( NULL );
	SetThink( NULL );
	SGTC_KilledRemove( this );
}

void CSGTCSkull::SkullThink( void )	// ref CRat::Think @0x14b50a..0x14c91b (monster_skull; no CSGTCSkull::Think)
{
	pev->movetype = MOVETYPE_FLY;

	if( !m_iSkullAxis )
		m_iSkullAxis = RANDOM_LONG( 1, 2 );
	if( !m_iSkullPlane )
		m_iSkullPlane = RANDOM_LONG( 1, 2 );
	if( !m_iSkullLane )
		m_iSkullLane = RANDOM_LONG( 1, 2 );
	m_iSkullMode = 3;
	SGTC_SkullNudgePeaks( pev, m_iSkullAxis, m_iSkullPlane, m_iSkullLane );

	CBaseEntity *pChase = NULL;
	CBaseEntity *pScan = NULL;
	int iBestDist = kSkullAcquireDist;
	BOOL bSkullClusterBurst = FALSE;

	if( m_flSkullBeamGate > gpGlobals->time )
		goto SkullThinkDone;

	SGTC_SkullBeamPoints( this );
	m_flSkullBeamGate = gpGlobals->time + kSkullBeamInterval;

	SGTC_SkullRampFramerate( pev, m_bSkullFrateRamp );
	// NOTE: ref does NOT write +0x3f4 here — that field is the attack-sound
	// gate armed ONLY by the dist<=19 arm block below (was time+0.01, mis-mapped).

	pScan = NULL;
	while( ( pScan = UTIL_FindEntityByClassname( pScan, "player" ) ) != NULL )
	{
		if( !pScan->IsAlive() )
			continue;

		if( !pScan->IsPlayer() )
			continue;
		CBasePlayer *pl = (CBasePlayer *)pScan;
		if( pl->IsObserver() )
			continue;
		if( !FVisible( pScan ) )
			continue;

		const int iDist = SGTC_Distance3D( pev->origin, pScan->pev->origin );
		if( iDist < iBestDist )
		{
			pChase = pScan;
			iBestDist = iDist;
		}

		if( iDist <= 19 && (CBaseEntity *)m_hEnemy != pScan )
		{
			m_hEnemy = pScan;
			// ref @0x14bcd8-0x14bde0 arm block: TakeDamage(30, DMG_PARALYZE=0x8000),
			// rendermode=5, renderamt=100; +0x3f4 = time+0.5 (attack gate),
			// +0x3f0 = time+0.01 (next-sound).
			pScan->TakeDamage( pev, pev, 30.0f, 0x8000 );
			pScan->pev->rendermode = 5;
			pScan->pev->renderamt = 100.0f;
			m_flSkullThinkGate = gpGlobals->time + 0.5f;
			m_flNextSound = gpGlobals->time + 0.01f;
		}

		if( m_pSkullOwner != NULL && pScan == m_pSkullOwner )
		{
			pChase = pScan;
			iBestDist = 0;
		}
	}

	if( m_pSkullOwner != NULL && !m_pSkullOwner->IsAlive() )
		m_pSkullOwner = NULL;

	if( m_hEnemy != 0 )
		UTIL_SetSize( pev, Vector( -kSkullNudge, -kSkullNudge, -kSkullNudge ),
			Vector( kSkullNudge, kSkullNudge, kSkullNudge ) );

	if( pChase == NULL && m_hEnemy != 0 )
	{
		if( SGTC_Distance3D( pev->origin, m_hEnemy->pev->origin ) <= 19 )
		{
			pev->velocity.x = (float)( RANDOM_LONG( 0, 0xdc ) - 0x6e );
			pev->velocity.y = (float)( RANDOM_LONG( 0, 0xdc ) - 0x6e );
			pev->velocity.z = (float)( RANDOM_LONG( 0, 0xdc ) - 0x6e );
		}
		else
			pChase = m_hEnemy;
	}

	if( pChase == NULL )
	{
		if( m_hEnemy == 0 )
		{
			if( m_pSkullOwner != NULL && !m_pSkullOwner->IsAlive() )
			{
				UTIL_Remove( this );
				return;
			}
		}
		else if( !m_hEnemy->IsAlive() )
		{
			UTIL_Remove( this );
			return;
		}
	}
	else
	{
		if( !pChase->IsAlive() )
		{
			UTIL_Remove( this );
			return;
		}
		SGTC_SkullChaseVelocity( pev, pChase );
	}

	pScan = NULL;
	while( ( pScan = UTIL_FindEntityByClassname( pScan, "monster_skull" ) ) != NULL )
	{
		if( pScan == this )
			continue;
		if( SGTC_Distance3D( pev->origin, pScan->pev->origin ) <= 0x40 )
			bSkullClusterBurst = TRUE;
	}

	if( bSkullClusterBurst && pChase == NULL )
	{
		pev->velocity.x = (float)( RANDOM_LONG( 0, 0xc8 ) - 0x64 );
		pev->velocity.y = (float)( RANDOM_LONG( 0, 0xc8 ) - 0x64 );
		pev->velocity.z = (float)( RANDOM_LONG( 0, 0xc8 ) - 0x64 );
	}

	if( m_flNextSound > gpGlobals->time )
		goto SkullThinkDone;

	// ref @0x14c670: armed attack gate (+0x3f4) → attack sound, else idle RL(1,3).
	if( m_flSkullThinkGate > gpGlobals->time )
	{
		// ref @0x14c833-0x14c8c8: RL(0,1)>0 ? attack1 : attack2,
		// EMIT_SOUND CHAN_BODY(4) vol 1.0 attn 0.8; +0x3f0 = time + 2.5.
		if( RANDOM_LONG( 0, 1 ) > 0 )
			EMIT_SOUND( ENT( pev ), CHAN_BODY, "misc/skull_attack1.wav", 1.0f, ATTN_NORM );
		else
			EMIT_SOUND( ENT( pev ), CHAN_BODY, "misc/skull_attack2.wav", 1.0f, ATTN_NORM );
		m_flNextSound = gpGlobals->time + 2.5f;	// ref rodata @B-0x2c458
		goto SkullThinkDone;
	}

	switch( RANDOM_LONG( 1, 3 ) )
	{
	case 1:
		// ref CRat::Think @0x14c6ff: EMIT_SOUND CHAN_BODY, vol 1.0, attn 0.8.
		EMIT_SOUND( ENT( pev ), CHAN_BODY, "misc/skull_idle1.wav", 1.0f, ATTN_NORM );
		break;
	case 2:
		EMIT_SOUND( ENT( pev ), CHAN_BODY, "misc/skull_idle2.wav", 1.0f, ATTN_NORM );
		break;
	default:
		EMIT_SOUND( ENT( pev ), CHAN_BODY, "misc/skull_idle3.wav", 1.0f, ATTN_NORM );
		break;
	}
	// ref @0x14c7cd-0x14c819 common tail: +0x3f0 = time + 2.0 + RL(1,50)*0.1 (ALL cases).
	m_flNextSound = gpGlobals->time + 2.0f + (float)RANDOM_LONG( 1, 50 ) * 0.1f;

SkullThinkDone:
	pev->nextthink = gpGlobals->time + kSkullThinkInterval;
}

// monster_mine -> CTripmineGrenade (tripmine.cpp); ref CTripmineGrenade @0x19ed18.

// =============================================================
// CSGTCTac — monster_tac (models/w_tacb.mdl deployed)
// Static sentry with tac sound set + anti-invis sweep.
// =============================================================

TYPEDESCRIPTION CSGTCTac::m_SaveData[] =
{
	DEFINE_FIELD( CSGTCTac, m_flNextAction, FIELD_TIME ),
	DEFINE_FIELD( CSGTCTac, m_flNextSweep, FIELD_TIME ),
	DEFINE_FIELD( CSGTCTac, m_hLockTarget, FIELD_EHANDLE ),
	DEFINE_FIELD( CSGTCTac, m_vecLockDelta, FIELD_VECTOR ),
	DEFINE_FIELD( CSGTCTac, m_bLockDeltaSet, FIELD_BOOLEAN ),
	DEFINE_FIELD( CSGTCTac, m_bTacArmed, FIELD_BOOLEAN ),
	DEFINE_FIELD( CSGTCTac, m_iTacMode, FIELD_INTEGER ),
	DEFINE_FIELD( CSGTCTac, m_iTacAnimStep, FIELD_INTEGER ),
	DEFINE_FIELD( CSGTCTac, m_hDeployer, FIELD_EHANDLE ),
	DEFINE_FIELD( CSGTCTac, m_iGammaWaveSpr, FIELD_INTEGER ),
	DEFINE_FIELD( CSGTCTac, m_iGammaWaveSpr2, FIELD_INTEGER ),
	DEFINE_FIELD( CSGTCTac, m_usTacEvent, FIELD_SHORT ),
	DEFINE_FIELD( CSGTCTac, m_flNextPickup, FIELD_TIME ),
	DEFINE_FIELD( CSGTCTac, m_flLastDamage, FIELD_TIME ),
	DEFINE_FIELD( CSGTCTac, m_flNextSmoke, FIELD_TIME ),
	DEFINE_FIELD( CSGTCTac, m_flNextHurt, FIELD_TIME ),
	DEFINE_FIELD( CSGTCTac, m_flNextHint, FIELD_TIME ),
	DEFINE_FIELD( CSGTCTac, m_flBubbleNext, FIELD_TIME ),
	DEFINE_FIELD( CSGTCTac, m_hBubbleViewer, FIELD_EHANDLE ),
	DEFINE_FIELD( CSGTCTac, m_iBubbleShells, FIELD_INTEGER ),
	DEFINE_FIELD( CSGTCTac, m_bFreeDeploy, FIELD_BOOLEAN ),
	DEFINE_FIELD( CSGTCTac, m_bGone, FIELD_BOOLEAN ),
};
IMPLEMENT_SAVERESTORE( CSGTCTac, CBaseMonster );
LINK_ENTITY_TO_CLASS( monster_tac, CSGTCTac );

void CSGTCTac::Precache( void )
{
	PRECACHE_MODEL( "models/w_tac.mdl" );
	PRECACHE_MODEL( "models/w_tacb.mdl" );
	PRECACHE_MODEL( "models/w_tacs.mdl" );
	PRECACHE_MODEL( "models/w_tacp.mdl" );
	PRECACHE_SOUND( "weapons/tac.wav" );
	PRECACHE_SOUND( "weapons/tacs.wav" );
	PRECACHE_SOUND( "weapons/tacp.wav" );
	PRECACHE_SOUND( "weapons/tacact.wav" );
	PRECACHE_SOUND( "weapons/place.wav" );
	PRECACHE_SOUND( "weapons/invis.wav" );
	PRECACHE_SOUND( "weapons/explode3.wav" );
	m_iGammaWaveSpr = m_iGammaWaveSpr2 = PRECACHE_MODEL( "sprites/gammawave.spr" );
	m_usTacEvent = PRECACHE_EVENT( 1, "events/tac.sc" );
}

void CSGTCTac::Spawn( void )
{
	Precache();

	SET_MODEL( ENT( pev ), "models/w_tacb.mdl" );
	SGTC_DiagSpawn( "monster_tac", NULL, pev->origin, pev->scale, pev->body, pev->skin, pev->sequence );
	UTIL_SetSize( pev, Vector( -16, -16, 0 ), Vector( 16, 16, 48 ) );
	UTIL_SetOrigin( pev, pev->origin );

	pev->movetype = MOVETYPE_FLY;	// ref @0x18c006 = 5
	pev->solid = SOLID_BBOX;	// ref @0x18c016 = 2
	pev->takedamage = DAMAGE_AIM;
	// The tacgun overwrites health/max_health from the per-type table after
	// Create; 60 is the mode-0 value so a map-placed tac is still sane.
	pev->health = 60.0f;
	pev->max_health = 60.0f;
	pev->view_ofs = Vector( 0, 0, 32 );
	m_flFieldOfView = -1.0f;
	m_MonsterState = MONSTERSTATE_NONE;
	m_bloodColor = DONT_BLEED;

	pev->sequence = 0;
	pev->frame = 0;
	pev->framerate = 1.0f;
	pev->fuser1 = gpGlobals->time + 300.0f;	// ref lifetime gate @0x18c4ff (+0x160)

	m_hLockTarget = NULL;
	m_bLockDeltaSet = FALSE;
	m_bTacArmed = FALSE;
	m_iTacMode = 0;
	m_iTacAnimStep = 0;
	m_hDeployer = NULL;
	m_flNextAction = 0;
	m_flNextSweep = 0;

	m_flNextPickup = gpGlobals->time + 1.0f;
	m_flLastDamage = 0.0f;
	m_flNextSmoke = 0.0f;
	m_flNextHurt = 0.0f;
	m_flNextHint = 0.0f;
	m_flBubbleNext = 0.0f;
	m_hBubbleViewer = NULL;
	m_iBubbleShells = 0;
	m_bFreeDeploy = FALSE;
	m_bGone = FALSE;

	if( pev->owner != NULL )
		m_hDeployer = CBaseEntity::Instance( pev->owner );

	m_Activity = (Activity)0xc5;
	m_IdealActivity = ACT_RESET;

	// ref CTacgun deploy @0x18c07d: place.wav CHAN_VOICE(2).
	EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "weapons/place.wav", 1.0, ATTN_NORM, 0, PITCH_NORM );

	SetThink( &CSGTCTac::TacThink );
	pev->nextthink = gpGlobals->time + 0.1f;
}

int CSGTCTac::TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType )
{
	// Gameplay change over the reference build (which zeroed every hit that was
	// not blast/club/energy/burn and removed the tac on the first bullet): the
	// tac now has real health, degrades visibly and explodes when it dies.
	if( m_bGone || pev->takedamage == DAMAGE_NO )
		return 0;

	if( bitsDamageType & DMG_RADIATION )
		return 0;

	if( flDamage <= 0.0f )
		return 0;

	// FRIENDLY FIRE follows the server cvar the same way player damage does.
	if( pevAttacker != NULL && pev->team != 0 && pevAttacker->team == pev->team &&
	    CVAR_GET_FLOAT( "mp_friendlyfire" ) == 0.0f )
		return 0;

	m_flLastDamage = gpGlobals->time;
	pev->health -= flDamage;

	if( pev->health <= 0.0f )
	{
		pev->health = 0.0f;
		Explode( pevAttacker );
		return 1;
	}

	if( gpGlobals->time >= m_flNextSmoke )
		EmitDamageSmoke();

	if( gpGlobals->time >= m_flNextHurt )
	{
		EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "weapons/tac.wav", 0.6f, ATTN_NORM,
			0, 96 + RANDOM_LONG( 0, 12 ) );
		m_flNextHurt = gpGlobals->time + 0.25f;
	}

	return 1;
}

void CSGTCTac::Killed( entvars_t *pevAttacker, int iGib )
{
	Explode( pevAttacker );
}

int CSGTCTac::ObjectCaps( void )
{
	return ( CBaseMonster::ObjectCaps() & ~FCAP_ACROSS_TRANSITION ) | FCAP_IMPULSE_USE;
}

CBasePlayer *CSGTCTac::Deployer( void )
{
	CBaseEntity *pOwner = ( m_hDeployer != NULL ) ? (CBaseEntity *)m_hDeployer : CBaseEntity::Instance( pev->owner );
	if( pOwner == NULL || !pOwner->IsPlayer() )
		return NULL;
	return (CBasePlayer *)pOwner;
}

void CSGTCTac::EmitDamageSmoke( void )
{
	float flRatio = ( pev->max_health > 0.0f ) ? ( pev->health / pev->max_health ) : 0.0f;
	if( flRatio < 0.0f ) flRatio = 0.0f;
	if( flRatio > 1.0f ) flRatio = 1.0f;

	const int iScale = 10 + (int)( ( 1.0f - flRatio ) * 18.0f );	// 1.0..2.8 x
	const int iFramerate = 10 + (int)( ( 1.0f - flRatio ) * 8.0f );
	Vector vecPuff = pev->origin + Vector( 0, 0, 32 );

	if( UTIL_PointContents( pev->origin ) == CONTENTS_WATER )
	{
		UTIL_Bubbles( vecPuff + Vector( 24, 24, 24 ), vecPuff - Vector( 24, 24, 24 ), 4 );
	}
	else
	{
		MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, vecPuff );
			WRITE_BYTE( TE_SMOKE );
			WRITE_COORD( vecPuff.x );
			WRITE_COORD( vecPuff.y );
			WRITE_COORD( vecPuff.z );
			WRITE_SHORT( g_sModelIndexSmoke );
			WRITE_BYTE( (BYTE)iScale );
			WRITE_BYTE( (BYTE)iFramerate );
		MESSAGE_END();
	}

	// Sparks and thicker smoke as health drops.
	if( flRatio < 0.66f && RANDOM_LONG( 0, 2 ) == 0 )
		{ Vector vDbg = vecPuff ; SGTC_DiagSparks( "monster_puff", vDbg ); UTIL_Sparks( vDbg ); }

	if( flRatio < 0.33f )
	{
		MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, pev->origin );
			WRITE_BYTE( TE_SMOKE );
			WRITE_COORD( pev->origin.x );
			WRITE_COORD( pev->origin.y );
			WRITE_COORD( pev->origin.z + 16.0f );
			WRITE_SHORT( g_sModelIndexSmoke );
			WRITE_BYTE( 28 );
			WRITE_BYTE( 10 );
		MESSAGE_END();
	}

	m_flNextSmoke = gpGlobals->time + 1.1f - ( 1.0f - flRatio ) * 0.8f;	// 1.1s .. 0.3s
}

void CSGTCTac::Explode( entvars_t *pevAttacker )
{
	if( m_bGone )
		return;
	m_bGone = TRUE;

	SetThink( NULL );
	SetTouch( NULL );
	pev->nextthink = 0;
	pev->takedamage = DAMAGE_NO;
	pev->solid = SOLID_NOT;

	CBasePlayer *pOwner = Deployer();
	entvars_t *pevCredit = ( pevAttacker != NULL ) ? pevAttacker : ( pOwner != NULL ? pOwner->pev : NULL );
	Vector vecOrigin = pev->origin;

	MESSAGE_BEGIN( MSG_PAS, SVC_TEMPENTITY, vecOrigin );
		WRITE_BYTE( TE_EXPLOSION );
		WRITE_COORD( vecOrigin.x );
		WRITE_COORD( vecOrigin.y );
		WRITE_COORD( vecOrigin.z );
		WRITE_SHORT( g_sModelIndexFireball );
		WRITE_BYTE( 21 );	// scale * 10
		WRITE_BYTE( 15 );	// framerate
		WRITE_BYTE( TE_EXPLFLAG_NONE );
	MESSAGE_END();

	EMIT_SOUND( ENT( pev ), CHAN_VOICE, "weapons/explode3.wav", 1.0f, ATTN_NORM );

	// Small, balanced splash credited to whoever destroyed it. Our own
	// takedamage is already off, so the blast cannot chain into this entity.
	::RadiusDamage( vecOrigin, pev, pevCredit, 50.0f, 160.0f, CLASS_NONE, DMG_BLAST );

	MESSAGE_BEGIN( MSG_PAS, SVC_TEMPENTITY, vecOrigin );
		WRITE_BYTE( TE_SMOKE );
		WRITE_COORD( vecOrigin.x );
		WRITE_COORD( vecOrigin.y );
		WRITE_COORD( vecOrigin.z + 16.0f );
		WRITE_SHORT( g_sModelIndexSmoke );
		WRITE_BYTE( 25 );
		WRITE_BYTE( 10 );
	MESSAGE_END();

	UTIL_Remove( this );
}

// Max ammo the Tacgun item declares for its pool, straight from the weapon
// registry that W_Precache fills (CTacgun::GetItemInfo publishes
// SGTC_TAC_MAX_CARRY there as iMaxAmmo1). Falls back to the compiled constant
// if the registry entry is missing.
static int SGTC_TacMaxCarry( void )
{
	for( int i = 0; i < MAX_WEAPONS; i++ )
	{
		const char *pszAmmo = CBasePlayerItem::ItemInfoArray[i].pszAmmo1;
		if( pszAmmo != NULL && !strcmp( pszAmmo, SGTC_AMMO_TAC ) )
			return CBasePlayerItem::ItemInfoArray[i].iMaxAmmo1;
	}
	return SGTC_TAC_MAX_CARRY;
}

void CSGTCTac::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if( useType != USE_SET || m_bGone )
		return;

	CBasePlayer *pPlayer = ( pActivator != NULL && pActivator->IsPlayer() ) ? (CBasePlayer *)pActivator : NULL;
	if( pPlayer == NULL || !pPlayer->IsAlive() )
		return;

	if( Deployer() != pPlayer )
	{
		// Owner-only: teammates cannot retrieve (or steal) someone else's tac.
		ClientPrint( pPlayer->pev, HUD_PRINTCENTER, "This TAC belongs to another player." );
		return;
	}

	if( m_bFreeDeploy )
	{
		ClientPrint( pPlayer->pev, HUD_PRINTCENTER, "Free-mode TACs cannot be retrieved." );
		return;
	}

	if( gpGlobals->time < m_flNextPickup )
		return;

	if( gpGlobals->time < m_flLastDamage + 1.0f )
	{
		ClientPrint( pPlayer->pev, HUD_PRINTCENTER, "Cannot retrieve the TAC while it is under fire." );
		return;
	}

	// Anti-dupe cap: the Tacgun's own declared ammo max (CTacgun::GetItemInfo
	// publishes SGTC_TAC_MAX_CARRY as iMaxAmmo1), read back from the weapon
	// registry so it cannot drift if the weapon's maximum changes.
	int iMaxCarry = SGTC_TacMaxCarry();
	if( iMaxCarry <= 0 )
		return;

	int iAmmoType = CBasePlayer::GetAmmoIndex( "Tacgun" );
	if( iAmmoType < 0 || iAmmoType >= MAX_AMMO_SLOTS )
		return;

	if( pPlayer->m_rgAmmo[iAmmoType] >= iMaxCarry )
	{
		ClientPrint( pPlayer->pev, HUD_PRINTCENTER, "TAC ammo is full." );
		return;
	}

	// If the weapon item was destroyed when the last tac was placed, re-grant
	// it without the item grant path adding its own ammo: GiveNamedItem never
	// touches the ammo pool, but be explicit so the count is exactly +1.
	const int iAmmoBefore = pPlayer->m_rgAmmo[iAmmoType];
	if( !pPlayer->HasNamedPlayerItem( "weapon_tacgun" ) )
	{
		pPlayer->GiveNamedItem( "weapon_tacgun" );
		if( !pPlayer->HasNamedPlayerItem( "weapon_tacgun" ) )
			return;	// could not carry the weapon: leave the tac where it is
		pPlayer->m_rgAmmo[iAmmoType] = iAmmoBefore + 1;
		pPlayer->TabulateAmmo();
	}
	else if( pPlayer->GiveAmmo( 1, "Tacgun", iMaxCarry ) < 0 )
	{
		return;
	}

	if( m_iTacMode < 0 || m_iTacMode > 2 )
		m_iTacMode = 0;

	// The retrieved tac keeps its damage for the next placement of this type.
	pPlayer->m_iSgtcTacCarryHealth[m_iTacMode] = (int)pev->health;
	pPlayer->m_flSgtcTacPlaceBlock = gpGlobals->time + 1.0f;
	pPlayer->pev->weapons |= ( 1 << SGTC_WPN_TAC );

	m_bGone = TRUE;
	SetThink( NULL );
	SetTouch( NULL );
	pev->nextthink = 0;
	pev->takedamage = DAMAGE_NO;
	pev->solid = SOLID_NOT;

	EMIT_SOUND( ENT( pev ), CHAN_VOICE, "weapons/tacact.wav", 0.8f, ATTN_NORM );
	ClientPrint( pPlayer->pev, HUD_PRINTCENTER, "TAC retrieved." );

	UTIL_Remove( this );
}

// ---------------------------------------------------------------------------
// Owner-only range bubble. SGTC_Distance3D is Manhattan (|dx|+|dy|+|dz|), so a
// radius of mp_tacdistance reaches an octahedron, not a sphere: a sphere of the
// same radius over-promises by up to ~73% on a 3D diagonal, where the true reach
// is R/sqrt(3). The bubble therefore draws the exact volume — six vertices at
// origin ± R on each axis and the twelve edges between them — with additive
// beams sent MSG_ONE to the placer alone. One shell on placement plus a second
// staggered shell reads as a fade-in; beam life fades the whole thing out.
// ---------------------------------------------------------------------------

static void SGTC_TacBubbleSegment( CBasePlayer *pViewer, const Vector &vecA, const Vector &vecB,
	int iSprite, int iR, int iG, int iB, int iBrightness, int iLifeTenths )
{
	if( pViewer == NULL || iSprite == 0 )
		return;

	MESSAGE_BEGIN( MSG_ONE, SVC_TEMPENTITY, NULL, pViewer->edict() );
		WRITE_BYTE( TE_BEAMPOINTS );
		WRITE_COORD( vecA.x );
		WRITE_COORD( vecA.y );
		WRITE_COORD( vecA.z );
		WRITE_COORD( vecB.x );
		WRITE_COORD( vecB.y );
		WRITE_COORD( vecB.z );
		WRITE_SHORT( (short)iSprite );
		WRITE_BYTE( 0 );		// startframe
		WRITE_BYTE( 0 );		// framerate
		WRITE_BYTE( (BYTE)iLifeTenths );
		WRITE_BYTE( 2 );		// width
		WRITE_BYTE( 0 );		// noise
		WRITE_BYTE( (BYTE)iR );
		WRITE_BYTE( (BYTE)iG );
		WRITE_BYTE( (BYTE)iB );
		WRITE_BYTE( (BYTE)iBrightness );
		WRITE_BYTE( 0 );		// speed
	MESSAGE_END();
}

void CSGTCTac::SendBubbleShell( CBasePlayer *pViewer, int iBrightness, int iLifeTenths )
{
	if( pViewer == NULL || m_iGammaWaveSpr2 == 0 )
		return;

	int iR = 255, iG = 80, iB = 80;
	switch( m_iTacMode )
	{
	case 1: iR = 255; iG = 225; iB = 80; break;
	case 2: iR = 90; iG = 255; iB = 120; break;
	default: break;
	}

	const float flRadius = (float)SGTC_TacSeparation();
	if( flRadius <= 0.0f )
		return;

	// Centre on the entity origin: that is the point SGTC_Distance3D measures
	// from, so it is also the point the drawn volume must be centred on.
	const Vector vecCenter = pev->origin;

	// Octahedron matching |dx|+|dy|+|dz| <= R: six vertices at ±R on each axis,
	// joined by the twelve edges between every non-opposite vertex pair
	// (6 choose 2 = 15 pairs, minus the 3 opposite pairs).
	Vector vecVert[6];
	vecVert[0] = vecCenter + Vector( flRadius, 0, 0 );
	vecVert[1] = vecCenter + Vector( -flRadius, 0, 0 );
	vecVert[2] = vecCenter + Vector( 0, flRadius, 0 );
	vecVert[3] = vecCenter + Vector( 0, -flRadius, 0 );
	vecVert[4] = vecCenter + Vector( 0, 0, flRadius );
	vecVert[5] = vecCenter + Vector( 0, 0, -flRadius );

	for( int iFrom = 0; iFrom < 6; iFrom++ )
	{
		for( int iTo = iFrom + 1; iTo < 6; iTo++ )
		{
			if( ( iFrom >> 1 ) == ( iTo >> 1 ) )	// 0-1, 2-3, 4-5: opposite
				continue;
			SGTC_TacBubbleSegment( pViewer, vecVert[iFrom], vecVert[iTo], m_iGammaWaveSpr2, iR, iG, iB,
				iBrightness, iLifeTenths );
		}
	}
}

void CSGTCTac::ShowRangeBubble( CBasePlayer *pViewer, BOOL bStaged )
{
	if( pViewer == NULL || !pViewer->IsPlayer() || !pViewer->IsAlive() )
		return;

	if( sgtc_tac_bubble.value == 0.0f )
		return;

	SendBubbleShell( pViewer, bStaged ? 24 : 44, bStaged ? 30 : 15 );

	if( bStaged )
	{
		m_hBubbleViewer = pViewer;
		m_iBubbleShells = 1;
		m_flBubbleNext = gpGlobals->time + 0.45f;
		// Let the staged pair play out before the look-at refresh can stack on it.
		m_flNextHint = gpGlobals->time + 1.5f;
	}
}

// ref CTac::Think armed TE trains @0x18d598 (mode-1 cylinder), @0x18d815 (beam), @0x18db65 (dlight).
static const float kTacBeamOriginZDelta = 3.0f;	// ref rodata @0x1dca20 (fld -0x27fdc(%ebx) @0x18d842)
static const float kTacCylinderLowZ = 15.0f;		// ref -0x27fec(%ebx)
static const float kTacCylinderHighAdd = 250.0f;	// ref -0x27fe8(%ebx) added after +kTacCylinderLowZ

static void SGTC_TacSendBeamCylinder( CSGTCTac *pTac, const Vector &vecEnd )
{
	if( !pTac->m_iGammaWaveSpr2 )
		return;
	MESSAGE_BEGIN( MSG_PAS, SVC_TEMPENTITY, &vecEnd.x );
		WRITE_BYTE( TE_BEAMCYLINDER );
		WRITE_COORD( vecEnd.x );
		WRITE_COORD( vecEnd.y );
		WRITE_COORD( vecEnd.z - kTacCylinderLowZ );
		WRITE_COORD( vecEnd.x );
		WRITE_COORD( vecEnd.y );
		WRITE_COORD( vecEnd.z + kTacCylinderLowZ + kTacCylinderHighAdd );
		WRITE_SHORT( pTac->m_iGammaWaveSpr2 );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 1 );
		WRITE_BYTE( 30 );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 255 );
		WRITE_BYTE( 255 );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 200 );
		WRITE_BYTE( 0 );
	MESSAGE_END();
}

static void SGTC_TacWriteBeamTailBytes( int iMode )
{
	switch( iMode )
	{
	case 0:
		WRITE_BYTE( 0 );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 2 );
		WRITE_BYTE( 6 );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 255 );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 0 );
		break;
	case 1:
		WRITE_BYTE( 30 );
		WRITE_CHAR( RANDOM_LONG( 3, 6 ) );
		WRITE_BYTE( 255 );
		WRITE_BYTE( 255 );
		WRITE_BYTE( 0 );
		break;
	case 2:
		WRITE_BYTE( 15 );
		WRITE_BYTE( 9 );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 255 );
		WRITE_BYTE( 0 );
		break;
	default:
		WRITE_BYTE( 0 );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 2 );
		break;
	}
	WRITE_BYTE( 200 );
	WRITE_BYTE( 0 );
}

static void SGTC_TacSendBeamPoints( CSGTCTac *pTac, const Vector &vecBeamEnd )
{
	if( !pTac->m_iGammaWaveSpr )
		return;

	pTac->pev->origin.z -= kTacBeamOriginZDelta;

	MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY, NULL );
		WRITE_BYTE( TE_BEAMPOINTS );
		WRITE_COORD( pTac->pev->origin.x );
		WRITE_COORD( pTac->pev->origin.y );
		WRITE_COORD( pTac->pev->origin.z );
		WRITE_COORD( vecBeamEnd.x );
		WRITE_COORD( vecBeamEnd.y );
		WRITE_COORD( vecBeamEnd.z );
		WRITE_SHORT( pTac->m_iGammaWaveSpr );
		SGTC_TacWriteBeamTailBytes( pTac->m_iTacMode );
	MESSAGE_END();
}

static void SGTC_TacSendDlight( CSGTCTac *pTac )
{
	MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, pTac->pev->origin );
		WRITE_BYTE( TE_DLIGHT );
		WRITE_COORD( pTac->pev->origin.x );
		WRITE_COORD( pTac->pev->origin.y );
		WRITE_COORD( pTac->pev->origin.z );
		WRITE_BYTE( 3 );
		switch( pTac->m_iTacMode )
		{
		case 0:
			// ref @0x18dc25: "weapons/tac.wav", CHAN_VOICE(2), vol 1.0, attn 0.8.
			EMIT_SOUND( ENT( pTac->pev ), CHAN_VOICE, "weapons/tac.wav", 1.0f, ATTN_NORM );
			WRITE_BYTE( 255 );
			WRITE_BYTE( 0 );
			WRITE_BYTE( 0 );
			WRITE_BYTE( 0 );
			WRITE_BYTE( 0 );
			WRITE_BYTE( 0 );
			break;
		case 1:
			// ref @0x18dcaf: "weapons/tacs.wav"
			EMIT_SOUND( ENT( pTac->pev ), CHAN_VOICE, "weapons/tacs.wav", 1.0f, ATTN_NORM );
			WRITE_BYTE( 255 );
			WRITE_BYTE( 255 );
			WRITE_BYTE( 0 );
			WRITE_BYTE( 0 );
			break;
		case 2:
			// ref @0x18dd3c: "weapons/tacp.wav"
			EMIT_SOUND( ENT( pTac->pev ), CHAN_VOICE, "weapons/tacp.wav", 1.0f, ATTN_NORM );
			WRITE_BYTE( 0 );
			WRITE_BYTE( 255 );
			WRITE_BYTE( 0 );
			WRITE_BYTE( 0 );
			break;
		default:
			// ref @0x18dd93+: no sound; colour 255/255/0.
			WRITE_BYTE( 255 );
			WRITE_BYTE( 255 );
			WRITE_BYTE( 0 );
			WRITE_BYTE( 0 );
			break;
		}
		WRITE_BYTE( 12 );
		WRITE_BYTE( 0 );
	MESSAGE_END();
}

static void SGTC_TacArmedStrikeFX( CSGTCTac *pTac, const Vector &vecTraceEnd, const Vector &vecBeamEnd, CBaseEntity *pHit )
{
	entvars_t *pevInflictor = ( pTac->m_hDeployer != NULL ) ? pTac->m_hDeployer->pev : pTac->pev;
	const int iClassIgnore = 0;
	const int iBits = 1024;

	// ref @0x18d4a1 / @0x18d4f1 — paired RadiusDamage bursts.
	pTac->CBaseMonster::RadiusDamage( vecTraceEnd, pevInflictor, pTac->pev, 23.0f, iClassIgnore, iBits );
	pTac->CBaseMonster::RadiusDamage( vecBeamEnd, pevInflictor, pTac->pev, 23.0f, iClassIgnore, iBits );

	if( pHit != NULL && pHit->IsAlive() )
	{
		if( pHit->IsPlayer() )
		{
			CBasePlayer *pPlayer = static_cast<CBasePlayer *>( pHit );
			pPlayer->m_bShieldTraceHit = TRUE;
			pPlayer->m_flShieldTraceTime = gpGlobals->time - 3.0f;
		}
		SGTC_TacSendBeamCylinder( pTac, vecTraceEnd );
	}

	SGTC_TacSendBeamPoints( pTac, vecBeamEnd );
	SGTC_TacSendDlight( pTac );
}
void CSGTCTac::TacThink( void )
{
	pev->nextthink = gpGlobals->time + 0.1f;

	// ref @0x18c285: latch the ground/ride entity via a long down-trace
	// (origin + Vector(0,0,-24)*300) when +0x408 is null.
	if( m_hLockTarget == NULL )
	{
		TraceResult tr;
		Vector vecEnd = pev->origin + Vector( 0, 0, -24.0f ) * 300.0f;
		UTIL_TraceLine( pev->origin, vecEnd, ignore_monsters, ENT( pev ), &tr );
		if( tr.flFraction < 1.0f )
		{
			if( pev->origin.z - tr.vecEndPos.z > 12.0f )
				pev->origin.z = tr.vecEndPos.z + 12.0f;
			m_hLockTarget = CBaseEntity::Instance( tr.pHit );
		}
	}
	else if( !m_bLockDeltaSet )
	{
		m_vecLockDelta = m_hLockTarget->pev->origin - pev->origin;
		m_bLockDeltaSet = TRUE;
	}

	// ref @0x18c437: ride moving lock targets (movetype 5).
	if( m_hLockTarget != NULL && m_bLockDeltaSet &&
		m_hLockTarget->pev->movetype == MOVETYPE_FLY )
	{
		pev->velocity = m_hLockTarget->pev->velocity;
	}

	// Follow the owner's team if they switch sides, so the tac does not keep
	// shooting the team it was originally deployed for.
	{
		CBasePlayer *pOwner = Deployer();
		if( pOwner != NULL && pOwner->IsAlive() )
		{
			int iTeam = pOwner->pev->team;
			if( !strncmp( pOwner->m_szTeamName, "Goa'uld", 7 ) )
				iTeam = SGTC_ENTTEAM_GOAULD;
			else if( !strncmp( pOwner->m_szTeamName, "Torri", 5 ) )
				iTeam = SGTC_ENTTEAM_TORRI;

			if( iTeam != 0 && pev->team != iTeam )
				pev->team = iTeam;
		}
	}

	// ref @0x18c4ff: pev->fuser1 lifetime expiry.
	if( pev->fuser1 > 0.0f && pev->fuser1 <= gpGlobals->time )
	{
		UTIL_Remove( this );
		return;
	}

	// ref @0x18c52a: +0x404 action gate — skip combat while pending.
	CBaseEntity *pTarget = NULL;
	if( m_flNextAction <= gpGlobals->time )
	{
		if( !m_bTacArmed )
		{
			// ref @0x18c560: density cull — too many players close by
			// destroys the tac before it can arm (svcrazy skips scan).
			if( sgtc_svcrazy.value == 0.0f )
			{
				int iFar = 0, iNear = 0;
				const int iSep = SGTC_TacSeparation();
				CBaseEntity *pScan = NULL;
				while( ( pScan = UTIL_FindEntityByClassname( pScan, "player" ) ) != NULL )
				{
					if( pScan == this )
						continue;
					const int iDist = SGTC_Distance3D( pev->origin, pScan->pev->origin );
					if( iDist < iSep )
						iFar++;
					if( iDist <= 9 )
						iNear++;
				}
				if( iFar > 2 || iNear > 1 )
				{
					UTIL_Remove( this );
					return;
				}
			}

			// ref @0x18c713: deploy sequence picks the tac mode AND model;
			// a single "tacact.wav" is emitted after (not per-case).
			switch( (int)pev->sequence )
			{
			case 30:
				m_iTacMode = 0;
				SET_MODEL( ENT( pev ), "models/w_tac.mdl" );
				break;
			case 31:
				m_iTacMode = 1;
				SET_MODEL( ENT( pev ), "models/w_tacs.mdl" );
				break;
			case 32:
				m_iTacMode = 2;
				SET_MODEL( ENT( pev ), "models/w_tacp.mdl" );
				break;
			default:
				break;
			}

			pev->movetype = MOVETYPE_FLY;
			pev->solid = SOLID_BBOX;
			// ref @0x18c82e: single post-deploy sound.
			EMIT_SOUND( ENT( pev ), CHAN_VOICE, "weapons/tacact.wav", 1.0f, ATTN_NORM );	// ref @0x18c838 chan 2
			m_bTacArmed = TRUE;
			m_flNextAction = gpGlobals->time + 1.0f;	// ref @0x18c854 (rodata 1.0)
			if( pev->owner != NULL )
				m_hDeployer = CBaseEntity::Instance( pev->owner );
			pev->owner = NULL;
		}
		else
		{
			// Armed combat — ref @0x18c8bc..0x18db47 (TE bytes simplified).
			m_flNextAction = gpGlobals->time + 0.3f;	// ref armed-entry cadence (dbl 0.3)

			// Decloak sweep: force nearby cloaked players visible again.
			if( m_flNextSweep <= gpGlobals->time )
			{
				CBaseEntity *pEnt = NULL;
				while( ( pEnt = UTIL_FindEntityInSphere( pEnt, pev->origin, 300.0f ) ) != NULL )
				{
					if( !pEnt->IsPlayer() || !pEnt->IsAlive() )
						continue;
					if( pev->team != 0 && pEnt->pev->team != 0 && pEnt->pev->team == pev->team )
						continue;
					if( pEnt->pev->renderamt < 255 )
					{
						pEnt->pev->renderamt = 255;
						pEnt->pev->renderfx = kRenderNormal;
						EMIT_SOUND( ENT( pev ), CHAN_VOICE, "weapons/invis.wav", 1.0f, ATTN_NORM );
					}
				}
				m_flNextSweep = gpGlobals->time + 1.0f;
			}

			// Nearest living enemy player inside tacdist.
			int iBest = 999999;
			CBaseEntity *pScan = NULL;
			while( ( pScan = UTIL_FindEntityByClassname( pScan, "player" ) ) != NULL )
			{
				if( !pScan->IsAlive() )
					continue;
				if( pev->team != 0 && pScan->pev->team != 0 && pScan->pev->team == pev->team )
					continue;
				const int iDist = SGTC_Distance3D( pev->origin, pScan->pev->origin );
				if( iDist > SGTC_TacSeparation() )
					continue;
				if( iDist < iBest )
				{
					iBest = iDist;
					pTarget = pScan;
				}
			}

			if( pTarget != NULL )
			{
				Vector vecAim = pTarget->pev->origin - pev->origin;
				Vector ang = UTIL_VecToAngles( vecAim );
				ang.x = -ang.x;
				pev->angles = ang;
				UTIL_MakeVectors( pev->angles );

				TraceResult trCombat;
				Vector vecSrc = pev->origin + gpGlobals->v_forward * 12.0f;
				Vector vecEnd = pTarget->pev->origin + pTarget->pev->view_ofs;
				UTIL_TraceLine( vecSrc, vecEnd, dont_ignore_monsters, ENT( pev ), &trCombat );
				if( trCombat.pHit != NULL )
				{
					TraceResult trBeam;
					UTIL_TraceLine( vecSrc, vecEnd, ignore_monsters, ENT( pev ), &trBeam );
					CBaseEntity *pHitEnt = CBaseEntity::Instance( trCombat.pHit );
					SGTC_TacArmedStrikeFX( this, trCombat.vecEndPos, trBeam.vecEndPos, pHitEnt );
				}
			}

			// ref @0x18ddcd: post-fire cadence — 1.0s, mode-2 spends 2.5s.
			m_flNextAction = gpGlobals->time + ( m_iTacMode == 2 ? 2.5f : 1.0f );
		}
	}

	// ref @0x18de13: mode-2 (w_tacp) framerate ramp — 4 beat burst cycle.
	if( m_iTacMode == 2 )
	{
		if( pTarget == NULL )	// local -0x2a: no valid target this tick
			m_iTacAnimStep++;
		else
			m_iTacAnimStep = 0;

		if( m_iTacAnimStep > 4 )
		{
			if( pev->framerate >= 254.0f )
			{
				EMIT_SOUND( ENT( pev ), CHAN_VOICE, "weapons/invis.wav", 1.0f, 0.8f );
				pev->sequence = 4;
			}
			if( pev->framerate <= 0.0f )
				pev->framerate = 0.0f;
			else
				pev->framerate -= 20.0f;
		}
		else
		{
			pev->sequence = 0;
			pev->framerate = 255.0f;
		}
	}

	// Staged second bubble shell (reads as a fade-in on placement).
	if( m_iBubbleShells > 0 && gpGlobals->time >= m_flBubbleNext )
	{
		CBasePlayer *pViewer = ( m_hBubbleViewer != NULL ) ? (CBasePlayer *)( (CBaseEntity *)m_hBubbleViewer ) : NULL;
		if( pViewer != NULL && pViewer->IsPlayer() && pViewer->IsAlive() )
			SendBubbleShell( pViewer, 40, 24 );
		m_iBubbleShells = 0;
		// The look-at refresh must not stack on the staged pair.
		m_flBubbleNext = gpGlobals->time + 1.0f;
	}

	// Owner feedback: while the placer is close and looking at their own tac,
	// refresh the range bubble and show the pickup hint. Both the tempent
	// burst and the hint are rate limited: m_flBubbleNext admits one 12-beam
	// shell per second per TAC, so the worst case for one client is 12 beams
	// per second for each of their own TACs they are looking at.
	if( gpGlobals->time >= m_flNextHint )
	{
		CBasePlayer *pOwner = Deployer();
		if( pOwner != NULL && pOwner->IsAlive() && pOwner->pev->deadflag == DEAD_NO )
		{
			Vector vecCenter = pev->origin;
			Vector vecDelta = vecCenter - ( pOwner->pev->origin + pOwner->pev->view_ofs );
			float flDist = vecDelta.Length();

			if( flDist > 1.0f && flDist < 200.0f )
			{
				UTIL_MakeVectors( pOwner->pev->v_angle );
				float flDot = DotProduct( vecDelta / flDist, gpGlobals->v_forward );
				if( flDot > 0.85f )
				{
					if( gpGlobals->time >= m_flBubbleNext )
					{
						ShowRangeBubble( pOwner, FALSE );
						m_flBubbleNext = gpGlobals->time + 1.0f;
					}
					if( flDist < 90.0f )
						ClientPrint( pOwner->pev, HUD_PRINTCENTER, "Press E to pick up the TAC" );
					m_flNextHint = gpGlobals->time + 1.0f;
				}
			}
		}
	}

	// ref @0x18df5d: schedule tail.
	m_Activity = (Activity)0xc5;
	m_IdealActivity = ACT_RESET;
}

// =============================================================
// CSGTCBodyPart — monster_arm/head/chest/leg/stomach 1-3
// Tier-scaled damage dummies (sk_monster_* cvars).
// =============================================================

LINK_ENTITY_TO_CLASS( monster_arm1, CSGTCBodyPart );
LINK_ENTITY_TO_CLASS( monster_arm2, CSGTCBodyPart );
LINK_ENTITY_TO_CLASS( monster_arm3, CSGTCBodyPart );
LINK_ENTITY_TO_CLASS( monster_head1, CSGTCBodyPart );
LINK_ENTITY_TO_CLASS( monster_head2, CSGTCBodyPart );
LINK_ENTITY_TO_CLASS( monster_head3, CSGTCBodyPart );
LINK_ENTITY_TO_CLASS( monster_chest1, CSGTCBodyPart );
LINK_ENTITY_TO_CLASS( monster_chest2, CSGTCBodyPart );
LINK_ENTITY_TO_CLASS( monster_chest3, CSGTCBodyPart );
LINK_ENTITY_TO_CLASS( monster_leg1, CSGTCBodyPart );
LINK_ENTITY_TO_CLASS( monster_leg2, CSGTCBodyPart );
LINK_ENTITY_TO_CLASS( monster_leg3, CSGTCBodyPart );
LINK_ENTITY_TO_CLASS( monster_stomach1, CSGTCBodyPart );
LINK_ENTITY_TO_CLASS( monster_stomach2, CSGTCBodyPart );
LINK_ENTITY_TO_CLASS( monster_stomach3, CSGTCBodyPart );

void CSGTCBodyPart::Precache( void )
{
	PRECACHE_MODEL( "models/skelete.mdl" );
}

void CSGTCBodyPart::Spawn( void )
{
	Precache();

	SET_MODEL( ENT( pev ), "models/skelete.mdl" );
	UTIL_SetSize( pev, Vector( -13, -13, 0 ), Vector( 13, 13, 72 ) );
	UTIL_SetOrigin( pev, pev->origin );

	pev->solid = SOLID_SLIDEBOX;
	pev->movetype = MOVETYPE_STEP;
	m_bloodColor = BLOOD_COLOR_RED;
	pev->view_ofs = Vector( 0, 0, 60 );
	m_flFieldOfView = 0.5f;
	m_MonsterState = MONSTERSTATE_NONE;
	pev->takedamage = DAMAGE_AIM;

	// Tier from the classname digit, scaled by the matching
	// sk_monster_<part><tier> damage-adjuster cvar.
	int iTier = 1;
	if ( FClassnameIs( pev, "monster_arm1" ) ) iTier = 1;
	else if ( FClassnameIs( pev, "monster_arm2" ) ) iTier = 2;
	else if ( FClassnameIs( pev, "monster_arm3" ) ) iTier = 3;
	else if ( FClassnameIs( pev, "monster_head1" ) ) iTier = 1;
	else if ( FClassnameIs( pev, "monster_head2" ) ) iTier = 2;
	else if ( FClassnameIs( pev, "monster_head3" ) ) iTier = 3;
	else if ( FClassnameIs( pev, "monster_chest1" ) ) iTier = 1;
	else if ( FClassnameIs( pev, "monster_chest2" ) ) iTier = 2;
	else if ( FClassnameIs( pev, "monster_chest3" ) ) iTier = 3;
	else if ( FClassnameIs( pev, "monster_leg1" ) ) iTier = 1;
	else if ( FClassnameIs( pev, "monster_leg2" ) ) iTier = 2;
	else if ( FClassnameIs( pev, "monster_leg3" ) ) iTier = 3;
	else if ( FClassnameIs( pev, "monster_stomach1" ) ) iTier = 1;
	else if ( FClassnameIs( pev, "monster_stomach2" ) ) iTier = 2;
	else if ( FClassnameIs( pev, "monster_stomach3" ) ) iTier = 3;

	float flAdjust = 1.0f;
	if ( FClassnameIs( pev, "monster_arm1" ) ) flAdjust = GetSkillCvar( "sk_monster_arm1" );
	else if ( FClassnameIs( pev, "monster_arm2" ) ) flAdjust = GetSkillCvar( "sk_monster_arm2" );
	else if ( FClassnameIs( pev, "monster_arm3" ) ) flAdjust = GetSkillCvar( "sk_monster_arm3" );
	else if ( FClassnameIs( pev, "monster_head1" ) ) flAdjust = GetSkillCvar( "sk_monster_head1" );
	else if ( FClassnameIs( pev, "monster_head2" ) ) flAdjust = GetSkillCvar( "sk_monster_head2" );
	else if ( FClassnameIs( pev, "monster_head3" ) ) flAdjust = GetSkillCvar( "sk_monster_head3" );
	else if ( FClassnameIs( pev, "monster_chest1" ) ) flAdjust = GetSkillCvar( "sk_monster_chest1" );
	else if ( FClassnameIs( pev, "monster_chest2" ) ) flAdjust = GetSkillCvar( "sk_monster_chest2" );
	else if ( FClassnameIs( pev, "monster_chest3" ) ) flAdjust = GetSkillCvar( "sk_monster_chest3" );
	else if ( FClassnameIs( pev, "monster_leg1" ) ) flAdjust = GetSkillCvar( "sk_monster_leg1" );
	else if ( FClassnameIs( pev, "monster_leg2" ) ) flAdjust = GetSkillCvar( "sk_monster_leg2" );
	else if ( FClassnameIs( pev, "monster_leg3" ) ) flAdjust = GetSkillCvar( "sk_monster_leg3" );
	else if ( FClassnameIs( pev, "monster_stomach1" ) ) flAdjust = GetSkillCvar( "sk_monster_stomach1" );
	else if ( FClassnameIs( pev, "monster_stomach2" ) ) flAdjust = GetSkillCvar( "sk_monster_stomach2" );
	else if ( FClassnameIs( pev, "monster_stomach3" ) ) flAdjust = GetSkillCvar( "sk_monster_stomach3" );
	if ( flAdjust <= 0.0f )
		flAdjust = 1.0f;

	pev->health = 50.0f * (float)iTier * flAdjust;
	pev->max_health = pev->health;

	pev->sequence = 0;
	pev->frame = 0;
	pev->framerate = 1.0;

	SetThink( &CSGTCBodyPart::IdleThink );
	pev->nextthink = gpGlobals->time + 1.0;
}

void CSGTCBodyPart::Killed( entvars_t *pevAttacker, int iGib )
{
	// No FL_MONSTER/MonsterInit: CBaseMonster::Killed would leave the dummy
	// standing unless it gibbed.
	SetTouch( NULL );
	SetThink( NULL );
	SGTC_KilledRemove( this );
}

void CSGTCBodyPart::IdleThink( void )
{
	// Passive target dummy: hold still, stay targetable.
	pev->nextthink = gpGlobals->time + 1.0;
	pev->velocity.x = 0;
	pev->velocity.y = 0;
}

// =============================================================
// CSGTCZatBoom — monster_zatboom (2.0, models/w_zat.mdl)
// =============================================================

TYPEDESCRIPTION CSGTCZatBoom::m_SaveData[] =
{
	DEFINE_FIELD( CSGTCZatBoom, m_flBlastTime, FIELD_TIME ),
	DEFINE_FIELD( CSGTCZatBoom, m_bSgtcZatLatch, FIELD_CHARACTER ),
};
IMPLEMENT_SAVERESTORE( CSGTCZatBoom, CBaseMonster );
LINK_ENTITY_TO_CLASS( monster_zatboom, CSGTCZatBoom );

void CSGTCZatBoom::Precache( void )
{
	PRECACHE_MODEL( "models/w_zat.mdl" );
	PRECACHE_SOUND( "weapons/electro4.wav" );	// build-11: placeholder for missing weapons/zatoverload.wav
	PRECACHE_SOUND( "weapons/explode3.wav" );	// build-11: placeholder for missing weapons/zatblow.wav
	PRECACHE_SOUND( "weapons/zatimpact.wav" );
	PRECACHE_SOUND( "items/weapondrop1.wav" );
	PRECACHE_MODEL( "sprites/zatimpact.spr" );
	PRECACHE_MODEL( "sprites/gammawave.spr" );
}

void CSGTCZatBoom::Spawn( void )
{
	Precache();

	SET_MODEL( ENT( pev ), "models/w_zat.mdl" );
	SGTC_DiagSpawn( "monster_zatboom", NULL, pev->origin, pev->scale, pev->body, pev->skin, pev->sequence );
	UTIL_SetSize( pev, Vector( -8, -8, 0 ), Vector( 8, 8, 16 ) );
	UTIL_SetOrigin( pev, pev->origin );

	pev->movetype = MOVETYPE_TOSS;
	pev->solid = SOLID_BBOX;
	pev->takedamage = DAMAGE_AIM;
	pev->health = 50.0f;	// ref CZatBoom::Spawn @0x14c9ac rodata 50
	m_flFieldOfView = 0;
	m_MonsterState = MONSTERSTATE_NONE;
	m_bloodColor = DONT_BLEED;

	m_flBlastTime = gpGlobals->time + 2.0;

	m_bSgtcZatLatch = 0;	// ref +0x3f4
	EMIT_SOUND_DYN( ENT( pev ), CHAN_WEAPON, "weapons/electro4.wav", 1.0, ATTN_NORM, 0, PITCH_NORM );	// ref @0x14cb40 chan 1

	SetThink( &CSGTCZatBoom::ChargeThink );
	pev->nextthink = gpGlobals->time + 0.1;
}

void CSGTCZatBoom::ChargeThink( void )
{
	// ref CZatBoom::Think @0x14cc24: first tick while FL_ONGROUND emits
	// weapondrop1 (CHAN_BODY) and latches +0x3f4; then blast-time check.
	if( !m_bSgtcZatLatch && ( pev->flags & FL_ONGROUND ) )
	{
		EMIT_SOUND( edict(), CHAN_BODY, "items/weapondrop1.wav", 1.0f, ATTN_NORM );
		m_bSgtcZatLatch = 1;
	}
	if ( gpGlobals->time >= m_flBlastTime )
	{
		Discharge();
		return;
	}
	pev->nextthink = gpGlobals->time + 0.1;
}

void CSGTCZatBoom::Killed( entvars_t *pevAttacker, int iGib )
{
	// No FL_MONSTER/MonsterInit: CBaseMonster::Killed would leave the charge
	// ticking down to Discharge() unless it gibbed.
	SetTouch( NULL );
	SetThink( NULL );
	SGTC_KilledRemove( this );
}

void CSGTCZatBoom::Discharge( void )
{
	CBaseEntity *pEnt = NULL;
	while ( ( pEnt = UTIL_FindEntityInSphere( pEnt, pev->origin, 192.0f ) ) != NULL )
	{
		if ( pEnt == this || pEnt->pev->takedamage == DAMAGE_NO )
			continue;
		if( pEnt->IsPlayer() )
		{
			CBasePlayer *pHit = (CBasePlayer *)pEnt;
			if( pHit->IsAlive() )
				pHit->Zat( pev );
		}
		else
			pEnt->TakeDamage( pev, pev, 80.0f, DMG_SHOCK );
	}
	EMIT_SOUND_DYN( ENT( pev ), CHAN_BODY, "weapons/explode3.wav", 1.0, ATTN_NORM, 0, PITCH_NORM );
	SetThink( NULL );
	UTIL_Remove( this );
}

// =============================================================
// CSGTCTrapBox — monster_trapbox (2.0, models/trapbox.mdl)
// =============================================================

// No save table: the box has no persistent member state of its own.
// m_iSgtcPlasmaSpr is a precache index (set again by Precache() on restore),
// m_vecPlace/m_iPlaceSlot are write-only and m_bTriggered only lives within one
// Trigger() call (the box removes itself). The ref's phase 1 / partner-link /
// armed state is not ported (#98): the port only ever runs phase 2.
LINK_ENTITY_TO_CLASS( monster_trapbox, CSGTCTrapBox );

void CSGTCTrapBox::Precache( void )
{
	PRECACHE_MODEL( "models/trapbox.mdl" );
	PRECACHE_SOUND( "weapons/place.wav" );
	// build-22: weapons/trapbox.wav is not in the payload; use the existing
	// C4 beep so monster_trapbox does not spam load errors.
	SGTC_PrecacheSound( "weapons/c4_beep.wav" );
	m_iSgtcPlasmaSpr = PRECACHE_MODEL( "sprites/plasma.spr" );
}

void CSGTCTrapBox::Spawn( void )
{
	Precache();

	SET_MODEL( ENT( pev ), "models/trapbox.mdl" );
	UTIL_SetSize( pev, Vector( -16, -16, 0 ), Vector( 16, 16, 32 ) );
	UTIL_SetOrigin( pev, pev->origin );

	pev->movetype = MOVETYPE_STEP;
	pev->solid = SOLID_BBOX;
	pev->takedamage = DAMAGE_AIM;
	pev->health = 50.0f;	// ref CTrapBox::Spawn @0x14df28 rodata 50 MATCH
	m_flFieldOfView = 0;
	m_MonsterState = MONSTERSTATE_NONE;
	m_bloodColor = DONT_BLEED;
	m_bTriggered = FALSE;

	EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "weapons/place.wav", 1.0, ATTN_NORM, 0, PITCH_NORM );	// ref @0x14e01d chan 2

	SetThink( &CSGTCTrapBox::TrapThink );
	pev->nextthink = gpGlobals->time + 0.1f;
}

int CSGTCTrapBox::TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType )
{
	// ref CTrapBox::TakeDamage @0x15ee94: DMG_CRUSH absorb; 0x1000000 remove;
	// team(attacker)==team(owner) filter; else base. Killed() runs Trigger().
	if( bitsDamageType == DMG_CRUSH )
		return 0;
	if( bitsDamageType == 0x1000000 )
	{
		UTIL_Remove( this );
		return 0;
	}
	if( pevAttacker && pev->owner )
	{
		CBaseEntity *pOwner = CBaseEntity::Instance( pev->owner );
		if( pOwner && pevAttacker->team == pOwner->pev->team )
			return 0;
	}
	return CBaseMonster::TakeDamage( pevInflictor, pevAttacker, flDamage, bitsDamageType );
}

void CSGTCTrapBox::Killed( entvars_t *pevAttacker, int iGib )
{
	Trigger();
	CBaseMonster::Killed( pevAttacker, iGib );
}

void CSGTCTrapBox::TrapThink( void )	// ref CTrapBox::Think @0x14e0d4 (phase 2: armed on deploy)
{
	pev->nextthink = gpGlobals->time + 0.1f;

	CBaseEntity *pEnt = NULL;
	while( ( pEnt = UTIL_FindEntityInSphere( pEnt, pev->origin, 96.0f ) ) != NULL )
	{
		if( pEnt->pev->takedamage != DAMAGE_NO && pEnt->pev->deadflag == DEAD_NO && pEnt->IsPlayer() )
		{
			if( pev->team && pEnt->pev->team == pev->team )
				continue;
			Trigger();
			return;
		}
	}
}

void CSGTCTrapBox::Trigger( void )
{
	// Re-entrancy guard: the 1000-damage blast below can kill something that
	// damages this box again (Killed -> Trigger -> TakeDamage ...) until the
	// stack overflows. Fire only once and stop taking damage before the loop.
	if( m_bTriggered )
		return;
	m_bTriggered = TRUE;
	pev->takedamage = DAMAGE_NO;
	pev->solid = SOLID_NOT;

	// The blast is team-blind: only the 96u proximity *trigger* sweep
	// (TrapThink) skips same-team players. The 128u damage sweep does not, so
	// an owner or teammate standing inside it takes the 1000 DMG_BLAST damage.
	// This is the observed behaviour; no saved reference artifact proves
	// upstream parity. Do not add an owner/team filter here without a replay
	// showing the intended behaviour differs (#195).
	CBaseEntity *pEnt = NULL;
	while ( ( pEnt = UTIL_FindEntityInSphere( pEnt, pev->origin, 128.0f ) ) != NULL )
	{
		if ( pEnt == this )
			continue;
		if ( FClassnameIs( pEnt->pev, "monster_trapbox" ) )
			continue;
		if ( pEnt->pev->takedamage != DAMAGE_NO )
			pEnt->TakeDamage( pev, pev, 1000.0f, DMG_BLAST );	// ref CTrapBox::Think vfun call @0x14e9bf rodata 1000
	}
	EMIT_SOUND_DYN( ENT( pev ), CHAN_BODY, "weapons/c4_beep.wav", 1.0, ATTN_NORM, 0, PITCH_NORM );
	SetThink( NULL );
	SetTouch( NULL );
	UTIL_Remove( this );
}

// =============================================================
// CSGTCShieldEmit — monster_shieldemit (2.0, models/shgen.mdl)
// =============================================================

TYPEDESCRIPTION CSGTCShieldEmit::m_SaveData[] =
{
	DEFINE_FIELD( CSGTCShieldEmit, m_pSgtcOwner, FIELD_CLASSPTR ),
	DEFINE_FIELD( CSGTCShieldEmit, m_iSgtcMode, FIELD_INTEGER ),
	DEFINE_FIELD( CSGTCShieldEmit, m_pSgtcEffect, FIELD_CLASSPTR ),
	DEFINE_FIELD( CSGTCShieldEmit, m_flSgtcNextFx, FIELD_TIME ),
};
IMPLEMENT_SAVERESTORE( CSGTCShieldEmit, CBaseMonster );
LINK_ENTITY_TO_CLASS( monster_shieldemit, CSGTCShieldEmit );

void CSGTCShieldEmit::Precache( void )
{
	PRECACHE_MODEL( "models/shgen.mdl" );
	PRECACHE_MODEL( "models/shield.mdl" );
	PRECACHE_MODEL( "models/bigshield.mdl" );
}

void CSGTCShieldEmit::Spawn( void )
{
	Precache();

	SET_MODEL( ENT( pev ), "models/shgen.mdl" );
	// build-11: shgen.mdl is a placeholder copy of shield.mdl (random shield
	// shape in MP maps).  Keep the blocking emitter, hide the placeholder mesh.
	pev->effects |= EF_NODRAW;
	UTIL_SetSize( pev, Vector( -24, -24, 0 ), Vector( 24, 24, 64 ) );
	UTIL_SetOrigin( pev, pev->origin );

	pev->movetype = MOVETYPE_NONE;
	pev->solid = SOLID_BBOX;
	pev->takedamage = DAMAGE_AIM;
	pev->health = 3000.0f;	// ref CSEmit::Spawn @0x1507b2 rodata 3000 (NOT turretHealth)
	pev->max_health = pev->health;
	m_flFieldOfView = 0;
	m_MonsterState = MONSTERSTATE_NONE;
	m_bloodColor = DONT_BLEED;

	SetThink( &CSGTCShieldEmit::EmitThink );
	pev->nextthink = gpGlobals->time + 1.0;
}

void CSGTCShieldEmit::Killed( entvars_t *pevAttacker, int iGib )
{
	// No FL_MONSTER/MonsterInit: CBaseMonster::Killed would leave the emitter
	// shielding unless it gibbed. Take the effect_bigshield child with it, as
	// the m_iSgtcMode == 2 path in EmitThink does.
	SetTouch( NULL );
	SetThink( NULL );
	if( m_pSgtcEffect )
	{
		UTIL_Remove( m_pSgtcEffect );
		m_pSgtcEffect = NULL;
	}
	SGTC_KilledRemove( this );
}

void CSGTCShieldEmit::EmitThink( void )	// ref CSEmit::Think @0x150872
{
	// Lazily create the effect_bigshield child once (ref this+0x3f8).
	if( !m_pSgtcEffect )
	{
		m_pSgtcEffect = CBaseEntity::Create( "effect_bigshield",
			pev->origin, Vector( 0, 0, 0 ), edict() );
		EMIT_SOUND( ENT( pev ), CHAN_VOICE,
			"forcefield/forcefield_loading.wav", 1.0f, 0.8f );	// ref @0x150921 chan 2
	}
	if( m_pSgtcEffect )
	{
		// Pass 1 (players): same-team, 3D dist <= 220, z-window
		// (-200..+100) -> stamp shield timestamp (ref ent+0xa2c).
		CBaseEntity *pScan = NULL;
		while( ( pScan = UTIL_FindEntityByClassname( pScan, "player" ) ) != NULL )
		{
			float dz = pScan->pev->origin.z - pev->origin.z;
			BOOL bOut = ( dz > 200.0f || -dz > 100.0f ) ? TRUE : FALSE;
			Vector vThem( pScan->pev->origin );
			Vector vUs( pev->origin );
			int dist = (int)( vUs - vThem ).Length();
			if( dist < 0xdc && pScan->pev->team == pev->team && !bOut )
				pScan->m_flSgtcShieldTime = gpGlobals->time + 0.1f;
		}
		// Pass 2 (all ents in 250 sphere): z-window only -> stamp
		// suppress timer (ref ent+0x128). The ref also stored a raw back-pointer
		// to the emitter (ent+0x12c); nothing here ever read it, so it is not
		// written any more (#98) — it would only have been able to dangle.
		pScan = NULL;
		while( ( pScan = UTIL_FindEntityInSphere( pScan, pev->origin, 250.0f ) ) != NULL )
		{
			float dz = pScan->pev->origin.z - pev->origin.z;
			if( dz > 200.0f || -dz > 100.0f )
				continue;
			pScan->m_flSgtcSuppressTime = gpGlobals->time + 0.1f;
		}
		// State machine (ref this+0x104): 1 -> child frame 80; 2 -> unload + remove both.
		if( m_iSgtcMode == 1 )
		{
			m_pSgtcEffect->pev->frame = 80.0f;
			m_iSgtcMode = 0;
		}
		if( m_iSgtcMode == 2 )
		{
			EMIT_SOUND( ENT( pev ), CHAN_VOICE,
				"forcefield/forcefield_unloading.wav", 1.0f, 0.8f );	// ref @0x150c00 chan 2
			UTIL_Remove( m_pSgtcEffect );
			UTIL_Remove( this );
			m_iSgtcMode = 0;
		}
		// FX heartbeat (ref this+0x3f4): TE_DLIGHT-ish pulse every 0.2 s.
		if( m_flSgtcNextFx < gpGlobals->time )
		{
			MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, pev->origin );
				WRITE_BYTE( TE_DLIGHT );
				WRITE_COORD( pev->origin.x );
				WRITE_COORD( pev->origin.y );
				WRITE_COORD( pev->origin.z );
				WRITE_BYTE( 40 );
				WRITE_BYTE( 255 );
				WRITE_BYTE( 232 );
				WRITE_BYTE( 70 );
				WRITE_BYTE( 5 );
				WRITE_BYTE( 5 );
			MESSAGE_END();
			m_flSgtcNextFx = gpGlobals->time + 0.2f;
		}
	}
	pev->nextthink = gpGlobals->time + 0.005f;	// ref bottom: time + 0.005
}

// =============================================================
// CSGTCLockDown — monster_lockdown (2.0, models/lockpatch.mdl)
// =============================================================

TYPEDESCRIPTION CSGTCLockDown::m_SaveData[] =
{
	DEFINE_FIELD( CSGTCLockDown, m_hLockTarget, FIELD_EHANDLE ),
	DEFINE_FIELD( CSGTCLockDown, m_bLockAnchored, FIELD_BOOLEAN ),
	DEFINE_FIELD( CSGTCLockDown, m_vecLockOffset, FIELD_VECTOR ),
};
IMPLEMENT_SAVERESTORE( CSGTCLockDown, CBaseEntity );
LINK_ENTITY_TO_CLASS( monster_lockdown, CSGTCLockDown );

void CSGTCLockDown::Precache( void )
{
	PRECACHE_MODEL( "models/lockpatch.mdl" );
	PRECACHE_SOUND( "weapons/place.wav" );
}

void CSGTCLockDown::Spawn( void )	// ref CLockDown::Spawn @0x14fc0c
{
	Precache();

	SET_MODEL( ENT( pev ), "models/lockpatch.mdl" );
	UTIL_SetSize( pev, Vector( -32, -32, 0 ), Vector( 32, 32, 64 ) );
	UTIL_SetOrigin( pev, pev->origin );

	pev->movetype = MOVETYPE_NONE;
	pev->solid = SOLID_BBOX;
	m_iBloodColor = DONT_BLEED;
	pev->takedamage = DAMAGE_AIM;
	m_bLockAnchored = FALSE;
	m_vecLockOffset = g_vecZero;

	if( pev->owner != NULL )
		m_hLockTarget = CBaseEntity::Instance( pev->owner );

	EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "weapons/place.wav", 1.0f, ATTN_NORM, 0, PITCH_NORM );	// ref @0x14fdb4 chan 2

	SetThink( &CSGTCLockDown::LockThink );
	pev->nextthink = gpGlobals->time + 0.1f;
}

int CSGTCLockDown::TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType )
{
	// ref CLockDown::TakeDamage @0x15018c tracks HP (8000, DieFX on zero) only
	// when the lock target is a func_door, and the owner-damage latch in
	// LockThink drains the same pool. This port never binds a door (the target
	// is always the owning player) and nothing writes m_iSgtcDoorLockDmg, so
	// those paths were unreachable and are gone (#98): the patch takes no damage.
	return 0;
}

void CSGTCLockDown::LockThink( void )	// ref CLockDown::Think @0x150272
{
	if( m_hLockTarget == 0 )
	{
		UTIL_Remove( this );
		return;
	}

	CBasePlayer *pPl = ( m_hLockTarget != 0 && m_hLockTarget->IsPlayer() )
		? (CBasePlayer *)(CBaseEntity *)m_hLockTarget : NULL;
	if( pPl == NULL )
	{
		UTIL_Remove( this );
		return;
	}

	if( !m_bLockAnchored )
	{
		m_bLockAnchored = TRUE;
		pPl->m_bSgtcDoorLock = 1;
		m_vecLockOffset = pev->origin - pPl->pev->origin;
	}

	Vector vecGoal = pPl->pev->origin + m_vecLockOffset;
	if( ( vecGoal - pev->origin ).Length() > 1.0f )
	{
		UTIL_SetOrigin( pev, vecGoal );
		pev->angles = pPl->pev->angles;
	}

	pev->nextthink = gpGlobals->time + 0.1f;
}

#endif // CLIENT_DLL
