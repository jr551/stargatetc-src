// sgtc_mech_state.cpp — build-13 observation-based mechanics state registry.
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "sgtc_mech_state.h"
#include "../game_shared/sgtc_mapmeta_gen.h"

#define SGTC_MECH_MAX_GATES 32
#define SGTC_MECH_MAX_RINGS 16
#define SGTC_MECH_MAX_DOORS 48

typedef struct
{
	char id[48];
	int entindex;
	int state;
	int irisOpen;
	int danger;
	float cooldown;
	float lastUse;
	float whooshUntil;
	int destinationId;
	int teamOnly;
	int hazardActive;
	float origin[3];
	float destOrigin[3];
} sgtc_mech_gate_rec_t;

typedef struct
{
	char id[48];
	int entindex;
	int state;
	float cooldown;
	float lastUse;
	int destinationId;
	int teamOnly;
	int hazardActive;
	float origin[3];
} sgtc_mech_ring_rec_t;

typedef struct
{
	char id[48];
	int entindex;
	int state;
	int locked;
} sgtc_mech_door_rec_t;

static sgtc_mech_gate_rec_t s_gates[SGTC_MECH_MAX_GATES];
static int s_numGates = 0;
static sgtc_mech_ring_rec_t s_rings[SGTC_MECH_MAX_RINGS];
static int s_numRings = 0;
static sgtc_mech_door_rec_t s_doors[SGTC_MECH_MAX_DOORS];
static int s_numDoors = 0;
#define SGTC_MECH_MAX_OBJECTIVES 32
static sgtc_mech_objective_t s_objectives[SGTC_MECH_MAX_OBJECTIVES];
static int s_numObjectives = 0;
static double s_flNextScan = 0.0;

static BOOL NameStartsWith( const char *psz, const char *pszPrefix )
{
	return psz && pszPrefix && !strncmp( psz, pszPrefix, strlen( pszPrefix ) );
}

static BOOL SendinelTarget( const char *psz )
{
	if( !psz || !psz[0] )
		return TRUE;
	return !strcmp( psz, "Nowhere" ) || !strcmp( psz, "nowhere" ) || !strcmp( psz, "nothing" );
}

static int DestIndexForTarget( const char *pszTarget )
{
	if( !pszTarget || !pszTarget[0] )
		return 0;
	edict_t *pDest = FIND_ENTITY_BY_TARGETNAME( NULL, pszTarget );
	if( FNullEnt( pDest ) )
		return 0;
	return ENTINDEX( pDest );
}

void SGTC_Mech_Init( void )
{
	s_numGates = s_numRings = s_numDoors = 0;
	s_flNextScan = 0.0;
}

void SGTC_Mech_NoteTeleport( CBaseEntity *pTrigger, CBaseEntity *pPlayer, const char *pszDest )
{
	if( !pTrigger || !pTrigger->pev )
		return;
	const char *pszTrig = pTrigger->pev->targetname ? STRING( pTrigger->pev->targetname ) : "";
	const int iDest = DestIndexForTarget( pszDest );
	if( NameStartsWith( pszTrig, "g1tele" ) || NameStartsWith( pszTrig, "g2tele" ) ||
		NameStartsWith( pszTrig, "rg1_tele" ) || NameStartsWith( pszTrig, "rg2_tele" ) ||
		NameStartsWith( pszTrig, "gtele" ) )
	{
		for( int i = 0; i < s_numGates; ++i )
		{
			if( !strcmp( s_gates[i].id, pszTrig ) )
			{
				s_gates[i].lastUse = gpGlobals->time;
				s_gates[i].whooshUntil = gpGlobals->time + 1.0f;
				s_gates[i].destinationId = iDest;
				s_gates[i].state = 4;	// open/just used
				break;
			}
		}
	}
	else if( NameStartsWith( pszTrig, "ring" ) || NameStartsWith( pszTrig, "rg" ) )
	{
		for( int i = 0; i < s_numRings; ++i )
		{
			if( !strcmp( s_rings[i].id, pszTrig ) )
			{
				s_rings[i].lastUse = gpGlobals->time;
				s_rings[i].state = 5;	// cooldown after use
				s_rings[i].destinationId = iDest;
				break;
			}
		}
	}
	(void)pPlayer;
}

static void MechScanGates( void )
{
	s_numGates = 0;
	CBaseEntity *pEnt = NULL;
	while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "trigger_teleport" ) ) != NULL && s_numGates < SGTC_MECH_MAX_GATES )
	{
		const char *pszId = pEnt->pev->targetname ? STRING( pEnt->pev->targetname ) : "";
		if( !pszId[0] )
			continue;
		const BOOL bRing = NameStartsWith( pszId, "ring" ) || NameStartsWith( pszId, "ringtele" );
		const BOOL bGate = NameStartsWith( pszId, "g1tele" ) || NameStartsWith( pszId, "g2tele" ) ||
			NameStartsWith( pszId, "rg1_tele" ) || NameStartsWith( pszId, "rg2_tele" ) ||
			NameStartsWith( pszId, "gtele" ) || NameStartsWith( pszId, "tele" );
		if( !bGate && !bRing )
			continue;
		const char *pszTarget = pEnt->pev->target ? STRING( pEnt->pev->target ) : "";
		const BOOL bMasterOn = UTIL_IsMasterTriggered( ( (CBaseToggle *)pEnt )->m_sMaster, NULL ) ? TRUE : FALSE;
		const BOOL bArmed = ( !SendinelTarget( pszTarget ) && bMasterOn ) ? TRUE : FALSE;
		BOOL bDuplicate = FALSE;
		if( bRing )
		{
			for( int i = 0; i < s_numRings; ++i )
				if( !strcmp( s_rings[i].id, pszId ) ) { bDuplicate = TRUE; break; }
		}
		else
		{
			for( int i = 0; i < s_numGates; ++i )
				if( !strcmp( s_gates[i].id, pszId ) ) { bDuplicate = TRUE; break; }
		}
		if( bDuplicate )
			continue;
		if( bRing && s_numRings < SGTC_MECH_MAX_RINGS )
		{
			sgtc_mech_ring_rec_t &rr = s_rings[s_numRings];
			memset( &rr, 0, sizeof( rr ) );
			strlcpy( rr.id, pszId, sizeof( rr.id ) );
			rr.entindex = ENTINDEX( ENT( pEnt->pev ) );
			rr.state = bArmed ? 2 : 1;
			rr.destinationId = bArmed ? DestIndexForTarget( pszTarget ) : 0;
			rr.teamOnly = SGTC_EntityTeamFilter( pEnt );
			rr.origin[0] = ( pEnt->pev->absmin.x + pEnt->pev->absmax.x ) * 0.5f;
			rr.origin[1] = ( pEnt->pev->absmin.y + pEnt->pev->absmax.y ) * 0.5f;
			rr.origin[2] = ( pEnt->pev->absmin.z + pEnt->pev->absmax.z ) * 0.5f;
			++s_numRings;
			continue;
		}
		if( !bGate )
			continue;
		sgtc_mech_gate_rec_t &r = s_gates[s_numGates];
		memset( &r, 0, sizeof( r ) );
		strlcpy( r.id, pszId, sizeof( r.id ) );
		r.entindex = ENTINDEX( ENT( pEnt->pev ) );
		r.irisOpen = -1;
		r.teamOnly = SGTC_EntityTeamFilter( pEnt );
		r.origin[0] = ( pEnt->pev->absmin.x + pEnt->pev->absmax.x ) * 0.5f;
		r.origin[1] = ( pEnt->pev->absmin.y + pEnt->pev->absmax.y ) * 0.5f;
		r.origin[2] = ( pEnt->pev->absmin.z + pEnt->pev->absmax.z ) * 0.5f;
		r.destinationId = bArmed ? DestIndexForTarget( pszTarget ) : 0;
		r.state = bArmed ? 2 : 1;	// armed (target set) / idle
		++s_numGates;
	}
}

static int s_irisOpen = -1;

static void MechScanIris( void )
{
	s_irisOpen = -1;
	CBaseEntity *pEnt = NULL;
	while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "func_wall" ) ) != NULL )
	{
		const char *pszId = pEnt->pev->targetname ? STRING( pEnt->pev->targetname ) : "";
		if( !pszId[0] || strncmp( pszId, "iris", 4 ) )
			continue;
		s_irisOpen = ( pEnt->pev->solid == SOLID_BSP ) ? 0 : 1;
		return;
	}
	pEnt = NULL;
	while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "func_door" ) ) != NULL )
	{
		const char *pszId = pEnt->pev->targetname ? STRING( pEnt->pev->targetname ) : "";
		if( !pszId[0] || strncmp( pszId, "iris", 4 ) )
			continue;
		CBaseToggle *pToggle = (CBaseToggle *)pEnt;
		s_irisOpen = ( (int)pToggle->m_toggle_state == 0 ) ? 1 : 0;
		return;
	}
}

static void MechScanDoors( void )
{
	s_numDoors = 0;
	CBaseEntity *pEnt = NULL;
	while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "func_door" ) ) != NULL && s_numDoors < SGTC_MECH_MAX_DOORS )
	{
		const char *pszId = pEnt->pev->targetname ? STRING( pEnt->pev->targetname ) : "";
		if( !pszId[0] )
			continue;
		sgtc_mech_door_rec_t &r = s_doors[s_numDoors];
		memset( &r, 0, sizeof( r ) );
		strlcpy( r.id, pszId, sizeof( r.id ) );
		r.entindex = ENTINDEX( ENT( pEnt->pev ) );
		CBaseToggle *pToggle = (CBaseToggle *)pEnt;
		const int iState = (int)pToggle->m_toggle_state;
		r.state = ( iState == 0 || iState == 2 ) ? 1 : 2;	// TS_AT_TOP/GOING_UP => open
		r.locked = ( pEnt->pev->spawnflags & 0x20 ) ? 1 : 0;	// SF_DOOR_LOCKED
		++s_numDoors;
	}
}

extern int SGTC_Zones_ObjectiveState( CBaseEntity *pEnt );
extern int SGTC_Token_ObjectiveState( CBaseEntity *pEnt );

static void MechScanObjectives( void )
{
	static const char *const kClasses[] =
	{
		"trigger_goal", "item_objective", "zone_capture", "zone_bomb_target",
		"zone_shelter", "game_nuke"
	};
	s_numObjectives = 0;
	for( int c = 0; c < (int)( sizeof( kClasses ) / sizeof( kClasses[0] ) ); ++c )
	{
		CBaseEntity *pEnt = NULL;
		while( ( pEnt = UTIL_FindEntityByClassname( pEnt, kClasses[c] ) ) != NULL &&
			s_numObjectives < SGTC_MECH_MAX_OBJECTIVES )
		{
			sgtc_mech_objective_t &o = s_objectives[s_numObjectives];
			memset( &o, 0, sizeof( o ) );
			const char *pszId = pEnt->pev->targetname ? STRING( pEnt->pev->targetname ) : "";
			if( pszId[0] )
				strlcpy( o.id, pszId, sizeof( o.id ) );
			else
				snprintf( o.id, sizeof( o.id ), "%s:%d", kClasses[c], ENTINDEX( ENT( pEnt->pev ) ) );
			strlcpy( o.classname, kClasses[c], sizeof( o.classname ) );
			o.team = (int)pEnt->pev->team;
			// Brush volume entities (zone_*/trigger_goal) keep origin at 0,0,0; use
			// the bbox centre so the anchor is actually on the map.
			o.origin[0] = ( pEnt->pev->absmin.x + pEnt->pev->absmax.x ) * 0.5f;
			o.origin[1] = ( pEnt->pev->absmin.y + pEnt->pev->absmax.y ) * 0.5f;
			o.origin[2] = ( pEnt->pev->absmin.z + pEnt->pev->absmax.z ) * 0.5f;
			if( o.origin[0] == 0.0f && o.origin[1] == 0.0f && o.origin[2] == 0.0f )
			{
				o.origin[0] = pEnt->pev->origin.x;
				o.origin[1] = pEnt->pev->origin.y;
				o.origin[2] = pEnt->pev->origin.z;
			}
			o.active = ( pEnt->pev->solid != SOLID_NOT ) ? 1 : 0;
			o.state = SGTC_Zones_ObjectiveState( pEnt );
			if( o.state < 0 && !strcmp( kClasses[c], "game_nuke" ) )
				o.state = SGTC_Token_ObjectiveState( pEnt );
			++s_numObjectives;
		}
	}
}

static const sgtc_mapmeta_map_t *MechMapMeta( void )
{
	const char *pszMap = STRING( gpGlobals->mapname );
	if( !pszMap || !pszMap[0] )
		return NULL;
	for( int i = 0; i < SGTC_MAPMETA_MAP_COUNT; ++i )
	{
		if( !strcmp( sgtc_mapmeta_maps[i].name, pszMap ) )
			return &sgtc_mapmeta_maps[i];
	}
	return NULL;
}

// state_id form used by the generator: "class:entindex" (trigger_hurt:195).
static BOOL MechMapMetaEntityActive( const char *pszStateId )
{
	if( !pszStateId )
		return FALSE;
	const char *pColon = strchr( pszStateId, ':' );
	if( !pColon )
		return FALSE;
	char szClass[48];
	const int iClassLen = (int)( pColon - pszStateId );
	if( iClassLen <= 0 || iClassLen >= (int)sizeof( szClass ) )
		return FALSE;
	strncpy( szClass, pszStateId, iClassLen );
	szClass[iClassLen] = 0;
	const int iIndex = atoi( pColon + 1 );
	if( iIndex <= 0 || iIndex >= gpGlobals->maxEntities )
		return FALSE;
	edict_t *pEdict = INDEXENT( iIndex );
	if( !pEdict || pEdict->free )
		return FALSE;
	const char *pszLive = STRING( pEdict->v.classname );
	if( strcmp( pszLive, szClass ) && !strstr( pszLive, "trigger" ) )
		return FALSE;
	return ( pEdict->v.solid == SOLID_TRIGGER ) ? TRUE : FALSE;
}

int SGTC_Mech_HazardFirstActive( float *pX, float *pY, float *pZ, float *pDamage )
{
	const sgtc_mapmeta_map_t *pMap = MechMapMeta();
	if( !pMap )
		return 0;
	for( int i = 0; i < pMap->num_hazards; ++i )
	{
		const sgtc_mapmeta_hazard_t &h = pMap->hazards[i];
		if( !MechMapMetaEntityActive( h.state_id ) )
			continue;
		if( pX ) *pX = h.x;
		if( pY ) *pY = h.y;
		if( pZ ) *pZ = h.z;
		if( pDamage ) *pDamage = h.damage;
		return 1;
	}
	return 0;
}

int SGTC_Mech_HazardAt( float x, float y, float z, float *pDamage )
{
	if( pDamage )
		*pDamage = 0.0f;
	const sgtc_mapmeta_map_t *pMap = MechMapMeta();
	if( !pMap )
		return 0;
	for( int i = 0; i < pMap->num_hazards; ++i )
	{
		const sgtc_mapmeta_hazard_t &h = pMap->hazards[i];
		const float dx = x - h.x, dy = y - h.y, dz = z - h.z;
		if( dx * dx + dy * dy + dz * dz > h.radius * h.radius )
			continue;
		if( !MechMapMetaEntityActive( h.state_id ) )
			continue;
		if( pDamage )
			*pDamage = h.damage;
		return 1;
	}
	return 0;
}

static int MechHazardNear( float x, float y, float z, BOOL bAssociationOnly )
{
	const sgtc_mapmeta_map_t *pMap = MechMapMeta();
	if( !pMap )
		return 0;
	for( int i = 0; i < pMap->num_hazards; ++i )
	{
		const sgtc_mapmeta_hazard_t &h = pMap->hazards[i];
		if( bAssociationOnly && !h.association )
			continue;
		const float dx = x - h.x, dy = y - h.y, dz = z - h.z;
		const float flNear = h.radius + 512.0f;
		if( dx * dx + dy * dy + dz * dz > flNear * flNear )
			continue;
		if( MechMapMetaEntityActive( h.state_id ) )
			return 1;
	}
	return 0;
}

static void MechUpdateDangers( void )
{
	for( int i = 0; i < s_numGates; ++i )
	{
		sgtc_mech_gate_rec_t &r = s_gates[i];
		r.cooldown = ( r.lastUse > 0.0f && gpGlobals->time < r.lastUse + 1.0f ) ? ( r.lastUse + 1.0f - gpGlobals->time ) : 0.0f;
		r.whooshUntil = ( r.lastUse > 0.0f && gpGlobals->time < r.lastUse + 1.0f ) ? r.lastUse + 1.0f : 0.0f;
		r.hazardActive = MechHazardNear( r.origin[0], r.origin[1], r.origin[2], TRUE );
		r.danger = ( r.whooshUntil > gpGlobals->time || r.hazardActive ) ? 1 : 0;
		if( r.lastUse > 0.0f && gpGlobals->time > r.lastUse + 1.0f && r.state == 4 )
			r.state = 2;
	}
	for( int i = 0; i < s_numRings; ++i )
	{
		sgtc_mech_ring_rec_t &r = s_rings[i];
		r.cooldown = ( r.lastUse > 0.0f && gpGlobals->time < r.lastUse + 10.0f ) ? ( r.lastUse + 10.0f - gpGlobals->time ) : 0.0f;
		if( r.lastUse > 0.0f && gpGlobals->time > r.lastUse + 10.0f && r.state == 5 )
			r.state = 1;
		r.hazardActive = MechHazardNear( r.origin[0], r.origin[1], r.origin[2], TRUE );
	}
}

void SGTC_Mech_Frame( void )
{
	if( gpGlobals->time < s_flNextScan )
	{
		MechUpdateDangers();
		return;
	}
	s_flNextScan = gpGlobals->time + 0.5;
	MechScanIris();
	MechScanGates();
	MechScanDoors();
	MechScanObjectives();
	for( int i = 0; i < s_numGates; ++i )
		s_gates[i].irisOpen = s_irisOpen;
	MechUpdateDangers();
}

int SGTC_Mech_GateState( const char *pszStableId, sgtc_mech_gate_t *pOut )
{
	if( !pszStableId || !pOut )
		return 0;
	for( int i = 0; i < s_numGates; ++i )
	{
		if( strcmp( s_gates[i].id, pszStableId ) )
			continue;
		memset( pOut, 0, sizeof( *pOut ) );
		pOut->state = s_gates[i].state;
		pOut->irisOpen = s_gates[i].irisOpen;
		pOut->busy = ( s_gates[i].state == 3 ) ? 1 : 0;
		pOut->canApproach = ( s_gates[i].danger || s_gates[i].state == 3 ) ? 0 : 1;
		pOut->canUse = ( s_gates[i].irisOpen != 0 && s_gates[i].state == 2 && !s_gates[i].danger ) ? 1 : 0;
		pOut->dialProgress = ( s_gates[i].state == 3 ) ? 0.5f : 0.0f;
		pOut->cooldown = s_gates[i].cooldown;
		pOut->danger = s_gates[i].danger;
		pOut->whooshTimeLeft = ( s_gates[i].whooshUntil > gpGlobals->time ) ? s_gates[i].whooshUntil - gpGlobals->time : 0.0f;
		pOut->destinationId = s_gates[i].destinationId;
		pOut->teamOnly = s_gates[i].teamOnly;
		return 1;
	}
	return 0;
}

int SGTC_Mech_GateCount( void )
{
	return s_numGates;
}

const char *SGTC_Mech_GateId( int iIndex, sgtc_mech_gate_t *pOut )
{
	if( iIndex < 0 || iIndex >= s_numGates )
		return NULL;
	if( pOut )
		SGTC_Mech_GateState( s_gates[iIndex].id, pOut );
	return s_gates[iIndex].id;
}

int SGTC_Mech_RingState( const char *pszStableId, sgtc_mech_ring_t *pOut )
{
	if( pszStableId && pOut )
	{
		for( int i = 0; i < s_numRings; ++i )
		{
			if( strcmp( s_rings[i].id, pszStableId ) )
				continue;
			memset( pOut, 0, sizeof( *pOut ) );
			pOut->state = s_rings[i].state;
			pOut->cooldown = s_rings[i].cooldown;
			pOut->destinationId = s_rings[i].destinationId;
			pOut->teamOnly = s_rings[i].teamOnly;
			pOut->canApproach = ( s_rings[i].state != 3 && s_rings[i].state != 4 ) ? 1 : 0;
			pOut->canUse = ( s_rings[i].state == 2 && s_rings[i].cooldown <= 0.0f ) ? 1 : 0;
			return 1;
		}
	}
	return 0;
}

int SGTC_Mech_RingCount( void ) { return s_numRings; }
const char *SGTC_Mech_RingId( int iIndex, sgtc_mech_ring_t *pOut )
{
	if( iIndex < 0 || iIndex >= s_numRings )
		return NULL;
	if( pOut )
		SGTC_Mech_RingState( s_rings[iIndex].id, pOut );
	return s_rings[iIndex].id;
}
int SGTC_Mech_DoorCount( void ) { return s_numDoors; }
const char *SGTC_Mech_DoorId( int iIndex, sgtc_mech_door_t *pOut )
{
	if( iIndex < 0 || iIndex >= s_numDoors )
		return NULL;
	if( pOut )
		SGTC_Mech_DoorState( s_doors[iIndex].id, pOut );
	return s_doors[iIndex].id;
}
int SGTC_Mech_IrisOpen( void ) { return s_irisOpen; }

int SGTC_Mech_EntityTeamOnly( const char *pszStableId )
{
	if( !pszStableId || !pszStableId[0] )
		return 0;
	CBaseEntity *pEnt = UTIL_FindEntityByTargetname( NULL, pszStableId );
	if( !pEnt )
		pEnt = UTIL_FindEntityByClassname( NULL, pszStableId );
	if( !pEnt || !pEnt->pev )
		return 0;
	return SGTC_EntityTeamFilter( pEnt );
}

int SGTC_Mech_ObjectiveCount( void ) { return s_numObjectives; }
const char *SGTC_Mech_ObjectiveId( int iIndex, sgtc_mech_objective_t *pOut )
{
	if( iIndex < 0 || iIndex >= s_numObjectives )
		return NULL;
	if( pOut )
		*pOut = s_objectives[iIndex];
	return s_objectives[iIndex].id;
}

int SGTC_Mech_DoorState( const char *pszStableId, sgtc_mech_door_t *pOut )
{
	if( !pszStableId || !pOut )
		return 0;
	for( int i = 0; i < s_numDoors; ++i )
	{
		if( strcmp( s_doors[i].id, pszStableId ) )
			continue;
		memset( pOut, 0, sizeof( *pOut ) );
		pOut->state = s_doors[i].state;
		pOut->locked = s_doors[i].locked;
		pOut->blockedForBots = 0;
		return 1;
	}
	return 0;
}
