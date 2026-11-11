// sgtc_radio.h — the mod's radio vocabulary and the decisions bots make about it.
//
// Header-only and engine-free on purpose: the server (dlls/) and the vendored
// bot code (bots/jk_botti/) must agree on the message ids, the order mapping and
// the obey/rate rules, and tests/features/test_bot_radio.py compiles this file
// on its own. Plain C++98 + no includes.
//
// The radio itself is the mod's own: the client menu sends `client <peer>` then
// `radio <msgId>`; the server relays the existing `Radio` usermessage (BYTE
// msgId, BYTE sender entindex) and the client renders it as
// "#RadioMessage[Goauld]NN" + "radio/son[goa]NN.wav" (cl_dll/sgtc_hud.cpp,
// MsgFunc_Radio). Nothing here adds an asset or a new message; the numbers
// below are the ids the shipped stargatetc/Titles.txt already names.

#pragma once
#ifndef SGTC_RADIO_H
#define SGTC_RADIO_H

// ---------------------------------------------------------------------------
// Message ids — fixed by stargatetc/Titles.txt and the client's NN formatting.
// MUST NOT CHANGE: the client builds the text key and the wav name from them.
// RadioBoutonNN is the menu label, RadioMessageNN the line that is printed.
// ---------------------------------------------------------------------------
enum SgtcRadioMessage
{
	SGTC_RADIO_COVER_ME      = 0,	// "Cover Me"
	SGTC_RADIO_FALL_BACK     = 1,	// "Fall Back"
	SGTC_RADIO_GO_GO_GO      = 2,	// "Go Go Go!"
	SGTC_RADIO_HOLD_POSITION = 3,	// "Hold Position"
	SGTC_RADIO_RECON         = 4,	// button "Follow me", line "I'm on recon"
	SGTC_RADIO_FOLLOW_TARGET = 5,	// "Follow the target"
	SGTC_RADIO_SPREAD_OUT    = 6,	// "Spread out"
	SGTC_RADIO_STOP           = 7,	// "Stop"
	SGTC_RADIO_FOLLOW_ME     = 8,	// "Follow me"
	SGTC_RADIO_IN_POSITION   = 10,	// "I'm in Position"
	SGTC_RADIO_GRENADE       = 11,	// "Grenade!"
	SGTC_RADIO_YES_SIR       = 12,	// "Yes Sir!"
	SGTC_RADIO_ENEMY         = 13,	// "Watch out Enemy!"
	SGTC_RADIO_ENEMIES       = 14,	// "Watch Out Enemies!"
	SGTC_RADIO_TAKING_GROUND = 15,	// "Enemy is taking ground"
	SGTC_RADIO_NEED_BACKUP   = 16,	// "Help, I need backup!"
	SGTC_RADIO_NEED_AMMO     = 20,	// "I need Ammo!"
	SGTC_RADIO_REPORTING     = 21,	// "Reporting"
	SGTC_RADIO_AFFIRMATIVE   = 22,	// "Affirmative"
	SGTC_RADIO_NEGATIVE      = 23,	// "Negitive" (sic, shipped spelling)
	SGTC_RADIO_DIE           = 27,	// "Die!!"
	SGTC_RADIO_EXPAND        = 28	// "Expand"
};

// ---------------------------------------------------------------------------
// What a radio line asks a teammate to do. The mapping below is the whole
// interpretation layer; the bot module only ever switches on these values.
// ---------------------------------------------------------------------------
enum SgtcRadioOrder
{
	SGTC_RADIO_ORDER_NONE     = 0,
	SGTC_RADIO_ORDER_FOLLOW   = 1,	// escort the speaker, stay on his heels
	SGTC_RADIO_ORDER_HOLD     = 2,	// camp where the speaker is standing
	SGTC_RADIO_ORDER_PUSH     = 3,	// advance toward the objective / enemy side
	SGTC_RADIO_ORDER_SUPPORT  = 4,	// go help the speaker right now
	SGTC_RADIO_ORDER_ENGAGE   = 5,	// move to the reported enemy area and fight
	SGTC_RADIO_ORDER_FALLBACK = 6,	// pull back toward our own base
	SGTC_RADIO_ORDER_SPREAD   = 7	// scatter away from the speaker
};

// Tuning shared by the bot code and the tests.
#define SGTC_RADIO_HEAR_RANGE        1200.0f	// world units a teammate is audible
#define SGTC_RADIO_ORDER_MIN_SECONDS 20.0f	// order lifetime, lower bound
#define SGTC_RADIO_ORDER_MAX_SECONDS 40.0f	// order lifetime, upper bound
#define SGTC_RADIO_ACCEPT_MIN_DELAY  0.3f	// reaction delay before an order applies
#define SGTC_RADIO_ACCEPT_MAX_DELAY  1.5f
#define SGTC_RADIO_DEFAULT_RATE      12.0f	// seconds between one bot's radio calls
#define SGTC_RADIO_TEAM_GAP          3.0f	// seconds between two radio calls on a team
#define SGTC_RADIO_NEED_BACKUP_MAX   2		// nearest N bots answer "need backup"

// bot_radio values.
enum SgtcBotRadioMode
{
	SGTC_BOT_RADIO_OFF   = 0,	// bots neither obey nor talk
	SGTC_BOT_RADIO_OBEY  = 1,	// bots obey orders, never speak
	SGTC_BOT_RADIO_TALK  = 2	// bots obey orders and use the radio themselves
};

// One 0..1 roll -> a jittered value. Deterministic so tests can pin it.
inline float SGTC_Radio_Lerp01( float r01, float lo, float hi )
{
	if ( r01 < 0.0f ) r01 = 0.0f;
	if ( r01 > 1.0f ) r01 = 1.0f;
	return lo + ( hi - lo ) * r01;
}

// Order a radio line carries, SGTC_RADIO_ORDER_NONE for chatter/acks that only
// report state. Follow lines are the mod's "regroup on me" as well: there is no
// separate regroup verb in the shipped menu.
inline int SGTC_Radio_OrderForMessage( int msgId )
{
	switch ( msgId )
	{
	case SGTC_RADIO_RECON:
	case SGTC_RADIO_FOLLOW_TARGET:
	case SGTC_RADIO_FOLLOW_ME:
		return SGTC_RADIO_ORDER_FOLLOW;

	case SGTC_RADIO_HOLD_POSITION:
	case SGTC_RADIO_STOP:
		return SGTC_RADIO_ORDER_HOLD;

	case SGTC_RADIO_GO_GO_GO:
	case SGTC_RADIO_EXPAND:
		return SGTC_RADIO_ORDER_PUSH;

	case SGTC_RADIO_COVER_ME:
	case SGTC_RADIO_NEED_BACKUP:
		return SGTC_RADIO_ORDER_SUPPORT;

	case SGTC_RADIO_ENEMY:
	case SGTC_RADIO_ENEMIES:
	case SGTC_RADIO_TAKING_GROUND:
		return SGTC_RADIO_ORDER_ENGAGE;

	case SGTC_RADIO_FALL_BACK:
		return SGTC_RADIO_ORDER_FALLBACK;

	case SGTC_RADIO_SPREAD_OUT:
	case SGTC_RADIO_GRENADE:
		return SGTC_RADIO_ORDER_SPREAD;

	default:
		// In Position / Yes Sir / Affirmative / Negative / Reporting /
		// Need Ammo / Die!! carry no movement order.
		return SGTC_RADIO_ORDER_NONE;
	}
}

// TRUE when msgId is one of the ids the shipped stargatetc/Titles.txt names.
// The client `radio` verb takes an arbitrary integer from the player, so both
// the relay and the bot layer only ever act on ids the client can render.
inline int SGTC_Radio_IsValidMessage( int msgId )
{
	switch ( msgId )
	{
	case SGTC_RADIO_COVER_ME:
	case SGTC_RADIO_FALL_BACK:
	case SGTC_RADIO_GO_GO_GO:
	case SGTC_RADIO_HOLD_POSITION:
	case SGTC_RADIO_RECON:
	case SGTC_RADIO_FOLLOW_TARGET:
	case SGTC_RADIO_SPREAD_OUT:
	case SGTC_RADIO_STOP:
	case SGTC_RADIO_FOLLOW_ME:
	case SGTC_RADIO_IN_POSITION:
	case SGTC_RADIO_GRENADE:
	case SGTC_RADIO_YES_SIR:
	case SGTC_RADIO_ENEMY:
	case SGTC_RADIO_ENEMIES:
	case SGTC_RADIO_TAKING_GROUND:
	case SGTC_RADIO_NEED_BACKUP:
	case SGTC_RADIO_NEED_AMMO:
	case SGTC_RADIO_REPORTING:
	case SGTC_RADIO_AFFIRMATIVE:
	case SGTC_RADIO_NEGATIVE:
	case SGTC_RADIO_DIE:
	case SGTC_RADIO_EXPAND:
		return 1;
	default:
		return 0;
	}
}

// TRUE for lines that answer another line rather than asking for something.
inline int SGTC_Radio_IsReply( int msgId )
{
	switch ( msgId )
	{
	case SGTC_RADIO_YES_SIR:
	case SGTC_RADIO_AFFIRMATIVE:
	case SGTC_RADIO_NEGATIVE:
	case SGTC_RADIO_IN_POSITION:
	case SGTC_RADIO_REPORTING:
		return 1;
	default:
		return 0;
	}
}

// The line a bot sends when it takes an order: "Yes Sir!" for low skill,
// "Affirmative" otherwise, so the voice matches the bot's assertiveness.
inline int SGTC_Radio_AcceptReply( int skill )
{
	return skill < 2 ? SGTC_RADIO_YES_SIR : SGTC_RADIO_AFFIRMATIVE;
}

// Radio line a bot volunteers when it takes an order of this type, or 0.
inline int SGTC_Radio_SpeakForOrder( int order, int skill )
{
	switch ( order )
	{
	case SGTC_RADIO_ORDER_PUSH:
		// a confident leader calls the push, everybody else just moves
		return skill >= 3 ? SGTC_RADIO_GO_GO_GO : 0;
	case SGTC_RADIO_ORDER_HOLD:
		return SGTC_RADIO_IN_POSITION;	// sent on arrival, see bot_radio.cpp
	default:
		return 0;
	}
}

// Chance a bot accepts an order. skill is 0..4 (bot_t::bot_skill); in_combat
// means the bot is shooting at / has just seen an enemy; low_health means it is
// hurt. A fight makes a bot far less likely to drop what it is doing.
inline float SGTC_Radio_ObeyChance( int skill, int in_combat, int low_health )
{
	if ( skill < 0 ) skill = 0;
	if ( skill > 4 ) skill = 4;

	float chance = 0.35f + 0.15f * (float)skill;	// 0.35 .. 0.95

	if ( in_combat )
		chance *= 0.35f;
	if ( low_health )
		chance *= 0.70f;

	if ( chance < 0.0f ) chance = 0.0f;
	if ( chance > 1.0f ) chance = 1.0f;
	return chance;
}

// Order lifetime in [MIN, MAX] seconds from one 0..1 roll.
inline float SGTC_Radio_OrderDuration( float r01 )
{
	return SGTC_Radio_Lerp01( r01, SGTC_RADIO_ORDER_MIN_SECONDS, SGTC_RADIO_ORDER_MAX_SECONDS );
}

// Reaction delay in [ACCEPT_MIN_DELAY, ACCEPT_MAX_DELAY] seconds.
inline float SGTC_Radio_AcceptDelay( float r01 )
{
	return SGTC_Radio_Lerp01( r01, SGTC_RADIO_ACCEPT_MIN_DELAY, SGTC_RADIO_ACCEPT_MAX_DELAY );
}

// Should a bot hear this line at all? Speaker and bot must be on the same side,
// different players, and within SGTC_RADIO_HEAR_RANGE. team values use the
// entity numbering (pev->team: 1 = Goa'uld, 2 = Torri); any other value is a
// spectator/unassigned and hears nothing.
inline int SGTC_Radio_ShouldHear( int speaker_team, int bot_team, int same_edict, float distance )
{
	if ( same_edict )
		return 0;
	if ( speaker_team < 1 || speaker_team > 2 )
		return 0;
	if ( bot_team != speaker_team )
		return 0;
	if ( distance > SGTC_RADIO_HEAR_RANGE )
		return 0;
	return 1;
}

// TRUE when a rate deadline cannot have been armed on the current map.
// gpGlobals->time restarts near 0 on a level change, but a deadline armed on
// the previous map keeps its old value: no gap can put it more than `gap` plus
// a second of slack ahead of `now`, so anything further out is stale. Without
// this a team that last spoke at t=1800 stays silent for half an hour on the
// next map.
inline int SGTC_Radio_DeadlineStale( float now, float next_allowed, float gap )
{
	if ( gap < 0.0f ) gap = 0.0f;
	return next_allowed > now + gap + 1.0f;
}

// Rate gate: a call is allowed once the previous slot has elapsed. A stale
// deadline (SGTC_Radio_DeadlineStale) counts as elapsed, so a map change can
// never silence a bot or a whole team for the rest of the new map.
inline int SGTC_Radio_RateAllows( float now, float next_allowed, float gap )
{
	if ( SGTC_Radio_DeadlineStale( now, next_allowed, gap ) )
		return 1;
	return next_allowed <= now;
}

// Next slot after a call that must be at least `gap` seconds apart.
inline float SGTC_Radio_NextSlot( float now, float gap )
{
	if ( gap < 0.0f ) gap = 0.0f;
	return now + gap;
}

// bot_radio gating.
inline int SGTC_Radio_ModeAllowsObey( int mode )
{
	return mode >= SGTC_BOT_RADIO_OBEY;
}

inline int SGTC_Radio_ModeAllowsTalk( int mode )
{
	return mode >= SGTC_BOT_RADIO_TALK;
}

#endif // SGTC_RADIO_H
