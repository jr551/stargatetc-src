//
// JK_Botti - be more human!
//
// bot_learn.h
//
// SGTC: the engine side of what the bots learn from the people playing; the
// counting and the controller live in bot_learn_core.h.
//
//   * where people go and fight, kept per map in <map>.heat next to the
//     waypoint file, which a bot with nothing to do walks toward;
//   * where the bots themselves have walked, which keeps a bot that is roaming
//     with no waypoint graph from pacing about the same room.
//
// sgtc_bot_learn 0 turns it off.

#ifndef BOT_LEARN_H
#define BOT_LEARN_H

#include "bot.h"

// worldspawn, after the waypoint file is loaded: read <map>.heat.
void BotLearn_LevelInit(void);

// Every server frame (cheap: it works once a second).
void BotLearn_Frame(void);

// A player died. Every death a person is involved in marks the heat map.
void BotLearn_OnKill(edict_t *victim, edict_t *attacker);

// Write <map>.heat if it changed.
void BotLearn_Save(void);

// A waypoint index for a bot with nothing to do, picked by how much people use
// the places around it. -1 means "no opinion, pick at random as before".
int BotLearn_PickGoal(bot_t &pBot);

// How often bots have walked near here, for roam targets.
float BotLearn_VisitWeight(const Vector &where);
void BotLearn_NoteVisit(bot_t &pBot);

// Server command body: sgtc_learn status | save | clear
void BotLearn_Command(void);

#endif // BOT_LEARN_H
