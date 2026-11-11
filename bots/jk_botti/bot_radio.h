//
// JK_Botti - be more human!
//
// bot_radio.h
//
// SGTC: the bot side of the mod's radio. Bots hear the lines their teammates
// send through the mod's `Radio` usermessage, turn the ones that carry an order
// into a movement goal and answer with the same radio vocabulary the client
// already renders (game_shared/sgtc_radio.h owns the ids and the decisions).
//
// The vocabulary header is engine-free and shared with the server glue and the
// tests; this header only adds the bot entry points.

#ifndef BOT_RADIO_H
#define BOT_RADIO_H

#include <extdll.h>

#include "../../game_shared/sgtc_radio.h"

#ifdef JK_BOTTI_NATIVE
#include "bot.h"
#endif

// Server-facing entry points. These need nothing but extdll types, so a dlls/
// translation unit that defines BUILD_JK_BOTTI (but not JK_BOTTI_NATIVE) can
// include this header directly. The same declarations live in
// dlls/sgtc_bots_radio.h; either include gives the caller the same prototype.
#if defined(JK_BOTTI_NATIVE) || defined(BUILD_JK_BOTTI)
void BotRadio_OnMessage( edict_t *speaker, int msgId, const Vector &area, qboolean hasArea );

// Map-change reset (worldspawn): clear the absolute deadlines that survive a
// level change while gpGlobals->time restarts near 0. Defined in bot_radio.cpp
// (JK_BOTTI_NATIVE); the server glue calls it through SGTC_Bots_RadioLevelInit.
void BotRadio_LevelInit( void );

void Bot_RadioOrder( edict_t *botEdict, int order, edict_t *target );
void Bot_RadioChattiness( edict_t *botEdict, float chattiness );
#endif

#ifdef JK_BOTTI_NATIVE
// Per-bot lifecycle, driven from bot.cpp.
void BotRadio_SpawnInit( bot_t &pBot );
void BotRadio_Think( bot_t &pBot );
qboolean BotRadio_HandleMovement( bot_t &pBot );
void BotRadio_IssueOrder( bot_t &pBot, int order, edict_t *target, const Vector &area, qboolean hasArea );
void BotRadio_SetChattiness( bot_t &pBot, float chattiness );
int  BotRadio_Chatter( bot_t &pBot, int msgId );   // 1 = queued
#endif

#endif // BOT_RADIO_H
