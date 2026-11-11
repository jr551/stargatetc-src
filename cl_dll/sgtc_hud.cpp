// sgtc_hud.cpp — SGTC HUD usermsg parsers (ref client.dll REA @0x10072e5f..0x10072c62).
#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "ammohistory.h"
#include "sgtc_hud.h"
#include "sgtc_vgui.h"

// defined in ammo.cpp — hud.txt-style slot lookup inside a client_sprite_t list.
client_sprite_t *GetSpriteList( client_sprite_t *pList, const char *psz, int iRes, int iCount );
#include <stdio.h>
#include <math.h>
#include <string.h>

CHudSgtc g_SgtcHud;

// ref FUN_10076ba2 — Stamina byte → global DAT_100f71dc.
static int s_iSgtcStaminaByte;

static WEAPON *SGTC_IconWeapon( void );
static WEAPON *SGTC_CurrentWeapon( void );

int g_iSgtcVoteCount = -1;	// ref DAT_100babe4 (5 = vote window)
int g_iSgtcVoteSel = -1;	// ref DAT_100babe0 (0..4 mode highlight)
float g_flSgtcVoteAnim = 0;	// ref DAT_100f71e0 (mode cycle timer)
int g_iSgtcAnnouncePending = 0;	// ref DAT_100f1ce8 (Concuss byte 2 → scroller @0x10074366)

static void SGTC_DrawLocalizedHudLine( int x, int y, const char *token, int r, int g, int b )
{
	const char *text = CHudTextMessage::BufferedLocaliseTextString( token );
	gHUD.DrawHudString( x, y, ScreenWidth, text, r, g, b );
}

// ref @0x10076306 / @0x1007636f — DrawHudString x=0x14, y=ScreenHeight-0xdc, iMaxX=0x26c (ds:0x100ed4c4).
static void SGTC_DrawVotePromptLine( const char *token, int r, int g, int b )
{
	const char *text = CHudTextMessage::BufferedLocaliseTextString( token );
	gHUD.DrawHudString( 20, ScreenHeight - 220, 620, text, r, g, b );
}

// ref @0x100761dd — pulse m_iBlipPulse toward 0x9b using byte @+0x1685 as direction.
static void SGTC_TickBlipPulse( CHudSgtc *hud )
{
	if( hud->m_iBlipPulse < 0x9b && !hud->m_cBlipPulseDir )
		hud->m_iBlipPulse++;
	else if( hud->m_iBlipPulse > 0 && hud->m_cBlipPulseDir )
		hud->m_iBlipPulse--;

	if( hud->m_iBlipPulse <= 0 || hud->m_iBlipPulse >= 0x9b )
		hud->m_cBlipPulseDir = hud->m_cBlipPulseDir ? 0 : 1;
}

// ref @0x10074a20..0x10074e58 — ticket dock slide + sprite rect helper @0x10051150.
static void SGTC_TickTicketDock( CHudSgtc *hud, float flTime )
{
	if( !hud->m_iTicketModeFlag )
		return;

	int slideDir = 0;	// ref ebp-0xb8 (1 = shrink offset @0x10074d4e)
	hud->m_cTicketSlideDir = 0;
	hud->m_bTicketSlide256 = 0;
	hud->m_bTicketSlide190 = 0;

	// ref @0x10074ad7 — cascade subtracts (not else-if); may peel multiple tiers per frame.
	if( hud->m_iTicketSlideRemain > 0 )
	{
		int &rem = hud->m_iTicketSlideRemain;
		if( rem >= 0x600 )
			rem -= 0x600;
		if( rem >= 0x300 )
			rem -= 0x300;
		if( rem >= 0x180 )
			rem -= 0x180;
		if( rem >= 0xc0 )
			rem -= 0xc0;
		if( rem >= 0x60 )
			rem -= 0x60;
		if( rem >= 0x30 )
			rem -= 0x30;
		if( rem >= 0x18 )
		{
			slideDir = 2;
			rem -= 0x18;
		}
		if( rem >= 0xc )
		{
			slideDir = 1;
			rem -= 0xc;
		}
		if( rem >= 0x6 )
		{
			hud->m_bTicketDockLayout78 = 1;
			rem -= 0x6;
		}
		if( rem >= 0x3 )
		{
			hud->m_bTicketSlide190 = 1;
			rem -= 0x3;
		}
		if( rem >= 0x2 )
		{
			hud->m_bTicketSlide256 = 1;
			rem -= 0x2;
		}
		else if( rem > 0 )
			rem = 0;
	}

	// ref @0x10074c97 — skip frame/offset latch until m_flKeyTime reached.
	if( flTime < hud->m_flKeyTime )
		return;

	// ref @0x10074cb1 — dock frame/tint latch only when slideDir==2 this frame.
	if( slideDir == 2 )
	{
		hud->m_iTicketDockFrame++;
		if( hud->m_iTicketDockFrame > 10 )
			hud->m_iTicketDockFrame = 0;
	}

	if( slideDir == 1 )
	{
		if( hud->m_iTicketSlideOffset > 0x32 )
			hud->m_iTicketSlideOffset -= 2;
	}
	else if( hud->m_iTicketSlideOffset < 0x7d )
		hud->m_iTicketSlideOffset += 2;

	hud->m_cTicketSlideDir = (char)slideDir;
}

// ref @0x10075719-0x1007582d — peel overlay (+0x1684 active): pct = (+0x1704*100)/+0x168c
// (@0x1007571f-0x1007575c; +0x168c latches +0x1704 when zero @0x1007571f).  Bar rect from
// +0x1738: right = ftol(pct/0x10095a48*0x1009b3f8), bottom = 0x12 (@0x10075781-0x100757b7);
// sprite +0x1734, drawn at (0x4f, H-0x23) via SPR_DrawHoles (@0x10075805/0x10075825).
// Tint verified @0x100757ba-0x100757d4: ftol(pct/0x10095a48*0x10095a48)+100 == (int)pct + 100,
// skipped when <= 100 (@0x100757da) — matches the port's `pct + 100` guard exactly.
static void SGTC_DrawTicketDockPeel( CHudSgtc *hud )
{
	if( !hud->m_iArmorParam )
		hud->m_iArmorParam = hud->m_iGtickets;
	if( hud->m_iArmorParam <= 0 )
		return;

	const int pct = ( hud->m_iGtickets * 100 ) / hud->m_iArmorParam;
	const int ammoIdx = gHUD.GetSpriteIndex( "ammoline" );
	if( !ammoIdx )
		return;

	wrect_t rc = gHUD.GetSpriteRect( ammoIdx );
	rc.right = (int)( (float)pct / 100.0f * 105.0f );	// ref @0x100757c8 — bar width = pct*1.05 (ftol)
	rc.bottom = 0x12;									// ref @0x100757e8 — absolute 0x12, not top+0x12
	const int tint = (int)pct + 100;
	if( tint <= 100 )
		return;

	SPR_Set( gHUD.GetSprite( ammoIdx ), tint, tint, tint );
	SPR_DrawHoles( 0, 0x4f, ScreenHeight - 0x23, &rc );
}

// ref @0x10075e22-0x10075e4b / @0x10076033-0x1007605c — HUD digit width, i.e.
// GetSpriteRect(number_0).right - .left (same value CHud::DrawHudNumber uses @hud_redraw.cpp:330).
static int SGTC_DigitWidth( void )
{
	return gHUD.GetSpriteRect( gHUD.m_HUD_number_0 ).right - gHUD.GetSpriteRect( gHUD.m_HUD_number_0 ).left;
}

static int SGTC_NumberSprite( int digit )
{
	char name[16];
	snprintf( name, sizeof( name ), "number_%d", digit % 10 );
	return gHUD.GetSpriteIndex( name );
}

// ref FUN_10057b66 / shipped DrawHudNumber — additive glyph (ds:0x100ed744).
static void SGTC_BlitHudDigit( int x, int y, int digit, int r, int g, int b )
{
	const int idx = SGTC_NumberSprite( digit );
	if( idx <= 0 )
		return;
	// SIGSEGV @0x492b0: sprite table can still be NULL during menu-11/NewRound
	// (hud.txt sprites not bound); guard like the separator below.
	const HSPRITE h = gHUD.GetSprite( idx );
	const wrect_t *rc = &gHUD.GetSpriteRect( idx );
	if( !h || !rc )
		return;
	SPR_Set( h, r, g, b );
	SPR_DrawAdditive( 0, x, y, rc );
}

// ref shipped FUN_100718df @0x10075d2d — [MM][2points][SS]; gate +0x16f0 && !+0x58.
static void SGTC_DrawRoundTimer( CHudSgtc *hud, float flTime )
{
	const float rem = hud->m_flRoundStart + hud->m_flRoundTime - gHUD.m_flTime;
	// Mode 1 (capture/TDM maps, e.g. ctp_amon) sends TicketMode 1 while the ref
	// still draws the bottom-left round timer alongside the ticket dock; the old
	// gate suppressed the timer on every ticket-mode map even once the server
	// armed it (#253). Keep suppressing only when the timer is off or a
	// non-mode-1 ticket HUD must own the slot.
	if( !hud->m_iRoundTimerOn || ( hud->m_iTicketModeFlag && hud->m_iRoundTimerMode != 1 ) )
		return;

	// P43g: alt arm tracks the SetRndTim mode byte (ref +0x70), not g_iUser1.
	const bool altArm = ( hud->m_iRoundTimerMode == 2 );

	SPR_DisableScissor();
	const int x0 = altArm ? ScreenWidth / 2 : 0x23;
	const int y0 = altArm ? ScreenHeight / 10
	                    : ScreenHeight - 0x8e + gHUD.m_iHudNumbersYOffset;
	const int r = 0xa8, g = 0xd3, b = 0xff;
	const int mins = ( rem < 0.0f ) ? 0 : (int)( rem / 60.0f );
	const int secs = ( rem < 0.0f ) ? 0 : (int)rem % 0x3c;

	if( altArm )
	{
		// P43j: orig :79 top-centre clock sits ~y=5 (cr3_top), not H/10 (~76). Xash additive
		// digits read white on FB — use SGTC_BlitHudDigit (same as live-FP timer @0x10073256).
		const int yAlt = 5 + gHUD.m_iHudNumbersYOffset;
		int wAlt = SGTC_DigitWidth();
		if( wAlt <= 0 )
			wAlt = 16;
		int x = ( x0 + 0x12 ) - ( wAlt * 9 ) / 4;
		if( mins >= 10 )
		{
			SGTC_BlitHudDigit( x, yAlt, mins / 10, r, g, b );
			x += wAlt;
		}
		else
			x += wAlt;
		SGTC_BlitHudDigit( x, yAlt, mins % 10, r, g, b );
		const int dotIdx = gHUD.GetSpriteIndex( "2points" );
		if( dotIdx > 0 )
		{
			SPR_Set( gHUD.GetSprite( dotIdx ), r, g, b );
			SPR_DrawAdditive( 0, x0 - wAlt / 8, yAlt, &gHUD.GetSpriteRect( dotIdx ) );
		}
		x = x0 + ( wAlt * 3 ) / 8;
		if( secs >= 10 )
		{
			SGTC_BlitHudDigit( x, yAlt, secs / 10, r, g, b );
			x += wAlt;
		}
		else
		{
			SGTC_BlitHudDigit( x, yAlt, 0, r, g, b );
			x += wAlt;
		}
		SGTC_BlitHudDigit( x, yAlt, secs % 10, r, g, b );
		return;
	}

	const int w = SGTC_DigitWidth();
	if( w <= 0 )
		return;

	// Minutes — ref FUN_10057b66 flags 3 @0x10075ee5 (live-FP bottom-left)
	int x = ( x0 + 0x12 ) - ( w * 9 ) / 4;
	if( mins >= 10 )
	{
		SGTC_BlitHudDigit( x, y0, mins / 10, r, g, b );
		x += w;
	}
	else
		x += w; // DHN_2DIGITS column skip
	SGTC_BlitHudDigit( x, y0, mins % 10, r, g, b );

	const int dotIdx = gHUD.GetSpriteIndex( "2points" );
	if( dotIdx > 0 )
	{
		SPR_Set( gHUD.GetSprite( dotIdx ), r, g, b );
		SPR_DrawAdditive( 0, x0 - w / 8, y0, &gHUD.GetSpriteRect( dotIdx ) );
	}

	x = x0 + ( w * 3 ) / 8;
	if( secs >= 10 )
	{
		SGTC_BlitHudDigit( x, y0, secs / 10, r, g, b );
		x += w;
	}
	else
	{
		SGTC_BlitHudDigit( x, y0, 0, r, g, b );
		x += w;
	}
	SGTC_BlitHudDigit( x, y0, secs % 10, r, g, b );

}

static void SGTC_DrawTicketDock( CHudSgtc *hud, float flTime )
{
	if( !hud->m_iTicketModeFlag )
		return;

	const int iDock = gHUD.GetSpriteIndex( "ticketdock" );
	if( !iDock )
		return;
	wrect_t dock = gHUD.GetSpriteRect( iDock );

	// ref CHudSgtc::Draw @0x10074a4c / @0x10074e8b — fixed anchor; SPR_Set tints (not position).
	const int x = ScreenWidth - 0x4e;
	const int y = ScreenHeight - 0x8e;

	int tintR, tintG, tintB;
	if( hud->m_cTicketSlideDir == 1 )	// ref @0x10074d4e
		tintR = tintG = tintB = hud->m_iTicketSlideOffset;
	else if( hud->m_iTicketDockFrame > 5 )	// ref @0x10074ccd
	{
		tintR = 0xf3;
		tintG = 0xb7;
		tintB = 0;
	}
	else
	{
		tintR = 0xff;
		tintG = 0xd5;
		tintB = 0x53;
	}
	if( hud->m_bTicketDockLayout78 )	// ref @0x10074df8
	{
		tintR = 0x78;
		tintG = 0x78;
		tintB = 0x32;
	}
	if( flTime < hud->m_flKeyTime )	// ref @0x10074e25
	{
		tintG = 0;
		tintB = 0;
	}

	SPR_Set( gHUD.GetSprite( iDock ), tintR, tintG, tintB );
	SPR_DrawAdditive( 0, x, y, &dock );

	// ref client_Draw_FUN100718df.c:531-540 — second ticketdock plate under TicketMode at (0, H-0xa8), tint 0x7d
	SPR_Set( gHUD.GetSprite( iDock ), 0x7d, 0x7d, 0x7d );
	SPR_DrawHoles( 0, 0, ScreenHeight - 0xa8, &dock );

	if( hud->m_iArmorOverlay != 0 )
	{
		SGTC_DrawTicketDockPeel( hud );
	}
	else
	{
		// ref @0x10074f0e — Think-fed ammo counters (+0x1704 @x=0x82, +0x1708 @x=0x46),
		// not the +0x5c/+0x60 ticket tallies.
		const int numY = ScreenHeight - 0x25;
		// ref client_Draw_FUN100718df.c:471-475 — FUN_10057b66 flags 5 (DHN_3DIGITS|DHN_DRAWZERO), tint 0x7d
		if( hud->m_iDockAmmoG > 0 )
			gHUD.DrawHudNumber( 0x46, numY, DHN_DRAWZERO | DHN_3DIGITS, hud->m_iDockAmmoG, 0x7d, 0x7d, 0x7d );
		if( hud->m_iDockAmmoT > 0 )
			gHUD.DrawHudNumber( 0x82, numY, DHN_DRAWZERO | DHN_3DIGITS, hud->m_iDockAmmoT, 0x7d, 0x7d, 0x7d );
		if( hud->m_iDockAmmoG > 0 && hud->m_iDockAmmoT > 0 )
		{
			const int ammoIdx = gHUD.GetSpriteIndex( "ammoline" );
			if( ammoIdx > 0 )
			{
				wrect_t bar = gHUD.GetSpriteRect( ammoIdx );
				SPR_Set( gHUD.GetSprite( ammoIdx ), 0x7d, 0x7d, 0x7d );
				SPR_DrawHoles( 0, 0x78, numY, &bar );
			}
		}
	}

	// ref @0x10074eba — after m_flKeyTime (+0x28), slidein overlay on ticket dock anchor.
	if( flTime <= hud->m_flKeyTime )
		return;
	const int siIdx = gHUD.GetSpriteIndex( "slidein" );
	if( !siIdx )
		return;
	wrect_t si = gHUD.GetSpriteRect( siIdx );
	if( hud->m_bTicketSlide256 )
	{
		SPR_Set( gHUD.GetSprite( siIdx ), 255, 0x82, 0 );
		si.right = si.left + 0x100;
		si.bottom = si.top + 0x82;
		SPR_DrawAdditive( 0, x, y, &si );
	}
	else if( hud->m_bTicketSlide190 )
	{
		SPR_Set( gHUD.GetSprite( siIdx ), 255, 0xbe, 0 );
		si.right = si.left + 0xbe;
		si.bottom = si.top + 0xbe;
		SPR_DrawAdditive( 0, x, y, &si );
	}
}

// Port substitute for the three stat wells inside the hud_left panel.
// The reference client never drew life/armor/stamina in the HUD (those values
// fed the Win32/VGUI panels), so these fills are an Xash substitute.  They use
// the statbar sprite (16x64) cropped bottom-up inside the three panel wells
// (hud_left art: 16x60 inner wells at x=4/28/52, y=82 of the 200x168 panel).
// Previous revisions cropped the 16-wide sprite horizontally and drew it at
// y=H-0x18, which produced the mis-placed "fills" seen in playtests.
static void SGTC_DrawWellFill( const char *barSprite, int wellX, float frac, int r, int g, int b )
{
	if( frac < 0.0f )
		frac = 0.0f;
	if( frac > 1.0f )
		frac = 1.0f;
	if( frac <= 0.0f )
		return;

	const int barIdx = gHUD.GetSpriteIndex( barSprite );
	if( barIdx <= 0 )
		return;

	wrect_t rc = gHUD.GetSpriteRect( barIdx );
	const int fullH = rc.bottom - rc.top;
	if( fullH <= 0 )
		return;

	const int wellY = ScreenHeight - 168 + 82;
	const int wellH = 60;
	const int fillH = (int)( wellH * frac + 0.5f );
	if( fillH <= 0 )
		return;

	// bottom-anchored: the sprite's top edge sits at the current value level
	rc.bottom = rc.top + ( fillH <= fullH ? fillH : fullH );
	SPR_Set( gHUD.GetSprite( barIdx ), r, g, b );
	SPR_DrawHoles( 0, wellX, wellY + wellH - ( rc.bottom - rc.top ), &rc );
}

static int SGTC_Sprite( const char *name, wrect_t *prc );

// ref @0x10074f66..0x100750c5 — ammoline / hud_top / drugs / tac legend entrance.
static void SGTC_TickEntranceSlides( CHudSgtc *hud )
{
	if( hud->m_iHudInfoMode == 0 )
	{
		if( hud->m_iAmmoLineSlide < 0x1d && hud->m_iAmmoLineStep < 0x46 )
			hud->m_iAmmoLineStep++;
		else if( hud->m_iAmmoLineSlide < 0x1d && hud->m_iAmmoLineStep >= 0x46 )
			hud->m_iAmmoLineSlide++;
	}
	else if( hud->m_iHudInfoMode > 0 && hud->m_iAmmoLineSlide > 0 )
		hud->m_iAmmoLineSlide--;

	if( hud->m_iKeyMode < 1 )
	{
		if( hud->m_iKeyBannerSlide < 0xa0 && hud->m_iKeyBannerStep < 0x46 )
			hud->m_iKeyBannerStep++;
		else if( hud->m_iKeyBannerSlide < 0xa0 && hud->m_iKeyBannerStep >= 0x46 )
			hud->m_iKeyBannerSlide += 2;
	}
	else
	{
		if( hud->m_iKeyBannerSlide > 0 )
			hud->m_iKeyBannerSlide -= 2;
		// ref @0x100759de-0x10075a58 — the tick never clears +0x1560; the bombkey banner block
		// (@0x10075ade) does once the slide reaches 0.
	}
}

static void SGTC_DrawEntranceBanners( CHudSgtc *hud )
{
	wrect_t rc;
	const int ammoIdx = gHUD.GetSpriteIndex( "ammoline" );
	if( ammoIdx > 0 )
	{
		rc = gHUD.GetSpriteRect( ammoIdx );
		const int h = rc.bottom - rc.top;
		const int yAmmo = ScreenHeight - h - 0x37 + hud->m_iAmmoLineSlide;
		SPR_Set( gHUD.GetSprite( ammoIdx ), 0x7d, 0x7d, 0x7d );
		SPR_DrawHoles( 0, 0x60, yAmmo, &rc );

		// ref client_Draw_FUN100718df.c:259-262 — hud_left full rect at (0, H-0xa8); reverts P24 sgtc2 crop.
		const int hlIdx = gHUD.GetSpriteIndex( "hud_left" );
		if( hlIdx > 0 )
		{
			wrect_t bar = gHUD.GetSpriteRect( hlIdx );
			SPR_Set( gHUD.GetSprite( hlIdx ), 0x7d, 0x7d, 0x7d );
			SPR_DrawHoles( 0, 0, ScreenHeight - 0xa8, &bar );
		}
	}

	// ref @0x10075abb-0x10075b84 — bombkey banner (+0x1cf8 handle / +0x1cfc rect) at (0x2f, H-0x140),
	// tint (0,0x64,0), SPR_DrawHoles; shown while key mode is up and the banner slide is home.
	// Reaches @0x10075abb (m_iKeyMode>0), @0x10075ad1 (slide==0), @0x10075ade (step clear).
	// Port fix (John: SP shows a stuck "nuke key" banner): m_iKeyMode persists
	// across map changes in the client library, so a listen/MP session leaves it
	// set when a single-player map without the key objective loads. Only draw
	// the banner when the map really carries the key objective (m_iHasKey).
	if( hud->m_iKeyMode > 0 && hud->m_iHasKey && hud->m_iKeyBannerSlide == 0 )
	{
		hud->m_iKeyBannerStep = 0;	// ref @0x10075ade

		wrect_t rcKey;
		if( SGTC_Sprite( "bombkey", &rcKey ) )
		{
			SPR_Set( gHUD.GetSprite( gHUD.GetSpriteIndex( "bombkey" ) ), 0, 0x64, 0 );
			SPR_DrawHoles( 0, 0x2f, ScreenHeight - 0x140, &rcKey );
		}
	}

	// ref @0x1007582e-0x100758e0 — drugs strip at (0x4b, H-0x34), rect.right = left+0x6b,
	// rect.bottom += 8, tint 0x78, SPR_DrawHoles (@0x100758ab/0x100758d8).  Gate in ref is the
	// cached handle +0x170c != 0 (@0x10075834); that member has NO writer anywhere in ref/.text
	// (objdump: the only reads are @0x10075834/0x100758b7) so the ref block is unreachable there.
	if( hud->m_iKeyMode > 0 && hud->m_iHasKey && hud->m_iKeyBannerSlide == 0 && SGTC_Sprite( "drugs", &rc ) )
	{
		const int dIdx = gHUD.GetSpriteIndex( "drugs" );
		wrect_t dr = gHUD.GetSpriteRect( dIdx );
		dr.right = dr.left + 0x6b;			// ref @0x100758a2 (store to +4)
		dr.bottom = dr.bottom + 8;			// ref @0x10075865 (store to +0xc)
		SPR_Set( gHUD.GetSprite( dIdx ), 0x78, 0x78, 0x78 );
		SPR_DrawHoles( 0, 0x4b, ScreenHeight - 0x34, &dr );
	}

	// ref @0x100758e1-0x10075995 — weapon-icon panel.  Gate: +0x1714 sgtcweapon
	// HSPRITE set by Think (weapon->+0x150 via FUN_1007671c), rect from +0x1718.
	// Rect forced to {left=0, right=0xb0, top=0, bottom=0x30} (176x48 hole crop);
	// drawn SPR_DrawAdditive at (W-0xb8, H-0x38), tint 0x78 — ref client_Draw_FUN100718df.c:491-499
	// Ref gates this widget on "the cached icon is the current weapon, HUD visible,
	// player alive, weapons hook not hidden" (DAT_100e52c4/0x80000000/5 checks).
	// Without the gates the last cached icon (e.g. the medic syringe) stayed on
	// screen after switching weapons or dying.
	if( hud->m_iWeaponIconSpr != 0
		&& SGTC_IconWeapon() != NULL && SGTC_IconWeapon() == SGTC_CurrentWeapon()
		&& !gHUD.m_fPlayerDead
		&& !( gHUD.m_iHideHUDDisplay & ( HIDEHUD_WEAPONS | HIDEHUD_ALL )))
	{
		wrect_t wi = hud->m_rcWeaponIcon;	// ref +0x1718 (sprite cache record)
		wi.left = 0;				// ref @0x1007591b
		wi.right = 0xb0;			// ref @0x1007592e (store to +4)
		wi.top = 0;					// ref @0x10075941
		wi.bottom = 0x30;			// ref @0x10075953 (store to +0xc)
		SPR_Set( hud->m_iWeaponIconSpr, 0x78, 0x78, 0x78 );
		SPR_DrawAdditive( 0, ScreenWidth - 0xb8, ScreenHeight - 0x38, &wi );
	}

	// ref @0x10075a5e-0x10075ab8 — "slidein" sprite strip, drawn unconditionally after
	// the key/hud_top slide FSM.  x = -0x14 - slide(+0x155c), y = H-0x15e, tint 0x7d,
	// SPR_DrawHoles (ds:0x100ed728).  Slides in as +0x155c falls toward 0.
	{
		const int siIdx = gHUD.GetSpriteIndex( "slidein" );
		if( siIdx > 0 )
		{
			wrect_t si = gHUD.GetSpriteRect( siIdx );
			SPR_Set( gHUD.GetSprite( siIdx ), 0x7d, 0x7d, 0x7d );
			SPR_DrawHoles( 0, -0x14 - hud->m_iKeyBannerSlide, ScreenHeight - 0x15e, &si );
		}
	}

	if( hud->m_iHudInfoMode > 0 && hud->m_iAmmoLineSlide == 0 )
	{
		const char *tac = NULL;
		if( hud->m_iHudInfoMode == 1 )
			tac = "tac_red";
		else if( hud->m_iHudInfoMode == 2 )
			tac = "tac_yellow";
		else if( hud->m_iHudInfoMode == 3 )
			tac = "tac_green";
		if( tac && SGTC_Sprite( tac, &rc ) )
		{
			const int tIdx = gHUD.GetSpriteIndex( tac );
			SPR_Set( gHUD.GetSprite( tIdx ), 0x7d, 0x7d, 0x7d );
			SPR_DrawHoles( 0, 0x73, ScreenHeight - 0x4c, &rc );
		}
	}
}

// ref @0x1004b45a / TeamNames VGUI @0x10057bed — fill DAT_100e1a34[0..0x1b] from resource bytes.
static void SGTC_InitProgressSlotX( CHudSgtc *hud )
{
	for( int i = 0; i < SGTC_BLIP_SLOTS; i++ )
		hud->m_iProgressSlotX[i] = ( i < 14 ) ? 0x10 : 0x12;
}

// ref FUN_1002a9ca @0x1002abe2: slots 0..13 use DAT_100e1a34[i]; slots 14..27 use
// (&DAT_100e1a6c)[i] which aliases DAT_100e1aa4 + (i-14)*4 (progress team ints).
static int SGTC_ProgressSlotStep( const CHudSgtc *hud, int slot )
{
	if( slot < 14 )
		return hud->m_iProgressSlotX[slot];
	return hud->m_iProgressTeam[slot - 14];
}

// ref FUN_1002a9ca tail @0x1002acc2: chip stays up when captured (team==1) unless st==2 or raw step==0.
static int SGTC_ProgressSlotChipVisible( const CHudSgtc *hud, int slot )
{
	const int st = hud->m_iProgressTeamUiState[slot];
	if( st == 2 || hud->m_iProgressSlotX[slot] == 0 )
		return 0;
	return hud->m_iProgressTeam[slot] == 1;
}

// ref FUN_1002a9ca @0x1002a9ca — per-slot team chip state (VGUI widgets on Win32; HUD SPR on Xash).
static void SGTC_TickProgressTeamUi( CHudSgtc *hud )
{
	// ref panel update has no m_iProgress gate; only Draw hides chips when progress==0.
	// ref FUN_1002a9ca entry: DAT_100e1b90 recomputed every VGUI/progress frame.
	hud->m_iProgressIconCursor = 0;

	for( int i = 0; i < SGTC_BLIP_SLOTS; i++ )
	{
		int &st = hud->m_iProgressTeamUiState[i];

		if( st == 1 )
			st = 0;

		const int team = hud->m_iProgressTeam[i];

		if( team == 0 )
		{
			if( st < 2 )
				st = 2;
		}
		else if( team == 1 )
		{
			if( st == 2 )
				st = 0;
		}
		else if( team == 2 )
		{
			hud->m_iProgressIconCursor += SGTC_ProgressSlotStep( hud, i );
			st = 1;
			hud->m_iProgressTeam[i] = 1;	// ref @0x1002acb7
		}
	}
}

// ref Progress msg byte1 -> DAT_100e1b94 (width budget in VGUI @0x1002944d; star tint in HUD).
static const char *SGTC_ProgressIconSprite( int icon )
{
	if( icon == 1 )
		return "star_g";
	if( icon == 2 )
		return "star_p";
	if( icon == 3 )
		return "star_b";
	return "star_s";
}

static void SGTC_DrawProgressTeamUi( CHudSgtc *hud )
{
	if( hud->m_iHideHud || hud->m_iProgress <= 0 )
		return;

	const char *sprName = SGTC_ProgressIconSprite( hud->m_iProgressIcon );
	const int idx = gHUD.GetSpriteIndex( sprName );
	if( !idx )
		return;

	wrect_t rc = gHUD.GetSpriteRect( idx );
	const int anchorX = ScreenWidth / 2 - hud->m_iProgressIconCursor;
	const int rowY0 = ScreenHeight - 0xb0;
	const int rowY1 = rowY0 + 0xe;

	int xWalk = 0;
	for( int i = 0; i < SGTC_BLIP_SLOTS; i++ )
	{
		if( !SGTC_ProgressSlotChipVisible( hud, i ) )
		{
			xWalk += SGTC_ProgressSlotStep( hud, i );
			continue;
		}

		const int y = ( i < 14 ) ? rowY0 : rowY1;
		const int x = anchorX + xWalk;
		xWalk += SGTC_ProgressSlotStep( hud, i );
		SPR_Set( gHUD.GetSprite( idx ), 255, 255, 255 );
		SPR_DrawHoles( 0, x, y, &rc );
	}
}

// ref @0x100750c5 — ramp m_iBlipProgress toward m_iProgress while 0 < progress < 100.
static void SGTC_TickBlipBarProgress( CHudSgtc *hud, float flTime )
{
	if( hud->m_iProgress <= 0 || hud->m_iProgress >= 100 )
		return;

	// ref @0x100750db: x87 `test ah,0x41` after comparing flTime to the latch => strict flTime > latch.
	if( flTime > hud->m_flBlipProgressLatch )
	{
		hud->m_iBlipProgress++;
		hud->m_flBlipProgressLatch = flTime + 0.15f; // ref fadd QWORD @0x10075115 (ds:0x10094a78 = 0.15)
	}

	const int iFloor = hud->m_iProgress - 7;
	if( hud->m_iBlipProgress < iFloor )
		hud->m_iBlipProgress = hud->m_iProgress;
}

// ref @0x10076464 — vote cast lines: x=ScreenWidth/3, iMaxX=ScreenWidth/3+0x12c.
static void SGTC_DrawVoteCastHudLine( int y, const char *token, int r, int g, int b )
{
	const char *text = CHudTextMessage::BufferedLocaliseTextString( token );
	const int x = ScreenWidth / 3;
	gHUD.DrawHudString( x, y, x + 300, text, r, g, b );
}

static void SGTC_VoteFormatCenter( int modeIndex )
{
	char buf[256];
	const char *fmt = gHUD.m_TextMessage.LookupString( "VoteMode", NULL );
	if( !fmt || !fmt[0] )
		fmt = "VoteMode %d";
	snprintf( buf, sizeof( buf ), fmt, modeIndex );
	CenterPrint( buf );
}

// ref @0x100612d2 — votecastmenu client command (cycles vote mode index).
static void SGTC_VoteCastMenu_f( void )
{
	if( g_iSgtcVoteCount >= 5 )
		return;
	const float now = gHUD.m_flTime;
	if( now <= g_flSgtcVoteAnim )
	{
		g_iSgtcVoteSel = 0;
		g_flSgtcVoteAnim = now + 5.0f;
	}
	else
	{
		g_flSgtcVoteAnim = now + 0.5f;
		g_iSgtcVoteSel++;
		if( g_iSgtcVoteSel > 4 )
			g_iSgtcVoteSel = 0;
	}
}

static void SGTC_DrawVoteCastMenu( float flTime )
{
	if( g_iSgtcVoteCount < 0 )	// window open at count>=0 (server sets 5)
		return;
	// ref @0x10076374 — flTime >= g_flSgtcVoteAnim → confirm @0x10076623 (sel>=0).
	if( flTime >= g_flSgtcVoteAnim )
	{
		if( g_iSgtcVoteSel >= 0 )
		{
			// ref @0x10076623 — confirm sends ClientCmd("VoteMode %d") server-side,
			// plays rt_button.wav, latches DAT_100babe4=sel, DAT_100babe0=-1.
			char cmd[260];
			snprintf( cmd, sizeof( cmd ), "VoteMode %d\n", g_iSgtcVoteSel );
			ClientCmd( cmd );
			gEngfuncs.pfnPlaySoundByName( "buttons/rt_button.wav", 1.0f );
			g_iSgtcVoteCount = g_iSgtcVoteSel;
			g_iSgtcVoteSel = -1;
		}
		return;
	}

	static const char *modeTok[] = {
		"1. #MODE0", "2. #MODE1", "3. #MODE2", "4. #MODE3", "5. #MODE4"
	};
	// ref @0x10076464 — y = ScreenHeight/2 + (-0x32..+0x1e), x = ScreenWidth/3.
	const int midY = ScreenHeight / 2;
	SGTC_DrawVoteCastHudLine( midY - 0x32, "#SELPREF", 100, 100, 255 );
	const int modeY[] = { midY - 0x1e, midY - 0xf, midY, midY + 0xf, midY + 0x1e };
	for( int i = 0; i < 5; i++ )
	{
		int r = 255, g = 255, b = 255;
		if( i == g_iSgtcVoteSel )
			r = 0;	// ref @0x1007646f — selected line r=0, g/b=0xff
		SGTC_DrawVoteCastHudLine( modeY[i], modeTok[i], r, g, b );
	}
}

static void SGTC_InitPortraitSlots( CHudSgtc *hud );

// ref PTR_DAT_100babe8[32] — announce / mode tokens (Concuss byte 2 indexes this).
static const char *SGTC_AnnounceTokenById( int id )
{
	static const char *const kTable[] = {
		NULL, "#KEY_COLLECTED", "#KEY_DROPPED", "#BOMB_ARMING", "#BOMB_ARMING_INTERRUPTED",
		"#BOMB_ARMED", "#CTRL_CAP_ENEMY", "#CTRL_CAP_ALLY", "#STRUCT_ATTACKED",
		"#ENEMY_CAP_KILLED", "#ALLY_CAP_KILLED", "#TOURN_START", "#TOURN_ABORTED", "#TOURN_AWARD",
		"#CTP_STND_TAU", "#CTP_STND_GOU",
		"#MODE0", "#MODE1", "#MODE2", "#MODE3", "#MODE4",
		"radio/son00.wav", "radio/son01.wav", "radio/son02.wav", "radio/son03.wav",
		"radio/son04.wav", "radio/son05.wav", "radio/son06.wav", "radio/son07.wav",
		"radio/son08.wav", "radio/son09.wav", "radio/son10.wav",
	};
	if( id < 0 || id >= (int)( sizeof( kTable ) / sizeof( kTable[0] ) ) )
		return NULL;
	return kTable[id];
}

static void SGTC_InitAnnounceLines( CHudSgtc *hud )
{
	for( int i = 0; i < SGTC_ANNOUNCE_LINES; i++ )
	{
		hud->m_szAnnounceTok[i] = NULL;
		hud->m_flAnnounceLatch[i] = 0.0f;
		hud->m_iAnnounceScroll[i] = 0;
	}
}

// ref @0x10074366 — shift queue + latch new token on DAT_100f1ce8.
static void SGTC_PushAnnounceLine( CHudSgtc *hud, float flTime, int tokenId )
{
	const char *tok = SGTC_AnnounceTokenById( tokenId );
	for( int i = 0; i < SGTC_ANNOUNCE_LINES - 1; i++ )
	{
		hud->m_szAnnounceTok[i] = hud->m_szAnnounceTok[i + 1];
		hud->m_flAnnounceLatch[i] = hud->m_flAnnounceLatch[i + 1];
		hud->m_iAnnounceScroll[i] = hud->m_iAnnounceScroll[i + 1];
	}
	const int last = SGTC_ANNOUNCE_LINES - 1;
	hud->m_szAnnounceTok[last] = tok;
	hud->m_flAnnounceLatch[last] = flTime;
	hud->m_iAnnounceScroll[last] = 0;
	gEngfuncs.pfnPlaySoundByName( "buttons/log.wav", 1.0f );
}

// ref @0x100744d4..0x10074757 — fade + DrawHudString + FillRGBA frame.
static void SGTC_DrawAnnounceScroller( CHudSgtc *hud, float flTime )
{
	if( g_iSgtcAnnouncePending > 0 )
	{
		SGTC_PushAnnounceLine( hud, flTime, g_iSgtcAnnouncePending );
		g_iSgtcAnnouncePending = 0;
	}

	const int lineH = 0xc;
	const int lineGap = 3;
	const int lineCount = SGTC_ANNOUNCE_LINES;
	const int maxW = 0x12c;
	int baseX = ScreenWidth / 2 - ( ( lineH + lineGap ) * lineCount ) * 2;
	int baseY = ScreenHeight / 5 - ( lineH * ( lineCount + lineGap ) ) / 2;
	if( baseY < 0x69 )
		baseY = 0x69;

	int minScroll = 100;
	for( int i = 0; i < lineCount; i++ )
	{
		int fade = 0;
		if( hud->m_iAnnounceScroll[i] == 0 && hud->m_flAnnounceLatch[i] != 0.0f
			&& flTime > hud->m_flAnnounceLatch[i] + 6.0f )
			fade = 1;
		if( hud->m_iAnnounceScroll[i] > 0 && hud->m_flAnnounceLatch[i] != 0.0f
			&& flTime > hud->m_flAnnounceLatch[i] + 0.08f )
			fade = 1;

		if( fade && hud->m_iAnnounceScroll[i] < 100 && hud->m_flAnnounceLatch[i] != 0.0f )
		{
			hud->m_iAnnounceScroll[i]++;
			hud->m_flAnnounceLatch[i] = flTime;
		}

		if( hud->m_iAnnounceScroll[i] < minScroll && hud->m_flAnnounceLatch[i] != 0.0f )
			minScroll = hud->m_iAnnounceScroll[i];

		if( hud->m_iAnnounceScroll[i] > 99 )
		{
			hud->m_iAnnounceScroll[i] = 0;
			hud->m_flAnnounceLatch[i] = 0.0f;
			hud->m_szAnnounceTok[i] = NULL;
		}

		if( hud->m_flAnnounceLatch[i] == 0.0f || !hud->m_szAnnounceTok[i] )
			continue;

		const int scroll = hud->m_iAnnounceScroll[i];
		const int r = 100 - scroll;
		const int g = 100 - scroll;
		const int b = ( 100 - scroll ) * 2;
		const char *text = CHudTextMessage::BufferedLocaliseTextString( hud->m_szAnnounceTok[i] );
		const int y = baseY + i * ( lineH + lineGap );
		gHUD.DrawHudString( baseX + 4, y, baseX + 4 + maxW, text, r, g, b );
	}

	const int frameA = 100 - minScroll;
	const int frameH = ( lineH + lineGap ) * lineCount + 8;
	// ref @0x10074757 — full-box fill then 2px border strips (5× FillRGBA).
	FillRGBA( baseX, baseY - 4, maxW + 8, frameH, 0x32, 0x32, 0xff, frameA );
	FillRGBA( baseX, baseY - 4, 2, frameH, 0x32, 0x32, 0xff, frameA );
	FillRGBA( baseX + maxW + 6, baseY - 4, 2, frameH, 0x32, 0x32, 0xff, frameA );
	FillRGBA( baseX + 1, baseY - 4, maxW + 6, 2, 0x32, 0x32, 0xff, frameA );
	FillRGBA( baseX + 1, baseY - 4 + frameH, maxW + 6, 2, 0x32, 0x32, 0xff, frameA );
}

static void SGTC_TimerDump_f( void )
{
	const float rem = g_SgtcHud.m_flRoundStart + g_SgtcHud.m_flRoundTime - gHUD.m_flTime;
	gEngfuncs.Con_Printf(
		"[sgtc] timer_dump on=%d mode=%d ticket=%d t=%.0f rem=%.0f digitW=%d hideHUD=%d hud_draw=%g\n",
		g_SgtcHud.m_iRoundTimerOn, g_SgtcHud.m_iRoundTimerMode, g_SgtcHud.m_iTicketModeFlag,
		g_SgtcHud.m_flRoundTime, rem, SGTC_DigitWidth(), gHUD.m_iHideHUDDisplay,
		gHUD.m_pCvarDraw ? gHUD.m_pCvarDraw->value : -1.0f );
}

int CHudSgtc::Init( void )
{
	// The server's auto-pack path sends "sgtc_loadout_done" to dismiss the
	// still-open loadout panels (keyboard focus would otherwise swallow the
	// weapon-switch keys).
	SgtcPanels_RegisterClientCommands();
	// A new map must not inherit the key/bomb banner state from a previous
	// (multiplayer) session in the same client library instance.
	m_iKeyMode = 0;
	m_iHasKey = 0;
	m_iKeyBannerSlide = 0;
	m_iKeyBannerStep = 0;
	m_flKeyTime = 0.0f;
	m_iFlags = HUDELEM_ACTIVE;
	Reset();
	gHUD.AddHudElem( this );
	gEngfuncs.pfnAddCommand( "sgtc_timer_dump", SGTC_TimerDump_f );
	// ref FUN_10072e5f — SGTC view toggles (default 0).
	gEngfuncs.pfnRegisterVariable( "view_lowvis", "0", FCVAR_ARCHIVE );
	gEngfuncs.pfnRegisterVariable( "view_radar", "0", FCVAR_ARCHIVE );
	gEngfuncs.pfnRegisterVariable( "view_portraits", "0", FCVAR_ARCHIVE );
	gEngfuncs.pfnAddCommand( "votecastmenu", SGTC_VoteCastMenu_f );
	SGTC_InitPortraitSlots( this );
	SGTC_InitProgressSlotX( this );
	SGTC_InitAnnounceLines( this );
	// ref FUN_10072e96 — reset vote globals unless mid-countdown at 5.
	if( g_iSgtcVoteCount != 5 )
		g_iSgtcVoteCount = -1;
	g_flSgtcVoteAnim = 0;
	g_iSgtcVoteSel = -1;
	return 1;
}

// Weapon icon cache for SGTC_SetWeaponIcon. The key is a pointer into the
// static gWR, which is the same on every level, while the HSPRITE it resolved
// goes stale on a reload, so VidInit drops the cache.
static WEAPON *s_pIconWeapon = NULL;

static WEAPON *SGTC_IconWeapon( void )
{
	return s_pIconWeapon;
}
static HSPRITE s_hIcon = 0;
static wrect_t s_rcIcon = { 0, 0, 0, 0 };

int CHudSgtc::VidInit( void )
{
	s_pIconWeapon = NULL;
	s_hIcon = 0;
	memset( &s_rcIcon, 0, sizeof( s_rcIcon ) );
	m_iWeaponIconSpr = 0;
	memset( &m_rcWeaponIcon, 0, sizeof( m_rcWeaponIcon ) );
	return 1;
}

// ref client.dll `this` object (FUN_10050582) = the ref CHudAmmo; its +0x1c is the
// cached current-weapon WEAPON*, which is CHudAmmo::m_pWeapon here. It is private,
// so Think reads it through CHudAmmo::GetCurrentWeapon().
static WEAPON *SGTC_CurrentWeapon( void )
{
	return gHUD.m_Ammo.GetCurrentWeapon();
}

// shipped client.dll FUN_1004c8fb — per-weapon sprite loader called from the
// WeapPickup copy (FUN_1004f2a0, after memcpy of the 0x15c weapon block) and
// the slot-sweep WeaponsInit (FUN_1004c7f0). It resolves "sgtcweapon" from
// sprites/<szName>.txt once per weapon into weapon+0x148 (HSPRITE) /
// +0x14c..0x158 (wrect); the ammo Draw (FUN_1004e355) SPR_Sets +0x148.
// The port resolves the same txt+key lazily on weapon switch instead — same
// file, same key, equivalent visible icon. VERIFIED 2026-10-21 (was [INFERENCE]).
static void SGTC_SetWeaponIcon( CHudSgtc *hud, WEAPON *pw )
{
	if( pw != s_pIconWeapon )
	{
		s_pIconWeapon = pw;
		s_hIcon = 0;
		memset( &s_rcIcon, 0, sizeof( s_rcIcon ) );

		if( pw )
		{
			char sz[256];
			sprintf( sz, "sprites/%s.txt", pw->szName );
			int iCount = 0;
			client_sprite_t *pList = SPR_GetList( sz, &iCount );
			if( pList )
			{
				client_sprite_t *p = GetSpriteList( pList, "sgtcweapon",
					GetSpriteRes( ScreenWidth, ScreenHeight ), iCount );
				if( p )
				{
					sprintf( sz, "sprites/%s.spr", p->szSprite );
					s_hIcon = SPR_Load( sz );
					s_rcIcon = p->rc;
				}
			}
		}
	}

	hud->m_iWeaponIconSpr = s_hIcon;
	hud->m_rcWeaponIcon = s_rcIcon;
}

// shipped client.dll FUN_1004e355 — CHudAmmo::Draw feeder: gates on
// DAT_100e52c4 (menu/intermission flag), weapon-bits bit 31 (DAT_100e7710 &
// 0x80000000) and hideHUD bits (DAT_100e53f4 & 5), then feeds the ticket-dock
// ammo counters (reserve via FUN_1004c844 on DAT_100e13d0, clip from
// weapon+0xa0 = WEAPON::iClip) and the weapon icon (weapon+0x148 sgtcweapon
// HSPRITE -> FUN_10073b9d SPR_Set-equivalent). Crosshair-bloom tail
// (FUN_1004f1c0/DAT_100e7754 + this+0x14 fade decay) belongs to the stock
// crosshair path and is intentionally not ported here.
void CHudSgtc::Think( void )
{
	if( gHUD.m_fPlayerDead )					// ref DAT_100e9bc8
		return;
	if( !( gHUD.m_iWeaponBits & ( 1 << WEAPON_SUIT ) ) )	// ref DAT_100ed6d0 & 0x80000000
		return;
	// SGTC sends m_iHideHUD=9 (WEAPONS|HEALTH) on spawn, but the original still
	// paints its custom icon panel with that value. Gate only on the user's
	// full-hide flag; the stock client display is not the ref's hide global.
	if( gHUD.m_iHideHUDDisplay & HIDEHUD_ALL )		// ref DAT_100e9d0c & 4
		return;

	if( !( m_iFlags & HUDELEM_ACTIVE ) )			// ref this->+0x10 & 1
		return;
	WEAPON *pw = SGTC_CurrentWeapon();			// ref this->+0x1c (m_pWeapon)
	if( !pw )
		return;

	if( pw->iAmmoType > 0 )					// ref player->+0x80
	{
		m_iDockAmmoT = gWR.CountAmmo( pw->iAmmoType );	// ref +0x1704 via FUN_1004ea94
		m_iDockAmmoG = pw->iClip;			// ref +0x1708 (player->+0xa0)
	}
	else
	{
		m_iDockAmmoT = 0;
		m_iDockAmmoG = 0;
	}

	SGTC_SetWeaponIcon( this, pw );				// ref FUN_1007671c(player->+0x150)
}

static cvar_t *SGTC_Cvar( const char *name )
{
	return gEngfuncs.pfnGetCvarPointer( name );
}

static int SGTC_Sprite( const char *name, wrect_t *prc )
{
	int idx = gHUD.GetSpriteIndex( name );
	if( idx <= 0 )
		return 0;
	if( prc )
		*prc = gHUD.GetSpriteRect( idx );
	return idx;
}

static void SGTC_DrawSprite( const char *name, int x, int y )
{
	wrect_t rc;
	if( SGTC_Sprite( name, &rc ) )
		SPR_DrawAdditive( 0, x, y, &rc );
}

// ref CHudSgtc::Draw @0x100738a6 — tac slide FSM (+0x1690): visup/visdown @0x100bbaac/ac0,
// tri-strip DAT_100f70cc (sprite path on Xash); phase +10s @0x10095a50, close +0.001s @0x10094178.
static const float kArmorTacPhaseHold = 10.0f;	// rodata @0x10095a50
static const float kArmorTacPhaseClose = 0.001f;	// rodata @0x10094178
static const float kArmorTacSlideSec = 0.35f;	// approx. vis slide (ref tri-strip +0xb48)

static void SGTC_CloseArmorTacSlide( CHudSgtc *hud, float flTime )
{
	gEngfuncs.pfnPlaySoundByName( "player/visdown.wav", 1.0f );
	hud->m_flArmorTacPhase = flTime + kArmorTacPhaseClose;
	hud->m_iArmorState = 0;
}

static void SGTC_RunArmorTacSlideFsm( CHudSgtc *hud, float flTime )
{
	if( hud->m_iArmorState == 1 )
	{
		gEngfuncs.pfnPlaySoundByName( "player/visup.wav", 1.0f );
		hud->m_iArmorState = 2;
		hud->m_flArmorTacStart = flTime;
		hud->m_flArmorTacPhase = flTime + kArmorTacPhaseHold;
		return;
	}
	if( hud->m_iArmorState == 2 )
	{
		hud->m_flArmorTacPhase = flTime + kArmorTacPhaseHold;
		if( gHUD.m_iHideHUDDisplay & HIDEHUD_ALL )
			SGTC_CloseArmorTacSlide( hud, flTime );
		return;
	}
	if( hud->m_iArmorState == 3 )
		SGTC_CloseArmorTacSlide( hud, flTime );
}

static void SGTC_DrawArmorTacOverlay( CHudSgtc *hud, float flTime, int lx, int ly )
{
	if( !hud->m_iArmorOverlay && !hud->m_iTacDispActive && hud->m_iArmorState == 0 )
		return;

	const int tx = lx + 36;
	int ty = ly - 44;
	if( hud->m_iArmorState == 2 )
	{
		float slide = ( flTime - hud->m_flArmorTacStart ) / kArmorTacSlideSec;
		if( slide < 0.0f )
			slide = 0.0f;
		if( slide > 1.0f )
			slide = 1.0f;
		ty -= (int)( ( 1.0f - slide ) * 48.0f );
	}

	wrect_t base;
	if( SGTC_Sprite( "tacdisp", &base ) )
		SPR_DrawAdditive( 0, tx, ty, &base );

	float armorPts = hud->m_flArmorBar;
	if( hud->m_iArmorParam > 0 )
		armorPts = (float)hud->m_iArmorParam;
	if( armorPts <= 2000.0f )
	{
		if( armorPts <= 100.0f )
			armorPts *= 100.0f;
	}

	const char *layer = "tac_green";
	if( armorPts > 5000.0f )
		layer = "tac_red";
	else if( armorPts > 2000.0f )
		layer = "tac_yellow";

	int r = 0, g = 0, b = 255;
	if( hud->m_iArmorTeamTint )
	{
		g = 255;
		b = 0;
	}

	wrect_t tint;
	if( !SGTC_Sprite( layer, &tint ) )
		return;

	SPR_Set( gHUD.GetSprite( gHUD.GetSpriteIndex( layer ) ), r, g, b );
	SPR_DrawAdditive( 0, tx, ty, &tint );
}

// ref @0x100740df — compass needle frame: trunc(view yaw deg) → 16 sectors of
// ~22.5° (10°-349°, else 0), rotated +3 mod 16 then mirrored (0xf - n).
static int SGTC_RadarRingFrame( int v )
{
	int frame = 0;
	if( v < 0x0b || v > 0x15d )
		goto done;
	if( v <= 0x0a )
		frame = 0;
	else if( v < 0x21 )
		frame = 1;
	else if( v <= 0x20 )
		frame = 0;
	else if( v < 0x38 )
		frame = 2;
	else if( v <= 0x36 )
		frame = 0;
	else if( v < 0x4e )
		frame = 3;
	else if( v <= 0x4d )
		frame = 0;
	else if( v < 0x65 )
		frame = 4;
	else if( v <= 0x64 )
		frame = 0;
	else if( v < 0x7b )
		frame = 5;
	else if( v <= 0x7a )
		frame = 0;
	else if( v < 0x92 )
		frame = 6;
	else if( v <= 0x91 )
		frame = 0;
	else if( v < 0xa8 )
		frame = 7;
	else if( v <= 0xa7 )
		frame = 0;
	else if( v < 0xbf )
		frame = 8;
	else if( v <= 0xbe )
		frame = 0;
	else if( v < 0xca )
		frame = 9;
	else if( v <= 0xc9 )
		frame = 0;
	else if( v < 0xec )
		frame = 10;
	else if( v <= 0xeb )
		frame = 0;
	else if( v < 0x102 )
		frame = 11;
	else if( v <= 0x101 )
		frame = 0;
	else if( v <= 0x118 )
		frame = 12;
	else if( v < 0x11a )
		frame = 0;	// ref client_Draw_FUN100718df.c:172-186 — 0x119 unbinned → frame 0
	else if( v <= 0x12e )
		frame = 13;
	else if( v < 0x146 )
		frame = 14;
	else if( v <= 0x145 )
		frame = 0;
	else if( v < 0x15e )
		frame = 15;
done:
	frame += 3;
	if( frame > 0x0f )
		frame -= 0x10;
	return 0x0f - frame;
}

static void SGTC_RadarLayout( int &slide )
{
	slide = 0x68; // ref ebp-0x18 default @0x10073e40
}

// ref @0x10073eec / @0x10073f07 — 200×200 radar, typed blip frames, ring @0x10074323.
static void SGTC_DrawRadarFull( CHudSgtc *hud, float flTime )
{
	int slide = 0;
	SGTC_RadarLayout( slide );

	const int radarX = ScreenWidth - 0xd0;
	// ref @0x10073ed6: Y is 0x70 minus the slide local, not ScreenHeight-0xc8.
	const int radarY = 0x70 - slide;

	int radarIdx = gHUD.GetSpriteIndex( "radar" );
	if( !radarIdx )
		return;

	wrect_t rcRadar = gHUD.GetSpriteRect( radarIdx );
	rcRadar.right = rcRadar.left + 0xc8;
	rcRadar.bottom = rcRadar.top + 0xc8;
	// ref @0x10073e60-0x10073ea0 — radar tint by active team (+0x170c8): team 2 → (0,0x78,0)
	// green; any other team → (0x78,0,0) red. The team is an SgtcEntTeam, so
	// green = Torri.
	if( hud->m_iPortraitActiveTeam == SGTC_ENTTEAM_TORRI )
		SPR_Set( gHUD.GetSprite( radarIdx ), 0, 0x78, 0 );
	else
		SPR_Set( gHUD.GetSprite( radarIdx ), 0x78, 0, 0 );
	SPR_DrawAdditive( 0, radarX, radarY, &rcRadar );

	const int blIdx = gHUD.GetSpriteIndex( "blip" );
	if( !blIdx )
		return;
	wrect_t blFull = gHUD.GetSpriteRect( blIdx );

	for( int slot = 1; slot < SGTC_RADAR_SLOTS; slot++ )
	{
		const int typ = hud->m_iRadarType[slot];
		if( typ <= 0 )
			continue;

		int alpha = hud->m_iRadarAlpha[slot];
		if( alpha <= 0 )
		{
			hud->m_iRadarType[slot] = 0;
			continue;
		}

		const int tint = (int)( alpha * 0.7f ); // rodata @0x10094c10
		int r = tint, g = tint, b = tint;
		if( hud->m_iPortraitActiveTeam == SGTC_ENTTEAM_TORRI )
			b = 0;

		const int bx = ScreenWidth - hud->m_iRadarX[slot] - 0x76;
		const int by = hud->m_iRadarY[slot] + 0xca - slide;
		const int frame = typ - 1;
		if( frame < 0 )
			continue;

		SPR_Set( gHUD.GetSprite( blIdx ), r, g, b );
		SPR_DrawAdditive( frame, bx, by, &blFull );

		alpha -= 5;
		hud->m_iRadarAlpha[slot] = alpha;
		if( alpha < 1 )
			hud->m_iRadarType[slot] = 0;
	}

	// ref @0x100740ce-0x10074347 — compass needle: view yaw (ds:0x100fcfa4, the
	// engine-cached view angles vector .y) truncated to int → 16-sector frame via
	// SGTC_RadarRingFrame, sprite +0x173c "needle" tinted (0xd2,0xd2,0) @0x100740af,
	// drawn with needle's own rect +0x1740 at (ScreenWidth-0x7c, 0xc4-slide).
	const int needleIdx = gHUD.GetSpriteIndex( "needle" );
	if( needleIdx )
	{
		float vAngles[3];
		gEngfuncs.GetViewAngles( vAngles );
		const int needleFrame = SGTC_RadarRingFrame( (int)vAngles[1] ); // ref: ftol(yaw) @0x100740d4
		SPR_Set( gHUD.GetSprite( needleIdx ), 0xd2, 0xd2, 0 );          // ref pushes b=0,g=0xd2,r=0xd2
		wrect_t rcNeedle = gHUD.GetSpriteRect( needleIdx );             // ref +0x1740 = needle rect
		SPR_DrawAdditive( needleFrame, ScreenWidth - 0x7c, 0xc4 - slide, &rcNeedle ); // ref @0x10074323
	}

	if( hud->m_iRadarFocusX || hud->m_iRadarFocusY )
	{
		const int fx = ScreenWidth - hud->m_iRadarFocusX - 0x76;
		const int fy = hud->m_iRadarFocusY + 0xca - slide;
		SPR_Set( gHUD.GetSprite( blIdx ), 255, 255, 255 );
		SPR_DrawAdditive( 0, fx, fy, &blFull );
	}
}

// Xash substitute: ref CHudSgtc::Draw never SPR_Draws life_bar/armor_bar/stamn_bar
// (sprite bind only in VidInit @0x1007310e; values go to Win32 VGUI panels).
static void SGTC_DrawBarNeedle( const char *bar, const char *needle, int x, int y, float frac )
{
	wrect_t rcBar, rcNeedle;
	if( !SGTC_Sprite( bar, &rcBar ) )
		return;
	SPR_DrawAdditive( 0, x, y, &rcBar );
	if( frac < 0.0f )
		frac = 0.0f;
	if( frac > 1.0f )
		frac = 1.0f;
	if( SGTC_Sprite( needle, &rcNeedle ) )
	{
		int ny = y + (int)( ( rcBar.bottom - rcBar.top - rcNeedle.bottom ) * ( 1.0f - frac ) );
		SPR_DrawAdditive( 0, x + 4, ny, &rcNeedle );
	}
}

// ref FUN_1007257b — drop slots whose team != m_iPortraitActiveTeam (DAT_0x100f70c8).
static void SGTC_ExpirePortraitSlots( CHudSgtc *hud )
{
	for( int i = 0; i < SGTC_PORTRAIT_SLOTS; i++ )
	{
		if( hud->m_iPortraitSlotTeam[i] == hud->m_iPortraitActiveTeam )
			continue;

		hud->m_iPortraitSlotId[i] = 0;
		hud->m_iPortraitSlotEnt[i] = 0;
		hud->m_iPortraitSlotFlash[i] = 0;
		hud->m_iPortraitSlotGray[i] = 0x7d;
		hud->m_iPortraitSlotTeam[i] = 0;
	}
}

// ref FUN_10072609 — Reset-msg mode!=0: evict the portrait slot whose SlotEnt(+0x264)
// == arg while SlotId(+0x64) is set.  Clears SlotId/SlotEnt/SlotFlash(+0x1568) and
// resets SlotGray(+0x15ec) to 0x7d — name/team retained (slot grays out).
static void SGTC_EvictPortraitSlot( CHudSgtc *hud, int ent )
{
	for( int i = 0; i < SGTC_PORTRAIT_SLOTS; i++ )
	{
		if( hud->m_iPortraitSlotEnt[i] == ent && hud->m_iPortraitSlotId[i] != 0 )
		{
			hud->m_iPortraitSlotId[i] = 0;
			hud->m_iPortraitSlotEnt[i] = 0;
			hud->m_iPortraitSlotFlash[i] = 0;
			hud->m_iPortraitSlotGray[i] = 0x7d;
		}
	}
}

// ref FUN_10072502 — clear 33 portrait slot records @ Init.
static void SGTC_InitPortraitSlots( CHudSgtc *hud )
{
	for( int i = 0; i < SGTC_PORTRAIT_SLOTS; i++ )
	{
		hud->m_iPortraitSlotId[i] = 0;
		hud->m_flPortraitSlotHpPct[i] = 0;
		hud->m_iPortraitSlotTeam[i] = 0;
		hud->m_iPortraitSlotAux[i] = 0;
		hud->m_iPortraitSlotEnt[i] = 0;
		hud->m_iPortraitSlotHp[i][0] = 0;
		hud->m_iPortraitSlotHp[i][1] = 0;
		hud->m_iPortraitSlotHp[i][2] = 0;
		hud->m_szPortraitSlotName[i][0] = 0;
		hud->m_iPortraitSlotColor[i] = 0;
		hud->m_iPortraitSlotFlash[i] = 0;
		hud->m_iPortraitSlotGray[i] = 0x7d;
	}
}

// ref FUN_10072694 — upsert portrait column cell for active team.
static void SGTC_UpdatePortraitSlot( CHudSgtc *hud, int spr, float hpPct, int team, int aux, int ent,
	int hp0, int hp1, int hp2, const char *name, int colorId )
{
	if( !spr || !ent || !name || !name[0] )
		return;

	int hit = -1;
	for( int i = 0; i < SGTC_PORTRAIT_SLOTS; i++ )
	{
		if( hud->m_iPortraitSlotEnt[i] == ent && hud->m_iPortraitSlotId[i] != 0 )
		{
			hit = i;
			break;
		}
	}

	if( hit < 0 && team == hud->m_iPortraitActiveTeam )
	{
		for( int i = 0; i < SGTC_PORTRAIT_SLOTS; i++ )
		{
			if( hud->m_iPortraitSlotId[i] == 0 )
			{
				hit = i;
				break;
			}
		}
	}

	if( hit < 0 )
		return;

	int oldTeam = hud->m_iPortraitSlotTeam[hit];
	hud->m_iPortraitSlotId[hit] = spr;
	hud->m_flPortraitSlotHpPct[hit] = hpPct;
	hud->m_iPortraitSlotTeam[hit] = team;
	hud->m_iPortraitSlotAux[hit] = aux;
	hud->m_iPortraitSlotEnt[hit] = ent;
	hud->m_iPortraitSlotHp[hit][0] = hp0;
	hud->m_iPortraitSlotHp[hit][1] = hp1;
	hud->m_iPortraitSlotHp[hit][2] = hp2;
	strncpy( hud->m_szPortraitSlotName[hit], name, sizeof( hud->m_szPortraitSlotName[hit] ) - 1 );
	hud->m_iPortraitSlotColor[hit] = colorId;
	if( oldTeam != team )
	{
		for( int i = 0; i < SGTC_PORTRAIT_SLOTS; i++ )
		{
			if( hud->m_iPortraitSlotEnt[i] != 0 && hud->m_iPortraitSlotEnt[i] != hud->m_iPortraitEnt )
			{
				hud->m_iPortraitSlotId[i] = 0;
				hud->m_flPortraitSlotHpPct[i] = 0;
				hud->m_iPortraitSlotTeam[i] = 0;
				hud->m_iPortraitSlotAux[i] = 0;
				hud->m_iPortraitSlotEnt[i] = 0;
				hud->m_iPortraitSlotFlash[i] = 0;
				hud->m_iPortraitSlotGray[i] = 0x7d;
				hud->m_szPortraitSlotName[i][0] = 0;
			}
		}
	}
}

// ref client_PortraitAvatarName_FUN10070a66.c — class ids 1..10 (gate 0<id<0xb); default inconnu.
static const char *SGTC_PortraitAvatarSprite( int id )
{
	static const char *const kTable[] =
	{
		"oneill", "carter", "jackson", "tealc", "commander",
		"systemlord", "anubis", "goauld3", "snakegoauld", "horus",
	};
	if( id < 1 || id > 10 )
		return "inconnu";
	return kTable[id - 1];
}

// ref FUN_100772f2 — packed color id decode @0x100776c8..0x1007777e (threshold chain).
struct SGTC_PortraitColorFlags
{
	char c0, b8, bc, c4, c8, cc;
};

static void SGTC_DecodePortraitColorId( int colorId, SGTC_PortraitColorFlags *f )
{
	int ac = colorId;
	memset( f, 0, sizeof( *f ) );
	if( ac >= 0xc0 ) { f->cc = 2; ac -= 0xc0; }
	if( ac >= 0x60 ) { f->cc = 1; ac -= 0x60; }
	if( ac >= 0x30 ) { f->c8 = 1; ac -= 0x30; }
	if( ac >= 0x18 ) { f->c4 = 2; ac -= 0x18; }
	if( ac >= 0xc ) { f->c4 = 1; ac -= 0xc; }
	if( ac >= 0x6 ) { f->c0 = 1; ac -= 0x6; }
	if( ac >= 0x3 ) { f->bc = 1; ac -= 0x3; }
	if( ac >= 0x2 ) { f->b8 = 1; ac -= 0x2; }
}

// ref jump table @0x10077bd3 (index colorId & 0x1f, max 0x12).
static void SGTC_PortraitTextRgb( int colorId, int &r, int &g, int &b )
{
	static const unsigned char kTbl[19][3] =
	{
		{ 0xa8, 0xd3, 0xff }, { 0xff, 0x00, 0x00 }, { 0xff, 0x82, 0x00 },
		{ 0xff, 0xff, 0x00 }, { 0x82, 0xff, 0x00 }, { 0x00, 0xff, 0x00 },
		{ 0x00, 0xff, 0x82 }, { 0x00, 0xff, 0xff }, { 0x00, 0x82, 0xff },
		{ 0x00, 0x00, 0xff }, { 0x82, 0x00, 0xff }, { 0xff, 0x00, 0xff },
		{ 0xff, 0x00, 0x82 }, { 0x78, 0xaf, 0x50 }, { 0xa0, 0x78, 0x82 },
		{ 0xff, 0xff, 0xff }, { 0xff, 0x80, 0xff }, { 0x80, 0x80, 0x40 },
		{ 0x80, 0x80, 0x80 },
	};
	int idx = colorId & 0x1f;
	if( idx > 18 )
		idx = 0;
	r = kTbl[idx][0];
	g = kTbl[idx][1];
	b = kTbl[idx][2];
	if( r + g + b == 0 )	// ref @0x10077604
	{
		r = 0xa8;
		g = 0xd3;
		b = 0xff;
	}
}

// ref FUN_100772f2 — name @+0x48, py-0x10; avatar tint + flash @0x10077792; HP @0x10077a99.
static void SGTC_DrawPortraitCell( CHudSgtc *hud, int slot, int px, int py, float flTime, char dimName )
{
	const int classId = hud->m_iPortraitSlotColor[slot];
	const int ent = hud->m_iPortraitSlotEnt[slot];
	const char *label = hud->m_szPortraitSlotName[slot];
	const int colorId = classId;
	// ref Draw @0x100718df: the own cell is column-excluded (DAT_100f134c[i] !=
	// DAT_100f21cc) and rendered by the VGUI cell manager (FUN_100736d0), which
	// draws the face only — no name line, no HP bar. Match: self -> avatar only.
	const int bSelf = ( ent != 0 && ent == hud->m_iPortraitEnt );
	if( classId < 1 || !ent || !label || !label[0] )
	{
		const char *err = CHudTextMessage::BufferedLocaliseTextString( "! ERROR !" );
		gHUD.DrawHudString( px, py - 0x10, ScreenWidth, err, 0, 0, 255 );
		return;
	}

	int r, g, b;
	SGTC_PortraitTextRgb( colorId, r, g, b );
	if( dimName )
		r = g = b = 0;

	if( !bSelf )
	{
		const char *text = CHudTextMessage::BufferedLocaliseTextString( label );
		gHUD.DrawHudString( px, py - 0x10, ScreenWidth, text, r, g, b );
	}

	SGTC_PortraitColorFlags flags;
	SGTC_DecodePortraitColorId( colorId, &flags );

	int sr = r, sg = g, sb = b;
	if( flags.c4 == 2 )
	{
		if( hud->m_iPortraitSlotFlash[slot] > 5 )
		{
			sr = 0xf3;
			sg = 0xb7;
			sb = 0;
		}
		else
		{
			sr = 0xff;
			sg = 0xd5;
			sb = 0x53;
		}
		hud->m_iPortraitSlotFlash[slot]++;
		if( hud->m_iPortraitSlotFlash[slot] > 10 )
			hud->m_iPortraitSlotFlash[slot] = 0;
	}
	else if( flags.c4 == 1 )
	{
		int gray = hud->m_iPortraitSlotGray[slot];
		if( gray > 0x32 )
		{
			gray -= 2;
			hud->m_iPortraitSlotGray[slot] = gray;
		}
		sr = sg = sb = gray;
	}
	else if( hud->m_iPortraitSlotGray[slot] < 0x7d )
	{
		int gray = hud->m_iPortraitSlotGray[slot] + 2;
		hud->m_iPortraitSlotGray[slot] = gray;
		sr = sg = sb = gray;
	}

	if( flags.c0 )
	{
		sr = 0x78;
		sg = 0x78;
		sb = 0x32;
	}

	const float hpPct = hud->m_flPortraitSlotHpPct[slot];
	if( hpPct < 1.0f )
		sg = sb = 0;	// ref client_PortraitDraw_FUN10074585.c:231-234

	const char *avatar = SGTC_PortraitAvatarSprite( classId );
	if( SGTC_Sprite( avatar, NULL ) )
	{
		wrect_t av;
		SGTC_Sprite( avatar, &av );
		av.right = av.left + 0x40;
		av.bottom = av.top + 0x40;
		SPR_Set( gHUD.GetSprite( gHUD.GetSpriteIndex( avatar ) ), sr, sg, sb );
		SPR_DrawAdditive( 0, px, py, &av );
	}

	if( hpPct > 0.0f )
	{
		if( flags.b8 )
		{
			const int zIdx = gHUD.GetSpriteIndex( "zat_touch" );
			if( zIdx > 0 )
			{
				wrect_t zr = gHUD.GetSpriteRect( zIdx );
				zr.right = zr.left + 0x40;
				zr.bottom = zr.top + 0x40;
				SPR_Set( gHUD.GetSprite( zIdx ), 0, 0x82, 0x100 );
				SPR_DrawAdditive( 0, px, py, &zr );
			}
		}
		else if( flags.bc )
		{
			const int dIdx = gHUD.GetSpriteIndex( "drugs" );
			if( dIdx > 0 )
			{
				wrect_t dr = gHUD.GetSpriteRect( dIdx );
				dr.right = dr.left + 0x40;
				dr.bottom = dr.top + 0x40;
				SPR_Set( gHUD.GetSprite( dIdx ), 0xbe, 0, 0xbe );
				SPR_DrawAdditive( 0, px, py, &dr );
			}
		}
	}

	float frac = hpPct / 100.0f;
	if( frac < 0.0f )
		frac = 0.0f;
	if( frac > 1.0f )
		frac = 1.0f;
	if( frac <= 0.0f || dimName || bSelf )
		return;

	const int barW = 0x40;
	const int barX = px + 0x42;
	const int barY = py + 0x44;	// ref client_PortraitDraw_FUN10074585.c:271-278 — row at y+0x44
	// ref @0x10077b6b-0x10077bca — HP fill is a red→green gradient, not solid:
	// FillRGBA(x, y+0x44, ftol(frac*0x40), 4, ftol(220-220*frac), ftol(220*frac), 0, 0xff)
	// via ds:0x100ed73c (220.0f @ds:0x100a1e70, 64.0f @ds:0x1009f014).
	FillRGBA( barX, barY, barW, 4, 0x1e, 0x1e, 0x1e, 255 );
	FillRGBA( barX, barY, (int)( barW * frac ), 4, (int)( 220.0f - 220.0f * frac ), (int)( 220.0f * frac ), 0, 255 );

	if( flags.cc == 1 )
		FillRGBA( barX, py + 0x40, barW, 2, 255, 0, 0, 255 );
	else if( flags.cc == 2 )
		FillRGBA( barX, py + 0x40, barW, 2, 0, 0x82, 0, 255 );
}

// ref client_Draw_FUN100718df.c:263-362 — own record on ticketdock anchor (W-0x4e, H-0x8e).
static int s_iOwnPortraitFlash;
static int s_iOwnPortraitGray = 0x7d;

static void SGTC_DrawOwnPortraitRecord( CHudSgtc *hud )
{
	if( hud->m_iPortraitSprite == 0 || hud->m_iPortraitId < 1 )
		return;

	const int x = ScreenWidth - 0x4e;
	const int y = ScreenHeight - 0x8e;
	const float hpVal = hud->m_flPortraitTime;

	SGTC_PortraitColorFlags flags;
	SGTC_DecodePortraitColorId( hud->m_iPortraitHp[2], &flags );

	int sr = 0x7d, sg = 0x7d, sb = 0x7d;
	if( hpVal > 0.0f )
	{
		if( flags.c4 == 2 )
		{
			if( s_iOwnPortraitFlash < 6 )
			{
				sr = 0xff;
				sg = 0xd5;
				sb = 0x53;
			}
			else
			{
				sr = 0xf3;
				sg = 0xb7;
				sb = 0;
			}
			s_iOwnPortraitFlash++;
			if( s_iOwnPortraitFlash > 10 )
				s_iOwnPortraitFlash = 0;
		}
		if( flags.c4 == 1 )
		{
			sg = sb = sr = s_iOwnPortraitGray;
			if( s_iOwnPortraitGray > 0x32 )
				s_iOwnPortraitGray -= 2;
		}
		else if( s_iOwnPortraitGray < 0x7d )
		{
			s_iOwnPortraitGray += 2;
			sg = sb = sr = s_iOwnPortraitGray;
		}
		if( flags.c0 )
		{
			sr = 0x78;
			sg = 0x78;
			sb = 0x32;
		}
	}
	if( hpVal < 1.0f )
		sg = sb = 0;

	const char *avatar = SGTC_PortraitAvatarSprite( hud->m_iPortraitId );
	wrect_t av;
	if( !SGTC_Sprite( avatar, &av ) )
		return;
	av.right = av.left + 0x40;
	av.bottom = av.top + 0x40;
	SPR_Set( gHUD.GetSprite( gHUD.GetSpriteIndex( avatar ) ), sr, sg, sb );
	SPR_DrawAdditive( 0, x, y, &av );

	if( hpVal != 0.0f )
	{
		if( flags.b8 )
		{
			const int zIdx = gHUD.GetSpriteIndex( "zat_touch" );
			if( zIdx > 0 )
			{
				wrect_t zr = gHUD.GetSpriteRect( zIdx );
				zr.right = zr.left + 0x40;
				zr.bottom = zr.top + 0x40;
				SPR_Set( gHUD.GetSprite( zIdx ), 0, 0x82, 0x100 );
				SPR_DrawAdditive( 0, x, y, &zr );
			}
		}
		else if( flags.bc )
		{
			const int dIdx = gHUD.GetSpriteIndex( "drugs" );
			if( dIdx > 0 )
			{
				wrect_t dr = gHUD.GetSpriteRect( dIdx );
				dr.right = dr.left + 0x40;
				dr.bottom = dr.top + 0x40;
				SPR_Set( gHUD.GetSprite( dIdx ), 0xbe, 0, 0xbe );
				SPR_DrawAdditive( 0, x, y, &dr );
			}
		}
	}
}

void CHudSgtc::DrawRoundTimer( float flTime )
{
	// ref @0x10075DD2 — post-list repaint; mode 2 must paint here when
	// HUD_Redraw skips CHudSgtc::Draw (HIDEHUD_ALL). Slab skip stays in Draw().
	SGTC_DrawRoundTimer( this, flTime );
}

int CHudSgtc::Draw( float flTime )
{
	// Build-19: keep the SDL pointer released while an SGTC menu is open. A
	// window focus change re-grabs it on macOS; this re-asserts every frame.
	SgtcPanels_CursorTick();

	const int panelH = 168;
	const int lx = 0;
	const int ly = ScreenHeight - panelH;

	// ref @0x100738a6 — tac FSM runs before DAT_100e9d0c body skip @0x10073ac1.
	SGTC_RunArmorTacSlideFsm( this, flTime );

	// ref HideWeapon fast path @0x10071967 → 0x10073256 — spectator SetRndTim mode 2
	// skips slab/radar/portraits; timer paints post-list (DrawRoundTimer).
	if( m_iRoundTimerMode == 2 )
	{
		SGTC_DrawRoundTimer( this, flTime );
		return 0;
	}

	if( m_iHideHud || ( gHUD.m_iHideHUDDisplay & HIDEHUD_ALL ) )
		return 0;

	// ref: hud_left (+0x1c90) IS drawn once by ref Draw — the entrance-banner ammo
	// underlay @0x100749a3 (ported in SGTC_DrawEntranceBanners).  The full 200x168
	// bottom-left panel below is a port keep (visual anchor; no matching ref draw).
	SGTC_DrawSprite( "hud_left", lx, ly );
	// ref client_Draw_FUN100718df.c:1647-1653 — hud_right full plate (no left/top crop).
	{
		wrect_t rcPanel;
		if( SGTC_Sprite( "hud_right", &rcPanel ) )
		{
			SPR_Set( gHUD.GetSprite( gHUD.GetSpriteIndex( "hud_right" ) ), 0x7d, 0x7d, 0x7d );
			SPR_DrawHoles( 0, ScreenWidth - 0xc6, ScreenHeight - 0xa1, &rcPanel );
		}
	}

	SGTC_DrawOwnPortraitRecord( this );


	// (Build 10) The duplicate, ungated bombkey draw that used to sit here at
	// lx+120/ly+100 — right behind the health/armor wells — is removed: a stale
	// m_iHasKey painted a key stuck under the HUD. The only key indicator is the
	// banner path in SGTC_DrawEntranceBanners, which gates on
	// m_iKeyMode > 0 && m_iHasKey && m_iKeyBannerSlide == 0.

	// ref draws the radar unconditionally (view_radar is a no-op toggle in ref client.dll);
	// the shared config sets view_radar "0" yet the orig renders the radar disc.
	SGTC_DrawRadarFull( this, flTime );

	SGTC_DrawAnnounceScroller( this, flTime );
	SGTC_TickTicketDock( this, flTime );
	SGTC_DrawTicketDock( this, flTime );
	SGTC_TickEntranceSlides( this );
	SGTC_DrawEntranceBanners( this );
	SGTC_TickProgressTeamUi( this );
	SGTC_DrawProgressTeamUi( this );

	// Well fills are drawn last: earlier versions drew them before the radar /
	// announce scroller / ticket dock, whose sprites overdraw the hud_left wells
	// and left them looking empty in playtests.
	SGTC_DrawArmorTacOverlay( this, flTime, lx, ly );
	{
		// Build-19 screenshot/test overrides, only when the QA gate is on:
		//   sgtc_hud_life / sgtc_hud_armor / sgtc_hud_stam  (0-100, -1 = off)
		// Lets the harness (or a Mac tester) show all three bars without needing
		// a live spawn with real stamina drain.
		float lifeOverride = -1.0f, armorOverride = -1.0f, stamOverride = -1.0f;
		if( gEngfuncs.pfnGetCvarFloat( "sv_cheats" ) >= 1.0f &&
			gEngfuncs.pfnGetCvarFloat( "sgtc_test_enable" ) > 0.0f )
		{
			lifeOverride = gEngfuncs.pfnGetCvarFloat( "sgtc_hud_life" );
			armorOverride = gEngfuncs.pfnGetCvarFloat( "sgtc_hud_armor" );
			stamOverride = gEngfuncs.pfnGetCvarFloat( "sgtc_hud_stam" );
		}

		float lifeFrac = (float)gHUD.m_Health.m_iHealth / 100.0f;
		if( lifeOverride >= 0.0f ) lifeFrac = lifeOverride / 100.0f;
		if( lifeFrac > 1.0f ) lifeFrac = 1.0f;
		SGTC_DrawWellFill( "life_bar", 4, lifeFrac,
			(int)( 220.0f * ( 1.0f - lifeFrac ) ), (int)( 220.0f * lifeFrac ), 0 );

		float armorFrac = m_flArmorBar / 100.0f;
		if( armorOverride >= 0.0f ) armorFrac = armorOverride / 100.0f;
		if( armorFrac > 1.0f ) armorFrac = 1.0f;
		SGTC_DrawWellFill( "armor_bar", 28, armorFrac, 255, 150, 40 );	// original armour orange

		float stamFrac = (float)s_iSgtcStaminaByte / 100.0f;
		if( stamOverride >= 0.0f ) stamFrac = stamOverride / 100.0f;
		if( stamFrac > 1.0f ) stamFrac = 1.0f;
		SGTC_DrawWellFill( "stamn_bar", 52, stamFrac, 255, 220, 60 );	// original stamina yellow
	}

	// ref draws portraits unconditionally (view_portraits is a no-op toggle in ref client.dll);
	// the shared config sets view_portraits "0" yet the orig renders the portrait cell.
	if( true )
	{
		const int portY = 0;	// ref client_Draw_FUN100718df.c:96-105 — column cells at y=0

		SGTC_ExpirePortraitSlots( this );

		// ref @0x10073c47 — the own cell is column-excluded unconditionally
		// (DAT_100f134c[i] != DAT_100f21cc): the local player never appears in
		// the top strip; the ref has no "view_seeownport" cvar, that gate was
		// a port invention that left a black own-cell at top-right.
		int col = 0;
		int lastPx = 0;
		for( int i = 0; i < SGTC_PORTRAIT_SLOTS; i++ )
		{
			if( !m_iPortraitSlotEnt[i] )
				continue;
			if( !m_szPortraitSlotName[i][0] )
				continue;
			if( m_iPortraitSlotColor[i] < 1 )
				continue;
			if( m_iPortraitSlotTeam[i] <= 0 )
				continue;
			if( m_iPortraitSlotTeam[i] != m_iPortraitActiveTeam )
				continue;
			if( m_iPortraitSlotEnt[i] == m_iPortraitEnt )
				continue;

			if( col >= 9 )	// ref client_DrawEntry_FUN100736d0.c:20 — slot count capped at 9
				break;
			col++;
			const int px = ScreenWidth - ( col * 0x50 + 0x10 );	// ref @0x10073c70
			if( px <= 0 )
				break;					// ref @0x10073c5f: skip columns off the left edge
			lastPx = px;

			// ref @0x10073c84-0x10073cbd — per-column backdrop: port_mid (+0x1cd8) at (px, 0),
			// constant tint 0x7d, SPR_DrawHoles.
			wrect_t gr;
			if( SGTC_Sprite( "port_mid", &gr ) )
			{
				SPR_Set( gHUD.GetSprite( gHUD.GetSpriteIndex( "port_mid" ) ), 0x7d, 0x7d, 0x7d );
				SPR_DrawHoles( 0, px, 0, &gr );
			}
			SGTC_DrawPortraitCell( this, i, px, portY, flTime, 0 );
		}

		// ref @0x10073d86-0x10073e21 — overflow caps, only when the strip ran off the left edge:
		// port_left (+0x1cc0) at (lastPx-0x10, 0) then port_right (+0x1cd0) at (W-0x10, 0),
		// tint 0x7d, SPR_DrawHoles.
		if( lastPx != 0 )
		{
			wrect_t rcCap;
			if( SGTC_Sprite( "port_left", &rcCap ) )
			{
				SPR_Set( gHUD.GetSprite( gHUD.GetSpriteIndex( "port_left" ) ), 0x7d, 0x7d, 0x7d );
				SPR_DrawHoles( 0, lastPx - 0x10, 0, &rcCap );
			}
			if( SGTC_Sprite( "port_right", &rcCap ) )
			{
				SPR_Set( gHUD.GetSprite( gHUD.GetSpriteIndex( "port_right" ) ), 0x7d, 0x7d, 0x7d );
				SPR_DrawHoles( 0, ScreenWidth - 0x10, 0, &rcCap );
			}
		}
	}

	SGTC_TickBlipPulse( this );
	SGTC_TickBlipBarProgress( this, flTime );

	// ref pbar block @0x10075156: pbare is the gray container, pbar is the top-drained fill.
	if( m_iBlipProgress > 0 && m_iBlipProgress < 0x65 )
	{
		const int bgIdx = gHUD.GetSpriteIndex( "pbare" );
		if( bgIdx > 0 )
		{
			// ref @0x1007517c: bgX = ScreenWidth/2 - 0x47, bgY = ScreenHeight - ScreenHeight/4 - 7.
			wrect_t bg = gHUD.GetSpriteRect( bgIdx );
			const int bgX = ScreenWidth / 2 - 0x47;
			const int bgY = ScreenHeight - ScreenHeight / 4 - 7;
			SPR_Set( gHUD.GetSprite( bgIdx ), 120, 120, 120 ); // ref @0x100751ef: push 0x78,0x78,0x78
			SPR_DrawHoles( 0, bgX, bgY, &bg ); // ref @0x1007520b: call ds:0x100ed728 (SPR_DrawHoles)
		}

		const int fIdx = gHUD.GetSpriteIndex( "pbar" );
		if( fIdx > 0 )
		{
			// ref @0x10075222: fillX = ScreenWidth/2 - 0x40, fillY = ScreenHeight - ScreenHeight/4.
			wrect_t fr = gHUD.GetSpriteRect( fIdx );
			const int fillX = ScreenWidth / 2 - 0x40;
			const int fillY = ScreenHeight - ScreenHeight / 4;

			int p = m_iBlipProgress;
			if( p < 0 )
				p = 0;
			const double rem = ( 100.0 - p ) / 100.0; // ref fsubr/fdiv ds:0x100a1d30 (100.0)

			// ref @0x10075296: rect.top -= (int)(rect.top * rem); left/right/bottom untouched.
			fr.top = fr.top - (int)( fr.top * rem );

			// ref @0x100752db: SPR_Set( h, (int)(180*rem), (int)(180-180*rem), 0 ).
			const int r = (int)( 180.0f * rem );               // 180.0f @ds:0x1009eff8
			const int g = (int)( 180.0f - 180.0f * rem );
			SPR_Set( gHUD.GetSprite( fIdx ), r, g, 0 );
			SPR_DrawHoles( 0, fillX, fillY, &fr ); // ref @0x10075315: call ds:0x100ed728 (SPR_DrawHoles)
		}
	}


	// ref @0x10075ff1-0x100761d7 — two-counter score row while TicketMode is active:
	// first number [+0x60] = Ttickets, second [+0x5c] = Gtickets (port MsgFunc_Ttickets/@FUN_10076d24
	// and MsgFunc_Gtickets/@FUN_10076cf7).  Layout flag byte[ebp-0x14] (cleared @0x10075dc8) always
	// takes the bottom-left arm (0x38, H-0x8e); the (W/2, H/9) arm is unreachable in ref.
	if( m_iTicketModeFlag )
	{
		const int w = SGTC_DigitWidth();
		int x0 = 0x38;				// ref @0x10076089
		const int y0 = ScreenHeight - 0x8e + gHUD.m_iHudNumbersYOffset;	// ref @0x10076098
		const int step = 0xd;			// ref @0x100760b8
		const int drawX0 = x0 - ( w * 9 ) / 4;

		if( m_iTtickets > 99 )			// ref @0x100760c8
			x0 -= step;
		gHUD.DrawHudNumber( drawX0, y0, DHN_DRAWZERO | DHN_2DIGITS, m_iTtickets, 0xa8, 0xd3, 0xff ); // ref flags 3
		if( m_iTtickets > 99 )			// ref @0x10076127
			x0 += step;
		if( m_iGtickets > 99 )			// ref @0x10076145
			x0 -= step;
		x0 += 0x2f;				// ref @0x10076163
		gHUD.DrawHudNumber( x0 - ( w * 9 ) / 4, y0, DHN_DRAWZERO | DHN_2DIGITS, m_iGtickets, 0xa8, 0xd3, 0xff ); // ref flags 3
	}

	if( m_fZatTouchActive && m_flZatTouchTime + 2.0f > flTime && gHUD.GetSpriteIndex( "zat_touch" ) )
	{
		wrect_t zt = gHUD.GetSpriteRect( gHUD.GetSpriteIndex( "zat_touch" ) );
		SPR_DrawAdditive( 0, ScreenWidth / 2 - zt.right / 2, ScreenHeight / 2 - zt.bottom / 2, &zt );
	}
	else if( m_fZatTouchActive && m_flZatTouchTime + 2.0f <= flTime )
		m_fZatTouchActive = 0;

	// ref @0x10076292 — vote prompts when portrait team filter active. Only ONE
	// line is drawn per frame: both conditions can be true at once and the two
	// prompts used to overprint each other at the same x/y ("Do not camp" on top
	// of the please-vote text, John build-11 playtest). Camp warning outranks the
	// vote prompt.
	if( m_iPortraitActiveTeam )
	{
		const int c = m_iBlipPulse + 100;
		// m_flKeyTime defaults to 0 and is only armed by a HasKey/keymode message
		// (mode 2 → now+5s); without the >0 guard flTime >= 0 was always true and
		// "Do not camp" showed on maps with no key objective at all (John's temple
		// run). Camp warning still outranks the please-vote prompt when armed.
		if( m_flKeyTime > 0.0f && flTime >= m_flKeyTime )
			SGTC_DrawVotePromptLine( "#CAMPWARN", 255, c, c );
		else if( g_iSgtcVoteCount < 0 && g_iSgtcVoteSel < 0 && g_iSgtcVoteCount < 5 )
			SGTC_DrawVotePromptLine( "#PLZVOTE", c, c, 255 );
	}

	SGTC_DrawVoteCastMenu( flTime );

	return 1;
}

void CHudSgtc::Reset( void )
{
	m_iGtickets = 0;
	m_iTtickets = 0;
	m_iHudInfoMode = 0;
	m_iStamina = 0;
	s_iSgtcStaminaByte = 0;
	m_iSubModel = 0;
	m_iHasKey = 0;
	m_iRadioTeam = 0;
	m_iRadioMsg = 0;
	m_iArmorTeamTint = 0;
	m_iTacDispActive = 0;
	m_flArmorBar = 0;
	m_iArmorAux = 0;
	m_iArmorOverlay = 0;
	m_iArmorState = 0;
	m_iArmorParam = 0;
	m_flArmorTacPhase = 0;
	m_flArmorTacStart = 0;
	m_iBlipPulse = 0;
	m_iBlipProgress = 0;
	m_flBlipProgressLatch = 0;
	m_cBlipPulseDir = 0;
	m_iRadarFocusX = 0;
	m_iRadarFocusY = 0;
	memset( m_iRadarType, 0, sizeof( m_iRadarType ) );
	memset( m_iRadarX, 0, sizeof( m_iRadarX ) );
	memset( m_iRadarY, 0, sizeof( m_iRadarY ) );
	memset( m_iRadarAlpha, 0, sizeof( m_iRadarAlpha ) );
	m_fZatTouchActive = 0;
	m_flZatTouchTime = 0;
	m_flKeyTime = 0;
	m_iHideHud = 0;
	m_iKeyMode = 0;
	m_iHasKey = 0;
	m_iKeyBannerSlide = 0;
	m_iKeyBannerStep = 0;
	m_flKeyTime = 0.0f;
	// ref Reset does NOT touch +0x16f0/+0x16e8/+0x16ec nor +0x58 (ticket dock):
	// NewRnd may sort after SetHudRoundTime/PlayerSpawn TicketMode in the same
	// frame batch — zeroing +0x58 here left ticket=0 on ctp_amon after
	// mp_resetround (P142). TicketMode / SetRndTim handlers own +0x58.
	// InitHUD disarm comes from the server's SetHudRoundTime(0,0).
	m_iTicketDockFrame = 0;
	m_iTicketSlideOffset = 0;
	m_iTicketSlideRemain = 0;
	m_bTicketDockLayout78 = 0;
	m_bTicketSlide256 = 0;
	m_bTicketSlide190 = 0;
	m_cTicketSlideDir = 0;
	m_iAmmoLineSlide = 0;
	m_iAmmoLineStep = 0;
	m_iKeyBannerSlide = 0;
	m_iKeyBannerStep = 0;
	m_iProgress = 0;
	m_iProgressIcon = 0;
	memset( m_iProgressTeam, 0, sizeof( m_iProgressTeam ) );
	m_iProgressEnd = 0;
	memset( m_iProgressTeamUiState, 0, sizeof( m_iProgressTeamUiState ) );
	m_iProgressIconCursor = 0;
	SGTC_InitProgressSlotX( this );
	SGTC_InitAnnounceLines( this );
	// ref reset @0x10070bbf→0x1007410c→0x10070700 clears ONLY the four 0x21-entry
	// blip arrays (DAT_100f1140/100f134c/100f21d4 → 0, DAT_100f11c4 → 0x7d). The
	// active-team global DAT_100f70c8 and the portrait records are never touched —
	// the ref's full Reset() runs once at HUD init, not per respawn.
	g_iSgtcAnnouncePending = 0;
}

// ref FUN_100772b4 — ZatTouch, REG size 0 on server.
int CHudSgtc::MsgFunc_ZatTouch( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	m_fZatTouchActive = 1;
	m_flZatTouchTime = gHUD.m_flTime;
	return 1;
}

// ref FUN_10076d24 — Ttickets → +0x60
int CHudSgtc::MsgFunc_Ttickets( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	m_iTtickets = READ_BYTE();
	m_iFlags |= HUD_ACTIVE;
	return 1;
}

// ref FUN_10076cf7 — Gtickets → +0x5c
int CHudSgtc::MsgFunc_Gtickets( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	m_iGtickets = READ_BYTE();
	m_iFlags |= HUD_ACTIVE;
	return 1;
}

// ref FUN_10076bfb — TicketMode → +0x58 flag
int CHudSgtc::MsgFunc_TicketMode( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	int b = READ_BYTE();
	m_iTicketModeFlag = ( b >= 1 ) ? 1 : 0;
	// P149f: do not arm ticket dock while spectator round timer (mode 2) is active.
	if( m_iRoundTimerMode == 2 )
		m_iTicketModeFlag = 0;
	if( m_iTicketModeFlag )
	{
		m_iTicketDockFrame = 0;
		m_iTicketSlideOffset = 0;
		m_iTicketSlideRemain = 0xc00;	// ref DAT_100f6fb0 initial @0x10074ad7
		m_bTicketDockLayout78 = 0;
	m_bTicketSlide256 = 0;
	m_bTicketSlide190 = 0;
	}
	m_iFlags |= HUD_ACTIVE;
	return 1;
}

// ref FUN_10076bce — HudInfoMode → +0x54
int CHudSgtc::MsgFunc_HudInfoMode( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	m_iHudInfoMode = READ_BYTE();
	m_iFlags |= HUD_ACTIVE;
	return 1;
}

// ref FUN_100592cf (Concuss, 2 bytes) — byte0 concussion when byte2==0; byte1 → DAT_100f1ce8.
int CHudSgtc::MsgFunc_Concuss( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	const int concuss = READ_BYTE();
	g_iSgtcAnnouncePending = READ_BYTE();
	if( g_iSgtcAnnouncePending == 0 )
	{
		gHUD.SetConcussEffectRaw( concuss );
		return 1;
	}
	m_iFlags |= HUD_ACTIVE;
	return 1;
}

int CHudSgtc::MsgFunc_Stamina( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	const int iPrev = s_iSgtcStaminaByte;
	s_iSgtcStaminaByte = READ_BYTE();
	m_iStamina = s_iSgtcStaminaByte;
	// Build-19 diagnostic (gated like sgtc_test): prove the server actually sends
	// stamina changes and that the stamn_bar sprite resolves (index 0 = missing).
	if( gEngfuncs.pfnGetCvarFloat( "sv_cheats" ) >= 1.0f &&
		gEngfuncs.pfnGetCvarFloat( "sgtc_test_enable" ) > 0.0f )
	{
		static bool s_bSpriteLogged = false;
		if( !s_bSpriteLogged )
		{
			s_bSpriteLogged = true;
			gEngfuncs.Con_Printf( "[sgtc_stamina] stamn_bar index=%d size=%dx%d\n",
				gHUD.GetSpriteIndex( "stamn_bar" ),
				gHUD.GetSpriteRect( gHUD.GetSpriteIndex( "stamn_bar" ) ).right,
				gHUD.GetSpriteRect( gHUD.GetSpriteIndex( "stamn_bar" ) ).bottom );
		}
		if( iPrev != s_iSgtcStaminaByte )
			gEngfuncs.Con_Printf( "[sgtc_stamina] byte %d -> %d (t=%.2f)\n",
				iPrev, s_iSgtcStaminaByte, gEngfuncs.GetClientTime() );
	}
	return 1;
}

// ref FUN_10076d51 — SubModel global DAT_100f6fb8
int CHudSgtc::MsgFunc_SubModel( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	m_iSubModel = READ_BYTE();
	return 1;
}

// ref FUN_10076d7d — HasKey: keymode + vote timer (offsets +0x34/+0x30).
int CHudSgtc::MsgFunc_HasKey( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	int mode = READ_BYTE();
	if( mode < 2 )
		m_iKeyMode = mode;
	else if( mode == 2 )
		m_flKeyTime = gHUD.m_flTime + 5.0f;
	else
		m_flKeyTime = 0.0f;

	int vote = READ_BYTE();
	if( vote == 1 )
	{
		g_iSgtcVoteCount = -1;
		g_iSgtcVoteSel = -1;
		m_iHasKey = 0;
		g_flSgtcVoteAnim = 0;
	}
	else if( vote == 2 )
	{
		if( g_iSgtcVoteCount >= 0 )
			SGTC_VoteFormatCenter( g_iSgtcVoteCount );
		else
		{
			g_iSgtcVoteCount = 5;
			g_iSgtcVoteSel = -1;
			m_iHasKey = 0;
			g_flSgtcVoteAnim = 0;
		}
	}
	return 1;
}

// ref FUN_1007675a — the REAL 'Radio' usermessage handler (thunk 0x10072b39
// registered @0x10072f42).  Reads msgId + playerEnt, GetPlayerInfo for the name,
// player class from the extra-info table (stride 0x1a → g_PlayerExtraInfo):
// class 2 = Goa'uld → '#RadioMessageGoauldNN' + 'radio/songoaNN.wav',
// class 1 = Torri → '#RadioMessageNN' + 'radio/sonNN.wav'.  Formats
// '%c(Radio)%s: %s' and pushes it to saytext.
int CHudSgtc::MsgFunc_Radio( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	int msgId = READ_BYTE();
	int ent   = READ_BYTE();
	if( msgId < 0 ) msgId = 0;

	hud_player_info_t info;
	memset( &info, 0, sizeof( info ) );
	GetPlayerInfo( ent, &info );

	const int pclass = ( ent >= 0 && ent <= MAX_PLAYERS ) ? g_PlayerExtraInfo[ent].playerclass : 0;

	char key[64];
	char wav[64];
	const char *name = info.name;
	if( !name ) name = "";
	if( pclass == 2 )
	{
		snprintf( key, sizeof( key ), "#RadioMessageGoauld%02d", msgId );
		snprintf( wav, sizeof( wav ), "radio/songoa%02d.wav", msgId );
	}
	else
	{
		snprintf( key, sizeof( key ), "#RadioMessage%02d", msgId );
		snprintf( wav, sizeof( wav ), "radio/son%02d.wav", msgId );
	}

	const char *msg = CHudTextMessage::BufferedLocaliseTextString( key );
	char line[256];
	snprintf( line, sizeof( line ), "%c(Radio)%s: %s\n", 2, name, msg ? msg : key );
	gHUD.m_SayText.SayTextPrint( line, sizeof( line ) - 1, ent );
	if( pclass == 1 || pclass == 2 )
		gEngfuncs.pfnPlaySoundByName( wav, 1.0f );
	return 1;
}

// ref FUN_10076a85 — 4-byte subcommand stream (server REG "Armor", 4).
int CHudSgtc::MsgFunc_Armor( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	int a = READ_BYTE();
	int b = READ_BYTE();
	int sub = READ_BYTE();
	int d = READ_BYTE();

	// ref FUN_10073e85 @0x10073e85 (the "Armor" usermsg handler) reads a SHORT
	// then a BYTE and stores the byte in DAT_100f225c — the flag the menu-12
	// Update uses to gate the glstaff/usas slots (0 = both locked).
	SgtcWepSel_SetArmorMode( sub );

	if( sub == 0 )
	{
		m_flArmorBar = (float)a;
		m_iArmorAux = b;
	}
	if( sub == 1 )
		m_iArmorOverlay = 1;
	else if( sub == 2 )
		m_iArmorOverlay = 0;	// ref @0x10076aed — sub==2 writes +0x1684=0 only; +0x168c kept

	if( sub == 3 && m_iArmorState == 0 )
	{
		m_iArmorState = 1;
		m_iArmorTeamTint = 0; // ref DAT_100f71d4
	}
	else if( sub == 4 && m_iArmorState == 2 )
	{
		m_iArmorState = 3;
		m_iArmorTeamTint = 0;
	}
	// ref @0x10076b0e — sub==4 clears DAT_100f70d0 unconditionally (outside the state gate).
	if( sub == 4 )
		m_iTacDispActive = 0;
	if( sub == 5 && m_iArmorState == 0 )
	{
		m_iArmorState = 1;
		m_iArmorTeamTint = 1;
	}
	if( sub == 10 )
		m_iTacDispActive = 1;
	if( sub == 1 )
		m_iArmorParam = d;
	return 1;
}

// ref FUN_10076c5f — Progress batch (icon + 28 team bytes + tail).
int CHudSgtc::MsgFunc_Progress( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	m_iProgress = READ_BYTE();
	m_iProgressIcon = READ_BYTE();
	for( int i = 0; i < SGTC_BLIP_SLOTS; i++ )
		m_iProgressTeam[i] = READ_BYTE();
	m_iProgressEnd = READ_BYTE();

	if( m_iProgress == 0 )
		m_iBlipProgress = 0; // ref FUN_10076c5f: no DAT_100e1b1c / DAT_100e1b90 reset on Progress msg
	if( m_iProgress == 100 )
		m_iBlipProgress = 100;

	// ref FUN_10076c5f (sgtc2_dll build) — the "Progress" handler also stores
	// byte 2 in DAT_100e1b94 (menu-12 budget), the 28 slot bytes in
	// DAT_100e1aa4 (per-slot state 0/1/2) and the trailing byte in
	// DAT_100e1b98 (reset request). Our server sends m_iNukeCharge /
	// m_iNukeBonus / m_iProgressTeam[28] / reset-flag in exactly that order
	// (dlls/sgtc_player.cpp SGTC_SendProgressIfDirty).
	{
		unsigned char szStates[SGTC_NUM_WEAPON_SLOTS];
		for( int i = 0; i < SGTC_NUM_WEAPON_SLOTS; i++ )
			szStates[i] = (unsigned char)m_iProgressTeam[i];
		SgtcWepSel_SetProgress( m_iProgressIcon, szStates, m_iProgressEnd );
	}
	m_iFlags |= HUD_ACTIVE;
	return 1;
}

// ref shipped FUN_10074236 (via thunk @0x10070da5) — Blip: type byte + 2
// READ_COORDs, ftol'd (NOT raw int16: FUN_100634a0 wraps the short read and the
// result goes through __ftol → wire is the coord×8 short).
int CHudSgtc::MsgFunc_Blip( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	int type = READ_BYTE();
	int rx = (int)READ_COORD();
	int ry = (int)READ_COORD();

	if( type == 0x32 )
	{
		m_iRadarFocusX = rx;
		m_iRadarFocusY = ry;
	}
	else if( type == 0x3d )
	{
		m_iRadarFocusX = 0;
		m_iRadarFocusY = 0;
	}
	else
	{
		for( int slot = 1; slot < SGTC_RADAR_SLOTS; slot++ )
		{
			if( m_iRadarType[slot] == 0 )
			{
				m_iRadarType[slot] = type;
				m_iRadarX[slot] = rx;
				m_iRadarY[slot] = ry;
				m_iRadarAlpha[slot] = 255;
				break;
			}
		}
	}
	m_iFlags |= HUD_ACTIVE;
	return 1;
}

// ref FUN_10076c3d (sgtc2) / shipped hook @0x10072c40 — BEGIN_READ only; no field
// stores (SGTC draw gates on gHUD.m_iHideHUDDisplay / ds:0x100e9d0c, not this msg).
int CHudSgtc::MsgFunc_HideHud( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	for( int i = 0; i < iSize; i++ )
		READ_BYTE();
	return 1;
}

#include "sgtc_env.h"

static int SGTC_DrainMsg( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	for( int i = 0; i < iSize; i++ )
		READ_BYTE();
	return 1;
}

// ref @0x100768d4 — short,long,short,short,short,short,short,short,string,byte
int CHudSgtc::MsgFunc_AddPortrait( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	int spr   = READ_SHORT();	// iVar1
	int t     = READ_LONG();	// iVar2 → +0x28
	int i3    = READ_SHORT();	// DAT_100f70c8
	int i4    = READ_SHORT();
	int x     = READ_SHORT();	// iVar5 → compared w/ this+0x40
	int hp0   = READ_SHORT();	// +0x44
	int hp1   = READ_SHORT();	// +0x48
	int hp2   = READ_SHORT();	// +0x4c
	const char *name = READ_STRING();
	int id    = READ_BYTE();

	if( m_iPortraitX == x )
	{
		m_iPortraitActiveTeam = i3;
		m_flPortraitTime = (float)t;
		m_iPortraitId = id;
		m_iPortraitHp[0] = hp0;
		m_iPortraitHp[1] = hp1;
		m_iPortraitHp[2] = hp2;
		if( spr )
		{
			m_iPortraitSprite = spr;
			m_iPortraitEnt = x;
		}
		strncpy( m_szPortraitName, name, sizeof( m_szPortraitName ) - 1 );
	}
	SGTC_UpdatePortraitSlot( this, spr, (float)t, i3, i4, x, hp0, hp1, hp2, name, id );
	m_iFlags |= HUD_ACTIVE;
	return 1;
}

// ref @0x10076eb9 — short + byte; byte==0 full reset, else portrait-slot eviction.
int CHudSgtc::MsgFunc_Reset( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	int arg = READ_SHORT();
	int mode = READ_BYTE();
	if( mode == 0 )
		Reset();
	else
		SGTC_EvictPortraitSlot( this, arg );	// ref FUN_10072609 — not m_iPortraitEnt=arg

	// ref tail (always): ClientCmd("customcolor %d") — %d reads the low dword of the
	// 'custcolor' cvar's double-rep (ref float->double->%d quirk; effectively 0 for
	// exact-valued floats).  Reproduced literally.
	{
		cvar_t *cv = gEngfuncs.pfnGetCvarPointer( "custcolor" );
		float fv = cv ? cv->value : 0.0f;
		double dv = (double)fv;
		long long llBits;
		memcpy( &llBits, &dv, sizeof( llBits ) );	// bit copy, not a type pun through a pointer
		int lo = (int)llBits;				// low dword of double bits (little-endian)
		char cmd[256];
		snprintf( cmd, sizeof( cmd ), "customcolor %d\n", lo );
		ClientCmd( cmd );
	}

	// ref @0x10076f88 + 0x10077017-0x10077054 — reset +0x1688 flag, vote anim/sel,
	// 5 announce lines, slide defaults +0x1554=0x1d / +0x155c=0xa0, +0x1560=0, +0x54=0.
	m_iArmorFlag1688 = 0;
	g_flSgtcVoteAnim = 0;
	g_iSgtcVoteCount = -1;
	g_iSgtcVoteSel = -1;
	SGTC_InitAnnounceLines( this );
	m_iAmmoLineSlide = 0x1d;
	m_iAmmoLineStep = 0;
	m_iKeyBannerSlide = 0xa0;
	m_iKeyBannerStep = 0;
	m_iHudInfoMode = 0;
	return 1;
}

// ref @0x10076a20 — byte 0 → short self @+0x20, short player @+0x40
int CHudSgtc::MsgFunc_AboutMe( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	if( READ_BYTE() == 0 )
	{
		m_iAboutMeSelf = READ_SHORT();
		m_iAboutMePlayer = READ_SHORT();
		m_iPortraitX = m_iAboutMePlayer; // ref +0x1564 copy
	}
	return 1;
}

// ref @0x10077088 — no payload; toggles view_portraits cvar.
int CHudSgtc::MsgFunc_TogglePor( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	cvar_t *cv = gEngfuncs.pfnGetCvarPointer( "view_portraits" );
	if( cv )
		gEngfuncs.Cvar_SetValue( "view_portraits", cv->value == 0.0f ? 1.0f : 0.0f );
	return 1;
}

// ref @0x100771f3 (sgtc2) / FUN_1007432f (shipped) — SHORT roundtime + BYTE state,
// matching ref SetHudRoundTime @0x145494 (WRITE_SHORT then WRITE_BYTE).
int CHudSgtc::MsgFunc_SetRndTim( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	m_flRoundTime = (float)READ_SHORT();	// shipped this+0x6c (FUN_100632dd)
	int state = READ_BYTE();	// then the byte → vtbl+0x24 = FUN_10073b62
	m_flRoundStart = gHUD.m_flTime;	// shipped this+0x68 = engine time (DAT_100e7800)
	// shipped calls vtbl+0x24(state) -> FUN_10073b62 = this+0x70 setter, i.e.
	// the message's state byte arms (1) AND disarms (0) the Draw gate
	// (@0x1007325c). The setter has no direct .text caller — vtable only.
	// VERIFIED 2026-10-21: vtbl slot 0x1009e738 = 0x10073b62 (was [INFERENCE]).
	// ref vtbl+0x24 setter: mode 1 arms round timer; 0 disarms; 2 is spectator-only.
	// P43b (2026-10-21): state 2 MUST arm the draw gate — server sends
	// SetHudRoundTime(roundtime,2) to observers (sgtc_round.cpp IsObserver arm),
	// and the shipped orig draws the observer clock top-centre (graphics_comparison
	// P43, orig ctp_crypt '9:15'). Draw gate +0x70 != 0 treats any nonzero state as
	// armed; port previously disarmed on state 2 -> observer clock never drawn.
	m_iRoundTimerMode = state;
	m_iRoundTimerOn = ( state != 0 ) ? 1 : 0;
	// P150/P142: mode-1 arms BL round timer on ticket maps but keeps +0x58 (ref
	// ticket dock). Clearing ticket on state==1 broke ctp_amon after mp_resetround.
	// Mode-2 clears ticket dock for observer top-centre clock (p43h/p149f).
	if( state == 2 && m_flRoundTime > 0.0f )
		m_iTicketModeFlag = 0;
	return 1;
}

// ref @0x1007724b — clears zat flag + portrait arrays + 256-slot loop.
int CHudSgtc::MsgFunc_NewRnd( const char *pszName, int iSize, void *pbuf )
{
	(void)pszName;
	BEGIN_READ( pbuf, iSize );
	m_fZatTouchActive = 0;
	Reset();
	// ref NewRnd also calls the viewport's HideVGUIMenu vtable slot via
	// FUN_1004954d(DAT_100e78bc) — drop any open VGUI menu on round start.
	if( g_pSgtcViewport )
		g_pSgtcViewport->HideVGUIMenu();
	return 1;
}

int CHudSgtc::MsgFunc_ClearEnv( const char *pszName, int iSize, void *pbuf )
{
	return g_ClientEnv.MsgFunc_ClearEnv( pszName, iSize, pbuf );
}

int CHudSgtc::MsgFunc_EnvFull( const char *pszName, int iSize, void *pbuf )
{
	return g_ClientEnv.MsgFunc_EnvFull( pszName, iSize, pbuf );
}

int CHudSgtc::MsgFunc_EnvRain( const char *pszName, int iSize, void *pbuf )
{
	return g_ClientEnv.MsgFunc_EnvRain( pszName, iSize, pbuf );
}

int CHudSgtc::MsgFunc_EnvFount( const char *pszName, int iSize, void *pbuf )
{
	return g_ClientEnv.MsgFunc_EnvFount( pszName, iSize, pbuf );
}

int CHudSgtc::MsgFunc_NewLens( const char *pszName, int iSize, void *pbuf )
{
	return g_ClientEnv.MsgFunc_NewLens( pszName, iSize, pbuf );
}

int CHudSgtc::MsgFunc_AddRing( const char *pszName, int iSize, void *pbuf )
{
	return g_ClientEnv.MsgFunc_AddRing( pszName, iSize, pbuf );
}

int CHudSgtc::MsgFunc_ChgPartSt( const char *pszName, int iSize, void *pbuf )
{
	return g_ClientEnv.MsgFunc_ChgPartSt( pszName, iSize, pbuf );
}
