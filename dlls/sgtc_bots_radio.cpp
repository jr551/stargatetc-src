// sgtc_bots_radio.cpp — server-side relay between the mod's radio and jk_botti.
//
// Flow, both directions:
//   human `radio <msgId>` verb (sgtc_rules_cmd.cpp)
//     -> addressed MSG_ONE to the selected teammate, unchanged shipped path
//     -> SGTC_Bots_RadioHear(): the bots on the speaker's team react to it
//   bot radio line (bots/jk_botti/bot_radio.cpp)
//     -> SGTC_Bots_RadioSend(): one MSG_ONE per living teammate using the same
//        `Radio` usermessage the client already renders (BYTE msgId, BYTE
//        sender entindex), then SGTC_Bots_RadioHear() so bot teammates listen
//        as well.
//
// No decisions live here: team rate limiting, obey rolls and order lifetimes
// belong to the bot layer. This file only fans a line out and forwards it.

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"

#include "sgtc_bots_radio.h"
#include "sgtc_rules_priv.h"

#include "../game_shared/sgtc_radio.h"
#include "../bots/jk_botti/bot_radio.h"

// Send one radio line from a bot (or anyone else) to every living teammate,
// then let the bots on that team hear it.
void SGTC_Bots_RadioSend( edict_t *speaker, int msgId )
{
	if( FNullEnt( speaker ) || gmsgRadio == 0 )
		return;

	// Only ids the client can render reach the usermessage and the bots.
	if( !SGTC_Radio_IsValidMessage( msgId ) )
		return;

	const int team = (int)speaker->v.team;

	// pev->team uses the entity numbering (1 = Goa'uld, 2 = Torri); anything
	// else is a spectator/unassigned speaker and has no teammates.
	if( team >= 1 && team <= 2 )
	{
		for( int i = 1; i <= gpGlobals->maxClients; i++ )
		{
			CBasePlayer *pl = (CBasePlayer *)UTIL_PlayerByIndex( i );

			if( pl == NULL || !pl->IsAlive() )
				continue;
			if( pl->edict() == speaker )
				continue;
			if( (int)pl->pev->team != team )
				continue;

			// Exactly the human path's message: BYTE msgId, BYTE sender.
			MESSAGE_BEGIN( MSG_ONE, gmsgRadio, NULL, pl->edict() );
				WRITE_BYTE( msgId );
				WRITE_BYTE( ENTINDEX( speaker ) );
			MESSAGE_END();
		}
	}

	// Bots listen even when no human teammate was there to hear the line.
	SGTC_Bots_RadioHear( speaker, msgId );
}

// Forward an already-sent radio line to the bots on the speaker's team. The
// human client command calls this after its own addressed MSG_ONE, so what the
// addressed player receives is untouched.
void SGTC_Bots_RadioHear( edict_t *speaker, int msgId )
{
	if( FNullEnt( speaker ) )
		return;

	// The human `radio` verb takes an arbitrary integer from the client; only
	// real SgtcRadioMessage ids may reach the bot layer.
	if( !SGTC_Radio_IsValidMessage( msgId ) )
		return;

	Vector area;
	qboolean hasArea = FALSE;

	// PUSH lines mean "go there": the nearest objective is the focus point.
	// Every other order is relative to the speaker and needs no area.
	if( SGTC_Radio_OrderForMessage( msgId ) == SGTC_RADIO_ORDER_PUSH )
		hasArea = SGTC_Bots_RadioObjectiveArea( speaker->v.origin, &area );

	BotRadio_OnMessage( speaker, msgId, area, hasArea );
}

// worldspawn: gpGlobals->time restarts near 0 on a new map, so the radio
// deadlines armed on the previous one must go. The stale-deadline check in
// SGTC_Radio_RateAllows is the safety net; this is the clean reset.
void SGTC_Bots_RadioLevelInit( void )
{
	BotRadio_LevelInit();
}
