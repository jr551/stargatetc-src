//
// JK_Botti - be more human!
//
// bot.h
//

#ifndef BOT_H
#define BOT_H

#include <ctype.h>
#include "safe_snprintf.h"

#include "compiler.h"


//
#define JKASSERT(_x_) \
    do { if (unlikely(!!(_x_))) UTIL_AssertConsolePrintf(__FILE__, #_x_, __LINE__); } while(0)


// stuff for Win32 vs. Linux builds

#if defined(__linux__) || defined(__APPLE__)
#define Sleep sleep
#define stricmp strcasecmp
#define strcmpi strcasecmp
#define strnicmp strncasecmp
typedef int BOOL;
#endif


// for detecting submods
#define SUBMOD_HLDM 0
#define SUBMOD_SEVS 1
#define SUBMOD_BUBBLEMOD 2
#define SUBMOD_XDM 3
#define SUBMOD_OP4 4
#define SUBMOD_ARENA 5
#define SUBMOD_SGTC 6


// global defines
#define PLAYER_SEARCH_RADIUS (64 - 4.0)


// define a new bit flag for bot identification
#define FL_THIRDPARTYBOT (1 << 27)


// teamplay_gamerules.h
#define MAX_TEAMNAME_LENGTH     16
#define MAX_TEAMS               32
#define TEAMPLAY_TEAMLISTLENGTH (MAX_TEAMS*MAX_TEAMNAME_LENGTH)


#define LADDER_UNKNOWN  0
#define LADDER_UP       1
#define LADDER_DOWN     2

#define WANDER_LEFT  1
#define WANDER_RIGHT 2

#define RESPAWN_IDLE             1
#define RESPAWN_NEED_TO_RESPAWN  2
#define RESPAWN_IS_RESPAWNING    3

// waypoint goal types
#define WPT_GOAL_NONE      0
#define WPT_GOAL_HEALTH    1
#define WPT_GOAL_ARMOR     2
#define WPT_GOAL_WEAPON    3
#define WPT_GOAL_AMMO      4
#define WPT_GOAL_ITEM      5
#define WPT_GOAL_LOCATION  8
#define WPT_GOAL_TRACK_SOUND  9
#define WPT_GOAL_ENEMY     10

// instant damage (from cbase.h)
#define DMG_CRUSH       (1 << 0) // crushed by falling or moving object
#define DMG_BURN        (1 << 3) // heat burned
#define DMG_FREEZE         (1 << 4) // frozen
#define DMG_FALL        (1 << 5) // fell too far
#define DMG_SHOCK       (1 << 8) // electric shock
#define DMG_DROWN       (1 << 14)   // Drowning
#define DMG_NERVEGAS    (1 << 16)   // nerve toxins, very bad
#define DMG_RADIATION      (1 << 18)   // radiation exposure
#define DMG_DROWNRECOVER   (1 << 19)   // drowning recovery
#define DMG_ACID        (1 << 20)   // toxic chemicals or acid burns
#define DMG_SLOWBURN    (1 << 21)   // in an oven
#define DMG_SLOWFREEZE     (1 << 22)   // in a subzero freezer


#define BOT_SKIN_LEN MAX_TEAMNAME_LENGTH
#define BOT_NAME_LEN 32

#define MAX_BOT_CHAT 100


typedef struct
{
   int    index;
   char * skin;
   char * name;
   int    skill;
   int    top_color;
   int    bottom_color;
} cfg_bot_record_t;

typedef struct breakable_list_s
{
   qboolean inuse;

   struct breakable_list_s * next;
   qboolean material_breakable;
   edict_t *pEdict;
} breakable_list_t;

typedef struct
{
   int  iId;     // weapon ID
   int  iClip;   // amount of ammo in the clip
   int  iAmmo1;  // amount of ammo in primary reserve
   int  iAmmo2;  // amount of ammo in secondary reserve
} bot_current_weapon_t;

typedef struct
{
   qboolean is_used;
   int userid;

   int cfg_bot_index;

   edict_t *pEdict;
   qboolean need_to_initialize;

   char name[BOT_NAME_LEN];
   char skin[BOT_SKIN_LEN];
   int bot_skill;
   int weapon_skill;

   float f_recoil;

   float f_kick_time;
   float f_create_time;
   float f_frame_time;
   float f_bot_spawn_time;

   int chat_percent;
   int taunt_percent;
   int whine_percent;
   int endgame_percent;
   int chat_tag_percent;
   int chat_drop_percent;
   int chat_swap_percent;
   int chat_lower_percent;

   double connect_time;
   double stay_time;

   qboolean b_on_ground;
   qboolean b_on_ladder;
   qboolean b_in_water;
   qboolean b_ducking;
   qboolean b_has_enough_ammo_for_good_weapon;
   qboolean b_only_has_weak_weapons;
   qboolean b_low_health;

   int eagle_secondary_state;

   // things from pev in CBasePlayer...
   float idle_angle;
   float idle_angle_time;
   float blinded_time;

   float f_frame_accumulator;

   float msecval;

   float f_max_speed;
   float f_prev_speed;
   float f_speed_check_time;
   Vector v_prev_origin;

   float f_find_item;
   edict_t *pBotPickupItem;

   int ladder_dir;
   float f_start_use_ladder_time;
   float f_end_use_ladder_time;
   qboolean  waypoint_top_of_ladder;

   float f_wall_check_time;
   float f_wall_on_right;
   float f_wall_on_left;
   float f_dont_avoid_wall_time;
   float f_look_for_waypoint_time;
   float f_jump_time;
   float f_drop_check_time;

   int wander_dir;
   qboolean b_not_maxspeed;
   float f_move_direction;
   float f_strafe_direction;  // 0 = none, negative = left, positive = right
   float f_strafe_time;
   float f_exit_water_time;

   Vector waypoint_origin;
   float f_waypoint_time;
   int curr_waypoint_index;
   int prev_waypoint_index[5];
   float f_random_waypoint_time;
   int waypoint_goal;
   float f_waypoint_goal_time;
   float prev_waypoint_distance;
   int wpt_goal_type;
   int trace_last_stuck_wpt;         // dedup: last wpt logged for stuck trace
   qboolean b_narrow_path;           // currently on narrow geometry (ramp, bridge, cliff)
   Vector v_prev_waypoint_origin;    // previous waypoint origin for path-following steering
   edict_t *pTrackSoundEdict;        // used when wpt_goal_type == WPT_GOAL_TRACK_SOUND
   float f_track_sound_time;         // how long we track sound?

#define EXCLUDE_POINTS_COUNT 10
   int exclude_points[EXCLUDE_POINTS_COUNT+1];  // ten item locations + 1 null

   float f_last_item_found;

   edict_t *pBotEnemy;
   float f_bot_see_enemy_time;
   Vector v_bot_see_enemy_origin;
   float f_next_find_visible_sound_enemy_time;

   // TAC targeting: throttle for the "did a player appear?" preemption scan.
   float f_tac_player_check_time;

   float f_last_time_attacked;

   edict_t *killer_edict;
   qboolean  b_bot_say;
   float f_bot_say;
   char  bot_say_msg[256];
   float f_bot_chat_time;
   qboolean b_bot_endgame;

   float f_duck_time;

   float f_random_jump_time;
   float f_random_jump_duck_time;
   float f_random_jump_duck_end;
   float f_random_duck_time;
   int prev_random_type;

   qboolean b_longjump;
   float f_combat_longjump;
   qboolean b_combat_longjump;
   float f_longjump_time;
   qboolean b_longjump_do_jump;

   float f_sniper_aim_time;

   float f_shoot_time;
   float f_primary_charging;
   float f_secondary_charging;
   float f_satchel_detonate_time;  // >0 = satchels deployed, value = force-detonate deadline
   float f_satchel_check_time;     // throttle for proximity checks
   qboolean b_satchel_detonating;  // TRUE = committed to detonation, waiting for weapon switch
   int   charging_weapon_id;
   float f_grenade_search_time;
   float f_grenade_found_time;
   int current_weapon_index;
   float current_opt_distance;

   float f_move_speed;
   float f_pause_time;
   float f_pause_look_time;
   float f_sound_update_time;

   qboolean  b_see_tripmine;
   qboolean  b_shoot_tripmine;
   Vector v_tripmine;
   edict_t *tripmine_edict;

   qboolean  b_use_health_station;
   float f_use_health_time;
   qboolean  b_use_HEV_station;
   float f_use_HEV_time;

   qboolean  b_use_button;
   float f_use_button_time;
   qboolean  b_lift_moving;

   Vector v_use_target;

   int   logo_percent;
   qboolean  b_spray_logo;
   float f_spray_logo_time;
   char  logo_name[16];
   int   top_color;
   int   bottom_color;

   float f_reaction_target_time;  // time when enemy targeting starts

   float f_weaponchange_time;

   float f_current_hearing_sensitivity;

   qboolean b_set_special_shoot_angle;
   float f_special_shoot_angle;

   bot_current_weapon_t current_weapon;  // one current weapon for each bot
   int m_rgAmmo[MAX_AMMO_SLOTS];  // total ammo amounts (1 array for each bot)

   // SGTC: no-waypoint roam fallback (dlls/sgtc_bots.cpp: sgtc_bot_roam).
   Vector v_roam_target;
   Vector v_roam_spawn;             // spawn the roam search is centred on
   float f_roam_target_time;        // deadline to pick a new roam point
   qboolean b_roam_target_set;
   qboolean b_roam_have_spawn;

   // SGTC: stuck escalation (jump -> reroute -> rescue, sgtc_bot_rescue).
   float f_stuck_time;              // 0 = moving; else time progress stalled
   int stuck_action_stage;          // 0 none, 1 jump, 2 reroute, 3 rescue
   qboolean b_stuck_event_pending;  // sgtc_bots.cpp drains this into a `stuck` event
   float f_stuck_event_duration;
   char stuck_event_action[12];

   // SGTC: radio orders and chatter (bot_radio.cpp).
   int radio_order;                 // SgtcRadioOrder or SGTC_RADIO_ORDER_NONE
   int radio_order_msg;             // SgtcRadioMessage that started it
   int radio_order_target;          // entindex of the teammate to follow/help, 0 = none
   Vector v_radio_order_origin;     // hold / push / fallback destination
   float f_radio_order_time;        // order deadline (gpGlobals->time)
   float f_radio_order_accept;      // reaction delay: order applies from here
   float f_radio_next_goal;         // next time to refresh the waypoint goal
   qboolean b_radio_order_arrived;
   qboolean b_radio_order_said;     // arrival line already sent
   int radio_pending_msg;           // msgId queued for emission, -1 = none (0 is a real id)
   float f_radio_pending_time;      // when the queued line may be emitted
   float f_radio_next_time;         // per-bot rate gate (bot_radio_rate)
   float f_radio_chattiness;        // director multiplier, default 1.0
   int radio_last_enemy;            // entindex of the last enemy we reported, -1 = none
   float f_radio_enemy_report_time; // last "watch out enemy" call, 8 s de-duplication

   // SGTC death telemetry (dlls/sgtc_bots.cpp). The per-frame tracker observes
   // a death only after the victim has already been processed, and the mod
   // parks every dead player off-map (SGTC_OFFMAP_COORD). The live writer is
   // SGTC_Bots_OnPlayerKilled(), called from CBasePlayer::Killed() while the
   // victim's origin and g_pevLastInflictor are still valid. The DeathMsg
   // writes in bot_client.cpp are inert in the native build (that user message
   // is not mirrored); the tracker consumes and clears this snapshot.
   Vector v_death_origin;
   qboolean b_death_snapshot_valid;
   char sz_death_weapon[32];
} bot_t;

#ifdef JK_BOTTI_NATIVE
// SGTC glue state, defined in dlls/sgtc_bots.cpp and refreshed once per frame
// from the sgtc_bot_* cvars. Bots read them for the roam, unstick and radio
// paths; keeping the cache here means the bot code never has to know about
// cvar registration or the engine's cvar lookup.
extern int sgtc_bot_roam_enabled;   // sgtc_bot_roam: wander when no waypoints exist
extern int sgtc_bot_rescue_enabled; // sgtc_bot_rescue: respawn a still-stuck bot
extern int sgtc_bot_radio_mode;     // bot_radio: 0 off, 1 obey orders, 2 also talk
extern float sgtc_bot_radio_rate;   // bot_radio_rate: seconds between one bot's calls

// Relay one radio line (an SgtcRadioMessage id) from `speaker` to the team via
// the mod's Radio usermessage (dlls/sgtc_bots_radio.cpp). Bots use it exactly
// like a human teammate's line.
void SGTC_Bots_RadioSend( edict_t *speaker, int msgId );

// Edict-budget gates (dlls/sgtc_bots.cpp): 1 = room for another bot, 0 =
// refuse and log `[sgtc_bots] spawn refused: ~N/M edicts used`. The margin is
// loadout-aware: 31 edicts while mp_freeguns > 1 (the CP world grant), 16 for a
// normal class kit. BotCreate passes SGTC_Bots_EdictBudgetMarginForLoadout() to
// the per-spawn gate so every spawn path (quota, jk_botti addbot, config) is
// covered; `sgtc_bot_add` passes the same margin to the count-aware gate before
// its spawn loop. C linkage so tests/stub_engine.c can drive the exact
// production wrappers.
#ifdef __cplusplus
extern "C" {
#endif
int SGTC_Bots_EdictBudgetMarginForLoadout( void );
int SGTC_Bots_EdictBudgetAllowSpawn( int margin );
int SGTC_Bots_EdictBudgetAllowSpawnCount( int count, int margin );
#ifdef __cplusplus
}
#endif
#endif

#define MAX_TEAMS 32
#define MAX_TEAMNAME_LENGTH 16

#define MAX_FLAGS  5

typedef struct {
   edict_t *edict;
   int  team_no;
} FLAG_S;

#define MAX_BACKPACKS 100

typedef struct {
   edict_t *edict;
   int  armor;   // 0=none
   int  health;  // 0=none
   int  ammo;    // 0=none
   int  team;    // 0=all, 1=Blue, 2=Red, 3=Yellow, 4=Green
} BACKPACK_S;


#define MAX_SKINS 200

typedef struct
{
   qboolean skin_used;
   char model_name[32];
   char bot_name[32];
} skin_t;

typedef struct
{
   qboolean can_modify;
   char text[81];
} bot_chat_t;

typedef struct
{
   char identification[4];    // should be WAD2 (or 2DAW) or WAD3 (or 3DAW)
   int  numlumps;
   int  infotableofs;
} wadinfo_t;

typedef struct
{
   int  filepos;
   int  disksize;
   int  size;              // uncompressed
   char type;
   char compression;
   char pad1, pad2;
   char name[16];          // must be null terminated
} lumpinfo_t;

extern bot_t bots[32];
extern int submod_id;
extern int m_spriteTexture;

//void UpdateClientData(const struct edict_s *ent, int sendweapons, struct clientdata_s *cd);

#include "util.h"
#include "bot_func.h"
#include "bot_inline_funcs.h"

#endif // BOT_H
