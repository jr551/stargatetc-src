// sgtc_bots.cpp - StargateTC glue for the vendored native jk_botti.
//
// Responsibilities, in the order the server calls them:
//   GameDLLInit  SGTC_Bots_Init          cvars, server commands, RNG seed
//   worldspawn   SGTC_Bots_OnSpawn       deterministic re-seed of the bot RNG
//                SGTC_Bots_OnSpawnPost   nav link scan + no-waypoints event
//   every frame  SGTC_Bots_StartFrame    jk_botti think, spawn/death/teleport/
//                                        stuck events, objective watch
//
// Two contracts matter to the rest of the tree:
//
//   * SGTCEVENT lines (see sgtc_bots_events.h) are the only machine-readable
//     bot telemetry; tests/playtest.sh and CI parse them.
//   * Team/class assignment is decided by the pure helpers in
//     sgtc_bots_loadout.cpp and applied here with a thin engine wrapper. The
//     pure half is linkable from tests/stub_engine.c (plain C linkage).
//
// Radio flow (sgtc_bots_radio.cpp): the human `radio` verb sends its addressed
// MSG_ONE and then calls SGTC_Bots_RadioHear(); a bot radio line goes through
// SGTC_Bots_RadioSend(), which sends the same MSG_ONE to each living teammate
// and then calls SGTC_Bots_RadioHear(). This file owns the cvar cache both
// sides read and the objective focus point a PUSH order is aimed at.

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "game.h"
#include "gamerules.h"

#include "sgtc_bots.h"
#include "sgtc_bots_budget.h"
#include "sgtc_bots_events.h"
#include "sgtc_bots_loadout.h"
#include "sgtc_director.h"
#include "sgtc_bots_radio.h"
#include "sgtc_bots_rules_ai.h"
#include "sgtc_enums.h"
#include "sgtc_gate.h"
#include "sgtc_gate_timing.h"
#include "sgtc_bots_gate.h"
#include "sgtc_sniper.h"	// per-bot sway compensation
#include "../bots/jk_botti/native/jk_botti_native_api.h"
#include "../bots/jk_botti/bot.h"
#include "../bots/jk_botti/bot_func.h"
#include "../bots/jk_botti/bot_name_sanitize.h"
#include "../bots/jk_botti/bot_trace.h"
#include "../bots/jk_botti/waypoint.h"

#include <string.h>
#include <time.h>

// bots/jk_botti/bot.h only aliases stricmp for __linux__; the host syntax
// check builds this translation unit on macOS as well, where the POSIX name
// lives in <strings.h>.
#if !defined(_WIN32) && !defined(__linux__)
#include <strings.h>
#define stricmp strcasecmp
#endif

extern bot_t bots[32];
extern int min_bots;
extern int max_bots;
extern int default_bot_skill;
extern cvar_t jk_botti_version;
extern int num_waypoints;
extern qboolean g_auto_waypoint;
extern void fast_random_seed(unsigned int seed);

// Read by the bot code (bots/jk_botti/bot.cpp, declared in bot.h).
int sgtc_bot_roam_enabled = 1;
int sgtc_bot_rescue_enabled = 1;
// bot_radio mode (0 off, 1 obey, 2 obey+talk) and per-bot rate, cached for the
// bot layer's radio decisions (bots/jk_botti/bot_radio.cpp).
int sgtc_bot_radio_mode = 2;
float sgtc_bot_radio_rate = 12.0f;

// ---------------------------------------------------------------------------
// cvars
// ---------------------------------------------------------------------------

static cvar_t sgtc_bot_min = { "sgtc_bot_min", "-1", FCVAR_SERVER };
static cvar_t sgtc_bot_max = { "sgtc_bot_max", "-1", FCVAR_SERVER };
static cvar_t sgtc_bot_skill = { "sgtc_bot_skill", "2", FCVAR_SERVER };
static cvar_t sgtc_bot_enable = { "sgtc_bot_enable", "1", FCVAR_SERVER };
static cvar_t sgtc_bot_seed = { "sgtc_bot_seed", "0", FCVAR_SERVER };
static cvar_t sgtc_bot_events = { "sgtc_bot_events", "1", FCVAR_SERVER };
static cvar_t sgtc_bot_quota = { "sgtc_bot_quota", "7", FCVAR_SERVER };
static cvar_t sgtc_bot_class = { "sgtc_bot_class", "0", FCVAR_SERVER };
static cvar_t sgtc_bot_team = { "sgtc_bot_team", "0", FCVAR_SERVER };
static cvar_t sgtc_bot_roam = { "sgtc_bot_roam", "1", FCVAR_SERVER };
static cvar_t sgtc_bot_rescue = { "sgtc_bot_rescue", "1", FCVAR_SERVER };
// Fully initialised: the warning gate counts -Wmissing-field-initializers
// per sgtc_* file, and the baseline predates these cvars.
static cvar_t sgtc_bot_kit = { "sgtc_bot_kit", "1", FCVAR_SERVER, 0.0f, NULL };
static cvar_t sgtc_bot_weaponlog = { "sgtc_bot_weaponlog", "0", FCVAR_SERVER };
// The rate cvar struct is suffixed: sgtc_bot_radio_rate is the float global the
// bot layer reads. Both are fully initialised so the analysis warning gate does
// not gain missing-field findings for this file.
static cvar_t sgtc_bot_radio = { "bot_radio", "2", FCVAR_SERVER, 2.0f, NULL };
static cvar_t sgtc_bot_radio_rate_cvar = { "bot_radio_rate", "12", FCVAR_SERVER, 12.0f, NULL };

// Seed actually handed to fast_random_seed(); per-bot choices are derived from
// it so two runs of the same seed produce the same bot decisions.
static unsigned int s_seed_base = 0;

static void SGTC_Bots_SyncCvars( void )
{
	if( sgtc_bot_quota.value >= 0.0f )
	{
		// sgtc_bot_quota wins over min/max: `+sgtc_bot_quota 4` on the command
		// line then means "keep exactly four bots".
		min_bots = max_bots = (int)sgtc_bot_quota.value;
	}
	else
	{
		if( sgtc_bot_min.value >= 0.0f )
			min_bots = (int)sgtc_bot_min.value;
		if( sgtc_bot_max.value >= 0.0f )
			max_bots = (int)sgtc_bot_max.value;
	}

	if( sgtc_bot_skill.value >= 0.0f && sgtc_bot_skill.value <= 5.0f )
		default_bot_skill = (int)sgtc_bot_skill.value;

	sgtc_bot_roam_enabled = ( sgtc_bot_roam.value != 0.0f ) ? 1 : 0;
	sgtc_bot_rescue_enabled = ( sgtc_bot_rescue.value != 0.0f ) ? 1 : 0;

	// bot_radio: 0 off, 1 obey orders, 2 obey + talk on its own.
	sgtc_bot_radio_mode = (int)sgtc_bot_radio.value;
	if( sgtc_bot_radio_mode < 0 )
		sgtc_bot_radio_mode = 0;
	else if( sgtc_bot_radio_mode > 2 )
		sgtc_bot_radio_mode = 2;

	sgtc_bot_radio_rate = sgtc_bot_radio_rate_cvar.value;
	if( sgtc_bot_radio_rate < 1.0f )
		sgtc_bot_radio_rate = 1.0f;
}

static void SGTC_Bots_SeedRandom( void )
{
	const int configured = (int)sgtc_bot_seed.value;

	if( configured != 0 )
		s_seed_base = (unsigned int)configured;
	else
		s_seed_base = (unsigned int)( time( 0 ) ^ (unsigned long)&bots[0] );

	fast_random_seed( s_seed_base );
}

BOOL SGTC_Bots_IsBotPlayer( CBasePlayer *pPlayer )
{
	if( !pPlayer || !pPlayer->pev )
		return FALSE;
	edict_t *pEdict = pPlayer->edict();
	if( FNullEnt( pEdict ) )
		return FALSE;
	for( int i = 0; i < 32; ++i )
	{
		if( bots[i].is_used && bots[i].pEdict == pEdict )
			return TRUE;
	}
	if( FBitSet( pPlayer->pev->flags, FL_FAKECLIENT ) )
		return TRUE;
	return FALSE;
}

int SGTC_Bots_Enabled( void )
{
	return ( sgtc_bot_enable.value != 0.0f ) ? 1 : 0;
}

int SGTC_Bots_KitEnabled( void )
{
	return ( sgtc_bot_kit.value != 0.0f ) ? 1 : 0;
}

unsigned int SGTC_Bots_SeedBase( void )
{
	return s_seed_base;
}

// ---------------------------------------------------------------------------
// event plumbing
// ---------------------------------------------------------------------------

// jk_botti names are already sanitised at creation; run the same sanitiser
// again so a name that changed after creation cannot smuggle control bytes into
// the log line (the formatter escapes anything left anyway).
static void SGTC_Bots_BotName( int index, char *dst, int dst_size )
{
	const char *name = "";

	if( index >= 0 && index < 32 )
	{
		if( bots[index].name[0] )
			name = bots[index].name;
		else if( !FNullEnt( bots[index].pEdict ) )
			name = STRING( bots[index].pEdict->v.netname );
	}

	strlcpy( dst, name, (size_t)dst_size );
	bot_name_sanitize( dst );
}

static void SGTC_Bots_LogEventLine( const char *line )
{
	// UTIL_LogPrintf is the required sink; SGTC_Bots_FormatEvent keeps lines
	// below its 1 KiB formatter so this never overflows.
	UTIL_LogPrintf( "%s\n", line );
}

static void SGTC_Bots_Emit( const char *event, const char *bot,
	const sgtc_bot_event_field_t *fields, int field_count, qboolean force )
{
	char line[SGTC_BOT_EVENT_LINE_MAX];

	if( !force && sgtc_bot_events.value == 0.0f )
		return;

	if( SGTC_Bots_FormatEvent( line, sizeof( line ), event,
			STRING( gpGlobals->mapname ), gpGlobals->time, bot, fields, field_count ) < 0 )
	{
		SGTC_Bots_LogEventLine( SGTC_BOT_EVENT_PREFIX
			"{\"event\":\"error\",\"map\":\"\",\"time\":0.00,\"bot\":\"\",\"detail\":\"event_overflow\"}" );
		return;
	}

	SGTC_Bots_LogEventLine( line );
}

static void SGTC_Bots_AddFieldString( sgtc_bot_event_field_t *f, const char *key, const char *value )
{
	f->key = key;
	f->kind = SGTC_BOT_FIELD_STRING;
	f->sval = value;
	f->ival = 0;
	f->fval = 0.0f;
	f->vec[0] = f->vec[1] = f->vec[2] = 0.0f;
}

static void SGTC_Bots_AddFieldInt( sgtc_bot_event_field_t *f, const char *key, int value )
{
	f->key = key;
	f->kind = SGTC_BOT_FIELD_INT;
	f->sval = NULL;
	f->ival = value;
	f->fval = 0.0f;
	f->vec[0] = f->vec[1] = f->vec[2] = 0.0f;
}

static void SGTC_Bots_AddFieldFloat( sgtc_bot_event_field_t *f, const char *key, float value )
{
	f->key = key;
	f->kind = SGTC_BOT_FIELD_FLOAT2;
	f->sval = NULL;
	f->ival = 0;
	f->fval = value;
	f->vec[0] = f->vec[1] = f->vec[2] = 0.0f;
}

static void SGTC_Bots_AddFieldVec( sgtc_bot_event_field_t *f, const char *key, const Vector &value )
{
	f->key = key;
	f->kind = SGTC_BOT_FIELD_VEC1;
	f->sval = NULL;
	f->ival = 0;
	f->fval = 0.0f;
	f->vec[0] = value.x;
	f->vec[1] = value.y;
	f->vec[2] = value.z;
}

static void SGTC_Bots_AddFieldRaw( sgtc_bot_event_field_t *f, const char *key, const char *json )
{
	f->key = key;
	f->kind = SGTC_BOT_FIELD_RAW;
	f->sval = json;
	f->ival = 0;
	f->fval = 0.0f;
	f->vec[0] = f->vec[1] = f->vec[2] = 0.0f;
}

// One `ability` line per shield/cloak/TAC/heal/talent action taken by
// SGTC_Bots_ClassPlay. Emitted only when sgtc_bot_events is on.
void SGTC_Bots_EmitAbility( const char *bot_name, const char *ability, int cls, const char *detail )
{
	sgtc_bot_event_field_t fields[3];

	SGTC_Bots_AddFieldString( &fields[0], "ability", ability ? ability : "" );
	SGTC_Bots_AddFieldInt( &fields[1], "cls", cls );
	SGTC_Bots_AddFieldString( &fields[2], "detail", detail ? detail : "" );

	SGTC_Bots_Emit( "ability", bot_name ? bot_name : "", fields, 3, FALSE );
}

// ---------------------------------------------------------------------------
// nav link registry (gate / ring / teleporter classification)
// ---------------------------------------------------------------------------

enum
{
	SGTC_LINK_TELEPORT = 0,
	SGTC_LINK_GATE = 1,
	SGTC_LINK_RING = 2
};

#define SGTC_MAX_LINKS 96
#define SGTC_MAX_ZONES 48
#define SGTC_LINK_MATCH_RADIUS 200.0f
#define SGTC_LINK_MIN_DISTANCE 32.0f
#define SGTC_LINK_DEDUPE_RADIUS 100.0f

typedef struct
{
	Vector from;
	Vector to;
	int kind;
	char target[40];
} sgtc_bot_link_t;

typedef struct
{
	Vector origin;
	float radius;
	int kind;
	char name[40];
} sgtc_bot_zone_t;

static sgtc_bot_link_t s_links[SGTC_MAX_LINKS];
static int s_num_links;
static sgtc_bot_zone_t s_zones[SGTC_MAX_ZONES];
static int s_num_zones;

// ---------------------------------------------------------------------------
// gate timing state
// ---------------------------------------------------------------------------
//
// Each map gate (1 and 2) has one timing model shared by every bot. The model
// is pure (dlls/sgtc_gate_timing.h); this file only finds the map's dial
// managers and applies the action to a bot. Entry waypoints are the near-side
// nodes created by WaypointAddTeleportLink for the gate's trigger_teleport.

#define SGTC_MAX_GATE_ENTRIES 16

typedef struct
{
	int		id;				// 1 or 2
	char		step_name[SGTC_GATE_CHEVRONS][40];	// one manager per chevron
	char		open_name[40];				// completion / event horizon
	char		press_name[40];				// DHD button (dial1/dial2), empty if none
	int		last_fired_chevron;
	int		open_fired;
	int		dial_owner;				// bot slot that fired the dial
} sgtc_bot_gate_def_t;

typedef struct
{
	int		waypoint;
	int		gate;		// index into s_gate_defs
	int		trigger;	// entindex of the trigger_teleport, 0 if synthetic
	int		resolved;	// target installed and nav link added
	Vector		origin;
} sgtc_bot_gate_entry_t;

static sgtc_bot_gate_def_t s_gate_defs[2];
static sgtc_gate_timing_t s_gate_timing[2];
static int s_gate_opened_seen[2];	// map's gNtele_x_on has fired for this opening
static int s_gate_observed_available[2];	// map actually has a gNtele_x_on relay
static sgtc_bot_gate_entry_t s_gate_entries[SGTC_MAX_GATE_ENTRIES];
static int s_num_gate_entries;

static void SGTC_Bots_InitGateDefs( void )
{
	for( int g = 0; g < 2; g++ )
	{
		sgtc_bot_gate_def_t &def = s_gate_defs[g];

		def.id = g + 1;
		def.press_name[0] = '\0';
		def.last_fired_chevron = 0;
		def.open_fired = 0;
		def.dial_owner = -1;

		for( int i = 0; i < SGTC_GATE_CHEVRONS; i++ )
			snprintf( def.step_name[i], sizeof( def.step_name[i] ), "multi_gate_%d",
				g == 0 ? i + 1 : 10 + i );

		snprintf( def.open_name, sizeof( def.open_name ), "multi_gate_%d", g == 0 ? 8 : 17 );
		SgtcGateTiming_Reset( &s_gate_timing[g], def.id, 0.0f );
	}
}

static void SGTC_Bots_ResetNav( void )
{
	s_num_links = 0;
	s_num_zones = 0;
	s_num_gate_entries = 0;
	s_gate_opened_seen[0] = 0;
	s_gate_opened_seen[1] = 0;
	s_gate_observed_available[0] = 0;
	s_gate_observed_available[1] = 0;
	SGTC_Bots_InitGateDefs();
}

static int SGTC_Bots_GateIndexFromName( const char *name )
{
	if( !name || !name[0] )
		return 0;

	if( strstr( name, "g2tele" ) || strstr( name, "2dhd" ) || strstr( name, "dial2" ) )
		return 1;

	return 0;
}

// sg_stargate's gate teleports are named g1tele_1..5 / g2tele_1..5 but carry
// no target at spawn: trigger_changetarget installs one when the gate opens.
// The trigger's own name is the authoritative gate number (the destination it
// points at is the *other* gate, so deriving it from the target picks wrong).
static int SGTC_Bots_GateIndexFromTriggerName( const char *name )
{
	if( !name || !name[0] )
		return -1;

	if( !strncmp( name, "g1tele_", 7 ) )
		return 0;
	if( !strncmp( name, "g2tele_", 7 ) )
		return 1;

	return -1;
}

static void SGTC_Bots_AddGateEntry( int waypoint, const Vector &origin, int gate,
	int trigger, int resolved )
{
	if( waypoint < 0 || gate < 0 || gate > 1 )
		return;

	for( int i = 0; i < s_num_gate_entries; i++ )
	{
		if( s_gate_entries[i].waypoint == waypoint )
			return;
	}

	if( s_num_gate_entries >= SGTC_MAX_GATE_ENTRIES )
		return;

	s_gate_entries[s_num_gate_entries].waypoint = waypoint;
	s_gate_entries[s_num_gate_entries].gate = gate;
	s_gate_entries[s_num_gate_entries].trigger = trigger;
	s_gate_entries[s_num_gate_entries].resolved = resolved;
	s_gate_entries[s_num_gate_entries].origin.x = origin.x;
	s_gate_entries[s_num_gate_entries].origin.y = origin.y;
	s_gate_entries[s_num_gate_entries].origin.z = origin.z;
	s_num_gate_entries++;
}

static sgtc_bot_gate_entry_t *SGTC_Bots_GateEntryForWaypoint( int waypoint )
{
	for( int i = 0; i < s_num_gate_entries; i++ )
	{
		if( s_gate_entries[i].waypoint == waypoint )
			return &s_gate_entries[i];
	}

	return NULL;
}

static qboolean SGTC_Bots_IsGateRelayName( const char *name )
{
	// Names fired by the multi_gate_N relay chains in dlls/sgtc_gate.cpp.
	static const char *const prefixes[] =
	{
		"g1tele_", "g2tele_", "gxtele_", "wormtele", "gateclose_tr", "vortex", "opening"
	};

	if( !name || !name[0] )
		return FALSE;

	for( int i = 0; i < (int)( sizeof( prefixes ) / sizeof( prefixes[0] ) ); i++ )
	{
		if( !strncmp( name, prefixes[i], strlen( prefixes[i] ) ) )
			return TRUE;
	}

	return FALSE;
}

static Vector SGTC_Bots_EntityCenter( edict_t *pent )
{
	return VecBModelOrigin( &pent->v );
}

// Vector declares a copy constructor but no copy assignment, so whole-Vector
// assignment is deprecated. Copy the components instead; the layout stays the
// engine's POD vec3_t.
static inline void SGTC_Bots_CopyVector( Vector &dst, const Vector &src )
{
	dst.x = src.x;
	dst.y = src.y;
	dst.z = src.z;
}

static void SGTC_Bots_AddZone( const Vector &origin, float radius, int kind, const char *name )
{
	if( s_num_zones >= SGTC_MAX_ZONES )
		return;

	if( radius < 64.0f )
		radius = 64.0f;

	for( int i = 0; i < s_num_zones; i++ )
	{
		if( ( s_zones[i].origin - origin ).Length() < SGTC_LINK_DEDUPE_RADIUS )
			return;
	}

	SGTC_Bots_CopyVector( s_zones[s_num_zones].origin, origin );
	s_zones[s_num_zones].radius = radius;
	s_zones[s_num_zones].kind = kind;
	strlcpy( s_zones[s_num_zones].name, name ? name : "", sizeof( s_zones[s_num_zones].name ) );
	s_num_zones++;
}

static void SGTC_Bots_RegisterLink( const Vector &from, const Vector &to, int kind, const char *name )
{
	if( ( to - from ).Length() < SGTC_LINK_MIN_DISTANCE )
		return; // degenerate: same trigger/destination, or two points in one room

	if( s_num_links >= SGTC_MAX_LINKS )
		return;

	for( int i = 0; i < s_num_links; i++ )
	{
		if( ( s_links[i].from - from ).Length() < SGTC_LINK_DEDUPE_RADIUS &&
			( s_links[i].to - to ).Length() < SGTC_LINK_DEDUPE_RADIUS )
			return;
		if( ( s_links[i].from - to ).Length() < SGTC_LINK_DEDUPE_RADIUS &&
			( s_links[i].to - from ).Length() < SGTC_LINK_DEDUPE_RADIUS )
			return;
	}

	SGTC_Bots_CopyVector( s_links[s_num_links].from, from );
	SGTC_Bots_CopyVector( s_links[s_num_links].to, to );
	s_links[s_num_links].kind = kind;
	strlcpy( s_links[s_num_links].target, name ? name : "", sizeof( s_links[s_num_links].target ) );
	s_num_links++;
}

// Best-effort classification of "the bot jumped this far in one frame".
static int SGTC_Bots_ClassifyTeleport( const Vector &from, const Vector &to )
{
	int best = -1;
	float best_distance = 2.0f * SGTC_LINK_MATCH_RADIUS;

	for( int i = 0; i < s_num_links; i++ )
	{
		float distance = ( s_links[i].from - from ).Length() + ( s_links[i].to - to ).Length();

		if( distance < best_distance )
		{
			best_distance = distance;
			best = i;
		}
	}

	if( best >= 0 )
		return s_links[best].kind;

	// No registered pair: fall back to "did either end land in a gate/ring
	// zone". Gate wins over ring when both match.
	for( int pass = 0; pass < 2; pass++ )
	{
		const int want = pass == 0 ? SGTC_LINK_GATE : SGTC_LINK_RING;

		for( int i = 0; i < s_num_zones; i++ )
		{
			if( s_zones[i].kind != want )
				continue;
			if( ( s_zones[i].origin - from ).Length() < s_zones[i].radius ||
				( s_zones[i].origin - to ).Length() < s_zones[i].radius )
				return want;
		}
	}

	return SGTC_LINK_TELEPORT;
}

static int SGTC_Bots_ScanTeleportClass( const char *classname, int base_kind )
{
	edict_t *pent = NULL;
	int added = 0;

	while( !FNullEnt( pent = FIND_ENTITY_BY_CLASSNAME( pent, classname ) ) )
	{
		const char *targetname = STRING( pent->v.targetname );
		const int named_gate = SGTC_Bots_GateIndexFromTriggerName( targetname );

		if( FStringNull( pent->v.target ) )
		{
			// A gate teleport has no target until trigger_changetarget
			// installs the exit when the gate opens. Register the near-side
			// node now so the bot can dial; ResolveGateEntry() adds the actual
			// destination link once the target appears. Other targetless
			// teleports keep the old "no link" behaviour.
			if( named_gate >= 0 && base_kind == SGTC_LINK_TELEPORT )
			{
				Vector from = SGTC_Bots_EntityCenter( pent );
				const int entry_waypoint = WaypointAddTeleportNode( from );

				if( entry_waypoint >= 0 )
				{
					SGTC_Bots_AddGateEntry( entry_waypoint, from, named_gate,
						ENTINDEX( pent ), 0 );
					SGTC_Bots_AddZone( from, 0.0f, SGTC_LINK_GATE, targetname );
					added++;
				}
			}

			continue;
		}

		const char *target = STRING( pent->v.target );

		// Destination: first entity matching the target name whose position is
		// not the null origin (some maps name a logic entity first).
		edict_t *dest = NULL;
		edict_t *scan = NULL;
		while( !FNullEnt( scan = FIND_ENTITY_BY_TARGETNAME( scan, target ) ) )
		{
			Vector center = SGTC_Bots_EntityCenter( scan );
			if( center.Length() > 1.0f )
			{
				dest = scan;
				break;
			}
		}
		if( !dest )
			continue;

		Vector from = SGTC_Bots_EntityCenter( pent );
		Vector to = SGTC_Bots_EntityCenter( dest );

		int kind = base_kind;
		if( SGTC_Bots_IsGateRelayName( target ) || SGTC_Bots_IsGateRelayName( targetname ) )
			kind = SGTC_LINK_GATE;

		SGTC_Bots_RegisterLink( from, to, kind, target );
		const int entry_waypoint = WaypointAddTeleportLink( from, to );

		if( kind == SGTC_LINK_GATE && entry_waypoint >= 0 )
		{
			const int gate = named_gate >= 0
				? named_gate : SGTC_Bots_GateIndexFromName( target );
			SGTC_Bots_AddGateEntry( entry_waypoint, from, gate, ENTINDEX( pent ), 1 );
		}

		if( kind == SGTC_LINK_GATE || kind == SGTC_LINK_RING )
			SGTC_Bots_AddZone( from, 0.0f, kind, classname );

		added++;
	}

	return added;
}

// Resolve a deferred gate link. sg_stargate leaves gNtele_* without a target
// at worldspawn and installs it from trigger_changetarget when the gate opens;
// this runs on the first frame the bot waits at that entry and registers the
// from->to nav pair once the map has installed it. It never changes the
// trigger, so player gate behaviour is untouched.
static qboolean SGTC_Bots_ResolveGateEntry( sgtc_bot_gate_entry_t *entry )
{
	if( entry == NULL )
		return FALSE;

	if( entry->resolved )
		return TRUE;

	if( entry->trigger <= 0 )
		return FALSE;

	edict_t *trigger = INDEXENT( entry->trigger );
	if( FNullEnt( trigger ) || FStringNull( trigger->v.target ) )
		return FALSE;

	const char *target = STRING( trigger->v.target );
	if( !target[0] || FStrEq( target, "nowhere" ) )
		return FALSE;

	edict_t *dest = NULL;
	edict_t *scan = NULL;
	while( !FNullEnt( scan = FIND_ENTITY_BY_TARGETNAME( scan, target ) ) )
	{
		Vector center = SGTC_Bots_EntityCenter( scan );
		if( center.Length() > 1.0f )
		{
			dest = scan;
			break;
		}
	}
	if( dest == NULL )
		return FALSE;

	Vector from = SGTC_Bots_EntityCenter( trigger );
	Vector to = SGTC_Bots_EntityCenter( dest );

	SGTC_Bots_RegisterLink( from, to, SGTC_LINK_GATE, target );
	const int entry_waypoint = WaypointAddTeleportLink( from, to );

	if( entry_waypoint >= 0 && entry_waypoint != entry->waypoint )
	{
		// The scan-created node was not reused (should be rare): keep the
		// original entry for the route the bot is already on and add an alias
		// for the linked node so later waypoint choices also work.
		SGTC_Bots_AddGateEntry( entry_waypoint, from, entry->gate,
			entry->trigger, 1 );
	}

	// The link adds a far-side node after worldspawn's single route rebuild;
	// refresh the matrix once so the bot can actually path across the gate.
	if( entry_waypoint >= 0 )
		WaypointRebuildRoutes();

	SGTC_Bots_AddZone( from, 0.0f, SGTC_LINK_GATE, target );
	entry->resolved = 1;
	return TRUE;
}

// zone_gatetravel marks players for gate travel; it carries no target, so it is
// a *zone* used for classification rather than a from->to link.
static void SGTC_Bots_ScanGateZones( void )
{
	edict_t *pent = NULL;

	while( !FNullEnt( pent = FIND_ENTITY_BY_CLASSNAME( pent, "zone_gatetravel" ) ) )
	{
		Vector center = SGTC_Bots_EntityCenter( pent );
		float radius = ( fabs( pent->v.size.x ) > fabs( pent->v.size.y ) )
			? fabs( pent->v.size.x ) * 0.5f : fabs( pent->v.size.y ) * 0.5f;

		SGTC_Bots_AddZone( center, radius + 32.0f, SGTC_LINK_GATE, "zone_gatetravel" );
		WaypointAddTeleportNode( center );
	}
}

// multi_gate_N entities only carry sound/relay targets; the nav-relevant facts
// are their positions and the well-known relay target names they fire.
static void SGTC_Bots_ScanMultiGates( void )
{
	static const char *const relay_names[] =
	{
		"g1tele_x_on", "g2tele_x_on",
		"gxtele_1_on_tr_on1", "gxtele_2_on_tr_on1", "gxtele_3_on_tr_on1",
		"gxtele_4_on_tr_on1", "gxtele_5_on_tr_on1",
		"wormtelex_1", "wormtelex_2"
	};
	char classname[32];

	for( int i = 1; i <= 49; i++ )
	{
		snprintf( classname, sizeof( classname ), "multi_gate_%d", i );

		edict_t *pent = NULL;
		while( !FNullEnt( pent = FIND_ENTITY_BY_CLASSNAME( pent, classname ) ) )
		{
			Vector center = SGTC_Bots_EntityCenter( pent );
			if( center.Length() > 1.0f )
				SGTC_Bots_AddZone( center, 0.0f, SGTC_LINK_GATE, classname );

			CMultiGate *pGate = static_cast<CMultiGate *>( CBaseEntity::Instance( pent ) );
			if( pGate == NULL )
				continue;

			const char *targetname = STRING( pent->v.targetname );
			if( !targetname || !targetname[0] )
				targetname = classname;

			// Which gate owns this manager? The completion managers are the
			// ones whose default list contains g1tele_x_on / g2tele_x_on.
			for( int g = 0; g < 2; g++ )
			{
				char open_target[24];
				snprintf( open_target, sizeof( open_target ), "g%dtele_x_on", g + 1 );

				if( pGate->HasTarget( ALLOC_STRING( open_target ) ) )
				{
					strlcpy( s_gate_defs[g].open_name, targetname, sizeof( s_gate_defs[g].open_name ) );
					s_gate_observed_available[g] = 1;
				}

				for( int i = 0; i < SGTC_GATE_CHEVRONS; i++ )
				{
					char step_target[24];
					snprintf( step_target, sizeof( step_target ), "dial%dchange%d", g + 1, i + 1 );

					if( pGate->HasTarget( ALLOC_STRING( step_target ) ) )
						strlcpy( s_gate_defs[g].step_name[i], targetname,
							sizeof( s_gate_defs[g].step_name[i] ) );
				}
			}
		}
	}

	for( int i = 0; i < (int)( sizeof( relay_names ) / sizeof( relay_names[0] ) ); i++ )
	{
		edict_t *relay = FIND_ENTITY_BY_TARGETNAME( NULL, relay_names[i] );

		if( FNullEnt( relay ) )
			continue;

		Vector center = SGTC_Bots_EntityCenter( relay );
		if( center.Length() <= 1.0f )
			continue;

		SGTC_Bots_AddZone( center, 0.0f, SGTC_LINK_GATE, relay_names[i] );
		WaypointAddTeleportNode( center );
	}

	// A map may use a DHD button without shipping multi_gate managers
	// (sg_stargate is exactly that). The relay entity still exists, so mark
	// the observation channel available and require it before entry.
	if( !FNullEnt( FIND_ENTITY_BY_TARGETNAME( NULL, "g1tele_x_on" ) ) )
		s_gate_observed_available[0] = 1;
	if( !FNullEnt( FIND_ENTITY_BY_TARGETNAME( NULL, "g2tele_x_on" ) ) )
		s_gate_observed_available[1] = 1;
}

// The shipped sg_stargate/sg_conquest gates are not multi_gate entities: a
// func_button (dial1/dial2) fires stage1/2stage1, and each chevron's
// trigger_changetarget points the button at the next stage. Pressing the same
// button eight times is therefore the real DHD sequence, and stage8 arms the
// map's own g1tele_x_on / g2tele_x_on delay. Find those buttons once at
// worldspawn so the bot dials the map, not a synthetic timer.
static void SGTC_Bots_ScanGateButtons( void )
{
	edict_t *pent = NULL;

	while( !FNullEnt( pent = FIND_ENTITY_BY_CLASSNAME( pent, "func_button" ) ) )
	{
		const char *target = STRING( pent->v.target );
		const char *targetname = STRING( pent->v.targetname );
		int gate = -1;

		if( !target || !target[0] )
			continue;

		if( !strcmp( target, "stage1" ) || !strcmp( targetname, "dial1" ) )
			gate = 0;
		else if( !strcmp( target, "2stage1" ) || !strcmp( targetname, "dial2" ) )
			gate = 1;

		if( gate >= 0 && targetname && targetname[0] )
			strlcpy( s_gate_defs[gate].press_name, targetname, sizeof( s_gate_defs[gate].press_name ) );
	}
}

// ---------------------------------------------------------------------------
// deterministic team / class loadout
// ---------------------------------------------------------------------------

typedef struct
{
	qboolean alive;
	qboolean spawn_emitted;
	qboolean dead_loadout_done;
	qboolean alive_loadout_done;
	qboolean have_origin;
	Vector prev_origin;
	float last_stuck_event;
	unsigned int rng;
	int spread_attempts;	// anti-stack relocations this life (small maps)
} sgtc_bot_track_t;

static sgtc_bot_track_t s_track[32];

static void SGTC_Bots_ResetTrackers( void )
{
	// The cast keeps -Wclass-memaccess quiet: sgtc_bot_track_t holds a Vector,
	// which is a non-trivial type only by its constructor, and zeroing the
	// whole array is exactly the intended reset.
	memset( (void *)s_track, 0, sizeof( s_track ) );

	for( int i = 0; i < 32; i++ )
		s_track[i].rng = SGTC_Bots_MixSeed( s_seed_base, i );
}

// Class caps live in the cvar_t objects the rules code registers
// (dlls/sgtc_round.cpp), and sgtc_rules_priv.h is not for general inclusion, so
// the objects are declared here. Reading them directly keeps the bot pick in
// lockstep with SGTC_ClassCapped() even if a cvar name changes: the underlying
// names are max_oneill / max_soldier, not max_oniell / max_commander.
extern cvar_t sgtc_cvar_max_syslord;
extern cvar_t sgtc_cvar_max_anubis;
extern cvar_t sgtc_cvar_max_jaffa;
extern cvar_t sgtc_cvar_max_serpent;
extern cvar_t sgtc_cvar_max_horus;
extern cvar_t sgtc_cvar_max_oniell;
extern cvar_t sgtc_cvar_max_carter;
extern cvar_t sgtc_cvar_max_jackson;
extern cvar_t sgtc_cvar_max_tealc;
extern cvar_t sgtc_cvar_max_commander;

static void SGTC_Bots_BuildLoadoutState( sgtc_bot_loadout_state_t *st, int ignore_entindex )
{
	static const cvar_t *const torri_caps[6] =
	{
		NULL, &sgtc_cvar_max_oniell, &sgtc_cvar_max_carter, &sgtc_cvar_max_jackson,
		&sgtc_cvar_max_tealc, &sgtc_cvar_max_commander
	};
	static const cvar_t *const goauld_caps[6] =
	{
		NULL, &sgtc_cvar_max_syslord, &sgtc_cvar_max_anubis, &sgtc_cvar_max_jaffa,
		&sgtc_cvar_max_serpent, &sgtc_cvar_max_horus
	};

	SGTC_Bots_LoadoutStateInit( st );

	// Cap sources are the same cvar objects SGTC_ClassCapped() reads
	// (dlls/sgtc_round.cpp), so our pick cannot disagree with the engine's
	// respawn gate.
	for( int cls = SGTC_CLASS_FIRST; cls <= SGTC_CLASS_LAST; cls++ )
	{
		st->class_caps[SGTC_TEAM_TORRI][cls] = (int)torri_caps[cls]->value;
		st->class_caps[SGTC_TEAM_GOAULD][cls] = (int)goauld_caps[cls]->value;
	}

	// Same projection UpdateClassCount() uses: alive players, pev->team
	// (SgtcEntTeam) and the class in play.
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pl = (CBasePlayer *)UTIL_PlayerByIndex( i );

		if( !pl || !pl->IsAlive() )
			continue;
		if( ignore_entindex > 0 && pl->entindex() == ignore_entindex )
			continue;

		const int team = SGTC_EntTeamToTeam( (int)pl->pev->team );
		const int cls = pl->m_iSgtcPortraitShown;

		if( team == SGTC_TEAM_NONE || cls < SGTC_CLASS_FIRST || cls > SGTC_CLASS_LAST )
			continue;

		st->team_counts[team]++;
		st->class_counts[team][cls]++;
	}
}

// True for a bot (never for a human player): the fake-client flag is set by
// BotCreate_CreateFakeClient and the engine.
static qboolean SGTC_Bots_IsBotEdict( edict_t *pent )
{
	if( FNullEnt( pent ) )
		return FALSE;
	if( !FBitSet( pent->v.flags, FL_CLIENT ) )
		return FALSE;
	return FBitSet( pent->v.flags, FL_FAKECLIENT ) ? TRUE : FALSE;
}

static void SGTC_Bots_ApplyLoadout( int index, qboolean alive )
{
	edict_t *pent = bots[index].pEdict;
	CBasePlayer *pl = (CBasePlayer *)CBaseEntity::Instance( pent );

	if( !pl || !SGTC_Bots_IsBotEdict( pent ) )
		return;

	const int forced_team_cvar = (int)sgtc_bot_team.value;
	const int forced_class_cvar = (int)sgtc_bot_class.value;

	// The director's plan wins over the fixed sgtc_bot_team/class cvars when it
	// has an opinion for this slot; 0 means "no opinion, keep the cvar".
	const int director_team = SGTC_Director_ForcedTeam( index + 1 );
	const int director_class = SGTC_Director_ForcedClass( index + 1 );

	const int forced_team = director_team != 0 ? director_team : forced_team_cvar;
	const int forced_class = director_class != 0 ? director_class : forced_class_cvar;

	sgtc_bot_loadout_state_t st;
	SGTC_Bots_BuildLoadoutState( &st, alive ? pl->entindex() : 0 );

	sgtc_bot_track_t &track = s_track[index];

	const int team = SGTC_Bots_PickTeam( &st, forced_team, SGTC_Bots_NextRandom( &track.rng ) );
	int cls = SGTC_Bots_PickClass( team, st.class_caps[team], st.class_counts[team],
		forced_class, SGTC_Bots_NextRandom( &track.rng ) );

	if( cls == SGTC_CLASS_NONE )
		return; // every class capped: leave the engine's own re-roll in charge

	char team_name[16];
	char model[32];

	if( SGTC_Bots_TeamName( team, team_name, sizeof( team_name ) ) != 0 )
		return;
	if( SGTC_Bots_ClassModelName( team, cls, model, sizeof( model ) ) != 0 )
		return;

	strlcpy( pl->m_szTeamName, team_name, sizeof( pl->m_szTeamName ) );
	pl->m_iSgtcTeam = team;
	pl->m_iSgtcPortraitSel = cls;
	if( alive )
		pl->m_iSgtcPortraitShown = cls;

	// SgtcEntTeam numbering: pev->team is the OPPOSITE of m_iSgtcTeam.
	pl->pev->team = SGTC_TeamToEntTeam( team );

	// Keep the client keys consistent with what ChangePlayerTeam and
	// SGTC_ApplyUserInfoTeam publish, so name-based team lookups agree.
	char *infobuf = g_engfuncs.pfnGetInfoKeyBuffer( pent );
	const int entindex = pl->entindex();

	g_engfuncs.pfnSetClientKeyValue( entindex, infobuf, "team", team_name );
	g_engfuncs.pfnSetClientKeyValue( entindex, infobuf, "model", model );

	// pfnSetClientKeyValue only changes the userinfo key, which the engine applies
	// at the NEXT spawn — a bot that is already alive kept rendering the default
	// player.mdl (John: "bots look like vanilla rigs"). Push the SGTC class studio
	// model onto the live edict as well.
	if( alive )
	{
		char studio[96];
		snprintf( studio, sizeof( studio ), "models/player/%s/%s.mdl", model, model );
		SET_MODEL( pent, studio );
		// build-9: on maps without team spawn entities the bot's class is only
		// known after CBasePlayer::Spawn, so the team branch never applied the
		// class speed/armor; do it now so the bot can move.
		extern void SGTC_SpawnApplyClassStatsAlive( CBasePlayer *pPlayer );
		SGTC_SpawnApplyClassStatsAlive( pl );
	}


	// The plan's weapons become the grant list for the next spawn: clear the
	// copy, then mark exactly the slots the director accepted for this
	// team/class. No usable weapon (count 0) leaves the previous behaviour -
	// the engine/loadout choice - untouched.
	int slots[SGTC_NUM_WEAPON_SLOTS];
	const int slot_count = SGTC_Director_BotWeaponSlots( index + 1, team, cls,
		slots, SGTC_NUM_WEAPON_SLOTS );
	if( slot_count > 0 )
	{
		for( int s = 0; s < SGTC_NUM_WEAPON_SLOTS; s++ )
			pl->m_iSgtcGondArgs[s] = 0;
		for( int s = 0; s < slot_count; s++ )
			pl->m_iSgtcGondArgs[slots[s]] = 1;
	}

	if( alive )
		SGTC_Bots_GiveSpawnLoadout( pl );	// first life has no spawn-time gond list

	BotTrace( bots[index], "loadout: team=%d class=%d alive=%d", team, cls, alive ? 1 : 0 );
}

// ---------------------------------------------------------------------------
// objective watch
// ---------------------------------------------------------------------------

#define SGTC_MAX_OBJECTIVES 32

typedef struct
{
	edict_t *pent;
	int fingerprint;
	qboolean used;
} sgtc_bot_objective_t;

static sgtc_bot_objective_t s_objectives[SGTC_MAX_OBJECTIVES];
static int s_num_objectives;
static float s_next_objective_poll;

static void SGTC_Bots_ResetObjectives( void )
{
	memset( s_objectives, 0, sizeof( s_objectives ) );
	s_num_objectives = 0;
	s_next_objective_poll = 0.0f;
}

static int SGTC_Bots_ObjectiveFingerprint( edict_t *pent )
{
	// Only engine-visible fields: objective entities keep their real state in
	// C++ members this translation unit cannot see. A change in any of these
	// still means "the entity did something".
	int fp = (int)pent->v.team;
	fp = fp * 31 + (int)pent->v.health;
	fp = fp * 31 + (int)pent->v.frags;
	fp = fp * 31 + (int)pent->v.iuser1;
	fp = fp * 31 + (int)pent->v.iuser2;
	fp = fp * 31 + (int)pent->v.iuser3;
	fp = fp * 31 + (int)pent->v.iuser4;
	fp = fp * 31 + (int)pent->v.frame;
	fp = fp * 31 + (int)pent->v.solid;
	fp = fp * 31 + (int)pent->v.spawnflags;
	fp = fp * 31 + ( FNullEnt( pent->v.owner ) ? 0 : ENTINDEX( pent->v.owner ) );
	return fp;
}

static qboolean SGTC_Bots_IsObjectiveClass( const char *classname )
{
	return FStrEq( classname, "trigger_goal" ) ||
		FStrEq( classname, "zone_capture" ) ||
		FStrEq( classname, "game_nuke" );
}

static void SGTC_Bots_CollectObjectives( void )
{
	static const char *const objective_classes[] = { "trigger_goal", "zone_capture", "game_nuke" };

	SGTC_Bots_ResetObjectives();

	for( int c = 0; c < (int)( sizeof( objective_classes ) / sizeof( objective_classes[0] ) ); c++ )
	{
		edict_t *pent = NULL;

		while( !FNullEnt( pent = FIND_ENTITY_BY_CLASSNAME( pent, objective_classes[c] ) ) )
		{
			if( s_num_objectives >= SGTC_MAX_OBJECTIVES )
				return;

			s_objectives[s_num_objectives].pent = pent;
			s_objectives[s_num_objectives].fingerprint = SGTC_Bots_ObjectiveFingerprint( pent );
			s_objectives[s_num_objectives].used = TRUE;
			s_num_objectives++;
		}
	}
}

// Search radius for the radio's PUSH focus point, world units.
#define SGTC_RADIO_OBJECTIVE_RADIUS 4096.0f

// Nearest objective to `from`: the focus point a PUSH order is aimed at.
// Objective entities are not removed when captured (they change state), so
// liveness here is "still in the objective watch list and not removed".
qboolean SGTC_Bots_RadioObjectiveArea( const Vector &from, Vector *out )
{
	qboolean found = FALSE;
	float best = SGTC_RADIO_OBJECTIVE_RADIUS;

	if( out == NULL )
		return FALSE;

	for( int i = 0; i < s_num_objectives; i++ )
	{
		if( !s_objectives[i].used )
			continue;

		edict_t *pent = s_objectives[i].pent;

		if( FNullEnt( pent ) )
			continue;

		Vector center = SGTC_Bots_EntityCenter( pent );
		float distance = ( center - from ).Length();

		if( distance > best )
			continue;

		best = distance;
		*out = center;
		found = TRUE;
	}

	return found;
}

static void SGTC_Bots_PollObjectives( void )
{
	if( s_num_objectives == 0 )
		return;
	if( s_next_objective_poll > gpGlobals->time )
		return;

	s_next_objective_poll = gpGlobals->time + 0.25f;

	for( int i = 0; i < s_num_objectives; i++ )
	{
		edict_t *pent = s_objectives[i].pent;

		if( FNullEnt( pent ) )
			continue;

		const int fp = SGTC_Bots_ObjectiveFingerprint( pent );

		if( fp == s_objectives[i].fingerprint )
			continue;

		s_objectives[i].fingerprint = fp;

		// Attribute to the nearest bot standing on it, when there is one.
		char nearest_name[64];
		nearest_name[0] = '\0';
		float nearest_distance = 512.0f;

		for( int b = 0; b < 32; b++ )
		{
			if( !bots[b].is_used || FNullEnt( bots[b].pEdict ) )
				continue;

			float distance = ( bots[b].pEdict->v.origin - SGTC_Bots_EntityCenter( pent ) ).Length();

			if( distance < nearest_distance )
			{
				nearest_distance = distance;
				SGTC_Bots_BotName( b, nearest_name, sizeof( nearest_name ) );
			}
		}

		const char *classname = STRING( pent->v.classname );
		char detail[64];

		snprintf( detail, sizeof( detail ), "state team=%d health=%d frags=%d",
			(int)pent->v.team, (int)pent->v.health, (int)pent->v.frags );

		sgtc_bot_event_field_t fields[2];
		SGTC_Bots_AddFieldString( &fields[0], "kind", SGTC_Bots_IsObjectiveClass( classname ) ? classname : "objective" );
		SGTC_Bots_AddFieldString( &fields[1], "detail", detail );

		SGTC_Bots_Emit( "objective", nearest_name, fields, 2, FALSE );
	}
}

// ---------------------------------------------------------------------------
// per-frame tracking: loadout, spawn/death/teleport/stuck events
// ---------------------------------------------------------------------------

// Store DeathMsg's third-field semantics in the bot slot. The prefix strip
// mirrors CHalfLifeMultiplay::DeathNotice() so `weapon` reads the same as the
// event a client would see ("trapbox", "9mmAR", "bolt"). "teammate" is the
// CStargateTeamDM::DeathNotice() marker and is not an inflictor class.
static void SGTC_Bots_StoreDeathWeapon( char *dst, int dst_size, const char *weapon_name )
{
	if( !weapon_name || !weapon_name[0] )
		weapon_name = "world";
	else if( strncmp( weapon_name, "weapon_", 7 ) == 0 )
		weapon_name += 7;
	else if( strncmp( weapon_name, "monster_", 8 ) == 0 )
		weapon_name += 8;
	else if( strncmp( weapon_name, "func_", 5 ) == 0 )
		weapon_name += 5;
	else if( strncmp( weapon_name, "crossbow_bolt", 13 ) == 0 )
		weapon_name = "bolt";

	strlcpy( dst, weapon_name, dst_size );
}

// The live native capture. DeathMsg is not mirrored by the native build's
// user-message registry, so BotClient_Valve_DeathMsg never runs and this is
// the only writer of the pre-park origin/weapon fields. Called from
// CBasePlayer::Killed() before PostThink parks the victim.
extern "C" void SGTC_Bots_CaptureDeathState( int slot, const float origin[3],
	const char *weapon_name )
{
	if( slot < 0 || slot >= 32 )
		return;

	if( origin )
	{
		bots[slot].v_death_origin.x = origin[0];
		bots[slot].v_death_origin.y = origin[1];
		bots[slot].v_death_origin.z = origin[2];
		bots[slot].b_death_snapshot_valid = TRUE;
	}
	else
	{
		// A death we could not observe is not a valid pre-park position; let
		// the tracker fall back to its last-alive frame (or the 4096 sentinel)
		// and omit `weapon` rather than carry a stale/default name.
		bots[slot].b_death_snapshot_valid = FALSE;
		bots[slot].sz_death_weapon[0] = '\0';
		return;
	}

	SGTC_Bots_StoreDeathWeapon( bots[slot].sz_death_weapon,
		sizeof( bots[slot].sz_death_weapon ), weapon_name );
}

// Test/telemetry reader for the slot capture. Returns 1 when a pre-park origin
// is stored. Fills out_origin/out_weapon when the pointers are non-NULL.
extern "C" int SGTC_Bots_GetDeathState( int slot, float out_origin[3],
	char *out_weapon, int weapon_size )
{
	if( slot < 0 || slot >= 32 )
		return 0;

	if( out_origin )
	{
		out_origin[0] = bots[slot].v_death_origin.x;
		out_origin[1] = bots[slot].v_death_origin.y;
		out_origin[2] = bots[slot].v_death_origin.z;
	}

	if( out_weapon && weapon_size > 0 )
		strlcpy( out_weapon, bots[slot].sz_death_weapon, weapon_size );

	return bots[slot].b_death_snapshot_valid ? 1 : 0;
}

void SGTC_Bots_OnPlayerKilled( int victim_index, const char *weapon_name )
{
	edict_t *pent;
	float origin[3];
	int index;

	if( victim_index < 1 )
		return;

	pent = INDEXENT( victim_index );

	if( FNullEnt( pent ) )
		return;

	index = UTIL_GetBotIndex( pent );

	if( index < 0 )
		return;

	origin[0] = pent->v.origin.x;
	origin[1] = pent->v.origin.y;
	origin[2] = pent->v.origin.z;

	SGTC_Bots_CaptureDeathState( index, origin, weapon_name );
}

static void SGTC_Bots_EmitSpawn( int index, CBasePlayer *pl )
{
	edict_t *pent = bots[index].pEdict;
	char name[64];
	char team_name[16];

	SGTC_Bots_BotName( index, name, sizeof( name ) );

	const int team = SGTC_EntTeamToTeam( (int)pl->pev->team );
	int cls = pl->m_iSgtcPortraitShown;

	if( cls == SGTC_CLASS_NONE )
		cls = pl->m_iSgtcPortraitSel;

	if( SGTC_Bots_TeamName( team, team_name, sizeof( team_name ) ) != 0 )
		team_name[0] = '\0';

	sgtc_bot_event_field_t fields[4];
	SGTC_Bots_AddFieldInt( &fields[0], "team", (int)pent->v.team );
	SGTC_Bots_AddFieldString( &fields[1], "team_name", team_name );
	SGTC_Bots_AddFieldInt( &fields[2], "class", cls );
	SGTC_Bots_AddFieldVec( &fields[3], "origin", pent->v.origin );

	SGTC_Bots_Emit( "spawn", name, fields, 4, FALSE );
}

static void SGTC_Bots_EmitDeath( int index )
{
	char name[64];
	char killer[64];
	edict_t *killer_edict = bots[index].killer_edict;

	SGTC_Bots_BotName( index, name, sizeof( name ) );

	if( FNullEnt( killer_edict ) || ENTINDEX( killer_edict ) <= 0 )
	{
		strlcpy( killer, "world", sizeof( killer ) );
	}
	else if( FBitSet( killer_edict->v.flags, FL_CLIENT ) )
	{
		const int killer_index = UTIL_GetBotIndex( killer_edict );

		if( killer_index >= 0 )
		{
			SGTC_Bots_BotName( killer_index, killer, sizeof( killer ) );
		}
		else
		{
			strlcpy( killer, STRING( killer_edict->v.netname ), sizeof( killer ) );
			bot_name_sanitize( killer );
		}
	}
	else
	{
		strlcpy( killer, "world", sizeof( killer ) );
	}

	if( !killer[0] )
		strlcpy( killer, "world", sizeof( killer ) );

	// The per-frame tracker only notices a death after the victim's post-think.
	// SGTC_SgtcThink parks every dead player at SGTC_OFFMAP_COORD, so the live
	// edict origin here is the sentinel, not where the bot died. Prefer the
	// CBasePlayer::Killed capture (the exact pre-park position), then the last
	// frame the tracker saw it alive.
	float snapshot[3];
	float last_alive[3];
	float current[3];
	float origin[3];

	snapshot[0] = bots[index].v_death_origin.x;
	snapshot[1] = bots[index].v_death_origin.y;
	snapshot[2] = bots[index].v_death_origin.z;

	last_alive[0] = s_track[index].prev_origin.x;
	last_alive[1] = s_track[index].prev_origin.y;
	last_alive[2] = s_track[index].prev_origin.z;

	current[0] = bots[index].pEdict->v.origin.x;
	current[1] = bots[index].pEdict->v.origin.y;
	current[2] = bots[index].pEdict->v.origin.z;

	SGTC_Bots_ResolveDeathOrigin( bots[index].b_death_snapshot_valid, snapshot,
		s_track[index].have_origin, last_alive, current, origin );

	sgtc_bot_event_field_t fields[3];
	SGTC_Bots_AddFieldString( &fields[0], "killer", killer );
	SGTC_Bots_AddFieldVec( &fields[1], "origin", Vector( origin[0], origin[1], origin[2] ) );

	int field_count = 2;

	// DeathMsg's third field is the inflictor classname, or the killer's
	// active weapon when the inflictor is the killer ("trapbox", "9mmAR",
	// "world"). The killer byte is 0 for every non-client killer, so without
	// this a monster kill and a world kill look identical.
	if( bots[index].sz_death_weapon[0] )
	{
		SGTC_Bots_AddFieldString( &fields[2], "weapon", bots[index].sz_death_weapon );
		field_count = 3;
	}

	SGTC_Bots_Emit( "death", name, fields, field_count, FALSE );

	// One-shot: never let this death's snapshot leak into the next spawn.
	bots[index].b_death_snapshot_valid = FALSE;
	bots[index].sz_death_weapon[0] = '\0';
}

static void SGTC_Bots_EmitTeleport( int index, const Vector &from, const Vector &to )
{
	char name[64];
	const int kind = SGTC_Bots_ClassifyTeleport( from, to );
	const char *event = "teleport";

	if( kind == SGTC_LINK_GATE )
		event = "gate_use";
	else if( kind == SGTC_LINK_RING )
		event = "ring_use";

	SGTC_Bots_BotName( index, name, sizeof( name ) );

	sgtc_bot_event_field_t fields[3];
	SGTC_Bots_AddFieldVec( &fields[0], "from", from );
	SGTC_Bots_AddFieldVec( &fields[1], "to", to );
	SGTC_Bots_AddFieldFloat( &fields[2], "distance", ( to - from ).Length() );

	SGTC_Bots_Emit( event, name, fields, 3, FALSE );
}

static void SGTC_Bots_EmitStuck( int index )
{
	bot_t &pBot = bots[index];
	char name[64];

	// Rate limit: one `stuck` event per bot per 5 seconds.
	if( s_track[index].last_stuck_event > 0.0f &&
		gpGlobals->time - s_track[index].last_stuck_event < 5.0f )
	{
		pBot.b_stuck_event_pending = FALSE;
		return;
	}

	s_track[index].last_stuck_event = gpGlobals->time;

	SGTC_Bots_BotName( index, name, sizeof( name ) );

	sgtc_bot_event_field_t fields[4];
	SGTC_Bots_AddFieldFloat( &fields[0], "duration", pBot.f_stuck_event_duration );
	SGTC_Bots_AddFieldInt( &fields[1], "goal", pBot.waypoint_goal );
	SGTC_Bots_AddFieldString( &fields[2], "action",
		pBot.stuck_event_action[0] ? pBot.stuck_event_action : "jump" );
	SGTC_Bots_AddFieldVec( &fields[3], "origin", pBot.pEdict->v.origin );

	SGTC_Bots_Emit( "stuck", name, fields, 4, FALSE );

	pBot.b_stuck_event_pending = FALSE;
}

// ---------------------------------------------------------------------------
// gate timing observers and per-bot driver
// ---------------------------------------------------------------------------

// Called from FireTargets() before it dispatches a target. This is an
// observer only: it never changes the entity, so ordinary player gate use keeps
// the map's own timing. It lets bots use a gate that a player dialed, and stop
// approaching one that a player closed.
void SGTC_Bots_ObserveGateRelay( const char *target, float now )
{
	if( !SGTC_Bots_Enabled() || !target || !target[0] )
		return;

	if( FStrEq( target, "g1tele_x_on" ) )
	{
		s_gate_opened_seen[0] = 1;
		SgtcGateTiming_ObserveOpened( &s_gate_timing[0], now );
	}
	else if( FStrEq( target, "g2tele_x_on" ) )
	{
		s_gate_opened_seen[1] = 1;
		SgtcGateTiming_ObserveOpened( &s_gate_timing[1], now );
	}
	else if( FStrEq( target, "g1tele_x_off" ) )
	{
		s_gate_opened_seen[0] = 0;
		SgtcGateTiming_ObserveClosed( &s_gate_timing[0], now );
	}
	else if( FStrEq( target, "g2tele_x_off" ) )
	{
		s_gate_opened_seen[1] = 0;
		SgtcGateTiming_ObserveClosed( &s_gate_timing[1], now );
	}
}

static void SGTC_Bots_GateEmit( bot_t &pBot, int gate_id, const char *action, int chevrons )
{
	char name[64];
	int slot = UTIL_GetBotIndex( pBot.pEdict );

	if( slot < 0 )
		return;

	SGTC_Bots_BotName( slot, name, sizeof( name ) );

	sgtc_bot_event_field_t fields[3];
	SGTC_Bots_AddFieldInt( &fields[0], "gate", gate_id );
	SGTC_Bots_AddFieldString( &fields[1], "action", action );
	SGTC_Bots_AddFieldInt( &fields[2], "chevron", chevrons );

	SGTC_Bots_Emit( "gate", name, fields, 3, FALSE );
}

// One frame of gate traversal for a bot. Called from BotThinkFinalizeMovement()
// before BotRunPlayerMove(), after navigation has chosen curr_waypoint_index.
//
// A bot whose next waypoint is a gate entry:
//   * waits short of the trigger while the model is dialing/warming/missed,
//   * dials through the map's own multi_gate chevron managers when closed,
//   * walks in only while the model's open window is valid.
void SGTC_Bots_GateTimingFrame( bot_t &pBot )
{
	if( !SGTC_Bots_Enabled() || FNullEnt( pBot.pEdict ) )
		return;

	const int waypoint = pBot.curr_waypoint_index;
	if( waypoint < 0 || waypoint >= num_waypoints )
		return;

	sgtc_bot_gate_entry_t *entry = SGTC_Bots_GateEntryForWaypoint( waypoint );
	if( entry == NULL )
		return;

	// sg_stargate installs the gate teleport's target only when the gate opens.
	// The entry exists from worldspawn, so resolve its nav link lazily here.
	if( !entry->resolved )
		SGTC_Bots_ResolveGateEntry( entry );

	const float now = gpGlobals->time;
	sgtc_gate_timing_t &timing = s_gate_timing[entry->gate];
	sgtc_bot_gate_def_t &def = s_gate_defs[entry->gate];

	SgtcGateTiming_Tick( &timing, now );

	if( timing.state == SGTC_GATE_STATE_MISSED )
		s_gate_opened_seen[entry->gate] = 0;

	// Do not take movement away from a bot that is fighting. It may still
	// cross incidentally (the old behaviour) but a quiet bot never does.
	if( pBot.pBotEnemy != NULL && !SgtcGateTiming_CanEnter( &timing, now ) )
		return;

	sgtc_bot_gate_ctx_t ctx;
	ctx.at_entry = 1;
	ctx.distance = ( pBot.waypoint_origin - pBot.pEdict->v.origin ).Length();
	ctx.can_dial = ( pBot.pBotEnemy == NULL ) ? 1 : 0;
	// The pure model says when the window is valid; the map's own
	// g1tele_x_on/g2tele_x_on relay says the event horizon is actually up.
	// Both must agree before the bot walks in, so a slow map dial cannot be
	// crossed early. A map without that relay keeps the model-only path
	// instead of never opening.
	//
	// The relay is fired by the dial itself, so it must only veto ENTER. It
	// must not veto DIAL: on sg_stargate g1tele_x_on exists from worldspawn
	// (s_gate_observed_available=1) but s_gate_opened_seen stays 0 until the
	// bot has pressed all eight chevrons. Feeding that 0 into the decision
	// made it return WAIT forever and the bot never started the dial.
	const qboolean map_says_open = !s_gate_observed_available[entry->gate] ||
		s_gate_opened_seen[entry->gate];
	ctx.allow_enter = ( timing.state == SGTC_GATE_STATE_OPEN ) ? map_says_open : 1;

	const int action = SgtcBots_GateDecision( &timing, &ctx, now );
	if( action == SGTC_BOT_GATE_NONE )
		return;

	const int slot = UTIL_GetBotIndex( pBot.pEdict );
	if( slot < 0 )
		return;

	if( action != SGTC_BOT_GATE_ENTER )
	{
		// Stop short of the event horizon and face it. f_waypoint_time is
		// refreshed so the jk_botti 5 s nav timeout does not throw away the
		// route while a 4.40 s dial + 2.15 s warm-up runs.
		pBot.f_move_speed = 0.0f;
		pBot.b_not_maxspeed = TRUE;
		pBot.f_waypoint_time = now;

		pBot.pEdict->v.button &= ~( IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT );

		Vector to_gate = pBot.waypoint_origin - GetGunPosition( pBot.pEdict );
		Vector angles = UTIL_VecToAngles( to_gate );
		pBot.pEdict->v.idealpitch = -angles.x;
		pBot.pEdict->v.ideal_yaw = angles.y;
	}

	if( action == SGTC_BOT_GATE_DIAL )
	{
		if( SgtcGateTiming_BeginDial( &timing, now ) )
		{
			def.dial_owner = slot;
			def.last_fired_chevron = 0;
			def.open_fired = 0;
			SGTC_Bots_GateEmit( pBot, def.id, "dial", 0 );
		}
	}

	if( def.dial_owner != slot )
		return;

	// Fire each newly locked step once. The eighth Tick() moves the model
	// straight from DIALING to WARMUP, so gate the loop on the pending press
	// (chevrons_locked > last_fired_chevron), not on the current state; the
	// old state test dropped the eighth press and sg_stargate never opened.
	// A real DHD button advances its own stage chain, so press the button
	// again; a map that only ships the multi_gate managers gets manager N and
	// its completion manager.
	while( timing.chevrons_locked > def.last_fired_chevron &&
		def.last_fired_chevron < SGTC_GATE_DIAL_STEPS )
	{
		def.last_fired_chevron++;
		if( def.press_name[0] )
		{
			FireTargets( def.press_name, CBaseEntity::Instance( pBot.pEdict ),
				CBaseEntity::Instance( pBot.pEdict ), USE_TOGGLE, 0 );
		}
		else if( def.last_fired_chevron <= SGTC_GATE_CHEVRONS )
		{
			FireTargets( def.step_name[def.last_fired_chevron - 1],
				CBaseEntity::Instance( pBot.pEdict ),
				CBaseEntity::Instance( pBot.pEdict ), USE_TOGGLE, 0 );
		}
		else
		{
			FireTargets( def.open_name, CBaseEntity::Instance( pBot.pEdict ),
				CBaseEntity::Instance( pBot.pEdict ), USE_TOGGLE, 0 );
			def.open_fired = 1;
		}
		SGTC_Bots_GateEmit( pBot, def.id, "chevron", def.last_fired_chevron );
	}

	// multi_gate maps may complete the dial on the eighth press without a
	// button; fire the opening manager once if the loop above did not.
	if( !def.press_name[0] && timing.state == SGTC_GATE_STATE_WARMUP && !def.open_fired )
	{
		FireTargets( def.open_name, CBaseEntity::Instance( pBot.pEdict ),
			CBaseEntity::Instance( pBot.pEdict ), USE_TOGGLE, 0 );
		def.open_fired = 1;
		SGTC_Bots_GateEmit( pBot, def.id, "open", SGTC_GATE_DIAL_STEPS );
	}
}

// build-9: fake clients on SP-style maps can be left at the engine's unspawned
// 4096 sentinel (no DM/team spawn point, lost respawn).  Track how long a bot
// has been there and respawn it through the normal gamerules path.
static float s_sgtcBotSentinelSince[32];
extern void respawn( entvars_t *pev, BOOL fCopyCorpse );

static void SGTC_Bots_TrackBots( void )
{
	for( int i = 0; i < 32; i++ )
	{
		bot_t &pBot = bots[i];
		sgtc_bot_track_t &track = s_track[i];
		edict_t *pent = pBot.pEdict;

		if( !pBot.is_used || FNullEnt( pent ) )
		{
			track.alive = FALSE;
			track.have_origin = FALSE;
			s_sgtcBotSentinelSince[i] = 0.0f;
			continue;
		}

		CBasePlayer *pl = (CBasePlayer *)CBaseEntity::Instance( pent );
		const qboolean alive = ( pl && pl->IsAlive() && SGTC_Bots_IsBotEdict( pent ) ) ? TRUE : FALSE;

		// build-9: keep respawned fallback-map bots armed.  GiveSpawnLoadout()
		// is idempotent (returns when a combat weapon is carried), so a slow
		// 2 s safety pass repairs bots whose first-life/respawn grant raced the
		// class resolution.
		static float s_sgtcBotRearmNext[32];
		if( alive && gpGlobals->time >= s_sgtcBotRearmNext[i] )
		{
			s_sgtcBotRearmNext[i] = gpGlobals->time + 2.0f;
			const unsigned int uiWeaponBits = (unsigned int)pl->pev->weapons;
			// A bot carrying only the team melee weapon (0 or one bit) lost its
			// kit on a death/respawn race.  Drop the stale item list and re-grant
			// the class kit; idempotent once the bits are back.
			SGTC_Bots_GiveSpawnLoadout( pl );
		}

		// Unspawned fake clients sit at 4096,4096,4096.  If that persists, push
		// them through respawn() (SP-style maps without info_player_deathmatch
		// now have the info_player_start/path_corner fallback above).
		if( !alive && pl && pl->pev )
		{
			const Vector &vecOrigin = pl->pev->origin;
			const qboolean sentinel = ( vecOrigin.x >= 4000.0f || vecOrigin.y >= 4000.0f ||
				vecOrigin.z >= 4000.0f ) ? TRUE : FALSE;
			if( sentinel )
			{
				if( s_sgtcBotSentinelSince[i] == 0.0f )
					s_sgtcBotSentinelSince[i] = gpGlobals->time;
				else if( gpGlobals->time - s_sgtcBotSentinelSince[i] > 2.0f )
				{
					s_sgtcBotSentinelSince[i] = gpGlobals->time;
					respawn( pl->pev, 0 );
					BotTrace( pBot, "sentinel respawn" );
				}
			}
			else
				s_sgtcBotSentinelSince[i] = 0.0f;
		}

		if( alive )
		{
			if( !track.alive )
			{
				// Respawned (or first seen alive): make sure a loadout is in
				// place, then report the spawn with what is actually in play.
				// Always run the alive loadout once per life: the dead-phase pass
				// only writes the pending class selection, so gating on
				// dead_loadout_done left the class model/infokey never applied
				// (bots rendered the default player.mdl after #238's spawn flow).
				if( !track.alive_loadout_done )
					SGTC_Bots_ApplyLoadout( i, TRUE );

				track.alive_loadout_done = TRUE;
				SGTC_Bots_GiveSpawnLoadout( pl );	// no-op once armed

				if( !track.spawn_emitted )
				{
					SGTC_Bots_EmitSpawn( i, pl );
					track.spawn_emitted = TRUE;
				}

				// The origin delta across a respawn (death spot -> spawn point,
				// or 0,0,0 on the first frame) is not a teleporter: restart the
				// tracking window at the spawn position.
				track.have_origin = FALSE;

				// Anti-stack: small maps (gateroom) spawn every fake client on
				// one info_player_deathmatch point, so bots render inside each
				// other. Nudge a bot that shares a spot with anyone else; the
				// tick retries until clear (max 8 per life).
				if( track.spread_attempts < 8 )
				{
					CBaseEntity *hitter = NULL;
					for( int j = 1; j <= gpGlobals->maxClients; j++ )
					{
						CBaseEntity *other = UTIL_PlayerByIndex( j );
						if( !other || other == pl )
							continue;
						if( ( other->pev->origin - pl->pev->origin ).Length() < 44.0f )
						{
							hitter = other;
							break;
						}
					}
					if( hitter )
					{
						track.spread_attempts++;
						const float a = (float)( SGTC_Bots_NextRandom( &track.rng ) % 628 ) * 0.01f;
						Vector spot = pl->pev->origin + Vector( cosf( a ) * 56.0f, sinf( a ) * 56.0f, 0.0f );
						TraceResult tr;
						UTIL_TraceLine( spot + Vector( 0, 0, 40 ), spot - Vector( 0, 0, 80 ),
							ignore_monsters, pent, &tr );
						if( !tr.fStartSolid && !tr.fAllSolid )
							spot = tr.vecEndPos;
						UTIL_SetOrigin( pl->pev, spot );
						pl->pev->velocity = Vector( 0, 0, 0 );
						BotTrace( pBot, "spread %d from ent %d", track.spread_attempts, hitter->entindex() );
					}
				}

				// A death snapshot is one-shot. If a death was never polled
				// (kicked bot), do not carry its origin into this life.
				pBot.b_death_snapshot_valid = FALSE;
				pBot.sz_death_weapon[0] = '\0';
			}

			if( track.have_origin )
			{
				const Vector from = track.prev_origin;
				const Vector to = pent->v.origin;

				if( ( to - from ).Length() > 512.0f )
					SGTC_Bots_EmitTeleport( i, from, to );
			}
		}
		else
		{
			if( track.alive )
			{
				SGTC_Bots_EmitDeath( i );

				for( int g = 0; g < 2; g++ )
				{
					if( s_gate_defs[g].dial_owner == i &&
						( s_gate_timing[g].state == SGTC_GATE_STATE_DIALING ||
						  s_gate_timing[g].state == SGTC_GATE_STATE_WARMUP ) )
					{
						SgtcGateTiming_Abort( &s_gate_timing[g], gpGlobals->time );
						s_gate_defs[g].dial_owner = -1;
					}
				}

				track.spawn_emitted = FALSE;
				track.dead_loadout_done = FALSE;
				track.alive_loadout_done = FALSE;
				track.spread_attempts = 0;
			}

			// While dead, write the *pending* class selection: the rules code
			// (FPlayerCanRespawn) consumes m_iSgtcPortraitSel into
			// m_iSgtcPortraitShown and applies the class stats on spawn.
			if( !track.dead_loadout_done )
			{
				SGTC_Bots_ApplyLoadout( i, FALSE );
				track.dead_loadout_done = TRUE;
			}
		}

		if( pBot.b_stuck_event_pending )
			SGTC_Bots_EmitStuck( i );

		SGTC_Bots_CopyVector( track.prev_origin, pent->v.origin );
		track.have_origin = TRUE;
		track.alive = alive;
	}
}

// ---------------------------------------------------------------------------
// server commands
// ---------------------------------------------------------------------------

static int SGTC_Bots_CountUsed( void )
{
	int count = 0;

	for( int i = 0; i < 32; i++ )
	{
		if( bots[i].is_used )
			count++;
	}

	return count;
}

// mp_freeguns lives in dlls/sgtc_round.cpp as a cvar_t object. sgtc_rules_priv.h
// is not for general inclusion, so declare it here like the class-cap cvars
// above; reading the object keeps this decision in lockstep with the grant path
// in sgtc_rules_spawn.cpp.
extern cvar_t sgtc_cvar_freeguns;

// Reserve for the current spawn loadout. mp_freeguns > 1 makes PlayerSpawn
// grant every weapon slot (up to 29 edicts), so that path needs the 31-edict
// reserve; any other loadout is a normal class kit (~15 edicts) and uses 16.
// BotCreate and the count-aware sgtc_bot_add pre-check both call this, so they
// cannot make different choices.
extern BOOL SGTC_ReaDebugActive( void );

extern "C" int SGTC_Bots_EdictBudgetMarginForLoadout( void )
{
	// build-20: sgtc_rea_debug arms every player with the full 31-weapon table
	// (and repeats it on every spawn/loadout), which is the same worst case as
	// the CP mp_freeguns spawn.  Ignoring it here was the sg_stargate
	// SV_AllocEdict crash with 7 bots.
	const BOOL bRea = SGTC_ReaDebugActive();
	// Bots are NOT given the rea_debug 31-weapon kit any more (see
	// SGTC_ReaDebug_ArmPlayer), so their spawn reserve is the normal kit.  The
	// human all-weapon reserve is added separately by the wrappers.
	if( bRea )
		return SGTC_BOT_EDICT_MARGIN_NORMAL;
	return SGTC_Bots_EdictBudgetSpawnMargin( sgtc_cvar_freeguns.value > 1.0f ? 1 : 0 );
}

// Human players already connected also reserve a worst-case loadout before any
// further bot spawn/loadout grant.  Fake clients are excluded (they are the
// bots whose margin is already being counted).
static int SGTC_Bots_HumanLoadoutReserve( int margin )
{
	int iHumans = 0;
	for( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		edict_t *e = INDEXENT( i );
		if( !e || e->free || !e->pvPrivateData )
			continue;
		if( !( e->v.flags & FL_CLIENT ) || ( e->v.flags & FL_FAKECLIENT ) )
			continue;
		++iHumans;
	}
	const long long reserve = (long long)iHumans * 31LL;
	return ( reserve > 1000000 ) ? 1000000 : (int)reserve;
}

// Engine-side wrapper for the pure budget decision. Called by BotCreate in
// bots/jk_botti/bot.cpp (quota and every other spawn path) with the loadout
// reserve, so a map that is already near MAX_EDICTS refuses the spawn instead
// of taking the server down with Host_Error: SV_AllocEdict. A refusal logs a
// `spawn refused` line (the replay marker).
extern "C" int SGTC_Bots_EdictBudgetAllowSpawn( int margin )
{
	const int iHumans = 0; // reserved below via helper
	const int used = (int)NUMBER_OF_ENTITIES() + SGTC_Bots_HumanLoadoutReserve( margin );
	const int max_edicts = gpGlobals->maxEntities;
	const BOOL bRea = SGTC_ReaDebugActive();
	const int iReserve = ( bRea || sgtc_cvar_freeguns.value > 1.0f ) ? ( margin < 80 ? 80 : margin ) : margin;
	const BOOL bAllow = SGTC_Bots_EdictBudgetAllows( used, max_edicts, iReserve ) ? TRUE : FALSE;
	UTIL_LogPrintf( "SGTCEVENT {\"event\":\"edict_gate\",\"used\":%d,\"max\":%d,"
		"\"margin\":%d,\"rea\":%d,\"freeguns\":%.0f,\"allow\":%d}\n",
		used, max_edicts, iReserve, bRea ? 1 : 0, sgtc_cvar_freeguns.value, bAllow ? 1 : 0 );
	if( bAllow )
		return 1;
	ALERT( at_console, "[sgtc_bots] spawn refused: ~%d/%d edicts used\n", used, max_edicts );
	return 0;
}

// Count-aware wrapper for `sgtc_bot_add`. It reserves one edict per requested
// bot up front, so a request that cannot fit that floor (for example 3 bots at
// 882/900) is refused once, before any fake client is created, instead of
// creating one bot and logging a refusal for each one that follows. Each
// BotCreate still calls the single-spawn gate above, so a request that passes
// this floor can still be refused mid-loop when the bots consume the remaining
// headroom: a spawn costs more than one edict, and quota reconcile retries
// every 0.5 s while the map is over budget. The caller passes the same
// loadout-aware margin as BotCreate.
extern "C" int SGTC_Bots_EdictBudgetAllowSpawnCount( int count, int margin )
{
	const int used = (int)NUMBER_OF_ENTITIES() + SGTC_Bots_HumanLoadoutReserve( margin );
	const int max_edicts = gpGlobals->maxEntities;

	if( SGTC_Bots_EdictBudgetAllowsRequest( used, max_edicts, margin, count ) )
		return 1;

	ALERT( at_console, "[sgtc_bots] spawn refused: ~%d/%d edicts used\n",
		used, max_edicts );
	return 0;
}

static void SGTC_Bots_CommandAdd( void )
{
	const int argc = CMD_ARGC();
	const int requested = ( argc > 1 && CMD_ARGV( 1 )[0] ) ? atoi( CMD_ARGV( 1 ) ) : 1;
	int skill = ( argc > 2 && CMD_ARGV( 2 )[0] ) ? atoi( CMD_ARGV( 2 ) ) : default_bot_skill;

	// A command-line +sgtc_bot_add runs before a map is loaded. Creating a fake
	// client then calls into a player factory and a world that do not exist yet,
	// so refuse instead of risking a crash: quota-driven spawning covers the
	// headless case and picks bots up as soon as the map is live.
	if( !gpGlobals->deathmatch || FNullEnt( INDEXENT( 0 ) ) || !g_pGameRules )
	{
		ALERT( at_console, "[sgtc_bots] sgtc_bot_add: map not loaded yet\n" );
		return;
	}

	if( argc > 3 && CMD_ARGV( 3 )[0] )
	{
		const int team = atoi( CMD_ARGV( 3 ) );

		if( team >= 0 && team <= 2 )
			CVAR_SET_FLOAT( "sgtc_bot_team", (float)team );
		else
			ALERT( at_console, "[sgtc_bots] team must be 0 (auto), 1 (Torri) or 2 (Goa'uld)\n" );
	}

	if( skill < 0 || skill > 5 )
		skill = default_bot_skill;

	int count = requested;

	if( count < 1 )
		count = 1;

	// Never exceed the engine's client slots or jk_botti's 32 bot slots.
	const int room = gpGlobals->maxClients - UTIL_GetClientCount();

	if( room <= 0 )
	{
		ALERT( at_console, "[sgtc_bots] no free client slots\n" );
		return;
	}

	if( count > room )
		count = room;
	if( count > 32 - SGTC_Bots_CountUsed() )
		count = 32 - SGTC_Bots_CountUsed();

	// Reserve one edict per requested bot up front: 882/3 is refused once here,
	// before any bot exists, instead of creating one and logging two refusals.
	// BotCreate still gates every spawn, so a request that passes this floor
	// can be refused per bot mid-loop (each bot costs more than one edict).
	if( count > 0 )
	{
		const int margin = SGTC_Bots_EdictBudgetMarginForLoadout();

		if( !SGTC_Bots_EdictBudgetAllowSpawnCount( count, margin ) )
		{
			ALERT( at_console, "[sgtc_bots] sgtc_bot_add: requested %d, added 0, bots now %d\n",
				requested, SGTC_Bots_CountUsed() );
			return;
		}
	}

	int added = 0;

	for( int i = 0; i < count; i++ )
	{
		const int before = SGTC_Bots_CountUsed();

		BotCreate( NULL, NULL, skill, -1, -1, -1 );

		if( SGTC_Bots_CountUsed() > before )
			added++;
	}

	ALERT( at_console, "[sgtc_bots] sgtc_bot_add: requested %d, added %d, bots now %d\n",
		requested, added, SGTC_Bots_CountUsed() );
}

static void SGTC_Bots_CommandKick( void )
{
	const char *arg = ( CMD_ARGC() > 1 ) ? CMD_ARGV( 1 ) : "";
	int count;

	if( !stricmp( arg, "all" ) )
	{
		count = 32;
	}
	else if( arg[0] )
	{
		count = atoi( arg );
		if( count < 1 )
			count = 1;
	}
	else
	{
		count = 1;
	}

	int kicked = 0;

	// Highest slot first: deterministic, and the newest bots live there.
	for( int i = 31; i >= 0 && kicked < count; i-- )
	{
		if( !bots[i].is_used )
			continue;

		BotKick( bots[i] );
		kicked++;
	}

	if( kicked == 0 )
		ALERT( at_console, "[sgtc_bots] no bots to kick\n" );
	else
		ALERT( at_console, "[sgtc_bots] sgtc_bot_kick: kicked %d, bots now %d\n",
			kicked, SGTC_Bots_CountUsed() );
}

static void SGTC_Bots_CommandQuota( void )
{
	if( CMD_ARGC() > 1 && CMD_ARGV( 1 )[0] )
	{
		int quota = atoi( CMD_ARGV( 1 ) );

		if( quota < -1 )
			quota = -1;
		if( quota > 31 )
			quota = 31;

		CVAR_SET_FLOAT( "sgtc_bot_quota", (float)quota );
	}

	SGTC_Bots_SyncCvars();

	ALERT( at_console, "[sgtc_bots] quota=%d (min_bots=%d max_bots=%d)\n",
		(int)sgtc_bot_quota.value, min_bots, max_bots );
}

#define SGTC_STATUS_PAGE_BOTS 6

static void SGTC_Bots_CommandStatus( void )
{
	char mapname[64];
	int used = 0;

	strlcpy( mapname, STRING( gpGlobals->mapname ), sizeof( mapname ) );

	ALERT( at_console, "[sgtc_bots] bots=%d map=%s seed=%d events=%d quota=%d\n",
		SGTC_Bots_CountUsed(), mapname, (int)sgtc_bot_seed.value,
		(int)sgtc_bot_events.value, (int)sgtc_bot_quota.value );

	char names[32][64];
	sgtc_bot_summary_t summaries[32];

	for( int i = 0; i < 32; i++ )
	{
		bot_t &pBot = bots[i];

		if( !pBot.is_used || FNullEnt( pBot.pEdict ) )
			continue;

		CBasePlayer *pl = (CBasePlayer *)CBaseEntity::Instance( pBot.pEdict );

		SGTC_Bots_BotName( i, names[i], sizeof( names[i] ) );

		summaries[used].name = names[i];
		summaries[used].team = pl ? SGTC_EntTeamToTeam( (int)pl->pev->team ) : 0;
		summaries[used].cls = pl ? pl->m_iSgtcPortraitShown : 0;
		summaries[used].health = (int)pBot.pEdict->v.health;
		summaries[used].frags = (int)pBot.pEdict->v.frags;
		summaries[used].origin[0] = pBot.pEdict->v.origin.x;
		summaries[used].origin[1] = pBot.pEdict->v.origin.y;
		summaries[used].origin[2] = pBot.pEdict->v.origin.z;

		ALERT( at_console, "  %-20s team=%d class=%d health=%d frags=%d origin=%.1f,%.1f,%.1f\n",
			names[i], summaries[used].team, summaries[used].cls,
			summaries[used].health, summaries[used].frags,
			summaries[used].origin[0], summaries[used].origin[1], summaries[used].origin[2] );

		used++;
	}

	// Machine-readable status. The engine log sink truncates around 1 KiB, so
	// the per-bot array is paginated; `bots` is the total either way.
	const int pages = ( used + SGTC_STATUS_PAGE_BOTS - 1 ) / SGTC_STATUS_PAGE_BOTS;

	if( pages == 0 )
	{
		sgtc_bot_event_field_t fields[3];
		SGTC_Bots_AddFieldInt( &fields[0], "bots", 0 );
		SGTC_Bots_AddFieldInt( &fields[1], "page", 1 );
		SGTC_Bots_AddFieldInt( &fields[2], "pages", 1 );
		SGTC_Bots_Emit( "status", "", fields, 3, TRUE );
		return;
	}

	for( int page = 0; page < pages; page++ )
	{
		char detail[512];
		const int first = page * SGTC_STATUS_PAGE_BOTS;
		int count = used - first;

		if( count > SGTC_STATUS_PAGE_BOTS )
			count = SGTC_STATUS_PAGE_BOTS;

		if( SGTC_Bots_FormatBotsDetail( detail, sizeof( detail ), &summaries[first], count ) < 0 )
			continue;

		sgtc_bot_event_field_t fields[4];
		SGTC_Bots_AddFieldInt( &fields[0], "bots", used );
		SGTC_Bots_AddFieldInt( &fields[1], "page", page + 1 );
		SGTC_Bots_AddFieldInt( &fields[2], "pages", pages );
		SGTC_Bots_AddFieldRaw( &fields[3], "bots_detail", detail );

		SGTC_Bots_Emit( "status", "", fields, 4, TRUE );
	}
}

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

void SGTC_Bots_Init( DLL_FUNCTIONS *gameApi, enginefuncs_t *engineApi )
{
	JK_Botti_NativeInit( gameApi, engineApi );

	SGTC_RegisterOrAdoptCvar( &sgtc_bot_enable );
	SGTC_RegisterOrAdoptCvar( &sgtc_bot_min );
	SGTC_RegisterOrAdoptCvar( &sgtc_bot_max );
	SGTC_RegisterOrAdoptCvar( &sgtc_bot_skill );
	SGTC_RegisterOrAdoptCvar( &sgtc_bot_seed );
	SGTC_RegisterOrAdoptCvar( &sgtc_bot_events );
	SGTC_RegisterOrAdoptCvar( &sgtc_bot_quota );
	SGTC_RegisterOrAdoptCvar( &sgtc_bot_class );
	SGTC_RegisterOrAdoptCvar( &sgtc_bot_team );
	SGTC_RegisterOrAdoptCvar( &sgtc_bot_roam );
	SGTC_RegisterOrAdoptCvar( &sgtc_bot_rescue );
	SGTC_RegisterOrAdoptCvar( &sgtc_bot_kit );
	SGTC_RegisterOrAdoptCvar( &sgtc_bot_weaponlog );
	SGTC_RegisterOrAdoptCvar( &sgtc_bot_radio );
	SGTC_RegisterOrAdoptCvar( &sgtc_bot_radio_rate_cvar );

	SGTC_RegisterOrAdoptCvar( &jk_botti_version );
	CVAR_SET_STRING( "jk_botti_version", "native-sgtc" );

	BotTraceRegisterCvar();

	SGTC_Bots_SyncCvars();
	SGTC_Bots_SeedRandom();
	SGTC_Bots_ResetTrackers();
	SGTC_Bots_ResetNav();
	SGTC_Bots_ResetObjectives();
	SGTC_Bots_ClassResetAll();

	if( engineApi->pfnAddServerCommand )
	{
		engineApi->pfnAddServerCommand( "jk_botti", JK_Botti_ServerCommand );
		engineApi->pfnAddServerCommand( "sgtc_bot_add", SGTC_Bots_CommandAdd );
		engineApi->pfnAddServerCommand( "sgtc_bot_kick", SGTC_Bots_CommandKick );
		// A command line/config value can create this cvar before the DLL loads;
		// the engine then refuses the command ("already defined as a var").
		// The cvar still works, so only install the handler when it is free.
		if( !CVAR_GET_POINTER( "sgtc_bot_quota" ) )
			engineApi->pfnAddServerCommand( "sgtc_bot_quota", SGTC_Bots_CommandQuota );
		engineApi->pfnAddServerCommand( "sgtc_bot_status", SGTC_Bots_CommandStatus );
	}

	JK_Botti_GameInit();

	// Registers the sv_botdirector* cvars and starts the HTTP worker. The
	// director is inert until sv_botdirector 1 plus a URL/model are configured.
	SGTC_Director_Init();
}

void SGTC_Bots_Shutdown( void )
{
	// Stop the network worker before the bot state it refers to goes away.
	SGTC_Director_Shutdown();

	for( int index = 0; index < 32; index++ )
	{
		if( bots[index].is_used )
			BotKick( bots[index] );
	}

	WaypointInit();

	for( int i = 0; i < 32; i++ )
		free_posdata_list( i );

	UTIL_FreeFuncBreakables();
	FreeCfgBotRecord();

	SGTC_Bots_ResetNav();
	SGTC_Bots_ResetObjectives();
}

void SGTC_Bots_OnSpawn( int result, edict_t *pent )
{
	(void)result;

	if( !sgtc_bot_enable.value )
		return;

	// worldspawn runs once per map: re-seed here (not in Init) so "seed set =
	// identical run on every map" holds for the whole session, and never fall
	// back to the wall clock when a seed is configured.
	if( FStrEq( STRING( pent->v.classname ), "worldspawn" ) )
	{
		SGTC_Bots_SyncCvars();
		SGTC_Bots_SeedRandom();
		SGTC_Bots_ResetTrackers();
		SGTC_Bots_ResetNav();
		SGTC_Bots_ResetObjectives();

		SGTC_Bots_RadioLevelInit();

		SGTC_Bots_ClassResetAll();

		// Re-arm after the previous level's SGTC_Bots_ServerDeactivate and drop
		// every per-map director decision before bots spawn onto the new map.
		SGTC_Director_Init();
		SGTC_Director_OnMapStart();
	}

	JK_Botti_OnSpawn( pent );
}

void SGTC_Bots_OnSpawnPost( int result, edict_t *pent )
{
	(void)result;

	if( !sgtc_bot_enable.value )
		return;

	JK_Botti_OnSpawnPost( pent );

	if( FStrEq( STRING( pent->v.classname ), "worldspawn" ) )
	{
		SGTC_Bots_RegisterMapNavLinks();
		SGTC_Bots_CollectObjectives();
		SGTC_Bots_RulesAI_Init();

		// SGTC ships a waypoint file for every payload map and a matching
		// precomputed path matrix.  jk_botti's auto-waypointing appends nodes
		// for every runtime-spawned weapon (drops, class kits) and therefore
		// recalculates the whole matrix at map start and on every drop; keep
		// it only for maps that loaded no waypoints at all.
		if( num_waypoints > 0 )
			g_auto_waypoint = FALSE;
	}
}

void SGTC_Bots_OnKeyValuePost( edict_t *pent, KeyValueData *pkvd )
{
	if( !sgtc_bot_enable.value )
		return;

	JK_Botti_OnKeyValuePost( pent, pkvd );
}

void SGTC_Bots_ClientConnect( edict_t *pEntity, const char *pszName, const char *pszAddress, char szRejectReason[128] )
{
	if( !sgtc_bot_enable.value )
		return;

	JK_Botti_ClientConnect( pEntity, pszName, pszAddress, szRejectReason );
}

void SGTC_Bots_ClientPutInServer( edict_t *pEntity )
{
	if( !sgtc_bot_enable.value )
		return;

	JK_Botti_ClientPutInServer( pEntity );
}

void SGTC_Bots_ClientDisconnect( edict_t *pEntity )
{
	if( !sgtc_bot_enable.value )
		return;

	JK_Botti_ClientDisconnect( pEntity );
}

void SGTC_Bots_ClientCommand( edict_t *pEntity )
{
	if( !sgtc_bot_enable.value )
		return;

	SGTC_Bots_SyncCvars();
	JK_Botti_ClientCommand( pEntity );
}

void SGTC_Bots_StartFrame( void )
{
	if( !sgtc_bot_enable.value )
		return;

	SGTC_Bots_SyncCvars();
	JK_Botti_StartFrame();

	SGTC_Bots_TrackBots();
	SGTC_Bots_PollObjectives();
	SGTC_Bots_RulesAI_Frame();

	// After jk_botti has thought: drains the HTTP reply, then runs the 1 Hz
	// local controller. Never blocks on the network.
	SGTC_Director_StartFrame();
}

void SGTC_Bots_ServerDeactivate( void )
{
	// ServerDeactivate is per level, not per DLL unload: the director is
	// re-armed from the next worldspawn (SGTC_Director_Init in OnSpawn).
	SGTC_Director_Shutdown();

	if( !sgtc_bot_enable.value )
		return;

	JK_Botti_ServerDeactivate();
}

void SGTC_Bots_PM_Move( struct playermove_s *ppmove, int server )
{
	if( !sgtc_bot_enable.value )
		return;

	JK_Botti_PM_Move( ppmove, (qboolean)server );
}

void SGTC_Bots_CmdStart( const edict_t *player, const struct usercmd_s *cmd, unsigned int random_seed )
{
	if( !sgtc_bot_enable.value )
		return;

	JK_Botti_CmdStart( player, cmd, random_seed );
	// Fire-frame only: cancels the sway the sniper weapon is about to add.
	SGTC_Sniper_BotCompensate( const_cast<edict_t *>(player), cmd );
}

void SGTC_Bots_PlayerPostThinkPost( edict_t *pEdict )
{
	if( !sgtc_bot_enable.value )
		return;

	JK_Botti_PlayerPostThinkPost( pEdict );
}

// ---------------------------------------------------------------------------
// map nav links
// ---------------------------------------------------------------------------

void SGTC_Bots_RegisterMapNavLinks( void )
{
	const int waypoints_before = num_waypoints;

	SGTC_Bots_ResetNav();

	if( waypoints_before == 0 )
	{
		// Reported before the teleporter scan creates its standalone nodes:
		// "no waypoint data for this map" is what a playtest wants to know.
		sgtc_bot_event_field_t fields[1];
		SGTC_Bots_AddFieldInt( &fields[0], "count", 0 );
		SGTC_Bots_Emit( "no_waypoints", "", fields, 1, FALSE );
	}

	SGTC_Bots_ScanTeleportClass( "trigger_teleport", SGTC_LINK_TELEPORT );
	SGTC_Bots_ScanTeleportClass( "trigger_adv_teleport", SGTC_LINK_RING );
	SGTC_Bots_ScanTeleportClass( "special_teleport_in", SGTC_LINK_TELEPORT );
	SGTC_Bots_ScanGateZones();
	SGTC_Bots_ScanMultiGates();
	SGTC_Bots_ScanGateButtons();

	if( num_waypoints > waypoints_before )
		WaypointRebuildRoutes();

	ALERT( at_console, "[sgtc_bots] nav: %d teleport links, %d gate/ring zones, %d waypoints\n",
		s_num_links, s_num_zones, num_waypoints );
}

// ---------------------------------------------------------------------------
// engine-wrapper test hooks
// ---------------------------------------------------------------------------
//
// tests/stub_engine.c drives SGTC_Bots_GateTimingFrame() end-to-end through
// these entry points (C linkage, dlsym). They are compiled only in the
// SGTC_EXPORT_ALL_SYMBOLS test/CI build, so the shipping dedicated-server DLL
// keeps its normal surface. No behaviour is changed by the hooks themselves:
// they set up a synthetic bot/entry and read the wrapper's own state back.
#ifdef SGTC_EXPORT_ALL_SYMBOLS
extern WAYPOINT waypoints[MAX_WAYPOINTS];

extern "C" {

void SGTC_Bots_TestResetNav( void )
{
	sgtc_bot_enable.value = 1.0f;

	for( int i = 0; i < 32; i++ )
		memset( (void *)&bots[i], 0, sizeof( bot_t ) );

	SGTC_Bots_ResetNav();
}

int SGTC_Bots_TestScanMap( void )
{
	SGTC_Bots_RegisterMapNavLinks();
	return s_num_gate_entries;
}

int SGTC_Bots_TestGateEntryCount( void )
{
	return s_num_gate_entries;
}

int SGTC_Bots_TestGateEntryWaypoint( int i )
{
	if( i < 0 || i >= s_num_gate_entries )
		return -1;

	return s_gate_entries[i].waypoint;
}

int SGTC_Bots_TestGateEntryGate( int i )
{
	if( i < 0 || i >= s_num_gate_entries )
		return -1;

	return s_gate_entries[i].gate;
}

int SGTC_Bots_TestGateEntryResolved( int i )
{
	if( i < 0 || i >= s_num_gate_entries )
		return -1;

	return s_gate_entries[i].resolved;
}

int SGTC_Bots_TestLinkCount( void )
{
	return s_num_links;
}

int SGTC_Bots_TestAddGateEntry( int waypoint, int gate, float x, float y, float z )
{
	if( waypoint < 0 || waypoint >= MAX_WAYPOINTS || gate < 0 || gate > 1 )
		return 0;

	if( waypoint >= num_waypoints )
		num_waypoints = waypoint + 1;

	Vector origin;
	origin.x = x;
	origin.y = y;
	origin.z = z;

	waypoints[waypoint].origin.x = x;
	waypoints[waypoint].origin.y = y;
	waypoints[waypoint].origin.z = z;

	SGTC_Bots_AddGateEntry( waypoint, origin, gate, 0, 1 );
	return 1;
}

int SGTC_Bots_TestDriveDial( int entindex, int waypoint, int gate, int frames,
	int step_ms, int *out_presses, int *out_state, float *out_open, float *out_close )
{
	if( entindex <= 0 || waypoint < 0 || waypoint >= num_waypoints )
		return 0;
	if( gate < 0 || gate > 1 || frames <= 0 || step_ms <= 0 )
		return 0;

	edict_t *pent = INDEXENT( entindex );
	sgtc_bot_gate_entry_t *entry = SGTC_Bots_GateEntryForWaypoint( waypoint );

	if( FNullEnt( pent ) || entry == NULL )
		return 0;

	bot_t &pBot = bots[0];

	memset( (void *)&pBot, 0, sizeof( bot_t ) );
	pBot.is_used = TRUE;
	pBot.pEdict = pent;
	pBot.curr_waypoint_index = waypoint;
	pBot.waypoint_origin.x = entry->origin.x;
	pBot.waypoint_origin.y = entry->origin.y;
	pBot.waypoint_origin.z = entry->origin.z;
	pBot.pBotEnemy = NULL;
	pBot.b_not_maxspeed = FALSE;
	strlcpy( pBot.name, "GateTestBot", sizeof( pBot.name ) );

	pent->v.origin[0] = entry->origin.x;
	pent->v.origin[1] = entry->origin.y;
	pent->v.origin[2] = entry->origin.z;
	pent->v.oldorigin[0] = entry->origin.x;
	pent->v.oldorigin[1] = entry->origin.y;
	pent->v.oldorigin[2] = entry->origin.z;
	pent->v.button = 0;

	for( int i = 0; i < frames; i++ )
	{
		gpGlobals->time += (float)step_ms / 1000.0f;
		SGTC_Bots_GateTimingFrame( pBot );
	}

	if( out_presses )
		*out_presses = s_gate_defs[gate].last_fired_chevron;
	if( out_state )
		*out_state = s_gate_timing[gate].state;
	if( out_open )
		*out_open = s_gate_timing[gate].open_time;
	if( out_close )
		*out_close = s_gate_timing[gate].close_time;

	return 1;
}

} // extern "C"
#endif // SGTC_EXPORT_ALL_SYMBOLS
