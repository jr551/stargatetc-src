// sgtc_vgui_panels.cpp — StargateTC VGUI menus 10, 11, 12 and 60.
//
// Port of the four SGTC-only CMenuPanel screens the mod's client.dll adds to
// the TFC viewport (briefing, team+class select, weapon loadout, pack select).
// Evidence (ref = stargatetc/cl_dlls/client.dll, sha256 9ec27e22…4fb3, Ghidra
// 12.1.4):
//
//   menu 10  CSgtcBriefingPanel   ctor FUN_1003f640 @0x1003f640,
//                                 refresh FUN_100404a0 @0x100404a0 (vtable
//                                 +0x210, called from Open @0x10040740),
//                                 reset FUN_10040433 @0x10040433.
//                                 Shown by ShowVGUIMenu case 10 via
//                                 FUN_100482af (pre-created panel).
//   menu 12  CSgtcWeaponSelPanel  ctor FUN_100261c0 @0x100261c0,
//                                 Reset FUN_10029888, Open FUN_1002982b,
//                                 Update FUN_10028323, shown by case 0xc via
//                                 FUN_10047ebb (viewport +0xa9c); Apply sends
//                                 "gond4h9wp <28 values>" (0x100b3ee4).
//   menu 60  CSgtcPackSelPanel    ctor FUN_10029ea0 @0x10029ea0, shown by
//                                 case 0x3c via FUN_10047fb8 (viewport
//                                 +0xaa0); buttons #PACK_1..#PACK_4,
//                                 #Annuler, #Apply.
//   menu 11  CSgtcTeamClassPanel  ctor FUN_1002bec0 @0x1002bec0,
//                                 refresh FUN_1002edd2 @0x1002edd2 (vtable
//                                 +0x210, called from Open @0x1002ef88),
//                                 reset FUN_1002ed4a @0x1002ed4a.
//                                 Shown by ShowVGUIMenu case 0xb via
//                                 FUN_100483ac (pre-created panel).
//
// Verified strings (search_strings/read_bytes on the ref):
//   0x100b4cb4 "title", 0x100b4cbc "#objectif_manquant", 0x100b4cd0/4 "#Ok"
//   0x100b4164 "Identifiez-vous", 0x100b41a0 "Choississez d'abords une classe"
//   0x100b41c0 "Stargate TC , B.I.G. & Endolaylith"
//   0x100b4174 "fichier sg1 absent", 0x100b4188 "fichier goauld absent"
//   0x100b43a0 "fichier absent %s"
//   0x100b41e4 "#Torri", 0x100b41ec "#Goauld", 0x100b41f4 "#Annuler",
//   0x100b4200 "#AutoAssign", 0x100b420c "#Spectateur"
//   0x100b4218 "spectate", 0x100b4224 "autopick", 0x100b42d8 "classe %d",
//   0x100b42e4 "jointeam %d"
//   0x100b4230 "oneill".."esclave" (info file base names),
//   0x100b4280 "#Oneill"..#Esclave (localised button labels),
//   0x100b42f8.. "info_oneill.txt" .. "info_esclave.txt",
//   0x100b43c4 "info_equipe.txt", 0x100b43d4 "info_sg1.txt",
//   0x100b43e4 "info_goauld.txt"
//
// Verified commands (ref ctor assembly):
//   class cell (row 0..1, col 0..4): "classe %d" with col+1 (1..5) and
//   "jointeam %d" with row+1 (1 = Torri row, 2 = Goauld row), then close.
//   AutoAssign "autopick", Spectateur "spectate", Annuler close.
//   Those jointeam ids are what our server accepts (dlls/sgtc_rules.h
//   SGTC_TEAM_TORRI 1 / SGTC_TEAM_GOAULD 2, CStargateTeamDM::ClientCommand).
//
// Both panels need the real widget stack (Label/Button/ScrollPanel/TextPanel)
// and the real TeamFortressViewport, so this file compiles only when the tree
// ships them (SGTC_REAL_VGUI, see sgtc_vgui.h); the VGUI-less tree keeps the
// console stubs in sgtc_vgui.cpp and this translation unit is empty.
//
// NEW file — sgtc_* only, no stock edits.
// Headers used: hud.h, cl_util.h, camera.h, kbutton.h, const.h, sgtc_vgui.h,
//               vgui_int.h, vgui_TeamFortressViewport.h

#include "hud.h"
#include "cl_util.h"

// cl_util.h defines fabs() as a function-like macro. MSVC implements
// std::abs(double) in <cstdlib> as "return _CSTD fabs(_Xx);", so any <cstdlib>
// parsed after this point (here pulled in by <typeinfo>) becomes
// "std::((_Xx) > 0 ? ...)" and fails with C2589 at cstdlib(24). This TU never
// calls fabs and <cmath> already supplies the real function, so drop the macro
// for the rest of the file.
#undef fabs

#include "camera.h"
#include "kbutton.h"
#include "const.h"
#include "sgtc_vgui.h"

#ifdef SGTC_REAL_VGUI

#include "vgui_int.h"
#include "vgui_TeamFortressViewport.h"
#include "l10n.h"
#include "demo_api.h"
#include <VGUI_TextImage.h>		// gEngfuncs.pDemoAPI->IsPlayingback()

#include <stdio.h>
#include <string.h>
#include <typeinfo>

// ---------------------------------------------------------------------------
// shared helpers
// ---------------------------------------------------------------------------

// gEngfuncs.COM_LoadFile wrapper (ref: (*DAT_100e7864)(name, 5, NULL)).
// Caller frees with COM_FreeFile.
static char *SgtcPanels_LoadFile( const char *pszFile )
{
	return (char *)gEngfuncs.COM_LoadFile( (char *)pszFile, 5, NULL );
}

// Show a SGTC panel through the real viewport, the same way
// TeamFortressViewport::ShowVGUIMenu does for the stock menus.
//
// Ref chain semantics (ShowVGUIMenu @0x10047b39 + :79 live evidence):
//   - received menus stay chained off m_pCurrentMenu; a same-id resend is
//     deduped over the WHOLE chain (no re-install, no re-show, no re-Reset —
//     the 1s menu-11 resend must not re-hide the class list under the cursor
//     nor bury a menu-10 briefing that is on top);
//   - a new id installs BEHIND the front panel — never HideAll+Close of the
//     visible menu (orig: menu 10 stayed up while 11s resend queued, then the
//     queued 11 surfaced when 10 closed);
//   - the front's own close pops it (ref dedup would block any hidden copy
//     anyway), AND leaves a "dismissed" mark: a same-id resend after a user
//     dismiss does NOT re-open the menu (orig: menu-11 Annuler stays closed
//     for 60s+ under the 1s resend);
//   - the dismissal clears the next time ANY menu front is dismissed (orig:
//     menu-10 Ok -> the next menu-11 resend re-popped 11).
static int s_iSgtcDismissedMenu = 0;	// SGTC menu id last closed by the user
static float s_flSgtcDismissedAt = 0.0f;	// gEngfuncs.GetClientTime() at that close
// The map-briefing window (menu id 4 on the wire; panel reads it as
// SGTC_MENU_MAPBRIEFING) must not cover the weapon loadout or re-pop after the
// player closed it. The server re-sends the briefing on the next round-state
// tick (John: "the motd pops up after weapon selection"), so keep a 60s
// suppression window and never let it install over the loadout screens.
static float s_flSgtcBriefingDismissedAt = -1000.0f;
#define SGTC_BRIEFING_SUPPRESS_SEC 60.0f

// Set when the server auto-grants a pack (see SgtcPanels_LoadoutDone): a menu
// 12/60 install that arrives *after* the grant is stale (the server opened the
// loadout panel just as the timer fired) and is swallowed once, so it cannot
// cover the now-armed player and steal keyboard focus.
static bool s_bLoadoutGranted = false;

static bool SgtcPanels_IsLoadoutMenu( int iMenu )
{
	return iMenu == SGTC_MENU_WPSEL || iMenu == SGTC_MENU_PACKSEL;
}

// The user-close mark only suppresses the *immediate* server resend (the
// dead-observer menu-11 resend runs at 1 Hz). The ref has no dismissal memory:
// ShowVGUIMenu dedups against the live menu chain only (REA
// .rea/spawn_vgui_rea.md), so a later explicit re-show — e.g. vguimenuyoyo 12
// after Close, which the orig honours — must install. Without a window the mark
// blocked every same-id re-show for the rest of the level.
#define SGTC_DISMISS_GUARD_SEC 2.0f

// ids the stock TeamFortressViewport owns (2..9). SGTC ids are 10/11/12/60.
static bool SgtcPanels_IsStockMenuId( int iMenu )
{
	return iMenu >= SGTC_MENU_TEAM && iMenu <= SGTC_MENU_SPECHELP;
}

// True when a fresh VGUIMenu(iMenu) may install: the id is neither already in
// the current-menu chain (ref dedup) nor the id the user just dismissed.
static bool SgtcPanels_ShouldShow( int iMenu )
{
	// Briefing must not sit on top of the weapon loadout, and must stay down
	// for a while after the player closed it (the server re-sends it).
	if ( iMenu == SGTC_MENU_BRIEFING )
	{
		if ( ( gEngfuncs.GetClientTime() - s_flSgtcBriefingDismissedAt ) < SGTC_BRIEFING_SUPPRESS_SEC )
			return false;
		if ( gViewPort->m_pCurrentMenu && SgtcPanels_IsLoadoutMenu( gViewPort->m_pCurrentMenu->GetMenuID() ) )
			return false;
	}

	if ( s_iSgtcDismissedMenu == iMenu )
	{
		if ( ( gEngfuncs.GetClientTime() - s_flSgtcDismissedAt ) < SGTC_DISMISS_GUARD_SEC )
			return false;
		s_iSgtcDismissedMenu = 0;	// guard expired: a later show is legitimate
	}

	for ( CMenuPanel *p = gViewPort->m_pCurrentMenu; p; p = p->GetNextMenu() )
	{
		if ( p->GetMenuID() == iMenu )
			return false;
	}
	return true;
}


static bool SgtcPanels_IsOurPanel( CMenuPanel *p );	// defined near HideAll

// Build-18 VGUI input fix: only the FRONT menu panel may accept mouse input.
// All SGTC panels are full-screen transparent overlays, so a chained-behind
// panel's background used to consume clicks meant for the visible menu
// (John: menus "take a moment then respond", clicks often lost). Implemented
// near HideAll where the static panel pointers are in scope.
static void SgtcPanels_ApplyInputState( void );
static void SgtcPanels_SetMenuCursor( bool bMenuOpen );

// Opt-in click diagnostics (same gate as sgtc_test: sv_cheats >= 1 AND
// sgtc_test_enable > 0). One line per click: coords, hit panel's menu id and
// the client time, so a Mac run can prove whether a click reaches the panel at
// all and which menu owns it. Silent when disabled.
class CSgtcHandler_InputDiag : public InputSignal
{
public:
	void cursorEntered( Panel *panel ) {}
	void cursorMoved( int x, int y, Panel *panel )
	{
		s_iLastX = x;
		s_iLastY = y;
	}
	void mousePressed( MouseCode code, Panel *panel )
	{
		if( gEngfuncs.pfnGetCvarFloat( "sv_cheats" ) < 1.0f )
			return;
		if( gEngfuncs.pfnGetCvarFloat( "sgtc_test_enable" ) <= 0.0f )
			return;

		int iW = 0, iH = 0;
		if( panel )
			panel->getSize( iW, iH );
		gEngfuncs.Con_Printf( "[sgtc] click x=%d y=%d code=%d panel=%dx%d t=%.2f front=%d\n",
			s_iLastX, s_iLastY, (int)code, iW, iH, gEngfuncs.GetClientTime(),
			gViewPort && gViewPort->m_pCurrentMenu ? gViewPort->m_pCurrentMenu->GetMenuID() : 0 );
	}
	void mouseReleased( MouseCode code, Panel *panel ) {}
	void mouseDoublePressed( MouseCode code, Panel *panel ) {}
	void cursorExited( Panel *panel ) {}
	void mouseWheeled( int delta, Panel *panel ) {}
	void keyPressed( KeyCode code, Panel *panel ) {}
	void keyTyped( KeyCode code, Panel *panel ) {}
	void keyReleased( KeyCode code, Panel *panel ) {}
	void keyFocusTicked( Panel *panel ) {}

private:
	static int s_iLastX;
	static int s_iLastY;
};
int CSgtcHandler_InputDiag::s_iLastX = 0;
int CSgtcHandler_InputDiag::s_iLastY = 0;
// The SGTC server never sends stock TFC menu ids (2..9); any such node in the
// chain is a leftover singleton that would pop over an SGTC screen when the
// front advances (the "stale menu-2" ghost). Drop hidden stock nodes before
// installing an SGTC menu; a stock menu currently on top is left alone —
// closing it legitimately reveals what is queued behind.
static void SgtcPanels_PruneStockMenus( void )
{
	while ( gViewPort->m_pCurrentMenu
		&& !gViewPort->m_pCurrentMenu->isVisible()
		&& SgtcPanels_IsStockMenuId( gViewPort->m_pCurrentMenu->GetMenuID() ) )
	{
		gViewPort->m_pCurrentMenu = gViewPort->m_pCurrentMenu->GetNextMenu();
	}

	for ( CMenuPanel *p = gViewPort->m_pCurrentMenu; p && p->GetNextMenu(); )
	{
		CMenuPanel *pNext = p->GetNextMenu();
		if ( !pNext->isVisible() && SgtcPanels_IsStockMenuId( pNext->GetMenuID() ) )
			p->SetNextMenu( pNext->GetNextMenu() );
		else
			p = pNext;
	}
}

static void SgtcPanels_ShowMenu( CMenuPanel *pNewMenu, int iMenu )
{
	if ( !pNewMenu || !gViewPort )
		return;

	// Don't open menus in demo playback (ref ShowVGUIMenu early-out).
	if ( gEngfuncs.pDemoAPI->IsPlayingback() )
		return;

	// Same-id resend while shown/queued/dismissed: drop it entirely — no
	// Reset, no re-install (ref chain dedup + dismissal, see above).
	if ( !SgtcPanels_ShouldShow( iMenu ) )
	{
		gEngfuncs.Con_Printf( "[sgtc] ShowMenu %d dedup (dismissed=%d)\n",
			iMenu, s_iSgtcDismissedMenu );
		return;
	}

	if ( s_bLoadoutGranted && SgtcPanels_IsLoadoutMenu( iMenu ) )
	{
		// Belt-and-braces: the loadout was already granted, so a late pack /
		// weapon panel is stale. Swallow this one install (the flag clears so a
		// later legitimate open still works).
		s_bLoadoutGranted = false;
		gEngfuncs.Con_Printf( "[sgtc] ShowMenu %d swallowed (loadout already granted)\n", iMenu );
		return;
	}

	gViewPort->HideCommandMenu();
	SgtcPanels_PruneStockMenus();

	// After class selection the server sends the weapon loadout while the
	// briefing is still front, so the loadout installed "behind" it and the
	// player never saw weapon selection (John). Surface the loadout: close the
	// briefing first and mark it dismissed so its next resend stays down.
	if ( SgtcPanels_IsLoadoutMenu( iMenu ) )
	{
		// Briefing can be the front panel OR queued behind the team/class
		// screen; either way it must not cover the loadout. Pop it when it is
		// front and unlink any queued copy, so the loadout surfaces as soon as
		// the team/class panel closes.
		for ( int i = 0; i < 4 && gViewPort->m_pCurrentMenu; i++ )
		{
			if ( gViewPort->m_pCurrentMenu->GetMenuID() != SGTC_MENU_BRIEFING )
				break;
			s_flSgtcBriefingDismissedAt = gEngfuncs.GetClientTime();
			gEngfuncs.Con_Printf( "[sgtc] ShowMenu %d pops briefing (front)\n", iMenu );
			gViewPort->HideTopMenu();
		}
		bool bUnlinked = false;
		for ( CMenuPanel *p = gViewPort->m_pCurrentMenu; p && p->GetNextMenu(); )
		{
			if ( p->GetNextMenu()->GetMenuID() == SGTC_MENU_BRIEFING )
			{
				s_flSgtcBriefingDismissedAt = gEngfuncs.GetClientTime();
				p->SetNextMenu( p->GetNextMenu()->GetNextMenu() );
				bUnlinked = true;
			}
			else
			{
				p = p->GetNextMenu();
			}
		}
		if ( bUnlinked )
			gEngfuncs.Con_Printf( "[sgtc] ShowMenu %d unlinked queued briefing\n", iMenu );
	}

	pNewMenu->SetMenuID( iMenu );
	pNewMenu->SetActive( true );
	pNewMenu->setParent( gViewPort );

	if ( gViewPort->m_pCurrentMenu )
	{
		// Chain behind the front panel like stock ShowVGUIMenu — the
		// visible menu keeps focus and this one surfaces when it closes.
		gViewPort->m_pCurrentMenu->SetNextMenu( pNewMenu );
	}
	else
	{
		gViewPort->SetCurrentMenu( pNewMenu );	// sets m_pCurrentMenu + Open()
	}
	gViewPort->UpdateCursorState();
	SgtcPanels_ApplyInputState();
	SgtcPanels_SetMenuCursor( true );
	if ( gViewPort->m_pCurrentMenu && SgtcPanels_IsOurPanel( gViewPort->m_pCurrentMenu ) )
		gViewPort->m_pCurrentMenu->requestFocus();

	gEngfuncs.Con_Printf( "[sgtc] ShowMenu %d installed%s (front id %d)\n", iMenu,
		gViewPort->m_pCurrentMenu ? " behind" : " front",
		gViewPort->m_pCurrentMenu ? gViewPort->m_pCurrentMenu->GetMenuID() : 0 );
}


// Close handler for SGTC "Annuler"/"Ok" buttons — the ref-equivalent of the
// menu's own close. A plain HideTopMenu pop lets the server's pending-menu
// resend (menu 11 fires every 1s for dead observers) re-open the menu
// instantly, which is what made clicks look dead. Recording the dismissed id
// makes SgtcPanels_ShouldShow drop same-id resends until a different menu
// front is dismissed — matching the orig (Close stays closed under resend;
// menu-10 Ok lets a resend re-pop 11). Deliberately NOT used on the join
// buttons (spectate/autopick/classe): those go through
// CMenuHandler_StringCommand/HIDE_TEXTWINDOW so a failed pick still lets the
// resend re-open the menu for another try.
class CSgtcHandler_DismissMenu : public ActionSignal
{
public:
	virtual void actionPerformed( Panel *panel )
	{
		if ( gViewPort && gViewPort->m_pCurrentMenu )
		{
			s_iSgtcDismissedMenu = gViewPort->m_pCurrentMenu->GetMenuID();
			s_flSgtcDismissedAt = gEngfuncs.GetClientTime();
			if ( s_iSgtcDismissedMenu == SGTC_MENU_BRIEFING )
				s_flSgtcBriefingDismissedAt = s_flSgtcDismissedAt;
			gEngfuncs.Con_Printf( "[sgtc] DismissMenu id %d\n", s_iSgtcDismissedMenu );
		}
		if ( gViewPort )
			gViewPort->HideTopMenu();
	}
};

// Join-action companion to CSgtcHandler_DismissMenu: the join buttons
// (spectate/autopick/classe cells) go through CMenuHandler_StringCommand +
// CMenuHandler_TextWindow(HIDE_TEXTWINDOW), which close the menu without
// recording a dismissal. A dead-observer VGUIMenu(11) resend that lands after
// the pop but before the spawn's IsAlive() gate closes the window then
// re-installs menu 11 over the live first-person view (spawn race —
// .rea/spawn_vgui_rea.md). Marking the front id FIRST (this signal precedes
// the command/close signals in the button's list) makes that late resend
// dedup exactly like an Annuler dismiss; the mark still clears on the next
// different-menu front dismissal, so Ok/dismiss re-pop semantics survive.
class CSgtcHandler_JoinMark : public ActionSignal
{
public:
	virtual void actionPerformed( Panel *panel )
	{
		if ( gViewPort && gViewPort->m_pCurrentMenu )
		{
			s_iSgtcDismissedMenu = gViewPort->m_pCurrentMenu->GetMenuID();
			s_flSgtcDismissedAt = gEngfuncs.GetClientTime();
			gEngfuncs.Con_Printf( "[sgtc] JoinMark id %d\n", s_iSgtcDismissedMenu );
		}
	}
};

//============================================================================
// Weapon-selection economy state — exact port of the ref client.dll globals
// the menu-12 panel reads (all BSS in the ref, filled by two usermessages):
//   DAT_100dd974[28]  per-slot cost       (TeamNames tail, 28 BYTE reads)
//   DAT_100ddad4      budget              (Progress byte 2)
//   DAT_100dd9e4[28]  per-slot state      (Progress bytes 3..30; 0/1/2)
//   DAT_100ddad8      reset-request flag  (Progress last byte)
//   DAT_100dda5c[28]  selection flags     (0 free, 1 selected, 2 blocked)
//   DAT_100ddad0      used points, DAT_100dda58 primaries, DAT_100dda54 specials
//   DAT_100f225c      armor mode byte     (Armor message; gates slots 0/1)
//============================================================================

// Slot numbers and group limits are the SgtcWeaponSlot values shared with the
// server (game_shared/sgtc_enums.h), so both sides validate the same loadout.
#define SGTC_WEP_SLOTS	SGTC_NUM_WEAPON_SLOTS
#define SGTC_WEP_PRIM	SGTC_WSLOT_NUM_PRIMARY	// slots 0..13 use DAT_100dd974
#define SGTC_WEP_MAX_PRIM	SGTC_WSLOT_MAX_PRIMARY	// ref: DAT_100dda58 < 3
#define SGTC_WEP_MAX_SPECIAL	SGTC_WSLOT_MAX_SPECIAL	// ref: DAT_100dda54 < 2 (c4 + gas grenades)

static int s_iWepCost[SGTC_WEP_SLOTS];
static int s_iWepState[SGTC_WEP_SLOTS];
static int s_iWepFlag[SGTC_WEP_SLOTS];
static int s_iWepBudget;
static int s_iWepUsed;
static int s_iWepPrimCount;
static int s_iWepSpecialCount;
static int s_iWepResetFlag;
static int s_iArmorMode;

// ref: the special-count group = c4 (slot 20) plus the four gas grenades
// (slots 23..26) — Update secondary loop `(0x16 < i+0xe && i+0xe < 0x1b) || i == 6`.
static bool SgtcWep_IsSpecialSlot( int iSlot )
{
	return SGTC_WeaponSlotIsSpecial( iSlot ) != 0;
}

// Forward decls — the panels below implement these.
static void SgtcWepSel_ApplyServerState( void );

// iCount is the number of tail bytes actually present after the team strings.
// The per-connect TeamNames carries a 28-byte zero pad tail (ref InitHUD), but
// CStargateUnas::RecountTeams(TRUE) broadcasts the same message with no tail at
// all. READ_BYTE past the end returns 0, which used to wipe every cost for the
// life of the map with no re-broadcast. A short tail is now ignored, never
// applied, so a tail-less recount cannot clear a good cost table.
void SgtcWepSel_SetCosts( const unsigned char *pCosts, int iCount )
{
	if ( !pCosts || iCount < SGTC_WEP_SLOTS )
		return;
	for ( int i = 0; i < SGTC_WEP_SLOTS; i++ )
		s_iWepCost[i] = pCosts[i];	// ref: READ_BYTE per slot
	SgtcWepSel_Refresh();
}

void SgtcWepSel_SetProgress( int iBudget, const unsigned char *pStates, int iReset )
{
	s_iWepBudget = iBudget;			// ref DAT_100ddad4
	if ( pStates )
	{
		for ( int i = 0; i < SGTC_WEP_SLOTS; i++ )
			s_iWepState[i] = pStates[i];	// ref DAT_100dd9e4
	}
	s_iWepResetFlag = iReset;		// ref DAT_100ddad8
	SgtcWepSel_Refresh();
}

void SgtcWepSel_SetArmorMode( int iMode )
{
	s_iArmorMode = iMode;			// ref DAT_100f225c
}

void SgtcWepSel_Refresh( void )
{
	// ref FUN_1004910b calls vtable+0x210 (Update) on the three SGTC panels;
	// FUN_10028323/FUN_1002a7a7 honour DAT_100ddad8 by running Reset first.
	if ( !gViewPort )
		return;
	SgtcWepSel_ApplyServerState();
}

//============================================================================
// Menu 10 — briefing/objective window (ref FUN_1003f640 / FUN_100404a0)
//============================================================================
class CSgtcBriefingPanel : public CMenuPanel
{
public:
	CSgtcBriefingPanel( int iTrans, int iRemoveMe, int x, int y, int wide, int tall );

	virtual void Open( void );	// refresh labels + briefing, then base Open
	virtual void Reset( void );	// re-arm the briefing load latch

private:
	void Refresh( void );

	CTransparentPanel *m_pWindow;
	Label *m_pTitle;
	Label *m_pServerName;
	Label *m_pMapName;
	CTFScrollPanel *m_pScrollPanel;
	TextPanel *m_pBriefing;
	CommandButton *m_pOKButton;
	bool m_bBriefingLoaded;
};

CSgtcBriefingPanel::CSgtcBriefingPanel( int iTrans, int iRemoveMe, int x, int y, int wide, int tall )
	: CMenuPanel( iTrans, iRemoveMe, x, y, wide, tall )
{
	// ref schemes: "Title Font" / "Briefing Text" (the third handle it looks up,
	// "Team Info Text", is unused by the briefing panel).
	CSchemeManager *pSchemes = gViewPort->GetSchemeManager();
	SchemeHandle_t hTitleScheme = pSchemes->getSchemeHandle( "Title Font" );
	SchemeHandle_t hBriefingText = pSchemes->getSchemeHandle( "Briefing Text" );
	int r, g, b, a;

	// Background TextPanel + LineBorder (ref +0xf8: 0x15,0x15, W-0x2a, H-0x3c).
	// The ref passes &DAT_100e126c as the panel colour; that object is in BSS,
	// its only reference is this ctor (xrefs 0x100e126c -> 0x1003f715) and
	// nothing ever writes it, so it is Color(0,0,0,0) = opaque black — the orig
	// briefing background is black, not the vgui default grey.
	TextPanel *pBackground = new TextPanel( "", XRES( 21 ), YRES( 21 ),
		ScreenWidth - XRES( 42 ), ScreenHeight - YRES( 60 ) );
	pBackground->setParent( this );
	pBackground->setBgColor( 0, 0, 0, 0 );
	// Ref border colour: Color(0,0,0xff,0) = solid blue (asm @0x1003f943).
	pBackground->setBorder( new LineBorder( Color( 0, 0, 255, 0 ) ) );

	// Inner window (ref +0xdc) with its own LineBorder — ctor asm @0x1003f855:
	// X(40), Y(40)+0x5a, X(560), Y(0x1009be28=360)-0x32. Same blue border.
	m_pWindow = new CTransparentPanel( 255, XRES( 40 ), YRES( 40 ) + 90,
		XRES( 560 ), YRES( 360 ) - 50 );
	m_pWindow->setParent( this );
	m_pWindow->setBorder( new LineBorder( Color( 0, 0, 255, 0 ) ) );

	// Banner (ref +0xec): the ctor builds an image Label, not a text label —
	// FUN_100339e3("title", x, y) = Label("") + setContentFitted(true) +
	// setImage(LoadTGAForRes(name)). The name is a *file* base name:
	// LoadTGAForRes builds "gfx/vgui/<res>_title.tga" with the ref ladder
	// (FUN_10033160: <640 -> 320, <1024 -> 640, else 1024), so 1024x768+ loads
	// gfx/vgui/1024_title.tga — the "STARGATETC" banner art (400x104, the
	// measured white core 315x44 matches the orig capture exactly). The ref
	// then parents it, setVisible(true) (asm 0x1003fa43 pushes 1 — the label
	// is VISIBLE) and setPos(ScreenWidth/2 - 0xc8, 0x23) at asm 0x1003fa5x.
	m_pTitle = new Label( "", ScreenWidth / 2 - 200, 35 );
	m_pTitle->setContentFitted( true );
	m_pTitle->setImage( LoadTGAForRes( "title" ) );
	m_pTitle->setParent( this );
	m_pTitle->setVisible( true );
	// FreeVGUI's Label inherits Panel's default background (sc_secondary3 =
	// (204,204,204)), which would draw a grey box around the banner; the orig
	// shows the art alone over the black panel, so the background is off.
	m_pTitle->setPaintBackgroundEnabled( false );

	// Server name (ref +0xe8) and map name (ref +0xe4) labels: Title Font,
	// fg 0x55,0x73,0xb9, bg black, content alignment 3. In the SDK enum the mod
	// was built against that value is Label::a_west (a_northwest=0, a_north,
	// a_northeast, a_west) — not centred, contrary to the port spec's note.
	m_pServerName = new Label( "", XRES( 16 ), YRES( 16 ) - 10 );
	m_pServerName->setParent( m_pWindow );
	m_pServerName->setFont( pSchemes->getFont( hTitleScheme ) );
	m_pServerName->setFgColor( 0x55, 0x73, 0xb9, 0 );
	m_pServerName->setBgColor( 0, 0, 0, 0xff );
	m_pServerName->setContentAlignment( vgui::Label::a_west );

	m_pMapName = new Label( "", XRES( 16 ), YRES( 16 ) + 40 );
	m_pMapName->setParent( m_pWindow );
	m_pMapName->setFont( pSchemes->getFont( hTitleScheme ) );
	m_pMapName->setFgColor( 0x55, 0x73, 0xb9, 0 );
	m_pMapName->setBgColor( 0, 0, 0, 0xff );
	m_pMapName->setContentAlignment( vgui::Label::a_west );

	// Scroll panel (ref +0xd8) + TextPanel (ref +0xf0, "Briefing Text" scheme
	// font, fg *hardcoded* 0x55,0x73,0xb9, bg from the scheme) holding the map
	// briefing. Ref asm: setFont(scheme) → (+0x120)(0x55,0x73,0xb9,0) →
	// getBgColor(scheme) → setBgColor(...). The orig briefing text is the same
	// blue as the labels, not the scheme's orange FgColor.
	// Default text is the localised "#objectif_manquant" key, exactly as the ref
	// sets it (FUN_1007ad9c(s__objectif_manquant_100b4cbc)).
	//
	// Ref geometry (FUN_1003f640 asm @0x1003fe00..0x1003ff69):
	//   scroll  = ( X(16), Y(48)+0x2d, X(0x1009be24=560)-2*X(16), Y(0x1009be20=296)-0x5a )
	//   text    = ( 0, 0x2d, X(560)-X(16), Y(296)-0x5a )
	// The client TextPanel is larger than the client clip on both axes, so
	// ScrollPanel's auto-visible mode shows BOTH scrollbars at runtime — the
	// orig panel shows the vertical bar on the right and the horizontal bar at
	// the bottom of the text area, even though the ctor calls
	// setScrollBarVisible(0,0).
	const int iScrollWide = XRES( 560 ) - 2 * XRES( 16 );
	const int iScrollTall = YRES( 296 ) - 90;

	m_pScrollPanel = new CTFScrollPanel( XRES( 16 ), YRES( 48 ) + 45, iScrollWide, iScrollTall );
	m_pScrollPanel->setParent( m_pWindow );
	m_pScrollPanel->setScrollBarVisible( false, false );
	m_pScrollPanel->setScrollBarAutoVisible( false, false );	// kill the auto thick gold bar

	m_pBriefing = new TextPanel( "", 0, 45, XRES( 560 ) - XRES( 16 ), YRES( 296 ) - 90 );
	m_pBriefing->setParent( m_pScrollPanel->getClient() );
	m_pBriefing->setFont( pSchemes->getFont( hBriefingText ) );
	m_pBriefing->setFgColor( 0x55, 0x73, 0xb9, 0 );
	pSchemes->getBgColor( hBriefingText, r, g, b, a );
	m_pBriefing->setBgColor( r, g, b, a );
	m_pBriefing->setText( L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( "#objectif_manquant" ) ) );

	// OK button (ref +0xd4, localised "#Ok", close handler id 0xb). The ref
	// builds two variants (asm @0x100400b5): W < 0x400 (low-res) uses
	// x=X(320)-X(62), y=H-Y(40)-Y(12)+5, w=X(124), h=Y(24)-10; the wide variant
	// uses y=H-Y(40)-Y(12), h=Y(24) with the same x/w.
	const int iOkX = XRES( 320 ) - XRES( 62 );
	const int iOkW = XRES( 124 );
	int iOkY;
	int iOkH;
	if ( ScreenWidth < 0x400 )
	{
		iOkY = ScreenHeight - YRES( 40 ) - YRES( 12 ) + 5;
		iOkH = YRES( 24 ) - 10;
	}
	else
	{
		iOkY = ScreenHeight - YRES( 40 ) - YRES( 12 );
		iOkH = YRES( 24 );
	}

	m_pOKButton = new CommandButton( L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( "#Ok" ) ),
		iOkX, iOkY, iOkW, iOkH );
	m_pOKButton->setParent( this );
	m_pOKButton->addActionSignal( new CSgtcHandler_DismissMenu() );

	m_bBriefingLoaded = false;

	m_pScrollPanel->validate();
}

void CSgtcBriefingPanel::Open( void )
{
	Refresh();
	CMenuPanel::Open();
}


void CSgtcBriefingPanel::Reset( void )
{
	CMenuPanel::Reset();
	m_bBriefingLoaded = false;
}

void CSgtcBriefingPanel::Refresh( void )
{
	// Server name arrives in the ServerName usermsg (viewport m_szServerName,
	// MsgFunc_ServerName @0x1004a0a9); the ref fills the label from the same
	// value (FUN_100404a0 "%s").
	m_pServerName->setText( "%s", gViewPort->m_szServerName );

	if ( m_bBriefingLoaded )
	{
		m_pScrollPanel->validate();
		return;
	}

	// Map name + briefing file from the level name (ref FUN_100404a0):
	// "maps/<map>.bsp" → map label "<map>", briefing file "maps/<map>.txt".
	const char *pszLevel = gEngfuncs.pfnGetLevelName();
	if ( !pszLevel || !pszLevel[0] )
		return;

	const char *pszBase = pszLevel;
	const char *pszSlash = strrchr( pszLevel, '/' );
	if ( !pszSlash )
		pszSlash = strrchr( pszLevel, '\\' );
	if ( pszSlash )
		pszBase = pszSlash + 1;

	char szMapName[64];
	strlcpy( szMapName, pszBase, sizeof( szMapName ) );
	char *pch = strrchr( szMapName, '.' );
	if ( pch )
		*pch = 0;
	m_pMapName->setText( "%s", szMapName );

	char szBriefingFile[256];
	strlcpy( szBriefingFile, pszLevel, sizeof( szBriefingFile ) );
	pch = strrchr( szBriefingFile, '.' );
	if ( pch )
		*pch = 0;
	strlcat( szBriefingFile, ".txt", sizeof( szBriefingFile ) );

	char *pfile = SgtcPanels_LoadFile( szBriefingFile );
	if ( pfile )
	{
		// Missing briefing keeps the localised "#objectif_manquant" default,
		// like the ref (it only calls setText when COM_LoadFile succeeded).
		m_pBriefing->setText( pfile );
		gEngfuncs.COM_FreeFile( pfile );
	}
	m_bBriefingLoaded = true;

	// Ref never resizes the text panel; ScrollPanel's auto-visible mode picks up
	// the overflow on validate().
	m_pScrollPanel->validate();

}

//============================================================================
// Menu 11 — "Identifiez-vous" team + class select (ref FUN_1002bec0 / FUN_1002edd2)
//============================================================================

#define SGTC_TEAMCLASS_TEAMS		2	// Torri column, Goauld column
#define SGTC_TEAMCLASS_PER_TEAM		5	// class cells per column
#define SGTC_TEAMCLASS_COUNT		( SGTC_TEAMCLASS_TEAMS * SGTC_TEAMCLASS_PER_TEAM )
#define SGTC_TEAMCLASS_INFO_LEN		1024

// ref class tables: "oneill".."esclave" (0x100b4230) are the info file base
// names, "#Oneill".."#Esclave" (0x100b4280) the localised cell labels. The ref
// rodata carries the full paths as "info/<name>.txt" (0x100b42f8..0x100b439c),
// not the "info_<name>.txt" the port used to build.
struct SgtcClassEntry
{
	const char *pszLabel;	// titles.txt key used as the button text
	const char *pszInfo;	// info file loaded into the cell's info panel
	const char *pszTga;	// LoadTGAForRes name for the hover portrait (+0x188)
};

static const SgtcClassEntry s_SgtcClasses[SGTC_TEAMCLASS_COUNT] =
{
	{ "#Oneill",  "info/oneill.txt",  "oneill"  },	// row 0 (Torri): 0x100b42f8
	{ "#Carter",  "info/carter.txt",  "carter"  },
	{ "#Jackson", "info/jackson.txt", "jackson" },
	{ "#Tealc",   "info/tealc.txt",   "tealc"   },
	{ "#Soldat",  "info/soldat.txt",  "soldat"  },
	{ "#Dieu",    "info/dieu.txt",    "dieu"    },	// row 1 (Goauld): 0x100b434c
	{ "#Anubis",  "info/anubis.txt",  "anubis"  },
	{ "#Jaffa",   "info/jaffa.txt",   "jaffa"   },
	{ "#Prima",   "info/prima.txt",   "prima"   },
	{ "#Esclave", "info/esclave.txt", "esclave" }
};
class CSgtcTeamClassPanel : public CMenuPanel
{
public:
	CSgtcTeamClassPanel( int iTrans, int iRemoveMe, int x, int y, int wide, int tall );

	virtual void Reset( void );
	virtual void SetActiveInfo( int iInput );	// hovered class cell (0..9)
	virtual void paintTraverse( bool repaint );

	void SetActiveTeam( int iTeam );		// hovered/selected column (0..1)
	// One shared list window + header on the left (ref +0x114/+0x180). Its
	// contents — the 5 class cells, the header text, the team info line —
	// belong to the team last hovered/clicked (SetActiveTeam swaps them). All
	// 10 class cells are born visible at the SAME coords; freevgui's
	// solveTraverse skips invisible children, so the Goa'uld set is hidden
	// inside the first paint() pass (after its first solve) instead of at
	// ctor/Reset time — later setVisible toggles then work normally.
	CTransparentPanel *m_pTeamWindow[SGTC_TEAMCLASS_TEAMS];
	Label *m_pTeamHeader[SGTC_TEAMCLASS_TEAMS];
	TextPanel *m_pTeamInfo[SGTC_TEAMCLASS_TEAMS];
	CommandButton *m_pClassButton[SGTC_TEAMCLASS_COUNT];
	TextPanel *m_pClassInfo[SGTC_TEAMCLASS_COUNT];
	Label *m_pPortrait[SGTC_TEAMCLASS_COUNT];
	CommandButton *m_pTeamButton[SGTC_TEAMCLASS_TEAMS];
	bool m_bFirstPaint;
	CommandButton *m_pCancelButton;
	CommandButton *m_pAutoButton;
	CommandButton *m_pSpecButton;
	TextPanel *m_pFooter;				// info_equipe.txt
	char m_szClassInfo[SGTC_TEAMCLASS_COUNT][SGTC_TEAMCLASS_INFO_LEN];
	int m_iCurrentClass;
	int m_iCurrentTeam;
};

// Hover handlers (ref pcharNode<1>: class cells carry the cell index, the team
// buttons carry 10/11).
class CSgtcHandler_ClassOver : public InputSignal
{
public:
	CSgtcHandler_ClassOver( CSgtcTeamClassPanel *pPanel, int iClass )
	{
		m_pPanel = pPanel;
		m_iClass = iClass;
	}

	void cursorEntered( Panel *panel );

	void cursorMoved( int x, int y, Panel *panel ) {}
	void mousePressed( MouseCode code, Panel *panel ) {}
	void mouseReleased( MouseCode code, Panel *panel ) {}
	void mouseDoublePressed( MouseCode code, Panel *panel ) {}
	void cursorExited( Panel *panel ) {}
	void mouseWheeled( int delta, Panel *panel ) {}
	void keyPressed( KeyCode code, Panel *panel ) {}
	void keyTyped( KeyCode code, Panel *panel ) {}
	void keyReleased( KeyCode code, Panel *panel ) {}
	void keyFocusTicked( Panel *panel ) {}

private:
	CSgtcTeamClassPanel *m_pPanel;
	int m_iClass;
};

// Hover over a team selector box: reveal that team's class list (ref pcharNode
// carries 10/11 for the team boxes; FUN_1002edd2 re-populates the shared list
// on hover, before any click). InputSignal, not ActionSignal.
class CSgtcHandler_TeamOver : public InputSignal
{
public:
	CSgtcHandler_TeamOver( CSgtcTeamClassPanel *pPanel, int iTeam )
	{
		m_pPanel = pPanel;
		m_iTeam = iTeam;
	}

	void cursorEntered( Panel *panel )
	{
		if ( m_pPanel )
			m_pPanel->SetActiveTeam( m_iTeam );
	}
	void cursorMoved( int x, int y, Panel *panel ) {}
	void mousePressed( MouseCode code, Panel *panel ) {}
	void mouseReleased( MouseCode code, Panel *panel ) {}
	void mouseDoublePressed( MouseCode code, Panel *panel ) {}
	void cursorExited( Panel *panel ) {}
	void mouseWheeled( int delta, Panel *panel ) {}
	void keyPressed( KeyCode code, Panel *panel ) {}
	void keyTyped( KeyCode code, Panel *panel ) {}
	void keyReleased( KeyCode code, Panel *panel ) {}
	void keyFocusTicked( Panel *panel ) {}

private:
	CSgtcTeamClassPanel *m_pPanel;
	int m_iTeam;
};
// Click on a team selector box: reveal its class list (the ref's team box is a
// reveal-only control — the server 'jointeam' verb is inert; the real join is
// the cell's "classe <class> <team>"). This is an ActionSignal (mouse click),
// not the hover InputSignal.

class CSgtcHandler_TeamSelect : public ActionSignal
{
public:
	CSgtcHandler_TeamSelect( CSgtcTeamClassPanel *pPanel, int iTeam )
	{
		m_pPanel = pPanel;
		m_iTeam = iTeam;
	}

	void actionPerformed( Panel *panel )
	{
		if ( m_pPanel )
			m_pPanel->SetActiveTeam( m_iTeam );
	}

private:
	CSgtcTeamClassPanel *m_pPanel;
	int m_iTeam;
};


CSgtcTeamClassPanel::CSgtcTeamClassPanel( int iTrans, int iRemoveMe, int x, int y, int wide, int tall )
	: CMenuPanel( iTrans, iRemoveMe, x, y, wide, tall )
{
	// Init before the implicit CMenuPanel::Reset() at the end of this ctor —
	// Reset reads m_iCurrentTeam through the m_bFirstPaint paint path.
	m_iCurrentTeam = -1;
	// ref schemes: "Title Font" (title + team headers) / "Briefing Text" (all
	// info panels, fg 0x55,0x73,0xb9, bg from the scheme).
	CSchemeManager *pSchemes = gViewPort->GetSchemeManager();
	SchemeHandle_t hTitleScheme = pSchemes->getSchemeHandle( "Title Font" );
	SchemeHandle_t hBriefingText = pSchemes->getSchemeHandle( "Briefing Text" );
	int r, g, b, a;

	// Background (ref +0x110: TextPanel 0x15,0x15, W-0x2a, H-0x15 + border).
	// ASM @0x1002c211: setBgColor(0,0,0x32,0x64) — dark navy translucent,
	// NOT the opaque black the earlier BSS-misread suggested.
	TextPanel *pBackground = new TextPanel( "", XRES( 21 ), YRES( 21 ),
		ScreenWidth - XRES( 42 ), ScreenHeight - YRES( 21 ) );
	pBackground->setParent( this );
	pBackground->setBgColor( 0, 0, 0x32, 0x64 );
	pBackground->setBorder( new LineBorder( Color( 0, 0, 255, 0 ) ) );

	// Title (ref +0x1b0, literal French string, not localised) — ctor asm
	// @0x1002bfa3: Label(0,0, X(0x100974e0=640), Y(0x100974e8=21)); fg is the
	// hardcoded periwinkle 0x8c,0x8c,0xf0 (@0x1002c074), not the scheme colour.
	Label *pTitle = new Label( "", 0, 0, XRES( 640 ), YRES( 21 ) );
	pTitle->setParent( this );
	pTitle->setFont( pSchemes->getFont( hTitleScheme ) );
	pTitle->setFgColor( 0x8c, 0x8c, 0xf0, 0 );
	pTitle->setBgColor( 0, 0, 0, 0xff );
	pTitle->setContentAlignment( Label::a_center );
	pTitle->setText( "%s", "Identifiez-vous" );

	// Ref column geometry (orig orig_class.png measured at 1024x768, units /
	// 1.6): the big bordered "Tau'ri"/"Goa'uld" box at top is the TEAM BUTTON
	// (ref +0xd4/+0xd8), a CommandButton; the tall bordered box below is the
	// team list window (ref +0x114/+0x118) that holds the class cells and is
	// hidden until the team is hovered.
	//   team button box = CommandButton(X(127)+i*X(282), Y(42), X(105), Y(40))
	//   list window     = TextPanel(255, X(21)/X(409), Y(105), X(210), Y(241))
	const int iWindowWide = XRES( 210 );
	const int iWindowTall = YRES( 241 );
	const int iClassWide = XRES( 120 );	// cells are full-width buttons
	const int iClassTall = YRES( 26 );
	char *pfile = NULL;

	// Team selector boxes (ref +0xd4/+0xd8) — hover OR click reveals the team.
	for ( int iTeam = 0; iTeam < SGTC_TEAMCLASS_TEAMS; iTeam++ )
	{
		const int iButtonX = XRES( 127 ) + iTeam * XRES( 282 );
		m_pTeamButton[iTeam] = new CommandButton(
			L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString(
				iTeam == 0 ? "#Torri" : "#Goauld" ) ),
			iButtonX, YRES( 42 ), XRES( 105 ), YRES( 40 ) );
		m_pTeamButton[iTeam]->setParent( this );
		m_pTeamButton[iTeam]->setContentAlignment( Label::a_center );
		m_pTeamButton[iTeam]->addActionSignal(
			new CSgtcHandler_TeamSelect( this, iTeam ) );
		m_pTeamButton[iTeam]->addInputSignal(
			new CSgtcHandler_TeamOver( this, iTeam ) );
	}

	// Single shared list window on the left (ref +0x114) + its header label —
	// the header text and the cells/infos inside swap with the active team.
	m_pTeamWindow[0] = new CTransparentPanel( 255, XRES( 21 ), YRES( 105 ),
		iWindowWide, iWindowTall );
	m_pTeamWindow[0]->setParent( this );
	m_pTeamWindow[0]->setPaintEnabled( true );
	m_pTeamWindow[0]->setBorder( new LineBorder( Color( 0, 0, 255, 0 ) ) );
	m_pTeamWindow[1] = NULL;

	m_pTeamHeader[0] = new Label( "", XRES( 21 ) + XRES( 4 ), YRES( 105 ) + YRES( 4 ) );
	m_pTeamHeader[0]->setParent( this );
	m_pTeamHeader[0]->setFont( pSchemes->getFont( hTitleScheme ) );
	pSchemes->getFgColor( hTitleScheme, r, g, b, a );
	m_pTeamHeader[0]->setFgColor( r, g, b, a );
	m_pTeamHeader[0]->setText( "%s", L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( "#Torri" ) ) );
	m_pTeamHeader[1] = NULL;

	// Team info lines share the same slot at the list bottom; the inactive
	// one is toggled off by SetActiveTeam. Both born visible so solveTraverse
	// reaches them; the first paint() hides team 1's (see m_bFirstPaint).
	for ( int iTeam = 0; iTeam < SGTC_TEAMCLASS_TEAMS; iTeam++ )
	{
		m_pTeamInfo[iTeam] = new TextPanel( "", XRES( 21 ) + XRES( 6 ), YRES( 105 ) + YRES( 214 ),
			iWindowWide - XRES( 12 ), YRES( 22 ) );
		m_pTeamInfo[iTeam]->setParent( this );
		m_pTeamInfo[iTeam]->setFont( pSchemes->getFont( hBriefingText ) );
		pSchemes->getFgColor( hBriefingText, r, g, b, a );
		m_pTeamInfo[iTeam]->setFgColor( r, g, b, a );
		m_pTeamInfo[iTeam]->setBgColor( 0, 0, 0, 0xff );
		pfile = SgtcPanels_LoadFile( iTeam == 0 ? "info/sg1.txt" : "info/goauld.txt" );
		if ( pfile )
		{
			m_pTeamInfo[iTeam]->setText( pfile );
			gEngfuncs.COM_FreeFile( pfile );
		}
	}

	// Class cells: all 10 born visible at the SAME slot inside the shared
	// left list — freevgui's solveTraverse only descends into visible
	// children, so they must be visible for the first solve; paint() then
	// hides the Goa'uld set before it ever renders. SetActiveTeam toggles.
	// iClass/5 = team (0 Torri, 1 Goauld).
	for ( int iClass = 0; iClass < SGTC_TEAMCLASS_COUNT; iClass++ )
	{
		const int iTeam = iClass / SGTC_TEAMCLASS_PER_TEAM;	// 0 Torri, 1 Goauld
		const int iCol = iClass % SGTC_TEAMCLASS_PER_TEAM;
		char szCommand[MAX_COMMAND_SIZE];

		const int iCellX = XRES( 21 ) + XRES( 45 );
		const int iCellY = YRES( 105 ) + YRES( 30 ) + iCol * YRES( 40 );
		m_pClassButton[iClass] = new CommandButton(
			L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( s_SgtcClasses[iClass].pszLabel ) ),
			iCellX, iCellY,
			iClassWide, iClassTall );
		m_pClassButton[iClass]->setParent( this );
		// ref signals: a single "classe <class> <team>" command (argv1 = class
		// slot 1..5, argv2 = team 1 Torri / 2 Goauld — the server's inert
		// 'jointeam' verb is not used), then close the menu.
		snprintf( szCommand, sizeof( szCommand ), "classe %d %d",
			iCol + SGTC_CLASS_FIRST, iTeam + SGTC_TEAM_TORRI );
		m_pClassButton[iClass]->addActionSignal( new CSgtcHandler_JoinMark() );
		m_pClassButton[iClass]->addActionSignal( new CMenuHandler_StringCommand( szCommand ) );
		m_pClassButton[iClass]->addActionSignal( new CMenuHandler_TextWindow( HIDE_TEXTWINDOW ) );

		// Mouse-over shows the class blurb (ref pcharNode<1>(this, iClass)).
		m_pClassButton[iClass]->addInputSignal( new CSgtcHandler_ClassOver( this, iClass ) );

		// Per-class info/blurb; parented to the panel (not the clipped window)
		// and placed in the right-hand column, where the orig draws it
		// (measured A/B: borders at 654..989 x 168..553 on 1024x768 →
		// XRES(409), YRES(105), XRES(209), YRES(240)). The old centre-gap
		// placement sat on top of the class portrait. Only one shows at a
		// time (SetActiveInfo). BORN VISIBLE — a born-invisible freevgui
		// child is never solved, so the hide is deferred to the first
		// paintTraverse (m_bFirstPaint), after the child's first solve.
		m_pClassInfo[iClass] = new TextPanel( "", XRES( 409 ), YRES( 105 ),
			XRES( 209 ), YRES( 240 ) );
		m_pClassInfo[iClass]->setParent( this );
		m_pClassInfo[iClass]->setFont( pSchemes->getFont( hBriefingText ) );
		pSchemes->getFgColor( hBriefingText, r, g, b, a );
		m_pClassInfo[iClass]->setFgColor( r, g, b, a );
		m_pClassInfo[iClass]->setBgColor( 0, 0, 0, 0xff );

		// Load the class blurb once; missing file keeps the ref's
		// "fichier absent %s" (with the file name substituted).
		pfile = SgtcPanels_LoadFile( s_SgtcClasses[iClass].pszInfo );
		if ( pfile )
		{
			strlcpy( m_szClassInfo[iClass], pfile, SGTC_TEAMCLASS_INFO_LEN );
			gEngfuncs.COM_FreeFile( pfile );
		}
		else
		{
			snprintf( m_szClassInfo[iClass], SGTC_TEAMCLASS_INFO_LEN,
				"fichier absent %s", s_SgtcClasses[iClass].pszInfo );
		}
	}

	// Class portraits (ref +0x188[10]: image-Labels with the class's
	// gfx/vgui/<res>_<name>.tga, centred in the 136x304 gap at
	// XRES(252),YRES(42) — ctor asm @0x1002dff7..0x1002e0c1). BORN VISIBLE
	// like the blurbs: first paintTraverse hides them; born-invisible would
	// strand them unsolved. SetActiveInfo shows one at a time.
	for ( int iClass = 0; iClass < SGTC_TEAMCLASS_COUNT; iClass++ )
	{
		m_pPortrait[iClass] = new Label( "" );
		m_pPortrait[iClass]->setContentFitted( true );
		m_pPortrait[iClass]->setImage( LoadTGAForRes( s_SgtcClasses[iClass].pszTga ) );
		m_pPortrait[iClass]->setPaintBackgroundEnabled( false );
		m_pPortrait[iClass]->setParent( this );
		int iPw = 0, iPh = 0;
		m_pPortrait[iClass]->getSize( iPw, iPh );
		m_pPortrait[iClass]->setPos(
			XRES( 252 ) + ( XRES( 136 ) - iPw ) / 2,
			YRES( 42 ) + ( YRES( 304 ) - iPh ) / 2 );
	}

	// Footer panel (ref +0x17c info/equipe.txt, fallback literal).
	// Ref ctor asm @0x1002cd1f builds it as TextPanel(0, 0, X(640)-2*X(21),
	// Y(0x10097f74=76)+Y(10)) and parents it to the container at panel+0x144,
	// which the port does not have — with `this` as the parent the ref (0,0)
	// origin lands on the title, so the size is taken from the ref and the
	// origin kept at the previously verified bottom-of-panel position.
	//
	// It is created BEFORE the interactive buttons: freevgui's
	// isWithinTraverse walks children in reverse order (last child gets the
	// first hit), and this transparent full-width footer overlaps the whole
	// bottom button strip — created last it swallowed every team/Annuler/
	// AutoAssign/Spectateur click. Keeping it before the buttons puts it
	// behind them in both traversal and paint order.
	m_pFooter = new TextPanel( "", XRES( 40 ), ScreenHeight - YRES( 104 ),
		ScreenWidth - 2 * XRES( 21 ), YRES( 76 ) + YRES( 10 ) );
	m_pFooter->setParent( this );
	m_pFooter->setFont( pSchemes->getFont( hBriefingText ) );
	pSchemes->getFgColor( hBriefingText, r, g, b, a );
	m_pFooter->setFgColor( r, g, b, a );
	pSchemes->getBgColor( hBriefingText, r, g, b, a );
	m_pFooter->setBgColor( r, g, b, a );
	m_pFooter->setText( "Stargate TC , B.I.G. & Endolaylith" );

	pfile = SgtcPanels_LoadFile( "info/equipe.txt" );
	if ( pfile )
	{
		m_pFooter->setText( pfile );
		gEngfuncs.COM_FreeFile( pfile );
	}

	// Bottom buttons (ref +0x104 Annuler / +0x10c AutoAssign / +0x108
	// Spectateur). Orig footer order (orig_class.png): Spectator | Auto Team |
	// Close, evenly spaced at the bottom. y = ScreenHeight - YRES(33) so the
	// strip sits at the panel bottom, not floating above the credits.
	m_pSpecButton = new CommandButton( L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( "#Spectateur" ) ),
		XRES( 127 ), ScreenHeight - YRES( 33 ), XRES( 105 ), YRES( 28 ) );
	m_pSpecButton->setParent( this );
	m_pSpecButton->addActionSignal( new CSgtcHandler_JoinMark() );
	m_pSpecButton->addActionSignal( new CMenuHandler_StringCommand( "spectate", 1 ) );

	m_pAutoButton = new CommandButton( L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( "#AutoAssign" ) ),
		XRES( 274 ), ScreenHeight - YRES( 33 ), XRES( 105 ), YRES( 28 ) );
	m_pAutoButton->setParent( this );
	m_pAutoButton->addActionSignal( new CSgtcHandler_JoinMark() );
	m_pAutoButton->addActionSignal( new CMenuHandler_StringCommand( "autopick", 1 ) );

	m_pCancelButton = new CommandButton( L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( "#Annuler" ) ),
		XRES( 409 ), ScreenHeight - YRES( 33 ), XRES( 105 ), YRES( 28 ) );
	m_pCancelButton->setParent( this );
	m_pCancelButton->addActionSignal( new CSgtcHandler_DismissMenu() );

	Reset();
}

void CSgtcTeamClassPanel::Reset( void )
{
	// ref FUN_1002ed4a: no class selected; per-class blurbs + portraits
	// hidden. The shared list's contents are NOT toggled here — solveTraverse
	// skips invisible children, so hiding the Goa'uld set inside Reset() (the
	// CMenuPanel ctor calls Reset() before setParent/attach) would strand
	// them unsolved forever. Instead m_bFirstPaint defers the first
	// SetActiveTeam(0) to the paint pass, which runs after the first solve.
	CMenuPanel::Reset();
	m_iCurrentClass = 0;
	m_iCurrentTeam = -1;
	m_bFirstPaint = true;

	// Blurbs/portraits: hide only once attached — a born-invisible freevgui
	// child is never solved, so a pre-attach hide would strand them. The
	// pre-attach case is covered by m_bFirstPaint in paintTraverse.
	if ( getSurfaceBase() )
	{
		for ( int i = 0; i < SGTC_TEAMCLASS_COUNT; i++ )
		{
			m_pClassInfo[i]->setVisible( false );
			m_pPortrait[i]->setVisible( false );
		}
	}

	// ref FUN_1002edd2 clears the armed state of every button.
	for ( int i = 0; i < SGTC_TEAMCLASS_COUNT; i++ )
		m_pClassButton[i]->setArmed( false );
	for ( int i = 0; i < SGTC_TEAMCLASS_TEAMS; i++ )
		m_pTeamButton[i]->setArmed( false );
	m_pCancelButton->setArmed( false );
	m_pAutoButton->setArmed( false );
	m_pSpecButton->setArmed( false );
}

// First traversal after the children have been solved once: force the ref's
// initial state — the menu opens with ONLY the two team selector boxes; the
// shared list window, header, both info lines, all 10 class cells and every
// blurb/portrait stay hidden until the first team hover/click
// (SetActiveTeam). Deferred from Reset() because freevgui never re-solves a
// born-invisible child — solveTraverse skips them before the first solve.
// paintTraverse (not paint) is the hook: it runs every frame once visible,
// while paint() only runs when a repaint is pending.
void CSgtcTeamClassPanel::paintTraverse( bool repaint )
{
	if ( m_bFirstPaint )
	{
		m_bFirstPaint = false;
		m_pTeamWindow[0]->setVisible( false );
		m_pTeamHeader[0]->setVisible( false );
		m_pTeamInfo[0]->setVisible( false );
		m_pTeamInfo[1]->setVisible( false );
		for ( int i = 0; i < SGTC_TEAMCLASS_COUNT; i++ )
		{
			m_pClassButton[i]->setVisible( false );
			m_pClassInfo[i]->setVisible( false );
			m_pPortrait[i]->setVisible( false );
		}
	}

	// Re-assert the revealed team's visibility inside the paint pass itself:
	// setVisible only flips the flag — it marks no repaint and does not
	// re-solve, so a hide/setVisible-toggle done between a child's last
	// solve and this traverse can leave it flagged visible yet undrawn.
	// Doing it here (pre-traversal, every frame) self-repairs any such gap.
	if ( m_iCurrentTeam >= 0 && m_iCurrentTeam < SGTC_TEAMCLASS_TEAMS )
	{
		m_pTeamWindow[0]->setVisible( true );
		m_pTeamHeader[0]->setVisible( true );
		m_pTeamInfo[0]->setVisible( m_iCurrentTeam == 0 );
		m_pTeamInfo[1]->setVisible( m_iCurrentTeam == 1 );

		for ( int i = 0; i < SGTC_TEAMCLASS_COUNT; i++ )
			m_pClassButton[i]->setVisible(
				i / SGTC_TEAMCLASS_PER_TEAM == m_iCurrentTeam );
	}


	CMenuPanel::paintTraverse( repaint );
}


// Mouse is over a class cell: bring up its blurb and portrait (ref

// SetActiveInfo FUN_1002efbc: shows m_pPortrait[i] at +0x188 + i*8 + team*4).
void CSgtcTeamClassPanel::SetActiveInfo( int iInput )
{
	if ( iInput < 0 || iInput >= SGTC_TEAMCLASS_COUNT )
		return;

	m_iCurrentClass = iInput;
	m_iCurrentTeam = iInput / SGTC_TEAMCLASS_PER_TEAM;

	for ( int i = 0; i < SGTC_TEAMCLASS_COUNT; i++ )
	{
		m_pClassInfo[i]->setVisible( i == iInput );
		m_pPortrait[i]->setVisible( i == iInput );
	}

	m_pClassInfo[iInput]->setText( m_szClassInfo[iInput] );
	if ( getSurfaceBase() )
		repaintAll();
}

// Team box hovered/clicked (ref FUN_1002edd2 re-populates the shared list on
// hover): swap header text + team info, show that team's 5 class cells and
// hide the other team's, drop its blurbs/portraits, arm its box.
void CSgtcTeamClassPanel::SetActiveTeam( int iTeam )
{
	m_iCurrentTeam = iTeam;

	if ( iTeam < 0 || iTeam >= SGTC_TEAMCLASS_TEAMS )
		return;
	// Ref FUN_1002edd2: reveal the shared list window + header + info for the
	// activated team (they were all hidden until the first hover/click).
	m_pTeamWindow[0]->setVisible( true );
	m_pTeamHeader[0]->setVisible( true );
	m_pTeamHeader[0]->setText( "%s", L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString(
		iTeam == 0 ? "#Torri" : "#Goauld" ) ) );
	m_pTeamInfo[0]->setVisible( iTeam == 0 );
	m_pTeamInfo[1]->setVisible( iTeam == 1 );

	for ( int i = 0; i < SGTC_TEAMCLASS_COUNT; i++ )
	{
		const bool bSel = ( i / SGTC_TEAMCLASS_PER_TEAM == iTeam );
		m_pClassButton[i]->setVisible( bSel );
		if ( !bSel )
		{
			m_pClassInfo[i]->setVisible( false );
			m_pPortrait[i]->setVisible( false );
		}
	}
	for ( int i = 0; i < SGTC_TEAMCLASS_TEAMS; i++ )
		m_pTeamButton[i]->setArmed( i == iTeam );

	// setVisible alone neither repaints nor re-solves: force both on the
	// revealed subtree NOW (hover enters mid-frame, so the next paint pass
	// would otherwise see stale clipRects / a cleared repaint flag) and let
	// the paintTraverse re-assert keep it sticky afterwards.
	solveTraverse();
	repaint();
	if ( getSurfaceBase() )
		repaintAll();
}

void CSgtcHandler_ClassOver::cursorEntered( Panel *panel )
{
	if ( m_pPanel )
		m_pPanel->SetActiveInfo( m_iClass );
}



//============================================================================
// Menu 12 — weapon loadout ("Identifiez-vous" + weapon picker).
// Ref: ctor FUN_100261c0 @0x100261c0, Reset FUN_10029888 @0x10029888 (vtable
// +0x1fc), Open FUN_1002982b @0x1002982b (vtable +0x200), Update FUN_10028323
// @0x10028323 (vtable +0x210), show helper FUN_10047ebb @0x10047ebb (viewport
// +0xa9c). Apply sends the ref's command word "gond4h9wp" (0x100b3ee4)
// followed by the 28 per-slot values — our server's handler
// (dlls/sgtc_player.cpp:1151, ref @0xcb34c) reads exactly
// CMD_ARGV(1..28) -> m_iSgtcGondArgs[] and SGTC_GiveFromMenu grants the
// matching spawn weapons.
//============================================================================

#define SGTC_WPSEL_SLOTS		SGTC_NUM_WEAPON_SLOTS
#define SGTC_WPSEL_PRIMARY		SGTC_WSLOT_NUM_PRIMARY	// slots 0..13 (ref buttons at +0x200)
#define SGTC_WPSEL_SECONDARY	( SGTC_NUM_WEAPON_SLOTS - SGTC_WSLOT_NUM_PRIMARY )	// slots 14..27 (ref buttons at +0x238)

// ref slot labels @0x100b3940 ("glstaff".."medicgoa") and the localised button
// keys @0x100b3a0c.. ("#WEAPON_GLSTAFF".."#WEAPON_MEDICGOA"); the per-slot info
// files are the ref's "info_<label>.txt" (Update switch @0x100b3cd0..).
struct SgtcWeaponEntry
{
	const char *pszLabel;	// ref slot label (also the info file base name)
	const char *pszKey;	// titles.txt key used as the button text
};

static const SgtcWeaponEntry s_SgtcWeapons[SGTC_WPSEL_SLOTS] =
{
	{ "glstaff",  "#WEAPON_GLSTAFF" },
	{ "usas",     "#WEAPON_USAS12" },
	{ "batsup",   "#WEAPON_BATSUP" },
	{ "m92s",     "#WEAPON_BER" },
	{ "mp5",      "#WEAPON_MP5" },
	{ "psg1",     "#WEAPON_PSG1" },
	{ "p90",      "#WEAPON_P90" },
	{ "spas",     "#WEAPON_SHOTGUN" },
	{ "m16",      "#WEAPON_M16" },
	{ "main",     "#WEAPON_MAIN" },
	{ "zat",      "#WEAPON_ZAT" },
	{ "lance",    "#WEAPON_LANCE" },
	{ "ter",      "#WEAPON_TER" },
	{ "zatarc",   "#WEAPON_ZATARC" },
	{ "tacgun",   "#WEAPON_TACGUN" },
	{ "grenade",  "#WEAPON_HANDGRENADE" },
	{ "mine",     "#WEAPON_MINE" },
	{ "invis",    "#WEAPON_INVIS" },
	{ "invis",    "#WEAPON_SHIELD" },
	{ "gacgoa",   "#WEAPON_GACGOA" },
	{ "c4",       "#WEAPON_C4" },
	{ "larve",    "#WEAPON_LARVE" },
	{ "medic",    "#WEAPON_MEDIC" },
	{ "smoke",    "#WEAPON_SMOKEGRENADE" },
	{ "nerve",    "#WEAPON_NERVEGRENADE" },
	{ "narco",    "#WEAPON_NARCOGRENADE" },
	{ "mustard",  "#WEAPON_MUSTARDGRENADE" },
	{ "medicgoa", "#WEAPON_MEDICGOA" }
};

// ref info/<name>.txt file stems (Update switch @0x100b3cd0..0x100b3e94) —
// these differ from the TGA/preview short names for 12 of the 28 slots.
static const char *s_SgtcWeaponInfo[SGTC_WPSEL_SLOTS] =
{
	"glstaff", "usas", "batsup", "ber", "mp5", "psg1", "p90", "spas",
	"m16", "main", "zat", "lancer", "ter", "zatarc",
	"tacs", "hgrenade", "mines", "invis", "shield", "gacgoa", "c4", "larve",
	"medic", "smoke_g", "nerve_g", "narco_g", "mustard_g", "medicgoa"
};

// Ref menu-12 / bottom buttons (FUN_1003321c ctor @0x1003321c): stock vgui
// Button visuals do NOT match the orig look — the orig draws a flat navy fill
// with a thin blue outline, turning red only while armed/selected (see the
// A/B shots). CommandButton paints an orange scheme outline, so menu 12 gets
// its own lightweight slot-button subclass.
class CSgtcWepButton : public CommandButton
{
public:
	CSgtcWepButton( const char *text, int x, int y, int wide, int tall )
		: CommandButton( text, x, y, wide, tall ) {}

	virtual void paintBackground()
	{
		// dark navy fill (matches the +0x168 background tint) then the outline:
		// blue at rest, red only while the cursor arms the button. A selected
		// slot is shown by its white label, not the outline — the orig's A/B
		// (menu 12: Zat'ni'katel selected, Staff Weapon hovered) draws red only
		// on the hovered row, and Reset returns every row to blue.
		int w, h;
		getPaintSize( w, h );
		drawSetColor( 0, 0, 0x32, 0x20 );
		drawFilledRect( 0, 0, w, h );
		if ( isArmed() )
			drawSetColor( 0xff, 0, 0, 0 );
		else
			drawSetColor( 0, 0, 0xff, 0 );
		drawOutlinedRect( 0, 0, w, h );
	}
};

class CSgtcWeaponSelPanel : public CMenuPanel
{
public:
	CSgtcWeaponSelPanel( int iTrans, int iRemoveMe, int x, int y, int wide, int tall );

	virtual void Reset( void );
	virtual void SetActiveInfo( int iInput );	// hovered slot (0..27)

	// ref FUN_10029888 + DAT_100ddad8 gate: restore the selection from the
	// server's 28 per-slot state bytes (Progress message). bReset is the ref's
	// `if (0 < DAT_100ddad8) Reset()` prologue — the plain Update path
	// (flag 0, e.g. a cost/TeamNames refresh) must not wipe the player's
	// in-progress clicks.
	void ApplyServerState( bool bReset = true );

	void ToggleSlot( int iSlot );			// click: select / deselect
	int SelectedSlotCount( void );
	int IsSlotSelected( int iSlot ) { return ( iSlot >= 0 && iSlot < SGTC_WPSEL_SLOTS ) ? m_iSelected[iSlot] : 0; }

private:
	void UpdateCounters( void );
	void ShowSlotInfo( int iSlot );

	Label *m_pTitle;
	CTFScrollPanel *m_pScroll;
	TextPanel *m_pInfo;
	Label *m_pPreview[SGTC_WPSEL_SLOTS];	// ref +0x2e0 per-slot TGA preview labels
	CommandButton *m_pSlotButton[SGTC_WPSEL_SLOTS];
	TextPanel *m_pSlotCost[SGTC_WPSEL_SLOTS];	// ref cost labels (+0x270 / +0x2a8)
	TextPanel *m_pCounter[3];			// #Total / #Used / #Free
	CommandButton *m_pCancelButton;
	CommandButton *m_pResetButton;
	CommandButton *m_pApplyButton;
	int m_iSelected[SGTC_WPSEL_SLOTS];
	int m_iCurrentSlot;
	char m_szSlotInfo[SGTC_WPSEL_SLOTS][SGTC_TEAMCLASS_INFO_LEN];
};

// Click handler: toggles one slot (ref Update polls the button state and
// flips the slot flag + plays a sound; the port reacts on the action signal).
class CSgtcHandler_SlotClick : public ActionSignal
{
public:
	CSgtcHandler_SlotClick( CSgtcWeaponSelPanel *pPanel, int iSlot )
	{
		m_pPanel = pPanel;
		m_iSlot = iSlot;
	}

	virtual void actionPerformed( Panel *panel );

private:
	CSgtcWeaponSelPanel *m_pPanel;
	int m_iSlot;
};

// Reset handler: clears the whole selection (ref Reset button -> panel Reset).
class CSgtcHandler_WepSelReset : public ActionSignal
{
public:
	CSgtcHandler_WepSelReset( CSgtcWeaponSelPanel *pPanel )
	{
		m_pPanel = pPanel;
	}

	virtual void actionPerformed( Panel *panel );

private:
	CSgtcWeaponSelPanel *m_pPanel;
};

// Apply handler: builds and sends the ref's "gond4h9wp <28 values>" command.
class CSgtcHandler_WepSelApply : public ActionSignal
{
public:
	CSgtcHandler_WepSelApply( CSgtcWeaponSelPanel *pPanel )
	{
		m_pPanel = pPanel;
	}

	virtual void actionPerformed( Panel *panel );

private:
	CSgtcWeaponSelPanel *m_pPanel;
};

class CSgtcHandler_SlotOver : public InputSignal
{
public:
	CSgtcHandler_SlotOver( CSgtcWeaponSelPanel *pPanel, int iSlot )
	{
		m_pPanel = pPanel;
		m_iSlot = iSlot;
	}

	void cursorEntered( Panel *panel );

	void cursorMoved( int x, int y, Panel *panel ) {}
	void mousePressed( MouseCode code, Panel *panel ) {}
	void mouseReleased( MouseCode code, Panel *panel ) {}
	void mouseDoublePressed( MouseCode code, Panel *panel ) {}
	void cursorExited( Panel *panel ) {}
	void mouseWheeled( int delta, Panel *panel ) {}
	void keyPressed( KeyCode code, Panel *panel ) {}
	void keyTyped( KeyCode code, Panel *panel ) {}
	void keyReleased( KeyCode code, Panel *panel ) {}
	void keyFocusTicked( Panel *panel ) {}

private:
	CSgtcWeaponSelPanel *m_pPanel;
	int m_iSlot;
};

CSgtcWeaponSelPanel::CSgtcWeaponSelPanel( int iTrans, int iRemoveMe, int x, int y, int wide, int tall )
	: CMenuPanel( iTrans, iRemoveMe, x, y, wide, tall )
{
	CSchemeManager *pSchemes = gViewPort->GetSchemeManager();
	SchemeHandle_t hTitleScheme = pSchemes->getSchemeHandle( "Title Font" );
	SchemeHandle_t hBriefingText = pSchemes->getSchemeHandle( "Briefing Text" );
	int r, g, b, a;

	// Row-height shrink ladder (ref FUN_100261c0 local_330): 2 below 800px,
	// 1 below 1024px, 0 above. Row pitch (0x25-9s), button h (0x1d-9s).
	int iShrink = 0;
	if ( ScreenWidth < 800 )
		iShrink = 2;
	else if ( ScreenWidth < 1024 )
		iShrink = 1;
	const int iRowPitch = 0x25 - iShrink * 9;
	const int iRowTall = 0x1d - iShrink * 9;

	// Background (ref +0x168: TextPanel 0x15,0x15, W-0x2a, H-0x2a, blue
	// LineBorder). Ref asm @0x10026545: setBgColor(0,0,0x32,0x64) — a dark
	// navy translucent fill (a=100 -> ~60% opaque), not opaque black.
	TextPanel *pBackground = new TextPanel( "", 0x15, 0x15,
		ScreenWidth - 0x2a, ScreenHeight - 0x2a );
	pBackground->setParent( this );
	pBackground->setBgColor( 0, 0, 0x32, 0x64 );
	pBackground->setBorder( new LineBorder( Color( 0, 0, 255, 0 ) ) );

	// Title (ref +0x350): "Identifiez-vous", Title Font, fg hardcoded
	// periwinkle (0x8c,0x8c,0xf0 — asm @0x100263a6), transparent bg, drawn
	// across the top centre of the panel.
	m_pTitle = new Label( "", 0, 0, ScreenWidth, 0x24 );
	m_pTitle->setParent( this );
	m_pTitle->setFont( pSchemes->getFont( hTitleScheme ) );
	m_pTitle->setFgColor( 0x8c, 0x8c, 0xf0, 0 );
	m_pTitle->setBgColor( 0, 0, 0, 0xff );
	m_pTitle->setContentAlignment( Label::a_center );
	m_pTitle->setText( "%s", "Identifiez-vous" );

	pSchemes->getFgColor( hBriefingText, r, g, b, a );

	// Counters (ref +0x16c/+0x170/+0x174: overlapping TextPanels with literal
	// widths — each starts where the previous one ends and runs to the same
	// right edge, so the blue borders nest; formats "#Total %d"/"#Used %d"/
	// "#Free %d" @0x100b3ec0.. with no colon).
	static const int iCounterX[3] = { 0x2a, 0xc2, 0x15a };
	static const int iCounterW[3] = { 0x1f2, 0x15a, 0xc2 };
	for ( int i = 0; i < 3; i++ )
	{
		m_pCounter[i] = new TextPanel( "", iCounterX[i], ScreenHeight - 0x86,
			iCounterW[i], 0x1d );
		m_pCounter[i]->setParent( this );
		m_pCounter[i]->setFont( pSchemes->getFont( hBriefingText ) );
		m_pCounter[i]->setFgColor( r, g, b, a );
		m_pCounter[i]->setBgColor( 0, 0, 0x32, 0x64 );
		m_pCounter[i]->setBorder( new LineBorder( Color( 0, 0, 0xff, 0 ) ) );
	}

	// Right column (ref +0xd4 scroll + +0x178 info text + +0x2e0[28] preview
	// labels): only exists at width > 800 — below that the ctor shrinks the
	// scroll bars away and Update() never shows the previews.
	const int iScrollY = (int)( ScreenHeight / 480.0f * 21.0f + 0.5f ) + 0x1c;
	const int iScrollH = ScreenHeight - ( iScrollY + 0x1c );
	if ( ScreenWidth > 800 )
	{
		m_pScroll = new CTFScrollPanel( 0x28f, iScrollY,
			ScreenWidth - 0x2b9, iScrollH - iScrollY );
		m_pScroll->setParent( this );

		m_pInfo = new TextPanel( "", 0, 0, ScreenWidth - 0x232, iScrollH );
		m_pInfo->setParent( m_pScroll->getClient() );
		m_pInfo->setFont( pSchemes->getFont( hBriefingText ) );
		m_pInfo->setFgColor( r, g, b, a );
		m_pInfo->setBgColor( 0, 0, 0x32, 0x64 );
		m_pInfo->setText( "" );

		// ref +0x2e0[i]: Label("") + setContentFitted + setImage() pinned at
		// 0x299,0x2a (top of the right column), hidden until a slot hover.
		// TGA is gfx/vgui/<res>_<shortname>.tga through LoadTGAForRes — the
		// ref's own name formatter is broken (FUN_100331d3 sprintf's the
		// short-name pointer into %2d), but the shipped files are the
		// <320,640,1024>_<short> set the orig shows.
		for ( int iSlot = 0; iSlot < SGTC_WPSEL_SLOTS; iSlot++ )
		{
			m_pPreview[iSlot] = new Label( "" );
			m_pPreview[iSlot]->setContentFitted( true );
			m_pPreview[iSlot]->setImage( LoadTGAForRes( s_SgtcWeapons[iSlot].pszLabel ) );
			m_pPreview[iSlot]->setPaintBackgroundEnabled( false );
			m_pPreview[iSlot]->setParent( this );
			m_pPreview[iSlot]->setPos( 0x299, 0x2a );
			m_pPreview[iSlot]->setVisible( false );
		}
	}
	else
	{
		m_pScroll = NULL;
		m_pInfo = NULL;
		for ( int iSlot = 0; iSlot < SGTC_WPSEL_SLOTS; iSlot++ )
			m_pPreview[iSlot] = NULL;
	}

	// 28 slot buttons (ref +0x200 primaries at x=0x2a, +0x238 secondaries at
	// x=0x160; each 0x104 x iRowTall) + per-slot cost TextPanels (+0x270 at
	// x=0x136, +0x2a8 at x=0x26c; 0x1b wide, grey text, blue LineBorder).
	for ( int iSlot = 0; iSlot < SGTC_WPSEL_SLOTS; iSlot++ )
	{
		const int iColumn = iSlot / SGTC_WPSEL_PRIMARY;
		const int iRow = iSlot % SGTC_WPSEL_PRIMARY;
		const int iBtnX = iColumn ? 0x160 : 0x2a;
		const int iCostX = iColumn ? 0x26c : 0x136;
		const int iY = iRowPitch * iRow + 0x2a;

		m_pSlotButton[iSlot] = new CSgtcWepButton(
			L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( s_SgtcWeapons[iSlot].pszKey ) ),
			iBtnX, iY, 0x104, iRowTall );
		m_pSlotButton[iSlot]->setParent( this );
		m_pSlotButton[iSlot]->setFgColor( 0x8c, 0x8c, 0x8c, 0 );
		m_pSlotButton[iSlot]->setFont( pSchemes->getFont( hBriefingText ) );
		m_pSlotButton[iSlot]->addActionSignal( new CSgtcHandler_SlotClick( this, iSlot ) );
		m_pSlotButton[iSlot]->addInputSignal( new CSgtcHandler_SlotOver( this, iSlot ) );

		m_pSlotCost[iSlot] = new TextPanel( "", iCostX, iY, 0x1b, iRowTall );
		m_pSlotCost[iSlot]->setParent( this );
		m_pSlotCost[iSlot]->setFont( pSchemes->getFont( hBriefingText ) );
		m_pSlotCost[iSlot]->setFgColor( 0x8c, 0x8c, 0x8c, 0 );
		m_pSlotCost[iSlot]->setBgColor( 0, 0, 0x32, 0x64 );
		m_pSlotCost[iSlot]->setBorder( new LineBorder( Color( 0, 0, 0xff, 0 ) ) );

		// Per-slot blurb (ref Update loads "info/<name>.txt" on hover with
		// the fallback "missing text file %s"; the port preloads like the
		// ctor-time local_118 table).
		char szFile[64];
		snprintf( szFile, sizeof( szFile ), "info/%s.txt", s_SgtcWeaponInfo[iSlot] );
		char *pfile = SgtcPanels_LoadFile( szFile );
		if ( pfile )
		{
			strlcpy( m_szSlotInfo[iSlot], pfile, SGTC_TEAMCLASS_INFO_LEN );
			gEngfuncs.COM_FreeFile( pfile );
		}
		else
		{
			snprintf( m_szSlotInfo[iSlot], SGTC_TEAMCLASS_INFO_LEN, "missing text file %s", szFile );
		}
	}

	// Bottom buttons, left→right as the ref draws them: +0x11c #Apply
	// (x=0x2a), +0x120 #Reset (x=0xd7), +0x118 #Annuler (x=0x184); all
	// y=H-0x54, 0x98 x 0x2a, created in the opposite order.
	m_pApplyButton = new CSgtcWepButton(
		L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( "#Apply" ) ),
		0x2a, ScreenHeight - 0x54, 0x98, 0x2a );
	m_pApplyButton->setParent( this );
	m_pApplyButton->addActionSignal( new CSgtcHandler_WepSelApply( this ) );

	m_pResetButton = new CSgtcWepButton(
		L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( "#Reset" ) ),
		0xd7, ScreenHeight - 0x54, 0x98, 0x2a );
	m_pResetButton->setParent( this );
	m_pResetButton->addActionSignal( new CSgtcHandler_WepSelReset( this ) );

	m_pCancelButton = new CSgtcWepButton(
		L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( "#Annuler" ) ),
		0x184, ScreenHeight - 0x54, 0x98, 0x2a );
	m_pCancelButton->setParent( this );
	m_pCancelButton->addActionSignal( new CSgtcHandler_DismissMenu() );

	Reset();
}

void CSgtcWeaponSelPanel::Reset( void )
{
	CMenuPanel::Reset();

	s_iWepUsed = 0;			// ref FUN_10029888: DAT_100ddad0 = 0
	s_iWepPrimCount = 0;		// DAT_100dda58 = 0
	s_iWepSpecialCount = 0;		// DAT_100dda54 = 0

	for ( int i = 0; i < SGTC_WPSEL_SLOTS; i++ )
	{
		if ( s_iWepFlag[i] == 1 )
			s_iWepFlag[i] = 0;
		m_iSelected[i] = 0;
		m_pSlotButton[i]->setArmed( false );
		// freevgui's momentary controller leaves Button::selected set when a
		// release never reaches the button; a stale selected state survived
		// Reset in the live A/B and kept the row painted as if armed.
		m_pSlotButton[i]->setSelectedDirect( false );
		m_pSlotButton[i]->setFgColor( 0x8c, 0x8c, 0x8c, 0 );
		m_pSlotButton[i]->setVisible( true );	// ref vtable+0x24 = setVisible
		if ( m_pSlotCost[i] )
			m_pSlotCost[i]->setFgColor( 0x8c, 0x8c, 0x8c, 0 );
		if ( m_pPreview[i] )
			m_pPreview[i]->setVisible( false );	// ref FUN_100282c1
	}

	m_iCurrentSlot = 0;
	if ( m_pInfo )
		m_pInfo->setText( "" );

	UpdateCounters();
}

// ref FUN_10029888 (vtable +0x1fc): the panel Reset body proper — called from
// Reset() above, from the DAT_100ddad8 gate and whenever the menu re-opens.
//   state[i] == 0 && flag[i] < 2  -> flag[i] = 2   (blocked: team/class gate)
//   state[i] == 1 && flag[i] == 2 -> flag[i] = 0   (unblock)
//   state[i] == 2                 -> select the slot, used += its cost
// The ref adds (&DAT_100dd9ac)[i] for secondary slots, which for i >= 0xe
// aliases the *state* array (0x100dd9e4) — the shipped binary therefore adds
// the state value (2) for every preselected secondary; replicated verbatim.
void CSgtcWeaponSelPanel::ApplyServerState( bool bReset )
{
	if ( bReset )
		Reset();

	for ( int i = 0; i < SGTC_WPSEL_SLOTS; i++ )
	{
		if ( s_iWepState[i] == 0 && s_iWepFlag[i] < 2 )
			s_iWepFlag[i] = 2;
		if ( s_iWepState[i] == 1 && s_iWepFlag[i] == 2 )
			s_iWepFlag[i] = 0;

		if ( s_iWepState[i] == 2 )
		{
			s_iWepFlag[i] = 1;
			m_iSelected[i] = 1;
			m_pSlotButton[i]->setArmed( true );	// ref +0x130 — paints red outline
			if ( i < SGTC_WEP_PRIM )
				s_iWepUsed += s_iWepCost[i];		// ref: (&DAT_100dd974)[i]
			else
				s_iWepUsed += s_iWepState[i];		// ref bug: (&DAT_100dd9ac)[i] aliases 0x100dd9e4
			s_iWepState[i] = 1;
		}

		// ref: flag == 2 (blocked by team/class) or cost == 0 -> the ref calls
		// vtable+0x24 (Panel::setVisible) on the slot BUTTON only; the cost
		// label stays visible but is painted black, so the orig shows an empty
		// blue-bordered box (see the A/B shots).
		if ( s_iWepFlag[i] == 2 || s_iWepCost[i] == 0 )
		{
			m_pSlotButton[i]->setVisible( false );
			if ( m_pSlotCost[i] )
				m_pSlotCost[i]->setFgColor( 0, 0, 0, 0xff );
		}
		else
		{
			// A refresh that arrives after a transient empty state (early open
			// before the cost/state messages land, or a wiped cost table) must
			// bring the row back. The old path only re-showed the cost label;
			// Button::setVisible(false) is sticky on freevgui and only the full
			// Reset() cleared it, so one blank paint kept the whole panel empty
			// for the life of the menu.
			m_pSlotButton[i]->setVisible( true );
			if ( m_pSlotCost[i] )
				m_pSlotCost[i]->setVisible( true );
		}
	}

	UpdateCounters();
}

void CSgtcWeaponSelPanel::ToggleSlot( int iSlot )
{
	if ( iSlot < 0 || iSlot >= SGTC_WPSEL_SLOTS )
		return;

	// ref FUN_10028323: a blocked/zero-cost slot is never toggled.
	if ( s_iWepFlag[iSlot] == 2 || s_iWepCost[iSlot] == 0 )
		return;

	const int iFree = s_iWepBudget - s_iWepUsed;

	if ( s_iWepFlag[iSlot] == 0 )
	{
		// Select: affordable + the per-group caps the ref enforces.
		if ( s_iWepCost[iSlot] > iFree )
			return;

		if ( iSlot < SGTC_WEP_PRIM )
		{
			// ref: DAT_100dda58 < 3, and slots 0/1 need DAT_100f225c != 0.
			if ( s_iWepPrimCount >= SGTC_WEP_MAX_PRIM )
				return;
			if ( ( iSlot == SGTC_WSLOT_GLSTAFF || iSlot == SGTC_WSLOT_USAS ) && s_iArmorMode == 0 )
				return;
			s_iWepPrimCount++;
		}
		else if ( SgtcWep_IsSpecialSlot( iSlot ) )
		{
			if ( s_iWepSpecialCount >= SGTC_WEP_MAX_SPECIAL )
				return;
			s_iWepSpecialCount++;
		}

		s_iWepFlag[iSlot] = 1;
		s_iWepUsed += s_iWepCost[iSlot];
	}
	else if ( s_iWepFlag[iSlot] == 1 )
	{
		s_iWepFlag[iSlot] = 0;
		s_iWepUsed -= s_iWepCost[iSlot];
		if ( iSlot < SGTC_WEP_PRIM )
		{
			if ( s_iWepPrimCount > 0 )
				s_iWepPrimCount--;
		}
		else if ( SgtcWep_IsSpecialSlot( iSlot ) && s_iWepSpecialCount > 0 )
		{
			s_iWepSpecialCount--;
		}
	}

	m_iSelected[iSlot] = ( s_iWepFlag[iSlot] == 1 );

	// Selected slots go white, unselected stay grey (ref pokes the button's
	// armed state + the cost label colour through the same flag).
	if ( m_iSelected[iSlot] )
		m_pSlotButton[iSlot]->setFgColor( 0xff, 0xff, 0xff, 0 );
	else
		m_pSlotButton[iSlot]->setFgColor( 0x8c, 0x8c, 0x8c, 0 );

	UpdateCounters();
}

int CSgtcWeaponSelPanel::SelectedSlotCount( void )
{
	int iCount = 0;
	for ( int i = 0; i < SGTC_WPSEL_SLOTS; i++ )
	{
		if ( m_iSelected[i] )
			iCount++;
	}
	return iCount;
}

void CSgtcWeaponSelPanel::UpdateCounters( void )
{
	// Ref FUN_10028323 counter block (0x10028fa6..0x1002905a):
	//   "#Total %d" DAT_100ddad4 | "#Used %d" DAT_100ddad0 | "#Free %d" free
	// (the shipped rodata strings have NO colon — 0x100b3ec0/0x100b3ecc/0x100b3ed8).
	char szText[64];

	snprintf( szText, sizeof( szText ), "#Total %d", s_iWepBudget );
	m_pCounter[0]->setText( L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( szText ) ) );
	snprintf( szText, sizeof( szText ), "#Used %d", s_iWepUsed );
	m_pCounter[1]->setText( L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( szText ) ) );
	snprintf( szText, sizeof( szText ), "#Free %d", s_iWepBudget - s_iWepUsed );
	m_pCounter[2]->setText( L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( szText ) ) );

	// Ref tints the #Free counter three ways (FUN_10028323 @0x1002903a..):
	// free < 3 -> red, else primaries already picked (DAT_100dda58 >= 3) ->
	// olive yellow, else blue-grey.
	if ( s_iWepBudget - s_iWepUsed < 3 )
		m_pCounter[2]->setFgColor( 0xb4, 0, 0, 0 );
	else if ( s_iWepPrimCount < 3 )
		m_pCounter[2]->setFgColor( 0x8c, 0x8c, 0xc8, 0 );
	else
		m_pCounter[2]->setFgColor( 0xb4, 0xb4, 0, 0 );

	// Ref cost labels (DAT_100ddacc block): "%d" of the slot cost, shown only
	// while the slot is not blocked — blocked slots keep the (now black)
	// bordered box but clear the text.
	for ( int i = 0; i < SGTC_WPSEL_SLOTS; i++ )
	{
		if ( !m_pSlotCost[i] )
			continue;
		if ( s_iWepFlag[i] < 2 )
		{
			char szCost[16];
			snprintf( szCost, sizeof( szCost ), "%d", s_iWepCost[i] );
			m_pSlotCost[i]->setText( szCost );
			m_pSlotCost[i]->setFgColor( 0x8c, 0x8c, 0x8c, 0 );
		}
		else
		{
			m_pSlotCost[i]->setText( "" );
		}
	}
}

void CSgtcWeaponSelPanel::ShowSlotInfo( int iSlot )
{
	if ( iSlot < 0 || iSlot >= SGTC_WPSEL_SLOTS )
		return;

	// ref FUN_10028323 hover block: hide the previous preview label, swap the
	// info text, show the new preview (right column exists at width > 800).
	if ( m_iCurrentSlot >= 0 && m_iCurrentSlot < SGTC_WPSEL_SLOTS &&
		m_pPreview[m_iCurrentSlot] )
		m_pPreview[m_iCurrentSlot]->setVisible( false );

	m_iCurrentSlot = iSlot;
	if ( m_pInfo )
		m_pInfo->setText( m_szSlotInfo[iSlot] );
	if ( m_pPreview[iSlot] )
		m_pPreview[iSlot]->setVisible( true );
	if ( m_pScroll )
		m_pScroll->setScrollValue( 0, 0 );	// ref scrolls to top on hover
}

void CSgtcWeaponSelPanel::SetActiveInfo( int iInput )
{
	ShowSlotInfo( iInput );
}

void CSgtcHandler_WepSelReset::actionPerformed( Panel *panel )
{
	// Ref Reset button runs the panel Reset vtable body (FUN_10029888), which
	// clears the selection AND re-applies the server's per-slot allow bytes, so
	// blocked rows stay hidden. The local Reset() helper only clears; calling it
	// alone made every one of the 28 rows visible (orig keeps blocked slots
	// hidden, A/B capture o_reset.png).
	if ( m_pPanel )
		m_pPanel->ApplyServerState();
}

void CSgtcHandler_SlotClick::actionPerformed( Panel *panel )
{
	if ( m_pPanel )
		m_pPanel->ToggleSlot( m_iSlot );
}

void CSgtcHandler_SlotOver::cursorEntered( Panel *panel )
{
	if ( m_pPanel )
		m_pPanel->SetActiveInfo( m_iSlot );
}

// Apply: the ref builds "gond4h9wp" + one value per slot and hands it to
// pfnClientCmd (FUN_10028323 @0x10029131..0x100291a7: the raw DAT_100dda5c
// flag value 0/1/2 is pushed for every slot — not a normalised 0/1). Our
// server's gond4h9wp handler reads CMD_ARGV(1..28) as m_iSgtcGondArgs[] and
// grants only slots whose arg is 1 (or every slot when mp_freeguns > 1) —
// dlls/sgtc_player.cpp SGTC_GiveFromMenu.
void CSgtcHandler_WepSelApply::actionPerformed( Panel *panel )
{
	if ( !m_pPanel || !gViewPort )
		return;

	char szCmd[256];
	int iLen = snprintf( szCmd, sizeof( szCmd ), "gond4h9wp" );
	for ( int i = 0; i < SGTC_WPSEL_SLOTS && iLen > 0 && iLen < (int)sizeof( szCmd ) - 4; i++ )
	{
		iLen += snprintf( szCmd + iLen, sizeof( szCmd ) - iLen, " %d", s_iWepFlag[i] );
	}
	szCmd[sizeof( szCmd ) - 1] = 0;

	gEngfuncs.pfnFilteredClientCmd( szCmd );
	gViewPort->HideTopMenu();
}

//============================================================================
// Menu 60 — pack select ("Identifiez-vous" + 4 packs).
// Ref: ctor FUN_10029ea0 @0x10029ea0, show helper FUN_10047fb8 @0x10047fb8
// (viewport +0xaa0), pack tables FUN_1002b15b @0x1002b15b, spawn helper
// FUN_1002b046 @0x1002b046. Buttons: #PACK_1..#PACK_4 (@0x100b403c..54),
// #Annuler @0x100b405c, #Apply @0x100b4068.
//============================================================================

#define SGTC_PACKSEL_COUNT	4
#define SGTC_PACK_MAX_SLOTS	6	// longest preset list (ref: 5 entries + sentinel)

// ref FUN_1002b15b @0x1002b15b: pack preset producer. Keyed on
//   +0x3c0 team  (ScoreInfo 4th short: GetTeamIndex+1; 1 = Torri, 2 = Goa'uld)
//   +0x3c4 class (ScoreInfo 3rd short, playerclass - 1 → 0..4)
//   +0x248 pack  (1..4, the pack button the player armed/selected)
// and writes 1 into the panel's 28-int slot array at byte offset 0x250
// (indices 2..27 are reachable; slots 0 glstaff and 1 usas never appear).
// Decoded from the decompile: byte offsets 0x258..0x2bc map to indices 2..27.
static const int s_SgtcPackSlots[2][SGTC_NUM_CLASSES][SGTC_PACKSEL_COUNT][SGTC_PACK_MAX_SLOTS] =
{
	{	// team 1 — Torri (ref +0x3c0 == 1)
		{ { 6, 10, 15, 20, 22, -1 }, { 3, 7, 11, 15, 23, -1 }, { -1 }, { -1 } },	// class 0
		{ { 8, 12, 22, 24, -1 },     { 4, 10, 20, 23, -1 },     { -1 }, { -1 } },	// class 1
		{ { 3, 5, 15, 20, 25, -1 },  { 6, 10, 16, 24, 26, -1 }, { -1 }, { -1 } },	// class 2
		{ { 8, 11, 16, 23, 24, -1 }, { 7, 10, 12, 15, 25, -1 }, { -1 }, { -1 } },	// class 3
		{ { 8, 15, 16, 23, 25, -1 }, { 3, 4, 20, 22, 26, -1 },
		  { 3, 6, 15, 16, -1 },      { 5, 10, 24, 26, -1 } },			// class 4
	},
	{	// team 2 — Goa'uld (ref +0x3c0 == 2)
		{ { 9, 10, 12, 18, -1 },     { 10, 14, 17, 27, -1 },    { -1 }, { -1 } },	// class 0
		{ { 10, 11, 14, 21, 27, -1 },{ 2, 10, 12, 13, 19, -1 }, { -1 }, { -1 } },	// class 1
		{ { 2, 10, 19, -1 },         { 11, 21, 27, -1 },
		  { 12, 13, 14, -1 },        { 10, 14, 21, -1 } },			// class 2
		{ { 11, 19, 21, 27, -1 },    { 9, 11, 21, 19, -1 },     { -1 }, { -1 } },	// class 3 prima PACK 1
		{ { 2, 13, 14, 17, -1 },     { 2, 11, 17, 21, -1 },     { -1 }, { -1 } },	// class 4
	}
};

// ref class-name tables 0x100b3f98 (Torri) / 0x100b3fac (Goa'uld), indexed by
// the same class value FUN_1002b046 stores in +0x3c4; the info file is
// "info/pack%d_%s.txt" (0x100b4088..0x100b40c4).
static const char *s_SgtcClassNames[2][SGTC_NUM_CLASSES] =
{
	{ "oneill", "carter", "jackson", "tealc", "soldat" },		// team 1
	{ "dieu",   "anubis", "jaffa",   "prima", "esclave" },		// team 2
};

int SgtcPack_GetSlots( int iTeam, int iClass, int iPack, int *piSlots )
{
	if ( !piSlots )
		return 0;
	if ( iTeam < SGTC_TEAM_TORRI || iTeam > SGTC_TEAM_GOAULD || iClass < 0 || iClass >= SGTC_NUM_CLASSES || iPack < 1 || iPack > SGTC_PACKSEL_COUNT )
		return 0;

	const int *pSrc = s_SgtcPackSlots[iTeam - SGTC_TEAM_TORRI][iClass][iPack - 1];
	int iCount = 0;
	while ( iCount < SGTC_PACK_MAX_SLOTS && pSrc[iCount] >= 0 )
	{
		piSlots[iCount] = pSrc[iCount];
		iCount++;
	}
	return iCount;
}

class CSgtcPackSelPanel : public CMenuPanel
{
public:
	CSgtcPackSelPanel( int iTrans, int iRemoveMe, int x, int y, int wide, int tall );

	virtual void Reset( void );
	virtual void SetActiveInfo( int iInput );
	virtual void paintTraverse( bool repaint );

	// ref Update (0x100295be..0x1002a209): the panel only carries the pack
	// buttons its class actually has — the orig shows exactly two
	// ("#PACK 1"/"#PACK 2") for an anubis player, the other two are absent,
	// not greyed out. The ScoreInfo cache may arrive after the ctor, so paint
	// re-checks, but the buttons are only touched when the local player's
	// team/class changed (or bForce, from Reset).
	void RefreshPackAvailability( bool bForce );

	void SelectPack( int iPack );
	// ref vtable +0x214/+0x218 (FUN_1002b046 + FUN_1002b15b): fill the 28
	// command values from the pack preset table for the local player.
	void FillPackSlots( void );
	int GetSlotValue( int iSlot ) { return ( iSlot >= 0 && iSlot < SGTC_WPSEL_SLOTS ) ? m_iSlots[iSlot] : 0; }
	int GetSelectedPack( void ) const { return m_iSelectedPack; }

private:
	Label *m_pTitle;
	TextPanel *m_pInfo;
	CommandButton *m_pPackButton[SGTC_PACKSEL_COUNT];
	CommandButton *m_pCancelButton;
	CommandButton *m_pApplyButton;
	int m_iSelectedPack;
	int m_iSlots[SGTC_WPSEL_SLOTS];	// ref panel +0x250 (28 ints)
	int m_iPackTeam;		// team/class RefreshPackAvailability last applied
	int m_iPackClass;
};

class CSgtcHandler_PackClick : public ActionSignal
{
public:
	CSgtcHandler_PackClick( CSgtcPackSelPanel *pPanel, int iPack )
	{
		m_pPanel = pPanel;
		m_iPack = iPack;
	}

	virtual void actionPerformed( Panel *panel );

private:
	CSgtcPackSelPanel *m_pPanel;
	int m_iPack;
};

class CSgtcHandler_PackApply : public ActionSignal
{
public:
	CSgtcHandler_PackApply( CSgtcPackSelPanel *pPanel )
	{
		m_pPanel = pPanel;
	}

	virtual void actionPerformed( Panel *panel );

private:
	CSgtcPackSelPanel *m_pPanel;
};

class CSgtcHandler_PackOver : public InputSignal
{
public:
	CSgtcHandler_PackOver( CSgtcPackSelPanel *pPanel, int iPack )
	{
		m_pPanel = pPanel;
		m_iPack = iPack;
	}

	void cursorEntered( Panel *panel );

	void cursorMoved( int x, int y, Panel *panel ) {}
	void mousePressed( MouseCode code, Panel *panel ) {}
	void mouseReleased( MouseCode code, Panel *panel ) {}
	void mouseDoublePressed( MouseCode code, Panel *panel ) {}
	void cursorExited( Panel *panel ) {}
	void mouseWheeled( int delta, Panel *panel ) {}
	void keyPressed( KeyCode code, Panel *panel ) {}
	void keyTyped( KeyCode code, Panel *panel ) {}
	void keyReleased( KeyCode code, Panel *panel ) {}
	void keyFocusTicked( Panel *panel ) {}

private:
	CSgtcPackSelPanel *m_pPanel;
	int m_iPack;
};

CSgtcPackSelPanel::CSgtcPackSelPanel( int iTrans, int iRemoveMe, int x, int y, int wide, int tall )
	: CMenuPanel( iTrans, iRemoveMe, x, y, wide, tall )
{
	CSchemeManager *pSchemes = gViewPort->GetSchemeManager();
	SchemeHandle_t hTitleScheme = pSchemes->getSchemeHandle( "Title Font" );
	SchemeHandle_t hBriefingText = pSchemes->getSchemeHandle( "Briefing Text" );
	int r, g, b, a;

	// Background (ref +0x164: TextPanel 0x15,0x15, W-0x2a, H-0x2a + border).
	// ASM @0x1002a1ca: setBgColor(0,0,0x32,0x64) — dark navy translucent,
	// same as menus 11/12 (0x100ddaf4 is the TextPanel ctor arg, not a colour).
	// Ref ctor uses raw pixel constants (VGUI1 ints), not 640x480 design units:
	// measured orig frame = x 21..1002, y 21..746 at 1024x768.
	TextPanel *pBackground = new TextPanel( "", 21, 21,
		ScreenWidth - 42, ScreenHeight - 42 );
	pBackground->setParent( this );
	pBackground->setBgColor( 0, 0, 0x32, 0x64 );
	pBackground->setBorder( new LineBorder( Color( 0, 0, 255, 0 ) ) );

	// Title (ref +0x234, literal "Identifiez-vous"; fg hardcoded periwinkle
	// 0x8c,0x8c,0xf0 @0x1002a02b — same as menus 11/12, not the scheme colour)
	m_pTitle = new Label( "", 40, 16 );
	m_pTitle->setParent( this );
	m_pTitle->setFont( pSchemes->getFont( hTitleScheme ) );
	m_pTitle->setFgColor( 0x8c, 0x8c, 0xf0, 0 );
	m_pTitle->setBgColor( 0, 0, 0, 0xff );
	m_pTitle->setText( "%s", "Identifiez-vous" );

	// 4 pack buttons (ref +0x1fc array, texts "#PACK_1".."#PACK_4")
	for ( int iPack = 0; iPack < SGTC_PACKSEL_COUNT; iPack++ )
	{
		char szKey[16];
		snprintf( szKey, sizeof( szKey ), "#PACK %d", iPack + 1 );	// ref 0x100b403c.. (space, no underscore)

		// Measured orig: x 42..301 (260 wide), rows at y 42 and 79 (pitch 37,
		// height 29). The menu-12 row pitch/height use the same 0x25/0x1d pair.
		m_pPackButton[iPack] = new CommandButton(
			L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( szKey ) ),
			42, 42 + iPack * 37, 260, 29 );
		m_pPackButton[iPack]->setParent( this );
		m_pPackButton[iPack]->addActionSignal( new CSgtcHandler_PackClick( this, iPack ) );
		m_pPackButton[iPack]->addInputSignal( new CSgtcHandler_PackOver( this, iPack ) );
		m_pPackButton[iPack]->setFgColor( 0x8c, 0x8c, 0x8c, 0 );
	}

	// Pack info panel (ref +0x174: x 0x143, y 0x2a, W-0x16d, H-0x93, fg 0x8c8c8c,
	// bg black)
	// Ref ctor: x 0x143 (323), y 0x2a (42), W-0x16d, H-0x93 — raw pixels.
	// Measured orig frame x 323..981, y 42..662 at 1024x768.
	m_pInfo = new TextPanel( "", 323, 42,
		ScreenWidth - 365, ScreenHeight - 147 );
	m_pInfo->setParent( this );
	m_pInfo->setFont( pSchemes->getFont( hBriefingText ) );
	pSchemes->getFgColor( hBriefingText, r, g, b, a );
	m_pInfo->setFgColor( 0x8c, 0x8c, 0x8c, 0 );
	m_pInfo->setBgColor( 0, 0, 0, 0xff );
	m_pInfo->setBorder( new LineBorder( Color( 0, 0, 0xff, 0 ) ) );

	// Bottom buttons (ref +0x114 #Annuler at W/2+0xad, +0x118 #Apply at W/2-0xad)
	// Measured orig: Apply/Close 152x42 at y 684 (H-84), x = W/2 +/- 173.
	m_pCancelButton = new CommandButton( L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( "#Annuler" ) ),
		ScreenWidth / 2 + 173, ScreenHeight - 84, 152, 42 );
	m_pCancelButton->setParent( this );
	m_pCancelButton->addActionSignal( new CSgtcHandler_DismissMenu() );

	m_pApplyButton = new CommandButton( L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( "#Apply" ) ),
		ScreenWidth / 2 - 173, ScreenHeight - 84, 152, 42 );
	m_pApplyButton->setParent( this );
	m_pApplyButton->addActionSignal( new CSgtcHandler_PackApply( this ) );

	m_iPackTeam = 0;
	m_iPackClass = -1;
	Reset();
}

void CSgtcPackSelPanel::Reset( void )
{
	CMenuPanel::Reset();
	m_iSelectedPack = -1;

	for ( int i = 0; i < SGTC_WPSEL_SLOTS; i++ )
		m_iSlots[i] = 0;			// ref: DAT_100ddad8 gate zeroes the array

	for ( int i = 0; i < SGTC_PACKSEL_COUNT; i++ )
	{
		m_pPackButton[i]->setArmed( false );
		m_pPackButton[i]->setFgColor( 0x8c, 0x8c, 0x8c, 0 );
	}
	RefreshPackAvailability( true );
	m_pInfo->setText( "" );
}

// ref Update: the pack list is per team/class — count the non-empty preset
// rows and hide the buttons past that count (the orig draws only the two
// anubis packs). Unknown team/class (ScoreInfo not yet received) keeps all
// four, matching the ref's uninitialised-cache path.
void CSgtcPackSelPanel::RefreshPackAvailability( bool bForce )
{
	int iPacks = SGTC_PACKSEL_COUNT;
	int iTeam = 0;
	int iClass = -1;
	cl_entity_t *pLocal = gEngfuncs.GetLocalPlayer();
	if ( pLocal && pLocal->index > 0 && pLocal->index <= MAX_PLAYERS )
	{
		iTeam = g_PlayerExtraInfo[pLocal->index].teamnumber;
		iClass = g_PlayerExtraInfo[pLocal->index].playerclass - SGTC_CLASS_FIRST;
	}

	// The preset table is static, so the result only depends on team/class.
	if ( !bForce && iTeam == m_iPackTeam && iClass == m_iPackClass )
		return;
	m_iPackTeam = iTeam;
	m_iPackClass = iClass;

	if ( iTeam >= SGTC_TEAM_TORRI && iTeam <= SGTC_TEAM_GOAULD && iClass >= 0 && iClass < SGTC_NUM_CLASSES )
	{
		iPacks = 0;
		for ( int p = 0; p < SGTC_PACKSEL_COUNT; p++ )
		{
			int iTmp[SGTC_PACK_MAX_SLOTS];
			if ( SgtcPack_GetSlots( iTeam, iClass, p + 1, iTmp ) > 0 )
				iPacks++;
		}
	}

	for ( int i = 0; i < SGTC_PACKSEL_COUNT; i++ )
	{
		m_pPackButton[i]->setVisible( i < iPacks );	// ref: absent, not disabled
		m_pPackButton[i]->setEnabled( i < iPacks );
	}
}

void CSgtcPackSelPanel::paintTraverse( bool repaint )
{
	RefreshPackAvailability( false );
	CMenuPanel::paintTraverse( repaint );
}

// ref FUN_1002b046 (+0x214): read the local player's team/class from the
// ScoreInfo cache, then FUN_1002b15b (+0x218) fills the slot array.
void CSgtcPackSelPanel::FillPackSlots( void )
{
	for ( int i = 0; i < SGTC_WPSEL_SLOTS; i++ )
		m_iSlots[i] = 0;

	// m_iSelectedPack is the 0-based button index (CSgtcHandler_PackClick
	// passes the ctor loop index); SgtcPack_GetSlots is 1-based like the ref's
	// panel +0x248 pack value. Passing the 0-based value made PACK 1 return
	// early (all-zero gond4h9wp, no grant) and PACK N >= 2 send pack N-1.
	if ( m_iSelectedPack < 0 || m_iSelectedPack >= SGTC_PACKSEL_COUNT )
		return;

	int iTeam = 0;
	int iClass = -1;

	cl_entity_t *pLocal = gEngfuncs.GetLocalPlayer();
	if ( pLocal && pLocal->index > 0 && pLocal->index <= MAX_PLAYERS )
	{
		// ref +0x3c0 = teamnumber (GetTeamIndex+1), +0x3c4 = playerclass - 1
		iTeam = g_PlayerExtraInfo[pLocal->index].teamnumber;
		iClass = g_PlayerExtraInfo[pLocal->index].playerclass - SGTC_CLASS_FIRST;
	}

	int iSlots[SGTC_PACK_MAX_SLOTS];
	int iCount = SgtcPack_GetSlots( iTeam, iClass, m_iSelectedPack + 1, iSlots );
	for ( int i = 0; i < iCount; i++ )
	{
		if ( iSlots[i] >= 0 && iSlots[i] < SGTC_WPSEL_SLOTS )
			m_iSlots[iSlots[i]] = 1;
	}
}

void CSgtcPackSelPanel::SelectPack( int iPack )
{
	if ( iPack < 0 || iPack >= SGTC_PACKSEL_COUNT )
		return;

	// ref Update: only packs with a non-empty preset row are selectable — the
	// orig omits empty pack buttons entirely (RefreshPackAvailability), and
	// rejects a pack row with no slots for this team/class.
	int iTeam = 0;
	int iClass = -1;
	cl_entity_t *pLocal = gEngfuncs.GetLocalPlayer();
	if ( pLocal && pLocal->index > 0 && pLocal->index <= MAX_PLAYERS )
	{
		iTeam = g_PlayerExtraInfo[pLocal->index].teamnumber;
		iClass = g_PlayerExtraInfo[pLocal->index].playerclass - SGTC_CLASS_FIRST;
	}

	int iTmp[SGTC_PACK_MAX_SLOTS];
	if ( SgtcPack_GetSlots( iTeam, iClass, iPack + 1, iTmp ) <= 0 )
		return;

	m_iSelectedPack = iPack;

	for ( int i = 0; i < SGTC_PACKSEL_COUNT; i++ )
		m_pPackButton[i]->setFgColor( i == iPack ? 0xff : 0x8c, i == iPack ? 0xff : 0x8c,
			i == iPack ? 0xff : 0x8c, 0 );

	// ref Update: the blurb file is "info/pack%d_%s.txt" with the class name
	// (PTR_s_oneill / PTR_DAT_100b3fac tables), fallback "missing text file %s".

	const char *pszClass = "esclave";
	if ( iTeam >= SGTC_TEAM_TORRI && iTeam <= SGTC_TEAM_GOAULD && iClass >= 0 && iClass < SGTC_NUM_CLASSES )
		pszClass = s_SgtcClassNames[iTeam - SGTC_TEAM_TORRI][iClass];

	char szFile[128];
	snprintf( szFile, sizeof( szFile ), "info/pack%d_%s.txt", iPack + 1, pszClass );

	char *pfile = SgtcPanels_LoadFile( szFile );
	if ( pfile )
	{
		m_pInfo->setText( pfile );
		gEngfuncs.COM_FreeFile( pfile );
	}
	else
	{
		char szMissing[160];
		snprintf( szMissing, sizeof( szMissing ), "missing text file %s", szFile );
		m_pInfo->setText( szMissing );
	}

	FillPackSlots();
}

void CSgtcPackSelPanel::SetActiveInfo( int iInput )
{
	SelectPack( iInput );
}

void CSgtcHandler_PackClick::actionPerformed( Panel *panel )
{
	if ( m_pPanel )
		m_pPanel->SelectPack( m_iPack );
}

void CSgtcHandler_PackOver::cursorEntered( Panel *panel )
{
	if ( m_pPanel )
		m_pPanel->SetActiveInfo( m_iPack );
}

// Apply: the ref's pack panel zeroes the 28 slot values, runs vtable +0x214
// (FUN_1002b046, local player team/class) then +0x218 (FUN_1002b15b, pack
// preset table), builds "gond4h9wp" + 28 " %d" values (FUN_1002a7a7
// @0x1002ad75..0x1002adfb) and sends it through pfnClientCmd.
void CSgtcHandler_PackApply::actionPerformed( Panel *panel )
{
	if ( !m_pPanel || !gViewPort )
		return;

	if ( m_pPanel->GetSelectedPack() < 0 )
		return;

	m_pPanel->FillPackSlots();

	char szCmd[256];
	int iLen = snprintf( szCmd, sizeof( szCmd ), "gond4h9wp" );
	for ( int i = 0; i < SGTC_WPSEL_SLOTS && iLen > 0 && iLen < (int)sizeof( szCmd ) - 4; i++ )
	{
		iLen += snprintf( szCmd + iLen, sizeof( szCmd ) - iLen, " %d", m_pPanel->GetSlotValue( i ) );
	}
	szCmd[sizeof( szCmd ) - 1] = 0;

	gEngfuncs.pfnFilteredClientCmd( szCmd );
	gViewPort->HideTopMenu();
}


//============================================================================
// entry points — created once, re-opened per message (ref pre-creates both
// panels in the viewport ctor: FUN_100482ed / FUN_100483ea)
//============================================================================

static CSgtcBriefingPanel *s_pBriefingPanel = NULL;
static CSgtcTeamClassPanel *s_pTeamClassPanel = NULL;
static CSgtcWeaponSelPanel *s_pWeaponSelPanel = NULL;
static CMenuPanel *s_pDmClassPanel = NULL;	// DM class panel (menu 13)
static CSgtcPackSelPanel *s_pPackSelPanel = NULL;
static TeamFortressViewport *s_pPanelOwner = NULL;

// True when p is one of our four singleton panels (used for chain surgery).
static bool SgtcPanels_IsOurPanel( CMenuPanel *p )
{
	return p == (CMenuPanel *)s_pBriefingPanel
		|| p == (CMenuPanel *)s_pTeamClassPanel
		|| p == (CMenuPanel *)s_pWeaponSelPanel
		|| p == (CMenuPanel *)s_pPackSelPanel
		|| p == s_pDmClassPanel;
}

// Force-hide every SGTC menu singleton AND take the singletons out of the
// m_pCurrentMenu chain (server HideVGUI = close-all; a hidden node left
// chained would still dedup the next resend via SgtcPanels_ShouldShow).
// Called when the server drops VGUI; the dismissal mark is cleared too —
// after HideVGUI the next menu send is a fresh install.
// Server-driven dismissal: SGTC_PackAuto_Tick sends this after it auto-grants a
// pack, because the still-open loadout panel keeps VGUI keyboard focus and the
// player can no longer switch weapons (John: slot2/invnext dead after
// auto-select). Same close path as the Apply button, but from the server.
void SgtcPanels_LoadoutDone( void )
{
	s_bLoadoutGranted = true;
	SgtcPanels_HideAll();
	if ( gViewPort )
		gViewPort->UpdateCursorState();
}

// QA-only panel opener for headless screenshots/tests (sv_cheats + sgtc_test_enable).
// Usage: sgtc_panel <10|11|12|13|60>. Opens that menu as the front panel.
static void SgtcPanels_QaPanelCommand( void )
{
	if ( gEngfuncs.pfnGetCvarFloat( "sv_cheats" ) < 1.0f )
		return;
	if ( gEngfuncs.pfnGetCvarFloat( "sgtc_test_enable" ) <= 0.0f )
		return;

	const int iMenu = gEngfuncs.Cmd_Argc() > 1 ? atoi( gEngfuncs.Cmd_Argv( 1 ) ) : 0;
	SgtcPanels_HideAll();

	switch ( iMenu )
	{
	case SGTC_MENU_BRIEFING:	SgtcBriefing_ShowPanel();	break;
	case SGTC_MENU_TEAMCLASS:	SgtcTeamClass_ShowPanel();	break;
	case SGTC_MENU_WPSEL:		SgtcWeaponSel_ShowPanel();	break;
	case SGTC_MENU_DMCLASS:		SgtcDmClass_ShowPanel();	break;
	case SGTC_MENU_PACKSEL:		SgtcPackSel_ShowPanel();	break;
	default:
		gEngfuncs.Con_Printf( "[sgtc] sgtc_panel: unknown menu %d\n", iMenu );
		break;
	}
}

// Registered once from the HUD init path.
void SgtcPanels_RegisterClientCommands( void )
{
	static bool s_bRegistered = false;
	if ( s_bRegistered )
		return;
	s_bRegistered = true;

	gEngfuncs.pfnAddCommand( "sgtc_loadout_done", SgtcPanels_LoadoutDone );
	gEngfuncs.pfnAddCommand( "sgtc_panel", SgtcPanels_QaPanelCommand );
}

void SgtcPanels_HideAll( void )
{
	if ( gViewPort )
	{
		while ( gViewPort->m_pCurrentMenu
			&& SgtcPanels_IsOurPanel( gViewPort->m_pCurrentMenu ) )
			gViewPort->m_pCurrentMenu = gViewPort->m_pCurrentMenu->GetNextMenu();

		for ( CMenuPanel *p = gViewPort->m_pCurrentMenu; p && p->GetNextMenu(); )
		{
			CMenuPanel *pNext = p->GetNextMenu();
			if ( SgtcPanels_IsOurPanel( pNext ) )
				p->SetNextMenu( pNext->GetNextMenu() );
			else
				p = pNext;
		}
	}

	if ( s_pBriefingPanel )	s_pBriefingPanel->setVisible( false );
	if ( s_pTeamClassPanel )	s_pTeamClassPanel->setVisible( false );
	if ( s_pWeaponSelPanel )	s_pWeaponSelPanel->setVisible( false );
	if ( s_pPackSelPanel )	s_pPackSelPanel->setVisible( false );
	if ( s_pDmClassPanel )	s_pDmClassPanel->setVisible( false );

	// Build-18 input fix: re-enable the right panel and re-assert the cursor on
	// every close path (a hide used to leave the cursor grabbed / input disabled).
	SgtcPanels_ApplyInputState();
	if ( gViewPort )
	{
		gViewPort->UpdateCursorState();
		// Build-20 weapon-switch fix: our panels used to keep keyboard focus
		// after closing, so slot1..slot7 / invnext / invprev were swallowed and
		// nothing switched (John). Hand focus back to the game root whenever no
		// SGTC menu is left.
		if ( !SgtcPanels_AnyMenuVisible() )
			gViewPort->requestFocus();
	}
	if ( !SgtcPanels_AnyMenuVisible() )
		SgtcPanels_SetMenuCursor( false );

	s_iSgtcDismissedMenu = 0;
}

// Build-18 VGUI input routing: only the FRONT chain panel accepts mouse input.
// Every other SGTC singleton is either chained behind (its full-screen
// transparent background would swallow clicks aimed at the visible menu) or
// hidden; both must be disabled so clicks reach the panel the player sees.
static void SgtcPanels_ApplyInputState( void )
{
	if ( !gViewPort )
		return;

	CMenuPanel *pFront = gViewPort->m_pCurrentMenu;
	if ( pFront && !SgtcPanels_IsOurPanel( pFront ) )
		pFront = NULL;	// a stock TFC menu owns input in that case

	for ( CMenuPanel *p = pFront; p; p = p->GetNextMenu() )
	{
		if ( SgtcPanels_IsOurPanel( p ) )
			p->setEnabled( p == pFront );
	}

	CMenuPanel *pAll[5] = { s_pBriefingPanel, s_pTeamClassPanel, s_pWeaponSelPanel, s_pPackSelPanel, s_pDmClassPanel };
	for ( int i = 0; i < 5; i++ )
	{
		CMenuPanel *p = pAll[i];
		if ( !p )
			continue;

		bool bInChain = false;
		for ( CMenuPanel *q = pFront; q; q = q->GetNextMenu() )
		{
			if ( q == p )
			{
				bInChain = true;
				break;
			}
		}
		p->setEnabled( bInChain ? ( p == pFront ) : false );
	}
}

// Build-19 urgent cursor release: on macOS the freevgui CursorSelect path can
// no-op, leaving the SDL pointer grabbed (crosshair) while an SGTC menu is up.
// Drive Xash's own _windowed_mouse (and m_rawinput) directly as a fallback and
// restore both when the last menu closes. Called on show/hide and re-asserted
// every frame by SgtcPanels_CursorTick() so a focus regain cannot re-grab it.
static bool s_bSgtcCursorReleased = false;
static float s_flSgtcRawInputSaved = -1.0f;

static void SgtcPanels_SetMenuCursor( bool bMenuOpen )
{
	if ( bMenuOpen )
	{
		if ( !s_bSgtcCursorReleased )
		{
			s_flSgtcRawInputSaved = gEngfuncs.pfnGetCvarFloat( "m_rawinput" );
			s_bSgtcCursorReleased = true;
			if ( gEngfuncs.pfnGetCvarFloat( "sv_cheats" ) >= 1.0f &&
				gEngfuncs.pfnGetCvarFloat( "sgtc_test_enable" ) > 0.0f )
			{
				gEngfuncs.Con_Printf( "[sgtc_cursor] menu open release: _windowed_mouse=%.0f m_rawinput=%.0f in_mouse=%.0f hud_capturemouse=%.0f rawinput_saved=%.0f\n",
					gEngfuncs.pfnGetCvarFloat( "_windowed_mouse" ),
					gEngfuncs.pfnGetCvarFloat( "m_rawinput" ),
					gEngfuncs.pfnGetCvarFloat( "in_mouse" ),
					gEngfuncs.pfnGetCvarFloat( "hud_capturemouse" ),
					s_flSgtcRawInputSaved );
			}
		}
		// Both paths on purpose: Cvar_SetValue updates the live engine cvar and
		// the client command goes through Xash's normal set path (which also
		// ungrabs SDL when the cvar has a callback).
		gEngfuncs.Cvar_SetValue( "_windowed_mouse", 0.0f );
		gEngfuncs.pfnClientCmd( "_windowed_mouse 0\n" );
		gEngfuncs.Cvar_SetValue( "m_rawinput", 0.0f );
	}
	else
	{
		gEngfuncs.Cvar_SetValue( "_windowed_mouse", 1.0f );
		gEngfuncs.pfnClientCmd( "_windowed_mouse 1\n" );
		if ( s_flSgtcRawInputSaved >= 0.0f )
			gEngfuncs.Cvar_SetValue( "m_rawinput", s_flSgtcRawInputSaved );
		if ( s_bSgtcCursorReleased &&
			gEngfuncs.pfnGetCvarFloat( "sv_cheats" ) >= 1.0f &&
			gEngfuncs.pfnGetCvarFloat( "sgtc_test_enable" ) > 0.0f )
		{
			gEngfuncs.Con_Printf( "[sgtc_cursor] menu closed restore: _windowed_mouse=1 m_rawinput=%.0f\n",
				gEngfuncs.pfnGetCvarFloat( "m_rawinput" ) );
		}
		s_flSgtcRawInputSaved = -1.0f;
		s_bSgtcCursorReleased = false;
	}
}

// Re-assert the menu cursor every frame while any SGTC menu is visible: a
// window focus change re-grabs the SDL pointer, which otherwise left John
// stuck at the crosshair with clicks going to the game.
void SgtcPanels_CursorTick( void )
{
	if ( SgtcPanels_AnyMenuVisible() )
	{
		if ( !s_bSgtcCursorReleased )
			SgtcPanels_SetMenuCursor( true );
		else if ( gEngfuncs.pfnGetCvarFloat( "_windowed_mouse" ) != 0.0f )
			SgtcPanels_SetMenuCursor( true );
	}
	else if ( s_bSgtcCursorReleased )
	{
		SgtcPanels_SetMenuCursor( false );
	}
}

// True while any SGTC VGUI menu is on screen — the client-side cursor gate in
// TeamFortressViewport::UpdateCursorState uses this so the SDL cursor stays
// ungrabbed (and clicks keep resolving) for the whole menu lifetime.
bool SgtcPanels_AnyMenuVisible( void )
{
	return ( s_pBriefingPanel && s_pBriefingPanel->isVisible() ) ||
	       ( s_pTeamClassPanel && s_pTeamClassPanel->isVisible() ) ||
	       ( s_pWeaponSelPanel && s_pWeaponSelPanel->isVisible() ) ||
	       ( s_pPackSelPanel && s_pPackSelPanel->isVisible() );
}

// Panels are children of gViewPort and die with it; drop a stale cache if the
// engine ever hands out a new viewport instance.
static void SgtcPanels_CheckOwner( void )
{
	if ( s_pPanelOwner != gViewPort )
	{
		s_pBriefingPanel = NULL;
		s_pTeamClassPanel = NULL;
		s_pWeaponSelPanel = NULL;
		s_pPackSelPanel = NULL;
		s_pDmClassPanel = NULL;
		s_iSgtcDismissedMenu = 0;	// new viewport = fresh menu state
		s_pPanelOwner = gViewPort;
	}
}

// ref FUN_10028323 / FUN_1002a7a7 prologue: `if (0 < DAT_100ddad8) { Reset(); DAT_100ddad8 = 0; }`
// — the server asks for a selection reset (class/team change) with the last
// byte of the Progress message. The port has no per-frame panel Update, so
// the message handlers call this through SgtcWepSel_Refresh().
static void SgtcWepSel_ApplyServerState( void )
{
	SgtcPanels_CheckOwner();

	if ( !s_pWeaponSelPanel )
		return;

	if ( s_iWepResetFlag )
	{
		s_iWepResetFlag = 0;
		s_pWeaponSelPanel->ApplyServerState();
	}
	else
	{
		// Costs/budget/state changed under a live panel with the reset flag
		// clear: ref Update runs the state/hide/cost pass only — a full
		// Reset() here would discard the player's in-progress clicks.
		s_pWeaponSelPanel->ApplyServerState( false );
	}
}

CMenuPanel *SgtcBriefing_ShowPanel( void )
{
	if ( !gViewPort )
		return NULL;

	SgtcPanels_CheckOwner();

	if ( !s_pBriefingPanel )
	{
		s_pBriefingPanel = new CSgtcBriefingPanel( 255, 0, 0, 0, ScreenWidth, ScreenHeight );
		s_pBriefingPanel->addInputSignal( new CSgtcHandler_InputDiag() );
		s_pBriefingPanel->setParent( gViewPort );
		s_pBriefingPanel->setVisible( false );
	}

	// Dedup BEFORE Reset: a resend while this menu is shown/queued/dismissed
	// must not wipe panel state (ref same-id dedup is over the whole chain).
	if ( !SgtcPanels_ShouldShow( SGTC_MENU_BRIEFING ) )
		return s_pBriefingPanel;

	s_pBriefingPanel->Reset();
	SgtcPanels_ShowMenu( s_pBriefingPanel, SGTC_MENU_BRIEFING );
	return s_pBriefingPanel;
}

CMenuPanel *SgtcTeamClass_ShowPanel( void )
{
	if ( !gViewPort )
		return NULL;

	SgtcPanels_CheckOwner();

	if ( !s_pTeamClassPanel )
	{
		s_pTeamClassPanel = new CSgtcTeamClassPanel( 255, 0, 0, 0, ScreenWidth, ScreenHeight );
		s_pTeamClassPanel->addInputSignal( new CSgtcHandler_InputDiag() );
		s_pTeamClassPanel->setParent( gViewPort );
		s_pTeamClassPanel->setVisible( false );
	}

	// The server re-sends VGUIMenu(11) every second while the player is a
	// dead observer (sgtc_round.cpp PlayerThink 1s teams-tick). Dedup BEFORE
	// Reset: a resend while menu 11 is shown/queued/dismissed must not re-arm
	// m_bFirstPaint (which re-hides the class list under the cursor ~1s after
	// every hover) nor reopen a menu the user dismissed.
	if ( !SgtcPanels_ShouldShow( SGTC_MENU_TEAMCLASS ) )
		return s_pTeamClassPanel;

	s_pTeamClassPanel->Reset();
	SgtcPanels_ShowMenu( s_pTeamClassPanel, SGTC_MENU_TEAMCLASS );
	return s_pTeamClassPanel;
}

CMenuPanel *SgtcWeaponSel_ShowPanel( void )
{
	if ( !gViewPort )
		return NULL;

	SgtcPanels_CheckOwner();

	if ( !s_pWeaponSelPanel )
	{
		s_pWeaponSelPanel = new CSgtcWeaponSelPanel( 100, 0, 0, 0, ScreenWidth, ScreenHeight );
		s_pWeaponSelPanel->addInputSignal( new CSgtcHandler_InputDiag() );
		s_pWeaponSelPanel->setParent( gViewPort );
		s_pWeaponSelPanel->setVisible( false );
	}

	if ( !SgtcPanels_ShouldShow( SGTC_MENU_WPSEL ) )
		return s_pWeaponSelPanel;

	s_pWeaponSelPanel->ApplyServerState();	// ref Update: Reset() honours DAT_100ddad8
	SgtcPanels_ShowMenu( s_pWeaponSelPanel, SGTC_MENU_WPSEL );
	return s_pWeaponSelPanel;
}

CMenuPanel *SgtcPackSel_ShowPanel( void )
{
	if ( !gViewPort )
		return NULL;

	SgtcPanels_CheckOwner();

	if ( !s_pPackSelPanel )
	{
		s_pPackSelPanel = new CSgtcPackSelPanel( 100, 0, 0, 0, ScreenWidth, ScreenHeight );
		s_pPackSelPanel->addInputSignal( new CSgtcHandler_InputDiag() );
		s_pPackSelPanel->setParent( gViewPort );
		s_pPackSelPanel->setVisible( false );
	}

	if ( !SgtcPanels_ShouldShow( SGTC_MENU_PACKSEL ) )
		return s_pPackSelPanel;

	s_pPackSelPanel->Reset();
	SgtcPanels_ShowMenu( s_pPackSelPanel, SGTC_MENU_PACKSEL );
	return s_pPackSelPanel;
}


// ---------------------------------------------------------------------------
// DM / non-teamplay class-only selection (menu 13). John: "in gateroom there
// isn't any teams - is this deathmatch? if so on deathmatch we don't show a
// team list, we show a custom class list that has ALL classes."
// Grid: 2 columns x 5 rows = all 10 classes (Torri 1-5 left, Goa'uld 6-10
// right), no team selector. A pick sends the GLOBAL class id with a single
// argument (Gene's server contract: `classe <1..10>`, implied team internally)
// and closes the menu, after which the server continues the pack/loadout flow.
// ---------------------------------------------------------------------------
class CSgtcHandler_DmClassClick : public ActionSignal
{
public:
	CSgtcHandler_DmClassClick( int iGlobalClass ) { m_iGlobalClass = iGlobalClass; }

	virtual void actionPerformed( Panel *panel )
	{
		char szCommand[32];
		snprintf( szCommand, sizeof( szCommand ), "classe %d", m_iGlobalClass );
		gEngfuncs.pfnClientCmd( szCommand );
		if ( gViewPort )
		{
			gViewPort->HideCommandMenu();
			gViewPort->UpdateCursorState();
		}
	}

private:
	int m_iGlobalClass;
};

class CSgtcDmClassPanel : public CMenuPanel
{
public:
	CSgtcDmClassPanel( int iTrans, int iRemoveMe, int x, int y, int wide, int tall )
		: CMenuPanel( iTrans, iRemoveMe, x, y, wide, tall )
	{
		CSchemeManager *pSchemes = gViewPort->GetSchemeManager();
		SchemeHandle_t hTitle = pSchemes->getSchemeHandle( "Title Font" );
		SchemeHandle_t hText = pSchemes->getSchemeHandle( "Briefing Text" );
		int r, g, b, a;

		// Stronger frame/title than the other panels: John reported the DM panel
		// looked faint (title nearly invisible against the map).
		TextPanel *pBackground = new TextPanel( "", 21, 21, ScreenWidth - 42, ScreenHeight - 42 );
		pBackground->setParent( this );
		pBackground->setBgColor( 0, 0, 0x28, 0xB4 );
		pBackground->setBorder( new LineBorder( Color( 120, 120, 255, 0 ) ) );

		m_pTitle = new Label( "", 0, YRES( 34 ), ScreenWidth, YRES( 26 ) );
		m_pTitle->setParent( this );
		m_pTitle->setFont( pSchemes->getFont( hTitle ) );
		m_pTitle->setFgColor( 0xE0, 0xE0, 0xFF, 0 );
		m_pTitle->setBgColor( 0, 0, 0, 0xff );
		m_pTitle->setContentAlignment( Label::a_center );
		m_pTitle->setText( "%s", "Select your class" );

		pSchemes->getFgColor( hText, r, g, b, a );

		const int iCellW = XRES( 190 );
		const int iCellH = YRES( 36 );
		const int iColGap = XRES( 40 );
		const int iStartX = ScreenWidth / 2 - iCellW - iColGap / 2;
		const int iStartY = YRES( 92 );

		for ( int iClass = 0; iClass < SGTC_TEAMCLASS_COUNT; iClass++ )
		{
			const int iTeam = iClass / SGTC_TEAMCLASS_PER_TEAM;	// 0 Torri, 1 Goauld
			const int iRow = iClass % SGTC_TEAMCLASS_PER_TEAM;

			m_pClassButton[iClass] = new CommandButton(
				L10n_ToLegacy( gHUD.m_TextMessage.BufferedLocaliseTextString( s_SgtcClasses[iClass].pszLabel ) ),
				iStartX + iTeam * ( iCellW + iColGap ),
				iStartY + iRow * YRES( 52 ),
				iCellW, iCellH );
			m_pClassButton[iClass]->setParent( this );
			m_pClassButton[iClass]->setContentAlignment( Label::a_center );
			m_pClassButton[iClass]->setFgColor( r, g, b, a );
			// Global id contract: 1..5 Torri, 6..10 Goauld (single argument).
			m_pClassButton[iClass]->addActionSignal( new CSgtcHandler_DmClassClick( iClass + 1 ) );
		}
	}

private:
	Label *m_pTitle;
	CommandButton *m_pClassButton[SGTC_TEAMCLASS_COUNT];
};

CMenuPanel *SgtcDmClass_ShowPanel( void )
{
	if ( !gViewPort )
		return NULL;

	SgtcPanels_CheckOwner();

	if ( !s_pDmClassPanel )
	{
		s_pDmClassPanel = (CMenuPanel *)new CSgtcDmClassPanel( 255, 0, 0, 0, ScreenWidth, ScreenHeight );
		s_pDmClassPanel->addInputSignal( new CSgtcHandler_InputDiag() );
		s_pDmClassPanel->setParent( gViewPort );
		s_pDmClassPanel->setVisible( false );
	}

	if ( !SgtcPanels_ShouldShow( SGTC_MENU_DMCLASS ) )
		return s_pDmClassPanel;

	SgtcPanels_ShowMenu( s_pDmClassPanel, SGTC_MENU_DMCLASS );
	return s_pDmClassPanel;
}

#endif // SGTC_REAL_VGUI
