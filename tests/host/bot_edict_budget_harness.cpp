// Host harness for the pure bot edict-budget decision (#193).
//
// Compiled by tests/features/test_bot_edict_budget.py together with
// dlls/sgtc_bots_budget.cpp. Prints one `CASE <name> PASS/FAIL` line per case
// and exits nonzero on any failure, matching tests/host/director_core_harness.
//
// The Python driver also checks the production call sites in
// dlls/sgtc_bots.cpp and bots/jk_botti/bot.cpp: this file covers the decision
// arithmetic and the same command pre-check/loop model those call sites use.

#include <stdio.h>

#include "sgtc_bots_budget.h"

// Worst-case CP spawn with mp_freeguns > 1: 26 loop item grants + post-loop
// invis + team melee + fake client = 29 edicts (review PT193-R3).
#define FREEGUNS_SPAWN_COST 29
// A normal class kit: 12 kit items + melee + possible invis + fake client.
#define NORMAL_SPAWN_COST 15

// The freeguns reserve is sized from the weapon-slot count, not a magic
// literal; the normal reserve is one edict above the ~15-edict class kit.
static_assert( SGTC_BOT_EDICT_MARGIN_FREEGUNS == SGTC_NUM_WEAPON_SLOTS + 3,
	"freeguns bot edict margin must cover the CP mp_freeguns>1 loadout" );
static_assert( SGTC_BOT_EDICT_MARGIN_NORMAL == 16,
	"normal bot edict margin must leave a one-edict cushion over a 15-edict kit" );

static int s_failures;
static int s_checks;

// Model the production choice by loadout class: a 29-edict spawn is the CP
// freeguns grant, anything smaller is a normal class kit. Production picks the
// same two values from the live mp_freeguns cvar.
static int margin_for_cost( int per_bot )
{
	return ( per_bot >= FREEGUNS_SPAWN_COST ) ?
		SGTC_BOT_EDICT_MARGIN_FREEGUNS : SGTC_BOT_EDICT_MARGIN_NORMAL;
}

static void check( const char *name, int used, int max_edicts, int margin, int want )
{
	const int got = SGTC_Bots_EdictBudgetAllows( used, max_edicts, margin );
	const int ok = ( got == want );

	s_checks++;
	if( !ok )
		s_failures++;

	printf( "CASE %-26s %s used=%d max=%d margin=%d got=%d want=%d\n",
		name, ok ? "PASS" : "FAIL", used, max_edicts, margin, got, want );
}

static void check_spawn_margin( const char *name, int freeguns, int want )
{
	const int got = SGTC_Bots_EdictBudgetSpawnMargin( freeguns );
	const int ok = ( got == want );

	s_checks++;
	if( !ok )
		s_failures++;

	printf( "CASE %-26s %s freeguns=%d margin=%d want=%d\n",
		name, ok ? "PASS" : "FAIL", freeguns, got, want );
}

static void check_request( const char *name, int used, int max_edicts, int margin, int count, int want )
{
	const int got = SGTC_Bots_EdictBudgetAllowsRequest( used, max_edicts, margin, count );
	const int ok = ( got == want );

	s_checks++;
	if( !ok )
		s_failures++;

	printf( "CASE %-26s %s used=%d max=%d margin=%d count=%d got=%d want=%d\n",
		name, ok ? "PASS" : "FAIL", used, max_edicts, margin, count, got, want );
}

// Mirror of SGTC_Bots_CommandAdd's budget handling: the count-aware pre-check
// runs once, then every BotCreate calls the per-spawn gate. Returns how many
// bots the request would create and how many `spawn refused` lines it would
// log. `per_bot` is the entity delta of a successful spawn.
static void simulate_add( int used, int count, int per_bot, int *created, int *refusals )
{
	const int margin = margin_for_cost( per_bot );

	*created = 0;
	*refusals = 0;

	if( !SGTC_Bots_EdictBudgetAllowsRequest( used, 900, margin, count ) )
	{
		(*refusals)++;	// the single pre-check refusal line
		return;
	}

	for( int i = 0; i < count; i++ )
	{
		if( !SGTC_Bots_EdictBudgetAllows( used, 900, margin ) )
		{
			(*refusals)++;
			continue;
		}

		used += per_bot;
		(*created)++;
	}
}

static void check_request_result( const char *name, int used, int count,
	int per_bot, int want_created, int want_refusals )
{
	int created = 0, refusals = 0;

	simulate_add( used, count, per_bot, &created, &refusals );

	const int ok = ( created == want_created && refusals == want_refusals );
	s_checks++;
	if( !ok )
		s_failures++;

	printf( "CASE %-26s %s used=%d count=%d per_bot=%d created=%d want_created=%d refusals=%d want_refusals=%d\n",
		name, ok ? "PASS" : "FAIL", used, count, per_bot, created, want_created, refusals, want_refusals );
}

static void check_freeguns( const char *name, int used )
{
	const int allowed = SGTC_Bots_EdictBudgetAllows( used, 900,
		SGTC_BOT_EDICT_MARGIN_FREEGUNS );
	const int total = used + FREEGUNS_SPAWN_COST;
	// The guard must never allow a 29-edict spawn that crosses 900.
	const int safe = ( !allowed || total <= 900 );

	s_checks++;
	if( !safe )
		s_failures++;

	printf( "CASE %-26s %s allows(%d,900,%d)=%d spawn_cost=%d total=%d max=900 over=%d\n",
		name, safe ? "PASS" : "FAIL", used, SGTC_BOT_EDICT_MARGIN_FREEGUNS, allowed,
		FREEGUNS_SPAWN_COST, total, total > 900 ? 1 : 0 );
}

int main( void )
{
	const int freeguns = SGTC_BOT_EDICT_MARGIN_FREEGUNS;	// 31
	const int normal = SGTC_BOT_EDICT_MARGIN_NORMAL;	// 16

	// The loadout selector feeds both wrappers. 1 = mp_freeguns > 1.
	check_spawn_margin( "selector-freeguns", 1, freeguns );
	check_spawn_margin( "selector-normal", 0, normal );

	// Freeguns reserve (31): the threshold is 900 - 31 = 869, so used <= 868
	// allows and used >= 869 refuses. 883/29 is the PT193-R3 reproducer.
	check( "fg-plenty-of-room", 100, 900, freeguns, 1 );
	check( "fg-last-allowed-868", 868, 900, freeguns, 1 );
	check( "fg-at-margin-869", 869, 900, freeguns, 0 );
	check( "fg-baseline-882", 882, 900, freeguns, 0 );
	check( "fg-exactly-max", 900, 900, freeguns, 0 );
	check( "fg-over-max", 901, 900, freeguns, 0 );
	check( "fg-far-over-max", 4000, 900, freeguns, 0 );

	// Normal reserve (16): allows at 883 (883 + 15 = 898 <= 900) and refuses
	// from 884, one edict of cushion over the 15-edict kit.
	check( "kit-883-allowed", 883, 900, normal, 1 );
	check( "kit-884-refused", 884, 900, normal, 0 );
	check( "kit-885-refused", 885, 900, normal, 0 );
	check( "kit-baseline-882", 882, 900, normal, 1 );
	check( "kit-plenty-of-room", 100, 900, normal, 1 );

	// Garbage readings must refuse, never allow.
	check( "negative-used", -1, 900, freeguns, 0 );
	check( "zero-max", 100, 0, freeguns, 0 );
	check( "negative-max", 100, -900, freeguns, 0 );
	check( "negative-margin", 100, 900, -1, 0 );
	check( "margin-equals-max", 0, SGTC_NUM_WEAPON_SLOTS + 3, freeguns, 0 );
	check( "margin-over-max", 0, SGTC_NUM_WEAPON_SLOTS + 3,
		SGTC_NUM_WEAPON_SLOTS + 4, 0 );

	// margin 0 means "refuse only at the hard limit".
	check( "zero-margin-below", 899, 900, 0, 1 );
	check( "zero-margin-at-max", 900, 900, 0, 0 );

	// No overflow on extreme but valid ints.
	check( "int-max-pool", 2147483646, 2147483647, 1, 0 );
	check( "int-max-pool-room", 2147483645, 2147483647, 1, 1 );

	// Count-aware request reservation for freeguns `sgtc_bot_add`: one edict
	// per bot on top of the 31-edict margin. 882/3 is still refused by the
	// increased margin even before the count reservation.
	check_request( "fg-request-868x1", 868, 900, freeguns, 1, 1 );
	check_request( "fg-request-868x2", 868, 900, freeguns, 2, 0 );
	check_request( "fg-request-867x2", 867, 900, freeguns, 2, 1 );
	check_request( "fg-request-867x3", 867, 900, freeguns, 3, 0 );
	check_request( "fg-request-869x1", 869, 900, freeguns, 1, 0 );
	check_request( "request-zero-count", 100, 900, freeguns, 0, 0 );
	check_request( "request-negative-count", 100, 900, freeguns, -1, 0 );
	check_request( "request-margin-equals-max", 0,
		SGTC_NUM_WEAPON_SLOTS + 3, freeguns, 1, 0 );

	// Normal-kit count reservation uses the same 16-edict margin.
	check_request( "kit-request-883x1", 883, 900, normal, 1, 1 );
	check_request( "kit-request-884x1", 884, 900, normal, 1, 0 );
	check_request( "kit-request-883x2", 883, 900, normal, 2, 0 );

	// End-to-end model of the command: pre-check once, then the per-spawn gate.
	// 882/3 is refused once before any bot exists.
	check_request_result( "add-882x3", 882, 3, 2, 0, 1 );
	// A request can pass the count-slot floor and still be refused mid-loop
	// because one bot costs >= 2 edicts. This residual is documented.
	check_request_result( "add-882x2-mincost", 882, 2, 2, 1, 1 );
	check_request_result( "add-880x3-mincost", 880, 3, 2, 2, 1 );
	// Requests that fit keep their existing spawn behaviour.
	check_request_result( "add-868x1", 868, 1, 2, 1, 0 );

	// Normal class kit (~15 edicts): the 883 case is the over-block regression
	// this rework fixes. 867/868/883 must spawn; 884/885 are refused by the
	// 16-edict reserve.
	check_request_result( "add-867x1-kit15", 867, 1, NORMAL_SPAWN_COST, 1, 0 );
	check_request_result( "add-868x1-kit15", 868, 1, NORMAL_SPAWN_COST, 1, 0 );
	check_request_result( "add-883x1-kit15", 883, 1, NORMAL_SPAWN_COST, 1, 0 );
	check_request_result( "add-884x1-kit15", 884, 1, NORMAL_SPAWN_COST, 0, 1 );
	check_request_result( "add-885x1-kit15", 885, 1, NORMAL_SPAWN_COST, 0, 1 );
	check_request_result( "add-867x2-kit15", 867, 2, NORMAL_SPAWN_COST, 2, 0 );
	check_request_result( "add-866x3-kit15", 866, 3, NORMAL_SPAWN_COST, 2, 1 );

	// CP freeguns worst case: 29 edicts must never be allowed to cross 900.
	check_freeguns( "freeguns-883", 883 );
	check_freeguns( "freeguns-882-baseline", 882 );
	check_freeguns( "freeguns-868-last", 868 );
	check_freeguns( "freeguns-869-refuse", 869 );
	check_freeguns( "freeguns-900-refuse", 900 );
	check_request_result( "add-883x1-freeguns", 883, 1, FREEGUNS_SPAWN_COST, 0, 1 );
	check_request_result( "add-868x1-freeguns", 868, 1, FREEGUNS_SPAWN_COST, 1, 0 );
	check_request_result( "add-868x2-freeguns", 868, 2, FREEGUNS_SPAWN_COST, 0, 1 );
	check_request_result( "add-867x2-freeguns", 867, 2, FREEGUNS_SPAWN_COST, 1, 1 );

	// Sweep every boundary value: no allowed freeguns spawn (cost 29) may
	// cross 900, and no allowed normal spawn (cost 15) may cross it either.
	{
		int unsafe = 0;

		for( int used = 800; used <= 900; used++ )
		{
			if( SGTC_Bots_EdictBudgetAllows( used, 900, freeguns ) &&
				used + FREEGUNS_SPAWN_COST > 900 )
				unsafe++;
			if( SGTC_Bots_EdictBudgetAllows( used, 900, normal ) &&
				used + NORMAL_SPAWN_COST > 900 )
				unsafe++;
		}

		s_checks++;
		if( unsafe )
			s_failures++;

		printf( "CASE %-26s %s used=800..900 freeguns_cost=%d normal_cost=%d unsafe_allows=%d want=0\n",
			"loadout-sweep", unsafe ? "FAIL" : "PASS",
			FREEGUNS_SPAWN_COST, NORMAL_SPAWN_COST, unsafe );
	}

	printf( "SUMMARY %s %d checks %d failed margins normal=%d freeguns=%d "
		"spawn_costs normal=%d freeguns=%d\n",
		s_failures == 0 ? "PASS" : "FAIL", s_checks, s_failures,
		normal, freeguns, NORMAL_SPAWN_COST, FREEGUNS_SPAWN_COST );
	return s_failures == 0 ? 0 : 1;
}
