// sgtc_round.cpp — StargateTC round lifecycle (NEW file).
//
// Clean-room port of the ref round core (ref/stc_i386.so):
//   CStargateTeamDM::PlayerThink  nm 0x17d088 (Ghidra 0x18d088)
//   CStargateTeamCP::PlayerThink  nm 0x17008c (Ghidra 0x18008c)
//   CStargateTeamDM::NewRound     nm 0x184f42 (Ghidra 0x194f42)
//   CStargateTeamCP::NewRound     nm 0x177b34 (Ghidra 0x187b34)
//   CStargateTeamDM::Goal(int)    nm 0x17b650 (Ghidra 0x18b650)
//   CStargateTeamDM::EndRound     nm 0x17b77c (Ghidra 0x18b77c)
//   PlayMusic(int)                nm 0x17b32c (Ghidra 0x18b32c)
// All Ghidra VMA == nm addr + 0x10000.
//
// Ref ABI map (verified from the decoded bodies):
//   gamerules vtable slots: +0x18 Think, +0x28 GetPlayerSpawnSpot,
//     +0x64 FPlayerCanRespawn, +0x94 ChangePlayerTeam,
//     +0x100 SetDefaultPlayerTeam, +0xe0 GetTeamIndex, +0x120 NewRound,
//     +0x34 RedrawPortrait (CBasePlayer*) — PlayerThink itself is +0x64 on
//     the PLAYER vtable, not here.
//   CBasePlayer vtable slots: +0x18 IsAlive, +0x24 (env-probe family call,
//     port: SendValueTo), +0x38 = per-player fade (port UTIL_ScreenFade).
//   CBaseEntity vtable slot +0x128 (ground weapon strip) -> port: solid=NOT
//     re-cache stand-in.
//   CBaseEntity+0x4 = pev. entvars: +0x160 = pev->health, +0x168 = pev->weapons,
//     +0x170 = pev->deadflag, +0x180 = pev->button, +0x1ac = pev->team.

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "satchel.h" // CSatchelCharge::Deactivate
#include "triggers.h" // CMultiManager::Stop
#include "gamerules.h"
#include "teamplay_gamerules.h"
#include "game.h"
#include "saverestore.h"
#include "shake.h"
#include "hltv.h"
#include "sgtc_rules.h"
#include "sgtc_token.h" // CToken::ResetForNewRound
#include "sgtc_misc.h" // CEntSpawn::Event
#include "sgtc_lanceproj.h"
#include "sgtc_zones.h"
#include "sgtc_monsters.h"

extern DLL_GLOBAL BOOL g_fGameOver;
extern void respawn( entvars_t *pev, BOOL fCopyCorpse ); // player.cpp
extern int gmsgScoreInfo;
extern int gmsgTeamScore;
extern int gmsgTeamInfo;
extern int gmsgShowGameTitle;
extern int gmsgNewRnd;
extern int gmsgGtickets;
extern int gmsgTtickets;
extern int gmsgTicketMode;
extern int sgtc_type; // sgtc_token.cpp (ref BSS type @0x236588)
extern Vector sgtc_firstplace; // sgtc_token.cpp (ref BSS firstplace @0x2368d4)
extern char sgtc_KeyInPlay; // sgtc_token.cpp (ref BSS KeyInPlay @0x2368c6)
extern int sgtc_G_players_last; // sgtc_player.cpp (ref BSS 0x2368a0)
extern int sgtc_T_players_last; // sgtc_player.cpp (ref BSS 0x236898)

// ---------------------------------------------------------------------------
// Ref globals (nm addresses of ref/stc_i386.so; all touched only by this TU
// and sgtc_player/sgtc_token/sgtc_zones peers via the externs below).
// ---------------------------------------------------------------------------
int sgtc_GameModeSet = 0;      // nm 0x1e0ad8
float sgtc_gamemodex = -1.0f;  // nm 0x2133bc (mp_gamemode numeric mirror)
float sgtc_tourny = -1.0f;     // nm 0x213574 (mp_tournament)
float sgtc_allowmodevote = -1.0f; // nm 0x213510 (mp_modevoting)
float sgtc_crazymode = -1.0f;  // nm 0x213470 (mp_skulls)
float sgtc_friendlyfire = -1.0f; // nm 0x213358 (mp_friendlyfire mirror)
float sgtc_roundtime = 300.0f; // nm 0x21336c
float sgtc_tickets = 10.0f;    // nm 0x2133d0 (mp_tickets)
float sgtc_endtround = 0.0f;   // BSS 0x2368b8 (goal deadline; checked by Think)
float sgtc_nrcooler = 0.0f;    // BSS 0x2368b4 (NewRound cooldown gate)
char sgtc_hasplayed = 0;       // BSS 0x2368f1 (round played flag)
int sgtc_G_tickets = 0;        // BSS 0x236880
int sgtc_T_tickets = 0;        // BSS 0x236884
int sgtc_G_players_focused = 0;  // BSS 0x2368a8
int sgtc_T_players_focused = 0;  // BSS 0x2368a0
int sgtc_under = 1;            // D 0x2208f4
cvar_t sgtc_fadetoblack = { "mp_fadetoblack", "0", FCVAR_SERVER };	// ref fadetoblack cvar_t @0x213588 — tournament flag, read by PlayerDeathThink fade gate
int sgtc_teamed = 0;           // BSS 0x23658c
float sgtc_roundtime_official = 0.0f; // this+0x72c mirror (round start tick)
Vector sgtc_masterkey( 0, 0, 0 ); // BSS 0x2368c8
int sgtc_lcaptain_T = 0;	// BSS 0x236890 (ref lcaptain_T)
int sgtc_lcaptain_G = 0;	// BSS 0x236894 (ref lcaptain_G)
int sgtc_UnasTeam = 0;		// BSS 0x2368ec (mode 3 invisible-pick team)
int sgtc_LastState[5];         // BSS 0x236864
int sgtc_LastStateI[5];        // BSS 0x23686c
int sgtc_G_players = 0;        // BSS 0x21455c — alive-team-1 count (RecountTeams)
int sgtc_T_players = 0;        // BSS 0x20c7d0 — alive-team-2 count
int sgtc_G_players_last = 0;   // BSS 0x2147bc — previous focused-G count
int sgtc_T_players_last = 0;   // BSS 0x21401c — previous focused-T count
int sgtc_G_lasty = 0;          // D 0x220ad0 — mode-2 ticket HUD latch (ref G_lasty)
int sgtc_T_lasty = 0;          // D 0x220ad4 — mode-2 ticket HUD latch (ref T_lasty)
int sgtc_SendPL = 0;           // BSS — ref m_SendPL (CP InitHUD arms it)
float sgtc_KeyTime = 0.0f;      // ref BSS KeyTime (DM Think key-respawn tick)
char sgtc_BombCarrierLatch = 0; // ref BOMBCARRIER byte latch (DM Think key block)
float sgtc_classchecktime = 0.0f; // ref classchecktime — UpdateClassCount cadence
float sgtc_recounttime = 0.0f;  // ref recounttime — RecountTeams/camper sweep cadence

// Per-class population tallies (BSS 0x2368f4..0x236918) — refilled by
// UpdateClassCount (vtable+0x118) and read by the classe cap switches.
int sgtc_count_syslord = 0;   // BSS 0x236904 (Goa'uld class 1)
int sgtc_count_prime = 0;     // BSS 0x236900 (Goa'uld class 2 — "prime")
int sgtc_count_jaffa = 0;     // BSS 0x2368fc (Goa'uld class 3)
int sgtc_count_serpent = 0;   // BSS 0x2368f8 (Goa'uld class 4)
int sgtc_count_horus = 0;     // BSS 0x2368f4 (Goa'uld class 5)
int sgtc_count_oniell = 0;    // BSS 0x236918 (Torri class 1 — "oniell" in .so)
int sgtc_count_carter = 0;    // BSS 0x236914 (Torri class 2)
int sgtc_count_jackson = 0;   // BSS 0x236910 (Torri class 3)
int sgtc_count_tealc = 0;     // BSS 0x23690c (Torri class 4)
int sgtc_count_commander = 0; // BSS 0x236908 (Torri class 5)

// ---------------------------------------------------------------------------
// Ref cvar table (12-byte .data blobs: {const char *name, const char *string,
// int flags}) — the .so reads/writes the string pointers directly:
//   friendlyfire @0x213358 mp_friendlyfire, roundtime @0x21336c mp_roundtime,
//   gamemodex @0x2133bc mp_gamemode, tickets @0x2133d0 mp_tickets,
//   crazymode @0x213470 mp_skulls, allowmodevote @0x213510 mp_modevoting,
//   tourny @0x213574 mp_tournament, superguns @0x2134d4 mp_superweapons.
// Ported as cvar_t over the stock registered cvars so NewRound/PlayerThink
// see live engine values (identical 32-bit layout).
// ---------------------------------------------------------------------------
cvar_t sgtc_cvar_roundtime = { "mp_roundtime", "300", FCVAR_SERVER };
cvar_t sgtc_cvar_gamemode = { "mp_gamemode", "0", FCVAR_SERVER };	// ref gamemodex cvar_t @0x2133bc name "mp_gamemode" default '0'
cvar_t sgtc_cvar_tickets = { "mp_tickets", "10", FCVAR_SERVER };
cvar_t sgtc_cvar_skulls = { "mp_skulls", "0", FCVAR_SERVER };
cvar_t sgtc_cvar_modevoting = { "mp_modevoting", "1", FCVAR_SERVER };	// ref allowmodevote cvar_t @0x213510 — name "mp_modevoting", default '1'
cvar_t sgtc_cvar_tournament = { "mp_tournament", "0", FCVAR_SERVER };	// ref tourny cvar_t @0x213574
cvar_t sgtc_cvar_superweapons = { "mp_superweapons", "0", FCVAR_SERVER };
cvar_t sgtc_cvar_particlelatency = { "sv_particlelatency", "3", FCVAR_SERVER };	// ref particlelat cvar_t @0x213498 — name "sv_particlelatency", default '3'
cvar_t sgtc_cvar_respawntime = { "mp_respawntime", "5", FCVAR_SERVER };	// ref respawntime cvar_t @0x2133e4 — default '5'
cvar_t sgtc_cvar_diepoints = { "mp_diepoints", "2", FCVAR_SERVER };	// ref vicpoints @0x213524 — cvar_t.string "2"
cvar_t sgtc_cvar_killpoints = { "mp_killpoints", "1", FCVAR_SERVER };	// ref killpoints @0x213538 — cvar_t.string "1"
cvar_t sgtc_cvar_truezat = { "mp_truezat", "0", FCVAR_SERVER };	// ref truezat @0x2368d4
cvar_t sgtc_cvar_oldzat = { "oldzat", "0", FCVAR_SERVER };	// ref oldzat @0x21359c — CZatProj +0x40c long-beam gate
cvar_t sgtc_cvar_wepselmode = { "mp_wepselmode", "1", FCVAR_SERVER };	// ref weaponsel @0x223484 — spawn menu mode; ref cvar name @0x1e0f51 registers default "1" (@0x1dfabd)
cvar_t sgtc_cvar_freeguns = { "mp_freeguns", "0", FCVAR_SERVER };		// ref freeguns @0x2238f8 — CP spawn grants all slots when >1
cvar_t sgtc_cvar_attackcountdown = { "mp_attackcountdown", "30", FCVAR_SERVER };	// ref attackcountdown @0x22390c
cvar_t sgtc_cvar_camperdist = { "mp_camperdistance", "400", FCVAR_SERVER };	// ref camperdist @0x223544 — cvar_t.string "400"
cvar_t sgtc_cvar_camptime = { "mp_campertime", "30", FCVAR_SERVER };	// ref camptime @0x223558 — cvar_t.string "30"
cvar_t sgtc_cvar_devkey = { "sgtc_devkey", "", FCVAR_PROTECTED };	// dev-verb unlock key (#29): empty = dev mode disabled; set only via server console/rcon
cvar_t sgtc_svcrazy = { "sv_crazy", "0", FCVAR_SERVER };	// ref sv_crazy cvar_t @0x213920 — tacgun scan/ammo gate (0 = rules on)
cvar_t sgtc_tacdist = { "mp_tacdistance", "600", FCVAR_SERVER };	// ref mp_tacdistance cvar_t @0x21345c — tac separation clamp(50..8000)
// Range bubble shown to the tac owner after placing / while looking at it.
cvar_t sgtc_tac_bubble = { "sgtc_tac_bubble", "1", FCVAR_SERVER };
cvar_t sgtc_cvar_roundforcing = { "mp_roundforcing", "0", FCVAR_SERVER };	// ref roundforcing cvar_t @0x213448 — NewRound force trigger (read in CStargateTeamDM::UpdateTeamList)
cvar_t sgtc_cvar_flagholdtime = { "mp_flagholdtime", "90", FCVAR_SERVER };	// ref @0x2124c0 — register-only (no ref .value read)
cvar_t sgtc_cvar_structhp = { "mp_structhp", "0", FCVAR_SERVER };		// ref @0x212434 — register-only
cvar_t sgtc_cvar_spawnsafetime = { "mp_spawnsafetime", "6", FCVAR_SERVER };	// ref @0x2124ac — register-only
cvar_t sgtc_cvar_simplezat = { "mp_simplezat", "0", FCVAR_SERVER };		// ref @0x21259c — register-only
cvar_t sgtc_cvar_instantdeath = { "mp_instantdeath", "0", FCVAR_SERVER };	// ref @0x2124e8 — register-only


// ref ClientCommand 'classe' reads these cvar_t blobs live (@0x213420
// autobalance, @0x2135d8 mp_altclasslimiter, @0x213600..0x2136b4 class caps):
cvar_t sgtc_cvar_resetround = { "mp_resetround", "0", FCVAR_SERVER };	// ref resetround @0x21340c (Think pulse)
cvar_t sgtc_cvar_roundspermap = { "mp_roundspermap", "2", FCVAR_SERVER };	// ref roundspermap @0x2133f8
cvar_t sgtc_cvar_autobalance = { "mp_autobalance", "2", FCVAR_SERVER };	// ref autobalance @0x213420
cvar_t sgtc_cvar_classlimitmodel = { "mp_altclasslimiter", "0", FCVAR_SERVER };	// ref classlimitmodel @0x2135d8
cvar_t sgtc_cvar_max_syslord = { "max_syslord", "1", FCVAR_SERVER };	// ref @0x213650
cvar_t sgtc_cvar_max_anubis = { "max_anubis", "3", FCVAR_SERVER };	// ref @0x21363c (counts vs count_prime)
cvar_t sgtc_cvar_max_jaffa = { "max_jaffa", "0", FCVAR_SERVER };		// ref @0x213628
cvar_t sgtc_cvar_max_serpent = { "max_serpent", "1", FCVAR_SERVER };	// ref @0x213614
cvar_t sgtc_cvar_max_horus = { "max_horus", "3", FCVAR_SERVER };		// ref @0x213600
cvar_t sgtc_cvar_max_oniell = { "max_oneill", "1", FCVAR_SERVER };	// ref @0x2136b4
cvar_t sgtc_cvar_max_carter = { "max_carter", "1", FCVAR_SERVER };	// ref @0x2136a0
cvar_t sgtc_cvar_max_jackson = { "max_jackson", "1", FCVAR_SERVER };	// ref @0x21368c
cvar_t sgtc_cvar_max_tealc = { "max_tealc", "1", FCVAR_SERVER };		// ref @0x213678
cvar_t sgtc_cvar_max_commander = { "max_soldier", "0", FCVAR_SERVER };	// ref @0x213664

// ---------------------------------------------------------------------------
// wpnC0..wpnC27 — ref .data cvar_t blobs @0x2236c8..0x2238e4 (20 B each:
// {name, default-string, FCVAR_SERVER=4, value, next}), read by the DM/CP
// Think m_SendPL block as ROUND(value) and written as the 28-byte cost tail of
// the TeamNames usermsg (menu-12 per-slot costs, client DAT_100dd974[28]).
// Names/defaults read from the ref .data + its rodata string pool
// (0x1e0ee9..0x1e12e2); defaults cross-checked against the orig menu-12
// screenshot (USAS 40, Beretta 5, MP5 7, PSG1 11, P90 9, M16 8, Zat 6,
// Lance 7, Hand Grenade 6, Mine 7, C4 13, Smoke 5, Nerve 6, Narco 6,
// Mustard 7).
// ---------------------------------------------------------------------------
cvar_t sgtc_cvar_wpnC0 = { "wpn_weapon_glstaff", "40", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC1 = { "wpn_weapon_usas", "40", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC2 = { "wpn_weapon_batsup", "3", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC3 = { "wpn_weapon_m92", "5", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC4 = { "wpn_weapon_mp5", "7", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC5 = { "wpn_weapon_psg1", "11", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC6 = { "wpn_weapon_p90", "9", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC7 = { "wpn_weapon_spas", "8", FCVAR_SERVER }; // ref cvar_t @0x212858 default '8' (game.cfg overrides to 10)
cvar_t sgtc_cvar_wpnC8 = { "wpn_weapon_m16", "8", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC9 = { "wpn_weapon_main", "8", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC10 = { "wpn_weapon_zat", "6", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC11 = { "wpn_weapon_lance", "7", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC12 = { "wpn_weapon_reetou", "9", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC13 = { "wpn_weapon_zatarc", "11", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC14 = { "wpn_weapon_tacgun", "6", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC15 = { "wpn_weapon_handgrenade", "6", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC16 = { "wpn_weapon_mine", "7", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC17 = { "wpn_weapon_invis", "8", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC18 = { "wpn_weapon_shield", "8", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC19 = { "wpn_weapon_gacgoa", "7", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC20 = { "wpn_weapon_c4", "13", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC21 = { "wpn_weapon_larve", "9", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC22 = { "wpn_weapon_medic", "8", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC23 = { "wpn_weapon_smokegrenade", "5", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC24 = { "wpn_weapon_nervegrenade", "6", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC25 = { "wpn_weapon_narcogrenade", "6", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC26 = { "wpn_weapon_mustardgrenade", "7", FCVAR_SERVER };
cvar_t sgtc_cvar_wpnC27 = { "wpn_weapon_goaregen", "7", FCVAR_SERVER };
// Index-ordered accessors for the cost tail (slot i ↔ wpnC<i>).
extern cvar_t *const sgtc_wpnC[SGTC_NUM_WEAPON_SLOTS] =
{
	&sgtc_cvar_wpnC0,  &sgtc_cvar_wpnC1,  &sgtc_cvar_wpnC2,  &sgtc_cvar_wpnC3,
	&sgtc_cvar_wpnC4,  &sgtc_cvar_wpnC5,  &sgtc_cvar_wpnC6,  &sgtc_cvar_wpnC7,
	&sgtc_cvar_wpnC8,  &sgtc_cvar_wpnC9,  &sgtc_cvar_wpnC10, &sgtc_cvar_wpnC11,
	&sgtc_cvar_wpnC12, &sgtc_cvar_wpnC13, &sgtc_cvar_wpnC14, &sgtc_cvar_wpnC15,
	&sgtc_cvar_wpnC16, &sgtc_cvar_wpnC17, &sgtc_cvar_wpnC18, &sgtc_cvar_wpnC19,
	&sgtc_cvar_wpnC20, &sgtc_cvar_wpnC21, &sgtc_cvar_wpnC22, &sgtc_cvar_wpnC23,
	&sgtc_cvar_wpnC24, &sgtc_cvar_wpnC25, &sgtc_cvar_wpnC26, &sgtc_cvar_wpnC27,
};
void SGTC_RegisterRulesCvars( void )
{
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_roundtime );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_gamemode );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_tickets );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_skulls );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_modevoting );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_tournament );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_superweapons );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_particlelatency );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_respawntime );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_diepoints );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_killpoints );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_truezat );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_oldzat );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_wepselmode );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_freeguns );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_attackcountdown );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_camperdist );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_camptime );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_devkey );
	SGTC_RegisterOrAdoptCvar( &sgtc_svcrazy );
	SGTC_RegisterOrAdoptCvar( &sgtc_tacdist );
	SGTC_RegisterOrAdoptCvar( &sgtc_tac_bubble );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_resetround );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_roundspermap );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_autobalance );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_classlimitmodel );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_max_syslord );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_max_anubis );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_max_jaffa );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_max_serpent );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_max_horus );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_max_oniell );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_max_carter );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_max_jackson );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_max_tealc );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_max_commander );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_roundforcing );
	SGTC_RegisterOrAdoptCvar( &sgtc_fadetoblack );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_flagholdtime );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_structhp );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_spawnsafetime );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_simplezat );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_instantdeath );
	for( int i = 0; i < SGTC_NUM_WEAPON_SLOTS; i++ )
		SGTC_RegisterOrAdoptCvar( sgtc_wpnC[i] );
}

float sgtc_roundstart = 0.0f; // mirror of rules this+0x72c
char sgtc_round_official = 0; // ref BSS round_official (byte)
Vector sgtc_mKill( 0, 0, 0 );

// ---------------------------------------------------------------------------
// Local decode helpers (ref @0x1abc1c / 0x1ab90e / 0x1ab9c9).
// ---------------------------------------------------------------------------
int SGTC_Round_intconstrain( int lo, int hi, int v )
{
	return v < lo ? lo : ( v > hi ? hi : v );
}

static int SGTC_Round_roundf( float v )
{
	return (int)floorf( v + 0.5f );
}

static int SGTC_Round_rand( int lo, int hi )
{
	return RANDOM_LONG( lo, hi );
}

static bool SGTC_Round_vne( const Vector &a, const Vector &b )
{
	return a.x != b.x || a.y != b.y || a.z != b.z;
}

// GiveFromMenu — ref nm 0x17ae8a (free function; body owned by the player
// helpers slice, which implements the SGTC menu loadout).
void SGTC_GiveFromMenu( CBasePlayer *pPlayer );

// ---------------------------------------------------------------------------
// PlayMusic(int) — nm 0x17b32c (Ghidra 0x18b32c).
// MESSAGE_BEGIN(MSG_ALL, gmsgShowGameTitle) / WRITE_BYTE(mode+1) / MESSAGE_END,
// then the Torri team spawn point re-plays the randomised round sample
// (music/tau01-03 = mode 0, goa01-03 = mode 1, draw01-03 = mode 2; rodata
// 0x1dbae0..0x1dbb74, byte-verified "info_player_torri" + tau/tau/draw order).
// ---------------------------------------------------------------------------
void SGTC_PlayMusic( int iMode )
{
	MESSAGE_BEGIN( MSG_ALL, gmsgShowGameTitle );
		WRITE_BYTE( iMode + 1 );
	MESSAGE_END();

	CBaseEntity *pSpot = UTIL_FindEntityByClassname( NULL, "info_player_torri" );
	// The ref dereferences the found entity unguarded; DM maps always
	// contain info_player_torri. Guard is an intentional port addition.
	if( !pSpot )
		return;

	int iPick = RANDOM_LONG( 1, 3 );
	const char *pszSample = NULL;
	if( iMode == 0 )
	{
		if( iPick == 1 ) pszSample = "music/tau01.wav";
		else if( iPick == 2 ) pszSample = "music/tau02.wav";
		else if( iPick == 3 ) pszSample = "music/tau03.wav";
	}
	else if( iMode == 1 )
	{
		if( iPick == 1 ) pszSample = "music/goa01.wav";
		else if( iPick == 2 ) pszSample = "music/goa02.wav";
		else if( iPick == 3 ) pszSample = "music/goa03.wav";
	}
	else if( iMode == 2 )
	{
		if( iPick == 1 ) pszSample = "music/draw01.wav";
		else if( iPick == 2 ) pszSample = "music/draw02.wav";
		else if( iPick == 3 ) pszSample = "music/draw03.wav";
	}
	if( pszSample )
		EMIT_SOUND( ENT( pSpot->pev ), 6, pszSample, 1.0, 0.0 );
}


void SGTC_GlobalText( int iTeam, int iText )
{
	// ref CStargateTeamDM::GlobalText @nm 0x17c222: alive players only
	// (vtable+0xa8); iTeam==0 -> all alive; else pev->team (+0x1ac) == iTeam;
	// fan-out via SendGlobalTextIndex (gmsgConcuss byte0=0, byte1=index).
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( !pPlayer || !pPlayer->IsAlive() )
			continue;
		if( iTeam != 0 && (int)pPlayer->pev->team != iTeam )
			continue;
		pPlayer->SendGlobalTextIndex( iText );
	}
}

void SGTC_GlobalTextAll( int iText )
{
	SGTC_GlobalText( 0, iText );
}

// ---------------------------------------------------------------------------
// EndRound — nm 0x17b77c (Ghidra 0x18b77c).
// Per-team gmsgTeamScore {WRITE_STRING team name, WRITE_SHORT score,
// WRITE_SHORT 0}, then per alive player gmsgScoreInfo
// {BYTE entindex, SHORT frags, SHORT +0xdf0 (deaths),
//  SHORT +0x4de == 1 ? 6 : +0xa40, SHORT GetTeamIndex(+0xe08)+1,
//  SHORT pev+0x170}, then virtual +0x120 == CStargateTeamDM::NewRound(self).
// ---------------------------------------------------------------------------
static void SGTC_EndRoundScoreInfo( CBasePlayer *pPlayer )
{
	MESSAGE_BEGIN( MSG_ALL, gmsgScoreInfo );
		WRITE_BYTE( pPlayer->entindex() );
		WRITE_SHORT( SGTC_Round_roundf( pPlayer->pev->frags ) );	// ref round(pev+0x164)
		WRITE_SHORT( pPlayer->m_iDeaths ); // ref +0xdf0
		// ref @0x17b91a: if ((player+0x4de ^ 1) != 0) write +0xa40 else write 6.
		WRITE_SHORT( pPlayer->m_bSnagInvisLatch != 1 ? pPlayer->m_iSgtcPortraitShown
				: SGTC_CLASS_UNAS ); // ref +0x4de / +0xa40
		WRITE_SHORT( g_pGameRules->GetTeamIndex( pPlayer->TeamID() ) + 1 );
		WRITE_SHORT( (int)pPlayer->pev->team ); // ref pev+0x170
	MESSAGE_END();
}

void CStargateTeamDM::EndRound( void )
{
	m_bRoundOver = TRUE;

	for( int i = 0; i < m_iSGTCTeamCount; i++ )
	{
		MESSAGE_BEGIN( MSG_ALL, gmsgTeamScore );
			WRITE_STRING( m_szSGTCTeams[i] );
			WRITE_SHORT( m_iSGTCTeamScores[i] );
			WRITE_SHORT( 0 );
		MESSAGE_END();
	}

	CBaseEntity *pEnt = NULL;
	while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "player" ) ) != NULL )
	{
		if( pEnt->IsAlive() )
			SGTC_EndRoundScoreInfo( (CBasePlayer *)pEnt );
	}

	NewRound();
}

// ---------------------------------------------------------------------------
// Goal(int) — nm 0x17b650 (Ghidra 0x18b650).
// Guards: rules+0x73c == 0 (round restart not armed), sgtc_hasplayed == 0 and
// GameModeSet in {0, 2}. Team 1 (0x1dbb90 "Goa'uld"): KeyInPlay = 0,
// PlayMusic(1), endtround = now + 6.0 (rodata 0x1dbba0), team score++. Team 2
// (0x1dbb98 "Torri"): PlayMusic(0), team score++. Always: hasplayed = 1.
// ---------------------------------------------------------------------------
void CStargateTeamDM::Goal( int iTeam )
{
	// ref @0x18b650: guards on rules+0x73c (m_iSgtcRoundTicks; DM +0x740 is
	// m_bRoundOver used only in FPlayerCanRespawn), hasplayed (BSS 0x2368f1),
	// GameModeSet==0 || ==2.
	if( m_iSgtcRoundTicks != 0 || sgtc_hasplayed != 0 )
		return;
	if( sgtc_GameModeSet != 0 && sgtc_GameModeSet != 2 )
		return;

	// iTeam is an SgtcEntTeam (pev->team numbering): 1 = Goa'uld, 2 = Torri.
	if( iTeam == SGTC_ENTTEAM_GOAULD )
	{
		// ref @0x18b6a7: KeyInPlay(BSS 0x2368c6) = 0 first, then PlayMusic(1).
		sgtc_KeyInPlay = 0;
		SGTC_PlayMusic( 1 );
		// ref @0x18b6c5: endtround = gpGlobals->time + 6.0f (rodata 0x1dbba0).
		sgtc_endtround = gpGlobals->time + 6.0f;
		m_iSGTCTeamScores[GetTeamIndex( "Goa'uld" )]++;	// ref &DAT_0x1ebb90
	}
	else if( iTeam == SGTC_ENTTEAM_TORRI )
	{
		// ref @0x18b70e: KeyInPlay = 0 here too.
		sgtc_KeyInPlay = 0;
		SGTC_PlayMusic( 0 );
		sgtc_endtround = gpGlobals->time + 6.0f;
		m_iSGTCTeamScores[GetTeamIndex( "Torri" )]++;	// ref "Torri" @0x1ebb98
	}

	// ref @0x18b76d: hasplayed even when iTeam is neither 1 nor 2.
	sgtc_hasplayed = 1;
}

// ---------------------------------------------------------------------------
// Think mp_timelimit outer gate — ref @0x1929ce before fraglimit/PlayMusic block.
// Main body when: no timelimit, time < limit, or GameModeSet!=0 &&
// (GameModeSet<1 || time<=endtround || hasplayed!=1). Else: ticket reset + +0x108.
// ---------------------------------------------------------------------------
BOOL SGTC_ThinkRunsMainRoundBody( void )
{
	float flTimeLimit = CVAR_GET_FLOAT( "mp_timelimit" ) * 60.0f;
	if( flTimeLimit == 0.0f || gpGlobals->time < flTimeLimit )
		return TRUE;
	if( sgtc_GameModeSet != 0 &&
	    ( sgtc_GameModeSet < 1 || gpGlobals->time <= sgtc_endtround || sgtc_hasplayed != 1 ) )
		return TRUE;
	return FALSE;
}

void SGTC_ThinkMpTimelimitResetTickets( void )
{
	sgtc_G_tickets = SGTC_Round_intconstrain( 0, 99,
		SGTC_Round_roundf( sgtc_cvar_tickets.value ) );
	sgtc_T_tickets = sgtc_G_tickets;
}

// ref Think @0x1929ce: mp_timeleft cvar refresh at end of gated main body only.
void SGTC_ThinkRefreshTimeleftCvar( void )
{
	extern cvar_t timeleft;

	static int s_last_time;
	float flTimeLimit = CVAR_GET_FLOAT( "mp_timelimit" ) * 60.0f;
	int iRemain = 0;
	if( flTimeLimit != 0.0f )
		iRemain = SGTC_Round_roundf( flTimeLimit - gpGlobals->time );

	if( timeleft.value != (float)s_last_time )
		g_engfuncs.pfnCvar_DirectSet( &timeleft, UTIL_VarArgs( "%i", iRemain ) );

	s_last_time = iRemain;
}

// ---------------------------------------------------------------------------
// Captain stamp base (SetCap / Think). The ref reads gpGlobals+0x98 as an int,
// which in the engine's globalvars_t is pStringBase (the map's string pool),
// not a death counter: SetCap stores base + entindex for the chosen captain and
// the mode 4 scan compares every player's base + entindex against it, so all it
// needs is a value that is stable for the life of the map. The string base
// pointer truncated to int is exactly what the 32-bit ref read; a raw byte
// offset would land on the wrong field in a 64-bit build.
static int SGTC_CaptainStampBase( void )
{
	return (int)(size_t)gpGlobals->pStringBase;
}

// ref SetCap(int) @0x18b0c8 (nm 0x17b0c8).
// iSide is an SgtcEntTeam (pev->team numbering) or SGTC_SETCAP_BOTH.
#define SGTC_SETCAP_BOTH 3
static void SGTC_SetCap( int iSide )
{
	// (#95: the counters used to be named the other way round; pev->team 1 is
	// Goa'uld and the ref draws the team-1 pick first, so that order is kept.)
	int nGoauld = 0;
	int nTorri = 0;
	CBaseEntity *pEnt = NULL;

	while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "player" ) ) != NULL )
	{
		CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
		if( !pPlayer->IsAlive() || pPlayer->IsObserver() )
			continue;
		if( pPlayer->pev->team == SGTC_ENTTEAM_GOAULD )
			nGoauld++;
		if( pPlayer->pev->team == SGTC_ENTTEAM_TORRI )
			nTorri++;
	}

	const int iPickGoauld = RANDOM_LONG( 1, nGoauld );
	const int iPickTorri = RANDOM_LONG( 1, nTorri );
	int iGoauldSeen = 0;
	int iTorriSeen = 0;

	pEnt = NULL;
	while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "player" ) ) != NULL )
	{
		CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
		if( !pPlayer->IsAlive() || pPlayer->IsObserver() )
			continue;

		if( pPlayer->pev->team == SGTC_ENTTEAM_GOAULD &&
			( iSide == SGTC_ENTTEAM_GOAULD || iSide == SGTC_SETCAP_BOTH ) )
		{
			iGoauldSeen++;
			if( iGoauldSeen == iPickGoauld )
			{
				pPlayer->m_bSgtcPePreset = 1;
				sgtc_lcaptain_G = SGTC_CaptainStampBase() + pPlayer->entindex();
			}
			else
				pPlayer->m_bSgtcPePreset = 0;
		}

		if( pPlayer->pev->team == SGTC_ENTTEAM_TORRI &&
			( iSide == SGTC_ENTTEAM_TORRI || iSide == SGTC_SETCAP_BOTH ) )
		{
			iTorriSeen++;
			if( iTorriSeen == iPickTorri )
			{
				sgtc_lcaptain_T = SGTC_CaptainStampBase() + pPlayer->entindex();
				pPlayer->m_bSgtcPePreset = 1;
			}
			else
				pPlayer->m_bSgtcPePreset = 0;
		}
	}
}

// ref Think @0x1929ce — GameModeSet==3 (ctp invis pick) and ==4 (captain scan).
void CStargateTeamDM::SGTC_ThinkGameMode34Side( void )
{
	if( sgtc_GameModeSet == 3 && m_flSgtcUnasPickTime < gpGlobals->time )
	{
		BOOL bAnyInvis = FALSE;
		CBaseEntity *pEnt = NULL;
		while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "player" ) ) != NULL )
		{
			CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
			if( pPlayer->m_bSnagInvisLatch || pPlayer->m_bSnagInvisFlag )
				bAnyInvis = TRUE;
		}

		if( !bAnyInvis )
		{
			if( sgtc_UnasTeam == 0 )
				sgtc_UnasTeam = RANDOM_LONG( SGTC_ENTTEAM_GOAULD, SGTC_ENTTEAM_TORRI );	// compared with pev->team

			int nOnTeam = 0;
			pEnt = NULL;
			while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "player" ) ) != NULL )
			{
				CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
				if( pPlayer->pev->team == sgtc_UnasTeam )
					nOnTeam++;
			}

			const int iPick = RANDOM_LONG( 1, nOnTeam );
			int iSeen = 0;
			pEnt = NULL;
			while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "player" ) ) != NULL )
			{
				CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
				pPlayer->m_bSnagInvisFlag = 0;
				if( pPlayer->pev->team == sgtc_UnasTeam )
				{
					iSeen++;
					if( iSeen == iPick )
						pPlayer->m_bSnagInvisFlag = 1;
				}
			}
		}

		m_flSgtcUnasPickTime = gpGlobals->time + 2.0f;
	}

	if( sgtc_GameModeSet == 4 && m_flSgtcCapScanTime < gpGlobals->time )
	{
		int nCapGoauld = 0;
		int nCapTorri = 0;
		CBaseEntity *pEnt = NULL;
		while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "player" ) ) != NULL )
		{
			CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
			const int iStamp = SGTC_CaptainStampBase() + pPlayer->entindex();
			if( pPlayer->pev->team == SGTC_ENTTEAM_GOAULD && iStamp == sgtc_lcaptain_G )
			{
				pPlayer->m_bSgtcPePreset = 1;
				nCapGoauld++;
			}
			else if( pPlayer->pev->team == SGTC_ENTTEAM_TORRI && iStamp == sgtc_lcaptain_T )
			{
				pPlayer->m_bSgtcPePreset = 1;
				nCapTorri++;
			}
			else
				pPlayer->m_bSgtcPePreset = 0;
		}

		if( nCapGoauld != 1 && sgtc_G_players > 0 )
			SGTC_SetCap( SGTC_ENTTEAM_GOAULD );
		if( nCapTorri != 1 && sgtc_T_players > 0 )
			SGTC_SetCap( SGTC_ENTTEAM_TORRI );

		m_flSgtcCapScanTime = gpGlobals->time + 2.0f;
	}
}

// Think mode 0 — ref @0x1929ce gated body: mp_fraglimit on team_scores → +0x108,
// elimination tick, then +0x724/+0x728 → +0x38 or PlayMusic stingers (+4s endtround).
// ---------------------------------------------------------------------------
BOOL CStargateTeamDM::SGTC_ThinkGameMode0( void )
{
	extern cvar_t fraglimit;
	extern cvar_t fragsleft;

	float flFragLimit = fraglimit.value;
	if( flFragLimit != 0.0f )
	{
		int iBestRemain = 9999;
		for( int t = 0; t < m_iSGTCTeamCount; t++ )
		{
			if( flFragLimit <= (float)m_iSGTCTeamScores[t] )
			{
				GoToIntermission();
				return TRUE;
			}
			int iRemain = SGTC_Round_roundf( flFragLimit - (float)m_iSGTCTeamScores[t] );
			if( iRemain < iBestRemain )
				iBestRemain = iRemain;
		}
		static int s_lastFragsRemain;
		if( iBestRemain != s_lastFragsRemain )
			g_engfuncs.pfnCvar_DirectSet( &fragsleft, UTIL_VarArgs( "%i", iBestRemain ) );
		s_lastFragsRemain = iBestRemain;
	}

	int totalClients = 0;
	int aliveTeamIdx[32];
	int presentTeamIdx[32];
	int nAliveTeams = 0;
	int nPresentTeams = 0;
	for( int t = 0; t < 32; t++ )
	{
		aliveTeamIdx[t] = -5;
		presentTeamIdx[t] = -5;
	}

	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( !pPlayer || !IsValidTeam( pPlayer->TeamID() ) )
			continue;
		totalClients++;
		if( !pPlayer->IsAlive() || pPlayer->m_bSgtcBadGuy )
			continue;

		int iTeam = GetTeamIndex( pPlayer->TeamID() );
		if( iTeam < 0 )
			continue;

		BOOL bNewAlive = TRUE;
		for( int a = 0; a < nAliveTeams; a++ )
		{
			if( aliveTeamIdx[a] == iTeam )
			{
				bNewAlive = FALSE;
				break;
			}
		}
		if( bNewAlive )
			aliveTeamIdx[nAliveTeams++] = iTeam;

		BOOL bNewPresent = TRUE;
		for( int p = 0; p < nPresentTeams; p++ )
		{
			if( presentTeamIdx[p] == iTeam )
			{
				bNewPresent = FALSE;
				break;
			}
		}
		if( bNewPresent )
			presentTeamIdx[nPresentTeams++] = iTeam;
	}

	if( nPresentTeams < 2 && m_bRoundOver != 0 )
		m_bRoundOver = 1;
	if( nPresentTeams > 1 )
		m_bRoundOver = 2;

	if( m_bRoundOver == 1 && nAliveTeams > 1 )
	{
		NewRound();
		return FALSE;
	}

	if( ( m_bRoundOver < 2 || nAliveTeams > 1 ) &&
		( nAliveTeams != 0 || totalClients == 0 ) )
	{
		if( m_iSgtcWinScoreSide == 0 )
			m_iSgtcRoundTicks = 0;
	}
	else
	{
		if( nAliveTeams != 0 )
		{
			m_iSgtcFirstTicks = aliveTeamIdx[0];
			m_iSgtcWinPending = 1;
		}
		if( m_iSgtcRoundTicks == 0 )
		{
			m_flSgtcRoundTickStart = gpGlobals->time;
			m_iSgtcRoundTicks = 1;
		}
	}

	const BOOL bRoundWindow =
		( m_flSgtcRoundTime + ( gpGlobals->time - m_flSgtcRoundEnd ) > 20.0f );
	const BOOL bRoundDue =
		( m_flSgtcRoundEnd != 0.0f && gpGlobals->time >= m_flSgtcRoundEnd &&
		  m_flSgtcRoundTime != 0.0f );
	const BOOL bElimPastTick =
		( m_iSgtcRoundTicks != 0 &&
		  gpGlobals->time - m_flSgtcRoundTickStart > 5.0f );

	if( ( bRoundDue || ( bElimPastTick && bRoundWindow ) ) )
	{
		EndRound();
	}
	else if( m_iSgtcRoundTicks != 0 &&
		gpGlobals->time - m_flSgtcRoundTickStart < 5.0f && bRoundWindow )
	{
		if( m_iSgtcWinScoreSide == SGTC_TEAM_TORRI )
			m_iSGTCTeamScores[GetTeamIndex( "Torri" )]++;
		else if( m_iSgtcWinScoreSide == SGTC_TEAM_GOAULD )
			m_iSGTCTeamScores[GetTeamIndex( "Goa'uld" )]++;

		if( m_iSgtcWinPending == 0 || m_iSgtcFirstTicks == -5 )
		{
			if( sgtc_hasplayed == 0 )
			{
				SGTC_PlayMusic( 2 );
				sgtc_endtround = gpGlobals->time + 4.0f;
				sgtc_hasplayed = 1;
			}
		}
		else if( sgtc_hasplayed == 0 )
		{
			if( m_iSgtcFirstTicks == 0 )
			{
				sgtc_endtround = gpGlobals->time + 4.0f;
				m_iSGTCTeamScores[GetTeamIndex( "Torri" )]++;
				SGTC_PlayMusic( 0 );
			}
			if( m_iSgtcFirstTicks == 1 )
			{
				sgtc_endtround = gpGlobals->time + 4.0f;
				m_iSGTCTeamScores[GetTeamIndex( "Goa'uld" )]++;
				SGTC_PlayMusic( 1 );
			}
			sgtc_hasplayed = 1;
		}
	}

	return FALSE;
}

// Think PlayMusic paths — ref @0x1929ce ticket stingers (modes 1/4). Mode 0 is
// SGTC_ThinkGameMode0(). Goal() keeps separate @0x18b650 paths.
// ---------------------------------------------------------------------------
BOOL CStargateTeamDM::SGTC_ThinkPlayMusic( void )
{
	if( sgtc_GameModeSet == 0 )
		return SGTC_ThinkGameMode0();

	SGTC_ThinkGameMode34Side();

	if( sgtc_GameModeSet == 1 || sgtc_GameModeSet == 4 )
	{
		m_flSgtcRoundEnd = gpGlobals->time + (float)SGTC_ROUNDTIME_TICKET_SEC;
		m_flSgtcRoundTime = (float)SGTC_ROUNDTIME_TICKET_SEC;

		if( sgtc_T_players < 1 && sgtc_T_tickets < 1 )
		{
			if( sgtc_hasplayed == 0 )
			{
				SGTC_PlayMusic( 1 );
				sgtc_endtround = gpGlobals->time + 6.0f;
				m_iSGTCTeamScores[GetTeamIndex( "Goa'uld" )]++;
			}
			sgtc_hasplayed = 1;
		}
		if( sgtc_G_players < 1 && sgtc_G_tickets < 1 )
		{
			if( sgtc_hasplayed == 0 )
			{
				SGTC_PlayMusic( 0 );
				sgtc_endtround = gpGlobals->time + 6.0f;
				m_iSGTCTeamScores[GetTeamIndex( "Torri" )]++;
			}
			sgtc_hasplayed = 1;
		}
		if( sgtc_G_players < 1 && sgtc_T_players < 1 &&
			sgtc_G_tickets < 1 && sgtc_T_tickets < 1 )
		{
			if( sgtc_hasplayed == 0 )
			{
				SGTC_PlayMusic( 2 );
				sgtc_endtround = gpGlobals->time + 6.0f;
			}
			sgtc_hasplayed = 1;
		}
	}
	else if( sgtc_GameModeSet == 2 )
	{
		// ref Think @0x1929ce — mode 2 has no ticket PlayMusic stingers (Goal arms
		// endtround); refresh round timers and latch lasty when tickets hit 100.
		if( sgtc_G_tickets == 100 )
			sgtc_G_lasty = 100;
		if( sgtc_T_tickets == 100 )
			sgtc_T_lasty = 100;
		m_flSgtcRoundEnd = gpGlobals->time + (float)SGTC_ROUNDTIME_TICKET_SEC;
		m_flSgtcRoundTime = (float)SGTC_ROUNDTIME_TICKET_SEC;
	}
	return FALSE;
}

// ---------------------------------------------------------------------------
// Think endtround rollover — ref CStargateTeamDM::Think @0x1929ce after P10
// stingers. Requires hasplayed==1 and gpGlobals->time >= endtround.
// vtable +0x38 = EndRound; +0x108 = GoToIntermission (fraglimit / map end).
// ---------------------------------------------------------------------------
void CStargateTeamDM::SGTC_ThinkEndtroundRollover( void )
{
	if( gpGlobals->time < sgtc_endtround || sgtc_hasplayed != 1 )
		return;

	if( sgtc_GameModeSet == 1 || sgtc_GameModeSet == 4 )
	{
		sgtc_endtround = 0.0f;
		m_iSgtcRoundsOnMap++;
		float flRoundsPerMap = CVAR_GET_FLOAT( "mp_roundspermap" );
		if( flRoundsPerMap - 1.0f < (float)m_iSgtcRoundsOnMap )
		{
			m_iSgtcRoundsOnMap = 0;
			sgtc_G_tickets = SGTC_Round_intconstrain( 0, 99,
				SGTC_Round_roundf( sgtc_cvar_tickets.value ) );
			sgtc_T_tickets = sgtc_G_tickets;
			GoToIntermission();
		}
		else
		{
			EndRound();
		}
		return;
	}

	if( sgtc_GameModeSet == 2 )
	{
		sgtc_endtround = 0.0f;
		m_iSgtcRoundsOnMap++;
		float flRoundsPerMap = CVAR_GET_FLOAT( "mp_roundspermap" );
		if( flRoundsPerMap - 1.0f < (float)m_iSgtcRoundsOnMap )
		{
			m_iSgtcRoundsOnMap = 0;
			MESSAGE_BEGIN( MSG_ALL, gmsgGtickets );
				WRITE_BYTE( 100 );
			MESSAGE_END();
			MESSAGE_BEGIN( MSG_ALL, gmsgTtickets );
				WRITE_BYTE( 100 );
			MESSAGE_END();
			GoToIntermission();
		}
		else
		{
			EndRound();
		}
	}

	// ref @0x1929ce — shared endtround tail for modes 0 and 2 (runs after mode-2
	// roundspermap rollover; skipped when NewRound cleared hasplayed).
	if( ( sgtc_GameModeSet == 0 || sgtc_GameModeSet == 2 ) && sgtc_hasplayed == 1 )
	{
		sgtc_endtround = 0.0f;
		EndRound();
	}
}

// ---------------------------------------------------------------------------
// NewRound body helper — nm 0x184f42 shared tail (Ghidra 0x194f42).
// ---------------------------------------------------------------------------
static void SGTC_NewRoundStripGroundWeapons( void )
{
	// ref @0x184f42 head: edict-array walk; a "weapon_*"-prefixed cache
	// entity with no owner (CBaseEntity +0x4de == 0) gets virtual +0x128
	// (respawn re-cache; the port marks it spawn-ineligible until touched).
	for( int i = 0; i < gpGlobals->maxEntities; i++ )
	{
		edict_t *ed = INDEXENT( i );
		if( FNullEnt( ed ) || !ed->v.classname )
			continue;
		const char *psz = STRING( ed->v.classname );
		if( strncmp( psz, "weapon_", 7 ) != 0 )
			continue;
		CBaseEntity *pEnt = CBaseEntity::Instance( ed );
		if( pEnt && ed->v.owner == 0 ) // ref: CBaseEntity +0x4de == 0
			pEnt->pev->solid = SOLID_NOT; // ref virtual +0x128
	}
}

// Each sweep below advances from the entity it just handled. Restarting the
// search from NULL after a reset finds the same entity again (UTIL_Remove only
// sets FL_KILLME, and the Reset handlers leave the entity in place), which
// hangs the server inside NewRound. The next entity is looked up before the
// current one is acted on, in case the handler removes or reclassifies it.
static void SGTC_NewRoundRemoveClass( const char *pszClassname, BOOL bThink )
{
	CBaseEntity *pEnt = UTIL_FindEntityByClassname( NULL, pszClassname );
	while( pEnt )
	{
		CBaseEntity *pNext = UTIL_FindEntityByClassname( pEnt, pszClassname );
		if( bThink )
			pEnt->Think(); // ref Reset slot +0x1c
		else
			UTIL_Remove( pEnt );
		pEnt = pNext;
	}
}

// ref NewRound sweep: CSatchelCharge::Deactivate @nm 0x155a1c (pev+0x10c=0, UTIL_Remove)
static void SGTC_NewRoundRemoveSatchels( void )
{
	CBaseEntity *pEnt = UTIL_FindEntityByClassname( NULL, "monster_satchel" );
	while( pEnt )
	{
		CBaseEntity *pNext = UTIL_FindEntityByClassname( pEnt, "monster_satchel" );
		((CSatchelCharge *)pEnt)->Deactivate();
		pEnt = pNext;
	}
}

static void SGTC_NewRoundResetNukes( void )
{
	CBaseEntity *pEnt = UTIL_FindEntityByClassname( NULL, "game_nuke" );
	while( pEnt )
	{
		CBaseEntity *pNext = UTIL_FindEntityByClassname( pEnt, "game_nuke" );
		((CToken *)pEnt)->ResetForNewRound();
		pEnt = pNext;
	}
}

static void SGTC_NewRoundStopMultiManagers( void )
{
	CBaseEntity *pEnt = UTIL_FindEntityByClassname( NULL, "multi_manager" );
	while( pEnt )
	{
		CBaseEntity *pNext = UTIL_FindEntityByClassname( pEnt, "multi_manager" );
		((CMultiManager *)pEnt)->Stop();
		pEnt = pNext;
	}
}

static void SGTC_NewRoundInitEntities( void )
{
	// ref NewRound tail (Ghidra 0x194f42) — Reset-style thinkers, then
	// round-lifecycle cleanup.
	SGTC_NewRoundRemoveClass( "func_breakable", TRUE );
	SGTC_NewRoundRemoveClass( "func_door", TRUE );
	SGTC_NewRoundRemoveClass( "func_door_rotating", TRUE );
	SGTC_NewRoundRemoveClass( "func_button", TRUE );
	SGTC_NewRoundRemoveClass( "func_rot_button", TRUE );
	SGTC_NewRoundStopMultiManagers();	// ref: CMultiManager::Stop (not Think)
	SGTC_NewRoundRemoveClass( "func_rotating", TRUE );
	SGTC_NewRoundRemoveClass( "func_train", TRUE );
	SGTC_NewRoundRemoveClass( "func_illusionary", TRUE );
	SGTC_NewRoundRemoveClass( "func_wall", TRUE );
	SGTC_NewRoundRemoveClass( "func_wall_toggle", TRUE );
	SGTC_NewRoundRemoveClass( "func_pushable", TRUE );
	SGTC_NewRoundRemoveClass( "func_tank", TRUE );
	SGTC_NewRoundRemoveClass( "func_tankgoauld", TRUE );
	SGTC_NewRoundRemoveClass( "func_tanklaser", TRUE );
	SGTC_NewRoundRemoveClass( "func_tankrocket", TRUE );
	SGTC_NewRoundRemoveClass( "func_tankmortar", TRUE );
	SGTC_NewRoundRemoveClass( "trigger_auto", TRUE );
	SGTC_NewRoundRemoveClass( "light", TRUE );
	SGTC_NewRoundRemoveClass( "light_spot", TRUE );
	SGTC_NewRoundResetNukes();		// ref: pev+0x160=30 then Think (+0xc4)
	SGTC_Zones_ResetBombTargets();	// ref: pev+0x160=69 then Think (+0xc4)
	SGTC_NewRoundRemoveClass( "trigger_changetarget", TRUE );
	SGTC_NewRoundRemoveClass( "trigger_multiple", TRUE );
	SGTC_NewRoundRemoveClass( "trigger_once", TRUE );
	SGTC_NewRoundRemoveClass( "multisource", TRUE );
	SGTC_NewRoundRemoveClass( "trigger_teleport", TRUE );
	SGTC_NewRoundRemoveClass( "trigger_adv_teleport", TRUE );
	SGTC_NewRoundRemoveClass( "trigger_hurt", TRUE );
	SGTC_NewRoundRemoveClass( "info_teleport_destination", TRUE );
	SGTC_NewRoundRemoveClass( "special_teleport_in", TRUE );
	SGTC_NewRoundRemoveClass( "env_render", TRUE );
	SGTC_NewRoundRemoveClass( "env_particule", TRUE );
	SGTC_NewRoundRemoveClass( "env_fountain", TRUE );
	SGTC_NewRoundRemoveClass( "env_beam", TRUE );

	SGTC_NewRoundRemoveClass( "effect_bigshield", FALSE );
	SGTC_NewRoundRemoveClass( "monster_shieldemit", FALSE );
	SGTC_NewRoundRemoveClass( "monster_lockdown", FALSE );
	SGTC_NewRoundRemoveClass( "monster_trapbox", FALSE );
	SGTC_NewRoundRemoveClass( "monster_zatboom", FALSE );
	SGTC_NewRoundRemoveClass( "monster_implanted", FALSE );
	SGTC_NewRoundRemoveClass( "monster_skull", FALSE );
	SGTC_NewRoundRemoveClass( "monster_tac", FALSE );
	SGTC_NewRoundRemoveClass( "monster_larve", FALSE );
	SGTC_NewRoundRemoveClass( "monster_mine", FALSE );
	SGTC_NewRoundRemoveSatchels();
	SGTC_NewRoundRemoveClass( "grenade", FALSE );
	SGTC_NewRoundRemoveClass( "sgtc_gascloud", FALSE );
	SGTC_NewRoundRemoveClass( "lance_proj", FALSE );
	SGTC_NewRoundRemoveClass( "big_lance_proj", FALSE );
	SGTC_NewRoundRemoveClass( "reetou_proj", FALSE );
	SGTC_NewRoundRemoveClass( "zat_proj", FALSE );
	SGTC_NewRoundRemoveClass( "effect_shield", FALSE );
	SGTC_NewRoundRemoveClass( "rpg_rocket", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_glock", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_9mmAR", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_medic", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_mp5", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_psg1", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_p90", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_tacgun", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_spas", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_usas", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_rpg", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_invis", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_gacgoa", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_m16", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_handgrenade", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_goaregen", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_mine", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_smokegrenade", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_nervegrenade", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_narcogrenade", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_mustardgrenade", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_c4", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_couteau", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_main", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_zat", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_flashbang", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_lance", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_glstaff", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_batsup", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_coujaf", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_reetou", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_larve", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_zatarc", FALSE );
	SGTC_NewRoundRemoveClass( "weapon_m92s", FALSE );
	SGTC_NewRoundRemoveClass( "struct_goa_actual", FALSE );
	SGTC_NewRoundRemoveClass( "struct_tau_actual", FALSE );
	SGTC_NewRoundRemoveClass( "struct_goa_actual_sub", FALSE );
	SGTC_NewRoundRemoveClass( "struct_tau_actual_sub", FALSE );
	SGTC_NewRoundRemoveClass( "item_objective", FALSE );
}

static void SGTC_NewRoundCorpseClear( void )
{
	// ref NewRound: corpse walk — pev->deadflag style state reset (0x148/0x14c)
	// then corpse origin moved off-map (SGTC_OFFMAP_COORD).
	CBaseEntity *pEnt = NULL;
	while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "corpse" ) ) != NULL )
	{
		pEnt->pev->team = 4;
		pEnt->pev->deadflag = 0;
		pEnt->pev->origin = Vector( SGTC_OFFMAP_COORD, SGTC_OFFMAP_COORD, SGTC_OFFMAP_COORD );
	}
}

static void SGTC_NewRoundSpritesWithoutMarker( void )
{
	// ref NewRound: for each env_sprite, if no info_marker shares its origin,
	// reset it (ref calls Reset +0x1c).
	CBaseEntity *pSprite = NULL;
	while( ( pSprite = UTIL_FindEntityByClassname( pSprite, "env_sprite" ) ) != NULL )
	{
		BOOL bMatch = FALSE;
		CBaseEntity *pMarker = NULL;
		while( ( pMarker = UTIL_FindEntityByClassname( pMarker, "info_marker" ) ) != NULL )
		{
			if( pMarker->pev->origin == pSprite->pev->origin )
			{
				bMatch = TRUE;
				break;
			}
		}
		if( !bMatch )
			pSprite->Think();
	}
}

// ---------------------------------------------------------------------------
// CStargateTeamDM::PlayerThink — nm 0x17d088 (Ghidra 0x18d088).
// ---------------------------------------------------------------------------
void CStargateTeamDM::PlayerThink( CBasePlayer *pPlayer )
{
	CHalfLifeMultiplay::PlayerThink( pPlayer );
	if( g_fGameOver )
		return;

	const float flNow = gpGlobals->time;

	// env entity iterator (ref param_1 + 0xc88/+0xc90/+0xc94). Index 0 is a
	// NULL slot — ALLOC_PRIVATE zeroes the stage, so start at 1.
	if( pPlayer->m_iSgtcEnvStage == 0 )
		pPlayer->m_iSgtcEnvStage = 1;
	if( pPlayer->m_iSgtcEnvStage < 5 && pPlayer->m_flSgtcEnvNext < flNow )
	{
		static const char *const envClasses[] =
		{
			NULL, "env_particule", "env_rain", "env_fountain", "env_lensflare"
		};
		int stage = pPlayer->m_iSgtcEnvStage;
		EHANDLE hFound = pPlayer->m_hSgtcEnvIterator;
		CBaseEntity *pFound = UTIL_FindEntityByClassname( hFound, envClasses[stage] );
		pPlayer->m_hSgtcEnvIterator = pFound;
		if( pFound )
		{
			// ref calls the found entity's vtable+0x24 — the env particle
			// family's per-client init (SendValueTo) for this player.
			if( FClassnameIs( pFound->pev, "env_particule" ) )
				( (CEnvParticule *)pFound )->SendValueTo( pPlayer );
			else if( FClassnameIs( pFound->pev, "env_rain" ) )
				( (CEnvRain *)pFound )->SendValueTo( pPlayer );
			else if( FClassnameIs( pFound->pev, "env_fountain" ) )
				( (CEnvFountain *)pFound )->SendValueTo( pPlayer );
			else if( FClassnameIs( pFound->pev, "env_lensflare" ) )
				( (CEnvLensFlare *)pFound )->SendValueTo( pPlayer );
		}
		else
			pPlayer->m_iSgtcEnvStage++;
		int lat = SGTC_Round_intconstrain( 1, 10,
			SGTC_Round_roundf( sgtc_cvar_particlelatency.value ) );
		sgtc_cvar_particlelatency.value = lat;
		pPlayer->m_flSgtcEnvNext = sgtc_cvar_particlelatency.value / 10.0f + flNow;
	}

	// +0xa3d round-start tick: ref sets when gpGlobals >= round anchor + 1s.
	if( flNow >= sgtc_roundtime_official + 1.0f )
		pPlayer->m_bSgtcPadA3D = 1;

	// superweapons (+0x465 preset gate; the once-per-round latch rides
	// +0xa9c — ref pev+0x168 is pev->weapons, weapon-OWNERSHIP bits 18/15,
	// not pev->button/IN_ALT1/IN_SCORE).
	if( pPlayer->m_bSgtcPePreset && pPlayer->m_bSgtcLatchA9c != 1 )
	{
		if( !FBitSet( pPlayer->pev->weapons, ( 1 << 18 ) ) &&
			pPlayer->pev->team == SGTC_ENTTEAM_GOAULD )
		{
			pPlayer->GiveNamedItem( "weapon_glstaff" );
			pPlayer->m_bSgtcLatchA9c = 1;
		}
		else if( !FBitSet( pPlayer->pev->weapons, ( 1 << 15 ) ) &&
			pPlayer->pev->team == SGTC_ENTTEAM_TORRI )
		{
			pPlayer->GiveNamedItem( "weapon_usas" );
			pPlayer->m_bSgtcLatchA9c = 1;
		}
	}

	// GiveFromMenu (0x4e5/0x4e7 + 0x4ac time gate) — ref pev+0x168 is
	// pev->weapons: owned-weapon bits 13 and 8 suppress the apply (same bit
	// indexes as IN_RELOAD/IN_RIGHT, but the FIELD is weapons, not button).
	// The 15s window is measured against +0x4ac (m_flSgtcMenuWindow), not
	// +0x4b0 (the menu-10 delay); ref term is gpGlobals < +0x4ac + 15.0.
	if( pPlayer->m_bSgtcMenuLock && !FBitSet( pPlayer->pev->weapons, ( 1 << 13 ) ) &&
		!FBitSet( pPlayer->pev->weapons, ( 1 << 8 ) ) &&
		( flNow < pPlayer->m_flSgtcMenuWindow + 15.0f ||
		  pPlayer->m_bSgtcMenuLockLatch != 1 ) )
	{
		SGTC_GiveFromMenu( pPlayer );
	}

	// ref DM PlayerThink @0x18d088: !IsAlive && !IsObserver &&
	// deadflag == DEAD_DEAD (pev+0x170 == 2) -> StartDeathCam. NOT pev->team.
	if( !pPlayer->IsAlive() && !pPlayer->IsObserver() &&
		pPlayer->pev->deadflag == DEAD_DEAD )
		pPlayer->StartDeathCam();

	if( pPlayer->m_flSgtcMenuDelay != 0.0f && pPlayer->m_flSgtcMenuDelay < flNow )
	{
		pPlayer->ShowVGUIMenu( 10 );
		pPlayer->m_flSgtcMenuDelay = 0.0f;
	}

	// 0x464 + alive → ScreenFade(0x38 slot, own pev, 5000.0, 1).
	if( pPlayer->m_bSgtcPad464 == 1 && sgtc_GameModeSet == 0 &&
		pPlayer->IsAlive() )
	{
		UTIL_ScreenFade( pPlayer, Vector( 0, 0, 0 ), 5000.0f, 1.0f, 0, FFADE_IN );
	}

	if( pPlayer->m_flSgtcTeamsTick < flNow )
	{
		// ref DM PlayerThink @0x18d088 ticket flag: name == "Goa'uld" &&
		// 0 < G_tickets || name == "Torri" && 0 < T_tickets, taken BEFORE
		// +0xe08 is rewritten below — this snapshot is the consume-gate input.
		BOOL bTickets = ( !strncmp( pPlayer->m_szTeamName, "Goa'uld", 7 ) &&
			0 < sgtc_G_tickets && !g_fGameOver ) ||
			( !strncmp( pPlayer->m_szTeamName, "Torri", 5 ) &&
			0 < sgtc_T_tickets && !g_fGameOver );

		// ref DM @0x18d088: ShowVGUIMenu(0xb) unless +0x4de, alive, tickets,
		// +0x3e4 (m_bSgtcRoundAlive) non-zero, or game-over. +0x4b0 only
		// arms the separate menu-10 emit above; it is NOT in this gate.
		if( !( pPlayer->m_bSnagInvisLatch || pPlayer->IsAlive() || bTickets ||
			( pPlayer->m_bSgtcRoundAlive || g_fGameOver ) ) )
			pPlayer->ShowVGUIMenu( 11 );	// ref 0xb

		// ref consume gate — verbatim transcription of the mode-dependent
		// chain (0x18d5c0..0x18d830). DM modes (GameModeSet >= 1): consume
		// only when the current team still has tickets OR a pending pick
		// exists (+0x438 != 0 && +0xa44 != -1). Mode 0: consume while the
		// round is < 30s old, a team is empty, or the player has tickets.
		// Alive non-observers and game-over never consume.
		BOOL bConsume;
		if( ( ( ( sgtc_GameModeSet < 1 || !bTickets ) &&
			( ( sgtc_GameModeSet < 1 ||
				( pPlayer->m_iSgtcTeam == SGTC_TEAM_NONE || pPlayer->m_iSgtcPortraitSel == -1 ) ) &&
				( sgtc_GameModeSet != 0 ||
					( ( pPlayer->m_bSgtcRoundAlive || pPlayer->m_iSgtcPortraitSel == -1 ) ||
						pPlayer->m_iSgtcTeam < 1 ) ) ) &&
			( ( sgtc_GameModeSet != 0 || sgtc_roundstart <= flNow - 30.0f ) || bTickets ) ) &&
			( ( sgtc_GameModeSet != 0 || m_iSgtcEmptyTeams == 0 ) || bTickets ) ) ||
			( ( pPlayer->IsAlive() && !pPlayer->IsObserver() ) || g_fGameOver ) )
			bConsume = FALSE;
		else
			bConsume = TRUE;

		if( bConsume )
		{
			if( pPlayer->m_iSgtcTeam == SGTC_TEAM_TORRI )
				strncpy( pPlayer->m_szTeamName, "Torri", TEAM_NAME_LENGTH );
			else if( pPlayer->m_iSgtcTeam == SGTC_TEAM_GOAULD )
				strncpy( pPlayer->m_szTeamName, "Goa'uld", TEAM_NAME_LENGTH );

			if( pPlayer->m_iSgtcTeam != SGTC_TEAM_NONE )
			{
				pPlayer->m_iSgtcPrevTeam = pPlayer->m_iSgtcActiveTeam;
				pPlayer->m_iSgtcActiveTeam = pPlayer->m_iSgtcTeam;
			}
			// ref vtable+0x64 = FPlayerCanRespawn(this,player) — the pending-class
			// consumer (@0x18ba08), NOT SetDefaultPlayerTeam. The consume of
			// +0xa44 -> shown class happens inside.
			FPlayerCanRespawn( pPlayer );

			if( 0 < pPlayer->m_iSgtcPortraitSel )
				pPlayer->m_iSgtcPortraitShown = pPlayer->m_iSgtcPortraitSel;

			// ref re-tests tickets on the NEW name (post-consume).
			BOOL bJoinTickets = ( !strncmp( pPlayer->m_szTeamName, "Goa'uld", 7 ) &&
				0 < sgtc_G_tickets ) ||
				( !strncmp( pPlayer->m_szTeamName, "Torri", 5 ) &&
				0 < sgtc_T_tickets );
			if( bJoinTickets )
			{
				ChangePlayerTeam( pPlayer, pPlayer->m_szTeamName, 0, 0 );	// ref vtable+0xec
				int iRespawn = SGTC_Round_intconstrain( 3, 120,
					SGTC_Round_roundf( sgtc_cvar_respawntime.value ) );
				if( sgtc_GameModeSet == 0 || !bTickets )
					iRespawn = 1;
				// ref: respawn only dead OBSERVERS; the mp_respawntime
				// delay applies only in modes >= 1 (mode 0 respawns at
				// once). Ref +0xc7c is PlayerDeathThink's m_fDeadTime;
				// the old m_flSgtcDeathTime member was a mislabel of this
				// offset and has been deleted.
				if( !pPlayer->IsAlive() && pPlayer->IsObserver() &&
					( sgtc_GameModeSet == 0 ||
						flNow - iRespawn > pPlayer->m_fDeadTime ) )
					respawn( pPlayer->pev, 0 );
			}
		}
		pPlayer->m_flSgtcTeamsTick = flNow + 1.0f;
	}

	if( pPlayer->IsAlive() )
		pPlayer->m_bSgtcRoundAlive = 1;

	RedrawPortrait( pPlayer );	// ref vtable+0x34 tail @0x18d088
}

// ---------------------------------------------------------------------------
// CStargateTeamCP::PlayerThink — nm 0x17008c (Ghidra 0x18008c).
// Shares the DM env/death-cam/menu frame; diverges in the team/ticket block
// (capture types 2/4 gate team-lock + respawn), no DM supergun branch.
// ---------------------------------------------------------------------------
void CStargateTeamCP::PlayerThink( CBasePlayer *pPlayer )
{
	CHalfLifeMultiplay::PlayerThink( pPlayer );
	if( g_fGameOver )
		return;

	const float flNow = gpGlobals->time;

	// env entity iterator (identical to DM).
	if( pPlayer->m_iSgtcEnvStage == 0 )
		pPlayer->m_iSgtcEnvStage = 1;
	if( pPlayer->m_iSgtcEnvStage < 5 && pPlayer->m_flSgtcEnvNext < flNow )
	{
		static const char *const envClasses[] =
		{
			NULL, "env_particule", "env_rain", "env_fountain", "env_lensflare"
		};
		int stage = pPlayer->m_iSgtcEnvStage;
		EHANDLE hFound = pPlayer->m_hSgtcEnvIterator;
		CBaseEntity *pFound = UTIL_FindEntityByClassname( hFound, envClasses[stage] );
		pPlayer->m_hSgtcEnvIterator = pFound;
		if( pFound )
		{
			// ref calls the found entity's vtable+0x24 — per-client init
			// (SendValueTo) for this player.
			if( FClassnameIs( pFound->pev, "env_particule" ) )
				( (CEnvParticule *)pFound )->SendValueTo( pPlayer );
			else if( FClassnameIs( pFound->pev, "env_rain" ) )
				( (CEnvRain *)pFound )->SendValueTo( pPlayer );
			else if( FClassnameIs( pFound->pev, "env_fountain" ) )
				( (CEnvFountain *)pFound )->SendValueTo( pPlayer );
			else if( FClassnameIs( pFound->pev, "env_lensflare" ) )
				( (CEnvLensFlare *)pFound )->SendValueTo( pPlayer );
		}
		else
			pPlayer->m_iSgtcEnvStage++;
		int lat = SGTC_Round_intconstrain( 1, 10,
			SGTC_Round_roundf( sgtc_cvar_particlelatency.value ) );
		sgtc_cvar_particlelatency.value = lat;
		pPlayer->m_flSgtcEnvNext = sgtc_cvar_particlelatency.value / 10.0f + flNow;
	}

	if( flNow >= sgtc_roundtime_official + 1.0f )
		pPlayer->m_bSgtcPadA3D = 1;

	// GiveFromMenu (0x4e5 + 0x4ac gate) — no supergun branch in CP.
	// ref pev+0x168 is pev->weapons: owned-weapon bits 13/8 (same indexes as
	// IN_RELOAD/IN_RIGHT, but the FIELD is weapons, not button). The 15s
	// window is measured against +0x4ac (m_flSgtcMenuWindow), not +0x4b0;
	// ref term is gpGlobals < +0x4ac + 15.0.
	if( pPlayer->m_bSgtcMenuLock && !FBitSet( pPlayer->pev->weapons, ( 1 << 13 ) ) &&
		!FBitSet( pPlayer->pev->weapons, ( 1 << 8 ) ) &&
		( flNow < pPlayer->m_flSgtcMenuWindow + 15.0f ||
		  pPlayer->m_bSgtcMenuLockLatch != 1 ) )
	{
		SGTC_GiveFromMenu( pPlayer );
	}

	// ref CP PlayerThink @0x18008c: !IsAlive && !IsObserver &&
	// deadflag == DEAD_DEAD (pev+0x170 == 2) -> StartDeathCam. NOT pev->team.
	if( !pPlayer->IsAlive() && !pPlayer->IsObserver() &&
		pPlayer->pev->deadflag == DEAD_DEAD )
		pPlayer->StartDeathCam();

	if( pPlayer->m_flSgtcMenuDelay != 0.0f && pPlayer->m_flSgtcMenuDelay < flNow )
	{
		pPlayer->ShowVGUIMenu( 10 );
		pPlayer->m_flSgtcMenuDelay = 0.0f;
	}

	pPlayer->m_bSgtcPortraitsFrozen = 1;
	if( pPlayer->m_bSgtcMenuLock )
		SGTC_GiveFromMenu( pPlayer );

	// 0x464 + type 2 + alive: the capture-holding team gets the fade tick
	// (teamed 1 -> "Goa'uld" team string, teamed 2 -> "Torri").
	if( pPlayer->m_bSgtcPad464 == 1 && sgtc_type == 2 && pPlayer->IsAlive() )
	{
		BOOL bFade = FALSE;
		if( sgtc_teamed == 1 && !stricmp( pPlayer->TeamID(), "Goa'uld" ) )
			bFade = TRUE;
		if( sgtc_teamed == 2 && !stricmp( pPlayer->TeamID(), "Torri" ) )
			bFade = TRUE;
		if( bFade )
		{
			// ref vtable+0x38: per-player fade helper (own pev, 5000.0, 1).
			UTIL_ScreenFade( pPlayer, Vector( 0, 0, 0 ), 5000.0f, 1.0f, 0, FFADE_IN );
		}
	}

	// ref CP PlayerThink @0x18008c gate: skip only when the 1s tick is due OR
	// the player is alive — observers and dead players consume below.
	if( flNow > pPlayer->m_flSgtcTeamsTick && !pPlayer->IsAlive() )
	{
		// ref CP @0x18008c: +0x3e4 != 1 && !g_fGameOver. +0x4b0 is menu-10 only.
		if( pPlayer->m_bSgtcRoundAlive != 1 && !g_fGameOver )
			pPlayer->ShowVGUIMenu( 11 );	// ref 0xb

		if( pPlayer->m_iSgtcTeam == SGTC_TEAM_TORRI )
			strncpy( pPlayer->m_szTeamName, "Torri", TEAM_NAME_LENGTH );
		else if( pPlayer->m_iSgtcTeam == SGTC_TEAM_GOAULD )
			strncpy( pPlayer->m_szTeamName, "Goa'uld", TEAM_NAME_LENGTH );

		if( pPlayer->m_iSgtcTeam != SGTC_TEAM_NONE )
		{
			pPlayer->m_iSgtcPrevTeam = pPlayer->m_iSgtcActiveTeam;
			pPlayer->m_iSgtcActiveTeam = pPlayer->m_iSgtcTeam;
		}
		// ref CP vtable+0x64 = FPlayerCanRespawn(this,player) — the pending-class
		// consumer (@0x17ea2a), NOT SetDefaultPlayerTeam. The consume of
		// +0xa44 -> shown class happens inside.
		FPlayerCanRespawn( pPlayer );

		if( 0 < pPlayer->m_iSgtcPortraitSel )
			pPlayer->m_iSgtcPortraitShown = pPlayer->m_iSgtcPortraitSel;

		// ref: either team NAME alone arms the team change — no ticket test
		// in CP (unlike the DM variant at 0x18d088).
		BOOL bJoin = !strncmp( pPlayer->m_szTeamName, "Torri", 5 ) ||
			!strncmp( pPlayer->m_szTeamName, "Goa'uld", 7 );

		// type 2 + 0x464: the capture-holding team is excluded.
		if( sgtc_type == 2 && pPlayer->m_bSgtcPad464 )
		{
			if( sgtc_teamed == 1 && !strncmp( pPlayer->m_szTeamName, "Goa'uld", 7 ) )
				bJoin = FALSE;
			if( sgtc_teamed == 2 && !strncmp( pPlayer->m_szTeamName, "Torri", 5 ) )
				bJoin = FALSE;
		}
		// type 4 + 0x464: rules+0x734 (m_iSgtcCPSide) excludes the chosen team.
		if( sgtc_type == 4 && pPlayer->m_bSgtcPad464 )
		{
			if( m_iSgtcCPSide != 0 && !strncmp( pPlayer->m_szTeamName, "Goa'uld", 7 ) )
				bJoin = FALSE;
			if( m_iSgtcCPSide == 0 && !strncmp( pPlayer->m_szTeamName, "Torri", 5 ) )
				bJoin = FALSE;
		}

		if( bJoin )
		{
			ChangePlayerTeam( pPlayer, pPlayer->m_szTeamName, 0, 0 );	// ref vtable+0xec
			// ref: respawn only dead OBSERVERS (the fresh-join path).
			if( !pPlayer->IsAlive() && pPlayer->IsObserver() )
				respawn( pPlayer->pev, 0 );
		}
		pPlayer->m_flSgtcTeamsTick = flNow + 1.0f;
	}

	if( pPlayer->IsAlive() )
		pPlayer->m_bSgtcRoundAlive = 1;

	RedrawPortrait( pPlayer );	// ref vtable+0x34 tail @0x18008c
}

// ---------------------------------------------------------------------------
// SGTC_NewRoundCommon — the ~700-line shared body of both NewRound variants
// (ref nm 0x184f42 / 0x177b34): entity reset, corpse clear, round state
// commit, key/objective restore, per-player re-team + respawn, tournament
// fan-out, skulls, ticket broadcast and the ref's virtual +0x120 tail call.
// bCaptureMode selects the CP-only divergences.
// ---------------------------------------------------------------------------
void CStargateTeamDM::SGTC_NewRoundCommon( BOOL bCaptureMode )
{
	// Ref reads the registered cvar_t .value live (GameDLLInit-registered);
	// refresh the mirrors so RCON/config changes track (Pass4/REA verified).
	sgtc_tourny        = sgtc_cvar_tournament.value;
	sgtc_allowmodevote = sgtc_cvar_modevoting.value;
	sgtc_crazymode     = sgtc_cvar_skulls.value;
	sgtc_gamemodex     = sgtc_cvar_gamemode.value;
	sgtc_roundtime     = CVAR_GET_FLOAT( "mp_roundtime" );
	sgtc_type          = 0;
	sgtc_teamed        = 0;
	// ref CP NewRound @0x187b34: info_cap_rules walk repopulates type/teamed
	// after the round-state zero (port had ReadCapInfo only before common, so
	// sgtc_type was always 0 for the player loop + PlayerSpawn TicketMode).
	if( bCaptureMode )
		SGTC_Rules_ReadCapInfo();

	// (b) strip every ground "weapon_*" cache entity with no owner.
	SGTC_NewRoundStripGroundWeapons();

	RecountTeams( FALSE );

	// (e) empty gmsgNewRnd broadcast.
	MESSAGE_BEGIN( MSG_ALL, gmsgNewRnd );
	MESSAGE_END();

	// (f) round state reset.
	for( int i = 0; i < 5; i++ )
	{
		sgtc_LastStateI[i] = 0;
		sgtc_LastState[i] = 0;
	}
	sgtc_recounttime = gpGlobals->time + 30.0f;	// ref NewRound tail (DM+CP)
	sgtc_round_official = 0;

	// (g) tournament gate.
	if( sgtc_tourny != 0.0f )
	{
		int avg = ( sgtc_G_players_focused + sgtc_T_players_focused ) / 2;
		BOOL fair = sgtc_G_players_focused <= avg + 2 &&
			sgtc_T_players_focused <= avg + 2;
		if( sgtc_G_players_focused < 4 || sgtc_T_players_focused < 4 || !fair )
		{
			sgtc_fadetoblack.value = 0;
			UTIL_ClientPrintAll( HUD_PRINTNOTIFY, "-=== Tournament Round Reject ===-\n" );
			if( sgtc_G_players_focused < 4 || sgtc_T_players_focused == 0 )
				UTIL_ClientPrintAll( HUD_PRINTNOTIFY, "Not enough players!\n" );
			if( !fair )
				UTIL_ClientPrintAll( HUD_PRINTNOTIFY, "Teams Unfair!\n" );
		}
		else
		{
			sgtc_fadetoblack.value = 1;
			sgtc_round_official = 1;
			if( !bCaptureMode )
			{
				if( sgtc_GameModeSet == 1 )
					sgtc_G_tickets = sgtc_T_tickets = avg * 7;
				else if( sgtc_GameModeSet == 0 )
					sgtc_roundtime = 300.0f;
				else if( sgtc_GameModeSet == 3 )
				{
					sgtc_GameModeSet = 2;
					sgtc_G_tickets = sgtc_T_tickets = avg * 7;
				}
				else if( sgtc_GameModeSet == 4 )
					sgtc_G_tickets = sgtc_T_tickets = avg * 7;
			}
			UTIL_ClientPrintAll( HUD_PRINTNOTIFY,
				"-=== Tournament Round Accepted ===-\n" );
		}
	}

	// ref CP NewRound @nm 0x177b34: zone_capture walk after tournament,
	// before LastState-style entity sweeps (fields +0x240/+0x244/+0x250).
	if( bCaptureMode )
		SGTC_Zones_ResetCaptureZones();

	// (h)+(i) entity reset + removal.
	SGTC_NewRoundInitEntities();
	SGTC_NewRoundCorpseClear();
	SGTC_NewRoundSpritesWithoutMarker();

	// entity_spawn_round markers fire Event() (CEntSpawn, sgtc_misc).
	CBaseEntity *pSpawn = NULL;
	while( ( pSpawn = UTIL_FindEntityByClassname( pSpawn,
			"entity_spawn_round" ) ) != NULL )
	{
		( (CEntSpawn *)pSpawn )->Event();
	}

	sgtc_mKill = Vector( 0, 0, 0 );
	BOOL bModeError = sgtc_GameModeSet > 4;
	if( bModeError )
	{
		ALERT( at_console, "Null #:Game Mode, Defaulting\n" );
		sgtc_GameModeSet = 0;
	}

	// (j) masterkey / firstplace / item_objective restore.
	if( SGTC_Round_vne( sgtc_masterkey, g_vecZero ) )
	{
		if( sgtc_GameModeSet < 1 )
		{
			sgtc_firstplace = sgtc_masterkey;
		}
		else
		{
			CBaseEntity *pObj = NULL;
			while( ( pObj = UTIL_FindEntityByClassname( pObj,
					"item_objective" ) ) != NULL )
				UTIL_Remove( pObj );
			sgtc_firstplace = Vector( SGTC_OFFMAP_COORD, SGTC_OFFMAP_COORD, SGTC_OFFMAP_COORD );
		}
		if( SGTC_Round_vne( sgtc_firstplace, g_vecZero ) )
		{
			CBaseEntity *pObj = NULL;
			while( ( pObj = UTIL_FindEntityByClassname( pObj,
					"item_objective" ) ) != NULL )
				UTIL_Remove( pObj );
			CBaseEntity::Create( "item_objective", sgtc_firstplace, g_vecZero, NULL );
		}
	}

	m_bRoundOver = FALSE;
	// ref writes: +0x750 = -5 (0xfffffffb), +0x74c = 0, +0x73c = 0, +0x754 = 0.
	m_iSgtcFirstTicks = -5;
	m_iSgtcWinPending = 0;
	m_iSgtcWinScoreSide = 0;
	m_flSgtcRoundTickStart = 0.0f;
	m_iSgtcRoundTicks = 0;
	// ref: LastKeyPlace = firstplace (BSS 0x2368e0)
	{
		extern Vector sgtc_LastKeyPlace;
		sgtc_LastKeyPlace = sgtc_firstplace;
	}
	sgtc_KeyInPlay = 0;

	m_flSgtcRoundTime = CVAR_GET_FLOAT( "mp_roundtime" );	// ref NewRound @0x194f42: this+0x728 = mp_roundtime
	sgtc_roundtime = m_flSgtcRoundTime;	// refresh the mirror from the live cvar
	if( 0 < sgtc_GameModeSet )
		m_flSgtcRoundTime = (float)SGTC_ROUNDTIME_TICKET_SEC;	// ref GameModeSet>0 default (clamped below)
	sgtc_G_tickets = SGTC_Round_intconstrain( 0, 99,
		SGTC_Round_roundf( sgtc_cvar_tickets.value ) );
	sgtc_T_tickets = sgtc_G_tickets;
	m_flSgtcRoundTime = (float)SGTC_Round_intconstrain( SGTC_ROUNDTIME_MIN_SEC, SGTC_ROUNDTIME_MAX_SEC,
		SGTC_Round_roundf( m_flSgtcRoundTime ) );
	sgtc_roundstart = gpGlobals->time;	// ref this+0x72c round-start tick
	sgtc_roundtime_official = sgtc_roundstart;	// port anchor read at PlayerThink :538/:684
	m_flSgtcRoundEnd = m_flSgtcRoundTime + gpGlobals->time;	// ref this+0x724 = mp_roundtime + roundstart

	if( sgtc_GameModeSet == 4 )
	{
		sgtc_lcaptain_T = 0;
		sgtc_lcaptain_G = 0;
		SGTC_SetCap( SGTC_SETCAP_BOTH );	// ref NewRound tail @0x194f42 — both sides
	}

	if( sgtc_friendlyfire != 0.0f )
		sgtc_friendlyfire = 0.0f;

	// (k) DM mode 2: guarantee one struct_goa/struct_tau and spawn the
	// actual/sub captain structures.
	if( !bCaptureMode && sgtc_GameModeSet == 2 )
	{
		sgtc_G_tickets = 100;
		sgtc_T_tickets = 100;
		// guarantee: count struct_goa / struct_tau; while either is missing
		// (max 10 rounds) clone one from a random team spawn spot.
		for( int tries = 0; tries < 10; tries++ )
		{
			int goa = 0, tau = 0;
			CBaseEntity *pEnt = NULL;
			while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "struct_goa" ) ) != NULL )
				goa++;
			pEnt = NULL;
			while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "struct_tau" ) ) != NULL )
				tau++;
			if( goa > 0 && tau > 0 )
				break;
			if( goa < 1 )
			{
				CBaseEntity *pSpot = NULL, *pPick = NULL;
				int count = 0, pick = SGTC_Round_rand( 1, 5 );
				while( ( pSpot = UTIL_FindEntityByClassname( pSpot,
						"info_player_goauld" ) ) != NULL )
				{
					count++;
					if( count == pick )
						pPick = pSpot;
				}
				if( pPick )
				{
					Vector spotOrg = pPick->pev->origin;
					Vector traceOrg = spotOrg;
					traceOrg.z += 10.0f;
					// ref Endo_FloorTrace drops the spot onto the floor.
					TraceResult tr;
					UTIL_TraceLine( traceOrg,
						traceOrg + Vector( 0, 0, -1024 ), ignore_monsters, NULL, &tr );
					spotOrg = tr.vecEndPos;
					CBaseEntity::Create( "struct_goa", spotOrg, g_vecZero, NULL );
				}
			}
			if( tau < 1 )
			{
				CBaseEntity *pSpot = NULL, *pPick = NULL;
				int count = 0, pick = SGTC_Round_rand( 1, 5 );
				while( ( pSpot = UTIL_FindEntityByClassname( pSpot,
						"info_player_torri" ) ) != NULL )
				{
					count++;
					if( count == pick )
						pPick = pSpot;
				}
				if( pPick )
				{
					Vector spotOrg = pPick->pev->origin;
					Vector traceOrg = spotOrg;
					traceOrg.z += 10.0f;
					TraceResult tr;
					UTIL_TraceLine( traceOrg,
						traceOrg + Vector( 0, 0, -1024 ), ignore_monsters, NULL, &tr );
					spotOrg = tr.vecEndPos;
					CBaseEntity::Create( "struct_tau", spotOrg, g_vecZero, NULL );
				}
			}
		}

		int goa = 0, tau = 0;
		CBaseEntity *pEnt = NULL;
		while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "struct_goa" ) ) != NULL )
			goa++;
		pEnt = NULL;
		while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "struct_tau" ) ) != NULL )
			tau++;
		if( tau < 1 && goa < 1 )
		{
			ALERT( at_console, "Null #:struct_goa/struct_tau \n" );
			sgtc_GameModeSet = 0;
			bModeError = TRUE;
		}
		else
		{
			// actual + actual_sub captain structures at the random structs.
			CBaseEntity *pPick = NULL;
			int count = 0, pick = SGTC_Round_rand( 1, goa );
			pEnt = NULL;
			while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "struct_goa" ) ) != NULL )
			{
				count++;
				if( count == pick )
					pPick = pEnt;
			}
			if( pPick )
			{
				CBaseEntity::Create( "struct_goa_actual",
					pPick->pev->origin, pPick->pev->angles, NULL );
				CBaseEntity::Create( "struct_goa_actual_sub",
					pPick->pev->origin, pPick->pev->angles, NULL );
			}
			count = 0;
			pick = SGTC_Round_rand( 1, tau );
			pEnt = NULL;
			while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "struct_tau" ) ) != NULL )
			{
				count++;
				if( count == pick )
					pPick = pEnt;
			}
			if( pPick )
			{
				CBaseEntity::Create( "struct_tau_actual",
					pPick->pev->origin, pPick->pev->angles, NULL );
				CBaseEntity::Create( "struct_tau_actual_sub",
					pPick->pev->origin, pPick->pev->angles, NULL );
			}
			bModeError = FALSE;
		}
	}

	// (l) per-player round restart: re-team, strip, respawn, tournament text.
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = (CBasePlayer *)UTIL_PlayerByIndex( i );
		// ref restart loop reaches observers — the `else IsObserver()` arm below
		// sends SetHudRoundTime(round,2) for the spectator HUD clock (P43c: the
		// old `m_iSgtcTeam==0` clause nulled connect-observers, so the mode-2
		// send never fired and the observer top-center clock stayed dark).
		if( pPlayer && !pPlayer->IsObserver() &&
			!IsValidTeam( pPlayer->TeamID() ) && pPlayer->m_iSgtcTeam == SGTC_TEAM_NONE )
			pPlayer = NULL;

		if( !pPlayer )
			continue;

		pPlayer->m_bSgtcPad464 = 0;
		pPlayer->m_bSgtcLatchA9c = 0;	// ref +0xa9c — superweapon re-arm each round
		if( !bCaptureMode && sgtc_GameModeSet != 4 )
			pPlayer->m_bSgtcPePreset = 0;
		pPlayer->ResetPortrait( 1 );

		if( pPlayer->m_iSgtcTeam == SGTC_TEAM_TORRI )
			strncpy( pPlayer->m_szTeamName, "Torri", TEAM_NAME_LENGTH );
		else if( pPlayer->m_iSgtcTeam == SGTC_TEAM_GOAULD )
			strncpy( pPlayer->m_szTeamName, "Goa'uld", TEAM_NAME_LENGTH );

		if( pPlayer->m_iSgtcTeam > SGTC_TEAM_NONE )
		{
			pPlayer->m_iSgtcPrevTeam = pPlayer->m_iSgtcActiveTeam;
			pPlayer->m_iSgtcActiveTeam = pPlayer->m_iSgtcTeam;
			// NB (#95): m_iSgtcActiveTeam is SgtcTeam; pev->team is SgtcEntTeam (#116).
			pPlayer->pev->team = SGTC_TeamToEntTeam( pPlayer->m_iSgtcActiveTeam );
		}

		SetDefaultPlayerTeam( pPlayer ); // ref gamerules vtable+0x100

		if( 0 < pPlayer->m_iSgtcPortraitSel )
			pPlayer->m_iSgtcPortraitShown = pPlayer->m_iSgtcPortraitSel;

		ChangePlayerTeam( pPlayer, pPlayer->TeamID(), 0, 0 ); // vtable+0x94
		pPlayer->RemoveAllItems( TRUE );
		SetDefaultPlayerTeam( pPlayer ); // ref g_pGameRules vtable+0xf0
		GetPlayerSpawnSpot( pPlayer );   // ref g_pGameRules vtable+0x6c

		// ref: DM loop calls SetHudRoundTime(ROUND(+0x728),1)+respawn;
		// CP loop calls SetHudRoundTime(type==2 ? (0,0) : (ROUND(+0x728),1))
		// then respawn, then the type-4 state writes (+0x934=6, +0x9ac[29]=0).
		if( !bCaptureMode )
		{
			pPlayer->SetHudRoundTime( (int)m_flSgtcRoundTime, 1 );
		}
		else if( sgtc_type == 2 )
		{
			// ref CP type-2 maps (ctp_crypt/frozen/lastn): arm round timer, TicketMode byte 0
			// (was (0,0) which disarmed client +0x16f0; see .rea/rndtim_arm_rea.md).
			pPlayer->SetHudRoundTime( (int)m_flSgtcRoundTime, 1 );
		}
		else
		{
			pPlayer->SetHudRoundTime( (int)m_flSgtcRoundTime, 1 );
			if( sgtc_type == 4 )
			{
				pPlayer->m_iNukeBonus = 6; // ref +0x934 = 6
				for( int t = 0; t < SGTC_NUM_WEAPON_SLOTS + 1; t++ )
				pPlayer->m_iSgtcGondArgs[t] = 0; // ref +0x9ac[29] = 0 — gond args, not m_iProgressTeam
			}
		}
		// P43c: observers entering the loop must NOT respawn — respawn() would
		// relocate the free-cam (P35 regression, grey frame); ref's mode-2 send
		// happens for the observer without disturbing the spectator camera.
		if( !pPlayer->IsObserver() )
			respawn( pPlayer->pev, 0 );

		if( sgtc_tourny != 0.0f )
		{
			if( sgtc_round_official == 0 )
				pPlayer->SendGlobalTextIndex( 12 );
			else
				pPlayer->SendGlobalTextIndex( 11 );

			if( bModeError )
				pPlayer->SendGlobalTextIndex( 11 );
			else if( sgtc_GameModeSet == 0 )
				pPlayer->SendGlobalTextIndex( 16 );
			else if( sgtc_GameModeSet == 1 )
				pPlayer->SendGlobalTextIndex( 17 );
			else if( sgtc_GameModeSet == 2 )
				pPlayer->SendGlobalTextIndex( 18 );
			else if( sgtc_GameModeSet == 3 )
				pPlayer->SendGlobalTextIndex( 19 );
			else if( sgtc_GameModeSet == 4 )
				pPlayer->SendGlobalTextIndex( 20 );
		}
		else if( pPlayer->IsObserver() ) // ref else: spectator HUD round time, mode 2
		{
			pPlayer->SetHudRoundTime( (int)m_flSgtcRoundTime, 2 );
			// P149e: type-2 hold-timer maps — ticket dock off for observer clock
			// (SetRndTim mode 2 clears +0x58 on client; TicketMode after ordering).
			if( bCaptureMode && sgtc_type == 2 )
			{
				MESSAGE_BEGIN( MSG_ONE, gmsgTicketMode, NULL, pPlayer->pev );
				WRITE_BYTE( 0 );
				MESSAGE_END();
			}
		}

		// P142: re-send TicketMode after SetHudRoundTime+respawn — NewRnd can be
		// ordered after PlayerSpawn in the client batch; ref keeps +0x58 across
		// NewRnd Reset (see MsgFunc_NewRnd / sgtc_hud Reset).
		// P149b: observers need TicketMode too (p43h spectate before mp_resetround).
		if( !pPlayer->IsObserver() || bCaptureMode )
		{
			const int iTicket = bCaptureMode
				? ( ( sgtc_type < 2 || sgtc_type == 4 ) ? 1 : 0 )
				: ( sgtc_GameModeSet > 0 ? 1 : 0 );
			MESSAGE_BEGIN( MSG_ONE, gmsgTicketMode, NULL, pPlayer->pev );
			WRITE_BYTE( iTicket );
			MESSAGE_END();
		}
	}

	// (m) CP divergences.
	if( bCaptureMode )
	{
		if( sgtc_type != 4 )
		{
			sgtc_G_tickets = 0;
			sgtc_T_tickets = 0;
		}
		sgtc_under = 1;
		if( sgtc_type == 3 || sgtc_type == 4 )
		{
			m_flSgtcRoundTime = (float)SGTC_ROUNDTIME_TICKET_SEC;
			m_flSgtcRoundEnd = gpGlobals->time + m_flSgtcRoundTime;
		}
		else if( sgtc_type == 2 )
		{
			m_flSgtcRoundTime = 60.0f;
			m_flSgtcRoundEnd = gpGlobals->time + 60.0f;
		}
		sgtc_roundstart = gpGlobals->time;
	}
	sgtc_mKill = Vector( 0, 0, 0 );

	// (n) crazymode skulls.
	if( sgtc_crazymode != 0.0f )
	{
		CBasePlayer *pFirst = (CBasePlayer *)UTIL_PlayerByIndex( 1 );
		if( pFirst )
		{
			for( int s = 0; s < 31; s++ )
			{
				Vector vecOrg(
					(float)SGTC_Round_rand( -4000, 4000 ),
					(float)SGTC_Round_rand( -4000, 4000 ),
					(float)SGTC_Round_rand( -4000, 4000 ) );
				CBaseEntity::Create( "monster_skull", vecOrg, g_vecZero, NULL );
			}
		}
	}

	// (o) ticket broadcasts.
	if( bCaptureMode )
	{
		if( sgtc_type == 4 )
		{
			MESSAGE_BEGIN( MSG_ALL, gmsgGtickets );
				WRITE_BYTE( SGTC_ClampWireByte( sgtc_G_tickets ) );
			MESSAGE_END();
			MESSAGE_BEGIN( MSG_ALL, gmsgTtickets );
				WRITE_BYTE( SGTC_ClampWireByte( sgtc_T_tickets ) );
			MESSAGE_END();
		}
	}
	else if( 0 < sgtc_GameModeSet )
	{
		MESSAGE_BEGIN( MSG_ALL, gmsgGtickets );
			WRITE_BYTE( SGTC_ClampWireByte( sgtc_G_tickets ) );
		MESSAGE_END();
		MESSAGE_BEGIN( MSG_ALL, gmsgTtickets );
			WRITE_BYTE( SGTC_ClampWireByte( sgtc_T_tickets ) );
		MESSAGE_END();
	}

	// (p) round state commit.
	sgtc_hasplayed = 0;
	sgtc_nrcooler = gpGlobals->time + 2.0f;
	// ref CGlobalState::ClearBodyQue — corpse-queue reset (gGlobalState).
	gGlobalState.ClearStates();

	// (q) ref tail: virtual +0x120 — re-enter NewRound once when the mode
	// defaulted. The nrcooler armed above would self-block the re-entry.
	if( bModeError )
	{
		sgtc_nrcooler = 0.0f;
		NewRound();
	}
}

// ---------------------------------------------------------------------------
// CStargateTeamDM::NewRound — nm 0x184f42 (Ghidra 0x194f42).
// ---------------------------------------------------------------------------
void CStargateTeamDM::NewRound( void )
{
	if( g_fGameOver || sgtc_nrcooler > gpGlobals->time )
		return;

	// (d) mode vote.
	if( m_iSgtcModeVote >= 0 && sgtc_tourny == 0.0f &&
		sgtc_allowmodevote != 0.0f )
	{
		BOOL changed = m_iSgtcModeVote != sgtc_GameModeSet;
		sgtc_GameModeSet = m_iSgtcModeVote;
		m_iSgtcModeVote = -1;
		if( changed )
			SGTC_GlobalTextAll( 0 );
	}

	SGTC_NewRoundCommon( FALSE );
}

// ---------------------------------------------------------------------------
// CStargateTeamCP::NewRound — nm 0x177b34 (Ghidra 0x187b34).
// CP divergences: no mode-vote block, zone_capture + info_cap_rules reads
// (teamed/type), tickets zeroed unless type 4, type-driven round windows and
// 0x934/0x9ac per-player state in the loop.
// ---------------------------------------------------------------------------
void CStargateTeamCP::NewRound( void )
{
	if( g_fGameOver || sgtc_nrcooler > gpGlobals->time )
		return;

	// ref @nm 0x177b34 is one monolith (weapon strip → RecountTeams → gmsgNewRnd
	// → tournament → zone_capture → shared tail); no duplicate prefix before it.
	SGTC_NewRoundCommon( TRUE );
}

// ---------------------------------------------------------------------------
// PlayerKilled additions — nm 0x17fb4a (Ghidra 0x18fb4a).
// Ref order: Penalty FIRST, then killpoints/vicpoints scoring, ticket
// decrement + broadcast for GameModeSet 1/3/4, and — only when
// !m_bDisableDeathPenalty (ref this+0x10) — stock PlayerKilled + RecountTeams.
// ---------------------------------------------------------------------------
void CStargateTeamDM::Penalty( CBasePlayer *pVictim, entvars_t *pKillerEv )
{
	// ref Penalty(CBasePlayer*, entvars_t*) @Ghidra 0x18ace2:
	// guards: killer ev non-null, victim non-null, killer entity alive
	// (vtable+0xa8 IsAlive), killer entity != victim entity,
	// victim +0x465 (m_bSgtcPePreset) set.
	if( !pVictim || !pKillerEv )
		return;
	CBaseEntity *pKiller = CBaseEntity::Instance( pKillerEv );
	if( !pKiller || !pKiller->IsAlive() )
		return;
	if( pKiller == pVictim || !pVictim->m_bSgtcPePreset )
		return;

	// Walk every "player" entity SKIPPING THE VICTIM (ref @0x18ad88:
	// CMP param_1(victim) vs scan). For each scanned player:
	//   same pev->team (+0x1ac) as victim && alive (vtable+0x94 IsAlive)
	//     -> vtable+0x38 = Use( scan, killerEv, useType=0x800000, 1000.0f )
	//        (stack pushes verified @0x18adec-0x18dfc: last push 0x800000
	//        is useType; rodata 0x447a0000 = 1000.0f is value)
	//   same team  -> SendGlobalTextIndex(10)
	//   other team -> SendGlobalTextIndex(9)
	const int victimTeam = (int)pVictim->pev->team;
	CBaseEntity *pScan = NULL;
	while( ( pScan = UTIL_FindEntityByClassname( pScan, "player" ) ) != NULL )
	{
		if( pScan == pVictim )
			continue;
		CBasePlayer *pMate = (CBasePlayer *)pScan;

		if( (int)pScan->pev->team == victimTeam && pMate->IsAlive() )
			pMate->Use( pMate, pKiller, (USE_TYPE)0x800000, 1000.0f );

		if( (int)pScan->pev->team == victimTeam )
			pMate->SendGlobalTextIndex( 10 );
		else
			pMate->SendGlobalTextIndex( 9 );
	}
}

static int SGTC_TournyTierFromFocusedKills( int iKills )
{
	int iTier = ( iKills > 4 ) ? 1 : 0;
	if( iKills > 9 )
		iTier = 2;
	if( iKills > 14 )
		iTier = 3;
	if( iKills > 19 )
		iTier = 4;
	return iTier;
}

void CStargateTeamDM::PlayerKilled( CBasePlayer *pVictim,
	entvars_t *pKiller, entvars_t *pInflictor )
{
	// ref @0x18fb66: Penalty(victim, killerEv) runs FIRST, before stock.
	Penalty( pVictim, pKiller );

	CBasePlayer *pKillerPl = NULL;
	if( pKiller )
	{
		CBaseEntity *pKillerEnt = CBaseEntity::Instance( pKiller );
		// ref @0x18fb4a / @0x18262a: killer != victim && killer team (+0x1ac)
		// != victim team -> killer +0x934 += ROUND(killpoints), victim +0x934
		// += ROUND(vicpoints). +0x934 is the SGTC point score (CBasePlayer
		// m_iNukeBonus — the menu-12 "Total" budget byte), NOT pev->frags.
		if( pKillerEnt && pKillerEnt->IsPlayer() && pKillerEnt != pVictim &&
			pKillerEnt->pev->team != pVictim->pev->team )
		{
			pKillerPl = (CBasePlayer *)pKillerEnt;
			pKillerPl->m_iNukeBonus +=
				SGTC_Round_roundf( sgtc_cvar_killpoints.value );
			pVictim->m_iNukeBonus +=
				SGTC_Round_roundf( sgtc_cvar_diepoints.value );
		}

		// ref @0x18fc20: spawn-camp window (+0x4a8) + crazymode>0 + enemy killer
		// -> killer +0x4a4 = now+10, monster_skull at killer origin +/-500.
		if( pKillerPl && gpGlobals->time < pVictim->m_flSpawnProxDeadline &&
			sgtc_crazymode > 0.0f )
		{
			Vector vecSpot = pKillerPl->pev->origin;
			vecSpot.x += (float)( SGTC_Round_rand( 0, 1000 ) - 500 );
			vecSpot.y += (float)( SGTC_Round_rand( 0, 1000 ) - 500 );
			vecSpot.z += (float)( SGTC_Round_rand( 0, 1000 ) - 500 );
			pKillerPl->m_flSpawnCampTouch = gpGlobals->time + 10.0f;
			CBaseEntity *pSkull = CBaseEntity::Create( "monster_skull", vecSpot,
				pKillerPl->pev->v_angle, NULL );
			if( pSkull )
			{
				((CSGTCSkull *)pSkull)->m_pSkullOwner = pKillerPl;
				pSkull->pev->dmg = 0;	// ref pev+0x16c
			}
		}
	}

	// ref @0x18fdce: remove skulls owned by the victim (+0xc0).
	CBaseEntity *pScan = NULL;
	while( ( pScan = UTIL_FindEntityByClassname( pScan, "monster_skull" ) ) != NULL )
	{
		if( ((CSGTCSkull *)pScan)->m_pSkullOwner == pVictim )
			UTIL_Remove( pScan );
	}

	// ref @0x18fe2e: crazymode == 2.0 -> three skulls at victim origin.
	if( sgtc_crazymode == 2.0f )
	{
		for( int i = 0; i < 3; i++ )
			CBaseEntity::Create( "monster_skull", pVictim->pev->origin,
				g_vecZero, NULL );
	}

	// ref @0x18fe70: GameModeSet 1/3/4 -> victim team ticket decrement +
	// gmsgGtickets/gmsgTtickets WRITE_BYTE broadcasts.
	if( sgtc_GameModeSet == 1 || sgtc_GameModeSet == 3 || sgtc_GameModeSet == 4 )
	{
		if( (int)pVictim->pev->team == SGTC_ENTTEAM_GOAULD && sgtc_G_tickets > 0 )
			sgtc_G_tickets--;
		if( (int)pVictim->pev->team == SGTC_ENTTEAM_TORRI && sgtc_T_tickets > 0 )
			sgtc_T_tickets--;

		MESSAGE_BEGIN( MSG_ALL, gmsgGtickets );
			WRITE_BYTE( SGTC_ClampWireByte( sgtc_G_tickets ) );
		MESSAGE_END();
		MESSAGE_BEGIN( MSG_ALL, gmsgTtickets );
			WRITE_BYTE( SGTC_ClampWireByte( sgtc_T_tickets ) );
		MESSAGE_END();
	}

	// ref this+0x10 (m_bDisableDeathPenalty): forced ChangePlayerTeam kills
	// suppress the stock death handling entirely.
	if( !m_bDisableDeathPenalty )
	{
		CHalfLifeMultiplay::PlayerKilled( pVictim, pKiller, pInflictor );
		UpdateGameMode( pVictim );	// ref vtable+0x34
		RecountTeams( FALSE );		// ref RecountTeams(this, 0)
	}

	// ref @0x18ffe5: death latch for capture-zone fade (+0x464).
	pVictim->m_bSgtcPad464 = 1;

	// ref @0x19000e: tourny + round_official -> elite tier from +0x930 streak.
	if( sgtc_tourny != 0.0f && sgtc_round_official != 0 )
	{
		const int iTier = SGTC_TournyTierFromFocusedKills(
			pVictim->m_iSgtcFocusedKills );
		if( pVictim->m_iSgtcElite < iTier )
		{
			pVictim->m_iSgtcElite = iTier;
			pVictim->m_iSgtcFocusedKills = 0;
		}
	}
}

// ---------------------------------------------------------------------------
// InitHUD — ref @0x19d126 (Unas, ShowVGUIMenu(2)) / @0x17d1f0 (Invis,
// ShowVGUIMenu(11)): both = stock base + TeamNames + RecountTeams + VGUI
// team menu + per-player TeamInfo fan-out.
// ---------------------------------------------------------------------------
void CStargateUnas::InitHUD( CBasePlayer *pPlayer )
{
	// ref @0x18a126: CHalfLifeMultiplay::InitHUD, MSG_ONE TeamNames, then
	// RecountTeams(0), ShowVGUIMenu(2), then per-player TeamInfo fan-out.
	CHalfLifeMultiplay::InitHUD( pPlayer );
	SGTC_SendTeamNames( pPlayer );
	RecountTeams( FALSE );
	pPlayer->ShowVGUIMenu( 2 );

	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *plr = UTIL_PlayerByIndex( i );
		if( plr && IsValidTeam( plr->TeamID() ) )
		{
			MESSAGE_BEGIN( MSG_ONE, gmsgTeamInfo, NULL, pPlayer->edict() );
				WRITE_BYTE( plr->entindex() );
				WRITE_STRING( plr->TeamID() );
			MESSAGE_END();
		}
	}
}

void CStargateInvis::InitHUD( CBasePlayer *pPlayer )
{
	// ref @0x16d1f0: CHalfLifeMultiplay::InitHUD, then MSG_ONE TeamNames
	// (gmsg slot -0x794 → gmsgTeamNames) with all m_teamlist entries, then
	// RecountTeams(0), then ShowVGUIMenu(0xb), then per-player MSG_ONE
	// TeamInfo {BYTE entindex, STRING TeamID} while IsValidTeam(TeamID).
	CHalfLifeMultiplay::InitHUD( pPlayer );
	SGTC_SendTeamNames( pPlayer );
	RecountTeams( FALSE );
	pPlayer->ShowVGUIMenu( 11 );

	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *plr = UTIL_PlayerByIndex( i );
		if( plr && IsValidTeam( plr->TeamID() ) )
		{
			MESSAGE_BEGIN( MSG_ONE, gmsgTeamInfo, NULL, pPlayer->edict() );
				WRITE_BYTE( plr->entindex() );
				WRITE_STRING( plr->TeamID() );
			MESSAGE_END();
		}
	}
}
