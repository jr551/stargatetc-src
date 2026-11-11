// sgtc_bots_classplay.cpp - engine side of the per-class bot playstyles.
//
// The pure decision functions live in sgtc_bots_classes.cpp and only see a
// context struct. This file is the half that has an engine: it resolves the
// bot's live team/class, gathers enemy/objective/ally state, and turns the
// decisions into client commands and IN_ATTACK bits.
//
// Two rules keep the bots compatible with the stock jk_botti weapon logic:
//
//   * Ability weapons are selected with FakeClientCommand("weapon_x") and
//     fired on the FOLLOWING think. Deploy/Holster run between the two, so an
//     ability is never fired from the weapon the bot was holding a tick ago.
//   * Buttons are ORed into pEdict->v.button and existing bits are never
//     cleared; combat keeps the frame if it already pressed fire.
//
// Every function here is deterministic for a given sgtc_bot_seed: the per-bot
// stream is seeded from SGTC_Bots_SeedBase() and advanced once per think.

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "game.h"
#include "gamerules.h"

#include "sgtc_bots.h"
#include "sgtc_bots_rules_ai.h"
#include "sgtc_bots_classes.h"
#include "sgtc_bots_loadout.h"
#include "sgtc_enums.h"
#include "sgtc_rules.h"
#include "sgtc_packs.h"
#include "sgtc_rea_debug.h"
#include "sgtc_weapons.h"
#include "sgtc_monsters.h"
#include "sgtc_util.h"
#include "../bots/jk_botti/bot.h"
#include "../bots/jk_botti/bot_func.h"
#include "../bots/jk_botti/util.h"

#include <string.h>

// mp_tacdistance, defined next to the rules cvars; same object Tacgun reads.
extern cvar_t sgtc_tacdist;

// Buy-menu costs and the menu mode, both owned by sgtc_round.cpp. The spawn
// loadout builder reads them exactly like SGTC_SanitizeGondLoadout() does.
extern cvar_t *const sgtc_wpnC[SGTC_NUM_WEAPON_SLOTS];
extern cvar_t sgtc_cvar_wepselmode;

// The live jk_botti slots (defined in bot.cpp).
extern bot_t bots[32];

// ---------------------------------------------------------------------------
// per-bot state
// ---------------------------------------------------------------------------

enum
{
	SGTC_CP_AB_SHIELD = 0,
	SGTC_CP_AB_CLOAK = 1,
	SGTC_CP_AB_TAC = 2,
	SGTC_CP_AB_TALENT = 3
};

enum
{
	SGTC_CP_PEND_NONE = 0,
	SGTC_CP_PEND_SHIELD,
	SGTC_CP_PEND_CLOAK,
	SGTC_CP_PEND_TAC,
	SGTC_CP_PEND_HEAL,
	SGTC_CP_PEND_TALENT
};

typedef struct
{
	// Director override (Bot_SetRole). 0 = use the class default.
	int role_override;

	// Next allowed time for shield/cloak/tac/talent, indexed by
	// SGTC_CP_AB_*. This gates STARTING an action, not firing a pending one.
	float next_ability[4];
	float next_heal;
	float next_counter;
	float next_retrieve;

	// Weapon selections (and the matching press) are suppressed for a short
	// time so normal jk_botti weapon logic gets the frame back.
	float suppress_until;

	// One pending two-phase action: selected this think, pressed after the
	// weapon's draw animation (0.55 s) on a later think.
	int pending;
	int pending_target;		// player index for a pending heal
	char pending_weapon[24];	// classname of the pending ability weapon
	int pending_cloak;		// desired cloak state for a pending toggle
	float pending_time;

	// Last observed shield charge (0..9) and TAC placement time.
	int last_charge;
	float last_tac_place;
	float cloak_since;		// world time when the current cloak started
	float cloak_retry_after;	// backoff after a refused uncloak toggle
	char combat_weapon[24];		// last non-ability weapon, redrawn after ability use

	qboolean alive_seen;
	edict_t *edict;			// slot identity; a replaced bot resets the slot
	unsigned int rng;
} sgtc_bot_classstate_t;

static sgtc_bot_classstate_t s_class_state[32];

static unsigned int s_class_rng_base;
static qboolean s_class_rng_ready;

static unsigned int SGTC_Bots_ClassXorShift( unsigned int *state )
{
	unsigned int x = *state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;

	// The caller stream must never become zero (xorshift has 0 as a fixed
	// point); the constant is the golden-ratio seed used elsewhere in the tree.
	if( x == 0 )
		x = 0x9e3779b9u;

	*state = x;
	return x;
}

static void SGTC_Bots_ClassStateReset( sgtc_bot_classstate_t *st )
{
	for( int i = 0; i < 4; i++ )
		st->next_ability[i] = 0.0f;

	st->next_heal = 0.0f;
	st->next_counter = 0.0f;
	st->next_retrieve = 0.0f;
	st->suppress_until = 0.0f;
	st->pending = SGTC_CP_PEND_NONE;
	st->pending_target = 0;
	st->pending_weapon[0] = '\0';
	st->pending_cloak = 0;
	st->pending_time = 0.0f;
	st->last_charge = 0;
	st->last_tac_place = -1.0f;
	st->cloak_since = 0.0f;
	st->cloak_retry_after = 0.0f;
	st->combat_weapon[0] = '\0';
}

// ---------------------------------------------------------------------------
// objectives (same classnames as SGTC_Bots_CollectObjectives)
// ---------------------------------------------------------------------------

#define SGTC_CP_MAX_OBJECTIVES 32

static Vector s_class_objectives[SGTC_CP_MAX_OBJECTIVES];
static int s_class_objective_count;
static float s_class_next_objective_scan;

static void SGTC_Bots_ClassScanObjectives( void )
{
	static const char *const objective_classes[] = { "trigger_goal", "zone_capture", "game_nuke" };

	if( gpGlobals->time < s_class_next_objective_scan )
		return;

	s_class_next_objective_scan = gpGlobals->time + 0.5f;
	s_class_objective_count = 0;

	for( int c = 0; c < (int)( sizeof( objective_classes ) / sizeof( objective_classes[0] ) ); c++ )
	{
		edict_t *pent = NULL;

		while( !FNullEnt( pent = FIND_ENTITY_BY_CLASSNAME( pent, objective_classes[c] ) ) )
		{
			if( s_class_objective_count >= SGTC_CP_MAX_OBJECTIVES )
				return;

			s_class_objectives[s_class_objective_count++] = VecBModelOrigin( &pent->v );
		}
	}
}

static float SGTC_Bots_ClassObjectiveDistance( const Vector &origin )
{
	float best = -1.0f;

	SGTC_Bots_ClassScanObjectives();

	for( int i = 0; i < s_class_objective_count; i++ )
	{
		const float distance = ( s_class_objectives[i] - origin ).Length();

		if( best < 0.0f || distance < best )
			best = distance;
	}

	return best;
}

static int SGTC_Bots_ClassCountTacs( CBasePlayer *pl )
{
	int limit = (int)sgtc_tacdist.value;
	int count = 0;
	edict_t *pent = NULL;

	if( limit < 50 )
		limit = 50;
	if( limit > 8000 )
		limit = 8000;

	// The weapon's own density check counts every TAC within mp_tacdistance,
	// whatever its owner (SGTC_TacDeployCounts). Match it, or the bot will
	// spend a press on a placement the weapon refuses.
	while( !FNullEnt( pent = FIND_ENTITY_BY_CLASSNAME( pent, "monster_tac" ) ) )
	{
		if( ( pent->v.origin - pl->pev->origin ).Length() >= (float)limit )
			continue;

		count++;
	}

	return count;
}

// ---------------------------------------------------------------------------
// tiny engine helpers
// ---------------------------------------------------------------------------

static int SGTC_Bots_ClassSkill( bot_t &pBot )
{
	if( pBot.bot_skill < 0 )
		return 0;
	if( pBot.bot_skill > 5 )
		return 5;
	return pBot.bot_skill;
}

static int SGTC_Bots_ClassResolve( CBasePlayer *pl, int *team, int *cls )
{
	int resolved_team;
	int resolved_class;

	if( !pl || !pl->pev )
		return 0;

	resolved_team = SGTC_EntTeamToTeam( (int)pl->pev->team );

	// Before the first spawn the entity team is still unset; the picked team
	// is the only value available. After spawn both agree.
	if( resolved_team == SGTC_TEAM_NONE &&
		pl->m_iSgtcTeam >= SGTC_TEAM_TORRI && pl->m_iSgtcTeam <= SGTC_TEAM_GOAULD )
	{
		resolved_team = pl->m_iSgtcTeam;
	}

	resolved_class = pl->m_iSgtcPortraitShown;
	if( resolved_class == SGTC_CLASS_NONE )
		resolved_class = pl->m_iSgtcPortraitSel;

	if( resolved_team == SGTC_TEAM_NONE )
		return 0;
	if( resolved_class < SGTC_CLASS_FIRST || resolved_class > SGTC_CLASS_LAST )
		return 0;

	*team = resolved_team;
	*cls = resolved_class;
	return 1;
}

static int SGTC_Bots_ClassEffectiveRole( const sgtc_bot_class_profile_t *p, int index )
{
	if( index >= 0 && index < 32 )
	{
		const int role = s_class_state[index].role_override;

		if( role > SGTC_BOT_ROLE_NONE && role < SGTC_BOT_ROLE_COUNT )
			return role;
	}

	return p ? p->role : SGTC_BOT_ROLE_NONE;
}

static const char *SGTC_Bots_ClassActiveWeapon( CBasePlayer *pl )
{
	if( !pl->m_pActiveItem )
		return NULL;

	return STRING( pl->m_pActiveItem->pev->classname );
}

static qboolean SGTC_Bots_ClassActiveIs( CBasePlayer *pl, const char *classname )
{
	return pl->m_pActiveItem && FClassnameIs( pl->m_pActiveItem->pev, classname );
}

static qboolean SGTC_Bots_ClassHasItem( CBasePlayer *pl, const char *classname )
{
	return pl->HasNamedPlayerItem( classname ) ? TRUE : FALSE;
}

static int SGTC_Bots_ClassAmmo( CBasePlayer *pl, const char *name )
{
	const int index = CBasePlayer::GetAmmoIndex( name );

	if( index < 0 || index >= MAX_AMMO_SLOTS )
		return 0;

	return pl->m_rgAmmo[index];
}

static int SGTC_Bots_ClassTacAmmo( CBasePlayer *pl )
{
	int ammo = SGTC_Bots_ClassAmmo( pl, SGTC_AMMO_TAC );

	if( ammo < 0 )
		ammo = 0;
	if( ammo > SGTC_TAC_MAX_CARRY )
		ammo = SGTC_TAC_MAX_CARRY;

	return ammo;
}

static Vector SGTC_Bots_ClassAllyChest( CBasePlayer *ally )
{
	Vector chest = ally->pev->origin;

	// view_ofs is the eye offset; 70% of it is roughly the chest and follows
	// crouching automatically.
	chest.z += ally->pev->view_ofs.z * 0.7f;
	return chest;
}

static void SGTC_Bots_ClassAimAt( edict_t *pEdict, const Vector &target )
{
	Vector eye = pEdict->v.origin + pEdict->v.view_ofs;
	Vector direction = target - eye;
	Vector angles;

	if( direction.Length() < 1.0f )
		return;

	angles = UTIL_VecToAngles( direction );
	angles.z = 0.0f;
	pEdict->v.v_angle = angles;

	// Keep the jk_botti aim filter pointing the same way for this frame; it
	// only weaves toward idealpitch/ideal_yaw, so without this it would drag
	// the shot back toward the stale enemy aim.
	pEdict->v.idealpitch = angles.x;
	pEdict->v.ideal_yaw = angles.y;
}

static void SGTC_Bots_ClassAimGround( edict_t *pEdict )
{
	Vector angles = pEdict->v.v_angle;

	// Tacgun deploys 128 units along the view; look at the floor just in
	// front so the trace has a surface to place on.
	angles.x = 40.0f;
	angles.z = 0.0f;
	pEdict->v.v_angle = angles;
	pEdict->v.idealpitch = angles.x;
	pEdict->v.ideal_yaw = angles.y;
}

static void SGTC_Bots_ClassEmit( bot_t &pBot, const char *ability, int cls, const char *detail )
{
	const char *name = pBot.name;

	if( !name[0] && !FNullEnt( pBot.pEdict ) )
		name = STRING( pBot.pEdict->v.netname );

	SGTC_Bots_EmitAbility( name, ability, cls, detail );
}

static qboolean SGTC_Bots_ClassFindWoundedAlly( CBasePlayer *pl, int team,
	CBasePlayer **out_ally, float *out_health, float *out_distance,
	qboolean *out_human )
{
	// John's rule: a wounded *real* player on the bot's team always gets the
	// heal before bots/objectives/patrol.  Humans therefore win the ally scan
	// over bots even at greater distance (still bounded by the 450u range).
	CBasePlayer *best_human = NULL;
	float best_human_distance = 0.0f;
	edict_t *bot_edict = pl->edict();
	Vector eye = pl->pev->origin + pl->pev->view_ofs;
	Vector forward;
	float best_distance = 0.0f;
	CBasePlayer *best = NULL;

	UTIL_MakeVectors( pl->pev->v_angle );
	forward = gpGlobals->v_forward;

	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *ally = (CBasePlayer *)UTIL_PlayerByIndex( i );

		if( !ally || ally == pl || !ally->IsAlive() )
			continue;
		if( SGTC_EntTeamToTeam( (int)ally->pev->team ) != team )
			continue;
		if( ally->pev->health >= 100.0f )
			continue;

		const float distance = ( ally->pev->origin - pl->pev->origin ).Length();
		Vector direction;

		if( distance > 450.0f )
			continue;

		direction = SGTC_Bots_ClassAllyChest( ally ) - eye;
		if( direction.Length() < 1.0f )
			continue;

		// WantsHeal only accepts an ally inside the aim cone; report the
		// nearest wounded, visible one and let the pure side decide.
		if( DotProduct( forward, direction.Normalize() ) < 0.5f )
			continue;

		TraceResult tr;
		UTIL_TraceLine( eye, SGTC_Bots_ClassAllyChest( ally ), dont_ignore_monsters, bot_edict, &tr );
		if( tr.flFraction < 1.0f && tr.pHit != ally->edict() )
			continue;

		const qboolean human = !FBitSet( ally->pev->flags, FL_FAKECLIENT ) &&
			!FBitSet( ally->pev->flags, FL_THIRDPARTYBOT ) ? TRUE : FALSE;

		if( human )
		{
			if( !best_human || distance < best_human_distance )
			{
				best_human = ally;
				best_human_distance = distance;
			}
		}
		else if( !best_human && ( !best || distance < best_distance ) )
		{
			best = ally;
			best_distance = distance;
		}
	}

	if( best_human )
	{
		best = best_human;
		best_distance = best_human_distance;
	}

	if( !best )
		return FALSE;

	if( out_human )
		*out_human = ( best == best_human ) ? TRUE : FALSE;
	*out_ally = best;
	*out_health = best->pev->health / 100.0f;
	if( *out_health < 0.0f )
		*out_health = 0.0f;
	if( *out_health > 1.0f )
		*out_health = 1.0f;
	*out_distance = best_distance;
	return TRUE;
}

static const char *SGTC_Bots_ClassHealWeapon( CBasePlayer *pl, int team )
{
	// Goa'uld classes carry the regeneration device; Torri use the medic pen.
	// Carter is Torri but has both, so the team check picks the stock Torri
	// behaviour.
	if( team == SGTC_TEAM_GOAULD && SGTC_Bots_ClassHasItem( pl, "weapon_goaregen" ) )
		return "weapon_goaregen";
	if( SGTC_Bots_ClassHasItem( pl, "weapon_medic" ) )
		return "weapon_medic";
	if( SGTC_Bots_ClassHasItem( pl, "weapon_goaregen" ) )
		return "weapon_goaregen";

	return NULL;
}

static int SGTC_Bots_ClassCountAllies( CBasePlayer *pl, int team, float radius )
{
	int count = 0;

	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *ally = (CBasePlayer *)UTIL_PlayerByIndex( i );

		if( !ally || ally == pl || !ally->IsAlive() )
			continue;
		if( SGTC_EntTeamToTeam( (int)ally->pev->team ) != team )
			continue;
		if( ( ally->pev->origin - pl->pev->origin ).Length() > radius )
			continue;

		count++;
	}

	return count;
}

// Preconditions CBasePlayer::Talent() checks before it creates anything:
// the commander needs a surface within 150 units while grounded and standing,
// the serpent needs a solid (non-monster) hit for the trapbox.
static qboolean SGTC_Bots_ClassTalentHasSpot( CBasePlayer *pl, int talent )
{
	Vector eye = pl->pev->origin + pl->pev->view_ofs;
	TraceResult tr;

	UTIL_MakeVectors( pl->pev->v_angle );
	UTIL_TraceLine( eye, eye + gpGlobals->v_forward * 360.0f,
		dont_ignore_monsters, pl->edict(), &tr );

	if( talent == SGTC_BOT_TALENT_EMITTER )
	{
		return ( SGTC_Distance3D( pl->pev->origin, tr.vecEndPos ) < 150.0f &&
			!( pl->pev->button & ( IN_JUMP | IN_DUCK ) ) ) ? TRUE : FALSE;
	}

	if( talent == SGTC_BOT_TALENT_TRAPBOX )
	{
		CBaseEntity *pHit = ( tr.flFraction < 1.0f )
			? CBaseEntity::Instance( tr.pHit ) : NULL;

		return ( pHit && !( pHit->pev->flags & FL_MONSTER ) ) ? TRUE : FALSE;
	}

	return TRUE;
}

static qboolean SGTC_Bots_ClassHasTalentItem( CBasePlayer *pl, int talent )
{
	switch( talent )
	{
	case SGTC_BOT_TALENT_GRENADE:
		// Talent() wants the grenade weapon bit, reserve ammo, and refuses
		// while the grenade itself is the active weapon.
		if( pl->m_pActiveItem && pl->m_pActiveItem->m_iId == SGTC_WPN_HANDGRENADE )
			return FALSE;
		return ( pl->pev->weapons & ( 1 << SGTC_WPN_HANDGRENADE ) ) &&
			SGTC_Bots_ClassAmmo( pl, SGTC_AMMO_HAND ) > 0 ? TRUE : FALSE;

	case SGTC_BOT_TALENT_ZATBOOM:
		// The zat must be active when the command runs; the action selects
		// it first, so possession is the requirement here.
		return ( pl->pev->weapons & ( 1 << SGTC_WPN_ZAT ) ) ? TRUE : FALSE;

	case SGTC_BOT_TALENT_SHIELD_BUFF:
		return pl->m_iSgtcTalentDeploy == 0 ? TRUE : FALSE;

	case SGTC_BOT_TALENT_LARVE:
		return pl->m_iSgtcTalentDeploy == 0 ? TRUE : FALSE;

	case SGTC_BOT_TALENT_TRAPBOX:
		return ( pl->m_iSgtcTalentDeploy < 2 &&
			SGTC_Bots_ClassTalentHasSpot( pl, talent ) ) ? TRUE : FALSE;

	case SGTC_BOT_TALENT_EMITTER:
		return SGTC_Bots_ClassTalentHasSpot( pl, talent );

	case SGTC_BOT_TALENT_FLASH:
		// Jaffa talent requires the gacgoa (id 10) in hand when it runs.
		return ( pl->pev->weapons & ( 1 << SGTC_WPN_GACGOA ) ) ? TRUE : FALSE;

	case SGTC_BOT_TALENT_EMITTER_UNLOAD:
	{
		// Talent() only retags an emitter the player already owns inside
		// 149 units; do not spam the command when there is nothing to unload.
		edict_t *pent = NULL;

		while( !FNullEnt( pent = FIND_ENTITY_BY_CLASSNAME( pent, "monster_shieldemit" ) ) )
		{
			if( FNullEnt( pent->v.owner ) || pent->v.owner != pl->edict() )
				continue;
			if( ( pent->v.origin - pl->pev->origin ).Length() <= 149.0f &&
				fabs( pent->v.origin.z - pl->pev->origin.z ) < 70.0f )
				return TRUE;
		}
		return FALSE;
	}

	case SGTC_BOT_TALENT_SNEAK:
	case SGTC_BOT_TALENT_ARMOR_TOGGLE:
	case SGTC_BOT_TALENT_NONE:
	default:
		return FALSE;
	}
}

// Nearest enemy monster_tac the bot can see and shoot, or NULL.
static edict_t *SGTC_Bots_ClassFindEnemyTac( CBasePlayer *pl )
{
	Vector eye = pl->pev->origin + pl->pev->view_ofs;
	edict_t *best = NULL;
	float best_distance = 701.0f;
	edict_t *pent = NULL;

	while( !FNullEnt( pent = FIND_ENTITY_BY_CLASSNAME( pent, "monster_tac" ) ) )
	{
		Vector target = pent->v.origin + Vector( 0.0f, 0.0f, 8.0f );
		float distance;

		if( pent->v.team == 0 || pent->v.team == pl->pev->team )
			continue;
		// The team field is copied from the owner on the TAC's arming think;
		// before that (or for a wild TAC) trust the recorded deployer.
		{
			CSGTCTac *tac = (CSGTCTac *)CBaseEntity::Instance( pent );
			CBasePlayer *deployer = tac ? tac->Deployer() : NULL;

			if( deployer && deployer->pev->team == pl->pev->team )
				continue;
		}

		distance = ( target - eye ).Length();
		if( distance > 700.0f || distance >= best_distance )
			continue;

		TraceResult tr;
		UTIL_TraceLine( eye, target, dont_ignore_monsters, pl->edict(), &tr );
		if( tr.flFraction < 1.0f && tr.pHit != pent )
			continue;

		best = pent;
		best_distance = distance;
	}

	return best;
}

// Nearest TAC this player deployed, within reach of +use.
static edict_t *SGTC_Bots_ClassFindOwnTac( CBasePlayer *pl, float radius )
{
	Vector eye = pl->pev->origin + pl->pev->view_ofs;
	edict_t *best = NULL;
	float best_distance = radius + 1.0f;
	edict_t *pent = NULL;

	while( !FNullEnt( pent = FIND_ENTITY_BY_CLASSNAME( pent, "monster_tac" ) ) )
	{
		Vector target = pent->v.origin + Vector( 0.0f, 0.0f, 8.0f );
		float distance;

		CSGTCTac *tac = (CSGTCTac *)CBaseEntity::Instance( pent );

		// An armed TAC clears pev->owner and keeps the deployer in
		// m_hDeployer, so owner checks must go through Deployer().
		if( !tac || tac->Deployer() != pl )
			continue;

		distance = ( pent->v.origin - pl->pev->origin ).Length();
		if( distance > radius || distance >= best_distance )
			continue;

		TraceResult tr;
		UTIL_TraceLine( eye, target, dont_ignore_monsters, pl->edict(), &tr );
		if( tr.flFraction < 1.0f && tr.pHit != pent )
			continue;

		best = pent;
		best_distance = distance;
	}

	return best;
}

// ---------------------------------------------------------------------------
// two-phase ability press (selected on the previous think)
// ---------------------------------------------------------------------------

static void SGTC_Bots_ClassPressPending( bot_t &pBot, CBasePlayer *pl,
	sgtc_bot_classstate_t &st, int cls, qboolean enemy_visible, qboolean *button_used )
{
	edict_t *pEdict = pBot.pEdict;

	if( st.pending == SGTC_CP_PEND_NONE )
		return;

	if( gpGlobals->time - st.pending_time > 1.5f )
	{
		st.pending = SGTC_CP_PEND_NONE;
		return;
	}

	// DefaultDeploy blocks ItemPreFrame for 0.5 s, so a press on the very next
	// tick would be swallowed. Wait out the draw animation, then fire once.
	if( gpGlobals->time - st.pending_time < 0.55f )
		return;

	// Combat owns this frame's trigger; keep the pending action for later.
	if( FBitSet( pEdict->v.button, IN_ATTACK ) )
		return;

	switch( st.pending )
	{
	case SGTC_CP_PEND_SHIELD:
		if( pl->m_bShieldWall )
		{
			st.pending = SGTC_CP_PEND_NONE;
			break;
		}
		if( !SGTC_Bots_ClassActiveIs( pl, "weapon_shield" ) )
		{
			if( gpGlobals->time - st.pending_time > 0.3f )
				st.pending = SGTC_CP_PEND_NONE;
			break;
		}

		pEdict->v.button |= IN_ATTACK;
		st.pending = SGTC_CP_PEND_NONE;
		st.next_ability[SGTC_CP_AB_SHIELD] = gpGlobals->time + 0.5f;
		{
			char detail[24];
			snprintf( detail, sizeof( detail ), "charge=%d", pl->m_iShieldCharge );
			SGTC_Bots_ClassEmit( pBot, "shield", cls, detail );
		}
		break;

	case SGTC_CP_PEND_CLOAK:
		if( !SGTC_Bots_ClassActiveIs( pl, "weapon_invis" ) )
		{
			if( gpGlobals->time - st.pending_time > 0.3f )
				st.pending = SGTC_CP_PEND_NONE;
			break;
		}
		// Donning the cloak needs charge > 3; dropping it is always allowed
		// (otherwise a bot that drained the cell while cloaked could never
		// toggle back out and would stay invisible forever).
		if( ( st.pending_cloak != 0 && pl->m_iShieldCharge <= 3 ) ||
			( ( pl->m_bShieldSprint != 0 ) ? 1 : 0 ) == st.pending_cloak )
		{
			st.pending = SGTC_CP_PEND_NONE;
			break;
		}

		pEdict->v.button |= IN_ATTACK;
		st.pending = SGTC_CP_PEND_NONE;
		st.next_ability[SGTC_CP_AB_CLOAK] = gpGlobals->time + 0.5f;
		SGTC_Bots_ClassEmit( pBot, "cloak", cls,
			st.pending_cloak ? "cloak on" : "cloak off" );
		break;

	case SGTC_CP_PEND_TAC:
		if( !SGTC_Bots_ClassActiveIs( pl, "weapon_tacgun" ) ||
			SGTC_Bots_ClassTacAmmo( pl ) <= 0 )
		{
			if( gpGlobals->time - st.pending_time > 0.3f )
				st.pending = SGTC_CP_PEND_NONE;
			break;
		}

		SGTC_Bots_ClassAimGround( pEdict );

		pEdict->v.button |= IN_ATTACK;
		st.pending = SGTC_CP_PEND_NONE;
		st.last_tac_place = gpGlobals->time;
		st.next_ability[SGTC_CP_AB_TAC] = gpGlobals->time + 1.0f;
		SGTC_Bots_ClassEmit( pBot, "tac", cls, "place" );
		break;

	case SGTC_CP_PEND_HEAL:
		{
			CBasePlayer *target = NULL;

			if( !st.pending_weapon[0] ||
				!SGTC_Bots_ClassActiveIs( pl, st.pending_weapon ) )
			{
				if( gpGlobals->time - st.pending_time > 0.3f )
					st.pending = SGTC_CP_PEND_NONE;
				break;
			}

			if( enemy_visible )
			{
				// The heal decision is only valid out of combat; a new sighting
				// cancels the pending shot rather than turning the back to it.
				st.pending = SGTC_CP_PEND_NONE;
				break;
			}

			if( st.pending_target >= 1 && st.pending_target <= gpGlobals->maxClients )
				target = (CBasePlayer *)UTIL_PlayerByIndex( st.pending_target );

			if( !target || !target->IsAlive() || target == pl ||
				SGTC_EntTeamToTeam( (int)target->pev->team ) !=
					SGTC_EntTeamToTeam( (int)pl->pev->team ) ||
				target->pev->health >= 100.0f )
			{
				st.pending = SGTC_CP_PEND_NONE;
				break;
			}

			SGTC_Bots_ClassAimAt( pEdict, SGTC_Bots_ClassAllyChest( target ) );
			pEdict->v.button |= IN_ATTACK;
			st.pending = SGTC_CP_PEND_NONE;
			st.pending_weapon[0] = '\0';
			st.next_heal = gpGlobals->time + 0.8f;
			SGTC_Bots_ClassEmit( pBot, "heal", cls, "ally" );
		}
		break;

	case SGTC_CP_PEND_TALENT:
		if( !st.pending_weapon[0] ||
			!SGTC_Bots_ClassActiveIs( pl, st.pending_weapon ) )
		{
			if( gpGlobals->time - st.pending_time > 0.3f )
				st.pending = SGTC_CP_PEND_NONE;
			break;
		}

		FakeClientCommand( pEdict, "talent", NULL, NULL );
		st.pending = SGTC_CP_PEND_NONE;
		st.pending_weapon[0] = '\0';
		st.next_ability[SGTC_CP_AB_TALENT] = gpGlobals->time + 1.0f;
		SGTC_Bots_ClassEmit( pBot, "talent", cls,
			SGTC_Bots_AbilityName( SGTC_BOT_AB_TALENT ) );
		break;

	default:
		st.pending = SGTC_CP_PEND_NONE;
		break;
	}

	*button_used = FBitSet( pEdict->v.button, IN_ATTACK ) ? TRUE : FALSE;
}

// ---------------------------------------------------------------------------
// public entry points
// ---------------------------------------------------------------------------

void SGTC_Bots_ClassResetAll( void )
{
	memset( s_class_state, 0, sizeof( s_class_state ) );
	s_class_objective_count = 0;
	s_class_next_objective_scan = 0.0f;
	s_class_rng_base = 0;
	s_class_rng_ready = FALSE;
}

void SGTC_Bots_ClassPlay( bot_t &pBot )
{
	edict_t *pEdict = pBot.pEdict;

	if( !pBot.is_used || FNullEnt( pEdict ) )
		return;
	if( !SGTC_Bots_Enabled() )
		return;
	if( !FBitSet( pEdict->v.flags, FL_CLIENT ) ||
		!FBitSet( pEdict->v.flags, FL_FAKECLIENT ) )
		return;

	CBasePlayer *pl = (CBasePlayer *)CBaseEntity::Instance( pEdict );
	const int index = UTIL_GetBotIndex( pEdict );

	if( !pl || index < 0 || index >= 32 )
		return;

	sgtc_bot_classstate_t &st = s_class_state[index];

	// A kicked and recreated bot inherits the slot: drop the previous bot's
	// timers and pending action. The director's role override is per slot and
	// survives on purpose.
	if( st.edict != pEdict )
	{
		const int keep_role = st.role_override;

		SGTC_Bots_ClassStateReset( &st );
		st.role_override = keep_role;
		st.edict = pEdict;
		st.alive_seen = FALSE;
	}

	// Keep the per-bot stream in lockstep with sgtc_bot_seed; a worldspawn
	// re-seed (new map) rebuilds every bot stream.
	{
		const unsigned int base = SGTC_Bots_SeedBase();

		if( !s_class_rng_ready || base != s_class_rng_base )
		{
			for( int i = 0; i < 32; i++ )
			{
				s_class_state[i].rng = SGTC_Bots_MixSeed( base, i );
				if( !s_class_state[i].rng )
					s_class_state[i].rng = 0x9e3779b9u;
			}

			s_class_objective_count = 0;
			s_class_next_objective_scan = 0.0f;
			s_class_rng_base = base;
			s_class_rng_ready = TRUE;
		}
	}

	if( !pl->IsAlive() || pl->IsObserver() )
	{
		if( st.alive_seen )
		{
			SGTC_Bots_ClassStateReset( &st );
			st.alive_seen = FALSE;
		}
		return;
	}

	if( !st.alive_seen )
	{
		SGTC_Bots_ClassStateReset( &st );
		st.alive_seen = TRUE;
	}

	int team = SGTC_TEAM_NONE;
	int cls = SGTC_CLASS_NONE;
	const sgtc_bot_class_profile_t *profile;

	if( !SGTC_Bots_ClassResolve( pl, &team, &cls ) )
		return;

	profile = SGTC_Bots_ClassProfile( team, cls );
	if( !profile )
		return;

	const int role = SGTC_Bots_ClassEffectiveRole( profile, index );
	const int skill = SGTC_Bots_ClassSkill( pBot );
	const float now = gpGlobals->time;
	const unsigned int rng = st.rng;

	SGTC_Bots_ClassXorShift( &st.rng );

	// --- gathered state ---------------------------------------------------
	const qboolean enemy_visible =
		( pBot.pBotEnemy != NULL && !FNullEnt( pBot.pBotEnemy ) &&
			FPredictedVisible( pBot ) ) ? TRUE : FALSE;

	float enemy_distance = -1.0f;
	if( pBot.pBotEnemy && !FNullEnt( pBot.pBotEnemy ) )
		enemy_distance = ( pBot.pBotEnemy->v.origin - pEdict->v.origin ).Length();

	float out_of_combat = 999.0f;
	if( pBot.f_bot_see_enemy_time > 0.0f && now >= pBot.f_bot_see_enemy_time )
		out_of_combat = now - pBot.f_bot_see_enemy_time;
	if( pBot.f_last_time_attacked > 0.0f && now >= pBot.f_last_time_attacked &&
		now - pBot.f_last_time_attacked < out_of_combat )
		out_of_combat = now - pBot.f_last_time_attacked;
	if( enemy_visible )
		out_of_combat = 0.0f;

	const qboolean under_fire =
		( pBot.f_last_time_attacked > 0.0f &&
			now - pBot.f_last_time_attacked < 2.0f ) ? TRUE : FALSE;

	float health = pl->pev->health / 100.0f;
	if( health < 0.0f )
		health = 0.0f;
	if( health > 1.0f )
		health = 1.0f;

	const int charge = pl->m_iShieldCharge;
	st.last_charge = charge;

	const int tac_ammo = SGTC_Bots_ClassTacAmmo( pl );
	const int tac_near = SGTC_Bots_ClassCountTacs( pl );
	const float objective_distance = SGTC_Bots_ClassObjectiveDistance( pl->pev->origin );
	// Defenders hold a wider slice of ground around the objective; the pure
	// decision still applies its own ammo/skill/enemy-clear gates.
	const float chokepoint_range = ( role == SGTC_BOT_ROLE_DEFENDER ) ? 900.0f : 700.0f;
	// f_pause_time is the deadline of the current jk_botti pause, not a flag.
	const qboolean at_chokepoint =
		( objective_distance >= 0.0f && objective_distance <= chokepoint_range ) ||
		( pBot.f_pause_time > now ) ? TRUE : FALSE;
	const float front_moved =
		( st.last_tac_place >= 0.0f ) ? now - st.last_tac_place : 999.0f;

	qboolean button_used = FBitSet( pEdict->v.button, IN_ATTACK ) ? TRUE : FALSE;

	// --- pending action from the previous think ---------------------------
	SGTC_Bots_ClassPressPending( pBot, pl, st, cls, enemy_visible, &button_used );

	// --- shield -----------------------------------------------------------
	if( ( profile->abilities & SGTC_BOT_AB_SHIELD ) &&
		SGTC_Bots_ClassHasItem( pl, "weapon_shield" ) )
	{
		sgtc_bot_shield_ctx_t ctx;

		memset( &ctx, 0, sizeof( ctx ) );
		ctx.skill = skill;
		ctx.health = health;
		ctx.charge = (float)charge;
		ctx.shield_up = pl->m_bShieldWall ? 1 : 0;
		ctx.enemy_visible = enemy_visible ? 1 : 0;
		ctx.enemy_distance = enemy_distance;
		ctx.under_fire = under_fire ? 1 : 0;
		ctx.out_of_combat = out_of_combat;
		ctx.rng = rng;

		const int want = SGTC_Bots_WantsShield( profile, &ctx );

		if( want > 0 && !pl->m_bShieldWall && !button_used &&
			now >= st.suppress_until &&
			now >= st.next_ability[SGTC_CP_AB_SHIELD] )
		{
			if( SGTC_Bots_ClassActiveIs( pl, "weapon_shield" ) )
			{
				pEdict->v.button |= IN_ATTACK;
				button_used = TRUE;
				st.next_ability[SGTC_CP_AB_SHIELD] = now + 0.5f;
				st.suppress_until = now + 0.4f;
				{
					char detail[24];
					snprintf( detail, sizeof( detail ), "charge=%d", charge );
					SGTC_Bots_ClassEmit( pBot, "shield", cls, detail );
				}
			}
			else
			{
				FakeClientCommand( pEdict, "weapon_shield", NULL, NULL );
				st.pending = SGTC_CP_PEND_SHIELD;
				st.pending_target = 0;
				st.pending_cloak = 0;
				st.pending_time = now;
				st.next_ability[SGTC_CP_AB_SHIELD] = now + 0.5f;
				st.suppress_until = now + 0.4f;
				if (pBot.f_shoot_time < now + 0.55f)
					pBot.f_shoot_time = now + 0.55f;
			}
		}
		else if( want < 0 && pl->m_bShieldWall )
		{
			// SGTC_ShieldThinker clears m_bShieldWall (and the sprint drain)
			// itself when the charge is empty; dropping it here is the same
			// clear on request, and the thinker keeps the two fields in sync.
			pl->m_bShieldWall = 0;
			st.next_ability[SGTC_CP_AB_SHIELD] = now + 0.5f;
			SGTC_Bots_ClassEmit( pBot, "shield", cls, "off" );
		}
	}

	// Remember the last ordinary (non-ability) weapon so an ability draw can be
	// followed by an immediate return to the real gun instead of leaving the
	// ability item active for jk_botti to re-select.
	if( pl->m_pActiveItem && pl->m_pActiveItem->pev )
	{
		const char *active = STRING( pl->m_pActiveItem->pev->classname );
		if( active && active[0] && strncmp( active, "weapon_invis", 12 ) &&
			strcmp( active, "weapon_shield" ) && strcmp( active, "weapon_tacgun" ) &&
			strcmp( active, "weapon_medic" ) && strcmp( active, "weapon_goaregen" ) &&
			strcmp( active, "weapon_larve" ) )
			strlcpy( st.combat_weapon, active, sizeof( st.combat_weapon ) );
	}

	// --- cloak ------------------------------------------------------------
	// sgtc_test forcecloak_<slot>: probe builds bypass the ability/item/shield
	// gate so the real weapon_invis toggle path can be driven deterministically.
	const qboolean forced_cloak_probe =
		SGTC_Bots_RulesAI_ForceCloakActive( index ) ? TRUE : FALSE;
	if( !button_used && now >= st.suppress_until &&
		( forced_cloak_probe ||
			( ( profile->abilities & SGTC_BOT_AB_CLOAK ) &&
				SGTC_Bots_ClassHasItem( pl, "weapon_invis" ) &&
				!pl->m_bShieldWall ) ) &&
		now >= st.next_ability[SGTC_CP_AB_CLOAK] )
	{
		sgtc_bot_cloak_ctx_t ctx;

		memset( &ctx, 0, sizeof( ctx ) );
		ctx.skill = skill;
		ctx.charge = (float)charge;
		ctx.cloaked = pl->m_bShieldSprint ? 1 : 0;
		ctx.enemy_visible = enemy_visible ? 1 : 0;
		ctx.enemy_distance = enemy_distance;
		ctx.out_of_combat = out_of_combat;
		ctx.cloaked_time = ( ctx.cloaked && st.cloak_since > 0.0f )
			? ( now - st.cloak_since ) : -1.0f;
		ctx.rng = rng;

		int want = SGTC_Bots_WantsCloak( profile, &ctx );

		// sgtc_test forcecloak: deterministically engage the cloak through the
		// real weapon toggle path for headless probes. Release still follows
		// the normal bounded/no-enemy/charge rules below.
		if( forced_cloak_probe && !ctx.cloaked )
			want = 1;

		// Hard bound independent of the profile: drop the cloak after 10 s of
		// continuous use even if the decision layer wants to keep it.
		if( ctx.cloaked && st.cloak_since > 0.0f && now - st.cloak_since > 10.0f )
			want = -1;
		if( !ctx.cloaked )
		{
			st.cloak_since = 0.0f;
			st.cloak_retry_after = 0.0f;
		}

		// weapon_invis can refuse a toggle (notably while m_iShieldCharge <= 3).
		// Without this the off request is pressed, rejected and re-pressed every
		// think, which reads as cloak churn in logs and server traffic.  Back off
		// and wait for the charge/weapon state to settle instead.
		if( ctx.cloaked && want < 0 && now < st.cloak_retry_after )
			want = 0;

		// The weapon refuses to don the cloak below 4 charge; dropping it is
		// always allowed, so the uncloak branch must not be gated by charge.
		if( want > 0 && charge <= 3 )
			want = 0;

		if( want != 0 )
		{
			const int desired = ( want > 0 ) ? 1 : 0;

			if( ( pl->m_bShieldSprint ? 1 : 0 ) != desired )
			{
				if( SGTC_Bots_ClassActiveIs( pl, "weapon_invis" ) )
				{
					pEdict->v.button |= IN_ATTACK;
					button_used = TRUE;
					st.next_ability[SGTC_CP_AB_CLOAK] = now + 0.5f;
					st.suppress_until = now + 0.4f;
					st.cloak_since = desired ? now : 0.0f;
					if( !desired )
						st.cloak_retry_after = now + ( ( charge <= 3 ) ? 2.5f : 1.0f );
					SGTC_Bots_ClassEmit( pBot, "cloak", cls,
						desired ? "cloak on" : "cloak off" );
				}
				else
				{
					FakeClientCommand( pEdict, "weapon_invis", NULL, NULL );
					st.pending = SGTC_CP_PEND_CLOAK;
					st.pending_target = 0;
					st.pending_cloak = desired;
					st.pending_time = now;
					st.next_ability[SGTC_CP_AB_CLOAK] = now + 0.5f;
					st.suppress_until = now + 0.4f;
					if( !desired )
						st.cloak_retry_after = now + ( ( charge <= 3 ) ? 2.5f : 1.0f );
					if (pBot.f_shoot_time < now + 0.55f)
						pBot.f_shoot_time = now + 0.55f;
				}
			}
		}
	}

	// --- TAC --------------------------------------------------------------
	if( !button_used && now >= st.suppress_until &&
		( profile->abilities & SGTC_BOT_AB_TAC ) &&
		SGTC_Bots_ClassHasItem( pl, "weapon_tacgun" ) &&
		now >= st.next_ability[SGTC_CP_AB_TAC] )
	{
		sgtc_bot_tac_ctx_t ctx;

		memset( &ctx, 0, sizeof( ctx ) );
		ctx.skill = skill;
		ctx.tac_ammo = tac_ammo;
		ctx.tac_near = tac_near;
		ctx.objective_distance = objective_distance;
		ctx.at_chokepoint = at_chokepoint ? 1 : 0;
		ctx.enemy_visible = enemy_visible ? 1 : 0;
		ctx.enemy_distance = enemy_distance;
		ctx.front_moved = front_moved;
		ctx.rng = rng;

		if( SGTC_Bots_WantsTacPlace( profile, &ctx ) > 0 && tac_ammo > 0 )
		{
			if( SGTC_Bots_ClassActiveIs( pl, "weapon_tacgun" ) )
			{
				SGTC_Bots_ClassAimGround( pEdict );

				pEdict->v.button |= IN_ATTACK;
				button_used = TRUE;
				st.last_tac_place = now;
				st.next_ability[SGTC_CP_AB_TAC] = now + 1.0f;
				st.suppress_until = now + 0.4f;
				SGTC_Bots_ClassEmit( pBot, "tac", cls, "place" );
			}
			else
			{
				FakeClientCommand( pEdict, "weapon_tacgun", NULL, NULL );
				st.pending = SGTC_CP_PEND_TAC;
				st.pending_target = 0;
				st.pending_cloak = 0;
				st.pending_time = now;
				st.next_ability[SGTC_CP_AB_TAC] = now + 1.0f;
				st.suppress_until = now + 0.4f;
				if (pBot.f_shoot_time < now + 0.55f)
					pBot.f_shoot_time = now + 0.55f;
			}
		}
	}

	// --- talent -----------------------------------------------------------
	if( ( profile->abilities & SGTC_BOT_AB_TALENT ) &&
		profile->talent != SGTC_BOT_TALENT_NONE &&
		now >= st.next_ability[SGTC_CP_AB_TALENT] )
	{
		sgtc_bot_talent_ctx_t ctx;

		memset( &ctx, 0, sizeof( ctx ) );
		ctx.skill = skill;
		ctx.talent = profile->talent;
		ctx.cooldown_ready = ( now >= pl->m_flTalentCooldown ) ? 1 : 0;
		ctx.enemy_visible = enemy_visible ? 1 : 0;
		ctx.enemy_distance = enemy_distance;
		ctx.allies_near = SGTC_Bots_ClassCountAllies( pl, team, 300.0f );
		ctx.has_item = SGTC_Bots_ClassHasTalentItem( pl, profile->talent ) ? 1 : 0;
		ctx.rng = rng;

		if( SGTC_Bots_WantsTalent( profile, &ctx ) > 0 )
		{
			const char *talent_weapon = NULL;

			if( profile->talent == SGTC_BOT_TALENT_ZATBOOM )
				talent_weapon = "weapon_zat";
			else if( profile->talent == SGTC_BOT_TALENT_FLASH )
				talent_weapon = "weapon_gacgoa";

			if( talent_weapon && !SGTC_Bots_ClassActiveIs( pl, talent_weapon ) )
			{
				// Two-phase: the talent validates the active weapon, so draw
				// it first and cast on a later think.
				FakeClientCommand( pEdict, talent_weapon, NULL, NULL );
				st.pending = SGTC_CP_PEND_TALENT;
				st.pending_target = 0;
				st.pending_cloak = 0;
				strlcpy( st.pending_weapon, talent_weapon,
					sizeof( st.pending_weapon ) );
				st.pending_time = now;
				st.next_ability[SGTC_CP_AB_TALENT] = now + 1.0f;
				st.suppress_until = now + 0.4f;
				if( pBot.f_shoot_time < now + 0.55f )
					pBot.f_shoot_time = now + 0.55f;
			}
			else
			{
				FakeClientCommand( pEdict, "talent", NULL, NULL );
				st.next_ability[SGTC_CP_AB_TALENT] = now + 1.0f;
				SGTC_Bots_ClassEmit( pBot, "talent", cls, "talent" );
			}
		}
	}

	// --- heal -------------------------------------------------------------
	const qboolean medic_urgent =
		( !enemy_visible || ( pBot.f_last_time_attacked < now - 1.0f ) ) ? TRUE : FALSE;

	if( !button_used && now >= st.suppress_until && medic_urgent &&
		( profile->abilities & SGTC_BOT_AB_HEAL ) &&
		now >= st.next_heal )
	{
		const char *heal_weapon = SGTC_Bots_ClassHealWeapon( pl, team );

		if( heal_weapon )
		{
			CBasePlayer *ally = NULL;
			float ally_health = 1.0f;
			float ally_distance = -1.0f;
			qboolean ally_human = FALSE;

			if( SGTC_Bots_ClassFindWoundedAlly( pl, team, &ally, &ally_health, &ally_distance, &ally_human ) )
			{
				sgtc_bot_heal_ctx_t ctx;
				const char *ammo_name =
					( strcmp( heal_weapon, "weapon_goaregen" ) == 0 )
						? SGTC_AMMO_GOAREGEN : SGTC_AMMO_MEDGOO;
				const int heal_ammo = SGTC_Bots_ClassAmmo( pl, ammo_name );

				memset( &ctx, 0, sizeof( ctx ) );
				ctx.skill = skill;
				ctx.ally_health = ally_health;
				ctx.ally_distance = ally_distance;
				ctx.ally_in_front = 1;
				ctx.ammo = heal_ammo;
				ctx.enemy_visible = enemy_visible ? 1 : 0;
				ctx.rng = rng;

				if( SGTC_Bots_WantsHeal( profile, &ctx ) > 0 )
				{
					if( SGTC_Bots_ClassActiveIs( pl, heal_weapon ) )
					{
						SGTC_Bots_ClassAimAt( pEdict, SGTC_Bots_ClassAllyChest( ally ) );
						pEdict->v.button |= IN_ATTACK;
						button_used = TRUE;
						st.next_heal = now + 0.8f;
						st.suppress_until = now + 0.4f;
						SGTC_Bots_ClassEmit( pBot, "heal", cls, "ally" );
					}
					else
					{
						FakeClientCommand( pEdict, heal_weapon, NULL, NULL );
						st.pending = SGTC_CP_PEND_HEAL;
						st.pending_target = ally->entindex();
						strlcpy( st.pending_weapon, heal_weapon,
							sizeof( st.pending_weapon ) );
						st.pending_cloak = 0;
						st.pending_time = now;
						st.next_heal = now + 0.8f;
						st.suppress_until = now + 0.4f;
						if( pBot.f_shoot_time < now + 0.55f )
							pBot.f_shoot_time = now + 0.55f;
					}
				}
			}
		}
	}

	// --- TAC retrieve -----------------------------------------------------
	// A TAC that has held a spot for 20 s is stale once the front moves; the
	// bot walks up and presses +use on its own TAC (TAC pickup requires
	// USE_SET and the owner, and returns exactly one Tacgun round). The
	// engine refuses while the pool is full or the TAC was hit in the last
	// second; both are harmless no-ops.
	if( ( profile->abilities & SGTC_BOT_AB_TAC ) && !button_used &&
		now >= st.next_retrieve && st.last_tac_place >= 0.0f &&
		now - st.last_tac_place >= 20.0f && tac_ammo < SGTC_TAC_MAX_CARRY )
	{
		edict_t *tac = SGTC_Bots_ClassFindOwnTac( pl, 64.0f );

		st.next_retrieve = now + 3.0f;

		if( tac )
		{
			SGTC_Bots_ClassAimAt( pEdict, tac->v.origin + Vector( 0.0f, 0.0f, 8.0f ) );
			pEdict->v.button |= IN_USE;
			button_used = TRUE;
			SGTC_Bots_ClassEmit( pBot, "tac", cls, "retrieve" );
		}
	}

	// --- counter TAC ------------------------------------------------------
	// A deployed TAC is a real threat and the enemy can see it too. Shoot it
	// down with whatever firearm is out, without switching weapons and
	// without emitting an ability event.
	if( !button_used && st.pending == SGTC_CP_PEND_NONE && !enemy_visible &&
		now >= st.next_counter )
	{
		const char *active = SGTC_Bots_ClassActiveWeapon( pl );
		const qboolean ability_weapon =
			( !active ||
				FStrEq( active, "weapon_shield" ) || FStrEq( active, "weapon_invis" ) ||
				FStrEq( active, "weapon_tacgun" ) || FStrEq( active, "weapon_medic" ) ||
				FStrEq( active, "weapon_goaregen" ) ) ? TRUE : FALSE;

		if( !ability_weapon )
		{
			edict_t *tac = SGTC_Bots_ClassFindEnemyTac( pl );

			if( tac )
			{
				SGTC_Bots_ClassAimAt( pEdict, tac->v.origin + Vector( 0.0f, 0.0f, 8.0f ) );
				pEdict->v.button |= IN_ATTACK;
				button_used = TRUE;
				st.next_counter = now + 0.5f;
			}
		}
	}

	// Return to the real gun promptly after an ability draw so the bot is not
	// left holding invis/shield/tacgun/medic; jk_botti would otherwise re-select
	// on its own schedule and the active-item churn reads as extra switching.
	if( st.pending == SGTC_CP_PEND_NONE && st.combat_weapon[0] &&
		now >= st.suppress_until && pl->m_pActiveItem && pl->m_pActiveItem->pev )
	{
		const char *active = STRING( pl->m_pActiveItem->pev->classname );
		if( active && ( !strcmp( active, "weapon_invis" ) || !strcmp( active, "weapon_shield" ) ||
			!strcmp( active, "weapon_tacgun" ) || !strcmp( active, "weapon_medic" ) ||
			!strcmp( active, "weapon_goaregen" ) || !strcmp( active, "weapon_larve" ) ) )
		{
			FakeClientCommand( pEdict, st.combat_weapon, NULL, NULL );
			st.suppress_until = now + 0.6f;
		}
	}

	(void)button_used;
}

// ---------------------------------------------------------------------------
// Spawn loadout. Bots do not drive the buy menu, so PlayerSpawn asks for their
// gond argument list here instead: the class preference order, filtered by the
// exact rules SGTC_SanitizeGondLoadout() enforces at grant time (allow-list,
// cost, budget, primary/special caps). The normal spawn grant then hands the
// items out, so a bot can never end up with something a human of the same
// team/class could not buy with the same budget. With mp_wepselmode 2 the
// grant path only accepts a fixed class pack, so the bot selects its class
// pack 1 instead.
//
// Call from PlayerSpawn after SetWeaponAllow()/SGTC_SpawnResendProgress() (the
// allow-list must be current and the resend can clear stale gond args) and
// before SGTC_SanitizeGondLoadout().
void SGTC_Bots_PrepareSpawnLoadout( CBasePlayer *pl )
{
	const sgtc_bot_class_profile_t *profile;
	int team = SGTC_TEAM_NONE;
	int cls = SGTC_CLASS_NONE;
	int cost[SGTC_NUM_WEAPON_SLOTS];
	int i;

	if( !pl || !pl->pev )
		return;
	if( !FBitSet( pl->pev->flags, FL_FAKECLIENT ) )
		return;
	if( !SGTC_Bots_KitEnabled() )
		return;
	if( !SGTC_Bots_ClassResolve( pl, &team, &cls ) )
		return;

	profile = SGTC_Bots_ClassProfile( team, cls );
	if( !profile )
		return;

	if( sgtc_cvar_wepselmode.value == 2.0f )
	{
		SGTC_Bots_SelectPackLoadout( profile, pl->m_iSgtcGondArgs );
		return;
	}

	for( i = 0; i < SGTC_NUM_WEAPON_SLOTS; i++ )
		cost[i] = (int)( sgtc_wpnC[i]->value + 0.5f );

	SGTC_Bots_SelectKitLoadout( profile, pl->m_iProgressTeam, cost,
		pl->m_iNukeBonus, pl->m_iSgtcGondArgs );
}

// Direct grant for bots whose team/class became known after their spawn
// (PlayerSpawn has already run, so the normal gond grant list was empty).
// Mirrors the spawn grant: same pack/kit slot list, melee, and ammo.
void SGTC_Bots_GiveSpawnLoadout( CBasePlayer *pl )
{
	if( !pl || !pl->pev )
		return;
	if( !FBitSet( pl->pev->flags, FL_FAKECLIENT ) )
		return;
	if( !SGTC_Bots_KitEnabled() )
		return;
	if( !pl->IsAlive() || pl->IsObserver() )
		return;
	// build-9: a bot spawned through the fallback path still gets the team melee
	// weapon in PlayerSpawn; that is not an armed state.  Treat melee-only as
	// unarmed so the class kit is granted on the first life too.
	qboolean bHasCombatWeapon = FALSE;
	for( int i = 0; i < MAX_ITEM_TYPES && !bHasCombatWeapon; ++i )
	{
		for( CBasePlayerItem *pItem = pl->m_rgpPlayerItems[i]; pItem; pItem = pItem->m_pNext )
		{
			const char *pszItem = STRING( pItem->pev->classname );
			if( strcmp( pszItem, "weapon_couteau" ) && strcmp( pszItem, "weapon_coujaf" ) )
			{
				bHasCombatWeapon = TRUE;
				break;
			}
		}
	}
	// SGTC weapon id 0 is the team melee weapon; bit 0 in pev->weapons is the
	// only bit a real "unarmed" bot carries.  A bot whose inventory scan sees
	// combat items but whose weapon bitfield has no combat bit is in the stale
	// post-death/respawn state: clear the leftovers and re-grant the kit.
	const BOOL bHasCombatBit = ( pl->pev->weapons & ~( 1 << 0 ) ) != 0;
	if( bHasCombatWeapon && bHasCombatBit )
		return;	// already armed and the client sees the bits
	if( bHasCombatWeapon && !bHasCombatBit )
	{
		pl->RemoveAllItems( TRUE );
		bHasCombatWeapon = FALSE;
	}
	if( bHasCombatWeapon == FALSE )
	{
		// GiveNamedItem is rejected by gamerules CanHavePlayerItem/CanHaveAmmo
		// when the reserve is already at its cap (a re-grant after a respawn
		// leaves m_rgAmmo full), so empty the pools before re-granting.  The
		// normal reserve fill below runs after the grant loop.
		for( int i = 0; i < MAX_AMMO_SLOTS; ++i )
			pl->m_rgAmmo[i] = 0;
	}

	int team = SGTC_TEAM_NONE;
	int cls = SGTC_CLASS_NONE;
	if( !SGTC_Bots_ClassResolve( pl, &team, &cls ) )
		return;
	const sgtc_bot_class_profile_t *profile = SGTC_Bots_ClassProfile( team, cls );
	if( !profile )
		return;

	int slots[SGTC_NUM_WEAPON_SLOTS];
	for( int i = 0; i < SGTC_NUM_WEAPON_SLOTS; ++i )
		slots[i] = 0;

	if( sgtc_cvar_wepselmode.value == 2.0f )
	{
		int preset[SGTC_PACK_MAX_SLOTS];
		const int n = SGTC_PackGetSlots( team, cls - SGTC_CLASS_FIRST, 1, preset );
		for( int i = 0; i < n; ++i )
		{
			if( preset[i] >= 0 && preset[i] < SGTC_NUM_WEAPON_SLOTS )
				slots[preset[i]] = 1;
		}
	}
	else
	{
		int cost[SGTC_NUM_WEAPON_SLOTS];
		for( int i = 0; i < SGTC_NUM_WEAPON_SLOTS; ++i )
			cost[i] = (int)( sgtc_wpnC[i]->value + 0.5f );
		SGTC_Bots_SelectKitLoadout( profile, pl->m_iProgressTeam, cost,
			pl->m_iNukeBonus, slots );
	}

	int granted = 0;
	for( int i = 0; i < SGTC_NUM_WEAPON_SLOTS; ++i )
	{
		if( !slots[i] || !SGTC_WeaponSlotClassname[i] )
			continue;
		pl->GiveNamedItem( SGTC_WeaponSlotClassname[i] );
		++granted;
	}

	// Reserve ammo: GiveNamedItem only creates the weapon, the map normally
	// supplies the ammo.  Fill every carried weapon's primary pool to its cap.
	for( int j = 0; j < MAX_ITEM_TYPES; ++j )
	{
		for( CBasePlayerItem *pItem = pl->m_rgpPlayerItems[j]; pItem; pItem = pItem->m_pNext )
		{
			ItemInfo info;
			memset( &info, 0, sizeof( info ) );
			if( !pItem->GetItemInfo( &info ) || !info.pszAmmo1 )
				continue;
			const int iMax = ( info.iMaxAmmo1 > 0 ) ? info.iMaxAmmo1 : 100;
			pl->GiveAmmo( iMax, info.pszAmmo1, info.iMaxAmmo1 );
		}
	}

	// spawn rules always add the team melee weapon
	pl->GiveNamedItem( ( pl->pev->team == SGTC_ENTTEAM_GOAULD ) ? "weapon_coujaf" : "weapon_couteau" );

	ALERT( at_console, "SGTC_BOT_LOADOUT {\"cmd\":\"grant\",\"name\":\"%s\",\"team\":%d,\"class\":%d,\"slots\":%d}\n",
		STRING( pl->pev->netname ), team, cls, granted );
	SGTC_ReaDebug_ArmPlayer( pl );	// build-8: no-op unless sgtc_rea_debug 1
}

int Bot_GetClassProfile( int bot_index, sgtc_bot_class_view_t *out )
{
	if( !out || bot_index < 0 || bot_index >= 32 )
		return -1;
	if( !bots[bot_index].is_used || FNullEnt( bots[bot_index].pEdict ) )
		return -1;

	CBasePlayer *pl = (CBasePlayer *)CBaseEntity::Instance( bots[bot_index].pEdict );
	int team = SGTC_TEAM_NONE;
	int cls = SGTC_CLASS_NONE;

	if( !pl || !SGTC_Bots_ClassResolve( pl, &team, &cls ) )
		return -1;

	const sgtc_bot_class_profile_t *profile = SGTC_Bots_ClassProfile( team, cls );

	if( !profile )
		return -1;

	memset( out, 0, sizeof( *out ) );
	out->team = team;
	out->cls = cls;
	out->role = SGTC_Bots_ClassEffectiveRole( profile, bot_index );
	out->abilities = profile->abilities;
	out->talent = profile->talent;
	out->range_min = profile->range_min;
	out->range_max = profile->range_max;
	out->class_name = profile->name;
	return 0;
}

int Bot_SetRole( int bot_index, int role )
{
	if( bot_index < 0 || bot_index >= 32 )
		return -1;
	if( role < SGTC_BOT_ROLE_NONE || role >= SGTC_BOT_ROLE_COUNT )
		return -1;

	s_class_state[bot_index].role_override = role;
	return 0;
}

int Bot_GetRole( int bot_index )
{
	if( bot_index < 0 || bot_index >= 32 )
		return -1;

	if( !bots[bot_index].is_used || FNullEnt( bots[bot_index].pEdict ) )
		return -1;

	const int override_role = s_class_state[bot_index].role_override;

	if( override_role > SGTC_BOT_ROLE_NONE && override_role < SGTC_BOT_ROLE_COUNT )
		return override_role;

	CBasePlayer *pl = (CBasePlayer *)CBaseEntity::Instance( bots[bot_index].pEdict );
	int team = SGTC_TEAM_NONE;
	int cls = SGTC_CLASS_NONE;

	if( !pl || !SGTC_Bots_ClassResolve( pl, &team, &cls ) )
		return -1;

	const sgtc_bot_class_profile_t *profile = SGTC_Bots_ClassProfile( team, cls );

	return profile ? profile->role : -1;
}
