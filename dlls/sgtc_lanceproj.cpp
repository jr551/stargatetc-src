// sgtc_lanceproj.cpp — StargateTC energy-bolt projectiles (NEW file).
//
// Clean-room port of ref/stc_i386.so (STC 2.0). All behavior facts decoded
// from the reference binary; offsets cited inline.
//
// entvars offsets verified against the ref binary's own usage:
//   pev->dmg=0x1e0, pev->takedamage=0x16c, pev->owner=0x198,
//   pev->enemy=0x190, pev->movetype=0x108, pev->solid=0x10c, pev->model=0x6c,
//   pev->classname=0x0, pev->nextthink=0x104, pev->origin=0x8,
//   pev->velocity=0x20, pev->v_angle=0x50, pev->friction=0x11c.
// C++ members: this+0x134 m_pfnThink, this+0x13c m_pfnTouch,
//   this+0x140 m_pfnUse, this+0x128 m_flDie, this+0x130
//   m_fExplosionProtection, this+0xd5 m_fAttendExplosion.
//
// pev->dmg sources (STC-extended skilldata_t — stock field names mapped):
//   lance  gSkillData+0x118 -> batteryCapacity
//   big    gSkillData+0x120 -> healthkitCapacity
//   reetou gSkillData+0x124 -> scientistHeal

#include "extdll.h"
#include "sgtc_diag.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"

static int s_iBigGlow = 0;
#include "skill.h"
#include "sgtc_weapons.h"
#include "sgtc_monsters.h"
#include "sgtc_lanceproj.h"
#include "sgtc_shield.h"
#include "effects.h"
#include "player.h"
#include "sgtc_util.h"

// ref helper @0xbf8e6 DoSpark: sparks + random spark sound at impact.
static void SGTC_DoSpark( entvars_t *pev, const Vector &pos )
{
	{
		Vector vDbg = pos + pev->size * 0.5f;
		char szDbg[96];
		const char *pszOwner = "none";
		int iFake = 0;
		if( pev->owner && !FNullEnt( pev->owner ) )
		{
			edict_t *pOwner = ENT( pev->owner );
			if( pOwner && pOwner->v.netname )
				pszOwner = STRING( pOwner->v.netname );
			if( pOwner && FBitSet( pOwner->v.flags, FL_FAKECLIENT ) )
				iFake = 1;
		}
		snprintf( szDbg, sizeof( szDbg ), "lance_impact:owner=%s:fake=%d", pszOwner, iFake );
		SGTC_DiagSparks( szDbg, vDbg );
		UTIL_Sparks( vDbg );
	}	// ref pev+0xf4 @0xcf8e6
	const float flVol = 0.4f * RANDOM_FLOAT( 0.25f, 0.75f );
	switch( (int)( 6.0f * RANDOM_FLOAT( 0.0f, 1.0f ) + 0.5f ) )
	{
	case 0: EMIT_SOUND( ENT( pev ), CHAN_VOICE, "buttons/spark1.wav", flVol, 0.8f ); break; // ref DoSpark @0xbf8e6 chan 2
	case 1: EMIT_SOUND( ENT( pev ), CHAN_VOICE, "buttons/spark2.wav", flVol, 0.8f ); break;
	case 2: EMIT_SOUND( ENT( pev ), CHAN_VOICE, "buttons/spark3.wav", flVol, 0.8f ); break;
	case 3: EMIT_SOUND( ENT( pev ), CHAN_VOICE, "buttons/spark4.wav", flVol, 0.8f ); break;
	case 4: EMIT_SOUND( ENT( pev ), CHAN_VOICE, "buttons/spark5.wav", flVol, 0.8f ); break;
	case 5: EMIT_SOUND( ENT( pev ), CHAN_VOICE, "buttons/spark6.wav", flVol, 0.8f ); break;
	default: break;
	}
}

int g_sModelIndexNarco = 0;

// ===========================================================================
// CSGTCProjBase — shared bolt skeleton
// ===========================================================================

// Save/restore (#99), shared by lance_proj / big_lance_proj / reetou_proj (each sets its own
// classname in BaseSpawn, so the engine re-creates the right class). Not saved:
// m_pWeapon / m_pOwner / m_pTarget (raw entity pointers that would dangle; m_pOwner reads back
// NULL, so a restored bolt loses its owner touch filter until they become EHANDLEs) and
// m_vecDir (written by the factories, never read).
TYPEDESCRIPTION CSGTCProjBase::m_SaveData[] =
{
	DEFINE_FIELD( CSGTCProjBase, m_flDie, FIELD_TIME ),
	DEFINE_FIELD( CSGTCProjBase, m_fAttendExplosion, FIELD_BOOLEAN ),
	DEFINE_FIELD( CSGTCProjBase, m_fExplosionProtection, FIELD_BOOLEAN ),
	DEFINE_FIELD( CSGTCProjBase, m_vecSavedVel, FIELD_VECTOR ),
	DEFINE_FIELD( CSGTCProjBase, m_flSavedSpeed, FIELD_FLOAT ),
};

IMPLEMENT_SAVERESTORE( CSGTCProjBase, CGrenade )

void CSGTCProjBase::BaseSpawn( const char *pszModel, const char *pszClass,
	float flSpeed )
{
	Precache();
	pev->movetype = MOVETYPE_BOUNCE;
	pev->solid = SOLID_BBOX;
	SET_MODEL( ENT( pev ), pszModel );
	UTIL_SetSize( pev, Vector( 0, 0, 0 ), Vector( 0, 0, 0 ) );
	UTIL_SetOrigin( pev, pev->origin );
	pev->classname = MAKE_STRING( pszClass );

	SetThink( &CSGTCProjBase::EffectThink );
	SetUse( NULL );
	SetTouch( &CSGTCProjBase::LanceTouch );

	m_flDie = gpGlobals->time + 5.0f;
	m_fAttendExplosion = FALSE;
	m_fExplosionProtection = FALSE;
	m_pWeapon = NULL;
	m_pOwner = NULL;
	m_pTarget = NULL;
	m_vecDir = g_vecZero;

	UTIL_MakeVectors( pev->v_angle );
	pev->velocity = pev->v_angle.Normalize() * flSpeed;	// ref uses v_angle
	pev->friction = 0.01f;
	m_vecSavedVel = pev->velocity;
	m_flSavedSpeed = pev->velocity.Length();
	SGTC_DiagSpawn( pszClass, NULL, pev->origin, pev->scale, pev->body, pev->skin, pev->sequence );
	pev->nextthink = gpGlobals->time + 0.001;
}

// ref CLanceProj::Accelere @0xfe3f8: at top speed add savedVel scaled by
// (savedSpeed/1000)*0.8, otherwise decay the bolt by 0.8.
void CSGTCProjBase::Accelere( void )
{
	float flSpeed = pev->velocity.Length();
	if( flSpeed < 2000.0f )
	{
		pev->velocity = pev->velocity +
			m_vecSavedVel * ( m_flSavedSpeed / 1000.0f * 0.8f );
		if( pev->velocity.Length() > 2000.0f )
			pev->velocity = pev->velocity.Normalize() * 2000.0f;
	}
}

// ref CLanceProj::Exploder @0x10e508 (big: parallel body; reetou uses Explodera
// @0x161984 with CSprite::SpriteCreate — no TE_SPRITE in lance/big exploders).
void CSGTCProjBase::Exploder( TraceResult *pTrace, int bitsDamageType )
{
	pev->model = iStringNull;	// ref @0x10e508 zeroes pev+0xb8 (model)
	pev->solid = SOLID_NOT;
	pev->takedamage = DAMAGE_NO;

	if( pTrace->flFraction != 1.0 )
	{
		pev->origin = pTrace->vecEndPos +
			( pTrace->vecPlaneNormal * ( pev->dmg - 24.0f ) * 0.6f );
	}

	UTIL_PointContents( pev->origin );	// ref evaluates and discards

	entvars_t *pevOwner = NULL;
	if( pev->owner )
		pevOwner = VARS( pev->owner );
	pev->owner = NULL;

	if( RANDOM_FLOAT( 0, 1 ) < 0.5f )
		UTIL_DecalTrace( pTrace, 11 );	// ref pushes 0xb
	else
		UTIL_DecalTrace( pTrace, 12 );	// ref pushes 0xc

	// ref @0x10e508: (m_flDie <= gpGlobals->time || m_fAttendExplosion) &&
	// !m_fExplosionProtection -> RadiusDamage(pev, pevOwner, pev->dmg,
	// CLASS_NONE, bitsDamageType). In the ref m_flDie stays 0 so the first
	// term is always true; now that m_flDie is the bolt's expiry (BaseSpawn)
	// an impact before expiry must still do damage, so only the protection
	// flag (shield emitter hit) suppresses it.
	if( !m_fExplosionProtection )
	{
		RadiusDamage( pev, pevOwner, pev->dmg, CLASS_NONE, bitsDamageType );
	}

	EMIT_SOUND( ENT( pev ), CHAN_VOICE,
		DebrisSounds()[RANDOM_LONG( 0, 2 )], DebrisVol(), 0.8f );	// ref @0xfe8e6: chan 2; lance 0.9, big/reetou 1.0

	pev->effects |= EF_NODRAW;	// ref @0x10e508 tail: |0x80 then g_vecZero
	pev->velocity = g_vecZero;
	pev->nextthink = gpGlobals->time + 0.3f;
	SetThink( &CBaseEntity::SUB_Remove );
}

// ref CLanceProj::ProjTouch @0xfe9d0.
void CSGTCProjBase::ProjTouch( CBaseEntity *pOther )
{
	if( pOther == m_pOwner )
		return;

	TraceResult tr;
	pev->enemy = pOther->edict();
	Vector vecSpot = pev->origin - pev->velocity.Normalize() * 32.0f;
	UTIL_TraceLine( vecSpot, vecSpot + pev->velocity.Normalize() * 64.0f,
		ignore_monsters, ENT( pev ), &tr );

	CBaseEntity *pHit = CBaseEntity::Instance( tr.pHit );
	// ref @0xfeb28 — IsPlayer() then byte +0xa3e = 1.
	if( pHit && pHit->IsPlayer() )
		((CBasePlayer *)pHit)->m_bLanceStunFlag = 1;

	SGTC_DoSpark( pev, tr.vecEndPos );
	Exploder( &tr, DMG_NEVERGIB );	// ref 0x1000 @0x10eb9c touch tail
}

// ref CGrenade::LanceTouch @0xf4662 — trace back then forward, DMG_BLAST.
void CSGTCProjBase::LanceTouch( CBaseEntity *pOther )
{
	TraceResult tr;
	pev->enemy = pOther->edict();
	Vector vecSpot = pev->origin - pev->velocity.Normalize() * 32.0f;
	UTIL_TraceLine( vecSpot, vecSpot + pev->velocity.Normalize() * 64.0f,
		ignore_monsters, ENT( pev ), &tr );
	Explode( &tr, DMG_BLAST );
}

// ref CLanceProj::EffectThink @0xfeb9c.
void CSGTCProjBase::EffectThink( void )
{
	pev->flags |= FL_DUCKING;	// ref @0xfebc6: pev+0x118 |= 0x40
	pev->nextthink = gpGlobals->time + 0.1;

	if( FClassnameIs( pev, "big_lance_proj" ) )
	{
		pev->gravity = 0.05f;
		pev->friction = 0.0f;
		MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY, pev->origin );
			WRITE_BYTE( TE_DLIGHT );
			WRITE_COORD( pev->origin.x );
			WRITE_COORD( pev->origin.y );
			WRITE_COORD( pev->origin.z );
			WRITE_BYTE( 180 );
			WRITE_BYTE( 255 );
			WRITE_BYTE( 180 );
			WRITE_BYTE( 60 );
			WRITE_BYTE( 1 );
			WRITE_BYTE( 250 );
		MESSAGE_END();
		if( s_iBigGlow > 0 )
		{
			MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY, pev->origin );
				WRITE_BYTE( TE_SPRITE );
				WRITE_COORD( pev->origin.x );
				WRITE_COORD( pev->origin.y );
				WRITE_COORD( pev->origin.z );
				WRITE_SHORT( s_iBigGlow );
				WRITE_BYTE( 26 );
				WRITE_BYTE( 220 );
			MESSAGE_END();
		}
		pev->nextthink = gpGlobals->time + 0.03f;
		if( pev->body == 1 )
			pev->velocity = g_vecZero;	// test freeze, keep FX emitting
	}

	// Heading: pev->angles is only the model orientation. UTIL_VecToAngles()
	// returns entity-convention pitch (+ = up) which UTIL_MakeVectors()
	// mirrors, so take the sweep direction from the velocity itself.
	Vector vecFwd = pev->velocity;
	if( vecFwd.Length() > 0.01f )
		pev->angles = UTIL_VecToAngles( vecFwd );
	else
		vecFwd = m_vecSavedVel;
	vecFwd = vecFwd.Normalize();
	Accelere();

	// Lead sweep: origin -> origin+vecFwd*50 (dont_ignore); if something
	// is inside the lead distance, force a Touch.
	Vector vecStart = pev->origin;
	TraceResult tr;
	UTIL_TraceLine( vecStart, vecStart + vecFwd * 50.0f,
		dont_ignore_monsters, ENT( pev ), &tr );

	// ref @0x10eb9c: only touch when the trace hit closer than 50u lead
	// (vecEndPos vs origin+vecFwd*50 via Endo_distance3d). A clear sweep
	// (flFraction 1) must not touch: Instance( NULL ) is the world, so
	// without this test every bolt touched the world on its first think.
	CBaseEntity *pHit = CBaseEntity::Instance( tr.pHit );
	if( tr.flFraction < 1.0f && pHit && SGTC_Distance3D( tr.vecEndPos,
		vecStart + vecFwd * 50.0f ) < 50 )
	{
		Touch( pHit );
	}

	// Kill nearby monster_shieldemit inside the z window (ref compares
	// bolt z vs emitter z within [100,200]).
	CBaseEntity *pEnt = NULL;
	while( ( pEnt = UTIL_FindEntityInSphere( pEnt, pev->origin, 250.0f ) ) !=
		NULL )
	{
		if( !FClassnameIs( pEnt->pev, "monster_shieldemit" ) )
			continue;

		// ref @0x10eb9c: z-window predicate never arms local_11 in practice;
		// within 250u always Exploder(0x1000) when !m_fAttendExplosion (+0xd5).
		if( !m_fAttendExplosion )
		{
			m_fExplosionProtection = TRUE;	// ref +0x130 before Exploder
			SGTC_ShieldEmitArmImpactFlash( pEnt );
			Exploder( &tr, DMG_NEVERGIB );
		}
	}

	// End-of-life (ref @0xfef18): expire at m_flDie (set in BaseSpawn).
	if( gpGlobals->time >= m_flDie && !m_fAttendExplosion )
	{
		if( pev->owner )
			VARS( pev->owner )->nextthink = 1.0f;
		Exploder( &tr, DMG_NEVERGIB );	// ref 0x1000 @0x10eb9c suppress-window tail
		return;
	}

	pev->nextthink = gpGlobals->time + 0.008;
}

// ===========================================================================
// CLanceProj — "lance_proj" (spawn speed 320; turret 250; debris1-3)
// ===========================================================================

LINK_ENTITY_TO_CLASS( lance_proj, CLanceProj );

static const char *s_sgLanceDebris[3] = {
	"weapons/debris1.wav", "weapons/debris2.wav", "weapons/debris3.wav"
};

const char * const *CLanceProj::DebrisSounds( void ) { return s_sgLanceDebris; }

void CLanceProj::Spawn( void )
{
	BaseSpawn( "models/proj.mdl", "lance_proj", 320.0f );
	pev->dmg = 60.0f;	// ref @0xfe3e3: gSkillData+0x118 (MP hardcode 60, P167 map)
}

void CLanceProj::Precache( void )
{
	PRECACHE_MODEL( "models/proj.mdl" );
	PRECACHE_SOUND( "weapons/rocket1.wav" );
	PRECACHE_SOUND( "weapons/debris1.wav" );
	PRECACHE_SOUND( "weapons/debris2.wav" );
	PRECACHE_SOUND( "weapons/debris3.wav" );
	PRECACHE_SOUND( "buttons/spark1.wav" );
	PRECACHE_SOUND( "buttons/spark2.wav" );
	PRECACHE_SOUND( "buttons/spark3.wav" );
	PRECACHE_SOUND( "buttons/spark4.wav" );
	PRECACHE_SOUND( "buttons/spark5.wav" );
}

// ref @0x1000ce.
CLanceProj *CLanceProj::CreateTourrelleProj( Vector vecOrigin, Vector vecAngles )
{
	CLanceProj *pProj = GetClassPtr( (CLanceProj *)NULL );

	UTIL_SetOrigin( pProj->pev, vecOrigin + vecAngles * 20.0f );
	pProj->pev->v_angle = vecAngles;
	pProj->Spawn();
	pProj->pev->velocity = vecAngles * 250.0f;
	pProj->SetTouch( &CLanceProj::ProjTouch );
	pProj->m_pfnUse = NULL;
	return pProj;
}

// ref @0x1001b8.
CLanceProj *CLanceProj::CreateLanceProj( Vector vecStart, Vector vecVel,
	Vector vecTarget, CBaseEntity *pOwner, CLanceJaffa *pWeapon )
{
	CBasePlayer *pPlayer = pWeapon ? pWeapon->m_pPlayer : NULL;
	CLanceProj *pProj = GetClassPtr( (CLanceProj *)NULL );

	UTIL_SetOrigin( pProj->pev, vecStart );
	pProj->pev->v_angle = vecTarget - vecStart;	// ref stores raw delta
	pProj->Spawn();
	pProj->SetTouch( &CLanceProj::ProjTouch );
	pProj->m_pfnUse = NULL;
	pProj->m_pWeapon = pWeapon;
	pProj->pev->owner = pOwner ? pOwner->edict() : NULL;
	if( pOwner && pOwner->IsPlayer() )
		pProj->m_pOwner = pOwner;
	pProj->pev->velocity = pProj->pev->velocity + vecVel * 1.5f;
	pProj->m_vecDir = vecVel;
	// build-7: models/proj.mdl has one sequence; 4 is out of range (warns).
	pProj->pev->sequence = 0;	// ref pev+0x148 @0x1101b8
	pProj->pev->framerate = 0.0f;	// ref pev+0x14c
	// ref @0x1101b8: owner entity +0x128 suppress window -> m_fAttendExplosion (+0xd5).
	if( pOwner && gpGlobals->time < pOwner->m_flSgtcSuppressTime )
		pProj->m_fAttendExplosion = TRUE;
	if( pPlayer )
		pProj->pev->v_angle = pPlayer->pev->v_angle;
	else if( pOwner )
		pProj->pev->v_angle = pOwner->pev->v_angle;
	SGTC_DiagSpawn( "lance_proj", pOwner, pProj->pev->origin, pProj->pev->scale,
		pProj->pev->body, pProj->pev->skin, pProj->pev->sequence );
	return pProj;
}

// ===========================================================================
// CBigLanceProj — "big_lance_proj" (dmg base 240, homing tail)
// ===========================================================================

LINK_ENTITY_TO_CLASS( big_lance_proj, CBigLanceProj );

static const char *s_sgBigLanceDebris[3] = {
	"weapons/explo1.wav", "weapons/explo2.wav", "weapons/explode3.wav"
};

const char * const *CBigLanceProj::DebrisSounds( void )
{
	return s_sgBigLanceDebris;
}

void CBigLanceProj::Spawn( void )
{
	BaseSpawn( "models/proj.mdl", "big_lance_proj", 320.0f );
	// build-21 (John): glider staff projectile = REALLY BIG: 10x the ref damage
	// (240 -> 2400) and a visibly larger model.  Exploder's RadiusDamage uses
	// pev->dmg as both damage and radius base, so the 10x also scales the blast.
	pev->dmg = 2400.0f;
	pev->scale = 2.5f;
	pev->effects |= EF_NODRAW;	// plasma FX replaces the blue sphere
	pev->gravity = 0.05f;		// build-22: far less decay
	pev->friction = 0.0f;
	m_flDie = gpGlobals->time + 8.0f;
}

void CBigLanceProj::Precache( void )
{
	s_iBigGlow = PRECACHE_MODEL( "sprites/animglow01.spr" );
	PRECACHE_MODEL( "models/proj.mdl" );
	PRECACHE_SOUND( "weapons/explo1.wav" );
	PRECACHE_SOUND( "weapons/explo2.wav" );
	PRECACHE_SOUND( "weapons/explode3.wav" );
	PRECACHE_SOUND( "weapons/explode4.wav" );	// build-21 audit
	PRECACHE_SOUND( "weapons/explode5.wav" );
	PRECACHE_MODEL( "sprites/fexplo.spr" );
}

void CBigLanceProj::Accelere( void )
{
	CSGTCProjBase::Accelere();

	// Homing tail (ref @0xb8b28+): once slow (<300), steer the saved vector
	// toward the target at 1.5 gain.
	if( pev->velocity.Length() < 300.0f && m_pTarget )
	{
		m_vecSavedVel =
			( m_pTarget->pev->origin - pev->origin ).Normalize() *
			m_flSavedSpeed * 1.5f;
	}
}

CBigLanceProj *CBigLanceProj::CreateTourrelleProj( Vector vecOrigin,
	Vector vecAngles )
{
	CBigLanceProj *pProj = GetClassPtr( (CBigLanceProj *)NULL );

	UTIL_SetOrigin( pProj->pev, vecOrigin + vecAngles * 20.0f );
	pProj->pev->v_angle = vecAngles;
	pProj->Spawn();
	pProj->pev->velocity = vecAngles * 250.0f;
	pProj->SetTouch( &CBigLanceProj::ProjTouch );
	pProj->m_pfnUse = NULL;
	return pProj;
}

// ref @0xb9e30.
CBigLanceProj *CBigLanceProj::CreateBigLanceProj( Vector vecStart,
	Vector vecVel, Vector vecTarget, CBaseEntity *pOwner,
	CBigLanceJaffa *pWeapon )
{
	CBigLanceProj *pProj = GetClassPtr( (CBigLanceProj *)NULL );

	UTIL_SetOrigin( pProj->pev, vecStart );
	pProj->pev->v_angle = vecTarget - vecStart;
	pProj->Spawn();
	pProj->SetTouch( &CBigLanceProj::ProjTouch );
	pProj->m_pfnUse = NULL;
	pProj->m_pWeapon = pWeapon;
	pProj->pev->owner = pOwner ? pOwner->edict() : NULL;
	if( pOwner && pOwner->IsPlayer() )
		pProj->m_pOwner = pOwner;
	pProj->pev->velocity = pProj->pev->velocity + vecVel * 1.5f;
	pProj->m_vecDir = vecVel;
	// build-7: models/proj.mdl has one sequence; 4 is out of range (warns).
	pProj->pev->sequence = 0;	// ref pev+0x148 @0xc9e30
	pProj->pev->framerate = 0.0f;	// ref pev+0x14c
	pProj->pev->dmg = pProj->pev->dmg * 2.0f;	// ref @0xb9fa7 doubles the field
	SGTC_DiagSpawn( "big_lance_proj", pOwner, pProj->pev->origin, pProj->pev->scale,
		pProj->pev->body, pProj->pev->skin, pProj->pev->sequence );
	if( pOwner && gpGlobals->time < pOwner->m_flSgtcSuppressTime )
		pProj->m_fAttendExplosion = TRUE;	// ref owner+0x128 @0xc9e30
	return pProj;
}

// ===========================================================================
// CReetouProj — "reetou_proj" (speed 1600, narco trail)
// ===========================================================================

LINK_ENTITY_TO_CLASS( reetou_proj, CReetouProj );

// Save/restore (#99): the narco-trail timer is the only member beyond CSGTCProjBase's.
TYPEDESCRIPTION CReetouProj::m_SaveData[] =
{
	DEFINE_FIELD( CReetouProj, m_flNarcoNext, FIELD_TIME ),
};

IMPLEMENT_SAVERESTORE( CReetouProj, CSGTCProjBase )

static const char *s_sgReetouDebris[3] = {
	"weapons/terblast1.wav", "weapons/terblast2.wav", "weapons/terblast3.wav"
};

const char * const *CReetouProj::DebrisSounds( void )
{
	return s_sgReetouDebris;
}

void CReetouProj::Spawn( void )
{
	BaseSpawn( "models/w_projreetou.mdl", "reetou_proj", 1600.0f );
	SetThink( &CReetouProj::EffectThink );	// BaseSpawn bound the shared EffectThink
	SetTouch( &CReetouProj::ProjTouch );
	m_flNarcoNext = 0.0f;
	pev->nextthink = gpGlobals->time + 0.1;
	pev->dmg = 80.0f;	// ref @0x1518f5: gSkillData+0x124 (MP hardcode 80)
	// build-7: the shipped w_projreetou.mdl has exactly one sequence,
	// bodypart and skin, so the ref field values (sequence 4 / body 19 /
	// skin 120) are out of range for it (engine logs SV_StudioSetupBones
	// warnings and falls back).  The ref's scale 50 is also not a bolt size:
	// Xash3D applies pev->scale to studio models (ref_gl/gl_rmain.c:458), so
	// every reetou shot rendered a 50x bolt with a matching narco trail - the
	// "random zat/gammawave flashes with nobody shooting" seen when bots
	// (Goa'uld Jaffa/Horus kits carry weapon_reetou) fire.
	pev->sequence = 0;
	pev->body = 0;
	pev->skin = 0;
	pev->rendermode = kRenderNormal;
	pev->renderamt = 255.0f;
	pev->scale = 1.0f;
}

void CReetouProj::Precache( void )
{
	PRECACHE_MODEL( "models/w_projreetou.mdl" );
	PRECACHE_MODEL( "sprites/xfireball3.spr" );
	g_sModelIndexNarco = PRECACHE_MODEL( "sprites/xfireball3.spr" );
	PRECACHE_SOUND( "weapons/terblast1.wav" );
	PRECACHE_SOUND( "weapons/terblast2.wav" );
	PRECACHE_SOUND( "weapons/terblast3.wav" );
}

// ref Explodera @0x161984: overrides the shared Exploder (virtual) so impacts
// and end-of-life both get the xfireball3 flash.
void CReetouProj::Exploder( TraceResult *pTrace, int bitsDamageType )
{
	// ref @0x161984: dmg > 10 → xfireball3 CSprite (not TE_SPRITE index 0).
	if( pev->dmg > 10.0f )
	{
		CSprite *pSpr = CSprite::SpriteCreate( "sprites/xfireball3.spr",
			pev->origin, TRUE );
		if( pSpr )
		{
			pSpr->pev->scale = 1.0f;	// ref +0x130 = 1.0f
			pSpr->AnimateAndDie( 28.0f );
			pSpr->pev->frame += 2.7f;	// ref +0x144 += 2.7
			pSpr->SetTransparency( kRenderTransAdd, 0x78, 0x50, 0xff, 0xff,
				kRenderFxNone );	// ref mode 5, colors, fx 0xe
		}
	}
	CSGTCProjBase::Exploder( pTrace, bitsDamageType );
}

void CReetouProj::EffectThink( void )
{
	// Narco-sprite trail every 0.1s (ref @0x151fe0-0x152143):
	// TE_SPRITE at origin, scale=(dmg-50)*0.7, brightness=12.
	if( m_flNarcoNext < gpGlobals->time )
	{
		MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, pev->origin );
			WRITE_BYTE( TE_SPRITE );
			WRITE_COORD( pev->origin.x );
			WRITE_COORD( pev->origin.y );
			WRITE_COORD( pev->origin.z );
			WRITE_SHORT( g_sModelIndexNarco );
			WRITE_BYTE( (int)( ( pev->dmg - 50.0f ) * 0.7f ) );
			WRITE_BYTE( 12 );
		MESSAGE_END();
		m_flNarcoNext = gpGlobals->time + 0.1;
	}

	// ref @0x152143: end-of-life — owner link + explode, 0.008s re-think.
	if( gpGlobals->time >= m_flDie && !m_fAttendExplosion )
	{
		if( pev->owner )
			VARS( pev->owner )->nextthink = 1.0f;
		TraceResult tr;
		// pev->angles is never set for this bolt (and MakeVectors would mirror
		// pitch of a VecToAngles result anyway): trace along the velocity.
		Vector vecFwd = pev->velocity;
		if( vecFwd.Length() < 0.01f )
			vecFwd = m_vecSavedVel;
		vecFwd = vecFwd.Normalize();
		UTIL_TraceLine( pev->origin, pev->origin + vecFwd,
			dont_ignore_monsters, ENT( pev ), &tr );
		Exploder( &tr, DMG_NEVERGIB );
	}

	pev->nextthink = gpGlobals->time + 0.008;
}

// ref @0x162e32.
CReetouProj *CReetouProj::CreateReetouProj( Vector vecStart, Vector vecAimPoint,
	CBaseEntity *pOwner, CReetou *pWeapon )
{
	CReetouProj *pProj = GetClassPtr( (CReetouProj *)NULL );

	UTIL_SetOrigin( pProj->pev, vecStart );
	pProj->pev->v_angle = vecAimPoint - vecStart;	// ref: delta into v_angle, Spawn normalizes
	pProj->Spawn();
	pProj->m_pfnUse = NULL;
	pProj->m_pWeapon = (CBaseEntity *)pWeapon;
	pProj->pev->owner = pOwner ? pOwner->edict() : NULL;
	if( pOwner && pOwner->IsAlive() )
		pProj->m_pOwner = pOwner;	// ref vfunc +0xa8 (IsAlive) @0x162e32
	// ref @0x162e32 tail: owner entity +0x128 suppress window -> m_fAttendExplosion (+0xd5).
	if( pOwner && gpGlobals->time < pOwner->m_flSgtcSuppressTime )
		pProj->m_fAttendExplosion = TRUE;
	pProj->pev->nextthink = gpGlobals->time + 0.15f;
	SGTC_DiagSpawn( "reetou_proj", pOwner, pProj->pev->origin, pProj->pev->scale,
		pProj->pev->body, pProj->pev->skin, pProj->pev->sequence );
	return pProj;
}

// ref helpers.c @0x110358 / glstaff.c @0xc9fea — MESSAGE_BEGIN TE_BEAMENTS loop.
void SGTC_LaserBeamEnts( CBaseEntity *pStart, CBaseEntity *pEnd, int iSprite,
	int iNoiseByte )
{
	if( !pStart || !pEnd || !iSprite )
		return;
	SGTC_DiagBeam( pStart, pEnd, iSprite, iNoiseByte );

	MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
		WRITE_BYTE( TE_BEAMENTS );
		WRITE_SHORT( pStart->entindex() );
		WRITE_SHORT( pEnd->entindex() );
		WRITE_SHORT( iSprite );
		WRITE_BYTE( 1 );		// ref framestart
		WRITE_BYTE( iNoiseByte );	// ref 5 (lance) / 6 (glstaff)
		WRITE_BYTE( 0xe6 );		// ref life byte
		WRITE_BYTE( 0x73 );		// ref width byte
		WRITE_BYTE( 0 );		// ref r
		WRITE_BYTE( 0xff );		// ref g
		WRITE_BYTE( 0 );		// ref b
		WRITE_BYTE( 255 );		// brightness (ref omits; stock beams need it)
		WRITE_BYTE( 0 );		// speed
	MESSAGE_END();
}
