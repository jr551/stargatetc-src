// sgtc_protocol.cpp — StargateTC client protocol registrations (NEW file).
//
// Source of truth: inventory_client_dll.json in the stargatetc-rea package repo
// (RigidFirefly RE of stargatetc/cl_dlls/client.dll) cross-checked against
// server string table (stargatetc/dlls/hl.dll) and stargatetc/events/*.sc,
// sprites/hud.txt + sprites/weapon_*.txt on disk.
//
// Design (follows hud_msg.cpp / ev_hldm.cpp / hl/hl_events.cpp patterns):
//   - Every SGTC user message gets its own MsgFunc_SGTC_* drain stub.
//     Payload formats are not recoverable client-side, so each handler only
//     drains the buffer (BEGIN_READ/READ_* can't know field layout) — i.e. it
//     returns 1 and ignores content, exactly like stock __MsgFunc_ValClass etc.
//     Server-side peers own the real layouts; wire real fields later by reading
//     dlls MULTIUSE/message sending code.
//   - Events: hook every events/*.sc name found in BOTH client.dll and hl.dll
//     string tables AND present in the server PRECACHE_EVENT list. Stock-file
//     names (glock1/2, mp5/mp52, rpg, train, tripfire) are already hooked by
//     stock Game_HookEvents and are NOT re-hooked here. Unknown script bodies
//     get a shared melee/gunshot fallback implemented with EV_HLDM helpers
//     (tracer + texture sound + decal), mirroring EV_Crowbar/EV_FireMP5 shape.
//   - Sprites/sounds: client only precaches what it draws/plays. Server-side
//     precache lists are authoritative for the 670 files missing from the mod
//     dir; here we SPR_Load only the 28 client-referenced sprites plus the
//     SGTC HUD-layout names from hud.txt so a missing-file printf names the
//     culprit instead of silently drawing nothing.

#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "entity_state.h"
#include "cl_entity.h"
#include "entity_types.h"
#include "usercmd.h"
#include "pm_defs.h"
#include "parsemsg.h"
#include "event_api.h"
#include "event_args.h"
#include "eventscripts.h"
#include "ev_hldm.h"
#include "r_efx.h"
#include "sgtc_protocol.h"
#include "sgtc_hud.h"
#include "sgtc_env.h"
#include "sgtc_music.h"
#include "sgtc_options.h"
#include "sgtc_vgui.h"
#include "sgtc_aim.h"
#include "sgtc_sniper_fx.h"
#include "in_defs.h"	// YAW for EV_EjectBrass

#include <string.h>

// ---------------------------------------------------------------------------
// 1. Custom user messages.
//
// Authoritative registry: LinkUserMessages in ref/stc_i386.so (2.0 Linux
// server, unstripped — disassembled, 61 REG_USER_MSG calls in order):
//   Blip Progress Gtickets Ttickets TicketMode HudInfoMode Concuss HasKey
//   Stamina SubModel Armor Spectator AddPortrait AboutMe TogglePor Reset
//   NewRnd SelAmmo CurWeapon Flashlight FlashBat Health Damage Train HudText
//   SayText TextMsg WeaponList ResetHUD InitHUD GameTitle DeathMsg ScoreInfo
//   TeamInfo TeamScore GameMode MOTD ServerName AmmoPickup WeapPickup
//   ItemPickup HideWeapon SetFOV ShowMenu ScreenShake ScreenFade AmmoX
//   TeamNames StatusText StatusValue VGUIMenu HideVGUI SetRndTim ZatTouch
//   EnvFull EnvRain EnvFount NewLens AddRing ChgPartSt Radio
// Client-side registry (verified vs ref client.dll 2026-10: 74
// pfnHookUserMsg sites — GoldSrc stock + TFC leftovers + SGTC):
//   env/particle (7): ClearEnv EnvFull EnvRain EnvFount NewLens AddRing
//     ChgPartSt — all real handlers in ref (CClientEnv HUD element):
//     ClearEnv drains only; EnvFull spawns env entry (19 coords, 9 bytes
//     incl 4 RGBA/255, 1 short, 1 id byte); EnvRain (6 coords, byte, short);
//     EnvFount (layout table in sgtc_env.h); NewLens (3 coords,
//     4 RGBA bytes); AddRing (4 coords + string); ChgPartSt (id byte +
//     state byte → env state).
//   SGTC HUD (19): AddPortrait Reset AboutMe TogglePor SetRndTim NewRnd
//     ZatTouch Radio Armor Stamina SubModel HasKey HideHud HudInfoMode
//     TicketMode Gtickets Ttickets Progress Blip — all real parsers in
//     ref (see sgtc_hud.cpp); CHudSgtc keeps wire parity for SGTC HUD state.
//   NOT hooked by ref (we do not hook either): Custom (present only as a
//     CHudMessage channel-name table entry, never passed to
//     pfnHookUserMsg), SecAmmoVal/SecAmmoIcon (TFC leftovers still
//     hooked by ref — covered by stock hud elements elsewhere).
// Server-only (registered, NO client string — engine-handled or unhandled,
// nothing to hook): SelAmmo ScreenShake ScreenFade Fade Shake SetCurWeap
// SetHudRoundTime.
// Stock reused unchanged: CurWeapon WeaponList AmmoPickup WeapPickup
// ItemPickup HideWeapon AmmoX SecAmmoVal SecAmmoIcon Flashlight FlashBat
// Geiger Health Damage Train HudText SayText TextMsg DeathMsg Logo
// ResetHUD InitHUD GameTitle DeathMsg ScoreInfo TeamInfo TeamScore
// GameMode MOTD ServerName SetFOV ShowMenu ViewMode StatusText StatusValue
// StatusIcon VGUIMenu HideVGUI TeamNames Spectator Concuss VoiceMask
// ReqState ValClass Feign Detpack BuildSt RandomPC AllowSpec.
// ---------------------------------------------------------------------------

#define SGTC_MSG_LIST \
	X( ClearEnv ) \
	X( EnvFull ) \
	X( EnvRain ) \
	X( EnvFount ) \
	X( NewLens ) \
	X( AddRing ) \
	X( ChgPartSt ) \
	X( AddPortrait ) \
	X( Reset ) \
	X( AboutMe ) \
	X( TogglePor ) \
	X( SetRndTim ) \
	X( NewRnd ) \
	X( ZatTouch ) \
	X( Radio ) \
	X( Armor ) \
	X( Stamina ) \
	X( SubModel ) \
	X( HasKey ) \
	X( HideHud ) \
	X( HudInfoMode ) \
	X( TicketMode ) \
	X( Gtickets ) \
	X( Ttickets ) \
	X( Progress ) \
	X( Blip )

#define X( name ) \
	static int __MsgFunc_##name( const char *pszName, int iSize, void *pbuf ) \
	{ \
		return g_SgtcHud.MsgFunc_##name( pszName, iSize, pbuf ); \
	}
SGTC_MSG_LIST
#undef X

static int __MsgFunc_Concuss( const char *pszName, int iSize, void *pbuf )
{
	return g_SgtcHud.MsgFunc_Concuss( pszName, iSize, pbuf );
}

void SGTC_ClientProtocol_InitMessages( void )
{
#define X( name ) gEngfuncs.pfnHookUserMsg( #name, __MsgFunc_##name );
SGTC_MSG_LIST
#undef X
	// ref client Concuss hook @0x10057aff — overrides stock after gHUD.Init().
	gEngfuncs.pfnHookUserMsg( "Concuss", __MsgFunc_Concuss );

	// SGTC wire addition (server SGTC_Sniper_RegisterMessages): hit feedback.
	gEngfuncs.pfnHookUserMsg( "SgtcHit", SGTC_SniperFx_MsgFunc_SgtcHit );
}

void SGTC_ClientHUD_Init( void )
{
	SgtcVgui_AttachToRoot();
	SgtcMusic_Init();	// archived sgtc_music / sgtc_musicvolume cvars
	SgtcOptions_Init();	// sgtc_options command
	g_SgtcHud.Init();
	g_ClientEnv.Init();
	SGTC_ClientProtocol_InitMessages();
	SGTC_ClientProtocol_HookEvents();

	// Sniper/aim modules: registered after the message hooks so the SgtcHit
	// handler already exists when the first message can arrive.
	SGTC_Aim_Init();
	SGTC_SniperFx_Init();
}

void SGTC_ClientHUD_VidInit( void )
{
	SgtcVgui_NewLevel();	// clear the SGTC menu-dismiss mark on new level
	SgtcMusic_LevelInit();	// re-resolve the map / re-apply music on this level
	SGTC_ClientProtocol_VidInit();
	g_SgtcHud.VidInit();
	g_ClientEnv.VidInit();	// not on CHud's VidInit list: clears the env particle buffer
}

// Called from HUD_Frame (every host frame). The music manager needs a frame
// hook because HUD_VidInit runs before the engine has parsed the map name; the
// options panel uses it to close itself when the console is opened.
void SGTC_ClientHUD_Frame( void )
{
	SgtcMusic_Frame();
	SgtcOptions_Frame();
}

// Counts the pfnHookUserMsg calls SGTC_ClientProtocol_InitMessages makes: the
// SGTC_MSG_LIST entries, the Concuss override and the SgtcHit hit-feedback
// message. Derived from the list so it cannot drift (it was a hard-coded 26
// against the 27 in sgtc_protocol.h).
int SGTC_CustomMessageCount( void )
{
	int n = 0;
#define X( name ) n++;
SGTC_MSG_LIST
#undef X
	return n + 2;	// + Concuss + SgtcHit
}

// ---------------------------------------------------------------------------
// 2. Event scripts (events/*.sc).
//
// Authoritative server list: PRECACHE_EVENT refs in ref/stc_i386.so (33):
//   batsup batsup2 beam_fire beam_stop biglance coujaf couteau eagle invis
//   lance lance2 larve m16 main main2 mainc mains mp5 p90 p90l psg1 reetou
//   reetou2 shield spas spas2 tac train tripfire usas zata zatarc zatb (33).
// Result (31 SGTC hooks below):
//   On disk + both binaries + server-precached: 30 scripts — batsup batsup2
//     beam_fire beam_stop biglance coujaf couteau eagle invis lance lance2
//     larve m16 main main2 mainc mains p90 p90l psg1 reetou reetou2 shield
//     spas spas2 tac usas zata zatarc zatb (30)
//   On disk + client, NOT server-precached: zat (1) — stub kept so the
//     client does not go silent if the server ever precaches it.
//   On disk, in NEITHER binary: couteau_ancien sniper — NOT hooked (dead
//     files; server never precaches them).
//   Server-precached but NOT on disk (stock HL, already hooked by stock
//     Game_HookEvents — NOT re-hooked): mp5 train tripfire.
//   Client-only legacy (stock HL, already hooked by stock — NOT re-hooked):
//     glock1 glock2 mp52 rpg tacfire. spas2→spas2.sc but server precaches
//     events/spas2.sc too (match); usas→events/usas.sc (match).

extern "C"
{
#define SGTC_EV_LIST \
	X( EV_SGTC_Batsup, "events/batsup.sc" ) \
	X( EV_SGTC_Batsup2, "events/batsup2.sc" ) \
	X( EV_SGTC_BeamFire, "events/beam_fire.sc" ) \
	X( EV_SGTC_BeamStop, "events/beam_stop.sc" ) \
	X( EV_SGTC_BigLance, "events/biglance.sc" ) \
	X( EV_SGTC_Coujaf, "events/coujaf.sc" ) \
	X( EV_SGTC_Couteau, "events/couteau.sc" ) \
	X( EV_SGTC_Eagle, "events/eagle.sc" ) \
	X( EV_SGTC_Invis, "events/invis.sc" ) \
	X( EV_SGTC_Lance, "events/lance.sc" ) \
	X( EV_SGTC_Lance2, "events/lance2.sc" ) \
	X( EV_SGTC_Larve, "events/larve.sc" ) \
	X( EV_SGTC_M16, "events/m16.sc" ) \
	X( EV_SGTC_Main, "events/main.sc" ) \
	X( EV_SGTC_Main2, "events/main2.sc" ) \
	X( EV_SGTC_MainC, "events/mainc.sc" ) \
	X( EV_SGTC_MainS, "events/mains.sc" ) \
	X( EV_SGTC_P90, "events/p90.sc" ) \
	X( EV_SGTC_P90L, "events/p90l.sc" ) \
	X( EV_SGTC_PSG1, "events/psg1.sc" ) \
	X( EV_SGTC_Reetou, "events/reetou.sc" ) \
	X( EV_SGTC_Reetou2, "events/reetou2.sc" ) \
	X( EV_SGTC_Shield, "events/shield.sc" ) \
	X( EV_SGTC_Spas, "events/spas.sc" ) \
	X( EV_SGTC_Spas2, "events/spas2.sc" ) \
	X( EV_SGTC_Tac, "events/tac.sc" ) \
	X( EV_SGTC_Usas, "events/usas.sc" ) \
	X( EV_SGTC_ZatA, "events/zata.sc" ) \
	X( EV_SGTC_ZatArc, "events/zatarc.sc" ) \
	X( EV_SGTC_ZatB, "events/zatb.sc" ) \
	X( EV_SGTC_Zat, "events/zat.sc" )

#define X( fn, script ) void fn( struct event_args_s *args );
SGTC_EV_LIST
#undef X
}

// ---------------------------------------------------------------------------
// SGTC event handlers — ported from ref client.dll (sha256 9ec27e22…).
// Each handler cites its ref address; dumps at .rea/dumps/events/.
// ---------------------------------------------------------------------------

void V_PunchAxis( int axis, float punch );	// ref FUN_1007f518

extern cvar_t *cl_lw;	// ref DAT_100ebce4 (beam-fire gate)

static int g_sgtcTracerCount[MAX_PLAYERS + 1];	// ref DAT_100ba5b8 (+idx indexing)

// args->entindex comes from the event: only players 1..MAX_PLAYERS own a slot,
// anything else (world, a non-player entity) gets a throwaway counter.
static int *SGTC_TracerCountPtr( int idx )
{
	if( idx >= 1 && idx <= MAX_PLAYERS )
		return &g_sgtcTracerCount[idx];
	static int s_sgtcTracerCountDiscard;
	return &s_sgtcTracerCountDiscard;
}
static BEAM *g_sgtcBeam;		// ref DAT_100ba5b8 reused as beam handle
static int g_sgtcP90Silencer;		// ref DAT_100ba67c
static int g_sgtcBeamStop = 1;		// ref _DAT_100b089c

// Shared fallback for EV_SGTC_Zat only (zat.sc is never hooked by the ref).
static void SGTC_EventFallback( struct event_args_s *args, int playSound )
{
	int idx = args->entindex;
	if( EV_IsLocal( idx ) )
		EV_MuzzleFlash();

	if( playSound && ( args->iparam1 & 1 ) )
	{
		float origin[3], angles[3], velocity[3], shellVelocity[3], shellOrigin[3];
		float forward[3], right[3], up[3];
		EV_GetGunPosition( args, origin, angles );
		EV_GetDefaultShellInfo( args, origin, velocity, shellVelocity, shellOrigin,
			forward, right, up, 20.0f, -12.0f, 4.0f );
		EV_EjectBrass( shellOrigin, shellVelocity, angles[YAW], 10, TE_BOUNCE_SHELL );
	}
	(void)idx;
}

extern "C"
{

// ref 0x100079eb — usas (spastir shell shotgun)
void EV_SGTC_Usas( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin, angles, velocity, forward, right, up;
	vec3_t vecSrc, vecAiming, ShellVelocity, ShellOrigin;
	int shell;

	VectorCopy( args->origin, origin );
	VectorCopy( args->angles, angles );
	VectorCopy( args->velocity, velocity );

	AngleVectors( angles, forward, right, up );

	shell = gEngfuncs.pEventAPI->EV_FindModelIndex( "models/sshell.mdl" );

	if( EV_IsLocal( idx ) )
	{
		EV_MuzzleFlash();
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 5, 2 );
		V_PunchAxis( 0, -10.0f );	// 0xc1200000
	}

	EV_GetDefaultShellInfo( args, origin, velocity, ShellVelocity, ShellOrigin,
		forward, right, up, 32.0f, -12.0f, 6.0f );

	EV_EjectBrass( ShellOrigin, ShellVelocity, angles[YAW], shell, TE_BOUNCE_SHOTSHELL );

	gEngfuncs.pEventAPI->EV_PlaySound( idx, origin, CHAN_WEAPON, "weapons/spastir.wav",
		gEngfuncs.pfnRandomFloat( 0.95f, 1.0f ),	// 0x3f733333,0x3f800000
		0.8f,					// 0x3f4ccccd = ATTN_NORM
		0, 93 + gEngfuncs.pfnRandomLong( 0, 0x1f ) );	// 0x5d+R(0,31)

	EV_GetGunPosition( args, vecSrc, origin );

	VectorCopy( forward, vecAiming );

	if( gEngfuncs.GetMaxClients() < 2 )	// single player
	{
		EV_HLDM_FireBullets( idx, forward, right, up, 6, vecSrc, vecAiming,
			2048.0f, BULLET_PLAYER_BUCKSHOT, 0, SGTC_TracerCountPtr( idx ),
			0.08716f, 0.08716f );
	}
	else
	{
		EV_HLDM_FireBullets( idx, forward, right, up, 4, vecSrc, vecAiming,
			2048.0f, BULLET_PLAYER_BUCKSHOT, 0, SGTC_TracerCountPtr( idx ),
			0.08716f, 0.04362f );
	}
}

// ref 0x1000765b — spas (identical to usas but anim 1)
void EV_SGTC_Spas( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin, angles, velocity, forward, right, up;
	vec3_t vecSrc, vecAiming, ShellVelocity, ShellOrigin;
	int shell;

	VectorCopy( args->origin, origin );
	VectorCopy( args->angles, angles );
	VectorCopy( args->velocity, velocity );

	AngleVectors( angles, forward, right, up );

	shell = gEngfuncs.pEventAPI->EV_FindModelIndex( "models/sshell.mdl" );

	if( EV_IsLocal( idx ) )
	{
		EV_MuzzleFlash();
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 1, 2 );
		V_PunchAxis( 0, -10.0f );	// 0xc1200000
	}

	EV_GetDefaultShellInfo( args, origin, velocity, ShellVelocity, ShellOrigin,
		forward, right, up, 32.0f, -12.0f, 6.0f );

	EV_EjectBrass( ShellOrigin, ShellVelocity, angles[YAW], shell, TE_BOUNCE_SHOTSHELL );

	gEngfuncs.pEventAPI->EV_PlaySound( idx, origin, CHAN_WEAPON, "weapons/spastir.wav",
		gEngfuncs.pfnRandomFloat( 0.95f, 1.0f ), 0.8f, 0,
		93 + gEngfuncs.pfnRandomLong( 0, 0x1f ) );

	EV_GetGunPosition( args, vecSrc, origin );

	VectorCopy( forward, vecAiming );

	if( gEngfuncs.GetMaxClients() < 2 )
	{
		EV_HLDM_FireBullets( idx, forward, right, up, 6, vecSrc, vecAiming,
			2048.0f, BULLET_PLAYER_BUCKSHOT, 0, SGTC_TracerCountPtr( idx ),
			0.08716f, 0.08716f );
	}
	else
	{
		EV_HLDM_FireBullets( idx, forward, right, up, 4, vecSrc, vecAiming,
			2048.0f, BULLET_PLAYER_BUCKSHOT, 0, SGTC_TracerCountPtr( idx ),
			0.08716f, 0.04362f );
	}
}

// ref 0x1000740b — spas2 pump: shell eject only, no sound/muzzle/fire
void EV_SGTC_Spas2( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin, angles, velocity, forward, right, up;
	vec3_t vecSrc, vecAiming, ShellVelocity, ShellOrigin;
	int shell;

	VectorCopy( args->origin, origin );
	VectorCopy( args->angles, angles );
	VectorCopy( args->velocity, velocity );

	AngleVectors( angles, forward, right, up );

	shell = gEngfuncs.pEventAPI->EV_FindModelIndex( "models/shell.mdl" );

	EV_GetDefaultShellInfo( args, origin, velocity, ShellVelocity, ShellOrigin,
		forward, right, up, 32.0f, -12.0f, 6.0f );

	EV_EjectBrass( ShellOrigin, ShellVelocity, angles[YAW], shell, TE_BOUNCE_SHOTSHELL );

	EV_GetGunPosition( args, vecSrc, origin );

	VectorCopy( forward, vecAiming );
}

// ref 0x10008e13 — eagle (m92s pistol)
void EV_SGTC_Eagle( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin, angles, velocity, forward, right, up;
	vec3_t vecSrc, vecAiming, ShellVelocity, ShellOrigin;
	int shell;
	static const char *sounds[3] =
	{
		"weapons/pistol-01.wav",
		"weapons/pistol-02.wav",
		"weapons/pistol-03.wav"
	};
	int snd;

	VectorCopy( args->origin, origin );
	VectorCopy( args->angles, angles );
	VectorCopy( args->velocity, velocity );

	AngleVectors( angles, forward, right, up );

	shell = gEngfuncs.pEventAPI->EV_FindModelIndex( "models/pshell.mdl" );

	if( EV_IsLocal( idx ) )
	{
		EV_MuzzleFlash();
		gEngfuncs.pEventAPI->EV_WeaponAnimation( args->bparam1 ? 6 : 2, 1 );
		V_PunchAxis( 0, -1.2f );	// 0xbf99999a
	}

	EV_GetDefaultShellInfo( args, origin, velocity, ShellVelocity, ShellOrigin,
		forward, right, up, 20.0f, -12.0f, 4.0f );

	EV_EjectBrass( ShellOrigin, ShellVelocity, angles[YAW], shell, TE_BOUNCE_SHELL );

	snd = (int)gEngfuncs.pfnRandomFloat( 0, 2 );
	if( snd >= 0 && snd <= 2 )
		gEngfuncs.pEventAPI->EV_PlaySound( idx, origin, CHAN_WEAPON, sounds[snd],
			1.0f, 0.8f, 0,					// ref volume literal 0x3f800000 (not R(0.95,1.0))
			98 + gEngfuncs.pfnRandomLong( 0, 3 ) );	// 0x62+R(0,3)

	EV_GetGunPosition( args, vecSrc, origin );

	VectorCopy( forward, vecAiming );

	EV_HLDM_FireBullets( idx, forward, right, up, 1, vecSrc, vecAiming,
		8192.0f, BULLET_PLAYER_9MM, 0, NULL, args->fparam1, args->fparam2 );
}

// ref 0x10009a4d — m16
void EV_SGTC_M16( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin, angles, velocity, forward, right, up;
	vec3_t vecSrc, vecAiming, ShellVelocity, ShellOrigin;
	vec3_t fwdScaled, shellVelScaled;
	int shell;

	VectorCopy( args->origin, origin );
	VectorCopy( args->angles, angles );
	VectorCopy( args->velocity, velocity );

	AngleVectors( angles, forward, right, up );

	shell = gEngfuncs.pEventAPI->EV_FindModelIndex( "models/rshell.mdl" );

	if( EV_IsLocal( idx ) )
	{
		EV_MuzzleFlash();
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 2, 2 );
		V_PunchAxis( 0, gEngfuncs.pfnRandomFloat( -2, 2 ) );	// 0xc0000000,0x40000000
	}

	// ref passes forward*0.95 as the forward arg of GetDefaultShellInfo
	VectorScale( forward, 0.95f, fwdScaled );
	EV_GetDefaultShellInfo( args, origin, velocity, ShellVelocity, ShellOrigin,
		fwdScaled, right, up, 20.0f, -12.0f, 6.0f );

	// ref scales the shell velocity by (RandomLong(0,4)/10)+0.8
	VectorScale( ShellVelocity,
		(float)( gEngfuncs.pfnRandomLong( 0, 4 ) / 10 ) + 0.8f,
		shellVelScaled );
	EV_EjectBrass( ShellOrigin, shellVelScaled, angles[YAW], shell, TE_BOUNCE_SHELL );

	gEngfuncs.pEventAPI->EV_PlaySound( idx, origin, CHAN_WEAPON, "weapons/M16-01.wav",
		1.0f, 0.8f, 0, 94 + gEngfuncs.pfnRandomLong( 0, 0xf ) );	// 0x5e+R(0,15)

	EV_GetGunPosition( args, vecSrc, origin );

	VectorCopy( forward, vecAiming );

	// ref branches on GetMaxClients()<2 but both sides are identical
	EV_HLDM_FireBullets( idx, forward, right, up, 1, vecSrc, vecAiming,
		8192.0f, BULLET_PLAYER_MP5, 2, SGTC_TracerCountPtr( idx ),
		args->fparam1, args->fparam2 );
}

// ref 0x100096a6 — p90 (anim 7 when silencer flag set, else 1)
void EV_SGTC_P90( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin, angles, velocity, forward, right, up;
	vec3_t vecSrc, vecAiming, ShellVelocity, ShellOrigin;
	int shell;

	VectorCopy( args->origin, origin );
	VectorCopy( args->angles, angles );
	VectorCopy( args->velocity, velocity );

	AngleVectors( angles, forward, right, up );

	shell = gEngfuncs.pEventAPI->EV_FindModelIndex( "models/rshell.mdl" );

	if( EV_IsLocal( idx ) )
	{
		EV_MuzzleFlash();
		if( g_sgtcP90Silencer == 0 )
			gEngfuncs.pEventAPI->EV_WeaponAnimation( 1, 2 );
		else
			gEngfuncs.pEventAPI->EV_WeaponAnimation( 7, 2 );
		g_sgtcP90Silencer = 0;
		V_PunchAxis( 0, gEngfuncs.pfnRandomFloat( -2, 2 ) );
	}

	EV_GetDefaultShellInfo( args, origin, velocity, ShellVelocity, ShellOrigin,
		forward, right, up, 20.0f, -12.0f, 4.0f );

	EV_EjectBrass( ShellOrigin, ShellVelocity, angles[YAW], shell, TE_BOUNCE_SHELL );

	gEngfuncs.pEventAPI->EV_PlaySound( idx, origin, CHAN_WEAPON, "weapons/P90-1.wav",
		1.0f, 0.8f, 0, 94 + gEngfuncs.pfnRandomLong( 0, 0xf ) );

	EV_GetGunPosition( args, vecSrc, origin );

	VectorCopy( forward, vecAiming );

	EV_HLDM_FireBullets( idx, forward, right, up, 1, vecSrc, vecAiming,
		8192.0f, BULLET_PLAYER_MP5, 2, SGTC_TracerCountPtr( idx ),
		args->fparam1, args->fparam2 );
}

// ref 0x1000968b — p90l: set silencer flag then run p90
void EV_SGTC_P90L( struct event_args_s *args )
{
	g_sgtcP90Silencer = 1;
	EV_SGTC_P90( args );
}

// ref 0x1000ad90 — psg1
void EV_SGTC_PSG1( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin, angles, velocity, forward, right, up;
	vec3_t vecSrc, vecAiming, ShellVelocity, ShellOrigin;
	int shell;

	VectorCopy( args->origin, origin );
	VectorCopy( args->angles, angles );
	VectorCopy( args->velocity, velocity );

	AngleVectors( angles, forward, right, up );

	shell = gEngfuncs.pEventAPI->EV_FindModelIndex( "models/shell.mdl" );

	if( EV_IsLocal( idx ) )
	{
		EV_MuzzleFlash();
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 2, 2 );
		V_PunchAxis( 0, -4.5f );	// strongest kick in the arsenal (was -2.0)
		V_PunchAxis( 2, gEngfuncs.pfnRandomFloat( -0.8f, 0.8f ) );	// roll
		SGTC_Aim_OnLocalFire();
	}

	EV_GetDefaultShellInfo( args, origin, velocity, ShellVelocity, ShellOrigin,
		forward, right, up, 20.0f, -12.0f, 4.0f );

	EV_EjectBrass( ShellOrigin, ShellVelocity, angles[YAW], shell, TE_BOUNCE_SHELL );

	// The mod's own PSG1 shot sample, unchanged from the original event.
	gEngfuncs.pEventAPI->EV_PlaySound( idx, origin, CHAN_WEAPON, "weapons/psg1_fire.wav",
		gEngfuncs.pfnRandomFloat( 0.94f, 1.0f ),
		0.8f, 0, 98 + gEngfuncs.pfnRandomLong( 0, 3 ) );

	EV_GetGunPosition( args, vecSrc, origin );

	VectorCopy( forward, vecAiming );

	// iTracerFreq 1 + the shared tracer counter: the PSG1 draws a tracer.
	EV_HLDM_FireBullets( idx, forward, right, up, 1, vecSrc, vecAiming,
		8192.0f, 19 /* ref bullet type 0x13 */, 1, SGTC_TracerCountPtr( idx ),
		args->fparam1, args->fparam2 );
}

// ref 0x1000a01b — reetou: local anim 1|2, body R(0,1), punch
void EV_SGTC_Reetou( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin;
	int seq, body;

	VectorCopy( args->origin, origin );

	if( EV_IsLocal( idx ) )
	{
		body = gEngfuncs.pfnRandomLong( 0, 1 );
		seq = gEngfuncs.pfnRandomLong( 0, 1 );
		if( seq == 0 )
			gEngfuncs.pEventAPI->EV_WeaponAnimation( 1, body );
		else
			gEngfuncs.pEventAPI->EV_WeaponAnimation( 2, body );
		V_PunchAxis( 0, gEngfuncs.pfnRandomFloat( -2, 2 ) );
	}
}

// ref 0x1000a10c — reetou2: local anim 3, body R(0,1)
void EV_SGTC_Reetou2( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin;

	VectorCopy( args->origin, origin );

	if( EV_IsLocal( idx ) )
	{
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 3, gEngfuncs.pfnRandomLong( 0, 1 ) );
	}
}

// ref 0x100091db — coujaf: swing1|2|3 then local anim(1, R(0,1))
void EV_SGTC_Coujaf( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin;
	static const char *sounds[3] =
	{
		"weapons/swing1.wav",
		"weapons/swing2.wav",
		"weapons/swing3.wav"
	};
	int snd;

	VectorCopy( args->origin, origin );

	snd = (int)gEngfuncs.pfnRandomFloat( 0, 2 );
	if( snd >= 0 && snd <= 2 )
		gEngfuncs.pEventAPI->EV_PlaySound( idx, origin, CHAN_WEAPON, sounds[snd],
			0.7f,	// 0x3f333333
			0.8f, 0, 100 );

	if( EV_IsLocal( idx ) )
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 1, gEngfuncs.pfnRandomLong( 0, 1 ) );
}

// ref 0x100095df — couteau: local anim(1, R(0,1)) only
void EV_SGTC_Couteau( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin;

	VectorCopy( args->origin, origin );

	if( EV_IsLocal( idx ) )
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 1, gEngfuncs.pfnRandomLong( 0, 1 ) );
}

// ref 0x10009343 — batsup: gunpos/aim copy, swing1, local anim(4,1)
void EV_SGTC_Batsup( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin, angles, forward, right, up;
	vec3_t vecSrc, vecAiming;

	VectorCopy( args->origin, origin );
	VectorCopy( args->angles, angles );

	AngleVectors( angles, forward, right, up );

	EV_GetGunPosition( args, vecSrc, origin );

	VectorCopy( forward, vecAiming );

	gEngfuncs.pEventAPI->EV_PlaySound( idx, origin, CHAN_WEAPON, "weapons/swing1.wav",
		0.7f, 0.8f, 0, 100 );

	if( EV_IsLocal( idx ) )
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 4, 1 );
}

// ref 0x100094a8 — batsup2: same minus sound, local anim(5,1)
void EV_SGTC_Batsup2( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin, angles, forward, right, up;
	vec3_t vecSrc, vecAiming;

	VectorCopy( args->origin, origin );
	VectorCopy( args->angles, angles );

	AngleVectors( angles, forward, right, up );

	EV_GetGunPosition( args, vecSrc, origin );

	VectorCopy( forward, vecAiming );

	if( EV_IsLocal( idx ) )
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 5, 1 );
}

// ref 0x1000a1a8 — invis: local anim(1,2) only
void EV_SGTC_Invis( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin;

	VectorCopy( args->origin, origin );

	if( EV_IsLocal( idx ) )
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 1, 2 );
}

// ref 0x10008771 — lance: local anim(1,2) + punch, tirlancegoa
void EV_SGTC_Lance( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin;

	VectorCopy( args->origin, origin );

	if( EV_IsLocal( idx ) )
	{
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 1, 2 );
		V_PunchAxis( 0, gEngfuncs.pfnRandomFloat( -2, 2 ) );
	}

	gEngfuncs.pEventAPI->EV_PlaySound( idx, origin, CHAN_WEAPON, "weapons/tirlancegoa.wav",
		1.0f, 0.8f, 0, 94 + gEngfuncs.pfnRandomLong( 0, 0xf ) );
}

// ref 0x100086a8 — biglance: punch rnd(5,10), pitch 75+R(0,15)
void EV_SGTC_BigLance( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin;

	VectorCopy( args->origin, origin );

	if( EV_IsLocal( idx ) )
	{
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 1, 2 );
		V_PunchAxis( 0, gEngfuncs.pfnRandomFloat( 5, 10 ) );	// 0x40a00000,0x41200000
	}

	gEngfuncs.pEventAPI->EV_PlaySound( idx, origin, CHAN_WEAPON, "weapons/tirlancegoa.wav",
		1.0f, 0.8f, 0, 75 + gEngfuncs.pfnRandomLong( 0, 0xf ) );	// 0x4b+R(0,15)
}

// ref 0x1000883a — lance2: cbar_miss1, local anim(2,1)
void EV_SGTC_Lance2( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin;

	VectorCopy( args->origin, origin );

	gEngfuncs.pEventAPI->EV_PlaySound( idx, origin, CHAN_WEAPON, "weapons/cbar_miss1.wav",
		1.0f, 0.8f, 0, 100 );

	if( EV_IsLocal( idx ) )
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 2, 1 );
}

// ref 0x100082c6 — main: gunpos + VectorMA(origin,200,fwd) + tirmain
void EV_SGTC_Main( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin, angles, forward, right, up;
	vec3_t vecSrc, vecEnd;
	pmtrace_t tr;

	memset( &tr, 0, sizeof( tr ) );	// ref FUN_1000b300

	VectorCopy( args->origin, origin );
	// ref calls EV_GetGunPosition before args->origin is copied in —
	// basis is the zeroed local, so vecSrc = view_ofs (NOT origin+view_ofs).
	EV_GetGunPosition( args, vecSrc, vec3_origin );

	VectorCopy( args->angles, angles );

	AngleVectors( angles, forward, right, up );

	VectorMA( origin, 200.0f, forward, vecEnd );

	(void)EV_IsLocal( idx );	// ref calls for side effect only

	gEngfuncs.pEventAPI->EV_PlaySound( idx, origin, CHAN_WEAPON, "weapons/tirmain.wav",
		1.0f, 0.8f, 0, 94 + gEngfuncs.pfnRandomLong( 0, 0xf ) );
}

// ref 0x100085f0 — main2: local anim(3,2) + punch -10, tirmain
void EV_SGTC_Main2( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin;

	VectorCopy( args->origin, origin );

	if( EV_IsLocal( idx ) )
	{
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 3, 2 );
		V_PunchAxis( 0, -10.0f );	// 0xc1200000
	}

	gEngfuncs.pEventAPI->EV_PlaySound( idx, origin, CHAN_WEAPON, "weapons/tirmain.wav",
		1.0f, 0.8f, 0, 94 + gEngfuncs.pfnRandomLong( 0, 0xf ) );
}

// ref 0x1000844e — mainc: gunpos + VectorMA(origin,200,fwd), no sound
void EV_SGTC_MainC( struct event_args_s *args )
{
	vec3_t origin, angles, forward, right, up;
	vec3_t vecSrc, vecEnd;
	pmtrace_t tr;

	memset( &tr, 0, sizeof( tr ) );

	VectorCopy( args->origin, origin );
	// ref calls EV_GetGunPosition before args->origin is copied in —
	// basis is the zeroed local, so vecSrc = view_ofs (NOT origin+view_ofs).
	EV_GetGunPosition( args, vecSrc, vec3_origin );

	VectorCopy( args->angles, angles );

	AngleVectors( angles, forward, right, up );

	VectorMA( origin, 200.0f, forward, vecEnd );
}

// ref 0x1000858d — mains: stop the main loop sound
void EV_SGTC_MainS( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin;

	VectorCopy( args->origin, origin );

	gEngfuncs.pEventAPI->EV_StopSound( idx, CHAN_STATIC, "weapons/tirmaincont.wav" );
}

// ref 0x100088e4 — zata: trace + zattir beam
void EV_SGTC_ZatA( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin, angles, forward, right, up;
	vec3_t vecSrc, vecEnd, traceStart;
	pmtrace_t tr;
	int mdl;

	memset( &tr, 0, sizeof( tr ) );

	VectorCopy( args->origin, origin );
	// ref calls EV_GetGunPosition before args->origin is copied in —
	// basis is the zeroed local, so vecSrc = view_ofs (NOT origin+view_ofs).
	EV_GetGunPosition( args, vecSrc, vec3_origin );

	VectorCopy( args->angles, angles );

	AngleVectors( angles, forward, right, up );

	VectorMA( origin, 8192.0f, forward, vecEnd );

	if( EV_IsLocal( idx ) )
	{
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 1, gEngfuncs.pfnRandomLong( 0, 1 ) );
		SGTC_Aim_OnLocalFire();
	}

	gEngfuncs.pEventAPI->EV_PlaySound( idx, origin, CHAN_WEAPON, "weapons/tirzata.wav",
		1.0f, 0.8f, 0, 94 + gEngfuncs.pfnRandomLong( 0, 0xf ) );

	VectorAdd( origin, vecSrc, traceStart );
	gEngfuncs.pEventAPI->EV_PlayerTrace( traceStart, vecEnd, PM_STUDIO_BOX, -1, &tr );

	if( EV_IsLocal( idx ) )
		EV_MuzzleFlash();

	mdl = gEngfuncs.pEventAPI->EV_FindModelIndex( "sprites/zattir.spr" );

	// Stargate energy blue (70,150,255): wide dim halo + narrow bright core.
	gEngfuncs.pEfxAPI->R_BeamEntPoint( idx | 0x1000, tr.endpos, mdl,
		0.3f,		// life
		3.0f,		// width	(outer)
		1.2f,		// amplitude
		100.0f,		// brightness	(outer, dim)
		0, 0,
		20.0f,		// framerate
		70.0f, 150.0f, 255.0f );

	gEngfuncs.pEfxAPI->R_BeamEntPoint( idx | 0x1000, tr.endpos, mdl,
		0.3f,		// life
		1.0f,		// width	(core)
		1.2f,		// amplitude
		160.0f,		// brightness	(core, bright)
		0, 0,
		20.0f,		// framerate
		70.0f, 150.0f, 255.0f );
}

// ref 0x10008b79 — zatb: identical to zata with tirzatb
void EV_SGTC_ZatB( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin, angles, forward, right, up;
	vec3_t vecSrc, vecEnd, traceStart;
	pmtrace_t tr;
	int mdl;

	memset( &tr, 0, sizeof( tr ) );

	VectorCopy( args->origin, origin );
	// ref calls EV_GetGunPosition before args->origin is copied in —
	// basis is the zeroed local, so vecSrc = view_ofs (NOT origin+view_ofs).
	EV_GetGunPosition( args, vecSrc, vec3_origin );

	VectorCopy( args->angles, angles );

	AngleVectors( angles, forward, right, up );

	VectorMA( origin, 8192.0f, forward, vecEnd );

	if( EV_IsLocal( idx ) )
	{
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 1, gEngfuncs.pfnRandomLong( 0, 1 ) );
		SGTC_Aim_OnLocalFire();
	}

	gEngfuncs.pEventAPI->EV_PlaySound( idx, origin, CHAN_WEAPON, "weapons/tirzatb.wav",
		1.0f, 0.8f, 0, 94 + gEngfuncs.pfnRandomLong( 0, 0xf ) );

	VectorAdd( origin, vecSrc, traceStart );
	gEngfuncs.pEventAPI->EV_PlayerTrace( traceStart, vecEnd, PM_STUDIO_BOX, -1, &tr );

	if( EV_IsLocal( idx ) )
		EV_MuzzleFlash();

	mdl = gEngfuncs.pEventAPI->EV_FindModelIndex( "sprites/zattir.spr" );

	// Stargate energy blue (70,150,255): wide dim halo + narrow bright core.
	gEngfuncs.pEfxAPI->R_BeamEntPoint( idx | 0x1000, tr.endpos, mdl,
		0.3f,		// life
		3.0f,		// width	(outer)
		1.2f,		// amplitude
		100.0f,		// brightness	(outer, dim)
		0, 0,
		20.0f,		// framerate
		70.0f, 150.0f, 255.0f );

	gEngfuncs.pEfxAPI->R_BeamEntPoint( idx | 0x1000, tr.endpos, mdl,
		0.3f,		// life
		1.0f,		// width	(core)
		1.2f,		// amplitude
		160.0f,		// brightness	(core, bright)
		0, 0,
		20.0f,		// framerate
		70.0f, 150.0f, 255.0f );
}

// ref 0x1000a214 — zatarc: trace + lazer beam
void EV_SGTC_ZatArc( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin, angles, forward, right, up;
	vec3_t vecSrc, vecEnd, traceStart;
	pmtrace_t tr;
	int mdl;
	int impact;

	memset( &tr, 0, sizeof( tr ) );

	VectorCopy( args->origin, origin );
	// ref calls EV_GetGunPosition before args->origin is copied in —
	// basis is the zeroed local, so vecSrc = view_ofs (NOT origin+view_ofs).
	EV_GetGunPosition( args, vecSrc, vec3_origin );

	VectorCopy( args->angles, angles );

	AngleVectors( angles, forward, right, up );

	VectorMA( origin, 8192.0f, forward, vecEnd );

	if( EV_IsLocal( idx ) )
	{
		gEngfuncs.pEventAPI->EV_WeaponAnimation( 1, 2 );
		SGTC_Aim_OnLocalFire();
	}

	gEngfuncs.pEventAPI->EV_PlaySound( idx, origin, CHAN_WEAPON, "weapons/zar.wav",
		1.0f, 0.8f, 0, 94 + gEngfuncs.pfnRandomLong( 0, 0xf ) );

	VectorAdd( origin, vecSrc, traceStart );
	gEngfuncs.pEventAPI->EV_PlayerTrace( traceStart, vecEnd, PM_STUDIO_BOX, -1, &tr );

	if( EV_IsLocal( idx ) )
		EV_MuzzleFlash();

	mdl = gEngfuncs.pEventAPI->EV_FindModelIndex( "sprites/lazer.spr" );

	// Stargate blue (60,140,255) arc: bright core plus a wide dim glow along
	// the same segment, then an impact flare at the trace end.
	gEngfuncs.pEfxAPI->R_BeamEntPoint( idx | 0x1000, tr.endpos, mdl,
		0.14f,		// life
		2.2f,		// width	(core)
		0.8f,		// amplitude
		200.0f,		// brightness
		0, 0,
		0,			// framerate
		60.0f, 140.0f, 255.0f );

	gEngfuncs.pEfxAPI->R_BeamEntPoint( idx | 0x1000, tr.endpos, mdl,
		0.14f,		// life
		6.0f,		// width	(glow)
		0.8f,		// amplitude
		90.0f,		// brightness	(glow)
		0, 0,
		0,			// framerate
		60.0f, 140.0f, 255.0f );

	impact = gEngfuncs.pEventAPI->EV_FindModelIndex( "sprites/zarimpact.spr" );
	if( impact <= 0 )
		impact = gEngfuncs.pEventAPI->EV_FindModelIndex( "sprites/zatimpact.spr" );
	if( impact > 0 )
		gEngfuncs.pEfxAPI->R_TempSprite( tr.endpos, vec3_origin, 1.0f, impact,
			kRenderGlow, kRenderFxNoDissipation, 1.0f, 0.25f, FTENT_FADEOUT );
}

// ref 0x1000a457 — tac: trace + lazer beam, no sound/anim/local gate
void EV_SGTC_Tac( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin, angles, forward, right, up;
	vec3_t vecSrc, vecEnd, traceStart;
	pmtrace_t tr;
	int mdl;

	memset( &tr, 0, sizeof( tr ) );

	VectorCopy( args->origin, origin );
	// ref calls EV_GetGunPosition before args->origin is copied in —
	// basis is the zeroed local, so vecSrc = view_ofs (NOT origin+view_ofs).
	EV_GetGunPosition( args, vecSrc, vec3_origin );

	VectorCopy( args->angles, angles );

	AngleVectors( angles, forward, right, up );

	VectorMA( origin, 600.0f, forward, vecEnd );

	VectorAdd( origin, vecSrc, traceStart );
	gEngfuncs.pEventAPI->EV_PlayerTrace( traceStart, vecEnd, PM_STUDIO_BOX, -1, &tr );

	mdl = gEngfuncs.pEventAPI->EV_FindModelIndex( "sprites/lazer.spr" );

	gEngfuncs.pEfxAPI->R_BeamEntPoint( idx | 0x1000, tr.endpos, mdl,
		0.1f, 1.0f, 0, 128.0f, 0, 0, 0, 255.0f, 0, 0 );
}

// ref 0x10009e21 — larve: local duck-offset hull trace + anim(5,0)
void EV_SGTC_Larve( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin, angles, forward;
	pmtrace_t tr;

	memset( &tr, 0, sizeof( tr ) );

	VectorCopy( args->origin, origin );
	VectorCopy( args->angles, angles );

	// ref passes NULL for right and up
	AngleVectors( angles, forward, NULL, NULL );

	if( EV_IsLocal( idx ) )
	{
		vec3_t start, end, ofs1, ofs2, tmp;

		if( args->ducking != 0 )
		{
			// ref adds Vector(-16,-16,-36) + Vector(-16,-16,-18) = (-32,-32,-54)
			ofs1[0] = -16.0f; ofs1[1] = -16.0f; ofs1[2] = -18.0f;	// -0x10,-0x10,-0x12
			ofs2[0] = -16.0f; ofs2[1] = -16.0f; ofs2[2] = -36.0f;	// -0x10,-0x10,-0x24
			VectorAdd( ofs2, ofs1, tmp );
			VectorAdd( origin, tmp, origin );
		}

		gEngfuncs.pEventAPI->EV_PushPMStates();
		gEngfuncs.pEventAPI->EV_SetSolidPlayers( idx - 1 );
		gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );

		VectorScale( forward, 64.0f, tmp );
		VectorAdd( origin, tmp, end );		// end = origin + forward*64
		VectorScale( forward, 20.0f, tmp );
		VectorAdd( origin, tmp, start );	// start = origin + forward*20
		gEngfuncs.pEventAPI->EV_PlayerTrace( start, end, PM_STUDIO_BOX, -1, &tr );

		if( !tr.allsolid && !tr.startsolid && tr.fraction > 0.25f )
			gEngfuncs.pEventAPI->EV_WeaponAnimation( 5, 0 );

		gEngfuncs.pEventAPI->EV_PopPMStates();
	}
}

// ref 0x1000a701 — beam_fire (mirror of stock EV_EgonFire structure)
void EV_SGTC_BeamFire( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin;

	VectorCopy( args->origin, origin );

	// ref reads fparam2/iparam1 into dead locals — unused, skipped

	if( EV_IsLocal( idx ) && g_sgtcBeam == NULL && cl_lw->value != 0.0f )
	{
		vec3_t vecSrc, vecEnd, angles, forward, right, up;
		pmtrace_t tr;
		cl_entity_t *pl = gEngfuncs.GetEntityByIndex( idx );
		int mdl;

		if( pl )
		{
			memset( &tr, 0, sizeof( tr ) );

			VectorCopy( gHUD.m_vecAngles, angles );	// ref DAT_100e53e4

			AngleVectors( angles, forward, right, up );

			EV_GetGunPosition( args, vecSrc, pl->origin );

			VectorMA( vecSrc, 2048.0f, forward, vecEnd );

			gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction( false, true );
			gEngfuncs.pEventAPI->EV_PushPMStates();
			gEngfuncs.pEventAPI->EV_SetSolidPlayers( idx - 1 );
			gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
			gEngfuncs.pEventAPI->EV_PlayerTrace( vecSrc, vecEnd, PM_STUDIO_BOX, -1, &tr );
			gEngfuncs.pEventAPI->EV_PopPMStates();

			mdl = gEngfuncs.pEventAPI->EV_FindModelIndex( "sprites/lazer.spr" );

			// ref calls IEngineStudio.IsHardware() here (DAT_100f71c8 = the
			// studio-api table at 0x100f7128 + 0xa0, filled by the engine via
			// HUD_GetStudioModelInterface). Result only halves a dead local
			// vec3 (50,50,125) never passed to R_BeamEntPoint — dead code in
			// ref, correctly omitted.

			g_sgtcBeam = gEngfuncs.pEfxAPI->R_BeamEntPoint( idx | 0x1000, tr.endpos, mdl,
				99999.0f,	// life		0x47c34f80
				0.2f,		// width	0x3e4ccccd
				0, 128.0f,	// amp, bright	0x43000000
				0, 0, 0,
				0, 255.0f,	// g		0x437f0000
				0 );
		}
	}
}

// ref 0x1000a9ee — beam_stop
void EV_SGTC_BeamStop( struct event_args_s *args )
{
	int idx = args->entindex;
	vec3_t origin;

	VectorCopy( args->origin, origin );

	g_sgtcBeamStop = 1;

	if( EV_IsLocal( idx ) && g_sgtcBeam != NULL )
	{
		g_sgtcBeam->die = 0.0f;	// ref writes +0x38 = 0
		g_sgtcBeam = NULL;
	}
}

// ref 0x10008e0e — shield: EMPTY body in ref
void EV_SGTC_Shield( struct event_args_s *args )
{
}

// zat.sc is NOT hooked by ref Game_HookEvents — keep the old fallback stub.
void EV_SGTC_Zat( struct event_args_s *args )
{
	SGTC_EventFallback( args, 1 );
}

}

void SGTC_ClientProtocol_HookEvents( void )
{
#define X( fn, script ) gEngfuncs.pfnHookEvent( script, fn );
SGTC_EV_LIST
#undef X
}

int SGTC_CustomEventCount( void )
{
	return 31;
}

// ---------------------------------------------------------------------------
// 3. Client-referenced sprites/sounds (precache audit, not engine precache).
//
// Client binary references exactly 28 sprite paths (3 are printf formats,
// not files). SGTC-specific effect/HUD sprites among them:
//   sprites/handtir.spr sprites/lazer.spr sprites/Muz1.spr
//   sprites/rain.spr sprites/snow.spr sprites/shockwave.spr
//   sprites/zatimpact.spr sprites/zarimpact.spr sprites/zattir.spr
//   sprites/fexplo.spr sprites/xfireball3.spr
// The SGTC HUD layout itself (sprites/hud.txt, 640-res section) adds:
//   hud/hud_left hud/hud_right hud/portrait_* hud/ticketdock hud/radar
//   hud/pbar hud/pbare hud/needle hud/blip hud/tacdisp hud/slidein
//   hud/tac_red hud/tac_yellow hud/tac_green hud/statbar hud/line hud/key
//   avatars/* crosshairs/aim crosshairs/goaim crosshairs/goaim2
//   crosshairs/sgaim crosshairs/zatarc crosshairs/scope
//   crosshairs/zatarczoom weapons/* (per weapon_*.txt).
// Sounds the client actually plays (209 .wav refs; SGTC audibles):
//   forcefield/* radio/son*.wav radio/songoa*.wav rt_sounds/rt_button.wav
//   weapons/* (M16-01 P90-1 PSG-01 tirmain* lance* zat* medic goaregen
//   gacgoa invis larve tac terblast* spastir spaspomp tacact tacp tacs …).
// нагрузку NOTE: VidInit below only SPR_Loads the small set the client draws
// every frame; hud.txt-driven sprites load through the stock CHud::VidInit
// path unchanged.
// ---------------------------------------------------------------------------

static const char *const s_sgtcClientSprites[] =
{
	"sprites/handtir.spr",
	"sprites/lazer.spr",
	"sprites/Muz1.spr",
	"sprites/rain.spr",
	"sprites/snow.spr",
	"sprites/shockwave.spr",
	"sprites/zatimpact.spr",
	"sprites/zarimpact.spr",
	"sprites/zattir.spr",
	"sprites/fexplo.spr",
	"sprites/xfireball3.spr",
	"sprites/steam1.spr",
	"sprites/wallpuff.spr",
	"sprites/richo1.spr",
	"sprites/laserbeam.spr",
	"sprites/voiceicon.spr",
	"sprites/crosshairs.spr",
	"sprites/camera.spr",
	"sprites/cursor.spr",
	"sprites/tile.spr",
	"sprites/iplayer.spr",
	"sprites/iplayerblue.spr",
	"sprites/iplayerred.spr",
	"sprites/iplayerdead.spr",
};

void SGTC_ClientProtocol_VidInit( void )
{
	for( size_t i = 0; i < sizeof( s_sgtcClientSprites ) / sizeof( s_sgtcClientSprites[0] ); i++ )
	{
		HSPRITE h = SPR_Load( s_sgtcClientSprites[i] );
		(void)h; // missing-file printf comes from the engine loader
	}
}
