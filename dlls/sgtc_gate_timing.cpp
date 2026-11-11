// sgtc_gate_timing.cpp - C-linkage exports for the pure gate timing model.
//
// The model itself is header-only so tests/host can compile it without the
// engine. These wrappers exist only so tests/stub_engine.c (C, dlsym) and the
// feature harness can drive the exact shipped inline functions.

#include "sgtc_gate_timing.h"
#include "sgtc_bots_gate.h"

extern "C" {

void SGTC_GateTiming_Reset( sgtc_gate_timing_t *gate, int gate_id, float now )
{
	SgtcGateTiming_Reset( gate, gate_id, now );
}

int SGTC_GateTiming_BeginDial( sgtc_gate_timing_t *gate, float now )
{
	return SgtcGateTiming_BeginDial( gate, now );
}

int SGTC_GateTiming_Abort( sgtc_gate_timing_t *gate, float now )
{
	return SgtcGateTiming_Abort( gate, now );
}

void SGTC_GateTiming_Tick( sgtc_gate_timing_t *gate, float now )
{
	SgtcGateTiming_Tick( gate, now );
}

int SGTC_GateTiming_CanEnter( const sgtc_gate_timing_t *gate, float now )
{
	return SgtcGateTiming_CanEnter( gate, now );
}

int SGTC_GateTiming_CanCommit( const sgtc_gate_timing_t *gate, float now, float travel )
{
	return SgtcGateTiming_CanCommit( gate, now, travel );
}

float SGTC_GateTiming_WindowRemaining( const sgtc_gate_timing_t *gate, float now )
{
	return SgtcGateTiming_WindowRemaining( gate, now );
}

int SGTC_GateTiming_ObserveOpened( sgtc_gate_timing_t *gate, float now )
{
	return SgtcGateTiming_ObserveOpened( gate, now );
}

int SGTC_GateTiming_ObserveClosed( sgtc_gate_timing_t *gate, float now )
{
	return SgtcGateTiming_ObserveClosed( gate, now );
}

int SGTC_Bots_GateDecision( const sgtc_gate_timing_t *gate, const sgtc_bot_gate_ctx_t *ctx, float now )
{
	return SgtcBots_GateDecision( gate, ctx, now );
}

} // extern "C"
