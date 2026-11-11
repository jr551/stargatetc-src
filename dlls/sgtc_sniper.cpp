// sgtc_sniper.cpp - server side of the PSG1 scope sway: cvars, bot aim
// compensation, the "SgtcHit" client feedback message and the corpse
// death-punch.
//
// Humans are never corrected here.  Their sway lives entirely client-side and
// travels inside the usercmd viewangles, so the server agrees by construction;
// this module only reproduces the same offset for fake clients so bots can
// cancel it before they aim.
//
// The evaluator is shared (game_shared/sgtc_sniper_sway.h) so both sides run
// literally the same arithmetic.

#include "extdll.h"
#include "util.h"
#include "usercmd.h"	// usercmd_s (bot fire-frame compensation)
#include "cbase.h"
#include "monsters.h"	// HITGROUP_HEAD
#include "player.h"
#include "weapons.h"	// BULLET_PLAYER_*
#include "skill.h"	// gSkillData (damage estimate for the hit marker)
#include "game.h"
#include "sgtc_weapons.h"	// SGTC_WPN_PSG1 / ZAT / ZATARC
#include "sgtc_sniper.h"

#include "sgtc_sniper_sway.h"

cvar_t sgtc_cvar_sniper_sway = { "sgtc_sniper_sway", "1", FCVAR_SERVER };
cvar_t sgtc_cvar_sniper_sway_bots = { "sgtc_sniper_sway_bots", "0.6", FCVAR_SERVER };
cvar_t sgtc_cvar_sniper_deathpunch = { "sgtc_sniper_deathpunch", "1", FCVAR_SERVER };

// Owned by this module only; SGTC_Sniper_RegisterMessages fills it in.
int gmsgSgtcHit = 0;

// Window after a sniper shot in which a kill still counts as "shot by the
// sniper" for the death punch.
#define SGTC_SNIPER_FIRE_WINDOW	0.35f

#define SGTC_SNIPER_HURT_WINDOW	1.5f
#define SGTC_SNIPER_FIRE_SWAY_WINDOW	1.2f

// Death punch tuning.
#define SGTC_DEATHPUNCH_PUSH		220.0f
#define SGTC_DEATHPUNCH_LIFT		120.0f
#define SGTC_DEATHPUNCH_PITCH		-8.0f
#define SGTC_DEATHPUNCH_YAW			4.0f

#define SGTC_SWAY_SPEED_REF			320.0f

void SGTC_Sniper_RegisterMessages( void )
{
	if( gmsgSgtcHit )
		return;

	gmsgSgtcHit = REG_USER_MSG( "SgtcHit", 2 );
}

void SGTC_Sniper_Init( void )
{
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_sniper_sway );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_sniper_sway_bots );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_sniper_deathpunch );

	// The "SgtcHit" usermsg is registered from SGTC_LinkUserMessages_Suffix
	// (SGTC_Sniper_RegisterMessages), not from GameDLLInit: user messages get
	// their slots assigned during link time, not at DLL load.
}

// ---------------------------------------------------------------------------
// shared evaluation helpers
// ---------------------------------------------------------------------------

static unsigned int SGTC_Sniper_Seed( CBasePlayer *pPlayer )
{
	unsigned int seed = (unsigned int)pPlayer->random_seed;

	if( seed == 0 )
		seed = (unsigned int)ENTINDEX( pPlayer->edict() ) * 2654435761u;

	return seed;
}

static bool SGTC_Sniper_HoldsScopedWeapon( CBasePlayer *pPlayer )
{
	if( !pPlayer || !pPlayer->m_pActiveItem )
		return false;

	switch( pPlayer->m_pActiveItem->m_iId )
	{
	case SGTC_WPN_PSG1:
	case SGTC_WPN_ZAT:
	case SGTC_WPN_ZATARC:
		return true;
	default:
		break;
	}

	return false;
}

static float SGTC_Sniper_Strength( void )
{
	float strength = sgtc_cvar_sniper_sway.value * sgtc_cvar_sniper_sway_bots.value;

	if( !( strength > 0.0f ) )
		return 0.0f;

	return strength;
}

static float SGTC_Sniper_WrapPitch( float pitch )
{
	while( pitch > 180.0f )
		pitch -= 360.0f;
	while( pitch < -180.0f )
		pitch += 360.0f;

	if( pitch > 89.0f )
		pitch = 89.0f;
	else if( pitch < -89.0f )
		pitch = -89.0f;

	return pitch;
}

static float SGTC_Sniper_WrapYaw( float yaw )
{
	while( yaw > 180.0f )
		yaw -= 360.0f;
	while( yaw < -180.0f )
		yaw += 360.0f;
	return yaw;
}

// One place builds the evaluator inputs for a bot, so SwayAimVector and
// BotCompensate can never disagree.
static void SGTC_Sniper_BuildInputs( CBasePlayer *pPlayer, float strength, SgtcSwayInputs &in )
{
	Vector velocity = pPlayer->pev->velocity;
	float speed = (float)sqrt( (double)( velocity.x * velocity.x + velocity.y * velocity.y ) );
	float move = speed / SGTC_SWAY_SPEED_REF;
	float now = gpGlobals->time;

	if( move > 1.0f )
		move = 1.0f;
	if( move < 0.0f )
		move = 0.0f;

	in.move = move;
	in.airborne = ( pPlayer->pev->flags & FL_ONGROUND ) ? 0.0f : 1.0f;
	in.crouch = ( pPlayer->pev->flags & FL_DUCKING ) ? 1.0f : 0.0f;
	in.hurt = ( pPlayer->m_flSgtcLastSniperHurt > 0.0f
		&& now - pPlayer->m_flSgtcLastSniperHurt < SGTC_SNIPER_HURT_WINDOW ) ? 1.0f : 0.0f;
	in.fire = ( pPlayer->m_flSgtcLastSniperFire > 0.0f
		&& now - pPlayer->m_flSgtcLastSniperFire < SGTC_SNIPER_FIRE_SWAY_WINDOW ) ? 1.0f : 0.0f;
	in.breath = 0.0f;	// bots have no hold-breath
	in.strength = strength;
}

// Applies a pitch/yaw offset to an aim direction, the same way the client
// rotates the view: through angles so both sides see identical geometry.
static void SGTC_Sniper_RotateAim( const Vector &vecAiming, float pitchDeg, float yawDeg, Vector *pOut )
{
	Vector angles = UTIL_VecToAngles( vecAiming );
	float rgflAngles[3];
	float rgflForward[3];

	angles.x = SGTC_Sniper_WrapPitch( angles.x + pitchDeg );
	angles.y = SGTC_Sniper_WrapYaw( angles.y + yawDeg );
	angles.z = 0.0f;

	rgflAngles[0] = angles.x;
	rgflAngles[1] = angles.y;
	rgflAngles[2] = angles.z;

	g_engfuncs.pfnAngleVectors( rgflAngles, rgflForward, NULL, NULL );
	*pOut = Vector( rgflForward ).Normalize();
}

// ---------------------------------------------------------------------------
// public hooks
// ---------------------------------------------------------------------------

void SGTC_Sniper_SwayAimVector( CBasePlayer *pPlayer, Vector *vecAiming )
{
	if( !pPlayer || !vecAiming )
		return;

	// Humans carry their sway in the usercmd; only bots need it here.
	if( !( pPlayer->pev->flags & FL_FAKECLIENT ) )
		return;

	float strength = SGTC_Sniper_Strength();

	if( strength <= 0.0f )
		return;

	SgtcSwayInputs in;
	SGTC_Sniper_BuildInputs( pPlayer, strength, in );

	float pitch = 0.0f;
	float yaw = 0.0f;

	SGTC_Sway_Eval( SGTC_Sniper_Seed( pPlayer ), gpGlobals->time, in, &pitch, &yaw );

	SGTC_Sniper_RotateAim( *vecAiming, pitch, yaw, vecAiming );
}

void SGTC_Sniper_NoteFire( CBasePlayer *pPlayer )
{
	if( !pPlayer )
		return;

	pPlayer->m_flSgtcLastSniperFire = gpGlobals->time;
}

void SGTC_Sniper_NoteHurt( CBasePlayer *pPlayer )
{
	if( !pPlayer )
		return;

	pPlayer->m_flSgtcLastSniperHurt = gpGlobals->time;
}

// Damage byte for the hit marker.  Cosmetic only: a few stock bullet types
// map onto the skill table, everything else reports 0.
static int SGTC_Sniper_DamageEstimate( int iBulletType )
{
	float damage = 0.0f;

	switch( iBulletType )
	{
	case BULLET_PLAYER_9MM:		damage = gSkillData.plrDmg9MM; break;
	case BULLET_PLAYER_MP5:		damage = gSkillData.plrDmgMP5; break;
	case BULLET_PLAYER_357:		damage = gSkillData.plrDmg357; break;
	case BULLET_PLAYER_BUCKSHOT:	damage = gSkillData.plrDmgBuckshot; break;
	case BULLET_PLAYER_CROWBAR:	damage = gSkillData.plrDmgCrowbar; break;
	// PSG1 and Zat'arc deal fixed damage (see FireBulletsPlayer: the PSG1 call
	// passes iDamage 40, the Zat'arc switch case 80).
	case SGTC_BULLET_PSG1:		damage = 40.0f; break;
	case SGTC_BULLET_ZATARC:	damage = 80.0f; break;
	default:			damage = 0.0f; break;
	}

	int value = (int)damage;

	if( value < 0 )
		value = 0;
	if( value > 255 )
		value = 255;

	return value;
}

static void SGTC_Sniper_SendHit( CBasePlayer *pPlayer, int flags, int damage )
{
	if( !gmsgSgtcHit || !pPlayer || !pPlayer->pev )
		return;

	// Bots have no HUD to flash.
	if( pPlayer->pev->flags & FL_FAKECLIENT )
		return;

	if( damage < 0 )
		damage = 0;
	if( damage > 255 )
		damage = 255;

	MESSAGE_BEGIN( MSG_ONE, gmsgSgtcHit, NULL, pPlayer->pev );
		WRITE_BYTE( flags & 0xFF );
		WRITE_BYTE( damage );
	MESSAGE_END();
}

void SGTC_Sniper_NotifyBulletHit( CBasePlayer *pAttacker, CBaseEntity *pHit, TraceResult *tr, int iBulletType )
{
	if( !pAttacker || !pHit )
		return;

	if( pAttacker->pev == pHit->pev )
		return;

	// Only human shooters get HUD feedback.
	if( pAttacker->pev->flags & FL_FAKECLIENT )
		return;

	// The caller passes whatever the trace hit, including the world entity.
	if( !pHit->IsPlayer() )
		return;

	CBasePlayer *pVictim = (CBasePlayer *)pHit;

	if( pVictim->pev->deadflag != DEAD_NO || pVictim->pev->health <= 0.0f )
		return;

	int flags = 1;	// hit

	if( tr && tr->iHitgroup == HITGROUP_HEAD )
		flags |= 2;	// headshot

	SGTC_Sniper_SendHit( pAttacker, flags, SGTC_Sniper_DamageEstimate( iBulletType ) );
}

void SGTC_Sniper_NotifyKill( CBasePlayer *pKiller, CBasePlayer *pVictim )
{
	if( !pKiller )
		return;

	SGTC_Sniper_SendHit( pKiller, 4, 0 );	// kill

	if( !pVictim || pVictim == pKiller )
		return;

	if( sgtc_cvar_sniper_deathpunch.value <= 0.0f )
		return;

	// Only kills that actually came from a sniper shot inside the window.
	if( pKiller->m_flSgtcLastSniperFire <= 0.0f )
		return;
	if( gpGlobals->time - pKiller->m_flSgtcLastSniperFire > SGTC_SNIPER_FIRE_WINDOW )
		return;

	// Shove the corpse along the bullet line and give it a death limp.
	Vector victimOrigin( pVictim->pev->origin );
	Vector killerOrigin( pKiller->pev->origin );
	Vector velocity( pVictim->pev->velocity );
	Vector away = victimOrigin - killerOrigin;

	away.z = 0.0f;

	if( away.Length() > 1.0f )
	{
		velocity = velocity + away.Normalize() * SGTC_DEATHPUNCH_PUSH;
	}
	else
	{
		Vector vecAngles( pKiller->pev->v_angle );
		float rgflAngles[3];
		float rgflForward[3];
		Vector vecDir;

		vecAngles.x = 0.0f;
		vecAngles.z = 0.0f;

		rgflAngles[0] = vecAngles.x;
		rgflAngles[1] = vecAngles.y;
		rgflAngles[2] = vecAngles.z;

		g_engfuncs.pfnAngleVectors( rgflAngles, rgflForward, NULL, NULL );
		vecDir = Vector( rgflForward );
		vecDir.z = 0.0f;
		velocity = velocity + vecDir.Normalize() * SGTC_DEATHPUNCH_PUSH;
	}

	velocity.z += SGTC_DEATHPUNCH_LIFT;
	pVictim->pev->velocity = velocity;

	Vector punchangle( pVictim->pev->punchangle );

	punchangle.x += SGTC_DEATHPUNCH_PITCH;
	punchangle.y += SGTC_DEATHPUNCH_YAW;
	pVictim->pev->punchangle = punchangle;
}

void SGTC_Sniper_BotCompensate( edict_t *pEdict, const struct usercmd_s *cmd )
{
	if( !pEdict || !cmd )
		return;

	if( !( pEdict->v.flags & FL_FAKECLIENT ) )
		return;

	// Compensate only on frames where the bot is actually shooting; doing it
	// every frame would fight BotPointGun's aim loop and leave the bot aiming
	// off target instead of on it.
	if( !( cmd->buttons & IN_ATTACK ) )
		return;

	CBasePlayer *pPlayer = (CBasePlayer *)CBaseEntity::Instance( pEdict );

	if( !pPlayer )
		return;

	if( !SGTC_Sniper_HoldsScopedWeapon( pPlayer ) )
		return;

	float strength = SGTC_Sniper_Strength();

	if( strength <= 0.0f )
		return;

	SgtcSwayInputs in;
	SGTC_Sniper_BuildInputs( pPlayer, strength, in );

	float pitch = 0.0f;
	float yaw = 0.0f;

	SGTC_Sway_Eval( SGTC_Sniper_Seed( pPlayer ), gpGlobals->time, in, &pitch, &yaw );

	// The weapon's PrimaryAttack evaluates the same offset at the same
	// gpGlobals->time a moment later and adds it to the aim. Removing it here
	// (from both the entity angles and the command the engine will execute)
	// makes the two cancel exactly, so bots keep their pre-sway accuracy.
	pEdict->v.v_angle.x = SGTC_Sniper_WrapPitch( pEdict->v.v_angle.x - pitch );
	pEdict->v.v_angle.y = SGTC_Sniper_WrapYaw( pEdict->v.v_angle.y - yaw );

	struct usercmd_s *pCmd = const_cast<struct usercmd_s *>( cmd );
	pCmd->viewangles[0] = SGTC_Sniper_WrapPitch( pCmd->viewangles[0] - pitch );
	pCmd->viewangles[1] = SGTC_Sniper_WrapYaw( pCmd->viewangles[1] - yaw );
}
