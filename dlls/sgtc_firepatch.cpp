// sgtc_firepatch.cpp - pure fire-patch cap decision (see sgtc_firepatch.h).
//
// No engine calls and no globals: dlls/sgtc_c4fire.cpp wraps this with the
// live entity counts and tests compile it directly on the host.

#include "sgtc_firepatch.h"

extern "C" int SGTC_FirePatch_Allows( int active, int used, int max_edicts )
{
	// Garbage in, refuse: a negative/garbage reading is exactly the case
	// where spawning would be unsafe.
	if( active < 0 || used < 0 || max_edicts <= SGTC_FIREPATCH_EDICT_RESERVE )
		return 0;

	if( active >= SGTC_FIREPATCH_MAX_ACTIVE )
		return 0;

	// Keep SGTC_FIREPATCH_EDICT_RESERVE edicts for gameplay spawns. Using
	// the subtraction form avoids overflow for a max near INT_MAX; both
	// operands are non-negative here and the difference is positive.
	return ( used < max_edicts - SGTC_FIREPATCH_EDICT_RESERVE ) ? 1 : 0;
}
