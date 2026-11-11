// Bitmap VGUI font resolution ladder (ROW-300).
//
// FreeVGUI draws a scheme's text from gfx/vgui/fonts/<res>_<scheme>.tga, and a
// sheet's glyph cells are a fixed pixel size — so the <res> prefix is the whole
// mapping from virtual pixels to device pixels. Stock VGUI2 picks the sheet for
// the screen it runs on (640, 800, 1024, 1152, 1280, 1600). The port used to
// step one tier up (640->1024, 800->1280, >=1024->1600) to pad the Title Font,
// which left "Briefing Text" ~33% wider than the reference's Times New Roman at
// 1024x768 and clipped the tail of every long line in the ref-sized menu-11
// class info panel (XRES(209), sgtc_vgui_panels.cpp).
//
// This table is the single source for that choice; tests/test_menu11_font_metrics.py
// reads it to resolve the same sheet the panel uses.
#ifndef VGUI_FONT_TIERS_H
#define VGUI_FONT_TIERS_H

struct SgtcFontTier
{
	int iMinWidth;	// viewport width that starts using this sheet
	int iRes;		// <res>_ prefix of gfx/vgui/fonts/<res>_<scheme>.tga
};

// Ascending iMinWidth; a width below the first entry uses the first sheet and a
// width above the last uses the last.
static const SgtcFontTier s_SgtcFontTiers[] =
{
	{ 0,    640 },
	{ 800,  800 },
	{ 1024, 1024 },
	{ 1152, 1152 },
	{ 1280, 1280 },
	{ 1600, 1600 },
};

static inline int Sgtc_FontTierForWidth( int iWidth )
{
	const int iCount = (int)( sizeof( s_SgtcFontTiers ) / sizeof( s_SgtcFontTiers[0] ) );
	int iRes = s_SgtcFontTiers[0].iRes;

	for ( int i = 0; i < iCount; i++ )
	{
		if ( iWidth < s_SgtcFontTiers[i].iMinWidth )
			break;
		iRes = s_SgtcFontTiers[i].iRes;
	}

	return iRes;
}

#endif // VGUI_FONT_TIERS_H
