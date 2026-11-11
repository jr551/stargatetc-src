// sgtc_options.cpp — in-game music options panel for the SGTC client.
//
// A small local-only VGUI menu (menu id SGTC_MENU_OPTIONS, 70) hosted by the
// TeamFortressViewport, opened and closed by the `sgtc_options` client command.
// It exposes the two archived cvars owned by sgtc_music.cpp:
//   - an On/Off toggle button,
//   - a 0..100 volume slider with a percentage label.
// Both write through SgtcMusic_Set*(), which updates the cvars and applies the
// change immediately, so the panel, console and config file cannot diverge.
//
// Escape closes the panel through TeamFortressViewport::KeyInput (its id is in
// the same list as the SGTC server menus 10/12/60); opening the console closes
// it too (SgtcOptions_Frame) so the console keeps the keyboard and cursor.
//
// Without an engine VGUI root (the VGUI-less client) the command prints the
// current on/off state, volume and active track, then the console equivalents
// instead of doing nothing.
//
// NEW file — sgtc_* only, no stock edits.
// Headers used: hud.h, cl_util.h, sgtc_music.h, sgtc_options.h, sgtc_vgui.h,
//               vgui_TeamFortressViewport.h, vgui_slider2.h (+ VGUI widget set)

#include "hud.h"
#include "cl_util.h"
#include "sgtc_music.h"
#include "sgtc_options.h"
#include "sgtc_vgui.h"

#ifdef SGTC_REAL_VGUI

#include "vgui_TeamFortressViewport.h"
#include "vgui_slider2.h"
#include "demo_api.h"

#include <VGUI_ActionSignal.h>
#include <VGUI_Color.h>
#include <VGUI_IntChangeSignal.h>
#include <VGUI_Label.h>
#include <VGUI_LineBorder.h>
#include <VGUI_TextPanel.h>

#include <stdio.h>

class CSgtcOptionsPanel;

static CSgtcOptionsPanel *s_pOptionsPanel = NULL;
static TeamFortressViewport *s_pOptionsOwner = NULL;

// ---------------------------------------------------------------------------
// panel
// ---------------------------------------------------------------------------

class CSgtcOptionsPanel : public CMenuPanel
{
public:
	CSgtcOptionsPanel( int iTrans, int iRemoveMe, int x, int y, int wide, int tall );

	virtual void Open( void );

	void SyncFromCvars( void );
	void OnVolumeChanged( int value );

private:
	TextPanel *m_pWindow;
	Label *m_pTitle;
	Label *m_pStatus;
	Label *m_pVolume;
	Label *m_pNowPlaying;
	Label *m_pHint;
	CommandButton *m_pToggle;
	CommandButton *m_pClose;
	Slider2 *m_pSlider;
	bool m_bSyncing;
};

class CSgtcOptionsHandler_Toggle : public ActionSignal
{
public:
	virtual void actionPerformed( Panel *panel )
	{
		SgtcMusic_SetEnabled( !SgtcMusic_GetEnabled( ) );
		if ( s_pOptionsPanel )
			s_pOptionsPanel->SyncFromCvars( );
	}
};

class CSgtcOptionsHandler_Volume : public IntChangeSignal
{
public:
	virtual void intChanged( int value, Panel *panel )
	{
		if ( s_pOptionsPanel )
			s_pOptionsPanel->OnVolumeChanged( value );
	}
};

class CSgtcOptionsHandler_Close : public ActionSignal
{
public:
	virtual void actionPerformed( Panel *panel )
	{
		if ( gViewPort )
			gViewPort->HideTopMenu( );
	}
};

CSgtcOptionsPanel::CSgtcOptionsPanel( int iTrans, int iRemoveMe, int x, int y, int wide, int tall )
	: CMenuPanel( iTrans, iRemoveMe, x, y, wide, tall )
{
	CSchemeManager *pSchemes = gViewPort->GetSchemeManager( );
	SchemeHandle_t hTitleScheme = pSchemes->getSchemeHandle( "Title Font" );
	SchemeHandle_t hTextScheme = pSchemes->getSchemeHandle( "Briefing Text" );

	const int iWindowWide = XRES( 400 );
	const int iWindowTall = YRES( 280 );
	const int iWindowX = ( ScreenWidth - iWindowWide ) / 2;
	const int iWindowY = ( ScreenHeight - iWindowTall ) / 2;

	// Opaque black window with the mod's blue border, same colours as the
	// briefing/team-class panels.
	m_pWindow = new TextPanel( "", iWindowX, iWindowY, iWindowWide, iWindowTall );
	m_pWindow->setParent( this );
	m_pWindow->setBgColor( 0, 0, 0, 0 );
	m_pWindow->setBorder( new LineBorder( Color( 0, 0, 255, 0 ) ) );

	m_pTitle = new Label( "", XRES( 24 ), YRES( 16 ) );
	m_pTitle->setParent( m_pWindow );
	m_pTitle->setFont( pSchemes->getFont( hTitleScheme ) );
	m_pTitle->setFgColor( 0x55, 0x73, 0xb9, 0 );
	m_pTitle->setPaintBackgroundEnabled( false );
	m_pTitle->setText( "Music" );

	m_pStatus = new Label( "", XRES( 24 ), YRES( 62 ) );
	m_pStatus->setParent( m_pWindow );
	m_pStatus->setFont( pSchemes->getFont( hTextScheme ) );
	m_pStatus->setFgColor( 0x55, 0x73, 0xb9, 0 );
	m_pStatus->setPaintBackgroundEnabled( false );

	m_pToggle = new CommandButton( "On", XRES( 24 ), YRES( 88 ), XRES( 90 ), YRES( 26 ) );
	m_pToggle->setParent( m_pWindow );
	m_pToggle->addActionSignal( new CSgtcOptionsHandler_Toggle( ) );

	m_pVolume = new Label( "", XRES( 24 ), YRES( 132 ) );
	m_pVolume->setParent( m_pWindow );
	m_pVolume->setFont( pSchemes->getFont( hTextScheme ) );
	m_pVolume->setFgColor( 0x55, 0x73, 0xb9, 0 );
	m_pVolume->setPaintBackgroundEnabled( false );

	m_pSlider = new Slider2( XRES( 150 ), YRES( 138 ), XRES( 210 ), YRES( 18 ), false );
	m_pSlider->setParent( m_pWindow );
	m_pSlider->setRange( 0, 100 );
	m_pSlider->addIntChangeSignal( new CSgtcOptionsHandler_Volume( ) );

	m_pNowPlaying = new Label( "", XRES( 24 ), YRES( 176 ) );
	m_pNowPlaying->setParent( m_pWindow );
	m_pNowPlaying->setFont( pSchemes->getFont( hTextScheme ) );
	m_pNowPlaying->setFgColor( 0x55, 0x73, 0xb9, 0 );
	m_pNowPlaying->setPaintBackgroundEnabled( false );

	m_pHint = new Label( "", XRES( 24 ), YRES( 212 ) );
	m_pHint->setParent( m_pWindow );
	m_pHint->setFont( pSchemes->getFont( hTextScheme ) );
	m_pHint->setFgColor( 0x55, 0x73, 0xb9, 0 );
	m_pHint->setPaintBackgroundEnabled( false );
	m_pHint->setText( "Changes are saved immediately. Press Escape to close." );

	m_pClose = new CommandButton( "Close", XRES( 270 ), YRES( 236 ), XRES( 90 ), YRES( 26 ) );
	m_pClose->setParent( m_pWindow );
	m_pClose->addActionSignal( new CSgtcOptionsHandler_Close( ) );

	m_bSyncing = false;

	SyncFromCvars( );
}

void CSgtcOptionsPanel::Open( void )
{
	SyncFromCvars( );
	CMenuPanel::Open( );
}

void CSgtcOptionsPanel::SyncFromCvars( void )
{
	const int enabled = SgtcMusic_GetEnabled( );
	const int percent = (int)( SgtcMusic_GetVolume( ) * 100.0f + 0.5f );

	m_pStatus->setText( "Ambient music: %s", enabled ? "On" : "Off" );
	m_pToggle->setText( enabled ? "On" : "Off" );

	// setValue() fires IntChangeSignal; the rebind must not look like a drag.
	m_bSyncing = true;
	m_pSlider->setValue( percent );
	m_bSyncing = false;

	m_pVolume->setText( "Volume: %d%%", percent );

	const char *slug = SgtcMusic_GetActiveSlug( );
	if ( enabled && slug && slug[0] )
		m_pNowPlaying->setText( "Now playing: music/%s", slug );
	else
		m_pNowPlaying->setText( "Now playing: (none)" );
}

void CSgtcOptionsPanel::OnVolumeChanged( int value )
{
	if ( m_bSyncing )
		return;

	if ( value < 0 )
		value = 0;
	else if ( value > 100 )
		value = 100;

	SgtcMusic_SetVolume( value / 100.0f );
	m_pVolume->setText( "Volume: %d%%", value );
}

// ---------------------------------------------------------------------------
// panel lifecycle
// ---------------------------------------------------------------------------

static void SgtcOptions_CheckOwner( void )
{
	if ( s_pOptionsOwner != gViewPort )
	{
		s_pOptionsPanel = NULL;	// children of the old viewport are gone with it
		s_pOptionsOwner = gViewPort;
	}
}

static void SgtcOptions_OpenPanel( void )
{
	SgtcOptions_CheckOwner( );
	if ( !gViewPort )
		return;

	if ( !s_pOptionsPanel )
	{
		s_pOptionsPanel = new CSgtcOptionsPanel( 255, 0, 0, 0, ScreenWidth, ScreenHeight );
		s_pOptionsPanel->setParent( gViewPort );
		s_pOptionsPanel->setVisible( false );
	}
	else
	{
		s_pOptionsPanel->setBounds( 0, 0, ScreenWidth, ScreenHeight );
	}

	s_pOptionsPanel->SyncFromCvars( );
	s_pOptionsPanel->Reset( );	// clears any stale next-menu link
	s_pOptionsPanel->SetMenuID( SGTC_MENU_OPTIONS );
	s_pOptionsPanel->SetActive( true );

	// Put the options in front of whatever server menu is up; closing them
	// re-opens the queued menu exactly like the viewport's own menus do.
	CMenuPanel *pPrevious = gViewPort->m_pCurrentMenu;
	if ( pPrevious && pPrevious != s_pOptionsPanel )
	{
		pPrevious->Close( );
		s_pOptionsPanel->SetNextMenu( pPrevious );
	}

	gViewPort->SetCurrentMenu( s_pOptionsPanel );
	gViewPort->UpdateCursorState( );
}

static void SgtcOptions_ClosePanel( void )
{
	SgtcOptions_CheckOwner( );
	if ( !s_pOptionsPanel || !gViewPort )
		return;

	if ( gViewPort->m_pCurrentMenu == s_pOptionsPanel )
	{
		gViewPort->HideTopMenu( );	// pops us and re-opens a queued menu
		return;
	}

	s_pOptionsPanel->Close( );	// not in front (unexpected): just hide it
}

#endif // SGTC_REAL_VGUI

// ---------------------------------------------------------------------------
// command entry points
// ---------------------------------------------------------------------------

void SgtcOptions_Init( void )
{
	gEngfuncs.pfnAddCommand( "sgtc_options", SgtcOptions_Toggle );
}

void SgtcOptions_Frame( void )
{
#ifdef SGTC_REAL_VGUI
	static bool s_bConsoleWasVisible = false;
	bool consoleVisible = gEngfuncs.Con_IsVisible( ) != 0;

	SgtcOptions_CheckOwner( );
	if ( consoleVisible && !s_bConsoleWasVisible && s_pOptionsPanel && s_pOptionsPanel->isVisible( ) )
		SgtcOptions_ClosePanel( );

	s_bConsoleWasVisible = consoleVisible;
#endif
}

// No engine VGUI root: report the same state the panel would show (on/off,
// volume, active track) and then how to change it from the console. Called by
// both the runtime probe failure and the VGUI-less translation unit.
static void SgtcOptions_PrintFallback( void )
{
	const char *slug = SgtcMusic_GetActiveSlug( );
	const char *map = SgtcMusic_GetMapName( );
	const char *state = SgtcMusic_GetEnabled( ) ? "on" : "off";
	const float volume = SgtcMusic_GetVolume( );

	if ( slug && slug[0] && map && map[0] )
	{
		gEngfuncs.Con_Printf( "[music] ambient music %s, volume %.2f, track music/%s (map %s)\n",
			state, volume, slug, map );
	}
	else if ( slug && slug[0] )
	{
		gEngfuncs.Con_Printf( "[music] ambient music %s, volume %.2f, track music/%s\n",
			state, volume, slug );
	}
	else
	{
		gEngfuncs.Con_Printf( "[music] ambient music %s, volume %.2f, no active track\n",
			state, volume );
	}

	gEngfuncs.Con_Printf( "[music] options UI unavailable (no VGUI); "
		"use sgtc_music 0|1 and sgtc_musicvolume 0..1\n" );
}

void SgtcOptions_Toggle( void )
{
#ifdef SGTC_REAL_VGUI
	// VGui_GetPanel() is the same root probe VGui_Startup() uses; without it
	// the viewport exists but is never painted, so a panel would be invisible.
	if ( !gViewPort || !gEngfuncs.VGui_GetPanel || !gEngfuncs.VGui_GetPanel( ) )
	{
		SgtcOptions_PrintFallback( );
		return;
	}

	if ( gEngfuncs.pDemoAPI && gEngfuncs.pDemoAPI->IsPlayingback( ) )
	{
		gEngfuncs.Con_Printf( "[music] options unavailable during demo playback\n" );
		return;
	}

	SgtcOptions_CheckOwner( );

	if ( s_pOptionsPanel && s_pOptionsPanel->isVisible( ) )
		SgtcOptions_ClosePanel( );
	else
		SgtcOptions_OpenPanel( );
#else
	SgtcOptions_PrintFallback( );
#endif
}
