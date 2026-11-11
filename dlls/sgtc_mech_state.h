// sgtc_mech_state.h — build-13 generic runtime mechanics state for bots.
// Observation-based (class + targetname), no mapmeta dependency; mapmeta stable
// IDs (Alan) can be looked up by targetname through SGTC_Mech_GateState().
#pragma once

class CBaseEntity;

typedef struct
{
	int state;			// sgtc_gate_state_t (0 unknown..6 cooldown)
	int irisOpen;		// -1 unknown, 0 closed, 1 open
	int busy;
	int canApproach;
	int canUse;
	float dialProgress;
	float cooldown;
	int danger;
	float whooshTimeLeft;
	int destinationId;
	int teamOnly;		// map m_team gate (1=Tau'ri, 2=Goa'uld, 0=any)
} sgtc_mech_gate_t;

typedef struct
{
	int state;			// 0 unknown, 1 idle, 2 armed, 3 descending, 4 ascending, 5 cooldown
	int busy;
	int canApproach;
	int canUse;
	float cooldown;
	int danger;
	float whooshTimeLeft;
	int destinationId;
	int returnDestinationId;
	int teamOnly;
} sgtc_mech_ring_t;

typedef struct
{
	int state;			// 0 unknown, 1 open, 2 closed, 3 locked, 4 blocked
	int locked;
	int blockedForBots;
} sgtc_mech_door_t;

void SGTC_Mech_Init( void );
void SGTC_Mech_Frame( void );
void SGTC_Mech_NoteTeleport( CBaseEntity *pTrigger, CBaseEntity *pPlayer, const char *pszDest );
int SGTC_Mech_GateState( const char *pszStableId, sgtc_mech_gate_t *pOut );
int SGTC_Mech_GateCount( void );
const char *SGTC_Mech_GateId( int iIndex, sgtc_mech_gate_t *pOut );
int SGTC_Mech_RingState( const char *pszStableId, sgtc_mech_ring_t *pOut );
int SGTC_Mech_DoorState( const char *pszStableId, sgtc_mech_door_t *pOut );
int SGTC_Mech_RingCount( void );
const char *SGTC_Mech_RingId( int iIndex, sgtc_mech_ring_t *pOut );
int SGTC_Mech_DoorCount( void );
const char *SGTC_Mech_DoorId( int iIndex, sgtc_mech_door_t *pOut );
int SGTC_Mech_IrisOpen( void );
int SGTC_Mech_EntityTeamOnly( const char *pszStableId );
int SGTC_Mech_HazardAt( float x, float y, float z, float *pDamage );	// mapmeta hazard volume query
int SGTC_Mech_HazardFirstActive( float *pX, float *pY, float *pZ, float *pDamage );

// Objective anchors (trigger_goal/item_objective/zone_*/game_nuke): position +
// class + team for bot pathing; live win/capture state is the game-rule layer's
// job and is intentionally reported as -1 here.
typedef struct
{
	char id[64];
	char classname[32];
	int team;
	float origin[3];
	int active;
	int state;			// -1 unknown
} sgtc_mech_objective_t;
int SGTC_Mech_ObjectiveCount( void );
const char *SGTC_Mech_ObjectiveId( int iIndex, sgtc_mech_objective_t *pOut );
