// sgtc_gate_timing.h - pure Stargate wormhole / DHD timing model.
//
// This file deliberately has no GoldSrc, jk_botti or engine includes. It is
// plain C-compatible data plus inline functions so the same state machine can
// be compiled into the game DLL and into a host unit-test harness. The engine
// wrapper (dlls/sgtc_bots.cpp) owns entities, links and FireTargets calls; this
// model only knows time.
//
// A gate cycles through:
//   CLOSED --BeginDial--> DIALING --7 chevrons--> WARMUP --warm-up--> OPEN
//   OPEN --window expires--> MISSED (must cool down, then dial again)
//   DIALING/WARMUP --Abort/timeout--> ABORTED (must cool down, then dial again)
//
// Entry is valid only in the OPEN window: at or after open_time and before
// close_time. The point of origin (eighth chevron) is folded into the dial
// completion: once seven chevrons are locked the ring is complete and the
// wormhole starts its warm-up; the next state boundary is open_time.

#ifndef SGTC_GATE_TIMING_H
#define SGTC_GATE_TIMING_H

// Seven address chevrons plus the point of origin. The shipped sg_stargate
// DHD is an eight-press button (stage1..stage7 retarget the button, stage8 is
// the point of origin); the model counts all eight as dial steps and reports
// the address chevrons separately.
#define SGTC_GATE_CHEVRONS          7
#define SGTC_GATE_DIAL_STEPS        8
// Time between two chevron/ring locks.
#define SGTC_GATE_RING_STEP         0.55f
// Kawoosh / event-horizon warm-up after the final chevron locks. The shipped
// sg_stargate/sg_conquest entity lumps delay g1tele_x_on by 2.15 s after the
// opening relay, which is the value this model follows.
#define SGTC_GATE_WARMUP            2.15f
// Usable window once the warm-up finishes. sg_stargate's stage8 fires
// g1tele_x_on at 2.15 s and openingb fires closing_but at 16 s after itself
// (T+17.65), so the usable window is 15.5 s.
#define SGTC_GATE_OPEN_WINDOW       15.5f
// After an abort or a missed window the DHD must re-arm.
#define SGTC_GATE_RETRY_COOLDOWN    2.0f
// A dial that has not reached the seventh chevron by now is failed.
#define SGTC_GATE_DIAL_TIMEOUT      15.0f
// A bot must not commit to the last fraction of a second of the window.
#define SGTC_GATE_ENTRY_MARGIN      0.20f
// Float compare tolerance for a caller that advances the clock in steps.
#define SGTC_GATE_TIME_EPSILON      0.001f
// Bot stops this far from the entry waypoint and dials instead of walking in.
#define SGTC_GATE_WAIT_DISTANCE     200.0f

// Gate states.
#define SGTC_GATE_STATE_CLOSED      0
#define SGTC_GATE_STATE_DIALING     1
#define SGTC_GATE_STATE_WARMUP      2
#define SGTC_GATE_STATE_OPEN        3
#define SGTC_GATE_STATE_MISSED      4
#define SGTC_GATE_STATE_ABORTED     5

// Failure reasons (0 means none).
#define SGTC_GATE_FAIL_NONE         0
#define SGTC_GATE_FAIL_ABORT        1
#define SGTC_GATE_FAIL_TIMEOUT      2
#define SGTC_GATE_FAIL_WINDOW       3

typedef struct
{
	int		state;			// SGTC_GATE_STATE_*
	int		gate_id;		// 1 or 2 (map gate number)
	int		chevrons_locked;	// 0..SGTC_GATE_DIAL_STEPS
	int		failure;		// SGTC_GATE_FAIL_*
	float		dial_start_time;	// when BeginDial() was accepted
	float		next_chevron_time;	// next ring/chevron lock
	float		warmup_start_time;	// when the seventh chevron locked
	float		open_time;		// first instant entry is valid
	float		close_time;		// first instant entry is invalid
	float		retry_time;		// earliest time a new dial may start
} sgtc_gate_timing_t;

static inline void SgtcGateTiming_Reset( sgtc_gate_timing_t *gate, int gate_id, float now )
{
	(void)now;
	gate->state = SGTC_GATE_STATE_CLOSED;
	gate->gate_id = gate_id;
	gate->chevrons_locked = 0;
	gate->failure = SGTC_GATE_FAIL_NONE;
	gate->dial_start_time = 0.0f;
	gate->next_chevron_time = 0.0f;
	gate->warmup_start_time = 0.0f;
	gate->open_time = 0.0f;
	gate->close_time = 0.0f;
	gate->retry_time = 0.0f;
}

// Start a dial. Returns 1 when accepted, 0 when the gate is busy, already open,
// still cooling down or the caller passed a backwards clock.
static inline int SgtcGateTiming_BeginDial( sgtc_gate_timing_t *gate, float now )
{
	if( gate->state == SGTC_GATE_STATE_DIALING ||
		gate->state == SGTC_GATE_STATE_WARMUP ||
		gate->state == SGTC_GATE_STATE_OPEN )
		return 0;

	if( now < gate->retry_time )
		return 0;

	gate->state = SGTC_GATE_STATE_DIALING;
	gate->chevrons_locked = 0;
	gate->failure = SGTC_GATE_FAIL_NONE;
	gate->dial_start_time = now;
	gate->next_chevron_time = now + SGTC_GATE_RING_STEP;
	gate->warmup_start_time = 0.0f;
	gate->open_time = 0.0f;
	gate->close_time = 0.0f;
	gate->retry_time = 0.0f;
	return 1;
}

// Abort an in-flight dial (bot died, lost its target, was interrupted). An
// already open gate is left open; the window is not shortened by an abort.
// Returns 1 when a dial/warm-up was actually aborted.
static inline int SgtcGateTiming_Abort( sgtc_gate_timing_t *gate, float now )
{
	if( gate->state != SGTC_GATE_STATE_DIALING &&
		gate->state != SGTC_GATE_STATE_WARMUP )
		return 0;

	gate->state = SGTC_GATE_STATE_ABORTED;
	gate->failure = SGTC_GATE_FAIL_ABORT;
	gate->chevrons_locked = 0;
	gate->next_chevron_time = 0.0f;
	gate->warmup_start_time = 0.0f;
	gate->open_time = 0.0f;
	gate->close_time = 0.0f;
	gate->retry_time = now + SGTC_GATE_RETRY_COOLDOWN;
	return 1;
}

// Advance the state machine to `now`. This is monotonic and idempotent: calling
// it twice with the same time cannot lock two chevrons. It never moves time
// backwards, so a map change must call Reset() with the new clock.
static inline void SgtcGateTiming_Tick( sgtc_gate_timing_t *gate, float now )
{
	switch( gate->state )
	{
	case SGTC_GATE_STATE_DIALING:
		if( now - gate->dial_start_time >= SGTC_GATE_DIAL_TIMEOUT )
		{
			gate->state = SGTC_GATE_STATE_ABORTED;
			gate->failure = SGTC_GATE_FAIL_TIMEOUT;
			gate->chevrons_locked = 0;
			gate->next_chevron_time = 0.0f;
			gate->retry_time = now + SGTC_GATE_RETRY_COOLDOWN;
			return;
		}

		while( gate->chevrons_locked < SGTC_GATE_DIAL_STEPS &&
			now + SGTC_GATE_TIME_EPSILON >= gate->next_chevron_time )
		{
			gate->chevrons_locked++;
			gate->next_chevron_time += SGTC_GATE_RING_STEP;
		}

		if( gate->chevrons_locked >= SGTC_GATE_DIAL_STEPS )
		{
			gate->state = SGTC_GATE_STATE_WARMUP;
			gate->warmup_start_time = now;
			gate->open_time = now + SGTC_GATE_WARMUP;
			gate->close_time = gate->open_time + SGTC_GATE_OPEN_WINDOW;
			gate->next_chevron_time = 0.0f;
		}
		break;

	case SGTC_GATE_STATE_WARMUP:
		if( now >= gate->open_time )
			gate->state = SGTC_GATE_STATE_OPEN;
		break;

	case SGTC_GATE_STATE_OPEN:
		if( now >= gate->close_time )
		{
			gate->state = SGTC_GATE_STATE_MISSED;
			gate->failure = SGTC_GATE_FAIL_WINDOW;
			gate->retry_time = now + SGTC_GATE_RETRY_COOLDOWN;
		}
		break;

	case SGTC_GATE_STATE_MISSED:
	case SGTC_GATE_STATE_ABORTED:
		if( now >= gate->retry_time )
			gate->state = SGTC_GATE_STATE_CLOSED;
		break;

	default:
		break;
	}
}

// Entry is valid in [open_time, close_time). Before the warm-up finishes and
// at/after close_time this is false, which is the property the bot gate logic
// relies on.
static inline int SgtcGateTiming_CanEnter( const sgtc_gate_timing_t *gate, float now )
{
	if( gate->state != SGTC_GATE_STATE_OPEN )
		return 0;

	return ( now >= gate->open_time && now < gate->close_time ) ? 1 : 0;
}

// Same window, but with a travel-time guard: a bot that would spend `travel`
// seconds reaching the event horizon must have that much window left.
static inline int SgtcGateTiming_CanCommit( const sgtc_gate_timing_t *gate, float now, float travel )
{
	if( gate->state != SGTC_GATE_STATE_OPEN )
		return 0;

	if( travel < 0.0f )
		travel = 0.0f;

	return ( now >= gate->open_time &&
		now + travel < gate->close_time - SGTC_GATE_ENTRY_MARGIN ) ? 1 : 0;
}

static inline int SgtcGateTiming_IsOpen( const sgtc_gate_timing_t *gate, float now )
{
	return SgtcGateTiming_CanEnter( gate, now );
}

static inline float SgtcGateTiming_WindowRemaining( const sgtc_gate_timing_t *gate, float now )
{
	if( gate->state != SGTC_GATE_STATE_OPEN || now >= gate->close_time )
		return 0.0f;

	if( now < gate->open_time )
		return gate->close_time - gate->open_time;

	return gate->close_time - now;
}

// A gate opened by a player or map relay (g1tele_x_on / g2tele_x_on) is usable
// for the same window. The engine observer calls this when it sees the relay;
// it deliberately does not reset an open gate, and it is ignored while the bot
// model is already dialling/warming up so a bot's own completion cannot move
// its own open_time earlier.
static inline int SgtcGateTiming_ObserveOpened( sgtc_gate_timing_t *gate, float now )
{
	if( gate->state == SGTC_GATE_STATE_DIALING ||
		gate->state == SGTC_GATE_STATE_WARMUP ||
		gate->state == SGTC_GATE_STATE_OPEN )
		return 0;

	gate->state = SGTC_GATE_STATE_OPEN;
	gate->failure = SGTC_GATE_FAIL_NONE;
	gate->open_time = now;
	gate->close_time = now + SGTC_GATE_OPEN_WINDOW;
	gate->retry_time = 0.0f;
	return 1;
}

// The relay that closes a gate (g1tele_x_off / g2tele_x_off). A bot that is
// already inside the trigger has already been moved; one still approaching
// stops and re-dials.
static inline int SgtcGateTiming_ObserveClosed( sgtc_gate_timing_t *gate, float now )
{
	if( gate->state != SGTC_GATE_STATE_OPEN &&
		gate->state != SGTC_GATE_STATE_WARMUP )
		return 0;

	gate->state = SGTC_GATE_STATE_MISSED;
	gate->failure = SGTC_GATE_FAIL_WINDOW;
	gate->close_time = now;
	gate->retry_time = now + SGTC_GATE_RETRY_COOLDOWN;
	return 1;
}

#endif // SGTC_GATE_TIMING_H
