// tests/host/font_fallback_harness.cpp - host check for the scheme font fallback.
//
// freevgui's vgui::Font(name, pFileData, ...) loads a 32-bit RLE TGA sheet. When
// the sheet cannot be parsed, Font::init leaves impl NULL, and every later
// glyph/measure call dereferences it: signal 11 at a nil address inside
// Font::getTextSize (the crash logged from the HUD/command-menu Label init).
//
// cl_dll/vgui_SchemeManager.cpp handles this by deleting such a font and
// creating the named system font instead (vgui::Font's header promises that
// fallback). This harness links the real freevgui font code with the platform
// system-font factory stubbed to fail, and checks:
//   corrupt-sheet-no-glyphs   a corrupt sheet leaves getPlat() NULL
//   fallback-measures         the named-font fallback measures text
//   fallback-sheet-is-used    a valid sheet is kept (getPlat() set, no fallback)
// Prints one `CASE <name> PASS/FAIL` line per case; nonzero exit on failure.
//
// Build (no engine, no 32-bit toolchain):
//   c++ -std=c++11 -O1 -g -Ifreevgui -Ifreevgui/include -Iutils/vgui/include \
//       -Iexternal -o font_fallback tests/host/font_fallback_harness.cpp \
//       freevgui/font.cpp freevgui/fileimage.cpp freevgui/platform/common/font.cpp

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "platform/common/font.h"

namespace vgui
{
	char *vgui_strdup( const char *s )
	{
		char *d = new char[strlen( s ) + 1];
		strcpy( d, s );
		return d;
	}

	// No system font available: forces the StubFontPlat path in Font::init.
	BaseFontPlat *FontPlat_CreateSystem( const char *, int, int, float, int, bool, bool, bool, bool )
	{
		return nullptr;
	}
}

static int s_failures;

static void report( const char *name, bool ok, const char *detail )
{
	printf( "CASE %-26s %s %s\n", name, ok ? "PASS" : "FAIL", detail );
	if( !ok )
		s_failures++;
}

int main()
{
	const char corrupt[] = "not a 32-bit RLE tga";
	char detail[96];

	// 1) A corrupt sheet leaves the font without a glyph source.
	vgui::Font *bad = new vgui::Font( "Times New Roman", (void *)corrupt, (int)sizeof( corrupt ),
		15, 0, 0, 400, false, false, false, false );
	report( "corrupt-sheet-no-glyphs", bad->getPlat() == nullptr, bad->getPlat() ? "unexpected platform font" : "getPlat NULL" );

	// The client's fallback: drop the broken font and build the named font.
	delete bad;
	vgui::Font *fallback = new vgui::Font( "Times New Roman", nullptr, 0,
		15, 0, 0, 400, false, false, false, false );
	int w = -1, h = -1;
	fallback->getTextSize( "Scoreboard", w, h );
	snprintf( detail, sizeof( detail ), "plat=%s size=%dx%d", fallback->getPlat() ? "set" : "NULL", w, h );
	report( "fallback-measures", fallback->getPlat() != nullptr && w > 0 && h > 0, detail );
	delete fallback;

	// 2) A valid sheet is kept: no fallback needed.
	// Build a 1-cell-per-byte RLE sheet: 256 cells, 1 px wide, 2 px tall, opaque.
	// Each row is written as literal packets of up to 128 px (type 10, 32 bpp).
	unsigned char sheet[18 + 256 * 2 * 4 * 2 + 64];
	memset( sheet, 0, sizeof( sheet ) );
	const int width = 256, height = 2;
	sheet[2] = 10;							// image type: RLE truecolor
	sheet[12] = width & 0xff; sheet[13] = width >> 8;
	sheet[14] = height & 0xff; sheet[15] = height >> 8;
	sheet[16] = 32;							// pixel depth
	sheet[17] = 8;							// 8 alpha bits, bottom-left origin
	size_t pos = 18;
	for( int y = 0; y < height; y++ )
	{
		for( int x = 0; x < width; x += 128 )
		{
			sheet[pos++] = 127;				// 128 literal pixels
			for( int k = 0; k < 128; k++ )
			{
				sheet[pos++] = 255; sheet[pos++] = 255; sheet[pos++] = 255; sheet[pos++] = 255;
			}
		}
	}
	vgui::Font *good = new vgui::Font( "Times New Roman", (void *)sheet, (int)pos,
		15, 0, 0, 400, false, false, false, false );
	report( "fallback-sheet-is-used", good->getPlat() != nullptr, good->getPlat() ? "sheet loaded" : "valid sheet rejected" );
	delete good;

	printf( "SUMMARY %s (%d failure%s)\n", s_failures ? "FAIL" : "PASS", s_failures, s_failures == 1 ? "" : "s" );
	return s_failures ? 1 : 0;
}
