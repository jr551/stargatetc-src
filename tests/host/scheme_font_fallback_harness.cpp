// tests/host/scheme_font_fallback_harness.cpp - client scheme fonts on the host.
//
// Compiles the real cl_dll/vgui_SchemeManager.cpp against a stub engine table
// that serves files from a game directory (the StargateTC install). It checks
// that every scheme font measures text, and, with a corrupt sheet forced for one
// scheme, that CSchemeManager falls back to the named system font instead of
// leaving a Font with no glyph source (the signal 11 crash in Font::getTextSize).
//
//   scheme_font_fallback_harness <game dir> <xres>                 all schemes, real sheets
//   scheme_font_fallback_harness <game dir> <xres> <scheme name>   corrupt that scheme's sheet
//
// <xres> selects the sheet tier (640, 800, 1024, 1152, 1280, 1600). The valve
// package ships 640/800/1152 sheets as uncompressed TGA (image type 2), which
// the loader rejects, so those tiers exercise the fallback with real data.
//
// Output: one `CASE <name> PASS|FAIL <detail>` line per check, then SUMMARY.
// Exit nonzero on any failure. Engine-side strings the client logs are echoed
// so the fallback message can be checked.
//
// Build (host only, no engine):
//   c++ -std=c++11 -O1 -g -Dstricmp=strcasecmp -Icl_dll -Icl_dll/hl -Idlls \
//       -Icommon -Iengine -Ipublic -Igame_shared -Ipm_shared -Iutils/vgui/include \
//       -Ifreevgui -Ifreevgui/include -Iexternal -o scheme_font_fallback \
//       tests/host/scheme_font_fallback_harness.cpp cl_dll/vgui_SchemeManager.cpp \
//       freevgui/font.cpp freevgui/fileimage.cpp freevgui/platform/common/font.cpp
// -Dstricmp=strcasecmp is needed only on POSIX hosts (the client uses stricmp).

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hud.h"
#include "vgui_SchemeManager.h"
#include "platform/common/font.h"

// ---------------------------------------------------------------------------
// Engine table
// ---------------------------------------------------------------------------
cl_enginefunc_t gEngfuncs;

static const char *s_gameDir;
static const char *s_corruptScheme;	// NULL = no corruption
static int s_corruptHits;
static int s_printedUnreadable;
static char s_lastPrint[512];

static void stub_print( const char *fmt, ... )
{
	char buf[512];
	va_list ap;
	va_start( ap, fmt );
	vsnprintf( buf, sizeof( buf ), fmt, ap );
	va_end( ap );
	strncpy( s_lastPrint, buf, sizeof( s_lastPrint ) - 1 );
	s_lastPrint[sizeof( s_lastPrint ) - 1] = '\0';
	if( strstr( buf, "is unreadable, using system font" ) )
		s_printedUnreadable++;
	fputs( buf, stdout );
}

static void stub_dprint( const char *, ... )
{
}

static byte *stub_com_loadfile( const char *path, int, int *len )
{
	// A corrupt sheet: right name, bytes that are not a 32-bit RLE TGA.
	if( s_corruptScheme && !strncmp( path, "gfx/vgui/fonts/", 15 ) &&
		strstr( path, s_corruptScheme ) )
	{
		static const char junk[] = "corrupt sheet for harness";
		char *data = (char *)malloc( sizeof( junk ) );
		memcpy( data, junk, sizeof( junk ) );
		if( len ) *len = (int)sizeof( junk );
		s_corruptHits++;
		return (byte *)data;
	}

	char full[1024];
	snprintf( full, sizeof( full ), "%s/%s", s_gameDir, path );
	FILE *f = fopen( full, "rb" );
	if( !f )
	{
		// The client falls back to the mod's shipped valve dir as well.
		snprintf( full, sizeof( full ), "%s/../valve/%s", s_gameDir, path );
		f = fopen( full, "rb" );
	}
	if( !f )
	{
		if( len ) *len = -1;
		return NULL;
	}
	fseek( f, 0, SEEK_END );
	long n = ftell( f );
	fseek( f, 0, SEEK_SET );
	char *data = (char *)malloc( n + 1 );
	fread( data, 1, n, f );
	data[n] = '\0';
	fclose( f );
	if( len ) *len = (int)n;
	return (byte *)data;
}

static void stub_com_freefile( void *p )
{
	free( p );
}

// Tokeniser with the engine's rules: // comments, quoted strings, single
// punctuation characters, and whitespace-separated words.
static char *stub_com_parsefile( const char *data, const char *token_in )
{
	char *token = (char *)token_in;
	if( !data )
		return NULL;
	token[0] = '\0';
	const char *p = data;
	for(;;)
	{
		while( *p && (unsigned char)*p <= ' ' ) p++;
		if( !*p ) return NULL;
		if( p[0] == '/' && p[1] == '/' )
		{
			while( *p && *p != '\n' ) p++;
			continue;
		}
		break;
	}
	int n = 0;
	if( *p == '"' )
	{
		p++;
		while( *p && *p != '"' && n < 1023 ) token[n++] = *p++;
		if( *p == '"' ) p++;
	}
	else if( *p == '{' || *p == '}' || *p == '(' || *p == ')' || *p == '=' )
	{
		token[n++] = *p++;
	}
	else
	{
		while( *p && (unsigned char)*p > ' ' && *p != '{' && *p != '}' && *p != '"' && n < 1023 )
			token[n++] = *p++;
	}
	token[n] = '\0';
	return (char *)p;
}

static cvar_t s_bitmapFontsCvar;

static cvar_t *stub_register_variable( const char *name, const char *value, int flags )
{
	(void)name;
	(void)flags;
	s_bitmapFontsCvar.name = (char *)name;
	s_bitmapFontsCvar.string = (char *)value;
	s_bitmapFontsCvar.value = (float)atof( value );
	return &s_bitmapFontsCvar;
}

// ---------------------------------------------------------------------------
// Checks
// ---------------------------------------------------------------------------
namespace vgui
{
	// Host stand-ins for the freevgui platform hooks. No system font exists on
	// the host, so named fonts take the StubFontPlat path, like a missing font.
	char *vgui_strdup( const char *s )
	{
		char *d = new char[strlen( s ) + 1];
		strcpy( d, s );
		return d;
	}

	BaseFontPlat *FontPlat_CreateSystem( const char *, int, int, float, int, bool, bool, bool, bool )
	{
		return nullptr;
	}
}

static int s_failures;

static void report( const char *name, bool ok, const char *detail )
{
	printf( "CASE %-34s %s %s\n", name, ok ? "PASS" : "FAIL", detail );
	if( !ok )
		s_failures++;
}

static const char *s_schemes[] = {
	"Primary Button Text", "CommandMenu Text", "Scoreboard Text",
	"Scoreboard Title Text", "Scoreboard Small Text", "Briefing Text",
	"Team Info Text", "Title Font",
};

int main( int argc, char **argv )
{
	if( argc < 3 )
	{
		fprintf( stderr, "usage: %s <game dir> <xres> [corrupt scheme name]\n", argv[0] );
		return 2;
	}
	s_gameDir = argv[1];
	const int xRes = atoi( argv[2] );
	s_corruptScheme = argc > 3 ? argv[3] : NULL;

	memset( &gEngfuncs, 0, sizeof( gEngfuncs ) );
	gEngfuncs.pfnRegisterVariable = stub_register_variable;
	gEngfuncs.COM_LoadFile = stub_com_loadfile;
	gEngfuncs.COM_FreeFile = stub_com_freefile;
	gEngfuncs.COM_ParseFile = stub_com_parsefile;
	gEngfuncs.Con_Printf = stub_print;
	gEngfuncs.Con_DPrintf = stub_dprint;

	Scheme_Init();

	CSchemeManager *mgr = new CSchemeManager( xRes, xRes * 10 / 16 );

	for( size_t i = 0; i < sizeof( s_schemes ) / sizeof( s_schemes[0] ); i++ )
	{
		const char *name = s_schemes[i];
		SchemeHandle_t handle = mgr->getSchemeHandle( name );
		vgui::Font *font = mgr->getFont( handle );
		char detail[160];

		if( !font )
		{
			snprintf( detail, sizeof( detail ), "no font for scheme" );
			report( name, false, detail );
			continue;
		}

		// The crash happens here: measuring text through the font.
		int w = -1, h = -1;
		font->getTextSize( "Scoreboard", w, h );
		const bool corrupt = s_corruptScheme && !strcmp( name, s_corruptScheme );
		snprintf( detail, sizeof( detail ), "plat=%s size=%dx%d", font->getPlat() ? "set" : "NULL", w, h );
		report( corrupt ? "corrupt-scheme-measures" : name,
			font->getPlat() != NULL && w > 0 && h > 0, detail );
	}

	if( s_corruptScheme )
	{
		report( "corrupt-sheet-served", s_corruptHits > 0,
			s_corruptHits ? "harness served the corrupt sheet" : "sheet never requested" );
		report( "fallback-logged", s_printedUnreadable > 0,
			s_printedUnreadable ? "client logged the fallback" : "no fallback message" );
	}

	if( !s_corruptScheme )
	{
		// Real sheets, including the valve 640/800/1152 ones that are stored as
		// uncompressed TGA: every sheet must load with no fallback message.
		report( "real-sheets-load-no-fallback", s_printedUnreadable == 0,
			s_printedUnreadable ? "a real sheet fell back to the system font" : "all sheets read" );
	}

	delete mgr;
	printf( "SUMMARY %s (%d failure%s)\n", s_failures ? "FAIL" : "PASS",
		s_failures, s_failures == 1 ? "" : "s" );
	return s_failures ? 1 : 0;
}
