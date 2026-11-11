#pragma once

#include "extdll.h"

class CBasePlayer;

void SGTC_Bots_Init(DLL_FUNCTIONS *gameApi, enginefuncs_t *engineApi);
void SGTC_Bots_Shutdown(void);

void SGTC_Bots_OnSpawn(int result, edict_t *pent);
void SGTC_Bots_OnSpawnPost(int result, edict_t *pent);
void SGTC_Bots_OnKeyValuePost(edict_t *pent, KeyValueData *pkvd);

void SGTC_Bots_ClientConnect(edict_t *pEntity, const char *pszName, const char *pszAddress, char szRejectReason[128]);
void SGTC_Bots_ClientPutInServer(edict_t *pEntity);
void SGTC_Bots_ClientDisconnect(edict_t *pEntity);
void SGTC_Bots_ClientCommand(edict_t *pEntity);
void SGTC_Bots_StartFrame(void);
void SGTC_Bots_ServerDeactivate(void);

// Native death telemetry. Called from CBasePlayer::Killed(), before the
// victim's PostThink can park it at SGTC_OFFMAP_COORD. `weapon_name` follows
// DeathMsg's third field semantics (inflictor class, the killer's active weapon
// when the inflictor is the killer, "world" when there is no inflictor,
// "teammate" for a client team kill); the slot capture strips the
// weapon_/monster_/func_ prefix exactly as DeathNotice() does. A no-op when
// victim_index is not a bot.
void SGTC_Bots_OnPlayerKilled(int victim_index, const char *weapon_name);

// Slot-level half of the above, exported with C linkage so tests/stub_engine.c
// can exercise the same capture the Killed() path uses (not just the resolver).
extern "C" {
void SGTC_Bots_CaptureDeathState(int slot, const float origin[3], const char *weapon_name);
int SGTC_Bots_GetDeathState(int slot, float out_origin[3], char *out_weapon, int weapon_size);
}

void SGTC_Bots_PM_Move(struct playermove_s *ppmove, int server);
void SGTC_Bots_CmdStart(const edict_t *player, const struct usercmd_s *cmd, unsigned int random_seed);
void SGTC_Bots_PlayerPostThinkPost(edict_t *pEdict);

// Read once per bot think by SGTC_Bots_ClassPlay; the value is a live cvar.
int SGTC_Bots_Enabled(void);
BOOL SGTC_Bots_IsBotPlayer( CBasePlayer *pPlayer );	// bot registry check (works before FL_FAKECLIENT is set)

// Seed base handed to fast_random_seed() (sgtc_bot_seed, or the wall clock when
// the cvar is 0). Per-bot class-play streams are derived from it.
unsigned int SGTC_Bots_SeedBase(void);

// Ability telemetry: one `ability` SGTCEVENT line (fields ability/cls/detail).
void SGTC_Bots_EmitAbility(const char *bot_name, const char *ability, int cls, const char *detail);

// ---------------------------------------------------------------------------
// Class play (dlls/sgtc_bots_classplay.cpp)
// ---------------------------------------------------------------------------

// Flattened view of the per-class profile, for the director / console tools.
typedef struct
{
	int team;			// SgtcTeam
	int cls;			// SgtcClass 1..5
	int role;			// sgtc_bot_role_t (effective: override or class default)
	int abilities;			// SGTC_BOT_AB_* mask
	int talent;			// sgtc_bot_talent_t
	int range_min;
	int range_max;
	const char *class_name;
} sgtc_bot_class_view_t;

// 0 on success, -1 for an invalid slot/bot/unresolved live class.
int Bot_GetClassProfile(int bot_index, sgtc_bot_class_view_t *out);

// Role override for the director. 0 = auto (class default).
// Returns 0 on success, -1 for an invalid slot or role.
int Bot_SetRole(int bot_index, int role);

// Effective role, or -1 when the slot/bot is unknown.
int Bot_GetRole(int bot_index);

// Called once per bot think, before BotRunPlayerMove. ORs ability presses into
// pBot.pEdict->v.button and issues weapon switches through FakeClientCommand.
// Declared only where bot_t is visible (bots/jk_botti/bot.cpp and
// sgtc_bots_classplay.cpp): this header is included by game TUs that must not
// pull in the jk_botti headers, whose JK_Botti_* prototypes clash with the
// extern "C" ones in native/jk_botti_native_api.h.

// `sgtc_bot_kit` (default 1): bot classes take their spawn loadout through the
// normal buy rules instead of a direct grant.
int SGTC_Bots_KitEnabled(void);

// Fills the bot's gond argument list with the class buy preference for the
// current team/class, so PlayerSpawn's normal grant (after
// SGTC_SanitizeGondLoadout) hands out exactly what a human could buy. Fake
// clients only; a no-op when sgtc_bot_kit is off or the class is unresolved.
// Call from PlayerSpawn after SetWeaponAllow and before sanitising.
void SGTC_Bots_PrepareSpawnLoadout(CBasePlayer *pl);

// Direct grant fallback for the bots whose class is resolved only after
// PlayerSpawn has already run (the first life: the spawn grant has no gond
// list yet).  Grants the class pack/kit list plus reserve ammo, and is a
// no-op once the bot already carries a weapon.
void SGTC_Bots_GiveSpawnLoadout(CBasePlayer *pl);

// Drops every per-bot class-play timer, pending action and the objective
// cache. Called on worldspawn: gpGlobals->time restarts with the map, so a
// deadline from the previous map would suppress abilities for minutes.
void SGTC_Bots_ClassResetAll(void);

void SGTC_Bots_RegisterMapNavLinks(void);

// Gate relay observer, called by FireTargets() before it dispatches a target.
// It only updates the bot timing model (g1tele_x_on / g2tele_x_on and
// their _off counterparts); no entity state is changed.
void SGTC_Bots_ObserveGateRelay(const char *target, float now);
