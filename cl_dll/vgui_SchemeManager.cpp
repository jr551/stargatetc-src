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

#include "hud.h"
#include "vgui_SchemeManager.h"
#include "vgui_font_tiers.h"
#include "cvardef.h"

#include <string.h>


cvar_t *g_CV_BitmapFonts;


void Scheme_Init()
{
	g_CV_BitmapFonts = gEngfuncs.pfnRegisterVariable("bitmapfonts", "1", 0);
}



//-----------------------------------------------------------------------------
// Purpose: Scheme managers data container
//-----------------------------------------------------------------------------
class CSchemeManager::CScheme
{
public:
	enum { 
		SCHEME_NAME_LENGTH = 32,
		FONT_NAME_LENGTH = 48,
		FONT_FILENAME_LENGTH = 64,
	};
	
	// name
	char schemeName[SCHEME_NAME_LENGTH];

	// font
	char fontName[FONT_NAME_LENGTH];

	int fontSize;
	int fontWeight;

	vgui::Font *font;
	int ownFontPointer; // true if the font is ours to delete

	// scheme
	byte fgColor[4];
	byte bgColor[4];
	byte armedFgColor[4];
	byte armedBgColor[4]; 
	byte mousedownFgColor[4];
	byte mousedownBgColor[4];
	byte borderColor[4];

	// construction/destruction
	CScheme();
	~CScheme();
};

CSchemeManager::CScheme::CScheme()
{
	schemeName[0] = 0;
	fontName[0] = 0;
	fontSize = 0;
	fontWeight = 0;
	font = NULL;
	ownFontPointer = false;
}

CSchemeManager::CScheme::~CScheme()
{
	// only delete our font pointer if we own it
	if ( ownFontPointer )
	{
		delete font;
	}
}

//-----------------------------------------------------------------------------
// Purpose: resolution information
//			!! needs to be shared out
//-----------------------------------------------------------------------------
static int g_ResArray[] =
{
	320,
	400,
	512,
	640,
	800,
	1024,
	1152,
	1280,
	1600
};
static int g_NumReses = sizeof(g_ResArray) / sizeof(int);

static byte *LoadFileByResolution( const char *filePrefix, int xRes, const char *filePostfix, char *loadedName = NULL, int loadedNameSize = 0 )
{
	// find our resolution in the res array
	int resNum = g_NumReses - 1;
	while ( g_ResArray[resNum] > xRes )
	{
		resNum--;

		if ( resNum < 0 )
			return NULL;
	}

	// try open the file
	byte *pFile = NULL;
	while ( 1 )
	{

		// try load
		char fname[256];
		snprintf( fname, sizeof( fname ), "%s%d%s", filePrefix, g_ResArray[resNum], filePostfix );
		pFile = gEngfuncs.COM_LoadFile( fname, 5, NULL );

		if ( pFile )
		{
			if ( loadedName && loadedNameSize > 0 )
				strlcpy( loadedName, fname, loadedNameSize );
			break;
		}

		if ( resNum == 0 )
			return NULL;

		resNum--;
	};

	return pFile;
}

static void ParseRGBAFromString( byte colorArray[4], const char *colorVector )
{
	int r, g, b, a;
	sscanf( colorVector, "%d %d %d %d", &r, &g, &b, &a );
	colorArray[0] = r;
	colorArray[1] = g;
	colorArray[2] = b;
	colorArray[3] = a;
}

//-----------------------------------------------------------------------------
// ClientScheme.res fallback (VGUI2 keyvalues)
//
// The stock VGUI1 path below reads "<res>_textscheme.txt": the ref client.dll
// carries exactly those strings ("_textscheme.txt" @0x100b4650, "Unable to
// find *_textscheme.txt" @0x100b4660) and no "ClientScheme" string at all
// (REA search_strings on the running client.dll, sha256 9ec27e22…). The mod
// ships its own scheme in the VGUI2 keyvalues format instead:
//     resource/ClientScheme.res  — Scheme { Colors / BaseSettings / Fonts / Borders }
// When the stock textscheme cannot be found (a mod tree without valve/'s
// 640..1600_textscheme.txt), the TFC scheme handles the panels ask for are
// built from ClientScheme.res so the panels keep the mod's colours/fonts
// instead of falling back to the vgui library defaults.
//-----------------------------------------------------------------------------

struct SgtcResColor
{
	char szName[32];
	byte rgba[4];
};

struct SgtcResFont
{
	char szName[32];
	char szFont[48];
	int iTall;
	int iWeight;
};

#define SGTC_RES_MAX_COLORS	64
#define SGTC_RES_MAX_FONTS	16
#define SGTC_RES_MAX_DEPTH	8

static SgtcResColor g_ResColors[SGTC_RES_MAX_COLORS];
static int g_iResColors = 0;
static SgtcResFont g_ResFonts[SGTC_RES_MAX_FONTS];
static int g_iResFonts = 0;

static SgtcResColor *Res_FindColor( const char *pszName )
{
	if ( !pszName )
		return NULL;

	for ( int i = 0; i < g_iResColors; i++ )
	{
		if ( !stricmp( g_ResColors[i].szName, pszName ) )
			return &g_ResColors[i];
	}
	return NULL;
}

static SgtcResFont *Res_FindFont( const char *pszName )
{
	if ( !pszName )
		return NULL;

	for ( int i = 0; i < g_iResFonts; i++ )
	{
		if ( !stricmp( g_ResFonts[i].szName, pszName ) )
			return &g_ResFonts[i];
	}
	return NULL;
}

// Minimal VGUI2 keyvalues reader: tokenises with the engine's COM_ParseFile
// (the same call the textscheme parser uses) and records the "Colors" and
// "Fonts" blocks. Returns true when at least one colour was read.
static bool ParseClientSchemeRes( void )
{
	char *pFile = (char *)gEngfuncs.COM_LoadFile( "resource/ClientScheme.res", 5, NULL );
	if ( !pFile )
		return false;

	char *pFileStart = pFile;
	char token[512];
	char szBlocks[SGTC_RES_MAX_DEPTH][40];
	memset( szBlocks, 0, sizeof( szBlocks ) );
	int iDepth = 0;
	int iFontIndex = -1;

	while ( ( pFile = gEngfuncs.COM_ParseFile( pFile, token ) ) != NULL && token[0] )
	{
		if ( !strcmp( token, "{" ) )
		{
			iDepth++;
			if ( iDepth >= SGTC_RES_MAX_DEPTH )
				break;
			continue;
		}
		if ( !strcmp( token, "}" ) )
		{
			if ( iDepth > 0 )
				iDepth--;
			if ( iFontIndex >= 0 && iDepth < 3 )
				iFontIndex = -1;
			continue;
		}

		// a name token: the next token is either a value or a "{" block start
		char szName[40];
		strlcpy( szName, token, sizeof( szName ) );

		pFile = gEngfuncs.COM_ParseFile( pFile, token );
		if ( !token[0] )
			break;

		if ( !strcmp( token, "{" ) )
		{
			// named block: remember it at the current depth
			if ( iDepth < SGTC_RES_MAX_DEPTH )
				strlcpy( szBlocks[iDepth], szName, sizeof( szBlocks[iDepth] ) );
			// "Fonts" -> "<FontName>" opens a font description
			if ( iDepth == 2 && !stricmp( szBlocks[1], "Fonts" ) && g_iResFonts < SGTC_RES_MAX_FONTS )
			{
				iFontIndex = g_iResFonts++;
				memset( &g_ResFonts[iFontIndex], 0, sizeof( g_ResFonts[iFontIndex] ) );
				strlcpy( g_ResFonts[iFontIndex].szName, szName, sizeof( g_ResFonts[iFontIndex].szName ) );
				strlcpy( g_ResFonts[iFontIndex].szFont, "Arial", sizeof( g_ResFonts[iFontIndex].szFont ) );
			}
			iDepth++;
			if ( iDepth >= SGTC_RES_MAX_DEPTH )
				break;
			continue;
		}

		// name/value pair
		if ( iDepth >= 2 && !stricmp( szBlocks[1], "Colors" ) && g_iResColors < SGTC_RES_MAX_COLORS )
		{
			SgtcResColor *pColor = &g_ResColors[g_iResColors];
			strlcpy( pColor->szName, szName, sizeof( pColor->szName ) );
			ParseRGBAFromString( pColor->rgba, token );
			g_iResColors++;
		}
		else if ( iFontIndex >= 0 && !stricmp( szName, "name" ) )
		{
			strlcpy( g_ResFonts[iFontIndex].szFont, token, sizeof( g_ResFonts[iFontIndex].szFont ) );
		}
		else if ( iFontIndex >= 0 && !stricmp( szName, "tall" ) )
		{
			g_ResFonts[iFontIndex].iTall = atoi( token );
		}
		else if ( iFontIndex >= 0 && !stricmp( szName, "weight" ) )
		{
			g_ResFonts[iFontIndex].iWeight = atoi( token );
		}
	}

	gEngfuncs.COM_FreeFile( pFileStart );
	return g_iResColors > 0;
}

// Applies a ClientScheme.res colour (when present) to a scheme's colour slot.
static void Res_ApplyColor( byte colorArray[4], const char *pszName, const byte *pDefault )
{
	SgtcResColor *pColor = Res_FindColor( pszName );
	memcpy( colorArray, pColor ? pColor->rgba : pDefault, 4 );
}

//-----------------------------------------------------------------------------
// Purpose: initializes the scheme manager
//			loading the scheme files for the current resolution
// Input  : xRes - 
//			yRes - dimensions of output window
//-----------------------------------------------------------------------------
CSchemeManager::CSchemeManager( int xRes, int yRes )
{
	// basic setup
	m_pSchemeList = NULL;
	m_iNumSchemes = 0;

	// find the closest matching scheme file to our resolution
	char token[1024];
	char textschemeFile[256];
	const char *schemeSource = "built-in default";
	textschemeFile[0] = 0;
	char *pFile = (char*)LoadFileByResolution( "", xRes, "_textscheme.txt", textschemeFile, sizeof( textschemeFile ) );
	m_xRes = xRes;

	char *pFileStart = pFile;
	
	byte *pFontData;
	int fontFileLength;
	char fontFilename[512];

	//
	// Read the scheme descriptions from the text file, into a temporary array
	// format is simply:
	// <paramName name> = <paramValue>
	//
	// a <paramName name> of "SchemeName" signals a new scheme is being described
	//

	const static int numTmpSchemes = 64;
	static CScheme tmpSchemes[numTmpSchemes];
	memset( tmpSchemes, 0, sizeof(tmpSchemes) );
	int currentScheme = -1;
	CScheme *pScheme = NULL;

	if ( !pFile )
	{
		gEngfuncs.Con_DPrintf( "Unable to find *_textscheme.txt\n");
		goto buildDefaultFont;
	}

	// record what has been entered so we can create defaults from the different values
	bool hasFgColor, hasBgColor, hasArmedFgColor, hasArmedBgColor, hasMouseDownFgColor, hasMouseDownBgColor;

	pFile = gEngfuncs.COM_ParseFile( pFile, token );
	while ( strlen(token) > 0 && (currentScheme < numTmpSchemes) )
	{
		// get the paramName name
		static const int tokenSize = 64;
		char paramName[tokenSize], paramValue[tokenSize];

		strlcpy( paramName, token, tokenSize );

		// get the '=' character
		pFile = gEngfuncs.COM_ParseFile( pFile, token );
		if ( stricmp( token, "=" ) )
		{
			if ( currentScheme < 0 )
			{
				gEngfuncs.Con_Printf( "error parsing font scheme text file at file start - expected '=', found '%s''\n", token );
			}
			else
			{
				gEngfuncs.Con_Printf( "error parsing font scheme text file at scheme '%s' - expected '=', found '%s''\n", tmpSchemes[currentScheme].schemeName, token );
			}
			break;
		}

		// get paramValue
		pFile = gEngfuncs.COM_ParseFile( pFile, token );
		strlcpy( paramValue, token, tokenSize );
		
		// is this a new scheme?
		if ( !stricmp(paramName, "SchemeName") )
		{
			// setup the defaults for the current scheme
			if ( pScheme )
			{
				// foreground color defaults (normal -> armed -> mouse down)
				if ( !hasFgColor )
				{
					pScheme->fgColor[0] = pScheme->fgColor[1] = pScheme->fgColor[2] = pScheme->fgColor[3] = 255;
				}
				if ( !hasArmedFgColor )
				{
					memcpy( pScheme->armedFgColor, pScheme->fgColor, sizeof(pScheme->armedFgColor) );
				}
				if ( !hasMouseDownFgColor )
				{
					memcpy( pScheme->mousedownFgColor, pScheme->armedFgColor, sizeof(pScheme->mousedownFgColor) );
				}

				// background color (normal -> armed -> mouse down)
				if ( !hasBgColor )
				{
					pScheme->bgColor[0] = pScheme->bgColor[1] = pScheme->bgColor[2] = pScheme->bgColor[3] = 0;
				}
				if ( !hasArmedBgColor )
				{
					memcpy( pScheme->armedBgColor, pScheme->bgColor, sizeof(pScheme->armedBgColor) );
				}
				if ( !hasMouseDownBgColor )
				{
					memcpy( pScheme->mousedownBgColor, pScheme->armedBgColor, sizeof(pScheme->mousedownBgColor) );
				}

				// font size
				if ( !pScheme->fontSize )
				{
					pScheme->fontSize = 17;
				}
				if ( !pScheme->fontName[0] )
				{
					strcpy( pScheme->fontName, "Arial" );
				}
			}

			// tmpSchemes is full: the loop bound only checks the index of the scheme
			// being filled, so another "SchemeName" here would write one past the end
			if ( currentScheme + 1 >= numTmpSchemes )
			{
				gEngfuncs.Con_Printf( "font scheme text file has more than %d schemes, ignoring the rest\n", numTmpSchemes );
				break;
			}

			// create the new scheme
			currentScheme++;
			pScheme = &tmpSchemes[currentScheme];
			hasFgColor = hasBgColor = hasArmedFgColor = hasArmedBgColor = hasMouseDownFgColor = hasMouseDownBgColor = false;

			strlcpy( pScheme->schemeName, paramValue, CScheme::SCHEME_NAME_LENGTH );
		}

		if ( !pScheme )
		{
			gEngfuncs.Con_Printf( "font scheme text file MUST start with a 'SchemeName'\n");
			break;
		}

		// pull the data out into the scheme
		if ( !stricmp(paramName, "FontName") )
		{
			strlcpy( pScheme->fontName, paramValue, CScheme::FONT_NAME_LENGTH );
		}
		else if ( !stricmp(paramName, "FontSize") )
		{
			pScheme->fontSize = atoi( paramValue );
		}
		else if ( !stricmp(paramName, "FontWeight") )
		{
			pScheme->fontWeight = atoi( paramValue );
		}
		else if ( !stricmp(paramName, "FgColor") )
		{
			ParseRGBAFromString( pScheme->fgColor, paramValue );
			hasFgColor = true;
		}
		else if ( !stricmp(paramName, "BgColor") )
		{
			ParseRGBAFromString( pScheme->bgColor, paramValue );
			hasBgColor = true;
		}
		else if ( !stricmp(paramName, "FgColorArmed") )
		{
			ParseRGBAFromString( pScheme->armedFgColor, paramValue );
			hasArmedFgColor = true;
		}	
		else if ( !stricmp(paramName, "BgColorArmed") )
		{
			ParseRGBAFromString( pScheme->armedBgColor, paramValue );
			hasArmedBgColor = true;
		}
		else if ( !stricmp(paramName, "FgColorMousedown") )
		{
			ParseRGBAFromString( pScheme->mousedownFgColor, paramValue );
			hasMouseDownFgColor = true;
		}
		else if ( !stricmp(paramName, "BgColorMousedown") )
		{
			ParseRGBAFromString( pScheme->mousedownBgColor, paramValue );
			hasMouseDownBgColor = true;
		}
		else if ( !stricmp(paramName, "BorderColor") )
		{
			ParseRGBAFromString( pScheme->borderColor, paramValue );
			hasMouseDownBgColor = true;
		}

		// get the new token last, so we now if the loop needs to be continued or not
		pFile = gEngfuncs.COM_ParseFile( pFile, token );
	}

	// free the file
	gEngfuncs.COM_FreeFile( pFileStart );

	if ( currentScheme >= 0 )
		schemeSource = textschemeFile;


buildDefaultFont:

	// No textscheme found: build the scheme list the panels ask for from the
	// mod's VGUI2 scheme (resource/ClientScheme.res, see the header comment on
	// ParseClientSchemeRes). Colours are stored in the file's convention
	// (alpha 255 = opaque) — the 255-a inversion below is applied to all
	// schemes at the end, exactly like the textscheme path.
	if ( currentScheme < 0 && ParseClientSchemeRes() )
	{
		struct SgtcResSchemeMap
		{
			const char *szHandle;	// TFC scheme handle the panels request
			const char *szFont;	// ClientScheme.res "Fonts" entry
			const char *szFg;	// ClientScheme.res "Colors" entry
			const char *szBg;
			const char *szArmedFg;
			const char *szArmedBg;
			const char *szBorder;
		};
		static const SgtcResSchemeMap sMap[] =
		{
			// buttons/menus: mod orange text, dark blue control background
			{ "Primary Button Text", "Default",         "BaseText",       "ControlBG",   "BrightBaseText", "SelectionBG", "BorderBright" },
			{ "CommandMenu Text",    "Default",         "BaseText",       "ControlBG",   "BrightBaseText", "SelectionBG", "BorderBright" },
			// scoreboard rows
			{ "Scoreboard Text",     "DefaultSmall",    "BaseText",       NULL, NULL, NULL, NULL },
			{ "Scoreboard Title Text", "ClassMenuTitleFont", "BaseText",  NULL, NULL, NULL, NULL },
			{ "Scoreboard Small Text", "DefaultVerySmall",   "BaseText",  NULL, NULL, NULL, NULL },
			// in-game panels
			{ "Title Font",          "ClassMenuTitleFont", "BaseText",    NULL, NULL, NULL, NULL },
			{ "Briefing Text",       "Default",         "LabelDimText",   "WindowBG", NULL, NULL, NULL },
			{ "Team Info Text",      "Default",         "BaseText",       "ControlBG", NULL, NULL, NULL },
		};
		const byte rgbaWhite[4] = { 255, 255, 255, 255 };	// textscheme default fg
		const byte rgbaNone[4] = { 0, 0, 0, 0 };		// textscheme default bg

		for ( int i = 0; i < (int)( sizeof( sMap ) / sizeof( sMap[0] ) ); i++ )
		{
			currentScheme++;
			pScheme = &tmpSchemes[currentScheme];
			strlcpy( pScheme->schemeName, sMap[i].szHandle, CScheme::SCHEME_NAME_LENGTH );

			SgtcResFont *pFont = Res_FindFont( sMap[i].szFont );
			if ( !pFont )
				pFont = Res_FindFont( "Default" );
			if ( pFont )
			{
				strlcpy( pScheme->fontName, pFont->szFont, CScheme::FONT_NAME_LENGTH );
				pScheme->fontSize = pFont->iTall;
				pScheme->fontWeight = pFont->iWeight;
			}
			else
			{
				strcpy( pScheme->fontName, "Arial" );
				pScheme->fontSize = 17;
			}

			Res_ApplyColor( pScheme->fgColor, sMap[i].szFg, rgbaWhite );
			Res_ApplyColor( pScheme->bgColor, sMap[i].szBg, rgbaNone );
			Res_ApplyColor( pScheme->armedFgColor, sMap[i].szArmedFg, pScheme->fgColor );
			Res_ApplyColor( pScheme->armedBgColor, sMap[i].szArmedBg, pScheme->bgColor );
			memcpy( pScheme->mousedownFgColor, pScheme->armedFgColor, sizeof( pScheme->mousedownFgColor ) );
			memcpy( pScheme->mousedownBgColor, pScheme->armedBgColor, sizeof( pScheme->mousedownBgColor ) );
			Res_ApplyColor( pScheme->borderColor, sMap[i].szBorder, rgbaNone );
		}

		schemeSource = "resource/ClientScheme.res";
	}

	// make sure we have at least 1 valid font
	if ( currentScheme < 0 )
	{
		currentScheme = 0;
		strcpy( tmpSchemes[0].schemeName, "Default Scheme" );
		strcpy( tmpSchemes[0].fontName, "Arial" );
		tmpSchemes[0].fontSize = 8;		// never hand 0 to vgui::Font's system-font path
		tmpSchemes[0].fontWeight = 400;		// defined weight; memset left it 0
		tmpSchemes[0].fgColor[0] = tmpSchemes[0].fgColor[1] = tmpSchemes[0].fgColor[2] = tmpSchemes[0].fgColor[3] = 255;
		tmpSchemes[0].armedFgColor[0] = tmpSchemes[0].armedFgColor[1] = tmpSchemes[0].armedFgColor[2] = tmpSchemes[0].armedFgColor[3] = 255;
		tmpSchemes[0].mousedownFgColor[0] = tmpSchemes[0].mousedownFgColor[1] = tmpSchemes[0].mousedownFgColor[2] = tmpSchemes[0].mousedownFgColor[3] = 255;
	}

	// A zero point size reaches FreeVGUI's system-font path, which does not
	// survive it. The last textscheme block is never finalised and
	// ClientScheme.res may lack a usable Fonts entry, so replace unset sizes.
	// Shipped textschemes legitimately use 6 and 7 point sizes, so only 0
	// (unset, or atoi of a malformed value) is replaced.
	for ( int i = 0; i <= currentScheme; i++ )
	{
		if ( tmpSchemes[i].fontSize <= 0 )
			tmpSchemes[i].fontSize = 8;
	}

	// we have the full list of schemes in the tmpSchemes array
	// now allocate the correct sized list
	m_iNumSchemes = currentScheme + 1; // 0-based index

	gEngfuncs.Con_DPrintf( "Scheme: %d schemes from %s\n", m_iNumSchemes, schemeSource );

	m_pSchemeList = new CScheme[ m_iNumSchemes ];

	// copy in the data
	memcpy( m_pSchemeList, tmpSchemes, sizeof(CScheme) * m_iNumSchemes );

	// Bitmap sheets are per scheme name, so schemes that share a font
	// name/size/weight still need their own sheet. Sharing here gave every
	// "Default" scheme the sheet of whichever one came first (Primary Button
	// Text), which is wider than Briefing Text's and kept clipping the class
	// info panel after the tier fix (ROW-300). Only the system-font path may
	// share one vgui::Font.
	const bool bBitmapFontSheets = g_CV_BitmapFonts && g_CV_BitmapFonts->value != 0;

	// create the fonts
	for ( int i = 0; i < m_iNumSchemes; i++ )
	{
		m_pSchemeList[i].font = NULL;

		// see if the current font values exist in a previously loaded font
		if ( !bBitmapFontSheets )
		{
			for ( int j = 0; j < i; j++ )
			{
				// check if the font name, size, and weight are the same
				if ( !stricmp(m_pSchemeList[i].fontName, m_pSchemeList[j].fontName)  
					&& m_pSchemeList[i].fontSize == m_pSchemeList[j].fontSize
					&& m_pSchemeList[i].fontWeight == m_pSchemeList[j].fontWeight )
				{
					// copy the pointer, but mark i as not owning it
					m_pSchemeList[i].font = m_pSchemeList[j].font;
					m_pSchemeList[i].ownFontPointer = false;
				}
			}
		}

		// if we haven't found the font already, load it ourselves
		if ( !m_pSchemeList[i].font )
		{
			fontFileLength = -1;
			pFontData = NULL;

			if(bBitmapFontSheets)
			{
				// The mod ships bitmap VGUI fonts as gfx/vgui/fonts/
				// <res>_<scheme>.tga in the game directory (mod gamedir first,
				// then valve). The orig's VGUI.DLL does *not* load these TGAs
				// (it falls back to the system font at the scheme's point
				// size), while FreeVGUI does load them, so our glyphs are the
				// TGA's fixed cell size: the <res> prefix is the entire virtual
				// to device pixel mapping. Use the sheet for the screen we are
				// running on (vgui_font_tiers.h, stock VGUI2 order). Stepping a
				// tier up (a 20 px em for Briefing Text at 1024x768, against the
				// ref's 15 px Times New Roman) padded the Title Font but put
				// the longest class-info line 35 px past the ref-sized menu-11
				// panel — ROW-300, tests/test_menu11_font_metrics.py.
				const int fontRes = Sgtc_FontTierForWidth( m_xRes );

				snprintf(fontFilename, sizeof( fontFilename ), "gfx/vgui/fonts/%d_%s.tga", fontRes, m_pSchemeList[i].schemeName);
				pFontData = gEngfuncs.COM_LoadFile( fontFilename, 5, &fontFileLength );
				if(!pFontData)
					gEngfuncs.Con_Printf("Missing bitmap font: %s\n", fontFilename);
			}

			m_pSchemeList[i].font = new vgui::Font(
				m_pSchemeList[i].fontName, 
				pFontData,
				fontFileLength,
				m_pSchemeList[i].fontSize, 
				0, 
				0, 
				m_pSchemeList[i].fontWeight, 
				false, 
				false, 
				false, 
				false);
			
			m_pSchemeList[i].ownFontPointer = true;
		}

		// fix up alpha values; VGUI uses 1-A (A=0 being solid, A=255 transparent)
		m_pSchemeList[i].fgColor[3] = 255 - m_pSchemeList[i].fgColor[3];
		m_pSchemeList[i].bgColor[3] = 255 - m_pSchemeList[i].bgColor[3];
		m_pSchemeList[i].armedFgColor[3] = 255 - m_pSchemeList[i].armedFgColor[3];
		m_pSchemeList[i].armedBgColor[3] = 255 - m_pSchemeList[i].armedBgColor[3];
		m_pSchemeList[i].mousedownFgColor[3] = 255 - m_pSchemeList[i].mousedownFgColor[3];
		m_pSchemeList[i].mousedownBgColor[3] = 255 - m_pSchemeList[i].mousedownBgColor[3];
	}
}

//-----------------------------------------------------------------------------
// Purpose: frees all the memory used by the scheme manager
//-----------------------------------------------------------------------------
CSchemeManager::~CSchemeManager()
{
	delete [] m_pSchemeList;
	m_iNumSchemes = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Finds a scheme in the list, by name
// Input  : char *schemeName - string name of the scheme
// Output : SchemeHandle_t handle to the scheme
//-----------------------------------------------------------------------------
SchemeHandle_t CSchemeManager::getSchemeHandle( const char *schemeName )
{
	// iterate through the list
	for ( int i = 0; i < m_iNumSchemes; i++ )
	{
		if ( !stricmp(schemeName, m_pSchemeList[i].schemeName) )
			return i;
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: always returns a valid scheme handle
// Input  : schemeHandle - 
// Output : CScheme
//-----------------------------------------------------------------------------
CSchemeManager::CScheme *CSchemeManager::getSafeScheme( SchemeHandle_t schemeHandle )
{
	if ( schemeHandle < m_iNumSchemes )
		return m_pSchemeList + schemeHandle;

	return m_pSchemeList;
}


//-----------------------------------------------------------------------------
// Purpose: Returns the schemes pointer to a font
// Input  : schemeHandle - 
// Output : vgui::Font
//-----------------------------------------------------------------------------
vgui::Font *CSchemeManager::getFont( SchemeHandle_t schemeHandle )
{
	return getSafeScheme( schemeHandle )->font;
}

void CSchemeManager::getFgColor( SchemeHandle_t schemeHandle, int &r, int &g, int &b, int &a )
{
	CScheme *pScheme = getSafeScheme( schemeHandle );
	r = pScheme->fgColor[0];
	g = pScheme->fgColor[1];
	b = pScheme->fgColor[2];
	a = pScheme->fgColor[3];
}

void CSchemeManager::getBgColor( SchemeHandle_t schemeHandle, int &r, int &g, int &b, int &a )
{
	CScheme *pScheme = getSafeScheme( schemeHandle );
	r = pScheme->bgColor[0];
	g = pScheme->bgColor[1];
	b = pScheme->bgColor[2];
	a = pScheme->bgColor[3];
}

void CSchemeManager::getFgArmedColor( SchemeHandle_t schemeHandle, int &r, int &g, int &b, int &a )
{
	CScheme *pScheme = getSafeScheme( schemeHandle );
	r = pScheme->armedFgColor[0];
	g = pScheme->armedFgColor[1];
	b = pScheme->armedFgColor[2];
	a = pScheme->armedFgColor[3];
}

void CSchemeManager::getBgArmedColor( SchemeHandle_t schemeHandle, int &r, int &g, int &b, int &a )
{
	CScheme *pScheme = getSafeScheme( schemeHandle );
	r = pScheme->armedBgColor[0];
	g = pScheme->armedBgColor[1];
	b = pScheme->armedBgColor[2];
	a = pScheme->armedBgColor[3];
}

void CSchemeManager::getFgMousedownColor( SchemeHandle_t schemeHandle, int &r, int &g, int &b, int &a )
{
	CScheme *pScheme = getSafeScheme( schemeHandle );
	r = pScheme->mousedownFgColor[0];
	g = pScheme->mousedownFgColor[1];
	b = pScheme->mousedownFgColor[2];
	a = pScheme->mousedownFgColor[3];
}

void CSchemeManager::getBgMousedownColor( SchemeHandle_t schemeHandle, int &r, int &g, int &b, int &a )
{
	CScheme *pScheme = getSafeScheme( schemeHandle );
	r = pScheme->mousedownBgColor[0];
	g = pScheme->mousedownBgColor[1];
	b = pScheme->mousedownBgColor[2];
	a = pScheme->mousedownBgColor[3];
}

void CSchemeManager::getBorderColor( SchemeHandle_t schemeHandle, int &r, int &g, int &b, int &a )
{
	CScheme *pScheme = getSafeScheme( schemeHandle );
	r = pScheme->borderColor[0];
	g = pScheme->borderColor[1];
	b = pScheme->borderColor[2];
	a = pScheme->borderColor[3];
}



