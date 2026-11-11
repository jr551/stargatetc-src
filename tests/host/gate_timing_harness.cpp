// tests/host/gate_timing_harness.cpp - pure assertions for the gate timing model.
//
// Compiles dlls/sgtc_gate_timing.h and dlls/sgtc_bots_gate.h with the host C++
// compiler (no engine, no DLL, no 32-bit toolchain) and drives the same state
// machine the server DLL ships. Every check prints a SIT line so
// tests/features/test_gate_timing.py can fail the feature on a single line.

#include "sgtc_gate_timing.h"
#include "sgtc_bots_gate.h"

#include <cmath>
#include <cstdio>

static int g_checks = 0;
static int g_failed = 0;

static void check( const char *name, bool ok, const char *detail )
{
	g_checks++;
	if( !ok )
		g_failed++;
	std::printf( "SIT\tgatedial:%s\t%s\t%s\n", name, ok ? "PASS" : "FAIL", detail );
}

static bool near( float a, float b )
{
	return std::fabs( a - b ) <= 0.001f;
}

static sgtc_bot_gate_ctx_t ctx( int at_entry, float distance, int can_dial )
{
	sgtc_bot_gate_ctx_t c;
	c.at_entry = at_entry;
	c.distance = distance;
	c.can_dial = can_dial;
	c.allow_enter = 1;
	return c;
}

int main( void )
{
	// A) A closed gate is not enterable and the bot's first move is to dial.
	{
		sgtc_gate_timing_t g;
		SgtcGateTiming_Reset( &g, 1, 100.0f );
		sgtc_bot_gate_ctx_t c = ctx( 1, 64.0f, 1 );
		check( "closed-near", !SgtcGateTiming_CanEnter( &g, 100.0f ),
			"closed gate must not accept entry" );
		check( "closed-decision", SgtcBots_GateDecision( &g, &c, 100.0f ) == SGTC_BOT_GATE_DIAL,
			"bot at a closed gate must dial" );
	}

	// B) Dial start: accepted once; a second begin while dialling is refused.
	{
		sgtc_gate_timing_t g;
		SgtcGateTiming_Reset( &g, 1, 100.0f );
		check( "dial-start", SgtcGateTiming_BeginDial( &g, 100.0f ) == 1,
			"first dial accepted" );
		check( "dial-start-once", SgtcGateTiming_BeginDial( &g, 100.1f ) == 0,
			"second dial refused while dialling" );
		check( "dial-state", g.state == SGTC_GATE_STATE_DIALING,
			"state is DIALING" );
		check( "dial-no-entry", !SgtcGateTiming_CanEnter( &g, 100.1f ),
			"no entry before the window" );
	}

	// C) Chevron progression: exactly seven locks, then warm-up, then open.
	{
		sgtc_gate_timing_t g;
		SgtcGateTiming_Reset( &g, 1, 100.0f );
		SgtcGateTiming_BeginDial( &g, 100.0f );
		float t = 100.0f;
		for( int i = 1; i <= SGTC_GATE_DIAL_STEPS; i++ )
		{
			t += SGTC_GATE_RING_STEP;
			SgtcGateTiming_Tick( &g, t );
			check( i == 1 ? "chevron-1" : i == SGTC_GATE_DIAL_STEPS ? "chevron-8" : "chevron-progress",
				g.chevrons_locked == i && !SgtcGateTiming_CanEnter( &g, t ),
				"dial steps lock in order with no entry before warm-up" );
		}
		check( "warmup", g.state == SGTC_GATE_STATE_WARMUP,
			"the point of origin starts the warm-up" );
		check( "warmup-open-time", near( g.open_time, t + SGTC_GATE_WARMUP ),
			"open_time is dial completion + warm-up" );
		check( "warmup-no-entry", !SgtcGateTiming_CanEnter( &g, g.open_time - 0.01f ),
			"entry is invalid just before the warm-up finishes" );

		sgtc_bot_gate_ctx_t c = ctx( 1, 64.0f, 1 );
		check( "warmup-wait", SgtcBots_GateDecision( &g, &c, g.open_time - 0.01f ) == SGTC_BOT_GATE_WAIT,
			"bot waits through the warm-up" );

		SgtcGateTiming_Tick( &g, g.open_time );
		check( "entry-valid-at-open", SgtcGateTiming_CanEnter( &g, g.open_time ),
			"entry is valid exactly at open_time" );
		check( "entry-decision-at-open", SgtcBots_GateDecision( &g, &c, g.open_time ) == SGTC_BOT_GATE_ENTER,
			"bot enters inside the window" );
		check( "commit-travel-guard", SgtcGateTiming_CanCommit( &g, g.open_time, 1.0f ) == 1,
			"a short walk still fits the window" );
		check( "window-remaining", near( SgtcGateTiming_WindowRemaining( &g, g.open_time ), SGTC_GATE_OPEN_WINDOW ),
			"full window reported at open" );

		// D) The last valid instant is just before close_time, and at close_time
		// the bot must stop entering and re-dial after the cooldown.
		check( "entry-valid-before-close", SgtcGateTiming_CanEnter( &g, g.close_time - 0.01f ),
			"entry valid just before the window closes" );
		SgtcGateTiming_Tick( &g, g.close_time );
		check( "entry-invalid-at-close", !SgtcGateTiming_CanEnter( &g, g.close_time ),
			"entry is invalid at close_time" );
		check( "entry-decision-at-close", SgtcBots_GateDecision( &g, &c, g.close_time ) == SGTC_BOT_GATE_WAIT,
			"bot does not enter a closed gate" );
		check( "missed-state", g.state == SGTC_GATE_STATE_MISSED && g.failure == SGTC_GATE_FAIL_WINDOW,
			"expired window records MISSED/window failure" );
		SgtcGateTiming_Tick( &g, g.retry_time );
		check( "missed-retry", g.state == SGTC_GATE_STATE_CLOSED,
			"cooldown returns the gate to CLOSED" );
		check( "missed-redial", SgtcBots_GateDecision( &g, &c, g.retry_time ) == SGTC_BOT_GATE_DIAL,
			"bot dials again after a missed window" );
	}

	// E) A dial aborted mid-ring never opens a window and does not accept entry,
	// even after the retry cooldown has elapsed.
	{
		sgtc_gate_timing_t g;
		SgtcGateTiming_Reset( &g, 1, 200.0f );
		SgtcGateTiming_BeginDial( &g, 200.0f );
		SgtcGateTiming_Tick( &g, 200.55f );
		check( "abort-mid-dial", SgtcGateTiming_Abort( &g, 200.6f ) == 1,
			"abort accepted while dialling" );
		check( "abort-state", g.state == SGTC_GATE_STATE_ABORTED && g.failure == SGTC_GATE_FAIL_ABORT,
			"abort records ABORTED/abort" );
		check( "abort-no-entry", !SgtcGateTiming_CanEnter( &g, 201.0f ),
			"aborted dial never opens" );
		check( "abort-no-entry-late", !SgtcGateTiming_CanEnter( &g, 220.0f ),
			"aborted dial still not open later" );
		sgtc_bot_gate_ctx_t c = ctx( 1, 64.0f, 1 );
		SgtcGateTiming_Tick( &g, 220.0f );
		check( "abort-retry", SgtcBots_GateDecision( &g, &c, 220.0f ) == SGTC_BOT_GATE_DIAL,
			"bot re-dials after an aborted sequence" );
	}

	// F) A dial that stalls is failed by the timeout and never opens.
	{
		sgtc_gate_timing_t g;
		SgtcGateTiming_Reset( &g, 1, 300.0f );
		SgtcGateTiming_BeginDial( &g, 300.0f );
		float t = 300.0f + SGTC_GATE_DIAL_TIMEOUT + 0.01f;
		SgtcGateTiming_Tick( &g, t );
		check( "dial-timeout", g.state == SGTC_GATE_STATE_ABORTED && g.failure == SGTC_GATE_FAIL_TIMEOUT,
			"stalled dial times out" );
		check( "dial-timeout-no-entry", !SgtcGateTiming_CanEnter( &g, t + 1.0f ),
			"timed-out dial never opens" );
	}

	// G) A gate opened elsewhere (player DHD or map relay) is usable for the
	// same window, and an observed close ends entry immediately.
	{
		sgtc_gate_timing_t g;
		SgtcGateTiming_Reset( &g, 1, 400.0f );
		check( "observed-open", SgtcGateTiming_ObserveOpened( &g, 400.0f ) == 1,
			"observed g1tele_x_on opens the model" );
		check( "observed-entry", SgtcGateTiming_CanEnter( &g, 400.1f ),
			"bot can use a player-opened gate" );
		check( "observed-close", SgtcGateTiming_ObserveClosed( &g, 410.0f ) == 1,
			"observed g1tele_x_off closes the model" );
		check( "observed-close-entry", !SgtcGateTiming_CanEnter( &g, 410.1f ),
			"no entry after an observed close" );
	}

	// H) A bot that is not at the entry, or is still far away, keeps navigating.
	{
		sgtc_gate_timing_t g;
		SgtcGateTiming_Reset( &g, 1, 500.0f );
		sgtc_bot_gate_ctx_t far = ctx( 1, SGTC_GATE_WAIT_DISTANCE + 1.0f, 1 );
		sgtc_bot_gate_ctx_t away = ctx( 0, 32.0f, 1 );
		check( "not-at-entry", SgtcBots_GateDecision( &g, &away, 500.0f ) == SGTC_BOT_GATE_NONE,
			"non-entry waypoint is normal navigation" );
		check( "far-from-entry", SgtcBots_GateDecision( &g, &far, 500.0f ) == SGTC_BOT_GATE_NONE,
			"bot approaches before committing" );
	}

	std::printf( "SIT\tgatedial:summary\t%s\t%d checks, %d failed\n",
		g_failed ? "FAIL" : "PASS", g_checks, g_failed );
	return g_failed ? 1 : 0;
}
