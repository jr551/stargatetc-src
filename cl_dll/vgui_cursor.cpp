// Engine-side cursor grab control for the FWGS freevgui build.
//
// The client's setCursorOveride() only takes effect when freevgui's internal
// tick resolves a mouse-focus change, and with the SDL cursor grabbed the
// engine reports a constant centre position — so the first ever focus change
// can never happen and VGUI panels never receive a working cursor. This calls
// the engine's vguiapi CursorSelect directly, which toggles
// host.mouse_visible / SDL grab and restores real mouse coordinates.
//
// Lives in its own TU because vgui_api.h injects global-scope MOUSE_* enum
// values that collide with VGUI_MouseCode.h in the HUD translation units.

#include "../engine/vgui_api.h"
#include <stdio.h>

// The freevgui platform's engine table, filled by InitVGUISupportAPI at load
// (INTERNAL_VGUI_SUPPORT build — the table lives in client.so, statically
// linked). NULL until the engine calls InitVGUISupportAPI.
//
// Only the statically linked FreeVGUI build defines that table
// (freevgui/platform/xash3d-fwgs/app.cpp). Win32 GoldSrc links the engine's
// vgui2.dll instead, which has no vguiapi_t and never calls
// InitVGUISupportAPI, so there is nothing to drive there.
#ifdef INTERNAL_VGUI_SUPPORT

namespace vgui { extern vguiapi_t *g_engine; }

void SGTC_VGuiSetCursor( VGUI_DefaultCursor cursor )
{
	if( vgui::g_engine && vgui::g_engine->CursorSelect )
		vgui::g_engine->CursorSelect( cursor );
}

#else

void SGTC_VGuiSetCursor( VGUI_DefaultCursor cursor )
{
	(void)cursor;
}

#endif // INTERNAL_VGUI_SUPPORT
