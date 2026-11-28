// Host harness for the traffic heat map the bots learn from people (bots/jk_botti/bot_learn_core.cpp).
//
//   g++ -std=c++11 -Wall -Wextra -I<repo>/bots/jk_botti <repo>/tests/host/bot_learn_harness.cpp <repo>/bots/jk_botti/bot_learn_core.cpp -o bot_learn_harness
//   ./bot_learn_harness
//
// Prints one `CASE <name> PASS/FAIL` line per check and exits nonzero on any failure.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bot_learn_core.h"

static int s_checks;
static int s_failures;

static void check( const char *name, int ok, const char *detail = "" )
{
	s_checks++;
	if( !ok )
		s_failures++;
	printf( "CASE %-46s %s %s\n", name, ok ? "PASS" : "FAIL", detail );
}

// small deterministic generator
static unsigned int s_rng = 12345u;

static float rnd01( void )
{
	s_rng = s_rng * 1664525u + 1013904223u;
	return (float)( ( s_rng >> 8 ) & 0xffffff ) / 16777216.0f;
}

static heat_map_t g_heat, g_heat2;

static void test_heat( void )
{
	char detail[160];

	Heat_Clear( &g_heat );
	check( "heat.empty", g_heat.count == 0 && Heat_At( &g_heat, 0, 0, 0 ) == 0.0f );

	Heat_Add( &g_heat, 10.0f, 10.0f, 10.0f, 2.0f );
	Heat_Add( &g_heat, 20.0f, 30.0f, 5.0f, 3.0f );     // same cell
	check( "heat.same_cell_accumulates", g_heat.count == 1 && fabsf( Heat_At( &g_heat, 0, 0, 0 ) - 5.0f ) < 1e-4f );
	check( "heat.dirty_after_add", g_heat.dirty == 1 );

	// negative coordinates floor into their own cell
	Heat_Add( &g_heat, -1.0f, -1.0f, -1.0f, 1.0f );
	check( "heat.negative_cell_is_separate", g_heat.count == 2 );

	// neighbours bleed a quarter
	Heat_Clear( &g_heat );
	Heat_Add( &g_heat, 0.0f, 0.0f, 0.0f, 8.0f );
	check( "heat.neighbour_bleeds_quarter", fabsf( Heat_At( &g_heat, HEAT_CELL_SIZE + 1.0f, 0.0f, 0.0f ) - 2.0f ) < 1e-4f );
	check( "heat.diagonal_does_not_bleed", Heat_At( &g_heat, HEAT_CELL_SIZE + 1.0f, HEAT_CELL_SIZE + 1.0f, 0.0f ) == 0.0f );
	check( "heat.two_away_zero", Heat_At( &g_heat, 2.0f * HEAT_CELL_SIZE + 1.0f, 0.0f, 0.0f ) == 0.0f );

	// rejects junk
	Heat_Clear( &g_heat );
	Heat_Add( &g_heat, 0, 0, 0, 0.0f );
	Heat_Add( &g_heat, 0, 0, 0, -3.0f );
	Heat_Add( &g_heat, 0, 0, 0, NAN );
	Heat_Add( &g_heat, NAN, 0, 0, 1.0f );
	Heat_Add( &g_heat, 0, INFINITY, 0, 1.0f );
	Heat_Add( &g_heat, 1.0e9f, 0, 0, 1.0f );
	check( "heat.junk_rejected", g_heat.count == 0 && g_heat.dirty == 0 );

	// cap per cell
	Heat_Add( &g_heat, 0, 0, 0, 4000.0f );
	Heat_Add( &g_heat, 0, 0, 0, 4000.0f );
	check( "heat.cell_capped", Heat_At( &g_heat, 0, 0, 0 ) <= HEAT_CELL_CAP + 1e-3f );

	// fill to capacity, every cell findable
	Heat_Clear( &g_heat );
	for( int i = 0; i < HEAT_MAX_CELLS; i++ )
		Heat_Add( &g_heat, (float)( i % 48 ) * HEAT_CELL_SIZE, (float)( i / 48 ) * HEAT_CELL_SIZE, 0.0f, 1.0f + (float)i );
	int findable = 0;
	for( int i = 0; i < HEAT_MAX_CELLS; i++ )
		if( Heat_At( &g_heat, (float)( i % 48 ) * HEAT_CELL_SIZE, (float)( i / 48 ) * HEAT_CELL_SIZE, 0.0f ) >= 1.0f + (float)i )
			findable++;
	snprintf( detail, sizeof( detail ), "%d of %d findable", findable, HEAT_MAX_CELLS );
	check( "heat.full_map_all_findable", g_heat.count == HEAT_MAX_CELLS && findable == HEAT_MAX_CELLS, detail );

	// a full map evicts its weakest for a stronger newcomer
	Heat_Add( &g_heat, 5000.0f * HEAT_CELL_SIZE, 0.0f, 0.0f, 10000.0f );
	check( "heat.full_evicts_weakest", g_heat.count == HEAT_MAX_CELLS &&
		Heat_At( &g_heat, 5000.0f * HEAT_CELL_SIZE, 0.0f, 0.0f ) >= 4999.0f &&
		// the weakest cell (0,0,0) is gone; only its neighbours' quarter share remains
		fabsf( Heat_At( &g_heat, 0.0f, 0.0f, 0.0f ) - 0.25f * ( 2.0f + 49.0f ) ) < 1e-3f );
	// ... and still finds everything else
	int still = 0;
	for( int i = 1; i < HEAT_MAX_CELLS; i++ )
		if( Heat_At( &g_heat, (float)( i % 48 ) * HEAT_CELL_SIZE, (float)( i / 48 ) * HEAT_CELL_SIZE, 0.0f ) >= 1.0f + (float)i )
			still++;
	check( "heat.full_keeps_the_rest", still >= HEAT_MAX_CELLS - 2 );

	// a newcomer weaker than the weakest is dropped
	const int before = g_heat.count;
	Heat_Add( &g_heat, 6000.0f * HEAT_CELL_SIZE, 0.0f, 0.0f, 0.5f );
	check( "heat.full_drops_weak_newcomer", g_heat.count == before && Heat_At( &g_heat, 6000.0f * HEAT_CELL_SIZE, 0, 0 ) == 0.0f );

	// decay
	Heat_Clear( &g_heat );
	Heat_Add( &g_heat, 0, 0, 0, 10.0f );
	Heat_Add( &g_heat, 1000, 0, 0, 0.5f );
	Heat_Decay( &g_heat, 0.5f, 1.0f );
	check( "heat.decay_drops_below_floor", g_heat.count == 1 && fabsf( Heat_At( &g_heat, 0, 0, 0 ) - 5.0f ) < 1e-4f );
	check( "heat.decay_table_consistent", Heat_At( &g_heat, 1000, 0, 0 ) == 0.0f );
	Heat_Add( &g_heat, 0, 0, 0, 1.0f );
	check( "heat.add_after_decay", g_heat.count == 1 && fabsf( Heat_At( &g_heat, 0, 0, 0 ) - 6.0f ) < 1e-4f );

	// serialize / parse roundtrip
	Heat_Clear( &g_heat );
	for( int i = 0; i < 300; i++ )
		Heat_Add( &g_heat, (float)( i * 97 % 3000 ) - 1500.0f, (float)( i * 31 % 2000 ) - 1000.0f, (float)( i % 5 ) * 100.0f, 1.0f + (float)( i % 17 ) );
	static char buf[200000];
	const int n = Heat_Serialize( &g_heat, buf, sizeof( buf ) );
	check( "heat.serialize_ok", n > 0 && strncmp( buf, "sgtc_heat 1\n", 12 ) == 0 );
	const int loaded = Heat_Parse( &g_heat2, buf );
	check( "heat.parse_count", loaded == g_heat.count );
	int same = 0;
	for( int i = 0; i < g_heat.count; i++ )
	{
		const heat_cell_t &c = g_heat.cells[i];
		const float a = Heat_At( &g_heat, ( (float)c.x + 0.5f ) * HEAT_CELL_SIZE, ( (float)c.y + 0.5f ) * HEAT_CELL_SIZE, ( (float)c.z + 0.5f ) * HEAT_CELL_SIZE );
		const float b = Heat_At( &g_heat2, ( (float)c.x + 0.5f ) * HEAT_CELL_SIZE, ( (float)c.y + 0.5f ) * HEAT_CELL_SIZE, ( (float)c.z + 0.5f ) * HEAT_CELL_SIZE );
		if( fabsf( a - b ) < 0.05f )
			same++;
	}
	check( "heat.roundtrip_same_weights", same == g_heat.count );
	check( "heat.parse_clears_dirty", g_heat2.dirty == 0 );
	check( "heat.serialize_too_small", Heat_Serialize( &g_heat, buf, 40 ) == -1 );
	check( "heat.serialize_tiny", Heat_Serialize( &g_heat, buf, 5 ) == -1 );

	// parse robustness
	check( "heat.parse_null", Heat_Parse( &g_heat2, NULL ) == -1 && g_heat2.count == 0 );
	check( "heat.parse_bad_header", Heat_Parse( &g_heat2, "nonsense\nc 1 2 3 4\n" ) == -1 && g_heat2.count == 0 );
	check( "heat.parse_wrong_version", Heat_Parse( &g_heat2, "sgtc_heat 2\nc 1 2 3 4\n" ) == -1 );
	check( "heat.parse_header_only", Heat_Parse( &g_heat2, "sgtc_heat 1" ) == 0 && Heat_Parse( &g_heat2, "sgtc_heat 1\n" ) == 0 );
	check( "heat.parse_no_trailing_newline", Heat_Parse( &g_heat2, "sgtc_heat 1\nc 1 2 3 4.5" ) == 1 );
	check( "heat.parse_skips_garbage", Heat_Parse( &g_heat2,
		"sgtc_heat 1\nhello\nc 1 2\nc a b c d\nc 1 2 3 nan\nc 1 2 3 -4\nc 999999999 0 0 1\nc 0 0 0 inf\nc 5 6 7 8\n\n\n" ) == 1 );
	check( "heat.parse_duplicates_once", Heat_Parse( &g_heat2, "sgtc_heat 1\nc 1 1 1 5\nc 1 1 1 7\n" ) == 1 &&
		fabsf( Heat_At( &g_heat2, 1.5f * HEAT_CELL_SIZE, 1.5f * HEAT_CELL_SIZE, 1.5f * HEAT_CELL_SIZE ) - 5.0f ) < 1e-4f );
	check( "heat.parse_clamps_weight", Heat_Parse( &g_heat2, "sgtc_heat 1\nc 1 1 1 9000\n" ) == 1 &&
		Heat_At( &g_heat2, 1.5f * HEAT_CELL_SIZE, 1.5f * HEAT_CELL_SIZE, 1.5f * HEAT_CELL_SIZE ) <= HEAT_CELL_CAP + 1e-3f );
	check( "heat.parse_rejects_silly_weight", Heat_Parse( &g_heat2, "sgtc_heat 1\nc 1 1 1 1e30\n" ) == 0 );

	// a file with more cells than the map can hold
	{
		static char big[400000];
		int o = snprintf( big, sizeof( big ), "sgtc_heat 1\n" );
		for( int i = 0; i < HEAT_MAX_CELLS + 500; i++ )
			o += snprintf( big + o, sizeof( big ) - (size_t)o, "c %d %d 0 2\n", i, i );
		check( "heat.parse_caps_cell_count", Heat_Parse( &g_heat2, big ) == HEAT_MAX_CELLS );
	}
}

static void test_pick( void )
{
	const float w[5] = { 0.0f, 3.0f, 0.0f, 1.0f, 0.0f };
	int counts[5] = { 0, 0, 0, 0, 0 };

	check( "pick.null", Heat_PickIndex( NULL, 5, 0.5f ) == -1 );
	check( "pick.empty", Heat_PickIndex( w, 0, 0.5f ) == -1 );
	check( "pick.negative_n", Heat_PickIndex( w, -3, 0.5f ) == -1 );
	{
		const float z[3] = { 0.0f, 0.0f, 0.0f };
		check( "pick.all_zero", Heat_PickIndex( z, 3, 0.5f ) == -1 );
		const float neg[3] = { -1.0f, -2.0f, 0.0f };
		check( "pick.all_negative", Heat_PickIndex( neg, 3, 0.5f ) == -1 );
		const float nan3[3] = { NAN, 0.0f, 0.0f };
		check( "pick.nan_weight", Heat_PickIndex( nan3, 3, 0.5f ) == -1 );
	}
	check( "pick.low_edge", Heat_PickIndex( w, 5, 0.0f ) == 1 );
	check( "pick.high_edge", Heat_PickIndex( w, 5, 0.99999f ) == 3 );
	check( "pick.rnd_one_is_safe", Heat_PickIndex( w, 5, 1.0f ) == 3 );
	check( "pick.rnd_negative_is_safe", Heat_PickIndex( w, 5, -1.0f ) == 1 );
	check( "pick.single", Heat_PickIndex( &w[3], 1, 0.7f ) == 0 );

	s_rng = 4242u;
	for( int i = 0; i < 20000; i++ )
		counts[Heat_PickIndex( w, 5, rnd01() )]++;
	char detail[120];
	snprintf( detail, sizeof( detail ), "idx1=%d idx3=%d", counts[1], counts[3] );
	check( "pick.never_picks_zero_weight", counts[0] == 0 && counts[2] == 0 && counts[4] == 0, detail );
	check( "pick.follows_weights", counts[1] > 14200 && counts[1] < 15800 && counts[3] > 4200 && counts[3] < 5800, detail );
}

int main( void )
{
	test_heat();
	test_pick();

	printf( "%d checks, %d failures\n", s_checks, s_failures );
	return s_failures ? 1 : 0;
}
