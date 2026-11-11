// sgtc_bots_rules_ai.cpp — see header.
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "game.h"
#include "gamerules.h"
#include "weapons.h"

#include "sgtc_enums.h"
#include "sgtc_bots_rules.h"
#include "sgtc_mech_state.h"
#include "sgtc_mapmeta_gen.h"
#include "sgtc_bots_rules_ai.h"

#include "../bots/jk_botti/bot.h"
#include "../bots/jk_botti/waypoint.h"
#include "../bots/jk_botti/bot_func.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

extern bot_t bots[32];
extern int num_waypoints;

#define SGTC_AI_MAX_TP     160
#define SGTC_AI_MAX_OBJ    40
#define SGTC_AI_MAX_HAZ   128
#define SGTC_AI_MAX_GATES   4
#define SGTC_AI_MAX_RINGS   4

typedef struct
{
	int        srcNode;
	int        dstNode;
	Vector     srcPos;
	Vector     dstPos;
	int        kind;        // 0 teleport, 1 gate, 2 ring
	int        gateId;
	char       stateId[64];
} sgtc_ai_tp_t;

typedef struct
{
	Vector origin;
	float  radius;
	int    kind;
	int    gateRelated;
	char   stateId[64];
} sgtc_ai_haz_t;

static const sgtc_mapmeta_map_t *s_map = NULL;
static sgtc_ai_tp_t  s_tp[SGTC_AI_MAX_TP];
static int           s_ntp = 0;
static sgtc_ai_haz_t s_haz[SGTC_AI_MAX_HAZ];
static int           s_nhaz = 0;
static char          s_summary[2048];
static int           s_fedDirector = 0;
static int           s_anchorsReady = 0;
static float         s_nextDirectorFeed = 0.0f;

// Cloak observability for botstate/headless probes: how often and how long a
// bot is cloaked (m_bShieldSprint carries the invis-weapon cloak state).
static float s_cloakSince[32];
static int   s_cloakFrames[32];
static int   s_totalFrames[32];
static int   s_wasCloaked[32];
#define AI_MAX_TRAPS 64
static Vector s_trapPos[AI_MAX_TRAPS];
static float  s_trapRadius[AI_MAX_TRAPS];
static int    s_nTraps;
static int    s_trapDeaths;
static int    s_wasAlive[32];
static int    s_forceCloak[32];

// Movement/weapon telemetry for botstate probes: weapon switches, jumps,
// distance moved, firing and moving time (frames at server tick).
static int   s_switches[32];
static int   s_abilitySwitches[32];
static int   s_jumps[32];
static int   s_fireFrames[32];
static int   s_moveFrames[32];
static float s_distMoved[32];
static Vector s_lastOrigin[32];
static Vector s_lastVel[32];
static int   s_lastWeaponId[32];
static int   s_lastJump[32];

static const char *AI_MapName( void )
{
	return STRING( gpGlobals->mapname );
}

static int AI_StrCmp( const char *a, const char *b )
{
	return strcmp( a ? a : "", b ? b : "" );
}

static CBasePlayer *AI_PlayerFromEdict( edict_t *edict )
{
	if( !edict || FNullEnt( edict ) )
		return NULL;
	return (CBasePlayer *)CBaseEntity::Instance( edict );
}

static void AI_SetLocationGoal( bot_t &bot, int node, float seconds )
{
	if( node < 0 || node >= num_waypoints )
		return;
	bot.waypoint_goal = node;
	bot.wpt_goal_type = WPT_GOAL_LOCATION;
	bot.f_waypoint_goal_time = gpGlobals->time + seconds;
}

static Vector AI_StandoffPoint( const Vector &gateOrigin, const Vector &botOrigin, float distance )
{
	Vector away = botOrigin - gateOrigin;
	away.z = 0.0f;
	if( away.Length() < 1.0f )
	{
		away.x = 64.0f;
		away.y = 0.0f;
	}
	away = away.Normalize() * distance;
	return gateOrigin + away;
}

static int AI_GateStateFor( const sgtc_ai_tp_t &tp, sgtc_mech_gate_t *out )
{
	if( tp.kind != 1 )
		return -1;
	/* Prefer the per-gate state id from the map header, else the teleport's
	   own source targetname (Gene keys by targetname where present). */
	for( int i = 0; i < s_map->num_gates; i++ )
	{
		const sgtc_mapmeta_gate_t &gate = s_map->gates[i];
		if( gate.id != tp.gateId )
			continue;
		if( gate.state_id && gate.state_id[0] )
			return SGTC_Mech_GateState( gate.state_id, out );
	}
	if( tp.stateId[0] )
		return SGTC_Mech_GateState( tp.stateId, out );
	return -1;
}

static int AI_RingStateFor( const sgtc_ai_tp_t &tp, sgtc_mech_ring_t *out )
{
	if( tp.kind != 2 || !s_map || s_map->num_rings <= 0 || !s_map->rings )
		return -1;
	if( s_map->rings[0].state_id && s_map->rings[0].state_id[0] )
		return SGTC_Mech_RingState( s_map->rings[0].state_id, out );
	if( tp.stateId[0] )
		return SGTC_Mech_RingState( tp.stateId, out );
	return -1;
}

static void AI_AimAt( bot_t &bot, const Vector &target )
{
	edict_t *edict = bot.pEdict;
	if( !edict )
		return;
	Vector delta = target - ( edict->v.origin + edict->v.view_ofs );
	if( delta.Length() < 1.0f )
		return;
	Vector angles = UTIL_VecToAngles( delta );
	edict->v.v_angle = angles;
	edict->v.idealpitch = angles.x;
	edict->v.ideal_yaw = angles.y;
	edict->v.angles = angles;
	if( angles.x > 180.0f )
		edict->v.angles.x -= 360.0f;
}

static void AI_ApplyVisibilityRules( bot_t &bot, CBasePlayer *observer )
{
	if( !bot.pBotEnemy || FNullEnt( bot.pBotEnemy ) )
		return;

	CBasePlayer *target = AI_PlayerFromEdict( bot.pBotEnemy );
	if( !target || !target->IsAlive() )
		return;

	const sgtc_vis_t vis = SGTC_Bots_TargetVisible( observer, target );
	const float revealAge = SGTC_Bots_TargetRevealAge( target );

	if( vis != SGTC_VIS_INVISIBLE )
		return;
	if( revealAge >= 0.0f && revealAge <= 1.5f )
		return;	// recently revealed (fire/damage/TER pulse)

	if( SGTC_Bots_ObserverHasTER( observer ) )
		return;	// already sweeping with the TER (active scan)

	if( observer->HasNamedPlayerItem( "weapon_reetou" ) )
	{
		/* Human-like TER use: draw the detector and pulse the active scan at
		   the last known position instead of shooting a ghost with bullets. */
		FakeClientCommand( bot.pEdict, "weapon_reetou", NULL, NULL );
		AI_AimAt( bot, target->pev->origin );
		bot.pEdict->v.button |= IN_ATTACK;
		bot.f_shoot_time = gpGlobals->time + 0.35f;
		return;
	}

	BotRemoveEnemy( bot, FALSE, "invis_rule" );
}

static void AI_ApplyShieldRules( bot_t &bot, CBasePlayer *observer )
{
	if( !bot.pBotEnemy || FNullEnt( bot.pBotEnemy ) )
		return;

	CBasePlayer *target = AI_PlayerFromEdict( bot.pBotEnemy );
	if( !target || !target->IsAlive() || !SGTC_Bots_EnemyShielded( target ) )
		return;

	if( !observer->m_pActiveItem )
		return;

	const int weaponId = observer->m_pActiveItem->m_iId;
	if( SGTC_Bots_WeaponBypassesShield( weaponId ) )
		return;	// already holding a knife/melee that can break the shield

	const char *melee = ( SGTC_EntTeamToTeam( (int)observer->pev->team ) == SGTC_TEAM_GOAULD )
		? "weapon_coujaf" : "weapon_couteau";
	if( !observer->HasNamedPlayerItem( melee ) )
		return;

	/* Shield wall absorbs bullets/energy/blast: draw the knife and close in.
	   Delay shooting while the switch deploys so the bot does not keep
	   emptying the old magazine into the shield. */
	FakeClientCommand( bot.pEdict, melee, NULL, NULL );
	bot.f_shoot_time = gpGlobals->time + 0.4f;
}

static void AI_ApplyMechRouting( bot_t &bot, CBasePlayer *observer )
{
	if( !s_map || !bot.pEdict || FNullEnt( bot.pEdict ) )
		return;

	const Vector botOrigin = bot.pEdict->v.origin;
	const float now = gpGlobals->time;

	for( int i = 0; i < s_ntp; i++ )
	{
		const sgtc_ai_tp_t &tp = s_tp[i];
		const float distSrc = ( tp.srcPos - botOrigin ).Length();
		const float distDst = ( tp.dstPos - botOrigin ).Length();

		if( distSrc > 340.0f && distDst > 220.0f )
			continue;

		int usable = 1;
		int danger = 0;
		float cooldown = 0.0f;

		if( tp.kind == 1 )
		{
			sgtc_mech_gate_t gateState;
			if( AI_GateStateFor( tp, &gateState ) >= 0 )
			{
				if( gateState.teamOnly && gateState.teamOnly !=
					SGTC_EntTeamToTeam( (int)observer->pev->team ) )
					continue;	// Tauri-only DHD/gate: not for this bot's team
				usable = ( gateState.canUse || gateState.canApproach ) ? 1 : 0;
				danger = ( gateState.danger || gateState.whooshTimeLeft > 0.0f ) ? 1 : 0;
				cooldown = gateState.cooldown;
			}
		}
		else if( tp.kind == 2 )
		{
			sgtc_mech_ring_t ringState;
			if( AI_RingStateFor( tp, &ringState ) >= 0 )
			{
				if( ringState.teamOnly && ringState.teamOnly !=
					SGTC_EntTeamToTeam( (int)observer->pev->team ) )
					continue;
				usable = ( ringState.canUse || ringState.canApproach ) ? 1 : 0;
				danger = ( ringState.danger || ringState.whooshTimeLeft > 0.0f ) ? 1 : 0;
				cooldown = ringState.cooldown;
			}
		}

		/* Also keep out of static gate hazards whenever a gate/ring is hot. */
		for( int h = 0; h < s_nhaz; h++ )
		{
			if( !s_haz[h].gateRelated )
				continue;
			if( ( s_haz[h].origin - botOrigin ).Length() <= s_haz[h].radius + 64.0f )
				danger = 1;
		}

		if( usable && !danger )
			continue;

		/* Closed/iris-closed/busy/dialing gate, or active whoosh: do not walk
		   through.  Queue at a stand-off node, or step out of the hazard
		   volume if already inside it. */
		const Vector standoff = AI_StandoffPoint( tp.srcPos, botOrigin, 280.0f );
		int node = WaypointFindNearest( standoff, Vector( 0, 0, 0 ), bot.pEdict, 280.0f, FALSE );
		if( node < 0 && distSrc < 140.0f )
		{
			const Vector out = AI_StandoffPoint( tp.srcPos, botOrigin, 420.0f );
			node = WaypointFindNearest( out, Vector( 0, 0, 0 ), bot.pEdict, 420.0f, FALSE );
		}
		if( node >= 0 && ( node != bot.curr_waypoint_index || distSrc < 140.0f ) )
		{
			const float wait = ( cooldown > 0.0f ) ? ( cooldown + 0.5f ) : 1.5f;
			AI_SetLocationGoal( bot, node, wait < 1.0f ? 1.0f : wait );
		}
		else if( distSrc < 140.0f )
		{
			/* No stand-off node found: freeze on the current waypoint rather
			   than step into the event horizon. */
			AI_SetLocationGoal( bot, bot.curr_waypoint_index, 1.2f );
		}
		break;
	}

	(void)now;
}

static int AI_ObjectiveRole( const sgtc_mech_objective_t &obj, int botTeam )
{
	/* 0 = ignore/unknown, 1 = defend (ours), 2 = attack (enemy/contested).
	   zone_capture state is 0 draw / 1 Tau'ri / 2 Goa'uld / 3 dispute and
	   zone_bomb/nuke use their packed live state; -1 = unknown anchor. */
	if( obj.state == 3 || obj.state == 0 )
		return 2;	// contested/draw: attack the point
	if( obj.state > 0 && obj.state == botTeam )
		return 1;	// ours: defend
	if( obj.state > 0 && obj.state != botTeam )
		return 2;	// enemy-owned: attack
	return 0;
}

static void AI_ChooseObjectiveGoal( bot_t &bot, CBasePlayer *observer )
{
	if( bot.pBotEnemy && !FNullEnt( bot.pBotEnemy ) )
		return;
	if( bot.f_waypoint_goal_time > gpGlobals->time )
		return;
	if( num_waypoints <= 0 )
		return;

	const int team = SGTC_EntTeamToTeam( (int)observer->pev->team );
	const int count = SGTC_Mech_ObjectiveCount();
	for( int i = 0; i < count; i++ )
	{
		sgtc_mech_objective_t obj;
		if( !SGTC_Mech_ObjectiveId( i, &obj ) )
			continue;
		if( obj.state < 0 )
			continue;	// anchor only (trigger_goal): handled by mapmeta anchors
		const int role = AI_ObjectiveRole( obj, team );
		if( role == 0 )
			continue;
		const Vector origin( obj.origin[0], obj.origin[1], obj.origin[2] );
		const int node = WaypointFindNearest( origin, Vector( 0, 0, 0 ), bot.pEdict, 380.0f, FALSE );
		if( node < 0 )
			continue;
		const int current = WaypointFindNearest( bot.pEdict->v.origin, Vector( 0, 0, 0 ), bot.pEdict, 120.0f, FALSE );
		if( current >= 0 && WaypointDistanceFromTo( current, node ) >= (float)WAYPOINT_UNREACHABLE )
			continue;
		AI_SetLocationGoal( bot, node, 15.0f );
		return;
	}
}

static void AI_BuildMapSummary( void )
{
	s_summary[0] = '\0';
	if( !s_map )
		return;

	int objectives = s_map->num_objectives;
	int teleports = s_map->num_teleports;
	int gates = s_map->num_gates;
	int rings = s_map->num_rings;
	int hazards = s_map->num_hazards;
	int gateHazards = 0;
	for( int i = 0; i < hazards; i++ )
		if( s_map->hazards[i].association )
			gateHazards++;

	snprintf( s_summary, sizeof( s_summary ),
		"{\"map\":\"%s\",\"objectives\":%d,\"teleports\":%d,"
		"\"gates\":%d,\"rings\":%d,\"hazards\":%d,\"gate_hazards\":%d,"
		"\"rules\":{\"gate_closed\":\"do_not_route_wait_outside\","
		"\"iris_closed\":\"wait\",\"whoosh\":\"keep_clear_then_cross\","
		"\"ring\":\"button_only_when_idle_wait_outside_circle\","
		"\"shield\":\"knife_only\",\"invis\":\"TER_pulse_then_engage\"}}",
		AI_MapName(), objectives, teleports, gates, rings, hazards, gateHazards );
}

static void AI_ResolveAnchors( void );

void SGTC_Bots_RulesAI_Init( void )
{
	s_map = NULL;
	s_ntp = 0;
	s_nhaz = 0;
	s_summary[0] = '\0';
	s_fedDirector = 0;
	s_nTraps = 0;
	s_trapDeaths = 0;
	{
		CBaseEntity *trap = NULL;
		while( ( trap = UTIL_FindEntityByClassname( trap, "monster_trapbox" ) ) != NULL &&
			s_nTraps < AI_MAX_TRAPS )
		{
			s_trapPos[s_nTraps] = trap->pev->origin;
			s_trapRadius[s_nTraps] = 180.0f;
			s_nTraps++;
		}
		for( int i = 0; i < 32; i++ )
			s_wasAlive[i] = 0;
	}
	for( int i = 0; i < 32; i++ )
	{
		s_cloakSince[i] = 0.0f;
		s_cloakFrames[i] = 0;
		s_totalFrames[i] = 0;
		s_wasCloaked[i] = 0;
		s_forceCloak[i] = 0;
		s_switches[i] = 0;
		s_abilitySwitches[i] = 0;
		s_jumps[i] = 0;
		s_fireFrames[i] = 0;
		s_moveFrames[i] = 0;
		s_distMoved[i] = 0.0f;
		s_lastWeaponId[i] = 0;
		s_lastJump[i] = 0;
		s_lastOrigin[i] = Vector( 0, 0, 0 );
		s_lastVel[i] = Vector( 0, 0, 0 );
	}

	const char *mapName = AI_MapName();
	for( int i = 0; i < SGTC_MAPMETA_MAP_COUNT; i++ )
	{
		if( AI_StrCmp( sgtc_mapmeta_maps[i].name, mapName ) == 0 )
		{
			s_map = &sgtc_mapmeta_maps[i];
			break;
		}
	}
	if( !s_map )
		return;

	AI_ResolveAnchors();

	for( int i = 0; i < s_map->num_hazards && s_nhaz < SGTC_AI_MAX_HAZ; i++ )
	{
		const sgtc_mapmeta_hazard_t &h = s_map->hazards[i];
		sgtc_ai_haz_t &row = s_haz[s_nhaz];
		memset( &row, 0, sizeof( row ) );
		row.origin = Vector( h.x, h.y, h.z );
		row.radius = h.radius;
		row.kind = h.kind;
		row.gateRelated = h.association;
		if( h.state_id )
			strncpy( row.stateId, h.state_id, sizeof( row.stateId ) - 1 );
		s_nhaz++;
	}

	AI_BuildMapSummary();
}

/* Waypoint anchors can only be resolved after the waypoint file loaded; on
   some maps SGTC_Bots_OnSpawnPost runs before JK_Botti loads it, so retry from
   the first frame once num_waypoints is non-zero. */
static void AI_ResolveAnchors( void )
{
	if( s_anchorsReady || !s_map || num_waypoints <= 0 )
		return;
	s_ntp = 0;
	for( int i = 0; i < s_map->num_teleports && s_ntp < SGTC_AI_MAX_TP; i++ )
	{
		const sgtc_mapmeta_teleport_t &tp = s_map->teleports[i];
		sgtc_ai_tp_t &row = s_tp[s_ntp];
		memset( &row, 0, sizeof( row ) );
		row.srcPos = Vector( tp.sx, tp.sy, tp.sz );
		row.dstPos = Vector( tp.dx, tp.dy, tp.dz );
		row.srcNode = WaypointFindNearest( row.srcPos, Vector( 0, 0, 0 ), INDEXENT( 0 ), 120.0f, FALSE );
		row.dstNode = WaypointFindNearest( row.dstPos, Vector( 0, 0, 0 ), INDEXENT( 0 ), 120.0f, FALSE );
		row.kind = tp.kind;	// 0 teleport, 1 gate, 2 ring
		row.gateId = tp.gate_id;
		if( tp.state_id )
			strncpy( row.stateId, tp.state_id, sizeof( row.stateId ) - 1 );
		s_ntp++;
	}
	s_anchorsReady = 1;
}

void SGTC_Bots_RulesAI_Frame( void )
{
	if( !s_anchorsReady )
		AI_ResolveAnchors();
	if( !s_fedDirector && s_summary[0] && gpGlobals->time >= s_nextDirectorFeed )
	{
		/* Feed the deterministic mapmeta summary into the LLM director prompt
		   if the operator has not set extra JSON themselves.  This is the
		   deterministic fallback: no response path or model key needed. */
		// build-22: always write the current flat map summary on the first feed;
		// a stale adopted cvar from a previous run must not keep an old nested
		// nav object in the request.
		CVAR_SET_STRING( "sv_botdirector_extra_json", s_summary );
		s_fedDirector = 1;
		s_nextDirectorFeed = gpGlobals->time + 5.0f;
	}

	for( int i = 0; i < 32; i++ )
	{
		bot_t &bot = bots[i];
		if( !bot.is_used || FNullEnt( bot.pEdict ) )
			continue;

		CBasePlayer *observer = AI_PlayerFromEdict( bot.pEdict );
		if( !observer || !observer->IsAlive() )
			continue;

		s_totalFrames[i]++;

		// Trapbox telemetry: the world killed this bot inside a box's radius.
		if( s_wasAlive[i] && !observer->IsAlive() )
		{
			for( int t = 0; t < s_nTraps; t++ )
			{
				if( ( observer->pev->origin - s_trapPos[t] ).Length() <= s_trapRadius[t] + 64.0f )
				{
					s_trapDeaths++;
					UTIL_ConsolePrintf( "[trapbox] bot=%s death_total=%d t=%.2f\n",
						STRING( bot.pEdict->v.netname ), s_trapDeaths, gpGlobals->time );
					break;
				}
			}
		}
		s_wasAlive[i] = observer->IsAlive() ? 1 : 0;

		// Keep out of map trapbox danger volumes (armed Jaffa ambush boxes):
		// stand off outside the blast radius instead of walking through.
		for( int t = 0; t < s_nTraps; t++ )
		{
			const float dist = ( observer->pev->origin - s_trapPos[t] ).Length();
			if( dist > s_trapRadius[t] + 120.0f )
				continue;
			Vector away = observer->pev->origin - s_trapPos[t];
			away.z = 0.0f;
			if( away.Length() < 1.0f )
			{
				away.x = 64.0f;
				away.y = 0.0f;
			}
			away = away.Normalize() * ( s_trapRadius[t] + 220.0f );
			const int node = WaypointFindNearest( s_trapPos[t] + away, Vector( 0, 0, 0 ),
				bot.pEdict, s_trapRadius[t] + 300.0f, FALSE );
			if( node >= 0 )
			{
				bot.waypoint_goal = node;
				bot.wpt_goal_type = WPT_GOAL_LOCATION;
				bot.f_waypoint_goal_time = gpGlobals->time + 1.5f;
			}
			break;
		}

		// Telemetry: weapon switches, jumps, distance, firing/moving frames.
		{
			const int activeId = observer->m_pActiveItem ? observer->m_pActiveItem->m_iId : 0;
			if( s_lastWeaponId[i] != 0 && activeId != s_lastWeaponId[i] )
			{
				s_switches[i]++;
				// Ability draws (invis/shield/tacgun/medic/regenerator) are
				// classplay, not jk_botti weapon choice: keep them separate so
				// the "bots switch a lot" metric measures real selection churn.
				const char *activeName = ( observer->m_pActiveItem && observer->m_pActiveItem->pev )
					? STRING( observer->m_pActiveItem->pev->classname ) : "";
				if( !strcmp( activeName, "weapon_invis" ) || !strcmp( activeName, "weapon_shield" ) ||
					!strcmp( activeName, "weapon_tacgun" ) || !strcmp( activeName, "weapon_medic" ) ||
					!strcmp( activeName, "weapon_goaregen" ) || !strcmp( activeName, "weapon_larve" ) )
					s_abilitySwitches[i]++;
			}
			s_lastWeaponId[i] = activeId;

			const int jumpNow = ( bot.pEdict->v.button & IN_JUMP ) ? 1 : 0;
			if( jumpNow && !s_lastJump[i] )
				s_jumps[i]++;
			s_lastJump[i] = jumpNow;

			if( observer->pev->button & IN_ATTACK )
				s_fireFrames[i]++;
			if( observer->pev->velocity.Length() > 60.0f )
				s_moveFrames[i]++;

			const Vector origin( observer->pev->origin );
			if( s_totalFrames[i] > 1 )
				s_distMoved[i] += ( origin - s_lastOrigin[i] ).Length();
			s_lastOrigin[i] = origin;
		}
		if( observer->m_bShieldSprint )
		{
			s_cloakFrames[i]++;
			if( !s_wasCloaked[i] )
				s_cloakSince[i] = gpGlobals->time;
			s_wasCloaked[i] = 1;
		}
		else
		{
			s_wasCloaked[i] = 0;
			s_cloakSince[i] = 0.0f;
		}

		AI_ApplyVisibilityRules( bot, observer );
		AI_ApplyShieldRules( bot, observer );
		AI_ApplyMechRouting( bot, observer );
		AI_ChooseObjectiveGoal( bot, observer );
	}
}

const char *SGTC_Bots_RulesAI_MapSummary( void )
{
	return s_summary;
}

int SGTC_Bots_RulesAI_HasMapMeta( void )
{
	return s_map ? 1 : 0;
}

int SGTC_Bots_RulesAI_DebugForceCloak( int iBotIndex, int iOn )
{
	if( iBotIndex < 0 || iBotIndex >= 32 )
		return 0;
	s_forceCloak[iBotIndex] = iOn ? 1 : 0;
	if( iOn && !FNullEnt( bots[iBotIndex].pEdict ) )
	{
		/* Probe builds: make the chosen bot actually capable of cloaking so the
		   real weapon_invis toggle path can be exercised even when the class
		   kit / charge / shield state would normally block it. */
		CBasePlayer *pl = AI_PlayerFromEdict( bots[iBotIndex].pEdict );
		if( pl && pl->IsAlive() )
		{
			if( !pl->HasNamedPlayerItem( "weapon_invis" ) )
				pl->GiveNamedItem( "weapon_invis" );
			pl->m_bShieldWall = 0;
			pl->m_bShieldSpawned = 0;
			pl->m_iShieldCharge = 10;
		}
	}
	return 1;
}

int SGTC_Bots_RulesAI_ForceCloakActive( int iBotIndex )
{
	if( iBotIndex < 0 || iBotIndex >= 32 )
		return 0;
	return s_forceCloak[iBotIndex];
}

int SGTC_Bots_RulesAI_Probe( int iIndex, char *buf, int buflen )
{
	if( !buf || buflen <= 0 || iIndex < 0 || iIndex >= 32 )
		return -1;
	bot_t &bot = bots[iIndex];
	if( !bot.is_used || FNullEnt( bot.pEdict ) )
		return -1;

	CBasePlayer *observer = AI_PlayerFromEdict( bot.pEdict );
	if( !observer )
		return -1;

	int vis = -1;
	int reveal = -1;
	int shielded = 0;
	int bypass = 0;
	int enemyShieldState = -1;
	int enemyDistance = -1;
	CBasePlayer *target = AI_PlayerFromEdict( bot.pBotEnemy );
	if( target )
	{
		vis = (int)SGTC_Bots_TargetVisible( observer, target );
		reveal = (int)( SGTC_Bots_TargetRevealAge( target ) * 1000.0f );
		shielded = SGTC_Bots_EnemyShielded( target );
		int shieldType = 0;
		float energy = -1.0f;
		enemyShieldState = SGTC_Bots_ShieldState( target, &shieldType, &energy );
		enemyDistance = (int)( target->pev->origin - observer->pev->origin ).Length();
	}
	if( observer->m_pActiveItem )
		bypass = SGTC_Bots_WeaponBypassesShield( observer->m_pActiveItem->m_iId );

	int gateUsable = -1;
	int gateDanger = -1;
	int gateState = -1;
	int ringUsable = -1;
	const int cloaked = observer->m_bShieldSprint ? 1 : 0;
	const int cloakPct = ( s_totalFrames[iIndex] > 0 )
		? ( 100 * s_cloakFrames[iIndex] ) / s_totalFrames[iIndex] : 0;
	const int cloakHoldMs = ( cloaked && s_cloakSince[iIndex] > 0.0f )
		? (int)( ( gpGlobals->time - s_cloakSince[iIndex] ) * 1000.0f ) : 0;
	const int shieldCharge = (int)observer->m_iShieldCharge;
	const float elapsed = ( gpGlobals->time > 0.0f ) ? gpGlobals->time : 1.0f;
	const int switchesMin = (int)( s_switches[iIndex] * 60.0f / elapsed );
	const int selSwitchesMin = (int)( ( s_switches[iIndex] - s_abilitySwitches[iIndex] ) * 60.0f / elapsed );
	const int abilitySwitchesMin = (int)( s_abilitySwitches[iIndex] * 60.0f / elapsed );
	const int jumpsMin = (int)( s_jumps[iIndex] * 60.0f / elapsed );
	const int firePct = ( s_totalFrames[iIndex] > 0 )
		? ( 100 * s_fireFrames[iIndex] ) / s_totalFrames[iIndex] : 0;
	const int movePct = ( s_totalFrames[iIndex] > 0 )
		? ( 100 * s_moveFrames[iIndex] ) / s_totalFrames[iIndex] : 0;
	const int distM = (int)( s_distMoved[iIndex] / 64.0f );
	const int weaponId = observer->m_pActiveItem ? observer->m_pActiveItem->m_iId : 0;
	const int renderEffects = (int)observer->pev->effects;
	const int renderMode = (int)observer->pev->rendermode;
	const int renderAmt = (int)observer->pev->renderamt;
	const int team = SGTC_EntTeamToTeam( (int)observer->pev->team );
	int gateTeamOnly = 0;
	int gateTeamAllowed = -1;
	int objCount = 0;
	int objState = -1;
	int objRole = 0;
	if( s_map )
	{
		for( int i = 0; i < s_ntp; i++ )
		{
			const sgtc_ai_tp_t &tp = s_tp[i];
			if( ( tp.srcPos - observer->pev->origin ).Length() > 400.0f )
				continue;
			if( tp.kind == 1 )
			{
				sgtc_mech_gate_t gs;
				if( AI_GateStateFor( tp, &gs ) >= 0 )
				{
					gateState = gs.state;
					gateUsable = ( gs.canUse || gs.canApproach ) ? 1 : 0;
					gateDanger = ( gs.danger || gs.whooshTimeLeft > 0.0f ) ? 1 : 0;
					gateTeamOnly = gs.teamOnly;
					gateTeamAllowed = ( gs.teamOnly == 0 ||
						gs.teamOnly == SGTC_EntTeamToTeam( (int)observer->pev->team ) ) ? 1 : 0;
				}
			}
			else if( tp.kind == 2 )
			{
				sgtc_mech_ring_t rs;
				if( AI_RingStateFor( tp, &rs ) >= 0 )
					ringUsable = ( rs.canUse || rs.canApproach ) ? 1 : 0;
			}
			if( gateState >= 0 || ringUsable >= 0 )
				break;
		}
	}

	{
		objCount = SGTC_Mech_ObjectiveCount();
		for( int i = 0; i < objCount; i++ )
		{
			sgtc_mech_objective_t obj;
			if( !SGTC_Mech_ObjectiveId( i, &obj ) )
				continue;
			const int role = AI_ObjectiveRole( obj, team );
			if( obj.state >= 0 && role > 0 )
			{
				objState = obj.state;
				objRole = role;
				break;
			}
		}
	}

	snprintf( buf, buflen,
		"{\"bot\":\"%s\",\"team\":%d,\"mapmeta\":%d,\"enemy\":%d,\"enemy_team\":%d,\"vis\":%d,\"reveal_ms\":%d,"
		"\"shielded\":%d,\"shield_state\":%d,\"weapon_bypass\":%d,\"enemy_dist\":%d,"
		"\"goal\":%d,\"curr\":%d,\"gate_state\":%d,\"gate_usable\":%d,"
		"\"gate_danger\":%d,\"gate_team_only\":%d,\"gate_team_allowed\":%d,\"obj_count\":%d,\"obj_state\":%d,\"obj_role\":%d,\"ring_usable\":%d,\"cloaked\":%d,\"shield_charge\":%d,\"cloak_pct\":%d,\"cloak_hold_ms\":%d,"
		"\"effects\":%d,\"rendermode\":%d,\"renderamt\":%d,"
		"\"weapon_id\":%d,\"switches_min\":%d,\"sel_switches_min\":%d,"
		"\"ability_switches_min\":%d,\"jumps_min\":%d,\"dist_m\":%d,"
		"\"fire_pct\":%d,\"move_pct\":%d}",
		STRING( bot.pEdict->v.netname ), SGTC_EntTeamToTeam( (int)observer->pev->team ),
		s_map ? 1 : 0, target ? 1 : 0,
		target ? SGTC_EntTeamToTeam( (int)target->pev->team ) : 0,
		vis, reveal, shielded, enemyShieldState, bypass,
		enemyDistance, bot.waypoint_goal, bot.curr_waypoint_index,
		gateState, gateUsable, gateDanger, gateTeamOnly, gateTeamAllowed,
		objCount, objState, objRole, ringUsable,
		cloaked, shieldCharge, cloakPct, cloakHoldMs,
		renderEffects, renderMode, renderAmt,
		weaponId, switchesMin, selSwitchesMin, abilitySwitchesMin, jumpsMin, distM, firePct, movePct );
	return 0;
}
