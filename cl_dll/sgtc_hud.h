// sgtc_hud.h — StargateTC custom HUD element (client.dll @ FUN_10072e5f hooks).
#pragma once
#if !defined( SGTC_HUD_H )
#define SGTC_HUD_H

#include "hud.h"
#include "sgtc_enums.h"	// SgtcTeam / SgtcEntTeam / SgtcClass / SgtcWeaponSlot, shared with dlls/ (game_shared)

// The Progress message carries one state byte per loadout weapon slot, in
// SgtcWeaponSlot order (the name is historic; they are not radar blips).
#define SGTC_BLIP_SLOTS SGTC_NUM_WEAPON_SLOTS
#define SGTC_RADAR_SLOTS 256
#define SGTC_PORTRAIT_SLOTS 33
#define SGTC_ANNOUNCE_LINES 5

// ref client.dll: SGTC HUD state on CHudSgtc (offsets from REA decompile).
class CHudSgtc : public CHudBase
{
public:
	int Init( void );
	int VidInit( void );
	void Think( void );
	int Draw( float flTime );
	void DrawRoundTimer( float flTime ); // ref @0x10075d2d — paint after full HUD list (Xash)
	void Reset( void );

	int MsgFunc_ZatTouch( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_Gtickets( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_Ttickets( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_TicketMode( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_HudInfoMode( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_Concuss( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_Stamina( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_SubModel( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_HasKey( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_Radio( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_Armor( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_Blip( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_Progress( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_HideHud( const char *pszName, int iSize, void *pbuf );
	// portrait / round messages — drain extended fields when present
	int MsgFunc_AddPortrait( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_Reset( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_AboutMe( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_TogglePor( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_SetRndTim( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_NewRnd( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_ClearEnv( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_EnvFull( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_EnvRain( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_EnvFount( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_NewLens( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_AddRing( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_ChgPartSt( const char *pszName, int iSize, void *pbuf );

	int m_iGtickets;
	int m_iTtickets;
	int m_iHudInfoMode;		// ref +0x54 (HudInfoMode, FUN_10076bce)
	int m_iStamina;
	int m_iSubModel;
	int m_iHasKey;
	int m_iRadioTeam;
	int m_iRadioMsg;

	int m_iArmorTeamTint;		// ref DAT_100f71d4 (Armor subs 3/4/5)
	int m_iTacDispActive;		// ref DAT_100f70d0 (Armor sub 10 on, sub 4 off)

	float m_flArmorBar;		// ref +0x2c
	int m_iArmorAux;		// ref DAT_100f71d8
	int m_iArmorOverlay;		// ref +0x1684
	int m_iArmorState;		// ref +0x1690
	int m_iArmorParam;		// ref +0x168c
	float m_flArmorTacPhase;	// ref tri-strip +0x14 (anim clock @0x100739bd)
	float m_flArmorTacStart;	// ref slide-in origin (state 1→2 @0x100738a6)

	int m_iBlipPulse;		// ref +0x1670 (Draw pulse 0..0x9b; vote tint @0x100762ba)
	int m_iBlipProgress;		// ref +0x1674 (pbar fill; Progress msg @0x10076ccc)
	float m_flBlipProgressLatch;	// ref +0x1678 (bar ramp clock @0x100750e7)
	char m_cBlipPulseDir;		// ref byte +0x1685 (pulse direction @0x100761f1)

	int m_iRadarFocusX;		// ref +0x169c (Blip type 0x32)
	int m_iRadarFocusY;		// ref +0x16a0
	int m_iRadarType[SGTC_RADAR_SLOTS];	// ref +0x564
	int m_iRadarX[SGTC_RADAR_SLOTS];	// ref +0x960
	int m_iRadarY[SGTC_RADAR_SLOTS];	// ref +0xd5c
	int m_iRadarAlpha[SGTC_RADAR_SLOTS];	// ref +0x1158

	int m_fZatTouchActive;		// ref +0x16f4
	float m_flZatTouchTime;		// ref +0x16f8

	// ref +0x1704/+0x1708 — Think-fed ammo counters shown in the ticket-dock counter row
	// (ref Draw @0x10075609-0x10075718 reads these, not +0x5c/+0x60 tickets).
	int m_iDockAmmoT;			// ref +0x1704 (DrawHudNumber @0x82)
	int m_iDockAmmoG;			// ref +0x1708 (DrawHudNumber @0x46)
	int m_iWeaponIconSpr;		// ref +0x1714 — "sgtcweapon" HSPRITE of current weapon (set by Think)
	wrect_t m_rcWeaponIcon;		// ref +0x1718 — cached weapon-icon rect (ref sprite-cache record)
	int m_iArmorFlag1688;		// ref +0x1688 — cleared by Reset msg (+0x1688=0 @0x10076f88)

	int m_iHideHud;

	// ref @0x100768d4 — AddPortrait record (short,long,short×6,string,byte)
	int m_iPortraitSprite;		// +0x24
	int m_iPortraitEnt;		// +0x3c
	float m_flPortraitTime;		// +0x28
	int m_iPortraitX;		// +0x40  (server ent index)
	int m_iPortraitHp[3];		// +0x44/48/4c
	char m_szPortraitName[32];
	int m_iPortraitId;		// last byte
	int m_iPortraitActiveTeam;	// ref DAT_100f70c8 (AddPortrait team filter; SgtcEntTeam, the server writes pev->team: 1 Goa'uld, 2 Torri)

	int m_iAboutMeSelf;		// ref +0x20
	int m_iAboutMePlayer;		// ref +0x40
	int m_iKeyMode;			// ref +0x34
	float m_flKeyTime;		// ref +0x30
	int m_iRoundTimerOn;		// ref +0x16f0 (round timer armed; Draw gate @0x10075dd2)
	int m_iRoundTimerMode;		// ref +0x70 — SetRndTim BYTE (0 off, 1 live, 2 spectator)
	float m_flRoundTime;		// ref +0x16ec
	float m_flRoundStart;		// ref +0x16e8
	int m_iTicketModeFlag;		// ref +0x58 (TicketMode msg)
	int m_iTicketDockFrame;		// ref +0x1694 (slide phase counter 0..10)
	int m_iTicketSlideOffset;	// ref +0x1698 (dock sub-offset 0..0x7d)
	int m_iTicketSlideRemain;	// ref DAT_100f6fb0 (slide-in distance, starts 0xc00)
	char m_bTicketDockLayout78;	// ref ticket tick @0x10074c3a — draw 0x78/0x78/0x32
	char m_bTicketSlide256;	// ref ebp-0xc4 — slidein 256×130 @0x10074ed7
	char m_bTicketSlide190;	// ref ebp-0xc0 — slidein 190×190 @0x10074f24
	char m_cTicketSlideDir;		// ref ebp-0xb8 this frame (1 = use +0x1698 for draw)
	int m_iAmmoLineSlide;		// ref +0x1554 — ticket/ammoline entrance offset (0..0x1c)
	int m_iAmmoLineStep;		// ref +0x1558 — entrance delay counter (to 0x46)
	int m_iKeyBannerSlide;		// ref +0x155c — key/hud_top slide (0..0x9f)
	int m_iKeyBannerStep;		// ref +0x1560 — key entrance counter (to 0x46)
	int m_iProgress;		// ref +0x38 (Progress msg)
	int m_iProgressIcon;		// ref DAT_100e1b94
	int m_iProgressTeam[SGTC_BLIP_SLOTS];	// ref DAT_100e1aa4[28]
	int m_iProgressEnd;		// ref DAT_100e1b98
	int m_iProgressTeamUiState[SGTC_BLIP_SLOTS];	// ref DAT_100e1b1c[28]
	int m_iProgressIconCursor;	// ref DAT_100e1b90 (cumulative icon strip offset)
	int m_iProgressSlotX[SGTC_BLIP_SLOTS];	// ref DAT_100e1a34[28] (0..13 top, 14..27 bot @+0x38)

	// ref CHudSgtc::Draw @0x10074366 — 5-line announce scroller (+0x16a4/+0x16b8/+0x16cc).
	const char *m_szAnnounceTok[SGTC_ANNOUNCE_LINES];
	float m_flAnnounceLatch[SGTC_ANNOUNCE_LINES];
	int m_iAnnounceScroll[SGTC_ANNOUNCE_LINES];

	// ref FUN_10072502 / FUN_10072694 portrait ring
	int m_iPortraitSlotId[SGTC_PORTRAIT_SLOTS];		// +0x64
	float m_flPortraitSlotHpPct[SGTC_PORTRAIT_SLOTS];	// +0xe4 (param_2)
	int m_iPortraitSlotTeam[SGTC_PORTRAIT_SLOTS];		// +0x164
	int m_iPortraitSlotAux[SGTC_PORTRAIT_SLOTS];		// +0x1e4
	int m_iPortraitSlotEnt[SGTC_PORTRAIT_SLOTS];		// +0x264
	int m_iPortraitSlotHp[SGTC_PORTRAIT_SLOTS][3];		// +0x2e4/+0x364/+0x3e4
	char m_szPortraitSlotName[SGTC_PORTRAIT_SLOTS][32];	// +0x464 (strdup in ref)
	int m_iPortraitSlotColor[SGTC_PORTRAIT_SLOTS];		// +0x4e4
	int m_iPortraitSlotFlash[SGTC_PORTRAIT_SLOTS];		// +0x1568
	int m_iPortraitSlotGray[SGTC_PORTRAIT_SLOTS];		// +0x15ec
};

extern CHudSgtc g_SgtcHud;

// ref client.dll globals DAT_100babe4 / DAT_100babe0 / DAT_100f71e0 (vote UI).
extern int g_iSgtcVoteCount;
extern int g_iSgtcVoteSel;
extern float g_flSgtcVoteAnim;
extern int g_iSgtcAnnouncePending;	// ref DAT_100f1ce8 (Concuss byte 2)

#endif