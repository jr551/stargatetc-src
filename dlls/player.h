/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
#pragma once
#if !defined(PLAYER_H)
#define PLAYER_H

#include "pm_materials.h"
#include "sgtc_enums.h"	// SgtcTeam / SgtcEntTeam / SgtcClass / SgtcWeaponSlot (game_shared)

#define PLAYER_FATAL_FALL_SPEED		1024// approx 60 feet
#define PLAYER_MAX_SAFE_FALL_SPEED	580// approx 20 feet
#define DAMAGE_FOR_FALL_SPEED		(float) 100 / ( PLAYER_FATAL_FALL_SPEED - PLAYER_MAX_SAFE_FALL_SPEED )// damage per unit per second.
#define PLAYER_MIN_BOUNCE_SPEED		200
#define PLAYER_FALL_PUNCH_THRESHHOLD (float)350 // won't punch player's screen/make scrape noise unless player falling at least this fast.

//
// Player PHYSICS FLAGS bits
//
#define PFLAG_ONLADDER		( 1<<0 )
#define PFLAG_ONSWING		( 1<<0 )
#define PFLAG_ONTRAIN		( 1<<1 )
#define PFLAG_ONBARNACLE	( 1<<2 )
#define PFLAG_DUCKING		( 1<<3 )		// In the process of ducking, but totally squatted yet
#define PFLAG_USING		( 1<<4 )		// Using a continuous entity
#define PFLAG_OBSERVER		( 1<<5 )		// player is locked in stationary cam mode. Spectators can move, observers can't.

//
// generic player
//
//-----------------------------------------------------
//This is Half-Life player entity
//-----------------------------------------------------
#define CSUITPLAYLIST	4		// max of 4 suit sentences queued up at any time

#define SUIT_GROUP			TRUE
#define	SUIT_SENTENCE		FALSE

#define	SUIT_REPEAT_OK		0
#define SUIT_NEXT_IN_30SEC	30
#define SUIT_NEXT_IN_1MIN	60
#define SUIT_NEXT_IN_5MIN	300
#define SUIT_NEXT_IN_10MIN	600
#define SUIT_NEXT_IN_30MIN	1800
#define SUIT_NEXT_IN_1HOUR	3600

#define CSUITNOREPEAT		32

#define	SOUND_FLASHLIGHT_ON		"items/flashlight1.wav"
#define	SOUND_FLASHLIGHT_OFF	"items/flashlight1.wav"

#define TEAM_NAME_LENGTH	16

typedef enum
{
	PLAYER_IDLE,
	PLAYER_WALK,
	PLAYER_JUMP,
	PLAYER_SUPERJUMP,
	PLAYER_DIE,
	PLAYER_ATTACK1
} PLAYER_ANIM;

#define MAX_ID_RANGE 2048
#define SBAR_STRING_SIZE 128

enum sbar_data
{
	SBAR_ID_TARGETNAME = 1,
	SBAR_ID_TARGETHEALTH,
	SBAR_ID_TARGETARMOR,
	SBAR_END
};

#define CHAT_INTERVAL 1.0f

class CBasePlayer : public CBaseMonster
{
public:
	// Spectator camera
	void	Observer_FindNextPlayer( bool bReverse );
	void	Observer_HandleButtons();
	void	Observer_SetMode( int iMode );
	void	Observer_CheckTarget();
	void	Observer_CheckProperties();
	EHANDLE	m_hObserverTarget;
	float	m_flNextObserverInput;
	int		m_iObserverWeapon;	// weapon of current tracked target
	int		m_iObserverLastMode;// last used observer mode
	int		IsObserver() { return pev->iuser1; };

	int					random_seed;    // See that is shared between client & server for shared weapons code

	int					m_iPlayerSound;// the index of the sound list slot reserved for this player
	int					m_iTargetVolume;// ideal sound volume. 
	int					m_iWeaponVolume;// how loud the player's weapon is right now.
	int					m_iExtraSoundTypes;// additional classification for this weapon's sound
	int					m_iWeaponFlash;// brightness of the weapon flash
	float				m_flStopExtraSoundTime;

	float				m_flFlashLightTime;	// Time until next battery draw/Recharge
	int					m_iFlashBattery;		// Flashlight Battery Draw

	int					m_afButtonLast;
	int					m_afButtonPressed;
	int					m_afButtonReleased;

	edict_t			   *m_pentSndLast;			// last sound entity to modify player room type
	float				m_flSndRoomtype;		// last roomtype set by sound entity
	float				m_flSndRange;			// dist from player to sound entity

	float				m_flFallVelocity;

	int					m_rgItems[MAX_ITEMS];
	int					m_fKnownItem;		// True when a new item needs to be added
	int					m_fNewAmmo;			// True when a new item has been added

	unsigned int		m_afPhysicsFlags;	// physics flags - set when 'normal' physics should be revisited or overriden
	float				m_fNextSuicideTime; // the time after which the player can next use the suicide command

	// these are time-sensitive things that we keep track of
	float				m_flTimeStepSound;	// when the last stepping sound was made
	float				m_flTimeWeaponIdle; // when to play another weapon idle animation.
	float				m_flSwimTime;		// how long player has been underwater
	float				m_flDuckTime;		// how long we've been ducking
	float				m_flWallJumpTime;	// how long until next walljump

	float				m_flSuitUpdate;					// when to play next suit update
	int					m_rgSuitPlayList[CSUITPLAYLIST];// next sentencenum to play for suit update
	int					m_iSuitPlayNext;				// next sentence slot for queue storage;
	int					m_rgiSuitNoRepeat[CSUITNOREPEAT];		// suit sentence no repeat list
	float				m_rgflSuitNoRepeatTime[CSUITNOREPEAT];	// how long to wait before allowing repeat
	int					m_lastDamageAmount;		// Last damage taken
	float				m_tbdPrev;				// Time-based damage timer

	float				m_flgeigerRange;		// range to nearest radiation source
	float				m_flgeigerDelay;		// delay per update of range msg to client
	int					m_igeigerRangePrev;
	int					m_iStepLeft;			// alternate left/right foot stepping sound
	char				m_szTextureName[CBTEXTURENAMEMAX];	// current texture name we're standing on
	char				m_chTextureType;		// current texture type

	int					m_idrowndmg;			// track drowning damage taken
	int					m_idrownrestored;		// track drowning damage restored

	int					m_bitsHUDDamage;		// Damage bits for the current fame. These get sent to 
										// the hude via the DAMAGE message
	BOOL				m_fInitHUD;				// True when deferred HUD restart msg needs to be sent
	BOOL				m_fGameHUDInitialized;
	int					m_iTrain;				// Train control position
	BOOL				m_fWeapon;				// Set this to FALSE to force a reset of the current weapon HUD info

	EHANDLE				m_pTank;				// the tank which the player is currently controlling,  NULL if no tank
	float				m_fDeadTime;			// the time at which the player died  (used in PlayerDeathThink())

	BOOL			m_fNoPlayerSound;	// a debugging feature. Player makes no sound if this is true. 
	BOOL			m_fLongJump; // does this player have the longjump module?

	float       m_tSneaking;
	int			m_iUpdateTime;		// stores the number of frame ticks before sending HUD update messages
	int			m_iClientHealth;	// the health currently known by the client.  If this changes, send a new
	int			m_iClientBattery;	// the Battery currently known by the client.  If this changes, send a new
	int			m_iHideHUD;		// the players hud weapon info is to be hidden
	int			m_iClientHideHUD;
	int			m_iFOV;			// field of view
	int			m_iClientFOV;	// client's known FOV

	// usable player items 
	CBasePlayerItem	*m_rgpPlayerItems[MAX_ITEM_TYPES];
	CBasePlayerItem *m_pActiveItem;
	CBasePlayerItem *m_pClientActiveItem;  // client version of the active item
	CBasePlayerItem *m_pLastItem;

	// shared ammo slots
	int	m_rgAmmo[MAX_AMMO_SLOTS];
	int	m_rgAmmoLast[MAX_AMMO_SLOTS];

	Vector				m_vecAutoAim;
	BOOL				m_fOnTarget;
	int					m_iDeaths;
	float				m_flRespawnTimer;	// used in PlayerDeathThink() to make sure players can always respawn

	int m_lastx, m_lasty;  // These are the previous update's crosshair angles, DON"T SAVE/RESTORE

	int m_nCustomSprayFrames;// Custom clan logo frames for this player
	float	m_flNextDecalTime;// next time this player can spray a decal

	char m_szTeamName[TEAM_NAME_LENGTH];

	// --- StargateTC extension fields (ref CBasePlayer extra offsets; the .so
	// uses a shifted entvars/CBasePlayer layout, mapped here by name) ---
	float	m_flNukeGrace;		// ref +0x414: game_nuke grace timer (killed if < now)
	float	m_flNukeChargeTime;	// ref +0x42c: arm-charge tick deadline (now+0.15)
	int	m_iNukeCharge;		// ref +0x45c: arm charge, fires at 100
	int	m_iInBombZone;		// ref +0xaec: >=1 while inside zone_bomb_target scope
	int	m_iSgtcFocusedKills;	// ref +0x930 — inflictor streak (Killed); tourny elite tiers
	int	m_iNukeBonus;		// ref +0x934: +4 on nuke arming; Progress HUD icon byte
	int	m_iProgressSentPri;	// ref +0x444: last Progress pri sent (+0x45c)
	char	m_bSgtcCloakTick;	// ref +0x448 — SgtcThink cloak pulse @0x139b5d (CP spawn / invis)
	int	m_iNukeBonusSent;	// ref +0x938: last Progress icon sent (+0x934)
	int	m_iProgressTeam[SGTC_NUM_WEAPON_SLOTS];	// ref +0x93c: Progress team bytes (28 slots)
	char	m_bBombKeyWarnSent;	// ref +0x927: HasKey bomb-zone warn latch
	int	m_iBombKeyTimer;	// ref +0x90c: bomb-zone warn countdown
	char	m_bBombZoneKeyLatch;	// ref +0x44c: HasKey sync while in bomb zone
	float	m_flZoneTime;		// ref +0x498: last capture-zone touch (now+0.3)
	int	m_iZoneId;		// ref +0x4cc: owning capture-zone id
	float	m_flSpawnCampTouch;	// ref +0x4a4: spawn-camp latch (now+0.3) / cleared on capture touch
	float	m_flSpawnProxDeadline;	// ref +0x4a8: extended while near team spawn info
	Vector	m_vecSpawnCampRef;	// ref +0x4b4: camp position snapshot
	char	m_bZoneFlag;		// ref +0x3e2: cleared on capture-zone touch
	int	m_iCapFlags2;		// ref +0xa70: cleared on capture-zone touch
	int	m_iHasNVG;		// ref +0xe1c: set by item_NVG
	float	m_flGateTravel;		// ref +0x408: gate-travel deadline (now+~1s)
	int	m_iGateTeam;		// ref +0x458: team copied from zone_gatetravel
	float	m_flRadarNext;		// ref +0x49c: next CBasePlayer::RadarScan tick
	Vector	m_vecHurtInvertRgb;	// ref +0x4c0 — recallall RGB triple (SgtcThink @0x139a90)
	char	m_bHurtInvertFx;	// ref +0x4dd — hurtinvert PE tint (@0x1399cf)
	int	m_iSgtcDoorLockDmg;	// ref +0xd0 — CLockDown::Think damage latch from owner
	char	m_bSgtcDoorLock;	// ref +0xd4 — CLockDown active on this player
	int	m_iShieldCharge;	// ref +0x158 — shield weapon charge 0..9 (ShieldThinker)
	float	m_flShieldLatch;	// ref +0x3f8
	float	m_flSgtcLatch3fc;	// ref +0x3fc — zat-maintain latch (rodata 0.125 @0x1d7528)
	float	m_flShieldThinkNext;	// ref +0x404
	char	m_bShieldWall;		// ref +0x3e0 — deploy personal shield
	char	m_bShieldSpawned;	// ref +0x4e4 — effect_shield entity active
	char	m_bCampZoneLatch;	// ref +0x3e2 (DM Think camper zone state)
	float	m_flCampWarnDeadline;	// ref +0x40c: camptime deadline once in zone
	float	m_flCampNextWarn;	// ref +0xa70: next warn tick (overlaid int)
	float	m_flCampRearm;		// ref +0xa80: zone re-arm tick
	// ref @0x193a4f — anti-camper warning fan-out; arg 0: "3" else "2".
	void CampWarn( int arg );
	char	m_bSgtcDeathCamArmed;	// ref +0x3e1 — set once StartDeathCam used the observer spot
	char	m_bLanceStunLatch;	// ref +0x4e8 — latched copy of +0xa3e (SgtcThink)
	char	m_bShieldSprint;	// ref +0x4e9
	float	m_flSgtcInvisReveal;	// gpGlobals time of last cloak break/reveal (bot visibility API) — sprint drain gate
	char	m_bSgtcCorpseLatched;	// ref +0x4ea — CopyToBodyQue once per death
	char	m_bShieldLarge;		// ref +0x460 — big shield sizing on spawn
	char	m_bSgtcPad461;		// ref +0x461 (unused in .so)
	char	m_bSgtcBadGuy;		// ref +0x462 — ??-BAD-GUY-?? netname trap @0x1397a7
	char	m_bNukeViewSnap;	// ref +0x463 — SgtcThink copies view_ofs @0x1398ac
	char	m_bWeapSubModel;	// ref +0x925 — SubModel disp flag (WeapDispSet 1/2)
	char	m_bZatLatch;		// ref +0x928 — set Zat @0x1337a2, cleared TakeDamage @0x13876d
	char	m_bSnagInvisLatch;	// ref +0x4de — invis snag latch (Spawn copies +0x4df)
	char	m_bSnagInvisFlag;	// ref +0x4df — armed by invis weapon, copied to latch on Spawn
	float	m_flSgtcSnagTimer;	// reetou SearchInvisible arming (CReetou secondary +0.3s path)
	int	m_fSgtcPushLatch;	// main secondary AoE stamp (ref vfunc +0x18c arg 0x25)
	int	m_iStamina;		// ref +0x43c — HUD Stamina usermsg byte
	int	m_iStaminaSent;		// ref +0xa24 — last value sent to client
	int	m_iSgtcAmmoDispLatch;	// ref +0xa1c — CZat::Deploy energy HUD latch
	int	m_iSgtcAmmoDispMax;	// ref +0xa20 — paired max (100 on zat deploy)
	unsigned short	m_usBeamStop;	// ref +0xa30 — events/mains.sc precache handle (StartDeathCam)
	char	m_bLanceStunFlag;	// ref +0xa3e — set by CLanceProj::ProjTouch @0xfeb57
	float	m_flShieldTraceTime;	// ref +0xa68 — shield TraceAttack timer (CheckTimeBasedDamage)
	float	m_flMoveLatch;		// ref +0xa6c — stamina tick gate (now+interval)
	float	m_flLanceStunEnd;	// ref +0xa74 — now+0.2 (rodata 0x1d7530)
	float	m_flMaxSpeedLatch;	// ref +0xa7c — pev->fuser2 / maxspeed snapshot
	char	m_bShieldTraceHit;	// ref +0xa78 — set TraceAttack @0x137256
	char	m_bZatTouchFlag;	// ref +0xa88 — set CBasePlayer::Zat @0x1338e2
	float	m_flCloakTickNext;	// ref +0xa90 — cloak pulse deadline (rodata +0.2s @0x1d754a)
	float	m_flSgtcTailTick;	// ref +0x424 — 0.5s gate for SgtcThink tail @0x13a88a..0x13b274
	int	m_iHudInfoMode;		// ref +0x434 — HudInfoMode usermsg byte
	char	m_bSnagArmorHud;	// ref +0x449 — snag-invis armor HUD arm (never written in .so)
	char	m_bSgtcPePreset;	// ref +0x465 — preset PE colors @0x13b28f
	float	m_flSgtcLatch490;	// ref +0x490 — copy of +0x3fc for concuss edge
	int	m_iConcussLatch;	// ref +0x508 — Concuss usermsg armed
	int	m_iHudInfoModeSent;	// ref +0x50c — last HudInfoMode byte sent
	float	m_flSgtcLatch400;	// ref +0x400 — clears +0x434 when == time
	int	m_iSnagArmorCap;	// ref +0x908 — armor HUD scale cap (coujaf sets 100)
	float	m_flSnagArmorRamp;	// ref pev+0x1bc — armor HUD ramp (coujaf seeds)
	int	m_iArmorHudSent;	// ref +0x91c — last Armor bar byte sent
	int	m_iSupergunsSent;	// ref +0x920 — last superguns aux sent
	int	m_iHudRoundTime;	// ref +0x4d0 — last SetRndTim roundtime sent
	int	m_iHudRoundMode;	// ref +0x4d8 — last SetRndTim mode sent
	int	m_iSgtcTeam;		// ref +0x438 — SGTC team id (SgtcTeam: 1 Torri, 2 Goa'uld; NOT the pev->team numbering)
	int	m_iSgtcActiveTeam;	// ref +0x430 (SgtcTeam)
	int	m_iSgtcPrevTeam;	// ref +0x4fc (SgtcTeam)
	int	m_iSgtcPortraitSel;	// ref +0xa44 — portrait menu selection (classe arg 1)
	int	m_iSgtcPortraitShown;	// ref +0xa40 — shown portrait = class in play (SgtcClass 1..5, 0 none); also Talent() switch index
	int	m_iSgtcPortraitPrev;	// ref +0xa38 — snapshot of +0xa40 on re-pick (classe)
	int	m_iSgtcInfoLock;	// ref +0xdc — class-limits/info lock gate (ClientUserInfoChanged)
	int	m_iSgtcClientIdx;	// ref +0x3e8 — "client" cmd arg: radio peer index
	int	m_iSgtcCustomColor;	// ref +0x450 — "customcolor" cmd arg
	char	m_bSgtcPad464;		// ref +0x464
	char	m_bSgtcMenuLock;	// ref +0x4e5 — gond4h9wp menu lock
	char	m_bSgtcPortraitsFrozen;	// ref +0x4e6 — portraits frozen latch
	char	m_bSgtcMenuLockLatch;	// ref +0x4e7 — menu lock edge latch
	int	m_iSgtcGondState;		// ref +0x4f4 — gond weapon-set state machine (0/1/2/3) driven
								//  by menu args 0x11/0x12 inside GiveFromMenu
	int	m_iSgtcCouteauThrowLatch;	// ref +0x4f8 — couteau PA/throw (0/1/2); 2→0 on Deploy/PA/SA
	float	m_flSgtcMenuWindow;	// ref +0x4ac — menu window deadline (GiveFromMenu/PlayerThink)
	char	m_bSgtcPadA3D;		// ref +0xa3d
	char	m_bSgtcRoundAlive;	// ref +0x3e4 — seen-alive latch
	char	m_bSgtcLatchA9c;	// ref +0xa9c — DM superweapon once-per-spawn latch
	int	m_iSgtcTicks;		// ref +0xc88 (int handle)
	EHANDLE	m_hSgtcEnvIterator;	// port-side env walk handle (ref +0xc88 slot reuse)
	int	m_iSgtcEnvStage;	// ref +0xc90
	float	m_flSgtcEnvNext;	// ref +0xc94
	int	m_iSgtcAmmoLatch904;	// ref +0x904 — shared SGTC ammo-transfer latch (CLarve PA/SA/WeaponIdle drain)
	float	m_flSgtcLatch48c;	// ref +0x48c — CInvis secondary shield-load cooldown gate
	float	m_flSgtcMenuDelay;	// ref +0x4b0
	float	m_flSgtcTeamsTick;	// ref +0x41c
	int	m_iSgtcSnapClass;	// ref +0x480 — last class counted (FPlayerCanRespawn gate)
	int	m_iSgtcSnapTeam;	// ref +0x484 — last pev->team counted (FPlayerCanRespawn gate)
	int	m_iNVGOn;		// ref +0xe18 — NVG active
	int	m_iNVGZoom;		// ref +0xe20 — NVG zoom level 0..5
	int	m_iSgtcSpecMode;	// ref +0x914 — special/dev mode unlock (-1 locked)
	int	m_iSgtcElite;		// ref +0xa54 — "elite" arg
	char	m_bSgtcSatchelFired;	// ref +0x910 — c4detonate latch
	char	m_bSgtcPortraitLatch;	// ref +0x926 — ResetPortrait clears
	char	m_bSgtcSuperman;	// ref +0x924 — savetheday armed
	float	m_flSgtcStuck;		// ref +0x420 — "stuck" reuse deadline
	float	m_flSgtcLastSniperFire;	// last sniper shot (scoped sway + death-punch window)
	float	m_flSgtcLastSniperHurt;	// last damage taken (scoped sway input)
	float	m_flTalentCooldown;	// ref +0x494 — per-talent reuse deadline
	int	m_iSgtcTalentDeploy;	// ref +0x918 — deployed/armed count (shieldemit, trapbox, zatboom, larve)
	float	m_flSgtcLatchA8c;	// ref +0xa8c — talent-case timer
	char	m_bSgtcLatchA94;	// ref +0xa94 — CTBD latch: renderamt=20 after 0.5s
	float	m_flSgtcLatchA98;	// ref +0xa98 — +0xa94 latch timer
	int	m_iSgtcGondArgs[SGTC_NUM_WEAPON_SLOTS + 1];	// ref +0x9ac — gond4h9wp progress copy; NewRound
								//  type-4 reset writes idx 0..0x1c (29 entries) so the ref
								//  itself overruns a 28-slot tail — field widened to match
	char	m_bSgtcPad3e3;		// ref +0x3e3 — DM/CP spawn clears it
	char	m_bSgtcInWorld;		// ref +0x44b — set on spawn when !observer
	int	m_iSgtcPrevClass;	// ref +0x500 — last spawned class (+0xa40 at resend)
	int	m_iSgtcPrevPeTeam;	// ref +0x504 — last spawned pev->team (+0x1ac)
	int	m_iSgtcSubmodelIdx;	// ref +0x440 — team body selector fed to ChangeSubmodel
	float	m_flSgtcAttackEnd;	// ref +0xa28 — CP type-4 attack deadline (roundstart + mp_attackcountdown)
	int	m_iSgtcPad4ac;		// ref +0x4ac — CP type-4 spawn clears it
	int	m_iSgtcFlagsCa8;	// ref +0xca8 — spawn flag word (&= ~0xb)
	void ChangeSubmodel( void );	// ref @0x153e88 — +0x440 body write + SubModel msg

	void SetHudRoundTime( int roundtime, int mode );	// ref @0x125494 — MSG_ONE gmsgSetHudRoundTime
	void ShowVGUIMenu( int menu );	// ref @0x125538 — MSG_ONE gmsgVGUIMenu
	void SendAboutMe( int a, const char *s, int c );	// ref @0x1262b8 — s unused in ref
	void SendGlobalTextIndex( int idx );	// ref @0x129026 — MSG_ONE gmsgConcuss
	void NVG( void );			// ref @0x13444a
	void NVG_Zoomin( void );	// ref @0x1345d4
	void NVG_Zoomout( void );	// ref @0x13442a
	void Talent( void );		// ref @0x134a02
	void ResetPortrait( int mode );	// ref @0x13633e — portrait msg + clears +0x926
	void TogglePortrait( void );	// ref @0x146412 — MSG_ONE TogglePor to owner


	void SGTC_RadarScan( void );	// ref @0x135592 — Blip usermsg to client HUD
	void SGTC_UpdateStamina( void );	// ref @0x1336e8
	void SGTC_ShieldThinker( void );	// ref @0x1390b2 (partial port)
	void SGTC_WeapDispSet( int mode );	// ref @0x133aca — MSG_ONE SubModel
	void SGTC_SendProgressIfDirty( void );	// ref @0x139f18 — MSG_ONE gmsgProgress
	void SGTC_TickLanceStunLatch( void );	// ref @0x139814 — lance stun timer + latch
	void SGTC_TickCloakPulse( void );	// ref @0x139b5d — cloak pulse while +0x448
	void SGTC_TickSpawnCampProximity( void );	// ref @0x139c13 — info_player_goauld/torri scan
	void SGTC_TickZatTouchMaintain( void );	// ref @0x139db0 — pev+0x160==1 zat FX sustain
	void SGTC_TickClearZatShieldPe( void );	// ref @0x139e51 — restore armor/render after zat
	void SGTC_TickNukeChargeExpiry( void );	// ref @0x139edb — clear +0x45c when charge time elapsed
	void SGTC_TickNukeViewSnap( void );	// ref @0x1398ac — pev+0x174 view_ofs (0,0,-10)
	void SGTC_TickHurtInvertPeFx( void );	// ref @0x1399cf — hurtinvert / recallall PE colors
	void SGTC_TickShieldEnergyAndMaxspeed( void );	// ref @0x13a15c — shield pev+0x14c, maxspeed
	void SGTC_SyncShieldEnergyFromContents( void );	// ref @0x13a3a5 — PointContents → pev+0x14c
	void SGTC_TryBombKeyCampWarn( void );	// ref @0x1398ef — MSG_ONE HasKey camp warn
	void SGTC_SyncBombZoneHasKey( void );	// ref @0x13a438 — HasKey on zone/key transitions
	void SGTC_TickSgtcThinkTail( void );	// ref @0x13a88a..0x13b31f — post-bomb-key tail
	void SGTC_MenuselectBombKey( int sel );	// ref ClientCommand @0xcb456 — menuselect <=4
	void SGTC_SgtcThink( void );	// ref @0x13963a — called from PostThink @0x1405b3
	void Zat( entvars_t *pevInflictor );	// ref @0x13378a — zat_proj / zatboom
	void SGTC_ToggleVis( void );	// ref @0x136454 — invis rendermode latch (+0x4e9)
	void SnagInvis( void );	// ref @0x1336b8
	void SGTC_TryShieldTraceHit( entvars_t *pevAttacker, TraceResult *ptr );	// ref TraceAttack @0x13720e
	int SGTC_IsArmorMetalHit( int iHitgroup ) const;
	void SGTC_ArmorMetalImpact( const Vector &vecPos );
	void SGTC_TickShieldTraceLatch( void );	// ref CheckTimeBasedDamage @0x13e995

	virtual void Spawn( void );
	void Pain( void );

	//virtual void Think( void );
	virtual void Jump( void );
	virtual void Duck( void );
	virtual void PreThink( void );
	virtual void PostThink( void );
	virtual Vector GetGunPosition( void );
	virtual int TakeHealth( float flHealth, int bitsDamageType );
	virtual void TraceAttack( entvars_t *pevAttacker, float flDamage, Vector vecDir, TraceResult *ptr, int bitsDamageType);
	virtual int TakeDamage( entvars_t* pevInflictor, entvars_t* pevAttacker, float flDamage, int bitsDamageType);
	virtual void	Killed( entvars_t *pevAttacker, int iGib );
	virtual Vector BodyTarget( const Vector &posSrc ) { return Center( ) + pev->view_ofs * RANDOM_FLOAT( 0.5, 1.1 ); };		// position to shoot at
	virtual void StartSneaking( void ) { m_tSneaking = gpGlobals->time - 1; }
	virtual void StopSneaking( void ) { m_tSneaking = gpGlobals->time + 30; }
	virtual BOOL IsSneaking( void ) { return m_tSneaking <= gpGlobals->time; }
	virtual BOOL IsAlive( void ) { return (pev->deadflag == DEAD_NO) && pev->health > 0; }
	virtual BOOL ShouldFadeOnDeath( void ) { return FALSE; }
	virtual	BOOL IsPlayer( void ) { return TRUE; }			// Spectators should return FALSE for this, they aren't "players" as far as game logic is concerned

	virtual BOOL IsNetClient( void ) { return TRUE; }		// Bots should return FALSE for this, they can't receive NET messages
															// Spectators should return TRUE for this
	virtual const char *TeamID( void );

	virtual int		Save( CSave &save );
	virtual int		Restore( CRestore &restore );
	void RenewItems(void);
	void PackDeadPlayerItems( void );
	void RemoveAllItems( BOOL removeSuit );
	BOOL SwitchWeapon( CBasePlayerItem *pWeapon );

	// JOHN:  sends custom messages if player HUD data has changed  (eg health, ammo)
	virtual void UpdateClientData( void );
	
	static	TYPEDESCRIPTION m_playerSaveData[];

	// Player is moved across the transition by other means
	virtual int		ObjectCaps( void ) { return CBaseMonster :: ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }
	virtual void	Precache( void );
	BOOL			IsOnLadder( void );
	BOOL			FlashlightIsOn( void );
	void			FlashlightTurnOn( void );
	void			FlashlightTurnOff( void );

	void UpdatePlayerSound ( void );
	void DeathSound ( void );

	int Classify ( void );
	void SetAnimation( PLAYER_ANIM playerAnim );
	void SetWeaponAnimType( const char *szExtention );
	char m_szAnimExtention[32];

	// custom player functions
	virtual void ImpulseCommands( void );
	void CheatImpulseCommands( int iImpulse );

	void StartDeathCam( void );
	void StartObserver( Vector vecPosition, Vector vecViewAngle );
	void StopObserver();

	void AddPoints( int score, BOOL bAllowNegativeScore );
	void AddPointsToTeam( int score, BOOL bAllowNegativeScore );
	BOOL AddPlayerItem( CBasePlayerItem *pItem );
	BOOL RemovePlayerItem( CBasePlayerItem *pItem, bool bCallHoster );
	void DropPlayerItem ( char *pszItemName );
	BOOL HasPlayerItem( CBasePlayerItem *pCheckItem );
	BOOL HasNamedPlayerItem( const char *pszItemName );
	BOOL HasPlayerItemFromID( int nID );
	BOOL HasWeapons( void );// do I have ANY weapons?
	void SelectPrevItem( int iItem );
	void SelectNextItem( int iItem );
	void SelectLastItem(void);
	void SelectItem(const char *pstr);
	void ItemPreFrame( void );
	void ItemPostFrame( void );
	void GiveNamedItem( const char *szName );
	void EnableControl(BOOL fControl);

	int  GiveAmmo( int iAmount, const char *szName, int iMax );
	void SendAmmoUpdate(void);

	void WaterMove( void );
	void EXPORT PlayerDeathThink( void );
	void PlayerUse( void );

	void CheckSuitUpdate();
	void SetSuitUpdate( const char *name, int fgroup, int iNoRepeat );
	void UpdateGeigerCounter( void );
	void CheckTimeBasedDamage( void );

	BOOL FBecomeProne ( void );
	void BarnacleVictimBitten ( entvars_t *pevBarnacle );
	void BarnacleVictimReleased ( void );
	static int GetAmmoIndex(const char *psz);
	int AmmoInventory( int iAmmoIndex );
	int Illumination( void );

	void ResetAutoaim( void );
	Vector GetAutoaimVector( float flDelta  );
	Vector AutoaimDeflection( Vector &vecSrc, float flDist, float flDelta  );

	void ForceClientDllUpdate( void );  // Forces all client .dll specific data to be resent to client.

	void DeathMessage( entvars_t *pevKiller );

	void SetCustomDecalFrames( int nFrames );
	int GetCustomDecalFrames( void );

	void TabulateAmmo( void );

	float m_flStartCharge;
	float m_flAmmoStartCharge;
	float m_flPlayAftershock;
	float m_flNextAmmoBurn;// while charging, when to absorb another unit of player's ammo?

	// Player ID
	void InitStatusBar( void );
	void UpdateStatusBar( void );
	int m_izSBarState[SBAR_END];
	float m_flNextSBarUpdateTime;
	float m_flStatusBarDisappearDelay;
	char m_SbarString0[SBAR_STRING_SIZE];
	char m_SbarString1[SBAR_STRING_SIZE];

	void SetPrefsFromUserinfo( char *infobuffer );

	float m_flNextChatTime;

	int m_iAutoWepSwitch;

	Vector m_vecLastViewAngles;

	// TAC retrieval (gameplay additions, outside the reference layout):
	// carried damage per tac type (-1 = nothing carried) and the short
	// placement lock applied after a tac is picked back up.
	int	m_iSgtcTacCarryHealth[3];
	float	m_flSgtcTacPlaceBlock;

	// Pack/loadout auto-select (gameplay addition, outside the reference layout).
	float	m_flSgtcPackDeadline;
};

#define AUTOAIM_2DEGREES  0.0348994967025
#define AUTOAIM_5DEGREES  0.08715574274766
#define AUTOAIM_8DEGREES  0.1391731009601
#define AUTOAIM_10DEGREES 0.1736481776669

extern int gmsgHudText;
extern BOOL gInitHUD;

#endif // PLAYER_H
