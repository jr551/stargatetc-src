// sgtc_bots_stamina.h - the bots' breathing rule.
//
// StargateTC players carry a stamina meter (CBasePlayer::m_iStamina). Every
// half second of movement costs 10 of it, standing still gives back about 4
// and walking about 5, and below SGTC_STAMINA_PINNED the game clamps the
// player's maxspeed to 10 u/s (dlls/sgtc_player.cpp, SgtcThink). A person
// stops to get their breath back; a bot that keeps pushing forward stays
// pinned at a crawl for the rest of the match, which is exactly what the
// first playtest of the bot layer showed (six bots, 130 s, nobody left a
// 100 unit circle).
//
// The decision is a pure function over plain integers, header only, so
// tests/host/bot_stamina_harness.cpp can compile it without an engine. The
// engine half is SGTC_Bots_StaminaGate in dlls/sgtc_bots_stamina.cpp.
#pragma once
#ifndef SGTC_BOTS_STAMINA_H
#define SGTC_BOTS_STAMINA_H

// What the game's stamina tick gives back every half second while stamina is
// not above health (dlls/sgtc_player.cpp, SGTC_TickSgtcThinkTail):
//   +3 while moving on both axes and not crouched, +2 otherwise;
//   +2 more on the ground, +5 with the snag/invis latch, +1 with the cloak
//   tick, and -7 while the gas latch is active.
//
// That tick never runs in this build: SGTC_TickSgtcThinkTail re-arms its own
// 0.5 s gate (m_flSgtcTailTick) on every call, outside the `if( bTailTick )`
// block, so the gate opens once per player and never again. Nothing else
// restores stamina, so a player who has run it down stays at the 10 u/s clamp
// until a medic or a talent refills it. The bots copy the tick themselves
// (SGTC_Bots_StaminaGate) while that is the case.
static inline int SGTC_Stamina_RegenPerTick( int moving_xy, int ducking, int onground,
	int snag, int cloak, int gas )
{
	int gain = ( moving_xy && !ducking ) ? 3 : 2;

	if( snag )
		gain += 5;
	if( onground )
		gain += 2;
	if( cloak )
		gain += 1;
	if( gas )
		gain -= 7;

	return gain;
}

// The game's own clamp point (kLowStaminaSpeed applies below this).
#define SGTC_STAMINA_PINNED 20
// A bot that is not in a fight stops moving once it falls below this, which
// leaves a little headroom above the clamp for the next half second of drain.
#define SGTC_STAMINA_REST_BELOW 30
// A bot caught resting that gets into a fight moves again once it is above
// this (the clamp lifts at SGTC_STAMINA_PINNED, plus a margin).
#define SGTC_STAMINA_FIGHT_FLOOR 24
// Below this much health stamina cannot climb high enough for a rest to help
// (it is capped near health), so a badly hurt bot just keeps moving and
// looks for a medic instead.
#define SGTC_STAMINA_MIN_HEALTH 40

// Stamina a resting bot waits for before it moves on again: 70 % of its
// health, between 36 and 70, and never more than it can actually reach.
static inline int SGTC_Stamina_ExitLevel( int health )
{
	int level = health * 7 / 10;

	if( level < 36 )
		level = 36;
	if( level > 70 )
		level = 70;
	if( level > health )
		level = health;

	return level;
}

// 1 when the bot should stand still this frame, 0 when it may move.
//   resting   the answer from the previous frame
//   engaged   an enemy is in view or the bot was hit a moment ago
//   busy      on a ladder or in water, where standing still is not an option
static inline int SGTC_Stamina_Decide( int stamina, int health, int engaged, int busy, int resting )
{
	if( busy || health < SGTC_STAMINA_MIN_HEALTH )
		return 0;

	if( resting )
	{
		if( stamina >= SGTC_Stamina_ExitLevel( health ) )
			return 0;

		if( engaged && stamina >= SGTC_STAMINA_FIGHT_FLOOR )
			return 0;

		return 1;
	}

	if( !engaged && stamina < SGTC_STAMINA_REST_BELOW )
		return 1;

	return 0;
}

#endif // SGTC_BOTS_STAMINA_H
