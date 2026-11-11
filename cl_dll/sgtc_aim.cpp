// sgtc_aim.cpp - StargateTC client scope sway, hold-breath stamina and the
// IN_ALT1 signal that mirrors holding breath into the usercmd.
//
// The sway itself lives in game_shared/sgtc_sniper_sway.h so the server can
// reproduce exactly the same offsets for bot compensation.  This file owns
// the client-side state machine around it:
//   * scope detection (sniper/zat weapon + zoomed FOV),
//   * the input bundle fed to the evaluator (movement, air, crouch, hurt, fire),
//   * hold-breath stamina/gasp, with the eased applied-offset bookkeeping,
//   * +/-sgtc_holdbreath commands and the +speed fallback.
//
// The applied offset is always removed from the view angles before a fresh one
// is evaluated, so the player's own aim is never integrated into the sway.

#include <math.h>

#include "hud.h"
#include "cl_util.h"
#include "ammo.h"
#include "cvardef.h"
#include "usercmd.h"
#include "kbutton.h"
#include "const.h"
#include "in_defs.h"	// PITCH / YAW
#include "pm_defs.h"	// playermove_t (predicted movement state)

#include "sgtc_aim.h"
#include "sgtc_sniper_sway.h"
#include "sgtc_weapons_client.h"

// input.cpp owns the key down states; entity.cpp owns g_iAlive.
extern int g_iAlive;
extern kbutton_t in_speed;
extern kbutton_t in_duck;
extern kbutton_t in_forward;
extern kbutton_t in_back;
extern kbutton_t in_moveleft;
extern kbutton_t in_moveright;

// pm_shared/pm_shared.c: the persistent movement state handed over by
// HUD_PlayerMoveInit, i.e. the client's predicted player state.  That
// translation unit is plain C, so the symbol has C linkage.
extern "C"
{
	extern playermove_t *pmove;
}

// Tuning (documented in sgtc_aim.h contract).
#define SGTC_AIM_FIRE_WINDOW		1.2f	// seconds of "just fired" sway
#define SGTC_AIM_HURT_WINDOW		1.5f	// seconds of "just got shot" sway
#define SGTC_AIM_FADE_OUT			0.15f	// offset ease-out when unscoping
#define SGTC_AIM_BREATH_UP			6.0f	// breath units per second while held
#define SGTC_AIM_BREATH_DOWN		4.0f	// breath units per second released
#define SGTC_AIM_REFILL_SECONDS		2.5f	// full lung refill time
#define SGTC_AIM_GASP_SECONDS		1.2f	// "out of breath" wobble length
#define SGTC_AIM_GASP_MULT			1.5f	// sway multiplier while gasping
#define SGTC_AIM_SPEED_REF			320.0f	// move input reference speed

static cvar_t *s_pCvarSway = NULL;
static cvar_t *s_pCvarDebug = NULL;
static cvar_t *s_pCvarHoldSeconds = NULL;
static cvar_t *s_pCvarHoldCooldown = NULL;

static bool s_bInit = false;

// Applied offset (degrees) and the frame it belongs to.
static float s_flAppliedPitch = 0.0f;
static float s_flAppliedYaw = 0.0f;

static float s_flFireTime = -1000.0f;
static float s_flHurtTime = -1000.0f;
static int s_iLastHealth = 100;
static bool s_bHealthKnown = false;

// Hold-breath state.
static bool s_bHoldKey = false;		// +sgtc_holdbreath pressed
static bool s_bHoldActive = false;	// breath currently being held this frame
static bool s_bNeedsRepress = false;	// lung emptied: release before holding again
static int s_iBreathPhase = 0;		// 0 ready, 1 cooldown/gasp, 2 refill
static float s_flStamina = 0.0f;	// seconds of lung left
static float s_flBreath = 0.0f;		// 0..1 eased breath
static float s_flCooldown = 0.0f;
static float s_flGasp = 0.0f;

static float SGTC_Aim_Abs( float x )
{
	return ( x < 0.0f ) ? -x : x;
}

static float SGTC_Aim_HoldSeconds( void )
{
	if( s_pCvarHoldSeconds && s_pCvarHoldSeconds->value > 0.1f )
		return s_pCvarHoldSeconds->value;
	return 3.0f;
}

static float SGTC_Aim_CooldownSeconds( void )
{
	if( s_pCvarHoldCooldown && s_pCvarHoldCooldown->value > 0.0f )
		return s_pCvarHoldCooldown->value;
	return 2.0f;
}

// Wall-clock seconds from the engine (same clock CL_CreateMove runs on).
static float SGTC_Aim_Now( void )
{
	return gEngfuncs.GetClientTime();
}

static void SGTC_Aim_HoldDown( void )
{
	s_bHoldKey = true;
}

static void SGTC_Aim_HoldUp( void )
{
	s_bHoldKey = false;
}

void SGTC_Aim_Init( void )
{
	if( s_bInit )
		return;
	s_bInit = true;

	// The server replicates sgtc_sniper_sway (FCVAR_SERVER); creating the
	// client cvar with the same name lets the replicated value land here.
	s_pCvarSway = CVAR_CREATE( "sgtc_sniper_sway", "1", 0 );
	s_pCvarDebug = CVAR_CREATE( "sgtc_sniper_sway_debug", "0", 0 );
	s_pCvarHoldSeconds = CVAR_CREATE( "sgtc_holdbreath_seconds", "3.0", FCVAR_ARCHIVE );
	s_pCvarHoldCooldown = CVAR_CREATE( "sgtc_holdbreath_cooldown", "2.0", FCVAR_ARCHIVE );

	gEngfuncs.pfnAddCommand( "+sgtc_holdbreath", SGTC_Aim_HoldDown );
	gEngfuncs.pfnAddCommand( "-sgtc_holdbreath", SGTC_Aim_HoldUp );

	s_flStamina = SGTC_Aim_HoldSeconds();
}

// The module is normally initialised from HUD_Init, but CL_CreateMove runs
// before the HUD exists on a few paths (demo playback), so stay defensive.
static void SGTC_Aim_EnsureInit( void )
{
	if( !s_bInit )
		SGTC_Aim_Init();
}

static bool SGTC_Aim_WeaponHasScope( void )
{
	WEAPON *pWeapon = gHUD.m_Ammo.GetCurrentWeapon();

	if( !pWeapon )
		return false;

	switch( pWeapon->iId )
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

bool SGTC_Aim_IsScoped( void )
{
	SGTC_Aim_EnsureInit();

	if( !SGTC_Aim_WeaponHasScope() )
		return false;

	// m_inScope is CHud's own "SetFOV zoomed" latch, fed by the server's
	// SetFOV message; it is already false when the FOV returns to default.
	return gHUD.m_inScope;
}

float SGTC_Aim_Stamina01( void )
{
	float full = SGTC_Aim_HoldSeconds();
	float stamina = s_flStamina;

	if( full <= 0.0f )
		return 0.0f;

	if( stamina < 0.0f )
		stamina = 0.0f;
	if( stamina > full )
		stamina = full;

	return stamina / full;
}

bool SGTC_Aim_HoldingBreath( void )
{
	return s_bHoldActive && s_flBreath > 0.25f;
}

void SGTC_Aim_GetOffset( float *pitchDeg, float *yawDeg )
{
	if( pitchDeg )
		*pitchDeg = s_flAppliedPitch;
	if( yawDeg )
		*yawDeg = s_flAppliedYaw;
}

void SGTC_Aim_OnLocalFire( void )
{
	s_flFireTime = SGTC_Aim_Now();
}

// Returns true when the player is asking for hold-breath this frame: either
// the dedicated +sgtc_holdbreath key or +speed (walk) while scoped.
static bool SGTC_Aim_WantsHold( bool scoped )
{
	if( s_bHoldKey )
		return true;
	if( scoped && ( in_speed.state & 1 ) )
		return true;
	return false;
}

// Advances stamina/breath/gasp.  `now` is only used to detect a fresh frame.
static void SGTC_Aim_UpdateBreath( float frametime, bool scoped )
{
	bool wantHold = SGTC_Aim_WantsHold( scoped );
	float full = SGTC_Aim_HoldSeconds();
	float refillRate = full / SGTC_AIM_REFILL_SECONDS;

	if( !wantHold )
		s_bNeedsRepress = false;

	s_bHoldActive = false;

	if( s_flStamina > full )
		s_flStamina = full;

	switch( s_iBreathPhase )
	{
	case 1:	// cooldown: lungs empty, gasping
		s_flBreath -= SGTC_AIM_BREATH_DOWN * frametime;
		if( s_flBreath < 0.0f )
			s_flBreath = 0.0f;

		if( s_flGasp > 0.0f )
		{
			s_flGasp -= frametime;
			if( s_flGasp < 0.0f )
				s_flGasp = 0.0f;
		}

		s_flCooldown -= frametime;
		if( s_flCooldown <= 0.0f )
		{
			s_flCooldown = 0.0f;
			s_iBreathPhase = 2;
		}
		break;

	case 2:	// refill: air coming back, still no hold until the lung is full
		s_flStamina += refillRate * frametime;
		if( s_flStamina >= full )
		{
			s_flStamina = full;
			s_iBreathPhase = 0;
		}

		s_flBreath -= SGTC_AIM_BREATH_DOWN * frametime;
		if( s_flBreath < 0.0f )
			s_flBreath = 0.0f;
		break;

	default:	// ready
		if( wantHold && !s_bNeedsRepress && s_flStamina > 0.0f )
		{
			s_bHoldActive = true;
			s_flBreath += SGTC_AIM_BREATH_UP * frametime;
			if( s_flBreath > 1.0f )
				s_flBreath = 1.0f;

			s_flStamina -= frametime;
			if( s_flStamina <= 0.0f )
			{
				s_flStamina = 0.0f;
				s_flBreath = 0.0f;
				s_iBreathPhase = 1;
				s_flCooldown = SGTC_Aim_CooldownSeconds();
				s_flGasp = SGTC_AIM_GASP_SECONDS;
				s_bNeedsRepress = true;
			}
		}
		else
		{
			s_flBreath -= SGTC_AIM_BREATH_DOWN * frametime;
			if( s_flBreath < 0.0f )
				s_flBreath = 0.0f;

			if( s_flStamina < full )
			{
				s_flStamina += refillRate * frametime;
				if( s_flStamina > full )
					s_flStamina = full;
			}
		}
		break;
	}
}

static void SGTC_Aim_TrackHealth( float now )
{
	int iHealth = gHUD.m_Health.m_iHealth;

	if( !s_bHealthKnown )
	{
		s_bHealthKnown = true;
		s_iLastHealth = iHealth;
		return;
	}

	if( iHealth < s_iLastHealth )
		s_flHurtTime = now;

	s_iLastHealth = iHealth;
}

static unsigned int SGTC_Aim_Seed( void )
{
	struct cl_entity_s *pLocal = gEngfuncs.GetLocalPlayer();
	unsigned int index = pLocal ? (unsigned int)pLocal->index : 0u;

	// No shared random seed is exposed to the client DLL, so the index is the
	// stable per-player input; the golden-ratio multiply gives it good bits.
	return index * 2654435761u;
}

static void SGTC_Aim_BuildInputs( SgtcSwayInputs &in, float now )
{
	float speed = 0.0f;
	bool bOnGround = true;
	bool bDucking = false;

	if( pmove )
	{
		// Predicted movement state: velocity, ground contact and duck state
		// exactly as the physics code sees them.
		const float *velocity = pmove->velocity;

		speed = (float)sqrt( (double)( velocity[0] * velocity[0] + velocity[1] * velocity[1] ) );
		bOnGround = ( pmove->flags & FL_ONGROUND ) ? true : false;
		bDucking = ( pmove->flags & FL_DUCKING ) || pmove->bInDuck ? true : false;
	}
	else
	{
		struct cl_entity_s *pLocal = gEngfuncs.GetLocalPlayer();

		if( pLocal )
		{
			const float *velocity = pLocal->curstate.velocity;

			speed = (float)sqrt( (double)( velocity[0] * velocity[0] + velocity[1] * velocity[1] ) );
			bOnGround = ( pLocal->curstate.velocity[2] > -60.0f );
		}
	}

	if( speed <= 1.0f )
	{
		// First step of a movement key: velocity has not caught up yet.
		bool bKeyMove = ( in_forward.state & 1 ) || ( in_back.state & 1 )
			|| ( in_moveleft.state & 1 ) || ( in_moveright.state & 1 );
		speed = bKeyMove ? SGTC_AIM_SPEED_REF : 0.0f;
	}

	float move = speed / SGTC_AIM_SPEED_REF;
	if( move > 1.0f )
		move = 1.0f;

	in.move = move;
	in.airborne = bOnGround ? 0.0f : 1.0f;
	in.crouch = ( bDucking || ( in_duck.state & 1 ) ) ? 1.0f : 0.0f;
	in.hurt = ( now - s_flHurtTime < SGTC_AIM_HURT_WINDOW ) ? 1.0f : 0.0f;
	in.fire = ( now - s_flFireTime < SGTC_AIM_FIRE_WINDOW ) ? 1.0f : 0.0f;
	in.breath = s_flBreath;
	in.strength = s_pCvarSway ? s_pCvarSway->value : 0.0f;

	if( in.strength < 0.0f )
		in.strength = 0.0f;

	if( s_flGasp > 0.0f )
		in.strength *= SGTC_AIM_GASP_MULT;
}

static float SGTC_Aim_WrapPitch( float pitch )
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

static float SGTC_Aim_WrapYaw( float yaw )
{
	while( yaw > 180.0f )
		yaw -= 360.0f;
	while( yaw < -180.0f )
		yaw += 360.0f;
	return yaw;
}

void SGTC_Aim_AdjustViewAngles( float frametime, float *viewangles )
{
	if( !viewangles )
		return;

	SGTC_Aim_EnsureInit();

	if( !( frametime > 0.0f ) )
		frametime = 0.0f;

	float now = SGTC_Aim_Now();
	bool scoped = SGTC_Aim_IsScoped();

	SGTC_Aim_TrackHealth( now );
	SGTC_Aim_UpdateBreath( frametime, scoped );

	// Remove the offset this module added last frame first: from here on the
	// angles are exactly what the player asked for.
	viewangles[PITCH] -= s_flAppliedPitch;
	viewangles[YAW] -= s_flAppliedYaw;

	bool bEligible = scoped && g_iAlive && g_iUser1 == 0 && gEngfuncs.GetLocalPlayer() != NULL;

	SgtcSwayInputs in;
	SGTC_Aim_BuildInputs( in, now );

	if( bEligible && in.strength > 0.0f )
	{
		float pitch = 0.0f;
		float yaw = 0.0f;

		SGTC_Sway_Eval( SGTC_Aim_Seed(), now, in, &pitch, &yaw );

		s_flAppliedPitch = pitch;
		s_flAppliedYaw = yaw;
	}
	else
	{
		// Ease the drift back to the true aim instead of snapping.
		float k = ( SGTC_AIM_FADE_OUT > 0.0f ) ? ( frametime / SGTC_AIM_FADE_OUT ) : 1.0f;
		if( k > 1.0f )
			k = 1.0f;
		if( k < 0.0f )
			k = 0.0f;

		s_flAppliedPitch -= s_flAppliedPitch * k;
		s_flAppliedYaw -= s_flAppliedYaw * k;

		if( SGTC_Aim_Abs( s_flAppliedPitch ) < 0.001f )
			s_flAppliedPitch = 0.0f;
		if( SGTC_Aim_Abs( s_flAppliedYaw ) < 0.001f )
			s_flAppliedYaw = 0.0f;
	}

	viewangles[PITCH] += s_flAppliedPitch;
	viewangles[YAW] += s_flAppliedYaw;

	viewangles[PITCH] = SGTC_Aim_WrapPitch( viewangles[PITCH] );
	viewangles[YAW] = SGTC_Aim_WrapYaw( viewangles[YAW] );

	if( s_pCvarDebug && s_pCvarDebug->value > 0.0f )
	{
		static float s_flDebugNext = 0.0f;

		if( now >= s_flDebugNext )
		{
			s_flDebugNext = now + 0.25f;
			gEngfuncs.Con_Printf( "sgtc_aim: pitch %.3f yaw %.3f breath %.2f stam %.2f\n",
				s_flAppliedPitch, s_flAppliedYaw, s_flBreath, SGTC_Aim_Stamina01() );
		}
	}
}

void SGTC_Aim_AddButtons( struct usercmd_s *cmd )
{
	if( !cmd )
		return;

	SGTC_Aim_EnsureInit();

	// IN_ALT1 travels with the command so the server/other players can tell
	// that this shot was taken with a held breath.
	if( g_iAlive && s_bHoldActive && s_flBreath > 0.1f && SGTC_Aim_IsScoped() )
		cmd->buttons |= IN_ALT1;
}
