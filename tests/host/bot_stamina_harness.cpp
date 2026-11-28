// Host harness for the bots' stamina rule (dlls/sgtc_bots_stamina.h).
//
//   g++ -std=c++11 -Wall -Wextra -I<repo>/dlls <repo>/tests/host/bot_stamina_harness.cpp -o bot_stamina_harness
//   ./bot_stamina_harness
//
// Besides the single-step decisions it simulates the game's own stamina
// arithmetic (dlls/sgtc_player.cpp: -10 per half second of movement, +4 or +5
// back per half second, maxspeed 10 below 20) to check that a bot following
// the rule keeps moving and a bot that ignores it stays pinned.

#include <stdio.h>

#include "sgtc_bots_stamina.h"

static int s_checks;
static int s_failures;

static void check( const char *name, int ok, const char *detail = "" )
{
	s_checks++;
	if( !ok )
		s_failures++;
	printf( "CASE %-44s %s %s\n", name, ok ? "PASS" : "FAIL", detail );
}

// One half-second game tick. `moving` is whether the bot pushed its stick this
// tick (the drain needs a positive velocity sum; assume the bot heads that
// way, the worst case), and speed is what the clamp then allows.
static int tick( int stamina, int health, int moving )
{
	if( moving )
		stamina -= 10;
	if( stamina < 0 )
		stamina = 0;
	if( stamina <= health )
		stamina += SGTC_Stamina_RegenPerTick( moving, 0, 1, 0, 0, 0 );
	return stamina;
}

static int speed_for( int stamina )
{
	return stamina < SGTC_STAMINA_PINNED ? 10 : 250;
}

int main( void )
{
	char detail[128];

	// regen per tick, the game's formula
	check( "regen.standing_ground", SGTC_Stamina_RegenPerTick( 0, 0, 1, 0, 0, 0 ) == 4 );
	check( "regen.moving_ground", SGTC_Stamina_RegenPerTick( 1, 0, 1, 0, 0, 0 ) == 5 );
	check( "regen.moving_crouched", SGTC_Stamina_RegenPerTick( 1, 1, 1, 0, 0, 0 ) == 4 );
	check( "regen.airborne", SGTC_Stamina_RegenPerTick( 0, 0, 0, 0, 0, 0 ) == 2 );
	check( "regen.snag_cloak", SGTC_Stamina_RegenPerTick( 0, 0, 1, 1, 1, 0 ) == 10 );
	check( "regen.gas_latch", SGTC_Stamina_RegenPerTick( 0, 0, 1, 0, 0, 1 ) == -3 );

	// exit level
	check( "exit.full_health", SGTC_Stamina_ExitLevel( 100 ) == 70 );
	check( "exit.health_60", SGTC_Stamina_ExitLevel( 60 ) == 42 );
	check( "exit.health_50", SGTC_Stamina_ExitLevel( 50 ) == 36 );
	check( "exit.never_above_health", SGTC_Stamina_ExitLevel( 40 ) == 36 && SGTC_Stamina_ExitLevel( 30 ) == 30 );
	check( "exit.over_100", SGTC_Stamina_ExitLevel( 150 ) == 70 );

	// enter
	check( "enter.low_calm", SGTC_Stamina_Decide( 25, 100, 0, 0, 0 ) == 1 );
	check( "enter.exactly_threshold", SGTC_Stamina_Decide( 30, 100, 0, 0, 0 ) == 0 );
	check( "enter.high", SGTC_Stamina_Decide( 80, 100, 0, 0, 0 ) == 0 );
	check( "enter.not_while_engaged", SGTC_Stamina_Decide( 10, 100, 1, 0, 0 ) == 0 );
	check( "enter.not_on_ladder_or_water", SGTC_Stamina_Decide( 10, 100, 0, 1, 0 ) == 0 );
	check( "enter.not_when_badly_hurt", SGTC_Stamina_Decide( 10, 39, 0, 0, 0 ) == 0 );
	check( "enter.hurt_but_ok", SGTC_Stamina_Decide( 10, 40, 0, 0, 0 ) == 1 );

	// stay / leave
	check( "rest.keeps_resting_midway", SGTC_Stamina_Decide( 50, 100, 0, 0, 1 ) == 1 );
	check( "rest.leaves_at_exit", SGTC_Stamina_Decide( 70, 100, 0, 0, 1 ) == 0 );
	check( "rest.leaves_when_engaged_and_able", SGTC_Stamina_Decide( 30, 100, 1, 0, 1 ) == 0 );
	check( "rest.stays_when_engaged_but_pinned", SGTC_Stamina_Decide( 10, 100, 1, 0, 1 ) == 1 );
	check( "rest.leaves_on_ladder", SGTC_Stamina_Decide( 10, 100, 0, 1, 1 ) == 0 );
	check( "rest.leaves_when_hurt_badly", SGTC_Stamina_Decide( 10, 20, 0, 0, 1 ) == 0 );
	check( "rest.hysteresis_no_flapping", SGTC_Stamina_Decide( 31, 100, 0, 0, 1 ) == 1 );

	// Simulation: 5 minutes, calm, always wants to walk.
	{
		int stamina = 100, resting = 0, moving_ticks = 0, rest_ticks = 0, pinned_ticks = 0;
		const int total = 600;

		for( int t = 0; t < total; t++ )
		{
			resting = SGTC_Stamina_Decide( stamina, 100, 0, 0, resting );
			const int moving = !resting;
			if( speed_for( stamina ) == 10 )
				pinned_ticks++;
			moving_ticks += moving;
			rest_ticks += resting;
			stamina = tick( stamina, 100, moving );
		}

		snprintf( detail, sizeof( detail ), "moving=%d resting=%d pinned=%d of %d", moving_ticks, rest_ticks, pinned_ticks, total );
		check( "sim.rule_keeps_bot_moving", moving_ticks * 100 / total >= 35, detail );
		check( "sim.rule_avoids_the_clamp", pinned_ticks * 100 / total <= 3, detail );
		check( "sim.rule_actually_rests", rest_ticks > 0 );
	}

	// Same bot without the rule: pinned almost all the time.
	{
		int stamina = 100, pinned_ticks = 0;
		const int total = 600;

		for( int t = 0; t < total; t++ )
		{
			if( speed_for( stamina ) == 10 )
				pinned_ticks++;
			stamina = tick( stamina, 100, 1 );
		}

		snprintf( detail, sizeof( detail ), "pinned=%d of %d", pinned_ticks, total );
		check( "sim.without_rule_bot_is_pinned", pinned_ticks * 100 / total >= 90, detail );
	}

	// A wounded bot (health 60) still cycles and never gets stuck resting.
	{
		int stamina = 60, resting = 0, moving_ticks = 0;
		const int total = 600;

		for( int t = 0; t < total; t++ )
		{
			resting = SGTC_Stamina_Decide( stamina, 60, 0, 0, resting );
			moving_ticks += !resting;
			stamina = tick( stamina, 60, !resting );
		}

		snprintf( detail, sizeof( detail ), "moving=%d of %d", moving_ticks, total );
		check( "sim.health_60_keeps_moving", moving_ticks * 100 / total >= 20, detail );
	}

	// A bot at the lowest health where resting applies still gets out of rest.
	{
		int stamina = 0, resting = 1, ticks_to_leave = 0;

		while( resting && ticks_to_leave < 100 )
		{
			stamina = tick( stamina, 40, 0 );
			resting = SGTC_Stamina_Decide( stamina, 40, 0, 0, resting );
			ticks_to_leave++;
		}

		snprintf( detail, sizeof( detail ), "left rest after %d ticks", ticks_to_leave );
		check( "sim.health_40_leaves_rest", !resting && ticks_to_leave < 40, detail );
	}

	printf( "%d checks, %d failures\n", s_checks, s_failures );
	return s_failures ? 1 : 0;
}
