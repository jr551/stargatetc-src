// sgtc_vgui_button.h — the button the SGTC menus use for team/class, briefing and
// MOTD actions.
//
// The stock armed (hovered) state paints the scheme's FgColorArmed (0,0,255) on
// its BgColorArmed (0,0,200), so the label disappeared under the mouse. This keeps
// the stock blue frame and label font, shows a white label and a navy fill while
// the button is armed, and otherwise matches CommandButton's idle grey label.
//
// NEW file — sgtc_* only, no stock edits.

#ifndef SGTC_VGUI_BUTTON_H
#define SGTC_VGUI_BUTTON_H

#include "vgui_TeamFortressViewport.h"

class CSgtcMenuButton : public CommandButton
{
public:
    CSgtcMenuButton( const char *text, int x, int y, int wide, int tall )
        : CommandButton( text, x, y, wide, tall )
    {
    }

    virtual void paint()
    {
        if ( isArmed() )
            setFgColor( 255, 255, 255, 0 );
        else
            setFgColor( 200, 200, 200, 0 );
        Button::paint();
    }

    virtual void paintBackground()
    {
        if ( isArmed() )
        {
            // Opaque navy highlight behind the hovered label, as the stock armed fill.
            drawSetColor( 0, 0, 0x80, 0 );
            drawFilledRect( 0, 0, _size[0], _size[1] );
        }
        // Blue frame, as the stock buttons.
        drawSetColor( 0, 0, 255, 0 );
        drawOutlinedRect( 0, 0, _size[0], _size[1] );
    }
};

#endif	// SGTC_VGUI_BUTTON_H
