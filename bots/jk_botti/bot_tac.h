//
// JK_Botti - be more human!
//
// bot_tac.h
//
// SGTC: the bot decision for engaging a deployed enemy TAC (monster_tac).
//
// A TAC is CLASS_MACHINE and does not carry FL_MONSTER, so the ordinary
// monster scan never sees it. This header describes the facts the bot can
// observe about one TAC; the decision itself is a pure function over that
// snapshot (dlls/sgtc_bots_tac.cpp) so tests/stub_engine.c can exercise it
// without a live engine, in the same style as the SGTC_Bots_* loadout hooks.
//
// Nothing here reads a cvar: mp_friendlyfire in particular is carried only so
// callers/tests can show that it does not make a teammate TAC a target.

#ifndef BOT_TAC_H
#define BOT_TAC_H

#ifdef __cplusplus
extern "C" {
#endif

// One observation of a deployed TAC, relative to the asking bot.
typedef struct
{
	int bot_team;             // bot's pev->team (SgtcTeam numbering: 1/2)
	int tac_team;             // TAC's pev->team, derived from its deployer
	int owned_by_bot;         // TAC pev->owner is this bot
	int armed;                // 2.7 s deploy delay elapsed (TAC cleared pev->owner)
	int alive;                // health > 0, DEAD_NO, solid, not FL_NOTARGET
	int damageable;           // pev->takedamage != DAMAGE_NO
	int visible;              // clear line of sight from the bot to the TAC
	float distance;           // bot -> TAC Manhattan distance (SGTC_Distance3D)
	float max_distance;       // mp_tacdistance, already clamped to 50..8000
	int higher_priority_enemy;// caller already has an enemy (players first)
	int friendlyfire;         // mp_friendlyfire != 0; must not affect the result
} sgtc_bot_tac_candidate_t;

// Decision result.
#define SGTC_BOT_TAC_TARGET_NONE       0  // do not engage a TAC
#define SGTC_BOT_TAC_TARGET_KEEP_ENEMY 1  // keep the higher-priority enemy
#define SGTC_BOT_TAC_TARGET_TAC        2  // engage this TAC

// Why the TAC was not chosen (also SGTC_BOT_TAC_REJECT_NONE when it was).
enum
{
	SGTC_BOT_TAC_REJECT_NONE = 0,
	SGTC_BOT_TAC_REJECT_HIGHER_PRIORITY,
	SGTC_BOT_TAC_REJECT_OWN,
	SGTC_BOT_TAC_REJECT_TEAM,
	SGTC_BOT_TAC_REJECT_DEAD,
	SGTC_BOT_TAC_REJECT_DAMAGE_NO,
	SGTC_BOT_TAC_REJECT_UNARMED,
	SGTC_BOT_TAC_REJECT_RANGE,
	SGTC_BOT_TAC_REJECT_VISIBILITY,
	SGTC_BOT_TAC_REJECT_BAD_INPUT
};

// 0..2, one of SGTC_BOT_TAC_TARGET_*.
int SGTC_Bots_TacTargetDecision( const sgtc_bot_tac_candidate_t *c );

// 0..9, one of SGTC_BOT_TAC_REJECT_*. Lets the caller find out whether
// visibility is the only remaining gate before paying for a trace.
int SGTC_Bots_TacTargetReason( const sgtc_bot_tac_candidate_t *c );

// Stable lower-case name for a reason code (never NULL).
const char *SGTC_Bots_TacTargetReasonName( int reason );

#ifdef __cplusplus
}
#endif

#endif // BOT_TAC_H
