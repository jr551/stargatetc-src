// Host harness for the pure fire-patch cap decision.
//
// Compiled together with dlls/sgtc_firepatch.cpp on the host (no engine):
//   c++ -std=c++11 -O1 -g -Idlls -o fire_patch_cap_harness
//       dlls/sgtc_firepatch.cpp tests/host/fire_patch_cap_harness.cpp
// Prints one `CASE <name> PASS/FAIL` line per case and exits nonzero on any
// failure, matching tests/host/bot_edict_budget_harness.cpp.
//
// The production wrapper (dlls/sgtc_c4fire.cpp, SGTC_FirePatch_Spawn) counts
// live `sgtc_c4_fire` edicts and passes NUMBER_OF_ENTITIES() and
// gpGlobals->maxEntities; this file covers the decision arithmetic and the
// burst model the `firepatchburst` test command exercises (201 requests).

#include <stdio.h>

#include "sgtc_firepatch.h"

static_assert( SGTC_FIREPATCH_MAX_ACTIVE == 64,
	"fire patch active cap must stay at 64" );
static_assert( SGTC_FIREPATCH_EDICT_RESERVE == 8,
	"fire patch edict reserve must stay at 8" );

static int s_failures;
static int s_checks;

static void check( const char *name, int active, int used, int max_edicts, int want )
{
	const int got = SGTC_FirePatch_Allows( active, used, max_edicts );
	const int ok = ( got == want );

	s_checks++;
	if( !ok )
		s_failures++;

	printf( "CASE %-26s %s active=%d used=%d max=%d got=%d want=%d\n",
		name, ok ? "PASS" : "FAIL", active, used, max_edicts, got, want );
}

// Mirror of SGTC_FirePatch_Spawn's burst behaviour: every allowed spawn
// increments both the active fire-patch count and the edict total, every
// refused spawn creates nothing. Returns how many of `requests` were created.
static void simulate_burst( const char *name, int active, int used, int max_edicts,
	int requests, int want_created )
{
	int created = 0;

	for( int i = 0; i < requests; i++ )
	{
		if( !SGTC_FirePatch_Allows( active, used, max_edicts ) )
			continue;

		active++;
		used++;
		created++;
	}

	const int ok = ( created == want_created );

	s_checks++;
	if( !ok )
		s_failures++;

	printf( "CASE %-26s %s requests=%d created=%d want_created=%d active_end=%d used_end=%d max=%d\n",
		name, ok ? "PASS" : "FAIL", requests, created, want_created, active, used, max_edicts );
}

int main( void )
{
	// Active-count cap (64): plenty of edicts, so only the count decides.
	check( "active-empty", 0, 100, 900, 1 );
	check( "active-last-allowed-63", 63, 100, 900, 1 );
	check( "active-at-cap-64", 64, 100, 900, 0 );
	check( "active-over-cap", 65, 100, 900, 0 );
	check( "active-far-over-cap", 1000, 100, 900, 0 );

	// Edict reserve (8) at the 900-edict engine pool: used <= 891 allows,
	// used >= 892 (= 900 - 8) refuses.
	check( "edict-plenty-of-room", 0, 100, 900, 1 );
	check( "edict-last-allowed-891", 0, 891, 900, 1 );
	check( "edict-at-reserve-892", 0, 892, 900, 0 );
	check( "edict-exactly-max", 0, 900, 900, 0 );
	check( "edict-over-max", 0, 901, 900, 0 );
	check( "edict-far-over-max", 0, 4000, 900, 0 );

	// Both limits together: either one refuses on its own.
	check( "both-at-limit", 64, 892, 900, 0 );
	check( "both-just-inside", 63, 891, 900, 1 );
	check( "active-ok-edict-full", 10, 892, 900, 0 );
	check( "edict-ok-active-full", 64, 10, 900, 0 );

	// Garbage readings must refuse, never allow.
	check( "negative-active", -1, 100, 900, 0 );
	check( "negative-used", 0, -1, 900, 0 );
	check( "zero-max", 0, 0, 0, 0 );
	check( "negative-max", 0, 0, -900, 0 );
	check( "max-at-reserve", 0, 0, SGTC_FIREPATCH_EDICT_RESERVE, 0 );
	check( "max-below-reserve", 0, 0, SGTC_FIREPATCH_EDICT_RESERVE - 1, 0 );

	// Smallest usable pool: one edict above the reserve.
	check( "tiny-pool-first", 0, 0, SGTC_FIREPATCH_EDICT_RESERVE + 1, 1 );
	check( "tiny-pool-full", 0, 1, SGTC_FIREPATCH_EDICT_RESERVE + 1, 0 );

	// No overflow on extreme but valid ints.
	check( "int-max-pool-room", 0, 2147483638, 2147483647, 1 );
	check( "int-max-pool-at-reserve", 0, 2147483639, 2147483647, 0 );

	// The `firepatchburst` repro: 201 requests from empty on a 900 pool
	// create exactly the 64-patch cap and refuse the remaining 137.
	simulate_burst( "burst-201-from-empty", 0, 100, 900, 201, 64 );
	// Already at the cap: a burst creates nothing.
	simulate_burst( "burst-at-cap", 64, 164, 900, 201, 0 );
	// Near-full pool: the edict reserve stops the burst before the
	// active cap is reached (890 and 891 allowed, 892 refused).
	simulate_burst( "burst-near-full-pool", 0, 890, 900, 201, 2 );
	// A single request still succeeds with room on both limits.
	simulate_burst( "single-with-room", 10, 100, 900, 1, 1 );

	// Sweep: no allowed spawn may cross either limit.
	{
		int unsafe = 0;

		for( int active = 0; active <= 80; active++ )
			for( int used = 880; used <= 910; used++ )
				if( SGTC_FirePatch_Allows( active, used, 900 ) &&
					( active >= SGTC_FIREPATCH_MAX_ACTIVE ||
					  used >= 900 - SGTC_FIREPATCH_EDICT_RESERVE ) )
					unsafe++;

		s_checks++;
		if( unsafe )
			s_failures++;

		printf( "CASE %-26s %s active=0..80 used=880..910 max=900 unsafe_allows=%d want=0\n",
			"limit-sweep", unsafe ? "FAIL" : "PASS", unsafe );
	}

	printf( "SUMMARY %s %d checks %d failed max_active=%d reserve=%d\n",
		s_failures == 0 ? "PASS" : "FAIL", s_checks, s_failures,
		SGTC_FIREPATCH_MAX_ACTIVE, SGTC_FIREPATCH_EDICT_RESERVE );
	return s_failures == 0 ? 0 : 1;
}
