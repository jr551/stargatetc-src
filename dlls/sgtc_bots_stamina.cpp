// sgtc_bots_stamina.cpp - engine half of the bots' breathing rule, see
// sgtc_bots_stamina.h for the why.
//
// Runs once per bot think from BotThinkFinalizeMovement, after class play and
// gate timing and before BotRunPlayerMove. When the bot should rest it zeroes
// its move speed (so the stuck detector, which compares against the previous
// move speed, stays quiet), drops a pending jump and crouches while nothing
// is in view. Aim and fire bits are left alone: a tired bot still shoots.

#include <stdio.h>

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"

#include "sgtc_bots.h"
#include "sgtc_bots_stamina.h"
#include "../bots/jk_botti/bot.h"
#include "../bots/jk_botti/bot_func.h"
#include "../bots/jk_botti/util.h"

// One latch per bot slot (bot.h's `bots` array is the slot owner).
static qboolean s_resting[32];
static float s_rest_since[32];
static int s_rest_count[32];
static float s_next_regen[32];
static qboolean s_game_regens[32];
static int s_regen_ticks[32];
static int s_expected[32];
static qboolean s_expect_valid[32];
static int s_rose[32];
static float s_last_seen[32];
static edict_t *s_edict[32];

extern bot_t bots[32];

// Read from the sgtc_bot_stamina cvar by dlls/sgtc_bots.cpp.
extern int sgtc_bot_stamina_enabled;

extern "C" int SGTC_Bots_StaminaRestCount( int slot )
{
	if( slot < 0 || slot >= 32 )
		return 0;

	return s_rest_count[slot];
}

extern "C" int SGTC_Bots_StaminaRegenTicks( int slot )
{
	if( slot < 0 || slot >= 32 )
		return 0;

	return s_regen_ticks[slot];
}

extern "C" int SGTC_Bots_StaminaResting( int slot )
{
	if( slot < 0 || slot >= 32 )
		return 0;

	return s_resting[slot] ? 1 : 0;
}

// A new bot in the slot, or a new map (the clock went backwards): forget what
// was learned about the previous one.
static void SGTC_Bots_StaminaSlotCheck( int slot, edict_t *pEdict )
{
	if( s_edict[slot] == pEdict && gpGlobals->time >= s_last_seen[slot] - 1.0f )
		return;

	s_edict[slot] = pEdict;
	s_resting[slot] = FALSE;
	s_next_regen[slot] = 0.0f;
	s_game_regens[slot] = FALSE;
	s_expected[slot] = 0;
	s_expect_valid[slot] = FALSE;
	s_rose[slot] = 0;
	s_last_seen[slot] = 0.0f;
}

// Copy of the game's half-second stamina regen for as long as the game's own
// tick is dead (see sgtc_bots_stamina.h).
//
// If the game's tick is ever fixed, this copy has to stop, or the bots would
// recover twice as fast as everyone else. The check is by behaviour, not by
// reading the game's gate: after each of our own ticks the stamina we left is
// remembered, and if it is found higher than that (by a small amount, as the
// game's tick gives) three times in a row, the game is regenerating by itself
// and our copy retires for that bot. A medic or talent refill is a jump of 20
// or more and does not count.
static void SGTC_Bots_StaminaRegen( int slot, CBasePlayer *pl )
{
	const float now = gpGlobals->time;

	s_last_seen[slot] = now;

	if( s_game_regens[slot] )
		return;

	// A map change restarts gpGlobals->time; a deadline from the last map
	// would otherwise freeze the regen for minutes.
	if( s_next_regen[slot] > now + 5.0f )
		s_next_regen[slot] = 0.0f;

	if( now < s_next_regen[slot] )
		return;

	s_next_regen[slot] = now + 0.5f;

	if( s_expect_valid[slot] )
	{
		const int rise = pl->m_iStamina - s_expected[slot];

		if( rise > 0 && rise <= 12 )
		{
			if( ++s_rose[slot] >= 3 )
			{
				s_game_regens[slot] = TRUE;
				return;
			}
		}
		else
		{
			s_rose[slot] = 0;
		}
	}

	if( (float)pl->m_iStamina > pl->pev->health )
	{
		s_expect_valid[slot] = FALSE;
		return;
	}

	const int gain = SGTC_Stamina_RegenPerTick(
		( pl->pev->velocity.x != 0.0f && pl->pev->velocity.y != 0.0f ) ? 1 : 0,
		( pl->pev->flags & FL_DUCKING ) ? 1 : 0,
		( pl->pev->flags & FL_ONGROUND ) ? 1 : 0,
		pl->m_bSnagInvisLatch ? 1 : 0,
		pl->m_bSgtcCloakTick ? 1 : 0,
		( pl->m_flSgtcLatch3fc > now ) ? 1 : 0 );

	pl->m_iStamina += gain;
	if( pl->m_iStamina < 0 )
		pl->m_iStamina = 0;

	pl->SGTC_UpdateStamina();
	s_expected[slot] = pl->m_iStamina;
	s_expect_valid[slot] = TRUE;
	s_regen_ticks[slot]++;
}

void SGTC_Bots_StaminaGate( bot_t &pBot )
{
	edict_t *pEdict = pBot.pEdict;

	if( !sgtc_bot_stamina_enabled || !pBot.is_used || FNullEnt( pEdict ) )
		return;

	const int slot = (int)( &pBot - &bots[0] );

	if( slot < 0 || slot >= 32 )
		return;

	CBasePlayer *pl = (CBasePlayer *)CBaseEntity::Instance( pEdict );

	if( !pl || !pl->IsAlive() || pl->IsObserver() )
	{
		s_resting[slot] = FALSE;
		return;
	}

	SGTC_Bots_StaminaSlotCheck( slot, pEdict );
	SGTC_Bots_StaminaRegen( slot, pl );

	const qboolean engaged = ( !FNullEnt( pBot.pBotEnemy ) ||
		( gpGlobals->time - pBot.f_last_time_attacked ) < 1.5f ) ? TRUE : FALSE;
	const qboolean busy = ( pBot.b_on_ladder || pBot.b_in_water ) ? TRUE : FALSE;
	const int was = s_resting[slot] ? 1 : 0;
	const int now = SGTC_Stamina_Decide( pl->m_iStamina, (int)pEdict->v.health,
		engaged, busy, was );

	if( now && !was )
	{
		s_rest_since[slot] = gpGlobals->time;
		s_rest_count[slot]++;
		SGTC_Bots_EmitAbility( pBot.name, "rest", pl->m_iSgtcPortraitShown, "start" );
	}
	else if( !now && was )
	{
		char detail[48];

		snprintf( detail, sizeof( detail ), "end %.1fs", gpGlobals->time - s_rest_since[slot] );
		SGTC_Bots_EmitAbility( pBot.name, "rest", pl->m_iSgtcPortraitShown, detail );
	}

	s_resting[slot] = now ? TRUE : FALSE;

	if( !now )
		return;

	pBot.f_move_speed = 0.0f;
	pEdict->v.button &= ~IN_JUMP;

	// Nothing in view: crouch while waiting, the way people do.
	if( FNullEnt( pBot.pBotEnemy ) )
		pEdict->v.button |= IN_DUCK;
}
