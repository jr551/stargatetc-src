// sgtc_bots_classes.cpp - per-class bot profiles and ability decisions.
// See sgtc_bots_classes.h. Pure code: no engine types, no mutable globals,
// no allocation. The table mirrors docs/BOTS-CLASSES.md; the class/talent
// numbers come from game_shared/sgtc_enums.h and CBasePlayer::Talent().
//
// Every decision function is deterministic for its inputs. The caller passes
// the bot's own PRNG value in the context; it is only consulted by the
// low-skill reaction gates, and the same draw always gives the same answer.

#include <stddef.h>

#include "sgtc_bots_classes.h"
#include "sgtc_packs.h"

// ---------------------------------------------------------------------------
// Tuning constants. Skill is 0..5.
//
// Low-skill bots act late: they only notice an opportunity on a percentage of
// ticks (the caller's rng stream) and they fire the ability at a lower charge.
// High-skill bots wait for a fuller charge, and the sharp tier (skill >= 4)
// may reposition with cloak or drop a TAC from an advanced position.
// ---------------------------------------------------------------------------
static const int SGTC_SKILL_LOW = 3;		// "skill < 3" is the sloppy tier
static const int SGTC_SKILL_CLOAK_LOW = 2;	// cloak gets its own cut-off
static const int SGTC_SKILL_HEAL_LOW = 2;
static const int SGTC_SKILL_TALENT_LOW = 2;
static const int SGTC_SKILL_SHARP = 4;		// "skill >= 4" reads the threat

// Shield wall: charge is 0..9. Low skill raises at 4, everyone else at 6.
static const float SGTC_SHIELD_CHARGE_LOW_SKILL = 4.0f;
static const float SGTC_SHIELD_CHARGE_HIGH_SKILL = 6.0f;
static const float SGTC_SHIELD_THREAT_RANGE = 900.0f;	// raise for a threat closer than this
static const float SGTC_SHIELD_DROP_OOC = 3.0f;		// seconds; lower the wall when idle
static const float SGTC_SHIELD_MAX_RAISE_OOC = 5.0f;	// never raise a wall this idle

// Cloak: 4 is enough to disappear, 6 for the low-skill tier. Break the cloak
// when a threat gets inside 700 units, or to reposition from beyond 1200.
static const float SGTC_CLOAK_MIN_CHARGE = 4.0f;
static const float SGTC_CLOAK_LOW_SKILL_CHARGE = 6.0f;
static const float SGTC_CLOAK_MIN_OOC = 0.5f;
static const float SGTC_CLOAK_BREAK_RANGE = 700.0f;
static const float SGTC_CLOAK_REPOSITION_RANGE = 1200.0f;
// Hard cap on a single cloak: the cell drains while active, and a bot that
// stays cloaked forever reads as "invisible team" to humans.  Also drop the
// cloak when there is no enemy in sight for a few seconds.
static const float SGTC_CLOAK_MAX_HOLD = 10.0f;
static const float SGTC_CLOAK_NO_ENEMY_HOLD = 3.0f;

// TAC: two own TACs near the point is the engine's placement limit. Drop one
// within 700 units of the objective (or on a chokepoint), reposition at 900.
static const int SGTC_TAC_MAX_NEAR = 2;
static const float SGTC_TAC_OBJECTIVE_RANGE = 700.0f;
static const float SGTC_TAC_ENEMY_CLEAR_RANGE = 900.0f;
static const float SGTC_TAC_REPOSITION_MOVE = 20.0f;
static const float SGTC_TAC_REPOSITION_RANGE = 900.0f;

// Heal only a teammate that is actually wounded and in the aim cone.
static const float SGTC_HEAL_MAX_RANGE = 450.0f;
static const float SGTC_HEAL_WOUNDED = 0.75f;

// Percentage of ticks on which a low-skill bot notices the opportunity.
static const int SGTC_GATE_SHIELD_BASE = 30;	// + 10 per skill point below 3
static const int SGTC_GATE_PER_SKILL = 10;
static const int SGTC_GATE_CLOAK = 50;
static const int SGTC_GATE_TAC = 50;
static const int SGTC_GATE_HEAL = 40;
static const int SGTC_GATE_TALENT = 35;

// True when the caller's rng draw falls inside the gate percentage.
static int sgtc_gate( unsigned int rng, int percent )
{
	if( percent <= 0 )
		return 0;
	if( percent >= 100 )
		return 1;
	return (int)( rng % 100u ) < percent;
}

// ---------------------------------------------------------------------------
// The profile table. Order is Torri 1..5 then Goa'uld 1..5; lookup is linear
// (ten entries, called a few times per bot per spawn, never per frame).
// ---------------------------------------------------------------------------
static const sgtc_bot_class_profile_t s_sgtc_bot_profiles[] =
{
	// Torri 1: O'Neill. Line infantry with every tool: TAC, sniper, heal,
	// grenades and the free-grenade talent.
	{
		SGTC_TEAM_TORRI, SGTC_CLASS_ONEILL, "O'Neill", SGTC_BOT_ROLE_ASSAULT,
		SGTC_BOT_AB_TAC | SGTC_BOT_AB_SNIPER | SGTC_BOT_AB_HEAL |
			SGTC_BOT_AB_GRENADE | SGTC_BOT_AB_TALENT,
		SGTC_BOT_TALENT_GRENADE, 300, 1200,
		"weapon_mp5", "weapon_m16", "weapon_psg1",
		{
			SGTC_WSLOT_TACGUN, SGTC_WSLOT_M92S, SGTC_WSLOT_ZAT,
			SGTC_WSLOT_HANDGRENADE, SGTC_WSLOT_M16, SGTC_WSLOT_MP5,
			SGTC_WSLOT_PSG1, SGTC_WSLOT_MEDIC, SGTC_WSLOT_SMOKEGRENADE, -1
		}
	},

	// Torri 2: Carter. Second-line support: SMG/P90, zat, heal pen, TAC and
	// the zat-overload talent.
	{
		SGTC_TEAM_TORRI, SGTC_CLASS_CARTER, "Carter", SGTC_BOT_ROLE_SUPPORT,
		SGTC_BOT_AB_TAC | SGTC_BOT_AB_HEAL | SGTC_BOT_AB_TALENT |
			SGTC_BOT_AB_GRENADE,
		SGTC_BOT_TALENT_ZATBOOM, 200, 900,
		"weapon_mp5", "weapon_p90", "weapon_zat",
		{
			SGTC_WSLOT_TACGUN, SGTC_WSLOT_ZAT, SGTC_WSLOT_M92S,
			SGTC_WSLOT_P90, SGTC_WSLOT_MP5, SGTC_WSLOT_MEDIC,
			SGTC_WSLOT_GOAREGEN, SGTC_WSLOT_SMOKEGRENADE, -1
		}
	},

	// Torri 3: Jackson. Sniper: P90 up close, PSG1 for the sightlines, and
	// crouch/sneak for the long hold.
	{
		SGTC_TEAM_TORRI, SGTC_CLASS_JACKSON, "Jackson", SGTC_BOT_ROLE_SNIPER,
		SGTC_BOT_AB_SNIPER | SGTC_BOT_AB_HEAL | SGTC_BOT_AB_TALENT,
		SGTC_BOT_TALENT_SNEAK, 700, 2500,
		"weapon_p90", "weapon_p90", "weapon_psg1",
		{
			SGTC_WSLOT_M92S, SGTC_WSLOT_PSG1, SGTC_WSLOT_ZAT,
			SGTC_WSLOT_P90, SGTC_WSLOT_MP5, SGTC_WSLOT_MEDIC,
			SGTC_WSLOT_HANDGRENADE, SGTC_WSLOT_MUSTARDGRENADE, -1
		}
	},

	// Torri 4: Teal'c. Close assault: shotguns and the shield buff that also
	// empowers the staff/knife melee.
	{
		SGTC_TEAM_TORRI, SGTC_CLASS_TEALC, "Teal'c", SGTC_BOT_ROLE_ASSAULT,
		SGTC_BOT_AB_TAC | SGTC_BOT_AB_HEAL | SGTC_BOT_AB_MELEE |
			SGTC_BOT_AB_TALENT,
		SGTC_BOT_TALENT_SHIELD_BUFF, 0, 900,
		"weapon_usas", "weapon_spas", "weapon_m16",
		{
			SGTC_WSLOT_TACGUN, SGTC_WSLOT_M92S, SGTC_WSLOT_ZAT,
			SGTC_WSLOT_SPAS, SGTC_WSLOT_M16, SGTC_WSLOT_USAS,
			SGTC_WSLOT_MINE, SGTC_WSLOT_MEDIC, SGTC_WSLOT_SMOKEGRENADE, -1
		}
	},

	// Torri 5: Commander. Defensive anchor: TAC + smoke, sniper rifle for the
	// chokepoint, shield emitter for the team.
	{
		SGTC_TEAM_TORRI, SGTC_CLASS_COMMANDER, "Commander", SGTC_BOT_ROLE_DEFENDER,
		SGTC_BOT_AB_TAC | SGTC_BOT_AB_SNIPER | SGTC_BOT_AB_HEAL |
			SGTC_BOT_AB_GRENADE | SGTC_BOT_AB_TALENT,
		SGTC_BOT_TALENT_EMITTER, 300, 1800,
		"weapon_spas", "weapon_m16", "weapon_psg1",
		{
			SGTC_WSLOT_M92S, SGTC_WSLOT_M16, SGTC_WSLOT_PSG1,
			SGTC_WSLOT_HANDGRENADE, SGTC_WSLOT_MEDIC, SGTC_WSLOT_MP5,
			SGTC_WSLOT_P90, SGTC_WSLOT_SPAS, SGTC_WSLOT_SMOKEGRENADE, -1
		}
	},

	// Goa'uld 1: System Lord. Ambusher: hand device, zat, cloak, shield and
	// the larve launcher; the widest kit in the table.
	{
		SGTC_TEAM_GOAULD, SGTC_CLASS_SYSLORD, "System Lord", SGTC_BOT_ROLE_AMBUSHER,
		SGTC_BOT_AB_SHIELD | SGTC_BOT_AB_CLOAK | SGTC_BOT_AB_TAC |
			SGTC_BOT_AB_HEAL | SGTC_BOT_AB_SNIPER | SGTC_BOT_AB_TALENT,
		SGTC_BOT_TALENT_LARVE, 150, 1500,
		"weapon_zat", "weapon_zatarc", "weapon_main",
		{
			SGTC_WSLOT_TACGUN, SGTC_WSLOT_ZAT, SGTC_WSLOT_MAIN,
			SGTC_WSLOT_ZATARC, SGTC_WSLOT_SHIELD, SGTC_WSLOT_INVIS,
			SGTC_WSLOT_LARVE, SGTC_WSLOT_MEDIC, SGTC_WSLOT_GOAREGEN, -1
		}
	},

	// Goa'uld 2: Anubis. Assault: lance and zat'arc, heal plus the larve
	// launcher; armor toggle is cosmetic and never worth a bot action.
	{
		SGTC_TEAM_GOAULD, SGTC_CLASS_ANUBIS, "Anubis", SGTC_BOT_ROLE_ASSAULT,
		SGTC_BOT_AB_TAC | SGTC_BOT_AB_HEAL | SGTC_BOT_AB_SNIPER |
			SGTC_BOT_AB_TALENT,
		SGTC_BOT_TALENT_ARMOR_TOGGLE, 200, 1600,
		"weapon_zat", "weapon_zatarc", "weapon_lance",
		{
			SGTC_WSLOT_TACGUN, SGTC_WSLOT_ZAT, SGTC_WSLOT_LANCE,
			SGTC_WSLOT_ZATARC, SGTC_WSLOT_BATSUP, SGTC_WSLOT_LARVE,
			SGTC_WSLOT_MEDIC, -1
		}
	},

	// Goa'uld 3: Jaffa. Defender: reetou disruptor at mid range, zat'arc at
	// long range, shield wall and the gacgoa flash.
	{
		SGTC_TEAM_GOAULD, SGTC_CLASS_JAFFA, "Jaffa", SGTC_BOT_ROLE_DEFENDER,
		SGTC_BOT_AB_SHIELD | SGTC_BOT_AB_TAC | SGTC_BOT_AB_HEAL |
			SGTC_BOT_AB_SNIPER | SGTC_BOT_AB_TALENT,
		SGTC_BOT_TALENT_FLASH, 250, 1500,
		"weapon_zat", "weapon_reetou", "weapon_zatarc",
		{
			SGTC_WSLOT_TACGUN, SGTC_WSLOT_ZAT, SGTC_WSLOT_SHIELD,
			SGTC_WSLOT_REETOU, SGTC_WSLOT_MEDIC, SGTC_WSLOT_GOAREGEN, -1
		}
	},

	// Goa'uld 4: Serpent Guard. Defender: glider staff and lance, shield,
	// trapboxes, melee. Closest-range Goa'uld line.
	{
		SGTC_TEAM_GOAULD, SGTC_CLASS_SERPENT, "Serpent Guard", SGTC_BOT_ROLE_DEFENDER,
		SGTC_BOT_AB_SHIELD | SGTC_BOT_AB_TAC | SGTC_BOT_AB_HEAL |
			SGTC_BOT_AB_MELEE | SGTC_BOT_AB_GRENADE | SGTC_BOT_AB_TALENT,
		SGTC_BOT_TALENT_TRAPBOX, 0, 1000,
		"weapon_batsup", "weapon_lance", "weapon_glstaff",
		{
			SGTC_WSLOT_TACGUN, SGTC_WSLOT_ZAT, SGTC_WSLOT_LANCE,
			SGTC_WSLOT_GLSTAFF, SGTC_WSLOT_GACGOA, SGTC_WSLOT_SHIELD,
			SGTC_WSLOT_BATSUP, SGTC_WSLOT_LARVE, SGTC_WSLOT_MEDIC,
			SGTC_WSLOT_GOAREGEN, -1
		}
	},

	// Goa'uld 5: Horus. Scout: reetou and baton, cloak to move first, the
	// emitter unload talent to strip an enemy-held field.
	{
		SGTC_TEAM_GOAULD, SGTC_CLASS_HORUS, "Horus", SGTC_BOT_ROLE_SCOUT,
		SGTC_BOT_AB_CLOAK | SGTC_BOT_AB_TAC | SGTC_BOT_AB_HEAL |
			SGTC_BOT_AB_MELEE | SGTC_BOT_AB_TALENT,
		SGTC_BOT_TALENT_EMITTER_UNLOAD, 100, 1000,
		"weapon_batsup", "weapon_reetou", "weapon_reetou",
		{
			SGTC_WSLOT_TACGUN, SGTC_WSLOT_REETOU, SGTC_WSLOT_BATSUP,
			SGTC_WSLOT_INVIS, SGTC_WSLOT_MEDIC, SGTC_WSLOT_GOAREGEN, -1
		}
	}
};

static const int s_sgtc_bot_profile_count =
	(int)( sizeof(s_sgtc_bot_profiles) / sizeof(s_sgtc_bot_profiles[0]) );

// ---------------------------------------------------------------------------
// Profile accessors.
// ---------------------------------------------------------------------------
const sgtc_bot_class_profile_t *SGTC_Bots_ClassProfile( int team, int cls )
{
	int i;

	for( i = 0; i < s_sgtc_bot_profile_count; i++ )
	{
		if( s_sgtc_bot_profiles[i].team == team &&
			s_sgtc_bot_profiles[i].cls == cls )
			return &s_sgtc_bot_profiles[i];
	}
	return NULL;
}

const char *SGTC_Bots_RoleName( int role )
{
	switch( role )
	{
	case SGTC_BOT_ROLE_ASSAULT:	return "assault";
	case SGTC_BOT_ROLE_SUPPORT:	return "support";
	case SGTC_BOT_ROLE_SNIPER:	return "sniper";
	case SGTC_BOT_ROLE_AMBUSHER:	return "ambusher";
	case SGTC_BOT_ROLE_DEFENDER:	return "defender";
	case SGTC_BOT_ROLE_SCOUT:	return "scout";
	case SGTC_BOT_ROLE_NONE:
	default:			return "none";
	}
}

const char *SGTC_Bots_AbilityName( int ability )
{
	switch( ability )
	{
	case SGTC_BOT_AB_SHIELD:	return "shield";
	case SGTC_BOT_AB_CLOAK:		return "cloak";
	case SGTC_BOT_AB_TAC:		return "tac";
	case SGTC_BOT_AB_SNIPER:	return "sniper";
	case SGTC_BOT_AB_HEAL:		return "heal";
	case SGTC_BOT_AB_MELEE:		return "melee";
	case SGTC_BOT_AB_GRENADE:	return "grenade";
	case SGTC_BOT_AB_TALENT:	return "talent";
	default:			return "none";
	}
}

int SGTC_Bots_ClassKitCount( const sgtc_bot_class_profile_t *p )
{
	int i;

	if( !p )
		return 0;
	for( i = 0; i < SGTC_BOT_PROFILE_MAX_KIT; i++ )
	{
		if( p->kit[i] < 0 )
			break;
	}
	return i;
}

int SGTC_Bots_SelectKitLoadout( const sgtc_bot_class_profile_t *p,
	const int *allowed, const int *cost, int budget, int *out_gond )
{
	int i;
	int count;
	int spent = 0;
	int primaries = 0;
	int specials = 0;
	int selected = 0;

	if( !out_gond )
		return 0;

	for( i = 0; i < SGTC_NUM_WEAPON_SLOTS; i++ )
		out_gond[i] = 0;

	if( !p || !allowed || !cost || budget <= 0 )
		return 0;

	count = SGTC_Bots_ClassKitCount( p );
	for( i = 0; i < count; i++ )
	{
		const int slot = p->kit[i];
		int primary;
		int special;

		if( slot < 0 || slot >= SGTC_NUM_WEAPON_SLOTS )
			continue;
		if( out_gond[slot] != 0 )
			continue;
		if( allowed[slot] != 1 )
			continue;
		if( cost[slot] <= 0 || spent + cost[slot] > budget )
			continue;

		primary = SGTC_WeaponSlotIsPrimary( slot );
		special = SGTC_WeaponSlotIsSpecial( slot );
		if( ( primary && primaries >= SGTC_WSLOT_MAX_PRIMARY ) ||
			( special && specials >= SGTC_WSLOT_MAX_SPECIAL ) )
			continue;

		out_gond[slot] = 1;
		spent += cost[slot];
		primaries += primary;
		specials += special;
		selected++;
	}
	return selected;
}

int SGTC_Bots_SelectPackLoadout( const sgtc_bot_class_profile_t *p, int *out_gond )
{
	int slots[SGTC_PACK_MAX_SLOTS];
	int i;
	int n;

	if( !out_gond )
		return 0;

	for( i = 0; i < SGTC_NUM_WEAPON_SLOTS; i++ )
		out_gond[i] = 0;

	if( !p )
		return 0;

	n = SGTC_PackGetSlots( p->team, p->cls - SGTC_CLASS_FIRST, 1, slots );
	for( i = 0; i < n; i++ )
	{
		if( slots[i] >= 0 && slots[i] < SGTC_NUM_WEAPON_SLOTS )
			out_gond[slots[i]] = 1;
	}
	return n;
}

// ---------------------------------------------------------------------------
// Shield wall. The class must own weapon_shield (SGTC_BOT_AB_SHIELD).
// Raise when charged and threatened, drop when drained or idle.
// ---------------------------------------------------------------------------
int SGTC_Bots_WantsShield( const sgtc_bot_class_profile_t *p,
	const sgtc_bot_shield_ctx_t *c )
{
	float threshold;

	if( !p || !c )
		return 0;
	if( !( p->abilities & SGTC_BOT_AB_SHIELD ) )
		return 0;

	if( c->shield_up )
	{
		// Drained, or idle with nothing in sight: drop the wall.
		if( c->charge <= 1.0f )
			return -1;
		if( c->out_of_combat > SGTC_SHIELD_DROP_OOC && !c->enemy_visible )
			return -1;
		return 0;
	}

	threshold = ( c->skill < SGTC_SKILL_LOW )
		? SGTC_SHIELD_CHARGE_LOW_SKILL
		: SGTC_SHIELD_CHARGE_HIGH_SKILL;
	if( c->charge < threshold )
		return 0;

	// Never raise the wall after a long idle period unless something is
	// actually shooting at the bot.
	if( c->out_of_combat > SGTC_SHIELD_MAX_RAISE_OOC && !c->under_fire )
		return 0;

	if( !c->under_fire &&
		!( c->enemy_visible && c->enemy_distance >= 0.0f &&
			c->enemy_distance <= SGTC_SHIELD_THREAT_RANGE ) )
		return 0;

	// Sloppy tier: the bot may simply be too slow to react this tick.
	if( c->skill < SGTC_SKILL_LOW &&
		!sgtc_gate( c->rng, SGTC_GATE_SHIELD_BASE + SGTC_GATE_PER_SKILL * c->skill ) )
		return 0;

	return 1;
}

// ---------------------------------------------------------------------------
// Cloak. The class must own weapon_invis (SGTC_BOT_AB_CLOAK).
// ---------------------------------------------------------------------------
int SGTC_Bots_WantsCloak( const sgtc_bot_class_profile_t *p,
	const sgtc_bot_cloak_ctx_t *c )
{
	float need;

	if( !p || !c )
		return 0;
	if( !( p->abilities & SGTC_BOT_AB_CLOAK ) )
		return 0;

	if( c->cloaked )
	{
		// A threat inside 700 units can see the shimmer; a drained cell
		// drops the cloak anyway.  Bounded hold and a no-enemy timeout keep
		// bots from wandering the map permanently invisible.
		if( ( c->enemy_visible && c->enemy_distance >= 0.0f &&
				c->enemy_distance <= SGTC_CLOAK_BREAK_RANGE ) ||
			c->charge <= 3.0f ||
			( c->cloaked_time >= 0.0f && c->cloaked_time >= SGTC_CLOAK_MAX_HOLD ) ||
			( !c->enemy_visible && c->cloaked_time >= 0.0f &&
				c->cloaked_time >= SGTC_CLOAK_NO_ENEMY_HOLD ) )
			return -1;
		return 0;
	}

	// Sharp bots may cloak in front of a distant enemy to reposition.
	if( c->skill >= SGTC_SKILL_SHARP && c->enemy_visible &&
		c->enemy_distance > SGTC_CLOAK_REPOSITION_RANGE )
	{
		if( c->charge < SGTC_CLOAK_MIN_CHARGE )
			return 0;
		return 1;
	}

	if( c->enemy_visible || c->out_of_combat < SGTC_CLOAK_MIN_OOC )
		return 0;

	// Idle cloak is an ambush tool only: sharp bots, rarely, never the
	// default state for a whole team.
	if( c->skill < SGTC_SKILL_SHARP )
		return 0;

	need = SGTC_CLOAK_MIN_CHARGE;
	if( c->charge < need )
		return 0;

	if( !sgtc_gate( c->rng, SGTC_GATE_CLOAK ) )
		return 0;

	return 1;
}

// ---------------------------------------------------------------------------
// TAC placement. The class must own weapon_tacgun (SGTC_BOT_AB_TAC).
// One TAC on the objective/chokepoint, never in front of a close enemy.
// ---------------------------------------------------------------------------
int SGTC_Bots_WantsTacPlace( const sgtc_bot_class_profile_t *p,
	const sgtc_bot_tac_ctx_t *c )
{
	int spot_ok;

	if( !p || !c )
		return 0;
	if( !( p->abilities & SGTC_BOT_AB_TAC ) )
		return 0;

	if( c->tac_ammo <= 0 || c->tac_near >= SGTC_TAC_MAX_NEAR )
		return 0;

	// The engine refuses the placement when a player is in the way; do not
	// waste ammo while a visible enemy is close enough to contest it.
	if( c->enemy_visible && c->enemy_distance >= 0.0f &&
		c->enemy_distance <= SGTC_TAC_ENEMY_CLEAR_RANGE )
		return 0;

	spot_ok = c->at_chokepoint ||
		( c->objective_distance >= 0.0f &&
			c->objective_distance <= SGTC_TAC_OBJECTIVE_RANGE );

	// Sharp bots redeploy after moving up, as long as the objective is close.
	if( !spot_ok && c->skill >= SGTC_SKILL_SHARP &&
		c->front_moved > SGTC_TAC_REPOSITION_MOVE &&
		c->objective_distance >= 0.0f &&
		c->objective_distance <= SGTC_TAC_REPOSITION_RANGE )
		spot_ok = 1;

	if( !spot_ok )
		return 0;

	if( c->skill < SGTC_SKILL_LOW && !sgtc_gate( c->rng, SGTC_GATE_TAC ) )
		return 0;

	return 1;
}

// ---------------------------------------------------------------------------
// Healing. The class must carry a heal item (SGTC_BOT_AB_HEAL).
// ---------------------------------------------------------------------------
int SGTC_Bots_WantsHeal( const sgtc_bot_class_profile_t *p,
	const sgtc_bot_heal_ctx_t *c )
{
	if( !p || !c )
		return 0;
	if( !( p->abilities & SGTC_BOT_AB_HEAL ) )
		return 0;

	if( c->enemy_visible )
		return 0;
	if( c->ally_distance < 0.0f || c->ally_distance > SGTC_HEAL_MAX_RANGE )
		return 0;
	if( c->ally_health >= SGTC_HEAL_WOUNDED )
		return 0;
	if( !c->ally_in_front )
		return 0;
	if( c->ammo <= 0 )
		return 0;

	if( c->skill < SGTC_SKILL_HEAL_LOW && !sgtc_gate( c->rng, SGTC_GATE_HEAL ) )
		return 0;

	return 1;
}

// ---------------------------------------------------------------------------
// Talents. The class must have SGTC_BOT_AB_TALENT. SNEAK and ARMOR_TOGGLE
// are intentionally never used by bots: one changes the view height, the
// other only paints the HUD.
// ---------------------------------------------------------------------------
static int sgtc_talent_condition( int talent, const sgtc_bot_talent_ctx_t *c )
{
	switch( talent )
	{
	case SGTC_BOT_TALENT_GRENADE:
		return c->enemy_visible && c->enemy_distance >= 250.0f &&
			c->enemy_distance <= 900.0f;

	case SGTC_BOT_TALENT_ZATBOOM:
		return c->enemy_visible && c->enemy_distance >= 150.0f &&
			c->enemy_distance <= 800.0f;

	case SGTC_BOT_TALENT_SHIELD_BUFF:
		return c->enemy_visible && c->enemy_distance >= 0.0f &&
			c->enemy_distance <= 900.0f;

	case SGTC_BOT_TALENT_LARVE:
		return c->enemy_visible && c->enemy_distance >= 0.0f &&
			c->enemy_distance <= 900.0f;

	case SGTC_BOT_TALENT_TRAPBOX:
		return !c->enemy_visible || c->enemy_distance > 600.0f;

	case SGTC_BOT_TALENT_EMITTER:
		return !c->enemy_visible && c->allies_near >= 1;

	case SGTC_BOT_TALENT_EMITTER_UNLOAD:
		return c->allies_near == 0;

	case SGTC_BOT_TALENT_FLASH:
		return c->enemy_visible && c->enemy_distance >= 0.0f &&
			c->enemy_distance <= 600.0f;

	case SGTC_BOT_TALENT_SNEAK:
	case SGTC_BOT_TALENT_ARMOR_TOGGLE:
	case SGTC_BOT_TALENT_NONE:
	default:
		return 0;
	}
}

int SGTC_Bots_WantsTalent( const sgtc_bot_class_profile_t *p,
	const sgtc_bot_talent_ctx_t *c )
{
	int talent;

	if( !p || !c )
		return 0;
	if( !( p->abilities & SGTC_BOT_AB_TALENT ) )
		return 0;

	// The context copies the talent for convenience, but callers that leave
	// it zero still get the class default.
	talent = c->talent;
	if( talent == SGTC_BOT_TALENT_NONE )
		talent = p->talent;
	if( talent == SGTC_BOT_TALENT_NONE )
		return 0;

	if( !c->cooldown_ready || !c->has_item )
		return 0;
	if( !sgtc_talent_condition( talent, c ) )
		return 0;

	if( c->skill < SGTC_SKILL_TALENT_LOW && !sgtc_gate( c->rng, SGTC_GATE_TALENT ) )
		return 0;

	return 1;
}
