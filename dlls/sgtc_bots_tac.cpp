// sgtc_bots_tac.cpp — pure jk_botti decision for engaging a deployed enemy TAC.
//
// The runtime scan lives in bots/jk_botti/bot_combat.cpp
// (BotFindEnemySearchTacs): it gathers the observable facts about each
// monster_tac and calls SGTC_Bots_TacTargetReason/Decision here. Keeping the
// rules pure lets tests/stub_engine.c prove the team/priority/range/visibility
// ordering without an engine, the same way the loadout hooks are tested.
//
// Why pev->owner decides "armed": CSGTCTac::Spawn leaves the deployer in
// pev->owner and TacThink clears it when the 2.7 s arm delay elapses
// (dlls/sgtc_monsters.cpp). A non-NULL owner therefore means the TAC is still
// inside its arm window; a NULL owner means it is armed (map-placed TACs have
// no owner and arm on their first 0.1 s think).

#include <stddef.h>

#include "sgtc_enums.h"
#include "../bots/jk_botti/bot_tac.h"

// Checks that need no trace. Ordered so the most important rejection wins:
// an existing enemy first (TACs never displace a player), then ownership and
// team, then state, then distance. Visibility is last so the caller can use
// the returned reason to decide whether a line trace is worth doing.
int SGTC_Bots_TacTargetReason( const sgtc_bot_tac_candidate_t *c )
{
	if( c == NULL )
		return SGTC_BOT_TAC_REJECT_BAD_INPUT;

	// Player targets keep priority. The runtime only scans for TACs after the
	// player/monster/breakable/sound searches, and passes a non-zero flag when
	// one of those already won; a TAC can never take that slot.
	if( c->higher_priority_enemy )
		return SGTC_BOT_TAC_REJECT_HIGHER_PRIORITY;

	// Own TAC and teammate TAC are never targets. This is deliberately a hard
	// team check, not a friendly-fire check: mp_friendlyfire must not turn a
	// teammate's TAC into a bot target (the field is carried for the test but
	// is never consulted below).
	if( c->owned_by_bot )
		return SGTC_BOT_TAC_REJECT_OWN;
	if( c->bot_team == SGTC_ENTTEAM_NONE || c->tac_team == SGTC_ENTTEAM_NONE ||
	    c->bot_team == c->tac_team )
		return SGTC_BOT_TAC_REJECT_TEAM;

	if( !c->alive )
		return SGTC_BOT_TAC_REJECT_DEAD;
	if( !c->damageable )
		return SGTC_BOT_TAC_REJECT_DAMAGE_NO;
	if( !c->armed )
		return SGTC_BOT_TAC_REJECT_UNARMED;

	// mp_tacdistance is the TAC's own reach (Manhattan); a TAC further away
	// than that is not the bot's fight.
	if( c->max_distance <= 0.0f || c->distance > c->max_distance )
		return SGTC_BOT_TAC_REJECT_RANGE;

	if( !c->visible )
		return SGTC_BOT_TAC_REJECT_VISIBILITY;

	return SGTC_BOT_TAC_REJECT_NONE;
}

int SGTC_Bots_TacTargetDecision( const sgtc_bot_tac_candidate_t *c )
{
	if( c != NULL && c->higher_priority_enemy )
		return SGTC_BOT_TAC_TARGET_KEEP_ENEMY;

	if( SGTC_Bots_TacTargetReason( c ) != SGTC_BOT_TAC_REJECT_NONE )
		return SGTC_BOT_TAC_TARGET_NONE;

	return SGTC_BOT_TAC_TARGET_TAC;
}

const char *SGTC_Bots_TacTargetReasonName( int reason )
{
	switch( reason )
	{
	case SGTC_BOT_TAC_REJECT_NONE:            return "none";
	case SGTC_BOT_TAC_REJECT_HIGHER_PRIORITY: return "higher-priority";
	case SGTC_BOT_TAC_REJECT_OWN:             return "own";
	case SGTC_BOT_TAC_REJECT_TEAM:            return "team";
	case SGTC_BOT_TAC_REJECT_DEAD:            return "dead";
	case SGTC_BOT_TAC_REJECT_DAMAGE_NO:       return "damage-no";
	case SGTC_BOT_TAC_REJECT_UNARMED:         return "unarmed";
	case SGTC_BOT_TAC_REJECT_RANGE:           return "range";
	case SGTC_BOT_TAC_REJECT_VISIBILITY:      return "visibility";
	case SGTC_BOT_TAC_REJECT_BAD_INPUT:       return "bad-input";
	default:                                  return "unknown";
	}
}
