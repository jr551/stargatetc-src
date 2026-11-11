// sgtc_director.h - server-side "game director" for the SGTC bots.
//
// The director is a two-level controller:
//   * a slow strategic layer asks an OpenAI-compatible endpoint (every
//     sv_botdirector_interval seconds) for a plan: teams, classes, skills,
//     roles, target areas/players, chat lines, pacing;
//   * a fast local layer runs every second, is fully functional without any
//     network, and steers bot skill/accuracy/aggression so each human's duel
//     win chance stays near 50% and their engagement follows the pacing curve.
//
// Wiring from dlls/sgtc_bots.cpp:
//   SGTC_Bots_Init          -> SGTC_Director_Init
//   worldspawn (OnSpawn)    -> SGTC_Director_OnMapStart
//   every frame StartFrame  -> SGTC_Director_StartFrame
//   loadout application     -> SGTC_Director_ForcedTeam / ForcedClass
//   ServerDeactivate/Shutdn -> SGTC_Director_Shutdown
//   player damage/kills     -> SGTC_Director_OnPlayerDamaged / OnPlayerKilled
//   player say              -> SGTC_Director_OnPlayerText
//
// CVars (registered by Init, documented in docs/BOT-DIRECTOR.md):
//   sv_botdirector, sv_botdirector_url, sv_botdirector_model,
//   sv_botdirector_key, sv_botdirector_keyfile, sv_botdirector_interval,
//   sv_botdirector_timeout, sv_botdirector_chat, sv_botdirector_debug,
//   sv_botdirector_max_tokens, sv_botdirector_extra_json.
//
// The API key is never printed: not in console output, cvarlist, status or the
// debug log. The key cvar carries FCVAR_PROTECTED|FCVAR_UNLOGGED|
// FCVAR_PRINTABLEONLY and never FCVAR_SERVER, so it cannot leak through the
// server rules reply, and SGTC_Director_KeyState() reports only set/unset.
#pragma once
#ifndef SGTC_DIRECTOR_H
#define SGTC_DIRECTOR_H

#include "extdll.h"

void SGTC_Director_Init( void );
void SGTC_Director_Shutdown( void );

// Called on worldspawn, before bots spawn onto the new map.
void SGTC_Director_OnMapStart( void );

// Once per server frame, after JK_Botti_StartFrame().
void SGTC_Director_StartFrame( void );

// Team/class the director wants for a bot slot (1..32). 0 = let the normal
// loadout pick decide (sgtc_bots_loadout.cpp).
int SGTC_Director_ForcedTeam( int bot_index );
int SGTC_Director_ForcedClass( int bot_index );

// Weapon slots (indices into the game's 28-entry weapon table) the plan asks
// this bot to carry for `team`/`cls`, filtered by that team/class allow-list
// and de-duplicated. Fills slots[0..cap) and returns the count (0 when the
// plan asks for nothing usable). Called while the bot is dead so the rules
// spawn code grants them on respawn.
int SGTC_Director_BotWeaponSlots( int bot_index, int team, int cls,
	int *slots, int cap );

// Event feeds. Indices are 1-based entity indices; 0 means "unknown/none".
void SGTC_Director_OnPlayerDamaged( int victim_index, int attacker_index,
	int damage, const char *weapon_name );
void SGTC_Director_OnPlayerKilled( int victim_index, int attacker_index,
	const char *weapon_name, int headshot );
void SGTC_Director_OnPlayerText( int player_index, const char *text );

// True when the director wants a bot to answer with a chat line right now.
// Called by the chat layer; returns NULL when no line is due.
const char *SGTC_Director_NextChatLine( int bot_index );

// Key presence without exposing the value: 1 = set, 0 = unset.
int SGTC_Director_KeyState( void );

#endif // SGTC_DIRECTOR_H
