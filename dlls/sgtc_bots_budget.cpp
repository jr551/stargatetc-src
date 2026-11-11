// sgtc_bots_budget.cpp - pure edict-budget decision (see sgtc_bots_budget.h).
//
// No engine calls and no globals: dlls/sgtc_bots.cpp wraps this with the live
// entity count and tests compile it directly on the host.

#include "sgtc_bots_budget.h"

// `freeguns` is a boolean: callers pass the live mp_freeguns > 1 test. Keeping
// the choice here (rather than in each wrapper) means BotCreate and the
// count-aware sgtc_bot_add pre-check cannot drift apart.
extern "C" int SGTC_Bots_EdictBudgetSpawnMargin( int freeguns )
{
	return freeguns ? SGTC_BOT_EDICT_MARGIN_FREEGUNS : SGTC_BOT_EDICT_MARGIN_NORMAL;
}

extern "C" int SGTC_Bots_EdictBudgetAllows( int used, int max_edicts, int margin )
{
	// Garbage in, refuse: a negative/garbage reading is exactly the case where
	// spawning would be unsafe.
	if( used < 0 || max_edicts <= 0 || margin < 0 )
		return 0;

	// A margin at or above the whole pool can never leave room for the new
	// bot, and `max_edicts - margin` would otherwise underflow.
	if( margin >= max_edicts )
		return 0;

	// Require `margin` free slots before this spawn: a pre-spawn reserve, not
	// a promise that this many remain afterwards. Using the subtraction form
	// avoids an int overflow for a max near INT_MAX: both operands are
	// non-negative here and max_edicts - margin is still positive.
	return ( used < max_edicts - margin ) ? 1 : 0;
}

extern "C" int SGTC_Bots_EdictBudgetAllowsRequest( int used, int max_edicts, int margin, int count )
{
	// A request for zero/negative bots is meaningless, and a negative count
	// would shrink the reservation below instead of growing it.
	if( count <= 0 )
		return 0;

	// Validate the base margin first: this rejects garbage input (including a
	// negative `used`) and a margin at/above the whole pool before subtracting.
	if( !SGTC_Bots_EdictBudgetAllows( used, max_edicts, margin ) )
		return 0;

	// Reserve one edict per requested bot; the first is covered by `margin`.
	// `margin < max_edicts` here, so room > 0. Compare before subtracting to
	// avoid overflowing `margin + count - 1` for a huge count.
	{
		const int room = max_edicts - margin;

		if( count - 1 >= room )
			return 0;

		return ( used < room - ( count - 1 ) ) ? 1 : 0;
	}
}
