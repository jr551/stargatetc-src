//========= Copyright (c) 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================
#pragma once
#ifndef VGUI_HELPERS_H
#define VGUI_HELPERS_H

#include "VGUI_Panel.h"
#include "VGUI_Label.h"
#include "VGUI_SurfaceBase.h"

inline int PanelTop( vgui::Panel *pPanel )
{
	int x, y, w, h;

	pPanel->getBounds( x, y, w, h );

	return y;
}

inline int PanelLeft( vgui::Panel *pPanel )
{
	int x, y, w, h;

	pPanel->getBounds( x, y, w, h );

	return x;
}

inline int PanelRight( vgui::Panel *pPanel )
{
	int x, y, w, h;

	pPanel->getBounds( x, y, w, h );

	return x + w;
}

inline int PanelBottom( vgui::Panel *pPanel )
{
	int x, y, w, h;

	pPanel->getBounds( x, y, w, h );

	return y + h;
}

inline int PanelWidth( vgui::Panel *pPanel )
{
	int x, y, w, h;

	pPanel->getBounds( x, y, w, h );

	return w;
}

inline int PanelHeight( vgui::Panel *pPanel )
{
	int x, y, w, h;

	pPanel->getBounds( x, y, w, h );

	return h;
}

// ROW-343: when the engine binds the external vgui.so support module, this
// client's statically linked freevgui App never receives surfaceBaseCreated,
// so App::getCursorPos would deref surfaces[0]==NULL and SIGSEGV. Query the
// panel's own live surface instead - the same SurfaceBase App::surfaces[0]
// would have held. Falls back to (0,0) if the panel is not parented yet.
inline void SGTC_VGuiGetCursorPos( vgui::Panel *pPanel, int &x, int &y )
{
	vgui::SurfaceBase *pSurface = pPanel ? pPanel->getSurfaceBase() : NULL;
	if( pSurface )
		pSurface->GetMousePos( x, y );
	else
	{
		x = 0;
		y = 0;
	}
}

// Places child at the requested position inside pParent. iAlignment is from Label::Alignment.
void AlignPanel( vgui::Panel *pChild, vgui::Panel *pParent, int alignment );

#endif // VGUI_HELPERS_H

