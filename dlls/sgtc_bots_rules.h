// sgtc_bots_rules.h — build-13 bot rules API (provisional until Victor's ref
// values are fully wired).  Server-side only; include from the jk_botti
// consumer and from sgtc_testcmd for headless probes.
#pragma once

class CBasePlayer;

typedef enum
{
	SGTC_VIS_UNKNOWN = 0,
	SGTC_VIS_VISIBLE = 1,
	SGTC_VIS_INVISIBLE = 2
} sgtc_vis_t;

typedef enum
{
	SGTC_SHIELD_NONE = 0,
	SGTC_SHIELD_PERSONAL = 1,
	SGTC_SHIELD_SPRINT = 2
} sgtc_shield_t;

typedef enum
{
	SGTC_WCAT_UNKNOWN = 0,
	SGTC_WCAT_MELEE,
	SGTC_WCAT_BULLET,
	SGTC_WCAT_ENERGY,
	SGTC_WCAT_BLAST,
	SGTC_WCAT_ZAT,
	SGTC_WCAT_UTILITY
} sgtc_weapon_category_t;

typedef struct
{
	int id;							// SGTC_WPN_* id
	int category;					// sgtc_weapon_category_t
	float damage;					// -1 = not populated yet
	float range;					// -1 = not populated yet
	int worksVsShield;				// 1 = knife/melee bypass (DMG_CLUB)
	int seesInvis;					// 1 = TER (weapon_reetou active scan)
	const char *ammoClassname;		// SGTC_AMMO_* or NULL
} sgtc_bot_weapon_info_t;

// Visibility of target from observer.  A cloaked target is invisible unless
// the observer is actively sweeping with the TER (weapon_reetou secondary scan).
sgtc_vis_t SGTC_Bots_TargetVisible( const CBasePlayer *observer, const CBasePlayer *target );
// Seconds since the target last fired/was damaged/was TER-revealed; <0 = never.
float SGTC_Bots_TargetRevealAge( const CBasePlayer *target );
// 1 when observer's active weapon is the TER with its cloak scan running.
int SGTC_Bots_ObserverHasTER( const CBasePlayer *observer );

// Shield predicates.
int SGTC_Bots_EnemyShielded( const CBasePlayer *victim );
int SGTC_Bots_ShieldState( const CBasePlayer *victim, int *pShieldType, float *pEnergy01 );
int SGTC_Bots_WeaponBypassesShield( int iWeaponId );

// Weapon knowledge (built from the port's item/weapon defs at init).
void SGTC_Bots_WeaponRulesInit( void );
int SGTC_Bots_WeaponInfo( int iWeaponId, sgtc_bot_weapon_info_t *pOut );

// Runtime mechanics state (gate/iris/ring/door) — implemented in
// sgtc_mech_state.*; declared here so bots have one include.
typedef enum
{
	SGTC_GATE_UNKNOWN = 0,
	SGTC_GATE_IDLE,
	SGTC_GATE_DIALING,
	SGTC_GATE_ARMED,
	SGTC_GATE_OPEN,
	SGTC_GATE_CLOSING,
	SGTC_GATE_COOLDOWN
} sgtc_gate_state_t;

typedef struct
{
	int state;						// sgtc_gate_state_t
	int irisOpen;					// 1 open, 0 closed/blocking
	int busy;						// dialing/occupied
	int canApproach;
	int canUse;
	float dialProgress;				// 0..1
	float cooldown;					// seconds
	int danger;						// whoosh/hazard active
	float whooshTimeLeft;			// seconds
	int destinationId;
} sgtc_gate_state_info_t;

int SGTC_Mech_GateState( const char *pszStableId, sgtc_gate_state_info_t *pOut );
