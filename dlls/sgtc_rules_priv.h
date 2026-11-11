// sgtc_rules_priv.h — declarations shared between the sgtc_rules*.cpp and
// sgtc_entities*.cpp files ONLY (#94). The public rules API is sgtc_rules.h; do
// not include this header from other sources.
//
// It holds the extern declarations sgtc_rules.cpp used to keep at the top of its
// 4.6k-line body, and the few helpers/tables that were file-static before the
// split and are now used from more than one of the new files.
#pragma once
#ifndef SGTC_RULES_PRIV_H
#define SGTC_RULES_PRIV_H

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "gamerules.h"
#include "voice_gamemgr.h"
#include "sgtc_rules.h"

// Engine/stock-rules globals and the round/token/player state the rules read.
extern CVoiceGameMgr g_VoiceGameMgr;
extern DLL_GLOBAL BOOL g_fGameOver;
extern int g_teamplay;	// gamerules.cpp
extern int sgtc_GameModeSet;	// sgtc_round.cpp (ref g_mapmode; ref nm 0x1e0ad8)
extern cvar_t timeleft, fragsleft;
extern int gmsgGameMode;
extern int gmsgSayText;
extern int gmsgTeamInfo;
extern int gmsgTeamNames;
extern int gmsgScoreInfo;
extern int gmsgDeathMsg;
extern int sgtc_teamed;	// sgtc_round.cpp (ref BSS teamed @0x23658c)
extern float sgtc_gamemodex;			// sgtc_round.cpp (mp_gamemode numeric mirror)
extern int sgtc_G_players;				// sgtc_round.cpp (ref G_players BSS 0x21455c)
extern int sgtc_T_players;				// sgtc_round.cpp (ref T_players BSS 0x20c7d0)
extern int sgtc_G_players_focused;		// sgtc_round.cpp (ref BSS 0x2368a8)
extern int sgtc_T_players_focused;		// sgtc_round.cpp (ref BSS 0x2368a0)
extern int sgtc_G_players_last;			// sgtc_round.cpp (ref BSS 0x2147bc)
extern int sgtc_T_players_last;			// sgtc_round.cpp (ref BSS 0x21401c)
extern int sgtc_SendPL;					// sgtc_round.cpp (ref m_SendPL)
extern int sgtc_type;	// sgtc_token.cpp (ref BSS type @0x236588)
extern int sgtc_G_tickets;	// sgtc_round.cpp (ref G_tickets BSS 0x236880)
extern int sgtc_T_tickets;	// sgtc_round.cpp (ref T_tickets BSS 0x236884)
extern float sgtc_roundstart;	// sgtc_round.cpp (rules this+0x72c mirror)
extern cvar_t sgtc_cvar_wepselmode;		// sgtc_round.cpp (ref weaponsel @0x223484)
extern cvar_t sgtc_cvar_freeguns;		// sgtc_round.cpp (ref freeguns @0x2238f8)
extern cvar_t sgtc_cvar_attackcountdown;	// sgtc_round.cpp (ref @0x22390c)
extern cvar_t sgtc_cvar_superweapons;	// sgtc_round.cpp (ref superguns @0x2234d4)
extern void SetWeaponAllow( CBasePlayer *pPlayer );	// ref @0x17a26c — reads +0x93c, sends gmsgProgress variants
extern cvar_t *const sgtc_wpnC[SGTC_NUM_WEAPON_SLOTS];	// sgtc_round.cpp — ref wpnC0..wpnC27 cost cvars
extern float sgtc_endtround;	// sgtc_round.cpp (ref BSS endtround @0x2468b8, goal+6s deadline)
extern char sgtc_round_official;			// sgtc_round.cpp (ref BSS 0x236888)
extern float sgtc_nrcooler;				// sgtc_round.cpp (ref BSS 0x2368b4)
extern cvar_t sgtc_cvar_autobalance;		// sgtc_round.cpp (ref autobalance @0x213420)
extern cvar_t sgtc_cvar_classlimitmodel;	// sgtc_round.cpp (ref classlimitmodel @0x2135d8)
extern cvar_t sgtc_cvar_max_syslord;		// ref @0x213650
extern cvar_t sgtc_cvar_max_anubis;			// ref @0x21363c
extern cvar_t sgtc_cvar_max_jaffa;			// ref @0x213628
extern cvar_t sgtc_cvar_max_serpent;		// ref @0x213614
extern cvar_t sgtc_cvar_max_horus;			// ref @0x213600
extern cvar_t sgtc_cvar_max_oniell;			// ref @0x2136b4 (max_oneill)
extern cvar_t sgtc_cvar_max_carter;			// ref @0x2136a0
extern cvar_t sgtc_cvar_max_jackson;		// ref @0x21368c
extern cvar_t sgtc_cvar_max_tealc;			// ref @0x213678
extern cvar_t sgtc_cvar_max_commander;		// ref @0x213664 (max_soldier)
extern int sgtc_count_syslord, sgtc_count_prime, sgtc_count_jaffa,
	sgtc_count_serpent, sgtc_count_horus, sgtc_count_oniell,
	sgtc_count_carter, sgtc_count_jackson, sgtc_count_tealc,
	sgtc_count_commander;					// sgtc_round.cpp — BSS 0x2368f4..0x236918
extern char sgtc_hasplayed;				// sgtc_round.cpp — BSS 0x2368f1
extern int sgtc_LastState[5];			// sgtc_round.cpp — ref BSS 0x236864
extern int sgtc_LastStateI[5];			// sgtc_round.cpp — ref BSS 0x23686c
extern float sgtc_KeyTime;				// sgtc_round.cpp — ref BSS KeyTime
extern char sgtc_BombCarrierLatch;		// sgtc_round.cpp — ref BOMBCARRIER latch
extern float sgtc_classchecktime;			// sgtc_round.cpp — ref classchecktime
extern float sgtc_recounttime;				// sgtc_round.cpp — ref recounttime
extern cvar_t sgtc_cvar_camperdist;		// sgtc_round.cpp — ref camperdist @0x223544
extern cvar_t sgtc_cvar_camptime;			// sgtc_round.cpp — ref camptime @0x223558
extern char sgtc_Elmo;					// sgtc_player.cpp — ref BSS Elmo @0x236890
extern char sgtc_KeyChargeLatch;			// sgtc_player.cpp — ref DAT_00246866
extern int sgtc_bomby;					// sgtc_token.cpp — ref BSS bomby @0x2368f0
extern char sgtc_KeyInPlay;				// sgtc_token.cpp — ref BSS KeyInPlay @0x2368c6
extern int sgtc_GlobalBombState;			// sgtc_token.cpp — ref BSS GlobalBombState
extern Vector sgtc_masterkey;			// sgtc_round.cpp — ref BSS 0x2368c8
extern Vector sgtc_LastKeyPlace;		// sgtc_token.cpp — ref BSS LastKeyPlace
extern Vector sgtc_firstplace;			// sgtc_token.cpp — ref BSS firstplace
extern cvar_t sgtc_cvar_tickets;			// sgtc_round.cpp (ref tickets @0x2133f8)
BOOL SGTC_ThinkRunsMainRoundBody( void );	// ref Think mp_timelimit gate @0x1929ce
void SGTC_ThinkMpTimelimitResetTickets( void );
void SGTC_ThinkRefreshTimeleftCvar( void );

// User messages defined in sgtc_rules_msg.cpp that sgtc_rules.h does not declare.
extern int gmsgGtickets;
extern int gmsgTtickets;
extern int gmsgTicketMode;
extern int gmsgSpectator;
extern int gmsgAddPortrait;
extern int gmsgAboutMe;
extern int gmsgTogglePortrait;
extern int gmsgReset;
extern int gmsgNewRnd;
extern int gmsgSetHudRoundTime;
extern int gmsgVGUIMenu;
extern int gmsgHideVGUI;
extern int gmsgRadio;

// Shared helpers and tables, defined once:
extern const char *const SGTC_TeamSpawnClassname[SGTC_MAX_SGTC_TEAMS];	// sgtc_rules.cpp: team index -> spawn classname
int SGTC_Rules_roundf( float v );	// sgtc_rules_msg.cpp: ref (int)ROUND(float)
int SGTC_ArmorHudWireByte( CBasePlayer *pPlayer );	// sgtc_rules_msg.cpp: armor HUD byte (armor * 100 / cap, clamped)
void SGTC_ApplyUserInfoTeam( CBasePlayer *pPlayer );	// sgtc_rules.cpp: team + model half of DM/CP ClientUserInfoChanged

#endif // SGTC_RULES_PRIV_H
