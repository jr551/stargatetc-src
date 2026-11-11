// sgtc_structs.cpp — SGTC team-structure entities (ref CGouStruct/CTauStruct family).

#include "extdll.h"
#include "sgtc_diag.h"
#include "util.h"
#include "cbase.h"
#include "gamerules.h"
#include "sgtc_rules.h"
#include "sgtc_structs.h"

int SGTC_Round_intconstrain( int lo, int hi, int v );

extern cvar_t sgtc_cvar_structhp;
extern int sgtc_G_players;
extern int sgtc_T_players;
extern int gmsgGtickets;
extern int gmsgTtickets;

static int SGTC_StructPct( int health, int maxHP )
{
	if( maxHP <= 0 )
		return 0;
	return ( health * 100 ) / maxHP;
}

// #36: pev->health is the real structure HP and is no longer reset in Think.
// A live structure is kept at or above this floor: TakeDamage clamps a lethal
// hit to leave exactly this much (so CBaseMonster::TakeDamage never reaches
// Killed()/GibMonster on the box), and Think completes the objective once
// health is down to it.
static const float kSgtcStructFloorHP = 1.0f;

static float SGTC_StructClampDamage( const entvars_t *pev, float flDamage )
{
	const float flMaxTake = pev->health - kSgtcStructFloorHP;
	return ( flDamage > flMaxTake ) ? flMaxTake : flDamage;
}

static BOOL SGTC_StructDepleted( const entvars_t *pev )
{
	return pev->health <= kSgtcStructFloorHP;
}

static void SGTC_StructBroadcastPct( int msg, int pct )
{
	if( msg <= 0 )
		return;
	MESSAGE_BEGIN( MSG_ALL, msg );
		WRITE_BYTE( pct );
	MESSAGE_END();
}

static int SGTC_StructResolveHP( int teamPlayers, BOOL bGoa )
{
	(void)bGoa;
	if( sgtc_cvar_structhp.value > 0.0f )
	{
		return SGTC_Round_intconstrain( 50, 10000,
			(int)sgtc_cvar_structhp.value );
	}

	if( teamPlayers > 0 )
	{
		int hp = teamPlayers;
		hp = ( hp << 2 ) + teamPlayers;	// *5
		hp = ( hp << 2 ) + hp;		// *25
		hp <<= 4;			// *400, ref 2000 * count
		return hp;
	}

	return 2000;	// ref default when team count <= 0
}

static void SGTC_StructRemoveAll( const char *pszClassname )
{
	CBaseEntity *pEnt = NULL;
	while( ( pEnt = UTIL_FindEntityByClassname( pEnt, pszClassname ) ) != NULL )
		UTIL_Remove( pEnt );
}

static void SGTC_StructEmitBreakModel( const Vector &origin, const Vector &size,
	const Vector &vel, int modelIndex, int iRandomness )
{
	MESSAGE_BEGIN( MSG_PAS, SVC_TEMPENTITY, origin );
		WRITE_BYTE( 0x6c );	// ref custom breakmodel TE id
		WRITE_COORD( origin.x );
		WRITE_COORD( origin.y );
		WRITE_COORD( origin.z );
		WRITE_COORD( size.x );
		WRITE_COORD( size.y );
		WRITE_COORD( size.z );
		WRITE_COORD( vel.x );
		WRITE_COORD( vel.y );
		WRITE_COORD( vel.z );
		WRITE_BYTE( iRandomness );
		WRITE_SHORT( modelIndex );
		WRITE_BYTE( 4 );	// ref count
		WRITE_BYTE( 25 );	// ref life 0x19
		WRITE_BYTE( 1 );	// ref flags
	MESSAGE_END();
}

static void SGTC_StructEmitGlowSprite( const Vector &origin )
{
	MESSAGE_BEGIN( MSG_PAS, SVC_TEMPENTITY, origin );
		WRITE_BYTE( TE_GLOWSPRITE );
		WRITE_COORD( origin.x );
		WRITE_COORD( origin.y );
		WRITE_COORD( origin.z );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 255 );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 7 );
		WRITE_BYTE( 0 );
	MESSAGE_END();
}

static void SGTC_StructTauDeathSparks( const Vector &origin )
{
	// ref @0xd307b: bound re-rolled EVERY iteration (RANDOM_LONG(0,5)+4 vs i).
	for( int i = 0; i <= RANDOM_LONG( 0, 5 ) + 4; i++ )
	{
		Vector spark = origin;
		spark.x += RANDOM_LONG( 0, 64 );
		spark.y -= 32.0f;
		spark.z += RANDOM_LONG( 0, 100 );
		{ Vector vDbg = spark ; SGTC_DiagSparks( "structs", vDbg ); UTIL_Sparks( vDbg ); }
	}
}

template<class TActual>
static void SGTC_StructDeathCommon( TActual *pStruct, const char *pszSubClass,
	int goalTeam, int ticketMsg, const Vector &fxOrigin, BOOL bTau )
{
	if( pStruct->m_bDeathHandled )
		return;

	pStruct->pev->health = 1.0f;	// ref clamps before FX when health <= 0 @0xd1c36 / Tau path
	pStruct->ResetSequenceInfo();
	pStruct->pev->frame = 1.0f;
	pStruct->pev->framerate = 0.2f;
	SGTC_StructRemoveAll( pszSubClass );

	int pitch = RANDOM_LONG( 0, 29 ) + 95;
	if( pitch > 97 && pitch < 103 )
		pitch = 100;

	if( bTau )
	{
		int soundPick = RANDOM_LONG( 0, 2 );
		const char *pszSound = "buttons/spark1.wav";
		if( soundPick == 1 )
			pszSound = "buttons/spark2.wav";
		else if( soundPick == 2 )
			pszSound = "buttons/spark3.wav";
		EMIT_SOUND_DYN( ENT( pStruct->pev ), CHAN_BODY, pszSound,
			1.0f, 0.8f, 0, pitch );
		SGTC_StructTauDeathSparks( fxOrigin );
	}
	else
	{
		const char *pszSound = ( RANDOM_LONG( 0, 1 ) == 0 )
			? "debris/bustglass1.wav" : "debris/bustglass2.wav";
		EMIT_SOUND_DYN( ENT( pStruct->pev ), CHAN_VOICE, pszSound,
			0.9f, 0.8f, 0, pitch );
	}

	SGTC_StructEmitGlowSprite( fxOrigin );
	const int breakRand = bTau ? 10 : 20;	// ref Tau 0x0a @0xd31f2, Gou 0x14 @0xd1f11
	SGTC_StructEmitBreakModel( fxOrigin, Vector( 8, 8, 8 ), Vector( 0, 0, -200 ),
		pStruct->m_iGlassGibsModel, breakRand );

	if( CStargateTeamDM *pRules = SGTC_GetTeamDMRules() )
		pRules->Goal( goalTeam );

	pStruct->m_bDeathHandled = TRUE;	// ref sets after Goal @0xd1fab / @0xd3258
	SGTC_StructBroadcastPct( ticketMsg, 0 );
}

static void SGTC_StructThinkActual( CGouStructActual *pStruct )
{
	pStruct->pev->enemy = NULL;
	if( pStruct->m_bDeathHandled )
	{
		pStruct->pev->nextthink = gpGlobals->time + 0.3f;
		return;
	}

	const int health = (int)pStruct->pev->health;
	const int maxHP = pStruct->m_iMaxHP;
	if( health > pStruct->m_iLastHealthSnapshot )
	{
		pStruct->m_flHealScheduleTime = gpGlobals->time + 0.3f;	// ref @0xd1b76
		if( gpGlobals->time < pStruct->m_flHealScheduleTime )
		{
			pStruct->pev->nextthink = pStruct->m_flHealScheduleTime;
			return;
		}
	}

	if( health == pStruct->m_iLastHealthSnapshot && !SGTC_StructDepleted( pStruct->pev ) )
	{
		pStruct->pev->nextthink = gpGlobals->time + 0.3f;
		return;
	}

	if( SGTC_StructDepleted( pStruct->pev ) )
	{
		Vector fx = pStruct->pev->origin;
		fx.x -= 3.0f;
		fx.y += 80.0f;
		fx.z -= 200.0f;
		SGTC_StructDeathCommon( pStruct, "struct_goa_actual_sub", 2, gmsgGtickets, fx, FALSE );
		return;
	}

	const int pct = SGTC_StructPct( health, maxHP );
	const int healGate = pStruct->m_iHealReadyFlag;
	if( health >= maxHP && healGate != 0 )
	{
		pStruct->pev->team = 0;
		pStruct->m_iLastHealthSnapshot = health;
		pStruct->m_iHealReadyFlag = 0;
		SGTC_StructBroadcastPct( gmsgGtickets, 0 );
	}
	else if( pct > healGate )
	{
		pStruct->pev->team = 0;
		pStruct->m_iLastHealthSnapshot = health;
		pStruct->m_iHealReadyFlag = pct;
		SGTC_StructBroadcastPct( gmsgGtickets, pct );
	}
	else if( pct <= 0 )
		SGTC_StructBroadcastPct( gmsgGtickets, 1 );
	else
		SGTC_StructBroadcastPct( gmsgGtickets, pct );

	if( pStruct->m_iHealReadyFlag <= 0 )
		pStruct->m_iHealReadyFlag = 1;
	pStruct->m_iLastHealthSnapshot = health;
	pStruct->pev->nextthink = gpGlobals->time + 0.3f;
}

static void SGTC_StructThinkActual( CTauStructActual *pStruct )
{
	pStruct->pev->enemy = NULL;
	if( pStruct->m_bDeathHandled )
	{
		pStruct->pev->nextthink = gpGlobals->time + 0.3f;
		return;
	}

	const int health = (int)pStruct->pev->health;
	const int maxHP = pStruct->m_iMaxHP;
	if( health > pStruct->m_iLastHealthSnapshot )
	{
		pStruct->m_flHealScheduleTime = gpGlobals->time + 0.3f;	// ref @0xd1b76
		if( gpGlobals->time < pStruct->m_flHealScheduleTime )
		{
			pStruct->pev->nextthink = pStruct->m_flHealScheduleTime;
			return;
		}
	}

	if( health == pStruct->m_iLastHealthSnapshot && !SGTC_StructDepleted( pStruct->pev ) )
	{
		pStruct->pev->nextthink = gpGlobals->time + 0.3f;
		return;
	}

	if( SGTC_StructDepleted( pStruct->pev ) )
	{
		Vector fx = pStruct->pev->origin;
		fx.x -= 3.0f;
		fx.y += 80.0f;
		fx.z -= 200.0f;
		SGTC_StructDeathCommon( pStruct, "struct_tau_actual_sub", 1, gmsgTtickets, fx, TRUE );
		return;
	}

	const int pct = SGTC_StructPct( health, maxHP );
	const int healGate = pStruct->m_iHealReadyFlag;
	if( health >= maxHP && healGate != 0 )
	{
		pStruct->pev->team = 0;
		pStruct->m_iLastHealthSnapshot = health;
		pStruct->m_iHealReadyFlag = 0;
		SGTC_StructBroadcastPct( gmsgTtickets, 0 );
	}
	else if( pct > healGate )
	{
		pStruct->pev->team = 0;
		pStruct->m_iLastHealthSnapshot = health;
		pStruct->m_iHealReadyFlag = pct;
		SGTC_StructBroadcastPct( gmsgTtickets, pct );
	}
	else if( pct <= 0 )
		SGTC_StructBroadcastPct( gmsgTtickets, 1 );
	else
		SGTC_StructBroadcastPct( gmsgTtickets, pct );

	if( pStruct->m_iHealReadyFlag <= 0 )
		pStruct->m_iHealReadyFlag = 1;
	pStruct->m_iLastHealthSnapshot = health;
	pStruct->pev->nextthink = gpGlobals->time + 0.3f;
}

void CGouStruct::Spawn( void )
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	pev->health = 100.0f;
	pev->renderamt = 0.0f;
	pev->rendermode = kRenderTransAdd;
}

void CGouStruct::Think( void )
{
	pev->nextthink = gpGlobals->time + 10.0f;
}

void CTauStruct::Spawn( void )
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	pev->health = 100.0f;
	pev->renderamt = 0.0f;
	pev->rendermode = kRenderTransAdd;
}

void CTauStruct::Think( void )
{
	pev->nextthink = gpGlobals->time + 10.0f;
}

void CGouStructActual::Precache( void )
{
	PRECACHE_MODEL( "models/goabox.mdl" );
	PRECACHE_MODEL( "models/goaboxg.mdl" );
	m_iGlassGibsModel = PRECACHE_MODEL( "models/glassgibs.mdl" );
	PRECACHE_SOUND( "debris/bustglass1.wav" );
	PRECACHE_SOUND( "debris/bustglass2.wav" );
}

void CGouStructActual::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/goabox.mdl" );
	UTIL_SetSize( pev, Vector( 0, 0, -32 ), Vector( 100, 32, 0 ) );
	pev->movetype = MOVETYPE_FLY;
	pev->solid = SOLID_BBOX;
	pev->takedamage = DAMAGE_YES;
	m_flNextAttack = 0.0f;
	m_iHealReadyFlag = -1;

	m_iMaxHP = SGTC_StructResolveHP( sgtc_G_players, TRUE );
	m_iLastHealthSnapshot = m_iMaxHP;
	pev->health = (float)m_iMaxHP;
	pev->frame = 0.0f;
	MonsterInit();
}

void CGouStructActual::Think( void )
{
	if( !m_bDeathHandled )
	{
		Vector mins = pev->origin + Vector( -33, -13, -20 );
		Vector maxs = pev->origin + Vector( 9, 75, 20 );
		UTIL_Bubbles( mins, maxs, 50 );	// ref @0xd1b2c, no HP gate
	}

	SGTC_StructThinkActual( this );
}

int CGouStructActual::TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker,
	float flDamage, int bitsDamageType )
{
	if( m_bDeathHandled )
		return 0;

	if( pevAttacker && (int)pevAttacker->team == SGTC_ENTTEAM_GOAULD )
		flDamage = 1.0f;
	else if( !( bitsDamageType & DMG_BULLET ) && !( bitsDamageType & DMG_CLUB ) )
		return 0;

	flDamage = SGTC_StructClampDamage( pev, flDamage );
	if( flDamage <= 0.0f )
		return 0;

	return CBaseMonster::TakeDamage( pevInflictor, pevAttacker, flDamage, bitsDamageType );
}

void CGouStructActual::TraceAttack( entvars_t *pevAttacker, float flDamage,
	Vector vecDir, TraceResult *ptr, int bitsDamageType )
{
	UTIL_Ricochet( ptr->vecEndPos, 1.0f );
	CBaseMonster::TraceAttack( pevAttacker, flDamage, -vecDir, ptr, bitsDamageType );
}

void CTauStructActual::Precache( void )
{
	PRECACHE_MODEL( "models/taubox.mdl" );
	PRECACHE_MODEL( "models/tauboxg.mdl" );
	PRECACHE_MODEL( "models/glassgibs.mdl" );
	PRECACHE_SOUND( "debris/bustglass1.wav" );
	PRECACHE_SOUND( "debris/bustglass2.wav" );
	PRECACHE_SOUND( "buttons/spark1.wav" );
	PRECACHE_SOUND( "buttons/spark2.wav" );
	PRECACHE_SOUND( "buttons/spark3.wav" );
	m_iGlassGibsModel = PRECACHE_MODEL( "models/glassgibs.mdl" );
}

void CTauStructActual::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/taubox.mdl" );
	UTIL_SetSize( pev, Vector( 0, 0, -37 ), Vector( 100, 37, 0 ) );
	pev->movetype = MOVETYPE_FLY;
	pev->solid = SOLID_BBOX;
	pev->takedamage = DAMAGE_AIM;
	m_flNextAttack = 0.0f;
	m_iHealReadyFlag = -1;

	m_iMaxHP = SGTC_StructResolveHP( sgtc_T_players, FALSE );
	m_iLastHealthSnapshot = m_iMaxHP;
	pev->health = (float)m_iMaxHP;
	pev->frame = 0.0f;
	MonsterInit();
}

void CTauStructActual::Think( void )
{
	if( (int)pev->health <= 49 && RANDOM_LONG( 0, 2 ) == 1 )	// ref @0xd29c0..0xd29f1
	{
		Vector spark = pev->origin;
		spark.x += RANDOM_LONG( 0, 64 );
		spark.y -= 32.0f;
		spark.z += RANDOM_LONG( 0, 100 );
		UTIL_Sparks( spark );

		int pitch = RANDOM_LONG( 0, 29 ) + 95;
		if( pitch > 97 && pitch < 103 )
			pitch = 100;
		int soundPick = RANDOM_LONG( 0, 2 );
		const char *pszSound = "buttons/spark1.wav";
		if( soundPick == 1 )
			pszSound = "buttons/spark2.wav";
		else if( soundPick == 2 )
			pszSound = "buttons/spark3.wav";
		EMIT_SOUND_DYN( ENT( pev ), CHAN_BODY, pszSound, 1.0f, 0.8f, 0, pitch );
	}

	SGTC_StructThinkActual( this );
}

int CTauStructActual::TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker,
	float flDamage, int bitsDamageType )
{
	if( m_bDeathHandled )
		return 0;

	if( pevAttacker && (int)pevAttacker->team == SGTC_ENTTEAM_TORRI )
		flDamage = 1.0f;
	else if( !( bitsDamageType & DMG_BULLET ) && !( bitsDamageType & DMG_CLUB ) )
		return 0;

	flDamage = SGTC_StructClampDamage( pev, flDamage );
	if( flDamage <= 0.0f )
		return 0;

	return CBaseMonster::TakeDamage( pevInflictor, pevAttacker, flDamage, bitsDamageType );
}

void CTauStructActual::TraceAttack( entvars_t *pevAttacker, float flDamage,
	Vector vecDir, TraceResult *ptr, int bitsDamageType )
{
	UTIL_Ricochet( ptr->vecEndPos, 0.8f );
	CBaseMonster::TraceAttack( pevAttacker, flDamage, -vecDir, ptr, bitsDamageType );
}

// The *_actual_sub boxes are static visuals: Spawn never arms pev->nextthink, so
// the Think overrides this port used to carry (a 0.3 s idle re-arm, and a 0.5 s
// TE_GLOWSPRITE pulse for the tau box) never ran and were removed (#98).
void CGouStructActualsb::Spawn( void )
{
	SET_MODEL( ENT( pev ), "models/goaboxg.mdl" );
	pev->movetype = MOVETYPE_FLY;
	pev->solid = SOLID_NOT;
	pev->takedamage = DAMAGE_NO;
	pev->renderamt = 120.0f;
	pev->rendermode = kRenderTransTexture;
}

void CTauStructActualsb::Spawn( void )
{
	SET_MODEL( ENT( pev ), "models/tauboxg.mdl" );
	pev->movetype = MOVETYPE_FLY;
	pev->solid = SOLID_NOT;
	pev->takedamage = DAMAGE_NO;
	pev->renderfx = kRenderFxNone;
	pev->renderamt = 180.0f;
	pev->rendercolor = Vector( 0, 255, 0 );
}

LINK_ENTITY_TO_CLASS( struct_goa, CGouStruct );
LINK_ENTITY_TO_CLASS( struct_tau, CTauStruct );
LINK_ENTITY_TO_CLASS( struct_goa_actual, CGouStructActual );
LINK_ENTITY_TO_CLASS( struct_tau_actual, CTauStructActual );
LINK_ENTITY_TO_CLASS( struct_goa_actual_sub, CGouStructActualsb );
LINK_ENTITY_TO_CLASS( struct_tau_actual_sub, CTauStructActualsb );
