// sgtc_bots_classes.h - per-class bot playstyle profiles and ability decisions.
//
// Pure interface, same contract as sgtc_bots_loadout.h: plain C linkage, no
// engine calls, no globals, no allocation. dlls/sgtc_bots_classplay.cpp reads
// the live player/enemy state and calls these; tests compile this module on
// the host (tests/features/test_bot_class_profiles.py) and through the stub
// engine (tests/stub_engine.c, situation "botclass").
//
// Source of truth for the values below is the game code, not this header:
//   classes/stats      dlls/sgtc_rules_spawn.cpp   (SGTCSClassDef table)
//   buy allow-list     dlls/sgtc_rules_spawn.cpp   (SetWeaponAllow)
//   talents            dlls/sgtc_player.cpp        (CBasePlayer::Talent)
//   shield/cloak fuel  dlls/sgtc_player.cpp        (SGTC_ShieldThinker)
//   weapons            dlls/sgtc_wpn_*.cpp
// docs/BOTS-CLASSES.md is the prose inventory behind every value here.
#pragma once
#ifndef SGTC_BOTS_CLASSES_H
#define SGTC_BOTS_CLASSES_H

#include "sgtc_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

// Longest kit preference list in the table, including the -1 terminator.
#define SGTC_BOT_PROFILE_MAX_KIT 12

// What the bot is for. The director (PR #176) may override this per bot with
// Bot_SetRole(); SGTC_Bots_ClassProfile() gives the class default.
typedef enum
{
	SGTC_BOT_ROLE_NONE = 0,
	SGTC_BOT_ROLE_ASSAULT,	// push and trade shots at mid range
	SGTC_BOT_ROLE_SUPPORT,	// heal/resupply allies, fight second line
	SGTC_BOT_ROLE_SNIPER,	// hold long sightlines, relocate after a kill
	SGTC_BOT_ROLE_AMBUSHER,	// flank with cloak, strike from behind
	SGTC_BOT_ROLE_DEFENDER,	// hold a chokepoint/objective with TACs and shield
	SGTC_BOT_ROLE_SCOUT,	// move first, spot and disrupt
	SGTC_BOT_ROLE_COUNT
} sgtc_bot_role_t;

// Abilities the class can actually use, as a bitmask. Every bit maps to code in
// this tree; nothing here is aspirational.
enum
{
	SGTC_BOT_AB_SHIELD   = 1 << 0,	// weapon_shield (blocks Zat stun only)
	SGTC_BOT_AB_CLOAK    = 1 << 1,	// weapon_invis
	SGTC_BOT_AB_TAC      = 1 << 2,	// weapon_tacgun
	SGTC_BOT_AB_SNIPER   = 1 << 3,	// PSG1 / Zat'arc, hold sightlines
	SGTC_BOT_AB_HEAL     = 1 << 4,	// weapon_medic / weapon_goaregen
	SGTC_BOT_AB_MELEE    = 1 << 5,	// melee is a real option, close distance
	SGTC_BOT_AB_GRENADE  = 1 << 6,	// carries gas/HE grenades
	SGTC_BOT_AB_TALENT   = 1 << 7	// class talent via the `talent` command
};

// The class talent, matching the switch in CBasePlayer::Talent().
typedef enum
{
	SGTC_BOT_TALENT_NONE = 0,
	SGTC_BOT_TALENT_GRENADE,		// Torri 1: free hand-grenade toss
	SGTC_BOT_TALENT_ZATBOOM,		// Torri 2: zat overload toss
	SGTC_BOT_TALENT_SNEAK,			// Torri 3: crouch/sneak toggle
	SGTC_BOT_TALENT_SHIELD_BUFF,	// Torri 4: stamina + shield timers, 10 s
	SGTC_BOT_TALENT_EMITTER,		// Torri 5: deploy ally shield emitter
	SGTC_BOT_TALENT_LARVE,			// Goa'uld 1: launch larve
	SGTC_BOT_TALENT_ARMOR_TOGGLE,	// Goa'uld 2: HUD-only armor display
	SGTC_BOT_TALENT_FLASH,			// Goa'uld 3: gacgoa flash (needs gas grenade item)
	SGTC_BOT_TALENT_TRAPBOX,		// Goa'uld 4: place up to two trapboxes
	SGTC_BOT_TALENT_EMITTER_UNLOAD	// Goa'uld 5: unload an owned emitter
} sgtc_bot_talent_t;

typedef struct
{
	int team;			// SgtcTeam (1 Torri, 2 Goa'uld)
	int cls;			// SgtcClass 1..5
	const char *name;		// canonical class name, e.g. "System Lord"
	int role;			// sgtc_bot_role_t
	int abilities;			// SGTC_BOT_AB_* mask
	int talent;			// sgtc_bot_talent_t

	// Preferred engagement range for this class, world units. Used for
	// positioning and weapon choice; not a hard limit.
	int range_min;
	int range_max;

	// One representative weapon per range bucket (classname), NULL when the
	// class has no ranged option in that bucket.
	const char *weapon_short;
	const char *weapon_mid;
	const char *weapon_long;

	// Spawn buy preference, as SgtcWeaponSlot indices (-1 ends the list). Bots
	// do not drive the buy menu, so PlayerSpawn turns this order into the same
	// gond argument list a human would send and hands it to the normal grant
	// path; SGTC_Bots_SelectKitLoadout() applies the buy rules first. The
	// order is ability item first, then the class's weapons from cheap to
	// expensive. Team melee is granted by PlayerSpawn and is not repeated.
	int kit[SGTC_BOT_PROFILE_MAX_KIT];
} sgtc_bot_class_profile_t;

// Never returns NULL for a valid (team, cls); returns NULL otherwise.
const sgtc_bot_class_profile_t *SGTC_Bots_ClassProfile( int team, int cls );

// "assault" / "support" / ... ; "none" for an unknown role.
const char *SGTC_Bots_RoleName( int role );

// "shield" / "cloak" / "tac" / "heal" / "talent" for telemetry; a
// comma-separated capability string is built by the caller from the mask.
const char *SGTC_Bots_AbilityName( int ability );

// Buy slots in p (stops at -1). Returns the count, 0 for a NULL profile.
int SGTC_Bots_ClassKitCount( const sgtc_bot_class_profile_t *p );

// Turns the class preference into a gond argument list for the normal spawn
// grant. Walks p->kit in order and keeps a slot only when the same checks that
// SGTC_SanitizeGondLoadout() applies at grant time accept it: allowed[slot]
// must be 1 (the player's m_iProgressTeam allow-list), cost[slot] must be
// positive, the running total must fit budget, and the buy-menu group caps
// (SGTC_WSLOT_MAX_PRIMARY / SGTC_WSLOT_MAX_SPECIAL) must still hold. Duplicate
// slots and out-of-range indices are skipped. out_gond (SGTC_NUM_WEAPON_SLOTS
// entries) is zeroed first. Returns the number of slots selected; 0 for bad
// input or when nothing is affordable.
//
// Selecting in preference order is what makes the list an order: the grant
// path itself walks slots in index order, so a bot that only marked its whole
// list would get the lowest-index affordable items instead.
int SGTC_Bots_SelectKitLoadout( const sgtc_bot_class_profile_t *p,
	const int *allowed, const int *cost, int budget, int *out_gond );

// mp_wepselmode 2 variant: the class's first fixed pack (pack 1), because the
// pack menu neither prices nor allow-lists its slots and the grant path only
// accepts a gond list that exactly matches one of the class packs. out_gond is
// zeroed first. Returns the number of slots written.
int SGTC_Bots_SelectPackLoadout( const sgtc_bot_class_profile_t *p, int *out_gond );

// ---------------------------------------------------------------------------
// Ability decisions. Every function is deterministic for its inputs (rng is
// the caller's per-bot stream, so two runs with the same sgtc_bot_seed replay
// identically) and returns:
//    1  do it (raise the shield / cloak / place a TAC / use the talent)
//   -1  undo it (drop the shield / uncloak)
//    0  hold the current state
// Skill 0..5 scales both timing and waste: low-skill bots act late, sometimes
// at bad energy, and miss follow-ups; high-skill bots wait for a full charge
// and act on the threat, not on the timer.
// ---------------------------------------------------------------------------

typedef struct
{
	int skill;			// 0..5
	float health;			// 0..1 of max health
	float charge;			// m_iShieldCharge, 0..9
	int shield_up;			// m_bShieldWall
	int enemy_visible;
	float enemy_distance;		// units; <0 = no enemy
	int under_fire;			// damaged within the last ~2 s
	float out_of_combat;		// seconds since last damage/enemy sighting
	unsigned int rng;
} sgtc_bot_shield_ctx_t;

int SGTC_Bots_WantsShield( const sgtc_bot_class_profile_t *p,
	const sgtc_bot_shield_ctx_t *c );

typedef struct
{
	int skill;
	float charge;			// m_iShieldCharge, 0..9
	int cloaked;			// m_bShieldSprint
	int enemy_visible;
	float enemy_distance;
	float out_of_combat;
	float cloaked_time;		// seconds the cloak has been held; <0 unknown/none
	unsigned int rng;
} sgtc_bot_cloak_ctx_t;

int SGTC_Bots_WantsCloak( const sgtc_bot_class_profile_t *p,
	const sgtc_bot_cloak_ctx_t *c );

typedef struct
{
	int skill;
	int tac_ammo;			// remaining Tacgun ammo, 0..3
	int tac_near;			// own TACs within mp_tacdistance of the point
	float objective_distance;	// units to the nearest objective; <0 = none
	int at_chokepoint;		// route/waypoint classified as a chokepoint
	int enemy_visible;
	float enemy_distance;
	float front_moved;		// seconds since the bot last placed a TAC here
	unsigned int rng;
} sgtc_bot_tac_ctx_t;

int SGTC_Bots_WantsTacPlace( const sgtc_bot_class_profile_t *p,
	const sgtc_bot_tac_ctx_t *c );

typedef struct
{
	int skill;
	float ally_health;		// 0..1
	float ally_distance;		// units; <0 = no wounded ally
	int ally_in_front;		// inside the bot's aim cone
	int ammo;			// medgoo / goaregen reserve
	int enemy_visible;
	unsigned int rng;
} sgtc_bot_heal_ctx_t;

int SGTC_Bots_WantsHeal( const sgtc_bot_class_profile_t *p,
	const sgtc_bot_heal_ctx_t *c );

typedef struct
{
	int skill;
	int talent;			// profile->talent, copied for convenience
	int cooldown_ready;
	int enemy_visible;
	float enemy_distance;		// units; <0 = no enemy
	int allies_near;		// allies within ~300 units
	int has_item;			// required item in inventory (gacgoa, zat, ...)
	unsigned int rng;
} sgtc_bot_talent_ctx_t;

int SGTC_Bots_WantsTalent( const sgtc_bot_class_profile_t *p,
	const sgtc_bot_talent_ctx_t *c );

#ifdef __cplusplus
}
#endif

#endif // SGTC_BOTS_CLASSES_H
