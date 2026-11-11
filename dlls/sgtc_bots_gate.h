// sgtc_bots_gate.h - pure bot gate decision on top of sgtc_gate_timing.h.
//
// The engine wrapper in dlls/sgtc_bots.cpp supplies "this bot's current
// waypoint is a gate entry", the distance to it and whether the bot is free to
// dial. The decision itself has no engine types, so tests/host and the stub
// engine can drive it directly.

#ifndef SGTC_BOTS_GATE_H
#define SGTC_BOTS_GATE_H

#include "sgtc_gate_timing.h"

// Bot action for this frame.
#define SGTC_BOT_GATE_NONE     0	// not at a gate entry: normal navigation
#define SGTC_BOT_GATE_WAIT     1	// hold position; dial/warm-up/window not ready
#define SGTC_BOT_GATE_DIAL     2	// start the DHD sequence
#define SGTC_BOT_GATE_ENTER    3	// window is valid: walk through the event horizon

typedef struct
{
	int	at_entry;	// current route waypoint is the gate's near-side node
	float	distance;	// distance from the bot to that node
	int	can_dial;	// free to start a dial (no live enemy, not already busy)
	int	allow_enter;	// false if the bot is physically unable to move yet
} sgtc_bot_gate_ctx_t;

// Decide what a bot at a gate entry should do. ENTER is only returned inside
// the valid window; DIAL only after the gate is closed/aborted/missed and its
// cooldown has expired; everything else is WAIT.
static inline int SgtcBots_GateDecision( const sgtc_gate_timing_t *gate,
	const sgtc_bot_gate_ctx_t *ctx, float now )
{
	if( !ctx->at_entry || ctx->distance > SGTC_GATE_WAIT_DISTANCE )
		return SGTC_BOT_GATE_NONE;

	if( !ctx->allow_enter )
		return SGTC_BOT_GATE_WAIT;

	if( SgtcGateTiming_CanEnter( gate, now ) )
		return SGTC_BOT_GATE_ENTER;

	if( !ctx->can_dial )
		return SGTC_BOT_GATE_WAIT;

	if( gate->state == SGTC_GATE_STATE_CLOSED ||
		gate->state == SGTC_GATE_STATE_MISSED ||
		gate->state == SGTC_GATE_STATE_ABORTED )
	{
		if( now >= gate->retry_time )
			return SGTC_BOT_GATE_DIAL;
	}

	return SGTC_BOT_GATE_WAIT;
}

#endif // SGTC_BOTS_GATE_H
