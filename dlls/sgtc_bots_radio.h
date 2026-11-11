// sgtc_bots_radio.h — server-side glue between the mod's radio and the bots.
//
// This is the only place where the SGTC side talks to the bot layer about
// radio. It reuses the mod's existing `Radio` usermessage (2 bytes: msgId,
// sender entindex), so bots are heard exactly like a human teammate: the same
// HUD line and the same radio/sonNN.wav. No asset or usermessage is added.
//
// Declarations only keep the call sites thin; the implementation is
// sgtc_bots_radio.cpp. `speaker` and `bot` are player edicts.

#pragma once
#ifndef SGTC_BOTS_RADIO_H
#define SGTC_BOTS_RADIO_H

#ifdef BUILD_JK_BOTTI

#include "extdll.h"

// Relay one radio line (an SgtcRadioMessage id) from `speaker` to every living
// teammate, using the existing Radio usermessage, and let the bots on that team
// hear it. Bots use this; the human client command keeps its own addressed
// send and calls SGTC_Bots_RadioHear() instead.
void SGTC_Bots_RadioSend( edict_t *speaker, int msgId );

// Let the bots on `speaker`'s team listen to a line that was already sent.
// Sends nothing; the human `radio` client command calls this after its own
// MSG_ONE so bots react to a player's radio without changing what the player
// sees.
void SGTC_Bots_RadioHear( edict_t *speaker, int msgId );

// worldspawn: drop the radio deadlines (per-team and per-bot) that keep their
// absolute values across a level change while gpGlobals->time restarts near 0.
void SGTC_Bots_RadioLevelInit( void );

// Focus point for PUSH orders: the centre of the nearest live objective to
// `from`, within the objective search radius. Implemented by the objective
// watch in sgtc_bots.cpp. FALSE (and *out untouched) when there is none.
qboolean SGTC_Bots_RadioObjectiveArea( const Vector &from, Vector *out );

// Director interface (thin, no decisions here): hand one bot an order from
// SgtcRadioOrder. `target` is the teammate to follow or help; when it is NULL
// the bot uses the speaker's current position. `chattiness` (default 1.0)
// scales how often that bot uses the radio on its own; 0 mutes it.
void Bot_RadioOrder( edict_t *bot, int order, edict_t *target );
void Bot_RadioChattiness( edict_t *bot, float chattiness );

#endif // BUILD_JK_BOTTI

#endif // SGTC_BOTS_RADIO_H
