//=========== (C) Copyright 1996-2002 Valve, L.L.C. All rights reserved. ===========
//
// The copyright to the contents herein is the property of Valve, L.L.C.
// The contents may be used and/or copied only with the written permission of
// Valve, L.L.C., or in accordance with the terms and conditions stipulated in
// the agreement/contract under which the contents have been supplied.
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//=============================================================================

#include "VGUI_Font.h"
#include "VGUI_ScrollPanel.h"
#include "VGUI_TextImage.h"


#include "hud.h"
#include "cl_util.h"
#include "camera.h"
#include "kbutton.h"
#include "const.h"

#include "vgui_int.h"
#include "vgui_TeamFortressViewport.h"
#include "l10n.h"
#include "sgtc_vgui.h"
#include "sgtc_vgui_button.h"

//-----------------------------------------------------------------------------
// Purpose: Displays the MOTD and basic server information
//-----------------------------------------------------------------------------
class CMessageWindowPanel : public CMenuPanel
{
public:
	CMessageWindowPanel( const char *szMOTD, const char *szTitle, int iShadeFullScreen, int iRemoveMe, int x, int y, int wide, int tall );

private:
	CTransparentPanel *m_pBackgroundPanel;
};

//-----------------------------------------------------------------------------
// Purpose: Creates a new CMessageWindowPanel
// Output : CMenuPanel - interface to the panel
//-----------------------------------------------------------------------------
CMenuPanel *CMessageWindowPanel_Create( const char *szMOTD, const char *szTitle, int iShadeFullscreen, int iRemoveMe, int x, int y, int wide, int tall )
{
	return new CMessageWindowPanel( szMOTD, szTitle, iShadeFullscreen, iRemoveMe, x, y, wide, tall );
}

//-----------------------------------------------------------------------------
// Purpose: Constructs a message panel
//-----------------------------------------------------------------------------
CMessageWindowPanel::CMessageWindowPanel( const char *szMOTD, const char *szTitle, int iShadeFullscreen, int iRemoveMe, int x, int y, int wide, int tall ) : CMenuPanel( iShadeFullscreen ? 100 : 255, iRemoveMe, x, y, wide, tall )
{
	// Get the scheme used for the Titles
	CSchemeManager *pSchemes = gViewPort->GetSchemeManager();

	// schemes
	SchemeHandle_t hTitleScheme = pSchemes->getSchemeHandle( "Title Font" );
	SchemeHandle_t hMOTDText = pSchemes->getSchemeHandle( "Briefing Text" );

	// color schemes
	int r, g, b, a;

	// Window: a card centred on the screen, sized in layout pixels (SgtcLayout_S)
	// so it keeps its proportions at any resolution. Blue frame, as on the other
	// SGTC menus (the stock window used an orange frame). CTransparentPanel's
	// value is transparency (0 would skip the fill), so 48 is a dark fill about
	// 80% opaque; the stock 255 left the text floating over the game.
	int iWinW = SgtcLayout_S( 640 );
	if ( iWinW > wide - 2 * SgtcLayout_S( 24 ) )
		iWinW = wide - 2 * SgtcLayout_S( 24 );
	int iWinH = SgtcLayout_S( 440 );
	if ( iWinH > tall - 2 * SgtcLayout_S( 24 ) )
		iWinH = tall - 2 * SgtcLayout_S( 24 );
	const int iWinX = x + ( wide - iWinW ) / 2;
	const int iWinY = y + ( tall - iWinH ) / 2;

	// Create the window
	m_pBackgroundPanel = new CTransparentPanel( iShadeFullscreen ? 48 : 100, iWinX, iWinY, iWinW, iWinH );
	m_pBackgroundPanel->setParent( this );
	m_pBackgroundPanel->setBorder( new LineBorder( Color( 0, 0, 255, 0 ) ) );
	m_pBackgroundPanel->setVisible( true );

	// Title, centred across the top of the window. The title font is the scheme's
	// own title font (the stock code overrode it with the 15pt button font).
	Label *pLabel = new Label( "", iWinX, iWinY + SgtcLayout_S( 14 ), iWinW, SgtcLayout_S( 30 ) );
	pLabel->setParent( this );
	pLabel->setFont( pSchemes->getFont( hTitleScheme ) );
	pLabel->setFgColor( 0x8c, 0x8c, 0xf0, 0 );
	pLabel->setPaintBackgroundEnabled( false );
	pLabel->setContentAlignment( vgui::Label::a_center );
	pLabel->setText( "%s", szTitle );

	// Scroll area between the title and the OK button, inset from the frame.
	const int iBtnW = SgtcLayout_S( 200 );
	const int iBtnH = SgtcLayout_S( 40 );
	const int iBtnY = iWinY + iWinH - SgtcLayout_S( 16 ) - iBtnH;
	const int iScrollX = iWinX + SgtcLayout_S( 24 );
	const int iScrollY = iWinY + SgtcLayout_S( 60 );
	const int iScrollW = iWinW - 2 * SgtcLayout_S( 24 );
	const int iScrollH = iBtnY - SgtcLayout_S( 12 ) - iScrollY;

	// Create the Scroll panel
	ScrollPanel *pScrollPanel = new CTFScrollPanel( iScrollX, iScrollY, iScrollW, iScrollH );
	pScrollPanel->setParent( this );

	// force the scrollbars on so clientClip will take them in account after the validate
	pScrollPanel->setScrollBarAutoVisible( false, false );
	pScrollPanel->setScrollBarVisible( true, true );
	pScrollPanel->validate();

	// Create the text panel
	TextPanel *pText = new TextPanel( "", 0, 0, 64, 64 );
	pText->setParent( pScrollPanel->getClient() );

	// Font from the scheme; colour is the SGTC blue, because the scheme's own
	// (0,0,200) is too dark to read on black.
	pText->setFont( pSchemes->getFont( hMOTDText ) );
	pText->setFgColor( 0x55, 0x73, 0xb9, 0 );
	pSchemes->getBgColor( hMOTDText, r, g, b, a );
	pText->setBgColor( r, g, b, a );
	pText->setText( szMOTD );

	// Get the total size of the MOTD text and resize the text panel
	int iScrollSizeX, iScrollSizeY;

	// First, set the size so that the client's wdith is correct at least because the
	//  width is critical for getting the "wrapped" size right.
	// You'll see a horizontal scroll bar if there is a single word that won't wrap in the
	//  specified width.
	pText->getTextImage()->setSize( pScrollPanel->getClientClip()->getWide(), pScrollPanel->getClientClip()->getTall() );
	pText->getTextImage()->getTextSizeWrapped( iScrollSizeX, iScrollSizeY );

	// Now resize the textpanel to fit the scrolled size
	pText->setSize( iScrollSizeX, iScrollSizeY );

	//turn the scrollbars back into automode
	pScrollPanel->setScrollBarAutoVisible( true, true );
	pScrollPanel->setScrollBarVisible( false, false );

	pScrollPanel->validate();

	// OK button, centred under the text. No key prefix, so the label is centred.
	CommandButton *pButton = new CSgtcMenuButton( L10n_ToLegacy( CHudTextMessage::BufferedLocaliseTextString( "#Menu_OK" ) ), iWinX + ( iWinW - iBtnW ) / 2, iBtnY, iBtnW, iBtnH );
	pButton->setBoundKey( (char)255 );
	pButton->setContentAlignment( vgui::Label::a_center );
	pButton->addActionSignal( new CMenuHandler_TextWindow( HIDE_TEXTWINDOW ) );
	pButton->setParent( this );
}






