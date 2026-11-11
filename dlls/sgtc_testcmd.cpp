// sgtc_testcmd.cpp - in-game test/debug commands for automated feature tests.
//
// Output contract: every command prints one or more lines beginning with
//   SGTCTEST {json}
// to the server console (engine ALERT at_console). Tests parse those lines;
// they never need to scrape human text.
//
// Commands (space separated, ; separated in a script):
//   help                      list commands
//   info                      map, time, entities/maxEntities/maxClients, gamemode, rules pointer
//   map                       map name only
//   entities [limit]          JSON lines: one per live entity (class, index, health, solid, flags)
//   entity <classname>        first entity of that class: full state
//   players                   players: netname, model, health, armor, team, frags, maxspeed, velocity, origin
//   rules                     current game rules pointer + mp_gamemode
//   cvar <name>               one cvar: name/value/string
//   set <name> <value>        set a cvar string (test servers only)
//   echo <text>               echo a line back (connectivity check)
//   quit                      ask the engine to quit (dedicated test runs)
//
// Gating: SGTC_TestCommand always works when called directly (the stub-engine
// harness and client command route call it). The automatic script
// ($sgtc_test_script executed from ServerActivate) only runs when
// $sgtc_test_enable is 1, so a normal server never runs test commands.

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "gamerules.h"
#include "weapons.h"
#include "game.h"
#include "sgtc_testcmd.h"
#include "sgtc_bots_rules.h"
#include "sgtc_mech_state.h"
#include "sgtc_c4fire.h"
#include "sgtc_weapons.h"
#include "sgtc_rea_debug.h"
#ifdef BUILD_JK_BOTTI
#include "sgtc_bots_rules_ai.h"
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#define SGTC_TEST_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define SGTC_TEST_EXPORT extern "C" __declspec(dllexport)
#endif

// Pack-auto arm path (dlls/sgtc_pack_auto.cpp) — used by the unarmed test
// helper below to reproduce the classed-but-unarmed spawn the VGUI class click
// produces (console "classe" spawns armed, which cancels the auto-select timer).
extern void SGTC_PackAuto_Start( CBasePlayer *pPlayer );

// Portable tokenizer (strtok_r/strtok_s differ across GCC, MinGW and MSVC).
static char *next_tok( char **state, const char *seps )
{
	char *s = *state;
	if( !s ) return NULL;
	while( *s && strchr( seps, *s ) ) ++s;
	if( !*s ) { *state = NULL; return NULL; }
	char *start = s;
	while( *s && !strchr( seps, *s ) ) ++s;
	if( *s ) { *s = 0; *state = s + 1; } else *state = NULL;
	return start;
}

static cvar_t sgtc_test_enable = { "sgtc_test_enable", "0", FCVAR_SERVER };
static cvar_t sgtc_test_script = { "sgtc_test_script", "", FCVAR_SERVER };
static cvar_t sgtc_test_quit   = { "sgtc_test_quit", "0", FCVAR_SERVER };
static cvar_t sgtc_test_watch  = { "sgtc_test_watch", "0", FCVAR_SERVER };
static cvar_t sgtc_test_probe  = { "sgtc_test_probe", "", FCVAR_SERVER };
static cvar_t sgtc_test_probe_interval = { "sgtc_test_probe_interval", "2", FCVAR_SERVER };
static cvar_t sgtc_test_timed_script = { "sgtc_test_timed_script", "", FCVAR_SERVER };
static cvar_t sgtc_test_timed_delay = { "sgtc_test_timed_delay", "0", FCVAR_SERVER };
static cvar_t sgtc_test_timed2_script = { "sgtc_test_timed2_script", "", FCVAR_SERVER };
static cvar_t sgtc_test_timed3_script = { "sgtc_test_timed3_script", "", FCVAR_SERVER };
static cvar_t sgtc_test_autotouch = { "sgtc_test_autotouch", "", FCVAR_SERVER };
static cvar_t sgtc_test_autotouch_start = { "sgtc_test_autotouch_start", "0", FCVAR_SERVER };
static cvar_t sgtc_test_autotouch_end = { "sgtc_test_autotouch_end", "30", FCVAR_SERVER };
static int s_test_autotouch_count = 0;
static cvar_t sgtc_test_timed2_delay = { "sgtc_test_timed2_delay", "0", FCVAR_SERVER };
static cvar_t sgtc_test_timed3_delay = { "sgtc_test_timed3_delay", "0", FCVAR_SERVER };
static cvar_t sgtc_test_repeat_script = { "sgtc_test_repeat_script", "", FCVAR_SERVER };
static cvar_t sgtc_test_repeat_delay = { "sgtc_test_repeat_delay", "0", FCVAR_SERVER };
static cvar_t sgtc_test_repeat_interval = { "sgtc_test_repeat_interval", "1.0", FCVAR_SERVER };
static cvar_t sgtc_test_repeat_count = { "sgtc_test_repeat_count", "0", FCVAR_SERVER };
static double s_test_watch_next = 0.0;
static double s_test_probe_next = 0.0;
static BOOL s_test_timed_done = FALSE;
static BOOL s_test_timed2_done = FALSE;
static BOOL s_test_timed3_done = FALSE;
static int s_test_repeat_done = 0;
static double s_test_repeat_next = 0.0;

static void test_out( const char *fmt, ... )
{
	char buf[4096];
	va_list ap;
	va_start( ap, fmt );
	vsnprintf( buf, sizeof( buf ), fmt, ap );
	va_end( ap );
	ALERT( at_console, "%s", buf );
	// Also write to stdout: the 32-bit stub-engine harness captures stdout,
	// and dedicated servers log stdout to the console anyway.
	fprintf( stdout, "%s\n", buf );
	fflush( stdout );
}

// Minimal JSON string escape; enough for map names, netnames and class names.
static void json_escape( const char *in, char *out, size_t outsz )
{
	size_t o = 0;
	for( const char *p = in; p && *p && o + 7 < outsz; ++p )
	{
		unsigned char c = (unsigned char)*p;
		if( c == '"' || c == '\\' ) { out[o++] = '\\'; out[o++] = (char)c; }
		else if( c == '\n' ) { out[o++] = '\\'; out[o++] = 'n'; }
		else if( c == '\r' ) { out[o++] = '\\'; out[o++] = 'r'; }
		else if( c == '\t' ) { out[o++] = '\\'; out[o++] = 't'; }
		else if( c < 0x20 ) { o += snprintf( out + o, outsz - o, "\\u%04x", c ); }
		else out[o++] = (char)c;
	}
	out[o] = 0;
}

static const char *classname_of( edict_t *e )
{
	if( !e || e->free ) return "";
	if( e->v.classname ) return STRING( e->v.classname );
	return "";
}

static void cmd_help( void )
{
	test_out( "SGTCTEST {\"cmd\":\"help\",\"commands\":[\"help\",\"info\",\"map\",\"entities\",\"entity\",\"monsters\",\"players\",\"rules\",\"cvar\",\"set\",\"echo\",\"tp\",\"spawn\",\"quit\"]}" );
}

static void cmd_info( void )
{
	char map[128], mode[128];
	json_escape( STRING( gpGlobals->mapname ), map, sizeof( map ) );
	json_escape( CVAR_GET_STRING( "mp_gamemode" ), mode, sizeof( mode ) );
	// Live edict budget, so a playtest can measure before/after a quota change
	// (issue #193: sg_escape is ~882/900 with no bots).
	const int entities = (int)NUMBER_OF_ENTITIES();
	test_out( "SGTCTEST {\"cmd\":\"info\",\"map\":\"%s\",\"time\":%.3f,\"entities\":%d,\"maxEntities\":%d,\"maxClients\":%d,\"gamemode\":\"%s\",\"rules\":%s}",
		map, gpGlobals->time, entities, gpGlobals->maxEntities, gpGlobals->maxClients, mode,
		g_pGameRules ? "true" : "false" );
}

static void cmd_entities( int limit )
{
	int n = 0;
	for( int i = 1; i < gpGlobals->maxEntities && n < limit; ++i )
	{
		edict_t *e = INDEXENT( i );
		if( !e || e->free || !e->pvPrivateData ) continue;
		char cls[128];
		json_escape( classname_of( e ), cls, sizeof( cls ) );
		test_out( "SGTCTEST {\"cmd\":\"entity\",\"index\":%d,\"class\":\"%s\",\"health\":%.0f,\"solid\":%d,\"flags\":%d}",
			i, cls, e->v.health, e->v.solid, e->v.flags );
		++n;
	}
	test_out( "SGTCTEST {\"cmd\":\"entities\",\"count\":%d,\"limit\":%d}", n, limit );
}

static void cmd_entity( const char *classname )
{
	if( !classname || !*classname ) { test_out( "SGTCTEST {\"cmd\":\"entity\",\"error\":\"missing classname\"}" ); return; }
	for( int i = 1; i < gpGlobals->maxEntities; ++i )
	{
		edict_t *e = INDEXENT( i );
		if( !e || e->free || !e->pvPrivateData ) continue;
		if( strcmp( classname_of( e ), classname ) ) continue;
		char cls[128];
		json_escape( classname_of( e ), cls, sizeof( cls ) );
		char model[128];
		json_escape( STRING( e->v.model ), model, sizeof( model ) );
		test_out( "SGTCTEST {\"cmd\":\"entity\",\"index\":%d,\"class\":\"%s\",\"model\":\"%s\",\"health\":%.0f,\"solid\":%d,\"movetype\":%d,\"flags\":%d,\"effects\":%d,"
			"\"sequence\":%d,\"frame\":%.0f,\"skin\":%d,\"body\":%d,\"animtime\":%.3f,\"framerate\":%.2f,\"rendermode\":%d,\"rendercolor\":[%.0f,%.0f,%.0f],\"renderamt\":%.0f,\"origin\":[%.1f,%.1f,%.1f]}",
			i, cls, model, e->v.health, e->v.solid, e->v.movetype, e->v.flags, (int)e->v.effects,
			(int)e->v.sequence, e->v.frame, (int)e->v.skin, (int)e->v.body,
			e->v.animtime, e->v.framerate, (int)e->v.rendermode,
			e->v.rendercolor.x, e->v.rendercolor.y, e->v.rendercolor.z, e->v.renderamt,
			e->v.origin.x, e->v.origin.y, e->v.origin.z );
		return;
	}
	test_out( "SGTCTEST {\"cmd\":\"entity\",\"error\":\"not found\",\"class\":\"%s\"}", classname );
}

static void cmd_players( void )
{
	test_out( "SGTCTEST {\"cmd\":\"edicts\",\"used\":%d,\"max\":%d}", (int)NUMBER_OF_ENTITIES(), gpGlobals->maxEntities );
	int n = 0;
	for( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		edict_t *e = INDEXENT( i );
		if( !e || e->free || !e->pvPrivateData ) continue;
		char name[128];
		char model[128];
		json_escape( STRING( e->v.netname ), name, sizeof( name ) );
		json_escape( STRING( e->v.model ), model, sizeof( model ) );
		CBasePlayer *pl = (CBasePlayer *)e->pvPrivateData;
		const char *pszActive = ( pl->m_pActiveItem ) ? STRING( pl->m_pActiveItem->pev->classname ) : "";
		char active_esc[128];
		json_escape( pszActive, active_esc, sizeof( active_esc ) );
		char ammo[256]; ammo[0] = 0; int ammo_len = 0;
		for( int ai = 0; ai < MAX_AMMO_SLOTS && ammo_len < 200; ++ai )
		{
			if( pl->m_rgAmmo[ai] == 0 ) continue;
			ammo_len += snprintf( ammo + ammo_len, sizeof( ammo ) - ammo_len, "%s%d:%d", ammo_len ? "," : "", ai, pl->m_rgAmmo[ai] );
		}
		test_out( "SGTCTEST {\"cmd\":\"player\",\"time\":%.3f,\"index\":%d,\"name\":\"%s\",\"model\":\"%s\",\"health\":%.0f,\"armor\":%.0f,\"team\":%d,\"frags\":%.0f,"
			"\"maxspeed\":%.1f,\"latch\":%.1f,\"stamina\":%d,\"fuser2\":%d,\"weapons\":%d,\"active\":\"%s\",\"rea\":%d,\"ammo\":[%s],\"velocity\":[%.1f,%.1f,%.1f],\"origin\":[%.1f,%.1f,%.1f],"
			"\"class\":%d,\"active_team\":%d,\"effects\":%d,\"nodraw\":%d,\"rendermode\":%d,\"renderamt\":%.0f,\"renderfx\":%d,"
			"\"cloaktick\":%d,\"sprint\":%d,\"snag\":%d,\"fake\":%d,\"observer\":%d}",
			gpGlobals->time, i, name, model, e->v.health, e->v.armorvalue, (int)e->v.team, e->v.frags,
			e->v.maxspeed, pl->m_flMaxSpeedLatch, pl->m_iStamina, (int)e->v.fuser2, (int)e->v.weapons, active_esc,
			SGTC_ReaDebugActive() ? 1 : 0, ammo,
			e->v.velocity.x, e->v.velocity.y, e->v.velocity.z,
			e->v.origin.x, e->v.origin.y, e->v.origin.z,
			pl->m_iSgtcPortraitShown, (int)pl->m_iSgtcActiveTeam,
			(int)e->v.effects, ( e->v.effects & EF_NODRAW ) ? 1 : 0,
			(int)e->v.rendermode, e->v.renderamt, (int)e->v.renderfx,
			(int)pl->m_bSgtcCloakTick, (int)pl->m_bShieldSprint, (int)pl->m_bSnagInvisLatch,
			FBitSet( e->v.flags, FL_FAKECLIENT ) ? 1 : 0, pl->IsObserver() ? 1 : 0 );
		++n;
	}
	test_out( "SGTCTEST {\"cmd\":\"players\",\"count\":%d}", n );
}

static void cmd_entinfo( const char *pszName );
static void cmd_useent( const char *pszName );
static CBaseEntity *SGTC_TestFindEntityByTargetField( const char *pszTarget )
{
	if( !pszTarget || !*pszTarget )
		return NULL;
	for( int i = 1; i < gpGlobals->maxEntities; ++i )
	{
		edict_t *pEdict = INDEXENT( i );
		if( !pEdict || pEdict->free || !pEdict->v.target )
			continue;
		if( !strcmp( STRING( pEdict->v.target ), pszTarget ) )
			return CBaseEntity::Instance( pEdict );
	}
	return NULL;
}

static void cmd_touchdest( const char *pszTarget, int iPlayerIndex )
{
	CBaseEntity *pEnt = SGTC_TestFindEntityByTargetField( pszTarget );
	CBaseEntity *pPlayer = NULL;
	for( int i = ( iPlayerIndex > 0 ? iPlayerIndex : 1 ); i <= gpGlobals->maxClients && !pPlayer; ++i )
	{
		if( iPlayerIndex > 0 && i != iPlayerIndex )
			continue;
		CBaseEntity *p = UTIL_PlayerByIndex( i );
		if( p && p->IsPlayer() && p->IsAlive() )
			pPlayer = p;
	}
	if( !pEnt || !pPlayer )
	{
		test_out( "SGTCTEST {\"cmd\":\"touchdest\",\"target\":\"%s\",\"found\":%d,\"player\":%d}",
			pszTarget ? pszTarget : "", pEnt ? 1 : 0, pPlayer ? 1 : 0 );
		return;
	}
	const Vector vecBefore = pPlayer->pev->origin;
	DispatchTouch( ENT( pEnt->pev ), ENT( pPlayer->pev ) );
	test_out( "SGTCTEST {\"cmd\":\"touchdest\",\"target\":\"%s\",\"class\":\"%s\",\"player\":\"%s\","
		"\"before\":[%.1f,%.1f,%.1f],\"after\":[%.1f,%.1f,%.1f]}",
		pszTarget ? pszTarget : "", STRING( pEnt->pev->classname ), STRING( pPlayer->pev->netname ),
		vecBefore.x, vecBefore.y, vecBefore.z,
		pPlayer->pev->origin.x, pPlayer->pev->origin.y, pPlayer->pev->origin.z );
}

static void cmd_touchent( const char *pszName, int iPlayerIndex );
static void cmd_touchdest( const char *pszTarget, int iPlayerIndex );
static void cmd_mechlist( void )
{
	test_out( "SGTCTEST {\"cmd\":\"mech\",\"kind\":\"iris\",\"open\":%d,\"gates\":%d,\"rings\":%d,\"doors\":%d}",
		SGTC_Mech_IrisOpen(), SGTC_Mech_GateCount(), SGTC_Mech_RingCount(), SGTC_Mech_DoorCount() );
	for( int i = 0; i < SGTC_Mech_GateCount(); ++i )
	{
		sgtc_mech_gate_t g;
		const char *psz = SGTC_Mech_GateId( i, &g );
		test_out( "SGTCTEST {\"cmd\":\"mechgate\",\"id\":\"%s\",\"state\":%d,\"iris\":%d,"
			"\"dest\":%d,\"cooldown\":%.2f,\"danger\":%d,\"teamOnly\":%d}", psz ? psz : "", g.state, g.irisOpen,
			g.destinationId, g.cooldown, g.danger, g.teamOnly );
	}
	for( int i = 0; i < SGTC_Mech_RingCount(); ++i )
	{
		sgtc_mech_ring_t r;
		const char *psz = SGTC_Mech_RingId( i, &r );
		test_out( "SGTCTEST {\"cmd\":\"mechring\",\"id\":\"%s\",\"state\":%d,\"dest\":%d,"
			"\"cooldown\":%.2f,\"canUse\":%d}", psz ? psz : "", r.state, r.destinationId, r.cooldown, r.canUse );
	}
	for( int i = 0; i < SGTC_Mech_ObjectiveCount(); ++i )
	{
		sgtc_mech_objective_t o;
		const char *psz = SGTC_Mech_ObjectiveId( i, &o );
		test_out( "SGTCTEST {\"cmd\":\"mechobj\",\"id\":\"%s\",\"class\":\"%s\",\"team\":%d,"
			"\"active\":%d,\"state\":%d,\"origin\":[%.1f,%.1f,%.1f]}", psz ? psz : "", o.classname, o.team,
			o.active, o.state, o.origin[0], o.origin[1], o.origin[2] );
	}
	for( int i = 0; i < SGTC_Mech_DoorCount(); ++i )
	{
		sgtc_mech_door_t d;
		const char *psz = SGTC_Mech_DoorId( i, &d );
		test_out( "SGTCTEST {\"cmd\":\"mechdoor\",\"id\":\"%s\",\"state\":%d,\"locked\":%d}",
			psz ? psz : "", d.state, d.locked );
	}
}

extern BOOL SGTC_PackAuto_HasChosenLoadout( CBasePlayer *pPlayer );
extern "C" int SGTC_ShouldCollide( edict_t *pentTouched, edict_t *pentOther );
static CBasePlayer *SGTC_TestPlayerAt( int iIndex );
static void cmd_bugtest( void );
static void cmd_teamtest( void );
static void cmd_shieldtest( void );
static void cmd_collidetest( void );
static void cmd_c4firetest( void );
static void cmd_c4count( void );
static void cmd_spawn( const char *classname, float flDist );
static void cmd_freezebolt( void );
static void cmd_c4drop( void );
static void cmd_firepatchtest( void );
static void cmd_reetoutest( void );
static void cmd_c4detonate( void );
static void cmd_hazardenable( const char *pszStateId );
static void cmd_hazardat( float x, float y, float z );
static CBaseEntity *SGTC_TestFindEntity( const char * );
static void cmd_useentdest( const char *pszTarget );
static void cmd_ruletest( void );
static void cmd_gategrab( void );
static void cmd_botstate( void );
static void cmd_forceshield( int iSlot );
static void cmd_forceinvis( int iSlot );
static void cmd_forceshieldall( void );
static void cmd_forceinvisall( void );

static void cmd_forceshieldall( void )
{
	int count = 0;
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *p = SGTC_TestPlayerAt( i );
		if( !p || !p->IsAlive() )
			continue;
		p->m_bShieldWall = 1;
		p->m_bShieldSpawned = 0;
		count++;
	}
	test_out( "SGTCTEST {\"cmd\":\"forceshieldall\",\"players\":%d}", count );
}

static void cmd_forceinvisall( void )
{
	int count = 0;
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *p = SGTC_TestPlayerAt( i );
		if( !p || !p->IsAlive() )
			continue;
		p->m_bShieldSprint = 1;
		p->m_flSgtcInvisReveal = 0.0f;
		count++;
	}
	test_out( "SGTCTEST {\"cmd\":\"forceinvisall\",\"players\":%d}", count );
}


// Scenario mover for behavioural probes: movebot_<slot>_<x>_<y>_<z> and
// fakehuman_<slot>_<health> (clears FL_FAKECLIENT so the bot looks like a real
// player to the heal-priority scan; probe servers only).
static void cmd_movebot( const char *pszArgs )
{
	int slot = 1;
	float x = 0.0f, y = 0.0f, z = 0.0f;
	if( sscanf( pszArgs, "%d_%f_%f_%f", &slot, &x, &y, &z ) != 4 )
	{
		test_out( "SGTCTEST {\"cmd\":\"movebot\",\"error\":\"usage slot_x_y_z\"}" );
		return;
	}
	CBasePlayer *p = SGTC_TestPlayerAt( slot );
	if( !p )
	{
		test_out( "SGTCTEST {\"cmd\":\"movebot\",\"slot\":%d,\"player\":0}", slot );
		return;
	}
	Vector pos( x, y, z );
	UTIL_SetOrigin( p->pev, pos );
	p->pev->velocity = Vector( 0, 0, 0 );
	test_out( "SGTCTEST {\"cmd\":\"movebot\",\"slot\":%d,\"name\":\"%s\",\"origin\":[%.1f,%.1f,%.1f]}",
		slot, STRING( p->pev->netname ), p->pev->origin.x, p->pev->origin.y, p->pev->origin.z );
}

static void cmd_fakehuman( const char *pszArgs )
{
	int slot = 1;
	float health = 45.0f;
	if( sscanf( pszArgs, "%d_%f", &slot, &health ) != 2 )
		sscanf( pszArgs, "%d", &slot );
	CBasePlayer *p = SGTC_TestPlayerAt( slot );
	if( !p )
	{
		test_out( "SGTCTEST {\"cmd\":\"fakehuman\",\"slot\":%d,\"player\":0}", slot );
		return;
	}
	p->pev->flags &= ~FL_FAKECLIENT;
	p->pev->health = health;
	test_out( "SGTCTEST {\"cmd\":\"fakehuman\",\"slot\":%d,\"name\":\"%s\",\"health\":%.0f,"
		"\"team\":%d,\"fakeclient\":0}", slot, STRING( p->pev->netname ), p->pev->health,
		SGTC_EntTeamToTeam( (int)p->pev->team ) );
}


static void cmd_forcecloak( int iSlot, int iOn )
{
#ifdef BUILD_JK_BOTTI
	if( iSlot < 0 || iSlot >= 32 )
	{
		test_out( "SGTCTEST {\"cmd\":\"forcecloak\",\"error\":\"slot\"}" );
		return;
	}
	SGTC_Bots_RulesAI_DebugForceCloak( iSlot, iOn );
	test_out( "SGTCTEST {\"cmd\":\"forcecloak\",\"slot\":%d,\"on\":%d}", iSlot, iOn ? 1 : 0 );
#else
	(void)iSlot; (void)iOn;
	test_out( "SGTCTEST {\"cmd\":\"forcecloak\",\"bots\":0}" );
#endif
}

static void cmd_clearforce( int iSlot );
static void cmd_movebot( const char *pszArgs );
static void cmd_fakehuman( const char *pszArgs );
static void cmd_forcecloak( int iSlot, int iOn );

static void SGTC_TestRunToken( const char *pszToken )
{
	if( !pszToken || !pszToken[0] )
		return;
	if( !strncmp( pszToken, "useent_", 7 ) )
		cmd_useent( pszToken + 7 );
	else if( !strncmp( pszToken, "touchent_", 9 ) )
		cmd_touchent( pszToken + 9, 0 );
	else if( !strncmp( pszToken, "touchdest_", 10 ) )
		cmd_touchdest( pszToken + 10, 0 );
	else if( !strncmp( pszToken, "useentdest_", 11 ) )
		cmd_useentdest( pszToken + 11 );
	else if( !strncmp( pszToken, "entinfo_", 8 ) )
		cmd_entinfo( pszToken + 8 );
	else if( !strcmp( pszToken, "ruletest" ) )
		cmd_ruletest();
	else if( !strcmp( pszToken, "mechlist" ) )
		cmd_mechlist();
	else if( !strcmp( pszToken, "weaponlog" ) )
	{
		CVAR_SET_STRING( "sgtc_bot_weaponlog", "1" );
		test_out( "SGTCTEST {\"cmd\":\"weaponlog\",\"on\":1}" );
	}
	else if( !strcmp( pszToken, "weaponlogoff" ) )
	{
		CVAR_SET_STRING( "sgtc_bot_weaponlog", "0" );
		test_out( "SGTCTEST {\"cmd\":\"weaponlog\",\"on\":0}" );
	}
	else if( !strcmp( pszToken, "gategrab" ) )
		cmd_gategrab();
	else if( !strcmp( pszToken, "botstate" ) )
		cmd_botstate();
	else if( !strncmp( pszToken, "forceshield_", 12 ) )
		cmd_forceshield( atoi( pszToken + 12 ) );
	else if( !strncmp( pszToken, "forceinvis_", 11 ) )
		cmd_forceinvis( atoi( pszToken + 11 ) );
	else if( !strcmp( pszToken, "forceshieldall" ) )
		cmd_forceshieldall();
	else if( !strcmp( pszToken, "forceinvisall" ) )
		cmd_forceinvisall();
	else if( !strncmp( pszToken, "movebot_", 8 ) )
		cmd_movebot( pszToken + 8 );
	else if( !strncmp( pszToken, "fakehuman_", 10 ) )
		cmd_fakehuman( pszToken + 10 );
	else if( !strncmp( pszToken, "forcecloak_", 11 ) )
		cmd_forcecloak( atoi( pszToken + 11 ), 1 );
	else if( !strncmp( pszToken, "clearcloak_", 11 ) )
		cmd_forcecloak( atoi( pszToken + 11 ), 0 );
	else if( !strncmp( pszToken, "clearforce_", 11 ) )
		cmd_clearforce( atoi( pszToken + 11 ) );
	else if( !strcmp( pszToken, "bugtest" ) )
		cmd_bugtest();
	else if( !strcmp( pszToken, "teamtest" ) )
		cmd_teamtest();
	else if( !strcmp( pszToken, "shieldtest" ) )
		cmd_shieldtest();
	else if( !strcmp( pszToken, "collidetest" ) )
		cmd_collidetest();
	else if( !strcmp( pszToken, "c4firetest" ) )
		cmd_c4firetest();
	else if( !strcmp( pszToken, "c4count" ) )
		cmd_c4count();
	else if( !strcmp( pszToken, "freezebolt" ) )
		cmd_freezebolt();
	else if( !strcmp( pszToken, "biglance" ) )
		cmd_spawn( "big_lance_proj", 120.0f );
	else if( !strcmp( pszToken, "firepatchtest" ) )
		cmd_firepatchtest();
	else if( !strcmp( pszToken, "c4drop" ) )
		cmd_c4drop();
	else if( !strcmp( pszToken, "reetoutest" ) )
		cmd_reetoutest();
	else if( !strcmp( pszToken, "c4detonate" ) )
		cmd_c4detonate();
	else if( !strncmp( pszToken, "hazardenable_", 13 ) )
		cmd_hazardenable( pszToken + 13 );
	else if( !strncmp( pszToken, "hazardat_", 10 ) )
		cmd_hazardat( 0.0f, 0.0f, 0.0f );
	else
		SGTC_TestCommand( pszToken );
}

static void cmd_teamtest( void )
{
	CBasePlayer *p = SGTC_TestPlayerAt( 1 );
	CBaseEntity *pDial1 = SGTC_TestFindEntity( "dial1" );
	CBaseEntity *pDial2 = SGTC_TestFindEntity( "dial2" );
	if( !p )
	{
		test_out( "SGTCTEST {\"cmd\":\"teamtest\",\"player\":0}" );
		return;
	}
	const int iSavedActive = p->m_iSgtcActiveTeam;
	const int iSavedTeam = p->m_iSgtcTeam;
	p->m_iSgtcActiveTeam = 1;
	p->m_iSgtcTeam = 1;
	const int iTorriDial1 = SGTC_TeamFilterAllows( pDial1, p ) ? 1 : 0;
	const int iTorriDial2 = SGTC_TeamFilterAllows( pDial2, p ) ? 1 : 0;
	p->m_iSgtcActiveTeam = 2;
	p->m_iSgtcTeam = 2;
	const int iGoauldDial1 = SGTC_TeamFilterAllows( pDial1, p ) ? 1 : 0;
	const int iGoauldDial2 = SGTC_TeamFilterAllows( pDial2, p ) ? 1 : 0;
	p->m_iSgtcActiveTeam = iSavedActive;
	p->m_iSgtcTeam = iSavedTeam;
	int iTeamTriggers = 0;
	int iNeedCardButtons = 0;
	char szFirstTeamTrigger[64] = {0};
	char szFirstNeedCard[64] = {0};
	CBaseEntity *pTeamTrigger = NULL;
	CBaseEntity *pNeedCardButton = NULL;
	for( int i = 1; i < gpGlobals->maxEntities; ++i )
	{
		edict_t *e = INDEXENT( i );
		if( !e || e->free || !e->v.classname )
			continue;
		const char *pszClass = STRING( e->v.classname );
		if( (int)e->v.fuser1 == 1 || (int)e->v.fuser1 == 2 )
		{
			if( !strncmp( pszClass, "trigger_", 8 ) )
			{
				++iTeamTriggers;
				if( !pTeamTrigger )
					pTeamTrigger = CBaseEntity::Instance( e );
				if( !szFirstTeamTrigger[0] )
					snprintf( szFirstTeamTrigger, sizeof( szFirstTeamTrigger ), "%s:%d", pszClass, i );
			}
		}
		if( !strncmp( pszClass, "func_button", 11 ) && e->v.fuser2 > 0.0f )
		{
			++iNeedCardButtons;
			if( !pNeedCardButton )
				pNeedCardButton = CBaseEntity::Instance( e );
			if( !szFirstNeedCard[0] )
				snprintf( szFirstNeedCard, sizeof( szFirstNeedCard ), "%s:%d", pszClass, i );
		}
	}
	p->m_iSgtcActiveTeam = 1;
	p->m_iSgtcTeam = 1;
	const int iTeamTrigTorri = pTeamTrigger ? ( SGTC_TeamFilterAllows( pTeamTrigger, p ) ? 1 : 0 ) : -1;
	p->m_iSgtcActiveTeam = 2;
	p->m_iSgtcTeam = 2;
	const int iTeamTrigGoauld = pTeamTrigger ? ( SGTC_TeamFilterAllows( pTeamTrigger, p ) ? 1 : 0 ) : -1;
	const int iNeedCard = SGTC_EntityNeedCard( pNeedCardButton ) ? 1 : 0;
	p->m_iSgtcActiveTeam = iSavedActive;
	p->m_iSgtcTeam = iSavedTeam;
	test_out( "SGTCTEST {\"cmd\":\"teamtest\",\"dial1\":%d,\"dial2\":%d,"
		"\"dial1_team\":%d,\"dial2_team\":%d,"
		"\"torri_dial1\":%d,\"torri_dial2\":%d,\"goauld_dial1\":%d,\"goauld_dial2\":%d,"
		"\"team_triggers\":%d,\"first_team_trigger\":\"%s\","
		"\"needcard_buttons\":%d,\"first_needcard\":\"%s\","
		"\"team_trigger_torri\":%d,\"team_trigger_goauld\":%d,\"needcard_flag\":%d}",
		pDial1 ? 1 : 0, pDial2 ? 1 : 0,
		(int)SGTC_EntityTeamFilter( pDial1 ), (int)SGTC_EntityTeamFilter( pDial2 ),
		iTorriDial1, iTorriDial2, iGoauldDial1, iGoauldDial2,
		iTeamTriggers, szFirstTeamTrigger, iNeedCardButtons, szFirstNeedCard,
		iTeamTrigTorri, iTeamTrigGoauld, iNeedCard );
}

static void cmd_bugtest( void )
{
	CBasePlayer *p = SGTC_TestPlayerAt( 1 );
	if( !p )
	{
		test_out( "SGTCTEST {\"cmd\":\"bugtest\",\"player\":0}" );
		return;
	}
	const unsigned int iSavedEffects = p->pev->effects;
	p->m_bSgtcCloakTick = 1;
	p->pev->health = 100.0f;
	p->m_flCloakTickNext = 0.0f;
	p->SGTC_TickCloakPulse();
	const int iCloakNodraw = ( p->pev->effects & EF_NODRAW ) ? 1 : 0;
	const float flCloakAmt = p->pev->renderamt;
	const int iCloakMode = (int)p->pev->rendermode;
	p->m_bSgtcCloakTick = 0;
	p->pev->effects = iSavedEffects;
	const unsigned int iSavedWeapons = (unsigned int)p->pev->weapons;
	p->pev->weapons = ( 1u << SGTC_WPN_COUTEAU );
	const int iBaseOnly = SGTC_PackAuto_HasChosenLoadout( p ) ? 1 : 0;
	p->pev->weapons = ( 1u << SGTC_WPN_COUTEAU ) | ( 1u << SGTC_WPN_M16 );
	const int iWithM16 = SGTC_PackAuto_HasChosenLoadout( p ) ? 1 : 0;
	p->pev->weapons = iSavedWeapons;
	test_out( "SGTCTEST {\"cmd\":\"bugtest\",\"cloak_nodraw\":%d,\"cloak_renderamt\":%.0f,"
		"\"cloak_rendermode\":%d,\"pack_base_only_cancel\":%d,\"pack_with_m16_cancel\":%d}",
		iCloakNodraw, flCloakAmt, iCloakMode, iBaseOnly, iWithM16 );
}

static void cmd_hazardenable( const char *pszStateId )
{
	if( !pszStateId || !*pszStateId )
	{
		test_out( "SGTCTEST {\"cmd\":\"hazardenable\",\"error\":\"missing\"}" );
		return;
	}
	const char *pColon = strchr( pszStateId, ':' );
	if( !pColon )
	{
		test_out( "SGTCTEST {\"cmd\":\"hazardenable\",\"error\":\"bad id\"}" );
		return;
	}
	const int iIndex = atoi( pColon + 1 );
	edict_t *pEdict = ( iIndex > 0 && iIndex < gpGlobals->maxEntities ) ? INDEXENT( iIndex ) : NULL;
	if( !pEdict || pEdict->free )
	{
		test_out( "SGTCTEST {\"cmd\":\"hazardenable\",\"id\":\"%s\",\"found\":0}", pszStateId );
		return;
	}
	pEdict->v.solid = SOLID_TRIGGER;
	test_out( "SGTCTEST {\"cmd\":\"hazardenable\",\"id\":\"%s\",\"class\":\"%s\",\"solid\":%d}",
		pszStateId, STRING( pEdict->v.classname ), (int)pEdict->v.solid );
}

static void cmd_hazardat( float x, float y, float z )
{
	float flDamage = 0.0f;
	int iHit;
	if( x == 0.0f && y == 0.0f && z == 0.0f )
		iHit = SGTC_Mech_HazardFirstActive( &x, &y, &z, &flDamage );
	else
		iHit = SGTC_Mech_HazardAt( x, y, z, &flDamage );
	test_out( "SGTCTEST {\"cmd\":\"hazardat\",\"pos\":[%.1f,%.1f,%.1f],\"hit\":%d,\"damage\":%.0f}",
		x, y, z, iHit, flDamage );
}

static void cmd_shieldtest( void )
{
	for( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if( !pEnt || !pEnt->IsPlayer() || !pEnt->IsAlive() )
			continue;
		CBasePlayer *p = (CBasePlayer *)pEnt;
		p->m_bShieldWall = 1;
		p->m_iShieldCharge = 10;
		p->SGTC_ShieldThinker();
		CBaseEntity *pShield = NULL;
		while( ( pShield = UTIL_FindEntityByClassname( pShield, "effect_shield" ) ) != NULL )
		{
			if( pShield->pev->owner != p->edict() )
				continue;
			test_out( "SGTCTEST {\"cmd\":\"shieldcolor\",\"player\":\"%s\",\"team\":%d,"
				"\"class\":%d,\"rgb\":[%.0f,%.0f,%.0f],\"renderamt\":%.0f}",
				STRING( p->pev->netname ), (int)p->m_iSgtcActiveTeam,
				p->m_iSgtcPortraitShown,
				pShield->pev->rendercolor.x, pShield->pev->rendercolor.y, pShield->pev->rendercolor.z,
				pShield->pev->renderamt );
		}
	}
}

static void cmd_collidetest( void )
{
	edict_t *pA = NULL, *pB = NULL, *pC = NULL;
	for( int i = 1; i <= gpGlobals->maxClients && (!pA || !pB || !pC); ++i )
	{
		edict_t *e = INDEXENT( i );
		if( !e || e->free || !( e->v.flags & FL_CLIENT ) )
			continue;
		if( !pA ) { pA = e; continue; }
		if( !pB && e->v.team == pA->v.team ) pB = e;
		else if( !pC && e->v.team != pA->v.team ) pC = e;
	}
	test_out( "SGTCTEST {\"cmd\":\"collidetest\",\"same_team\":%d,\"enemy\":%d,"
		"\"teamA\":%d,\"teamB\":%d,\"teamC\":%d}",
		SGTC_ShouldCollide( pA, pB ), SGTC_ShouldCollide( pA, pC ),
		pA ? (int)pA->v.team : -1, pB ? (int)pB->v.team : -1, pC ? (int)pC->v.team : -1 );
}

static void cmd_c4firetest( void )
{
	CBasePlayer *p = SGTC_TestPlayerAt( 1 );
	if( !p ) { test_out( "SGTCTEST {\"cmd\":\"c4firetest\",\"player\":0}" ); return; }
	CBaseEntity *pCharge = CBaseEntity::Create( "monster_satchel",
		p->pev->origin + gpGlobals->v_forward * 40.0f, g_vecZero, p->edict() );
	if( !pCharge ) { test_out( "SGTCTEST {\"cmd\":\"c4firetest\",\"created\":0}" ); return; }
	pCharge->pev->velocity = gpGlobals->v_forward * 200.0f;
	pCharge->Use( p, p, USE_ON, 0 );
	test_out( "SGTCTEST {\"cmd\":\"c4firetest\",\"created\":1}" );
}

static void cmd_c4drop( void )
{
	CBasePlayer *p = SGTC_TestPlayerAt( 1 );
	if( !p ) { test_out( "SGTCTEST {\"cmd\":\"c4drop\",\"player\":0}" ); return; }
	Vector vecDrop = p->pev->origin + gpGlobals->v_forward * 48.0f + Vector( 0, 0, 64.0f );
	CBaseEntity *pCharge = CBaseEntity::Create( "monster_satchel", vecDrop, g_vecZero, p->edict() );
	if( !pCharge ) { test_out( "SGTCTEST {\"cmd\":\"c4drop\",\"created\":0}" ); return; }
	pCharge->pev->velocity = Vector( 60.0f, 0.0f, -120.0f );
	test_out( "SGTCTEST {\"cmd\":\"c4drop\",\"created\":1,\"origin\":[%.1f,%.1f,%.1f]}",
		vecDrop.x, vecDrop.y, vecDrop.z );
}

static void cmd_c4detonate( void )
{
	CBasePlayer *p = SGTC_TestPlayerAt( 1 );
	CBaseEntity *pCharge = NULL;
	int n = 0;
	while( ( pCharge = UTIL_FindEntityByClassname( pCharge, "monster_satchel" ) ) != NULL )
	{
		if( p && pCharge->pev->owner == p->edict() )
		{
			pCharge->Use( p, p, USE_ON, 0 );
			++n;
		}
	}
	test_out( "SGTCTEST {\"cmd\":\"c4detonate\",\"charges\":%d}", n );
}

static void cmd_firepatchtest( void )
{
	CBasePlayer *p = SGTC_TestPlayerAt( 1 );
	if( !p ) { test_out( "SGTCTEST {\"cmd\":\"firepatchtest\",\"player\":0}" ); return; }
	Vector pos = p->pev->origin + gpGlobals->v_forward * 96.0f;
	SGTC_FirePatch_Spawn( pos, 3.5f, 7.0f );
	test_out( "SGTCTEST {\"cmd\":\"firepatchtest\",\"origin\":[%.1f,%.1f,%.1f]}", pos.x, pos.y, pos.z );
}

static void cmd_c4count( void )
{
	int nFire = 0;
	CBaseEntity *pEnt = NULL;
	while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "sgtc_c4_fire" ) ) != NULL )
		++nFire;
	test_out( "SGTCTEST {\"cmd\":\"c4count\",\"fire\":%d,\"time\":%.1f}", nFire, gpGlobals->time );
}

static void cmd_reetoutest( void )
{
	CBasePlayer *p = SGTC_TestPlayerAt( 1 );
	if( !p ) { test_out( "SGTCTEST {\"cmd\":\"reetoutest\",\"player\":0}" ); return; }
	CBasePlayerWeapon *pReetou = NULL;
	for( int i = 0; i < MAX_ITEM_TYPES && !pReetou; ++i )
		for( CBasePlayerItem *it = p->m_rgpPlayerItems[i]; it; it = it->m_pNext )
			if( FClassnameIs( it->pev, "weapon_reetou" ) ) { pReetou = (CBasePlayerWeapon *)it; break; }
	if( !pReetou )
	{
		p->GiveNamedItem( "weapon_reetou" );
		for( int i = 0; i < MAX_ITEM_TYPES && !pReetou; ++i )
			for( CBasePlayerItem *it = p->m_rgpPlayerItems[i]; it; it = it->m_pNext )
				if( FClassnameIs( it->pev, "weapon_reetou" ) ) { pReetou = (CBasePlayerWeapon *)it; break; }
	}
	if( !pReetou ) { test_out( "SGTCTEST {\"cmd\":\"reetoutest\",\"weapon\":0}" ); return; }
	p->m_pActiveItem = pReetou;
	pReetou->SecondaryAttack();	// on
	test_out( "SGTCTEST {\"cmd\":\"reetou_on\",\"renderfx\":%d,\"amt\":%.0f,\"rgb\":[%.0f,%.0f,%.0f]}",
		(int)p->pev->renderfx, p->pev->renderamt,
		p->pev->rendercolor.x, p->pev->rendercolor.y, p->pev->rendercolor.z );
	pReetou->m_flNextSecondaryAttack = UTIL_WeaponTimeBase();
	pReetou->SecondaryAttack();	// off
	test_out( "SGTCTEST {\"cmd\":\"reetou_off\",\"renderfx\":%d,\"amt\":%.0f,\"rgb\":[%.0f,%.0f,%.0f]}",
		(int)p->pev->renderfx, p->pev->renderamt,
		p->pev->rendercolor.x, p->pev->rendercolor.y, p->pev->rendercolor.z );
}

static void cmd_gategrab( void )
{
	cmd_mechlist();
	CBasePlayer *pPlayer = NULL;
	for( int i = 1; i <= gpGlobals->maxClients && !pPlayer; ++i )
	{
		CBaseEntity *p = UTIL_PlayerByIndex( i );
		if( p && p->IsPlayer() && p->IsAlive() )
			pPlayer = (CBasePlayer *)p;
	}
	if( !pPlayer )
	{
		test_out( "SGTCTEST {\"cmd\":\"gategrab\",\"player\":0}" );
		return;
	}
	for( int i = 0; i < SGTC_Mech_GateCount(); ++i )
	{
		sgtc_mech_gate_t g;
		const char *pszId = SGTC_Mech_GateId( i, &g );
		if( !pszId || g.state != 2 || !g.destinationId )
			continue;
		CBaseEntity *pTrig = SGTC_TestFindEntity( pszId );
		if( !pTrig )
			continue;
		const Vector vecBefore = pPlayer->pev->origin;
		DispatchTouch( ENT( pTrig->pev ), ENT( pPlayer->pev ) );
		test_out( "SGTCTEST {\"cmd\":\"gategrab\",\"id\":\"%s\",\"player\":\"%s\","
			"\"before\":[%.1f,%.1f,%.1f],\"after\":[%.1f,%.1f,%.1f]}",
			pszId, STRING( pPlayer->pev->netname ), vecBefore.x, vecBefore.y, vecBefore.z,
			pPlayer->pev->origin.x, pPlayer->pev->origin.y, pPlayer->pev->origin.z );
		return;
	}
	test_out( "SGTCTEST {\"cmd\":\"gategrab\",\"armed\":0}" );
}

static void cmd_useentdest( const char *pszTarget )
{
	CBaseEntity *pEnt = SGTC_TestFindEntityByTargetField( pszTarget );
	CBasePlayer *pPlayer = NULL;
	for( int i = 1; i <= gpGlobals->maxClients && !pPlayer; ++i )
	{
		CBaseEntity *p = UTIL_PlayerByIndex( i );
		if( p && p->IsPlayer() && p->IsAlive() )
			pPlayer = (CBasePlayer *)p;
	}
	if( !pEnt || !pPlayer )
	{
		test_out( "SGTCTEST {\"cmd\":\"useentdest\",\"target\":\"%s\",\"found\":%d}", pszTarget ? pszTarget : "", pEnt ? 1 : 0 );
		return;
	}
	pEnt->Use( pPlayer, NULL, USE_TOGGLE, 0 );
	test_out( "SGTCTEST {\"cmd\":\"useentdest\",\"target\":\"%s\",\"class\":\"%s\",\"activated\":1}", pszTarget ? pszTarget : "", STRING( pEnt->pev->classname ) );
}

// Periodic movement sampler for headless runs (dedicated server or listen):
// set $sgtc_test_watch 1 and every 2 seconds this prints one `players` sample
// (time, model, maxspeed, velocity, origin per player) to the server log.
// Used to measure player/bot movement speed without a client UI.
void SGTC_TestFrame( void )
{
	// build-12 mechanics acceptance: one-shot delayed script (runs after players
	// spawn) and a periodic entinfo probe, both test-only cvars.
	cvar_t *pTimed = CVAR_GET_POINTER( "sgtc_test_timed_script" );
	cvar_t *pTimedDelay = CVAR_GET_POINTER( "sgtc_test_timed_delay" );
	if( pTimed && pTimed->string && pTimed->string[0] && pTimedDelay && pTimedDelay->value > 0.0f &&
		!s_test_timed_done && gpGlobals->time >= pTimedDelay->value )
	{
		s_test_timed_done = TRUE;
		test_out( "SGTCTEST {\"cmd\":\"timed_script\",\"state\":\"start\",\"script\":\"%s\"}", pTimed->string );
		SGTC_TestRunToken( pTimed->string );
		test_out( "SGTCTEST {\"cmd\":\"timed_script\",\"state\":\"done\"}" );
	}
	// Second one-shot script (e.g. gate touch after the DHD chain has dialled).
	cvar_t *pTimed2 = CVAR_GET_POINTER( "sgtc_test_timed2_script" );
	cvar_t *pTimed2Delay = CVAR_GET_POINTER( "sgtc_test_timed2_delay" );
	if( pTimed2 && pTimed2->string && pTimed2->string[0] && pTimed2Delay && pTimed2Delay->value > 0.0f &&
		!s_test_timed2_done && gpGlobals->time >= pTimed2Delay->value )
	{
		s_test_timed2_done = TRUE;
		test_out( "SGTCTEST {\"cmd\":\"timed2_script\",\"state\":\"start\",\"script\":\"%s\"}", pTimed2->string );
		SGTC_TestRunToken( pTimed2->string );
		test_out( "SGTCTEST {\"cmd\":\"timed2_script\",\"state\":\"done\"}" );
	}
	// Third one-shot script (e.g. ring touch after the window closed).
	cvar_t *pTimed3 = CVAR_GET_POINTER( "sgtc_test_timed3_script" );
	cvar_t *pTimed3Delay = CVAR_GET_POINTER( "sgtc_test_timed3_delay" );
	if( pTimed3 && pTimed3->string && pTimed3->string[0] && pTimed3Delay && pTimed3Delay->value > 0.0f &&
		!s_test_timed3_done && gpGlobals->time >= pTimed3Delay->value )
	{
		s_test_timed3_done = TRUE;
		test_out( "SGTCTEST {\"cmd\":\"timed3_script\",\"state\":\"start\",\"script\":\"%s\"}", pTimed3->string );
		SGTC_TestRunToken( pTimed3->string );
		test_out( "SGTCTEST {\"cmd\":\"timed3_script\",\"state\":\"done\"}" );
	}
	// Frame-accurate trigger touch for narrow map windows (rings.bsp 10 ms):
	// while the trigger's target is armed (set to a real destination), dispatch
	// a touch every frame from a live player; logs the exact hit.
	cvar_t *pAuto = CVAR_GET_POINTER( "sgtc_test_autotouch" );
	cvar_t *pAutoStart = CVAR_GET_POINTER( "sgtc_test_autotouch_start" );
	cvar_t *pAutoEnd = CVAR_GET_POINTER( "sgtc_test_autotouch_end" );
	if( pAuto && pAuto->string && pAuto->string[0] && gpGlobals->time >= ( pAutoStart ? pAutoStart->value : 0.0f ) &&
		gpGlobals->time <= ( pAutoEnd ? pAutoEnd->value : 30.0f ) )
	{
		CBaseEntity *pTrig = SGTC_TestFindEntity( pAuto->string );
		if( pTrig && pTrig->pev->target && pTrig->pev->target != iStringNull )
		{
			const char *pszTarget = STRING( pTrig->pev->target );
			if( pszTarget && pszTarget[0] && strcmp( pszTarget, "Nowhere" ) && strcmp( pszTarget, "nowhere" ) &&
				strcmp( pszTarget, "nothing" ) )
			{
				CBasePlayer *pPl = NULL;
				for( int i = 1; i <= gpGlobals->maxClients && !pPl; ++i )
				{
					CBaseEntity *p = UTIL_PlayerByIndex( i );
					if( p && p->IsPlayer() && p->IsAlive() )
						pPl = (CBasePlayer *)p;
				}
				if( pPl )
				{
					const Vector vecBefore = pPl->pev->origin;
					DispatchTouch( ENT( pTrig->pev ), ENT( pPl->pev ) );
					test_out( "SGTCTEST {\"cmd\":\"autotouch\",\"trigger\":\"%s\",\"dest\":\"%s\","
						"\"player\":\"%s\",\"before\":[%.1f,%.1f,%.1f],\"after\":[%.1f,%.1f,%.1f]}",
						pAuto->string, pszTarget, STRING( pPl->pev->netname ),
						vecBefore.x, vecBefore.y, vecBefore.z,
						pPl->pev->origin.x, pPl->pev->origin.y, pPl->pev->origin.z );
					++s_test_autotouch_count;
					CVAR_SET_FLOAT( "sgtc_test_autotouch", 0.0f );	// one successful window per run
				}
			}
		}
	}
	// Repeating action (DHD press chain): count presses at interval after delay.
	cvar_t *pRep = CVAR_GET_POINTER( "sgtc_test_repeat_script" );
	cvar_t *pRepDelay = CVAR_GET_POINTER( "sgtc_test_repeat_delay" );
	cvar_t *pRepInt = CVAR_GET_POINTER( "sgtc_test_repeat_interval" );
	cvar_t *pRepCount = CVAR_GET_POINTER( "sgtc_test_repeat_count" );
	if( pRep && pRep->string && pRep->string[0] && pRepCount && pRepCount->value > 0.0f &&
		( pRepDelay ? pRepDelay->value : 0.0f ) >= 0.0f &&
		s_test_repeat_done < (int)pRepCount->value && gpGlobals->time >= ( pRepDelay ? pRepDelay->value : 0.0f ) &&
		gpGlobals->time >= s_test_repeat_next )
	{
		if( s_test_repeat_done == 0 )
			s_test_repeat_next = gpGlobals->time;
		if( s_test_repeat_next <= gpGlobals->time )
		{
			++s_test_repeat_done;
			test_out( "SGTCTEST {\"cmd\":\"repeat\",\"step\":%d,\"script\":\"%s\"}", s_test_repeat_done, pRep->string );
			SGTC_TestRunToken( pRep->string );
			const float flInt = ( pRepInt && pRepInt->value > 0.1f ) ? pRepInt->value : 1.0f;
			s_test_repeat_next = gpGlobals->time + flInt;
		}
	}
	cvar_t *pProbe = CVAR_GET_POINTER( "sgtc_test_probe" );
	cvar_t *pProbeInterval = CVAR_GET_POINTER( "sgtc_test_probe_interval" );
	if( pProbe && pProbe->string && pProbe->string[0] && gpGlobals->time >= s_test_probe_next )
	{
		const float flInterval = ( pProbeInterval && pProbeInterval->value > 0.1f ) ? pProbeInterval->value : 2.0f;
		s_test_probe_next = gpGlobals->time + flInterval;
		char line[256];
		snprintf( line, sizeof( line ), "entinfo %s", pProbe->string );
		SGTC_TestCommand( line );
	}

	cvar_t *pWatch = CVAR_GET_POINTER( "sgtc_test_watch" );
	if( !pWatch || pWatch->value <= 0.0f ) return;
	if( gpGlobals->time < s_test_watch_next ) return;
	s_test_watch_next = gpGlobals->time + 2.0;
	cmd_players();
}

// Lists every live monster_* entity. The campaign scenarios used
// `entities 20`, which only covers the first 20 entities in edict order and
// never reaches the monsters on a 174-entity SP map like gateroom; this
// probe makes "are the monsters/NPCs present and simulating" checkable
// without guessing an entity index.
static void cmd_monsters( int limit )
{
	int n = 0;
	for( int i = 1; i < gpGlobals->maxEntities && n < limit; ++i )
	{
		edict_t *e = INDEXENT( i );
		if( !e || e->free || !e->pvPrivateData ) continue;
		const char *cls = classname_of( e );
		if( strncmp( cls, "monster_", 8 ) != 0 ) continue;
		char cls_esc[128];
		json_escape( cls, cls_esc, sizeof( cls_esc ) );
		test_out( "SGTCTEST {\"cmd\":\"monster\",\"index\":%d,\"class\":\"%s\",\"health\":%.0f,\"movetype\":%d,\"flags\":%d,\"sequence\":%d,\"origin\":[%.1f,%.1f,%.1f]}",
			i, cls_esc, e->v.health, (int)e->v.movetype, (int)e->v.flags, (int)e->v.sequence,
			e->v.origin.x, e->v.origin.y, e->v.origin.z );
		++n;
	}
	test_out( "SGTCTEST {\"cmd\":\"monsters\",\"count\":%d,\"limit\":%d}", n, limit );
}

static void cmd_rules( void )
{
	char mode[128];
	json_escape( CVAR_GET_STRING( "mp_gamemode" ), mode, sizeof( mode ) );
	test_out( "SGTCTEST {\"cmd\":\"rules\",\"gamemode\":\"%s\",\"pointer\":%s,\"sv_cheats\":%.0f}",
		mode, g_pGameRules ? "true" : "false", CVAR_GET_FLOAT( "sv_cheats" ) );
}

static void cmd_cvar( const char *name )
{
	if( !name || !*name ) { test_out( "SGTCTEST {\"cmd\":\"cvar\",\"error\":\"missing name\"}" ); return; }
	cvar_t *cv = CVAR_GET_POINTER( name );
	char val[256];
	json_escape( cv ? cv->string : "", val, sizeof( val ) );
	test_out( "SGTCTEST {\"cmd\":\"cvar\",\"name\":\"%s\",\"found\":%s,\"value\":\"%s\",\"float\":%.3f}",
		name, cv ? "true" : "false", val, cv ? cv->value : 0.0f );
}

// Test-only teleport: move the local player to an absolute origin so a
// headless run can put the camera next to a spawned monster for screenshots.
static void cmd_tp( const char *x, const char *y, const char *z,
	const char *yaw, const char *pitch )
{
	edict_t *e = INDEXENT( 1 );
	CBasePlayer *pPlayer = ( e && !e->free ) ? (CBasePlayer *)e->pvPrivateData : NULL;
	if( !pPlayer )
	{
		test_out( "SGTCTEST {\"cmd\":\"tp\",\"error\":\"no player\"}" );
		return;
	}
	Vector vec( (float)atof( x ? x : "0" ), (float)atof( y ? y : "0" ), (float)atof( z ? z : "0" ) );
	pPlayer->pev->origin = vec;
	pPlayer->pev->velocity = Vector( 0, 0, 0 );
	pPlayer->pev->flags &= ~FL_ONGROUND;
	if( yaw && *yaw )
	{
		Vector vecAng( (float)atof( pitch ? pitch : "0" ), (float)atof( yaw ), 0 );
		pPlayer->pev->v_angle = vecAng;
		pPlayer->pev->angles = vecAng;
		pPlayer->pev->fixangle = 1;
	}
	test_out( "SGTCTEST {\"cmd\":\"tp\",\"origin\":[%.1f,%.1f,%.1f],\"yaw\":%.1f,\"pitch\":%.1f}",
		vec.x, vec.y, vec.z, yaw && *yaw ? (float)atof( yaw ) : 0.0f,
		pitch && *pitch ? (float)atof( pitch ) : 0.0f );
}

// Test-only spawn: create a class in front of the local player.  Used by the
// SP enemy/vs-weapon headless checks to get a target without console aiming.
static void cmd_freezebolt( void )
{
	CBaseEntity *pEnt = NULL;
	int n = 0;
	while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "big_lance_proj" ) ) != NULL )
	{
		pEnt->pev->velocity = g_vecZero;
		pEnt->pev->avelocity = g_vecZero;
		pEnt->pev->body = 1;	// EffectThink freeze flag: keep FX alive
		++n;
	}
	test_out( "SGTCTEST {\"cmd\":\"freezebolt\",\"frozen\":%d}", n );
}

static void cmd_spawn( const char *classname, float flDist )
{
	edict_t *e = INDEXENT( 1 );
	CBasePlayer *pPlayer = ( e && !e->free ) ? (CBasePlayer *)e->pvPrivateData : NULL;
	if( !pPlayer || !classname || !*classname )
	{
		test_out( "SGTCTEST {\"cmd\":\"spawn\",\"error\":\"no player or classname\"}" );
		return;
	}
	UTIL_MakeVectors( pPlayer->pev->v_angle );
	Vector vecOrigin = pPlayer->pev->origin + gpGlobals->v_forward * flDist;
	vecOrigin.z += 8.0f;
	CBaseEntity *pEnt = CBaseEntity::Create( classname, vecOrigin, pPlayer->pev->v_angle, NULL );
	if( !pEnt )
	{
		test_out( "SGTCTEST {\"cmd\":\"spawn\",\"class\":\"%s\",\"error\":\"create failed\"}", classname );
		return;
	}
	test_out( "SGTCTEST {\"cmd\":\"spawn\",\"class\":\"%s\",\"origin\":[%.1f,%.1f,%.1f],\"health\":%.0f}",
		classname, pEnt->pev->origin.x, pEnt->pev->origin.y, pEnt->pev->origin.z, pEnt->pev->health );
}

// SGTCTEST unarmed <team> <class>
//
// Test-only: put the first human player into the classed-but-unarmed state a
// VGUI class click produces (no weapons granted), then arm the random-pack
// timer and open the loadout panel. Lets a headless run observe
// arm -> (>= sgtc_pack_autoselect_time) -> cancel/select and the panel close,
// which the console "classe" path cannot reach because it spawns armed.
static void cmd_unarmed( int team, int cls )
{
	CBasePlayer *pPlayer = NULL;
	for( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBaseEntity *plr = UTIL_PlayerByIndex( i );
		if( !plr || !plr->IsPlayer() )
			continue;
		CBasePlayer *p = (CBasePlayer *)plr;
		if( FBitSet( p->pev->flags, FL_FAKECLIENT ) )
			continue;
		pPlayer = p;
		break;
	}
	if( !pPlayer || !pPlayer->pev )
	{
		test_out( "SGTCTEST {\"cmd\":\"unarmed\",\"error\":\"no human player\"}" );
		return;
	}

	pPlayer->m_iSgtcTeam = team;
	pPlayer->m_iSgtcPortraitSel = cls;
	pPlayer->m_iSgtcPortraitShown = cls;
	pPlayer->RemoveAllItems( TRUE );

	const float flBefore = pPlayer->m_flSgtcPackDeadline;
	SGTC_PackAuto_Start( pPlayer );

	// Same panel the class-click flow opens, so the client-side close path is
	// exercised end to end.
	pPlayer->ShowVGUIMenu( 60 );

	test_out( "SGTCTEST {\"cmd\":\"unarmed\",\"player\":\"%s\",\"team\":%d,\"class\":%d,"
		"\"weapons\":%d,\"deadline_before\":%.3f,\"deadline\":%.3f}",
		STRING( pPlayer->pev->netname ), team, cls, (int)pPlayer->pev->weapons,
		flBefore, pPlayer->m_flSgtcPackDeadline );
}

// Test-only spawn at an absolute origin (no player needed), so a ServerActivate
// test script can place a projectile/entity and probe it headlessly.
static void cmd_spawnat( const char *classname, const char *x, const char *y, const char *z )
{
	if( !classname || !*classname ) { test_out( "SGTCTEST {\"cmd\":\"spawnat\",\"error\":\"missing classname\"}" ); return; }
	Vector vecOrigin( (float)atof( x ? x : "0" ), (float)atof( y ? y : "0" ), (float)atof( z ? z : "0" ) );
	CBaseEntity *pEnt = CBaseEntity::Create( classname, vecOrigin, Vector( 0, 0, 0 ), NULL );
	if( !pEnt ) { test_out( "SGTCTEST {\"cmd\":\"spawnat\",\"class\":\"%s\",\"error\":\"create failed\"}", classname ); return; }
	Vector vecModel = pEnt->pev->size;
	test_out( "SGTCTEST {\"cmd\":\"spawnat\",\"class\":\"%s\",\"origin\":[%.1f,%.1f,%.1f],\"size\":[%.1f,%.1f,%.1f],\"scale\":%.2f,\"body\":%d,\"skin\":%d,\"sequence\":%d}",
		classname, pEnt->pev->origin.x, pEnt->pev->origin.y, pEnt->pev->origin.z,
		vecModel.x, vecModel.y, vecModel.z, pEnt->pev->scale,
		(int)pEnt->pev->body, (int)pEnt->pev->skin, (int)pEnt->pev->sequence );
}

// Test-only use/fire: switch the selected player to <weapon> and run its
// PrimaryAttack, so a headless run can prove a class/team-forbidden weapon
// actually fires (build-8 sgtc_rea_debug acceptance).
static void cmd_use( const char *classname, int index )
{
	if( !classname || !*classname ) { test_out( "SGTCTEST {\"cmd\":\"use\",\"error\":\"missing classname\"}" ); return; }
	CBasePlayer *pPlayer = NULL;
	for( int i = ( index > 0 ? index : 1 ); i <= gpGlobals->maxClients && !pPlayer; ++i )
	{
		CBaseEntity *plr = UTIL_PlayerByIndex( i );
		if( !plr || !plr->IsPlayer() ) continue;
		CBasePlayer *p = (CBasePlayer *)plr;
		if( index > 0 && i != index ) continue;
		if( p->IsAlive() && !p->IsObserver() ) pPlayer = p;
	}
	if( !pPlayer ) { test_out( "SGTCTEST {\"cmd\":\"use\",\"error\":\"no player\"}" ); return; }
	CBasePlayerItem *pItem = NULL;
	for( int s = 0; s < MAX_ITEM_TYPES && !pItem; ++s )
	{
		for( CBasePlayerItem *it = pPlayer->m_rgpPlayerItems[s]; it; it = it->m_pNext )
		{
			if( !strcmp( STRING( it->pev->classname ), classname ) || !strcmp( STRING( it->pev->classname ), classname + 7 ) )
			{ pItem = it; break; }
		}
	}
	if( !pItem ) { test_out( "SGTCTEST {\"cmd\":\"use\",\"player\":\"%s\",\"weapon\":\"%s\",\"error\":\"not carried\"}", STRING( pPlayer->pev->netname ), classname ); return; }
	const float flBefore = pPlayer->m_flNextAttack;
	pPlayer->SwitchWeapon( pItem );
	if( pPlayer->m_pActiveItem ) ((CBasePlayerWeapon *)pPlayer->m_pActiveItem)->PrimaryAttack();
	test_out( "SGTCTEST {\"cmd\":\"use\",\"player\":\"%s\",\"weapon\":\"%s\",\"active\":\"%s\",\"next_attack_before\":%.3f,\"next_attack_after\":%.3f}",
		STRING( pPlayer->pev->netname ), classname,
		pPlayer->m_pActiveItem ? STRING( pPlayer->m_pActiveItem->pev->classname ) : "",
		flBefore, pPlayer->m_flNextAttack );
}

static CBaseEntity *SGTC_TestFindEntity( const char *pszName )
{
	if( !pszName || !*pszName )
		return NULL;
	CBaseEntity *pEnt = UTIL_FindEntityByTargetname( NULL, pszName );
	if( pEnt )
		return pEnt;
	return UTIL_FindEntityByClassname( NULL, pszName );
}

static void cmd_entinfo( const char *pszName )
{
	CBaseEntity *pEnt = SGTC_TestFindEntity( pszName );
	if( !pEnt )
	{
		test_out( "SGTCTEST {\"cmd\":\"entinfo\",\"name\":\"%s\",\"found\":false}", pszName ? pszName : "" );
		return;
	}
	const char *pszClass = STRING( pEnt->pev->classname );
	int iToggle = -1;
	float flMoveDist = 0.0f;
	float flActivated = 0.0f;
	if( pszClass && ( !strncmp( pszClass, "func_door", 9 ) || !strcmp( pszClass, "momentary_door" ) ||
		!strcmp( pszClass, "func_button" ) || !strcmp( pszClass, "func_rot_button" ) ) )
	{
		CBaseToggle *pToggle = (CBaseToggle *)pEnt;
		iToggle = (int)pToggle->m_toggle_state;
		flMoveDist = pToggle->m_flMoveDistance;
		flActivated = pToggle->m_flActivateFinished;
	}
	test_out( "SGTCTEST {\"cmd\":\"entinfo\",\"name\":\"%s\",\"found\":true,\"class\":\"%s\","
		"\"targetname\":\"%s\",\"target\":\"%s\",\"origin\":[%.1f,%.1f,%.1f],\"velocity\":[%.1f,%.1f,%.1f],"
		"\"absmin\":[%.1f,%.1f,%.1f],\"absmax\":[%.1f,%.1f,%.1f],"
		"\"spawnflags\":%d,\"solid\":%d,\"movetype\":%d,\"effects\":%d,\"toggle\":%d,\"moveDist\":%.1f,\"activated\":%.2f}",
		pszName ? pszName : "", pszClass,
		pEnt->pev->targetname ? STRING( pEnt->pev->targetname ) : "",
		pEnt->pev->target ? STRING( pEnt->pev->target ) : "",
		pEnt->pev->origin.x, pEnt->pev->origin.y, pEnt->pev->origin.z,
		pEnt->pev->velocity.x, pEnt->pev->velocity.y, pEnt->pev->velocity.z,
		pEnt->pev->absmin.x, pEnt->pev->absmin.y, pEnt->pev->absmin.z,
		pEnt->pev->absmax.x, pEnt->pev->absmax.y, pEnt->pev->absmax.z,
		(int)pEnt->pev->spawnflags, (int)pEnt->pev->solid, (int)pEnt->pev->movetype, (int)pEnt->pev->effects,
		iToggle, flMoveDist, flActivated );
}

static void cmd_useent( const char *pszName )
{
	CBaseEntity *pEnt = SGTC_TestFindEntity( pszName );
	CBasePlayer *pPlayer = NULL;
	for( int i = 1; i <= gpGlobals->maxClients && !pPlayer; ++i )
	{
		CBaseEntity *p = UTIL_PlayerByIndex( i );
		if( p && p->IsPlayer() && p->IsAlive() )
			pPlayer = (CBasePlayer *)p;
	}
	if( !pEnt )
	{
		test_out( "SGTCTEST {\"cmd\":\"useent\",\"name\":\"%s\",\"found\":false}", pszName ? pszName : "" );
		return;
	}
	pEnt->Use( pPlayer, NULL, USE_TOGGLE, 0 );
	test_out( "SGTCTEST {\"cmd\":\"useent\",\"name\":\"%s\",\"class\":\"%s\",\"activated\":%d}",
		pszName ? pszName : "", STRING( pEnt->pev->classname ), pPlayer ? 1 : 0 );
}

static void cmd_touchent( const char *pszName, int iPlayerIndex )
{
	CBaseEntity *pEnt = SGTC_TestFindEntity( pszName );
	CBaseEntity *pPlayer = NULL;
	for( int i = ( iPlayerIndex > 0 ? iPlayerIndex : 1 ); i <= gpGlobals->maxClients && !pPlayer; ++i )
	{
		if( iPlayerIndex > 0 && i != iPlayerIndex )
			continue;
		CBaseEntity *p = UTIL_PlayerByIndex( i );
		if( p && p->IsPlayer() && p->IsAlive() )
			pPlayer = p;
	}
	if( !pEnt || !pPlayer )
	{
		test_out( "SGTCTEST {\"cmd\":\"touchent\",\"name\":\"%s\",\"found\":%d,\"player\":%d}",
			pszName ? pszName : "", pEnt ? 1 : 0, pPlayer ? 1 : 0 );
		return;
	}
	const Vector vecBefore = pPlayer->pev->origin;
	DispatchTouch( ENT( pEnt->pev ), ENT( pPlayer->pev ) );
	test_out( "SGTCTEST {\"cmd\":\"touchent\",\"name\":\"%s\",\"class\":\"%s\",\"player\":\"%s\","
		"\"before\":[%.1f,%.1f,%.1f],\"after\":[%.1f,%.1f,%.1f]}",
		pszName ? pszName : "", STRING( pEnt->pev->classname ), STRING( pPlayer->pev->netname ),
		vecBefore.x, vecBefore.y, vecBefore.z,
		pPlayer->pev->origin.x, pPlayer->pev->origin.y, pPlayer->pev->origin.z );
}

static CBasePlayer *SGTC_TestPlayerAt( int iIndex )
{
	for( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBaseEntity *p = UTIL_PlayerByIndex( i );
		if( p && p->IsPlayer() && ( iIndex <= 0 || i == iIndex ) )
			return (CBasePlayer *)p;
	}
	return NULL;
}

static void cmd_ruletest( void )
{
	CBasePlayer *pObs = SGTC_TestPlayerAt( 1 );
	CBasePlayer *pTgt = SGTC_TestPlayerAt( 2 );
	if( !pObs || !pTgt )
	{
		test_out( "SGTCTEST {\"cmd\":\"ruletest\",\"players\":0}" );
		return;
	}
	pTgt->m_bShieldWall = 1;
	pTgt->m_bShieldSpawned = 0;
	float fl0 = pTgt->pev->health;
	pTgt->TakeDamage( pObs->pev, pObs->pev, 25.0f, DMG_BULLET );
	float fl1 = pTgt->pev->health;
	pTgt->TakeDamage( pObs->pev, pObs->pev, 25.0f, DMG_CLUB );
	float fl2 = pTgt->pev->health;
	const int iVisCloaked = (int)SGTC_Bots_TargetVisible( pObs, pTgt );
	pTgt->m_bShieldSprint = 1;
	pTgt->m_bShieldWall = 0;
	const int iVisAfterCloak = (int)SGTC_Bots_TargetVisible( pObs, pTgt );
	pTgt->m_bShieldSprint = 0;
	sgtc_bot_weapon_info_t wi;
	SGTC_Bots_WeaponInfo( SGTC_WPN_COUTEAU, &wi );
	const int iKnifeBypass = wi.worksVsShield;
	SGTC_Bots_WeaponInfo( SGTC_WPN_M16, &wi );
	const int iM16Bypass = wi.worksVsShield;
	test_out( "SGTCTEST {\"cmd\":\"ruletest\",\"players\":1,"
		"\"hp_before\":%.0f,\"hp_after_bullet\":%.0f,\"hp_after_club\":%.0f,"
		"\"shield_before\":1,\"shield_after_bullet\":1,"
		"\"vis_normal\":%d,\"vis_cloaked\":%d,"
		"\"knife_bypass\":%d,\"m16_bypass\":%d,\"knife_vis_sees_invis\":%d}",
		fl0, fl1, fl2,
		iVisCloaked, iVisAfterCloak, iKnifeBypass, iM16Bypass,
		SGTC_Bots_WeaponInfo( SGTC_WPN_REETOU, &wi ) ? wi.seesInvis : 0 );
	pTgt->m_bShieldWall = 0;
	pTgt->m_bShieldSpawned = 0;
}


// --- rules-AI headless probes ---------------------------------------------
static void cmd_botstate( void )
{
	char buf[512];
	for( int i = 0; i < 32; i++ )
	{
		if( SGTC_Bots_RulesAI_Probe( i, buf, (int)sizeof( buf ) ) != 0 )
			continue;
		test_out( "SGTCTEST {\"cmd\":\"botstate\",\"state\":%s}", buf );
	}
}

static void cmd_forceshield( int iSlot )
{
	CBasePlayer *p = SGTC_TestPlayerAt( iSlot );
	if( !p )
	{
		test_out( "SGTCTEST {\"cmd\":\"forceshield\",\"player\":0}" );
		return;
	}
	p->m_bShieldWall = 1;
	p->m_bShieldSpawned = 0;
	test_out( "SGTCTEST {\"cmd\":\"forceshield\",\"player\":%d,\"shield\":1,\"name\":\"%s\"}",
		iSlot, STRING( p->pev->netname ) );
}

static void cmd_forceinvis( int iSlot )
{
	CBasePlayer *p = SGTC_TestPlayerAt( iSlot );
	if( !p )
	{
		test_out( "SGTCTEST {\"cmd\":\"forceinvis\",\"player\":0}" );
		return;
	}
	// The rules API models the cloak as m_bShieldSprint (invis weapon / sprint
	// shield share the flag); reveal age starts at never.
	p->m_bShieldSprint = 1;
	p->m_flSgtcInvisReveal = 0.0f;
	CBasePlayer *pObs = SGTC_TestPlayerAt( 1 );
	int vis = -1;
	if( pObs && pObs != p )
		vis = (int)SGTC_Bots_TargetVisible( pObs, p );
	test_out( "SGTCTEST {\"cmd\":\"forceinvis\",\"player\":%d,\"vis\":%d,\"name\":\"%s\"}",
		iSlot, vis, STRING( p->pev->netname ) );
}

static void cmd_clearforce( int iSlot )
{
	CBasePlayer *p = SGTC_TestPlayerAt( iSlot );
	if( !p )
		return;
	p->m_bShieldWall = 0;
	p->m_bShieldSpawned = 0;
	p->m_bShieldSprint = 0;
	p->m_flSgtcInvisReveal = 0.0f;
	p->m_bSgtcCloakTick = 0;
	test_out( "SGTCTEST {\"cmd\":\"clearforce\",\"player\":%d}", iSlot );
}

static void cmd_set( const char *name, const char *value )
{
	if( !name || !*name ) { test_out( "SGTCTEST {\"cmd\":\"set\",\"error\":\"missing name\"}" ); return; }
	CVAR_SET_STRING( name, value ? value : "" );
	test_out( "SGTCTEST {\"cmd\":\"set\",\"name\":\"%s\",\"value\":\"%s\"}", name, value ? value : "" );
}

static void run_line( char *line )
{
	// Trim.
	char *p = line;
	while( *p == ' ' || *p == '\t' ) ++p;
	char *end = p + strlen( p );
	while( end > p && ( end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n' ) ) *--end = 0;
	if( !*p ) return;

	char *argv[8];
	int argc = 0;
	char *save = p;
	for( char *t = next_tok( &save, " \t" ); t && argc < 8; t = next_tok( &save, " \t" ) )
		argv[argc++] = t;
	if( argc == 0 ) return;

	if( !strcmp( argv[0], "help" ) ) cmd_help();
	else if( !strcmp( argv[0], "info" ) ) cmd_info();
	else if( !strcmp( argv[0], "map" ) ) cmd_info();
	else if( !strcmp( argv[0], "entities" ) ) cmd_entities( argc > 1 ? atoi( argv[1] ) : 64 );
	else if( !strcmp( argv[0], "entity" ) ) cmd_entity( argc > 1 ? argv[1] : "" );
	else if( !strcmp( argv[0], "monsters" ) ) cmd_monsters( argc > 1 ? atoi( argv[1] ) : 64 );
	else if( !strcmp( argv[0], "players" ) ) cmd_players();
	else if( !strcmp( argv[0], "rules" ) ) cmd_rules();
	else if( !strcmp( argv[0], "cvar" ) ) cmd_cvar( argc > 1 ? argv[1] : "" );
	else if( !strcmp( argv[0], "set" ) ) cmd_set( argc > 1 ? argv[1] : "", argc > 2 ? argv[2] : "" );
	else if( !strcmp( argv[0], "echo" ) ) test_out( "SGTCTEST {\"cmd\":\"echo\",\"value\":\"%s\"}", argc > 1 ? argv[1] : "" );
	else if( !strcmp( argv[0], "unarmed" ) ) cmd_unarmed( argc > 1 ? atoi( argv[1] ) : 2, argc > 2 ? atoi( argv[2] ) : 1 );
	else if( !strcmp( argv[0], "tp" ) ) cmd_tp( argc > 1 ? argv[1] : "0", argc > 2 ? argv[2] : "0", argc > 3 ? argv[3] : "0", argc > 4 ? argv[4] : "", argc > 5 ? argv[5] : "" );
	else if( !strcmp( argv[0], "spawn" ) ) cmd_spawn( argv[1] ? argv[1] : "", argc > 2 ? (float)atof( argv[2] ) : 120.0f );
	else if( !strcmp( argv[0], "spawnat" ) ) cmd_spawnat( argv[1] ? argv[1] : "", argv[2] ? argv[2] : "0", argv[3] ? argv[3] : "0", argv[4] ? argv[4] : "0" );
	else if( !strcmp( argv[0], "use" ) ) cmd_use( argv[1] ? argv[1] : "", argc > 2 ? atoi( argv[2] ) : 0 );
	else if( !strcmp( argv[0], "entinfo" ) ) cmd_entinfo( argv[1] ? argv[1] : "" );
	else if( !strcmp( argv[0], "useent" ) ) cmd_useent( argv[1] ? argv[1] : "" );
	else if( !strcmp( argv[0], "touchent" ) ) cmd_touchent( argv[1] ? argv[1] : "", argc > 2 ? atoi( argv[2] ) : 0 );
	else if( !strcmp( argv[0], "touchdest" ) ) cmd_touchdest( argv[1] ? argv[1] : "", argc > 2 ? atoi( argv[2] ) : 0 );
	else if( !strcmp( argv[0], "ruletest" ) ) cmd_ruletest();
	else if( !strcmp( argv[0], "mechlist" ) ) cmd_mechlist();
	else if( !strcmp( argv[0], "bugtest" ) ) cmd_bugtest();
	else if( !strcmp( argv[0], "teamtest" ) ) cmd_teamtest();
	else if( !strcmp( argv[0], "shieldtest" ) ) cmd_shieldtest();
	else if( !strcmp( argv[0], "collidetest" ) ) cmd_collidetest();
	else if( !strcmp( argv[0], "c4firetest" ) ) cmd_c4firetest();
	else if( !strcmp( argv[0], "c4count" ) ) cmd_c4count();
	else if( !strcmp( argv[0], "hazardat" ) ) cmd_hazardat( argc > 1 ? (float)atof( argv[1] ) : 0.0f, argc > 2 ? (float)atof( argv[2] ) : 0.0f, argc > 3 ? (float)atof( argv[3] ) : 0.0f );
	else if( !strcmp( argv[0], "useentdest" ) ) cmd_useentdest( argv[1] ? argv[1] : "" );
	else if( !strcmp( argv[0], "quit" ) ) { test_out( "SGTCTEST {\"cmd\":\"quit\"}" ); SERVER_COMMAND( "quit\n" ); }
	else test_out( "SGTCTEST {\"cmd\":\"unknown\",\"line\":\"%s\"}", p );
}

SGTC_TEST_EXPORT void SGTC_TestCommand( const char *line )
{
	if( !line ) return;
	char buf[1024];
	snprintf( buf, sizeof( buf ), "%s", line );
	// Allow ; separated commands in one line.
	char *save = buf;
	for( char *part = next_tok( &save, ";" ); part; part = next_tok( &save, ";" ) )
		run_line( part );
}

void SGTC_TestInit( void )
{
	SGTC_RegisterOrAdoptCvar( &sgtc_test_enable );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_script );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_quit );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_watch );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_probe );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_probe_interval );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_timed_script );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_timed_delay );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_timed2_script );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_timed2_delay );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_timed3_script );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_autotouch );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_autotouch_start );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_autotouch_end );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_timed3_delay );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_repeat_script );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_repeat_delay );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_repeat_interval );
	SGTC_RegisterOrAdoptCvar( &sgtc_test_repeat_count );
}

void SGTC_TestActivate( void )
{
	if( !sgtc_test_enable.value ) return;
	const char *script = sgtc_test_script.string;
	if( !script || !*script ) return;
	test_out( "SGTCTEST {\"cmd\":\"script\",\"state\":\"start\"}" );
	SGTC_TestCommand( script );
	test_out( "SGTCTEST {\"cmd\":\"script\",\"state\":\"done\"}" );
	if( sgtc_test_quit.value ) SERVER_COMMAND( "quit\n" );
}

void SGTC_TestClientCommand( void *pEntity )
{
	// Rebuild the command line from CMD_ARGV so the client can type e.g.
	//   sgtc_test entities 20
	char line[1024] = {0};
	for( int i = 1; i < CMD_ARGC(); ++i )
	{
		if( i > 1 ) strncat( line, " ", sizeof( line ) - strlen( line ) - 1 );
		strncat( line, CMD_ARGV( i ), sizeof( line ) - strlen( line ) - 1 );
	}
	SGTC_TestCommand( line );
	CLIENT_PRINTF( (edict_t *)pEntity, print_console, UTIL_VarArgs( "sgtc_test: %s\n", line ) );
}

