// sgtc_sp_enemies.cpp — single-player enemy substitution.
//
// The StargateTC single-player maps were authored against stock Half-Life and
// place stock HL monster classes.  Only some of those classes exist in the SGTC
// link table (gargantua, zombie, alien_slave, scientists, barney); the rest are
// dropped by the engine with "No spawn function" and never appear at all.  The
// stock monsters that DO spawn render as untextured stock HL models ("rigs"),
// which is John's SP complaint.
//
// This file adds a Stargate Jaffa enemy and substitutes it, SP-only:
//   * classes that are not in the SGTC link table get a spawn shim (same idea
//     as sp_shims.cpp): the shim creates a Jaffa at the authored origin and
//     removes itself.  In multiplayer the shim removes itself without spawning
//     anything, reproducing the reference "entity not spawned" outcome.
//   * classes that are registered (monster_gargantua, monster_zombie,
//     monster_alien_slave) are replaced by SGTC_SP_EnemiesActivate() right
//     after the map entities have spawned (ServerActivate).
//
// Gate: genuine SP = the non-multiplayer rules object (CHalfLifeRules,
// deathmatch 0) and a one-client server, same gate as sp_shims.cpp.  New cvar
// sgtc_sp_enemies (default 1) switches the substitution off for parity tests.
//
// Jaffa model: models/jaffa.mdl (82 sequences, grunt-style animation labels:
// walk1/run/idle1/combatidle/standing_mp5/dieback1) with external textures in
// the companion jaffaT.mdl (engine loads it when numtextures == 0; see
// xashsrc engine/common/mod_studio.c:953).  Ranged attack reuses CZatProj so
// the SP Jaffa shoots the same visible zat wave bolt the player's zat uses.

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "gamerules.h"
#include "player.h"
#include "weapons.h"
#include "sgtc_misc.h"

// ---------------------------------------------------------------------------
// Cvar / gate
// ---------------------------------------------------------------------------
cvar_t sgtc_cvar_sp_enemies = { "sgtc_sp_enemies", "1", FCVAR_SERVER };

static bool SPEnemiesEnabled( void )
{
	return sgtc_cvar_sp_enemies.value != 0.0f
		&& g_pGameRules != NULL
		&& !g_pGameRules->IsMultiplayer()
		&& gpGlobals->maxClients <= 1;
}

void SGTC_SP_EnemiesInit( void )
{
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_sp_enemies );
}

// ---------------------------------------------------------------------------
// Jaffa monster
// ---------------------------------------------------------------------------
#define JAFFA_MODEL			"models/jaffa.mdl"
#define JAFFA_HP_NORMAL		60.0f
#define JAFFA_HP_BIG		320.0f
#define JAFFA_SCALE_BIG		1.5f
#define JAFFA_SPEED_NORMAL	190.0f	// run while chasing
#define JAFFA_SPEED_BIG		130.0f
#define JAFFA_ATTACK_DIST	260.0f	// stop and shoot from here
#define JAFFA_MELEE_DIST	85.0f
#define JAFFA_MELEE_DAMAGE	20.0f
#define JAFFA_RANGED_DELAY	1.25f
#define JAFFA_MELEE_DELAY	1.1f

class CSgtcJaffa : public CBaseMonster
{
public:
	virtual void Spawn( void );
	virtual void Precache( void );
	virtual int  BloodColor( void ) { return BLOOD_COLOR_RED; }
	virtual int  Classify( void ) { return CLASS_HUMAN_MILITARY; }
	virtual void Killed( entvars_t *pevAttacker, int iGib );

	virtual int  Variant( void ) { return 0; }	// 0 = normal Jaffa, 1 = heavy (gargantua slot)

	void EXPORT JaffaThink( void );
	void SetAnim( const char *pszLabel, int iFallback, float flFramerate );
	void FindPlayer( CBaseEntity **ppPlayer, float *pDist );
	void MeleeAttack( CBaseEntity *pTarget );
	void RangedAttack( CBaseEntity *pTarget );

	float m_flNextRanged;
	float m_flNextMelee;
	float m_flRemoveTime;
	float m_flStuckTime;
	Vector m_vecLastOrigin;
};

LINK_ENTITY_TO_CLASS( monster_sgtc_jaffa, CSgtcJaffa );

class CSgtcJaffaBig : public CSgtcJaffa
{
public:
	virtual void Spawn( void ) { CSgtcJaffa::Spawn(); }
	virtual int  Variant( void ) { return 1; }
};
LINK_ENTITY_TO_CLASS( monster_sgtc_jaffa_big, CSgtcJaffaBig );

void CSgtcJaffa::Precache( void )
{
	PRECACHE_MODEL( JAFFA_MODEL );
	PRECACHE_SOUND( "weapons/zatvide.wav" );
	PRECACHE_SOUND( "weapons/zatimpact.wav" );
	PRECACHE_SOUND( "weapons/hitbody1.wav" );
}

void CSgtcJaffa::SetAnim( const char *pszLabel, int iFallback, float flFramerate )
{
	int iSeq = LookupSequence( pszLabel );
	if( iSeq < 0 )
		iSeq = iFallback;
	if( iSeq < 0 )
		return;
	if( pev->sequence == iSeq )
		return;
	pev->sequence = iSeq;
	pev->frame = 0;
	pev->framerate = flFramerate;
	ResetSequenceInfo();
}

void CSgtcJaffa::Spawn( void )
{
	const bool bBig = ( Variant() == 1 );

	Precache();
	SET_MODEL( ENT( pev ), JAFFA_MODEL );

	pev->solid = SOLID_SLIDEBOX;
	pev->movetype = MOVETYPE_STEP;
	pev->flags |= FL_MONSTER;
	pev->takedamage = DAMAGE_AIM;
	pev->health = bBig ? JAFFA_HP_BIG : JAFFA_HP_NORMAL;
	pev->max_health = pev->health;
	pev->view_ofs = Vector( 0, 0, 28 );
	pev->yaw_speed = 20;
	pev->scale = bBig ? JAFFA_SCALE_BIG : 1.0f;
	pev->skin = bBig ? 0 : RANDOM_LONG( 0, 2 );
	pev->ideal_yaw = pev->angles.y;
	pev->animtime = gpGlobals->time;
	pev->framerate = 1.0f;

	m_bloodColor = BLOOD_COLOR_RED;
	m_flFieldOfView = 0.5f;
	m_flDistLook = 2048.0f;
	m_flDistTooFar = 1024.0f;
	m_flNextRanged = gpGlobals->time + RANDOM_FLOAT( 0.5f, 1.5f );
	m_flNextMelee = gpGlobals->time;
	m_flRemoveTime = 0.0f;
	m_flStuckTime = 0.0f;
	m_vecLastOrigin = pev->origin;

	InitBoneControllers();
	UTIL_SetSize( pev, VEC_HUMAN_HULL_MIN, VEC_HUMAN_HULL_MAX );

	// settle on the floor like StartMonster() does; origin comes from the map
	// or from the stock entity we replaced
	pev->origin.z += 1.0f;
	DROP_TO_FLOOR( ENT( pev ) );

	SetAnim( "idle1", 11, 1.0f );

	SetThink( &CSgtcJaffa::JaffaThink );
	pev->nextthink = gpGlobals->time + 0.1f;
}

void CSgtcJaffa::Killed( entvars_t *pevAttacker, int iGib )
{
	if( pev->deadflag != DEAD_NO )
		return;
	pev->health = 0.0f;
	pev->takedamage = DAMAGE_NO;
	pev->deadflag = DEAD_DYING;
	pev->velocity = Vector( 0, 0, 0 );
	if( RANDOM_LONG( 0, 1 ) == 0 )
		SetAnim( "dieback1", 29, 1.0f );
	else
		SetAnim( "dieforward", 30, 1.0f );
	m_flRemoveTime = gpGlobals->time + 2.5f;
}

void CSgtcJaffa::FindPlayer( CBaseEntity **ppPlayer, float *pDist )
{
	CBaseEntity *pBest = NULL;
	float flBest = 8192.0f;
	for( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		edict_t *e = INDEXENT( i );
		if( !e || e->free || !e->pvPrivateData )
			continue;
		CBaseEntity *pEnt = CBaseEntity::Instance( e );
		if( !pEnt || !pEnt->IsPlayer() || !pEnt->IsAlive() )
			continue;
		const float flDist = ( pEnt->pev->origin - pev->origin ).Length();
		if( flDist < flBest )
		{
			flBest = flDist;
			pBest = pEnt;
		}
	}
	if( ppPlayer ) *ppPlayer = pBest;
	if( pDist ) *pDist = flBest;
}

void CSgtcJaffa::MeleeAttack( CBaseEntity *pTarget )
{
	UTIL_MakeVectors( pev->angles );
	Vector vecSrc = pev->origin + pev->view_ofs;
	Vector vecEnd = vecSrc + gpGlobals->v_forward * 70.0f;

	TraceResult tr;
	UTIL_TraceLine( vecSrc, vecEnd, dont_ignore_monsters, ENT( pev ), &tr );

	SetAnim( "frontkick", 14, 1.0f );
	if( tr.pHit && CBaseEntity::Instance( tr.pHit ) == pTarget )
	{
		ClearMultiDamage();
		pTarget->TraceAttack( pev, JAFFA_MELEE_DAMAGE, gpGlobals->v_forward, &tr, DMG_CLUB );
		ApplyMultiDamage( pev, pev );
		EMIT_SOUND( ENT( pev ), CHAN_ITEM, "weapons/hitbody1.wav", 1.0f, ATTN_NORM );
	}
	m_flNextMelee = gpGlobals->time + JAFFA_MELEE_DELAY;
}

void CSgtcJaffa::RangedAttack( CBaseEntity *pTarget )
{
	UTIL_MakeVectors( pev->angles );
	Vector vecSrc = pev->origin + pev->view_ofs + gpGlobals->v_forward * 20.0f;
	vecSrc.z += 4.0f;
	Vector vecAim = pTarget->pev->origin + pTarget->pev->view_ofs - vecSrc;
	vecAim = vecAim.Normalize();

	const Vector vecAngles = UTIL_VecToAngles( vecAim );
	CZatProj *pProj = CZatProj::CreateZatProj( vecSrc, vecAngles, this, NULL );
	if( pProj )
	{
		pProj->pev->v_angle = vecAngles;
		pProj->pev->velocity = vecAim * 1600.0f;
		// w_projreetou.mdl has a single sequence; the ref's pev->sequence=4 is
		// out of range on the shipped model (engine warning spam), so use 0.
		pProj->pev->sequence = 0;
	}
	SetAnim( "standing_mp5", 44, 1.0f );
	EMIT_SOUND( ENT( pev ), CHAN_WEAPON, "weapons/zatvide.wav", 0.8f, ATTN_NORM );
	m_flNextRanged = gpGlobals->time + JAFFA_RANGED_DELAY;
}

void CSgtcJaffa::JaffaThink( void )
{
	if( pev->deadflag != DEAD_NO )
	{
		pev->velocity = Vector( 0, 0, 0 );
		StudioFrameAdvance();
		if( m_flRemoveTime != 0.0f && gpGlobals->time > m_flRemoveTime )
		{
			UTIL_Remove( this );
			return;
		}
		pev->nextthink = gpGlobals->time + 0.1f;
		return;
	}

	StudioFrameAdvance();

	CBaseEntity *pTarget = NULL;
	float flDist = 8192.0f;
	FindPlayer( &pTarget, &flDist );

	if( !pTarget )
	{
		pev->velocity = Vector( 0, 0, 0 );
		SetAnim( "idle1", 11, 1.0f );
		pev->nextthink = gpGlobals->time + 0.25f;
		return;
	}

	Vector vecToTarget = pTarget->pev->origin - pev->origin;
	Vector vecDir = vecToTarget.Normalize();

	// face the player
	pev->ideal_yaw = VecToYaw( vecToTarget );
	ChangeYaw( pev->yaw_speed );

	// line of sight to the player's chest
	Vector vecEye = pev->origin + pev->view_ofs;
	Vector vecTargetEye = pTarget->pev->origin + pTarget->pev->view_ofs;
	TraceResult tr;
	UTIL_TraceLine( vecEye, vecTargetEye, dont_ignore_monsters, ENT( pev ), &tr );
	const bool bLOS = ( tr.flFraction >= 1.0f ||
		CBaseEntity::Instance( tr.pHit ) == pTarget );

	if( flDist > JAFFA_ATTACK_DIST || !bLOS )
	{
		// chase: direct step movement; the engine's step physics applies gravity
		// and ground friction.  MOVETYPE_STEP does not do path finding, which is
		// acceptable for the flat SP map rooms; when a wall stops us for a
		// moment, sidestep for a bit so we do not grind into a corner forever.
		Vector vecMove( vecDir.x, vecDir.y, 0.0f );
		if( vecMove.Length() > 0.0f )
			vecMove = vecMove.Normalize();

		const float flSpeed = Variant() ? JAFFA_SPEED_BIG : JAFFA_SPEED_NORMAL;
		if( ( pev->origin - m_vecLastOrigin ).Length() < 1.0f &&
			gpGlobals->time > m_flNextRanged )
		{
			m_flStuckTime = gpGlobals->time + 0.4f;
		}
		if( m_flStuckTime > gpGlobals->time )
		{
			vecMove = Vector( -vecMove.y, vecMove.x, 0.0f );
		}
		m_vecLastOrigin = pev->origin;
		pev->velocity = vecMove * flSpeed;
		SetAnim( "run", 1, 1.0f );
	}
	else
	{
		pev->velocity = Vector( 0, 0, 0 );
		if( flDist <= JAFFA_MELEE_DIST && gpGlobals->time >= m_flNextMelee )
		{
			MeleeAttack( pTarget );
		}
		else if( gpGlobals->time >= m_flNextRanged )
		{
			RangedAttack( pTarget );
		}
		else
		{
			SetAnim( "combatidle", 13, 1.0f );
		}
	}

	pev->nextthink = gpGlobals->time + 0.1f;
}

// ---------------------------------------------------------------------------
// Spawn shims for stock monster classes that the SGTC link table does not
// register (so the engine drops them): the shim creates a Jaffa at the
// authored origin and removes itself.  In MP it removes itself without
// spawning anything, matching the reference "no spawn function" outcome.
// ---------------------------------------------------------------------------
#define SP_MONSTER_SHIM( shimClass, jaffaClass )			\
	class shimClass : public CBaseEntity						\
	{														\
	public:													\
		void Spawn( void )									\
		{													\
			if( !SPEnemiesEnabled() )						\
			{												\
				UTIL_Remove( this );						\
				return;										\
			}												\
			CBaseEntity *pJaffa = CBaseEntity::Create( jaffaClass, \
				pev->origin, pev->angles, NULL );			\
			if( pJaffa )									\
			{												\
				pJaffa->pev->targetname = pev->targetname;	\
				pJaffa->pev->target = pev->target;			\
				ALERT( at_console, "SGTC_SP_ENEMIES {\"cmd\":\"shim\",\"old\":\"%s\",\"new\":\"%s\",\"origin\":[%.1f,%.1f,%.1f]}\n", \
					STRING( pev->classname ), jaffaClass,	\
					pev->origin.x, pev->origin.y, pev->origin.z ); \
			}												\
			UTIL_Remove( this );							\
		}													\
	};

SP_MONSTER_SHIM( CSpShimMonsterHGrun, "monster_sgtc_jaffa" )
SP_MONSTER_SHIM( CSpShimMonsterAGrunt, "monster_sgtc_jaffa" )
SP_MONSTER_SHIM( CSpShimMonsterBullsquid, "monster_sgtc_jaffa" )
SP_MONSTER_SHIM( CSpShimMonsterController, "monster_sgtc_jaffa" )
SP_MONSTER_SHIM( CSpShimMonsterHAssassin, "monster_sgtc_jaffa" )
SP_MONSTER_SHIM( CSpShimMonsterHoundeye, "monster_sgtc_jaffa" )
SP_MONSTER_SHIM( CSpShimMonsterTentacle, "monster_sgtc_jaffa_big" )
SP_MONSTER_SHIM( CSpShimMonsterBigMom, "monster_sgtc_jaffa_big" )
SP_MONSTER_SHIM( CSpShimMonsterApache, "monster_sgtc_jaffa" )
SP_MONSTER_SHIM( CSpShimMonsterHeadcrab, "monster_sgtc_jaffa" )

LINK_ENTITY_TO_CLASS( monster_hgrunt, CSpShimMonsterHGrun );
LINK_ENTITY_TO_CLASS( monster_agrunt, CSpShimMonsterAGrunt );
LINK_ENTITY_TO_CLASS( monster_bullsquid, CSpShimMonsterBullsquid );
LINK_ENTITY_TO_CLASS( monster_controller, CSpShimMonsterController );
LINK_ENTITY_TO_CLASS( monster_hassassin, CSpShimMonsterHAssassin );
LINK_ENTITY_TO_CLASS( monster_houndeye, CSpShimMonsterHoundeye );
LINK_ENTITY_TO_CLASS( monster_tentacle, CSpShimMonsterTentacle );
LINK_ENTITY_TO_CLASS( monster_bigmom, CSpShimMonsterBigMom );
LINK_ENTITY_TO_CLASS( monster_bigmomma, CSpShimMonsterBigMom );
LINK_ENTITY_TO_CLASS( monster_apache, CSpShimMonsterApache );
LINK_ENTITY_TO_CLASS( monster_headcrab, CSpShimMonsterHeadcrab );

// ---------------------------------------------------------------------------
// Replacement of registered stock hostiles (gargantua/zombie/alien_slave)
// right after map entities spawned (called from ServerActivate).
// ---------------------------------------------------------------------------
struct spEnemyMap_t
{
	const char *pszStock;
	const char *pszJaffa;
};

static const spEnemyMap_t s_spEnemyMap[] =
{
	{ "monster_gargantua", "monster_sgtc_jaffa_big" },
	{ "monster_zombie", "monster_sgtc_jaffa" },
	{ "monster_alien_slave", "monster_sgtc_jaffa" },
	{ "monster_vortigaunt", "monster_sgtc_jaffa" },
};

void SGTC_SP_EnemiesActivate( void )
{
	if( !SPEnemiesEnabled() )
		return;

	const int iMax = gpGlobals->maxEntities;
	int iReplaced = 0;

	for( int i = 1; i < iMax; ++i )
	{
		edict_t *e = INDEXENT( i );
		if( !e || e->free || !e->pvPrivateData )
			continue;
		const char *pszClass = STRING( e->v.classname );
		if( !pszClass || !*pszClass )
			continue;

		const char *pszNew = NULL;
		for( int k = 0; k < (int)( sizeof( s_spEnemyMap ) / sizeof( s_spEnemyMap[0] ) ); ++k )
		{
			if( !strcmp( pszClass, s_spEnemyMap[k].pszStock ) )
			{
				pszNew = s_spEnemyMap[k].pszJaffa;
				break;
			}
		}
		if( !pszNew )
			continue;

		// capture before create: Create() may reallocate/consume the edict table
		const Vector vecOrigin = e->v.origin;
		const Vector vecAngles = e->v.angles;
		const string_t targetname = e->v.targetname;
		const string_t target = e->v.target;

		CBaseEntity *pJaffa = CBaseEntity::Create( pszNew, vecOrigin, vecAngles, NULL );
		if( pJaffa )
		{
			pJaffa->pev->targetname = targetname;
			pJaffa->pev->target = target;
			++iReplaced;
			ALERT( at_console, "SGTC_SP_ENEMIES {\"cmd\":\"replace\",\"old\":\"%s\",\"new\":\"%s\",\"origin\":[%.1f,%.1f,%.1f]}\n",
				pszClass, pszNew, vecOrigin.x, vecOrigin.y, vecOrigin.z );
		}
		UTIL_Remove( CBaseEntity::Instance( e ) );
	}

	ALERT( at_console, "SGTC_SP_ENEMIES {\"cmd\":\"activate\",\"map\":\"%s\",\"replaced\":%d}\n",
		STRING( gpGlobals->mapname ), iReplaced );
}
