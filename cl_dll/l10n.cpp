// l10n.cpp — client-side per-language titles overlay.
//
// Loads resource/l10n/<cl_language>/titles.txt and answers text lookups
// that the client DLL would otherwise take straight from the engine's
// titles.txt. See l10n.h for the format and the lookup order.

#include <stdlib.h>
#include <string.h>

#include "cl_dll.h"
#include "cvardef.h"
#include "l10n.h"

// Table limits. An overlay is untrusted input (it may be a third-party
// translation), so everything is bounded: a name line is truncated to
// L10N_MAX_KEY - 1 bytes, one text block to L10N_MAX_TEXT - 1 bytes (later
// lines of an over-long block are dropped, not wrapped), and the table holds
// L10N_MAX_ENTRIES keys. Exceeding the entry count discards the whole overlay
// (the client falls back to English) rather than silently dropping entries;
// length overruns are truncated and reported once per load.
// tests/features/test_l10n_parser.py pins these numbers.
#define L10N_MAX_ENTRIES	1024
#define L10N_MAX_KEY		256
#define L10N_MAX_TEXT		2048
#define L10N_PATH_SIZE		256
#define L10N_LANG_SIZE		64
#define L10N_LEGACY_BUFFERS	4

typedef struct
{
	char	*key;
	char	*text;
} l10n_entry_t;

static cvar_t		*s_languageCvar;
static l10n_entry_t	s_entries[L10N_MAX_ENTRIES];
static int		s_numEntries;
static char		s_language[L10N_LANG_SIZE] = "english";

static int L10n_StriCmp( const char *a, const char *b )
{
	while( *a && *b )
	{
		int ca = (unsigned char)*a;
		int cb = (unsigned char)*b;

		if( ca >= 'A' && ca <= 'Z' )
			ca += 'a' - 'A';
		if( cb >= 'A' && cb <= 'Z' )
			cb += 'a' - 'A';
		if( ca != cb )
			return ca - cb;

		a++;
		b++;
	}

	return (unsigned char)*a - (unsigned char)*b;
}

static char *L10n_Strdup( const char *src )
{
	size_t	len = strlen( src ) + 1;
	char	*dst = (char *)malloc( len );

	if( dst )
		memcpy( dst, src, len );

	return dst;
}

static void L10n_FreeEntries( void )
{
	int	i;

	for( i = 0; i < s_numEntries; i++ )
	{
		free( s_entries[i].key );
		free( s_entries[i].text );
	}

	memset( s_entries, 0, sizeof( s_entries ) );
	s_numEntries = 0;
}

// A language name becomes a directory below resource/l10n, so keep it to
// characters that cannot escape that directory.
static int L10n_ValidLanguage( const char *lang )
{
	const char	*p;

	if( !lang[0] )
		return 0;

	for( p = lang; *p; p++ )
	{
		char c = *p;

		if( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) ||
			( c >= '0' && c <= '9' ) || c == '_' || c == '-' || c == '.' )
			continue;

		return 0;
	}

	if( !strcmp( lang, "." ) || !strcmp( lang, ".." ) )
		return 0;

	return 1;
}

static void L10n_Trim( const char *line, int length, int *start, int *end )
{
	int	s = 0;
	int	e = length;

	while( s < e && (unsigned char)line[s] <= ' ' )
		s++;
	while( e > s && (unsigned char)line[e - 1] <= ' ' )
		e--;

	*start = s;
	*end = e;
}

static char	s_legacyBuffers[L10N_LEGACY_BUFFERS][L10N_MAX_TEXT];
static int	s_legacyIndex;

// CP1252 bytes 0x80..0x9F, as Unicode code points (0 = unmapped).
static int L10n_Cp1252High( int index )
{
	static const int high[32] =
	{
		0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
		0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
		0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
		0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178
	};

	return high[index];
}

static int L10n_ToCp1252( int cp )
{
	int	i;

	if( cp < 0x80 )
		return cp;

	// Latin-1 maps one-to-one onto CP1252 above 0x9F. Bytes 0x80..0x9F
	// stay as-is too: they are pass-through codes in the legacy font
	// sheet and no Unicode text should encode controls there anyway.
	if( cp <= 0xFF )
		return cp;

	for( i = 0; i < 32; i++ )
	{
		if( L10n_Cp1252High( i ) == cp )
			return 0x80 + i;
	}

	return '?';
}

// Decodes one UTF-8 sequence. Returns the number of bytes consumed and
// stores the code point, or -1 for an invalid sequence.
static int L10n_Utf8Decode( const unsigned char *s, int *cp )
{
	int	c = s[0];
	int	need;
	int	value;
	int	i;

	if( c < 0x80 )
	{
		*cp = c;
		return 1;
	}

	if( c >= 0xC2 && c <= 0xDF )
	{
		need = 1;
		value = c & 0x1F;
	}
	else if( c >= 0xE0 && c <= 0xEF )
	{
		need = 2;
		value = c & 0x0F;
	}
	else if( c >= 0xF0 && c <= 0xF4 )
	{
		need = 3;
		value = c & 0x07;
	}
	else
	{
		*cp = -1;
		return 1;
	}

	for( i = 1; i <= need; i++ )
	{
		if( ( s[i] & 0xC0 ) != 0x80 )
		{
			*cp = -1;
			return i;
		}

		value = ( value << 6 ) | ( s[i] & 0x3F );
	}

	*cp = value;
	return need + 1;
}

const char *L10n_ToLegacy( const char *utf8 )
{
	const unsigned char	*src;
	char			*dst;
	int			out = 0;

	if( !utf8 )
		return "";

	dst = s_legacyBuffers[s_legacyIndex];
	s_legacyIndex = ( s_legacyIndex + 1 ) % L10N_LEGACY_BUFFERS;

	src = (const unsigned char *)utf8;

	while( *src && out < L10N_MAX_TEXT - 1 )
	{
		int	cp = -1;
		int	used;
		int	k;

		used = L10n_Utf8Decode( src, &cp );

		if( cp < 0 )
		{
			// Not UTF-8: copy the bytes through so already-legacy text
			// (and the pre-conversion English titles.txt) still renders.
			for( k = 0; k < used && out < L10N_MAX_TEXT - 1; k++ )
				dst[out++] = (char)src[k];
		}
		else
		{
			dst[out++] = (char)( cp == 0 ? '?' : L10n_ToCp1252( cp ) );
		}

		src += used;
	}

	dst[out] = 0;
	return dst;
}

static int L10n_AddEntry( const char *key, const char *text )
{
	char	*keyCopy;
	char	*textCopy;
	int	i;

	// Later definitions of the same key win, like a file override would.
	for( i = 0; i < s_numEntries; i++ )
	{
		if( !L10n_StriCmp( s_entries[i].key, key ) )
		{
			textCopy = L10n_Strdup( text );
			if( !textCopy )
				return 0;

			free( s_entries[i].text );
			s_entries[i].text = textCopy;
			return 1;
		}
	}

	if( s_numEntries >= L10N_MAX_ENTRIES )
		return 0;	// caller discards the overlay (see L10N_MAX_ENTRIES)

	keyCopy = L10n_Strdup( key );
	textCopy = L10n_Strdup( text );

	if( !keyCopy || !textCopy )
	{
		free( keyCopy );
		free( textCopy );
		return 0;
	}

	s_entries[s_numEntries].key = keyCopy;
	s_entries[s_numEntries].text = textCopy;
	s_numEntries++;

	return 1;
}

// Appends one raw text line plus its newline, honouring the text cap.
// Returns 0 when the line had to be truncated.
static int L10n_AppendTextLine( char *text, int *textLength, const char *line, int length )
{
	int	room = L10N_MAX_TEXT - 1 - *textLength;
	int	copied = length;
	int	truncated = 0;

	if( copied + 1 > room )
	{
		copied = room - 1;
		truncated = 1;
	}

	if( copied < 0 )
		copied = 0;

	if( copied > 0 )
	{
		memcpy( text + *textLength, line, copied );
		*textLength += copied;
	}

	if( *textLength < L10N_MAX_TEXT - 1 )
	{
		text[(*textLength)++] = '\n';
		text[*textLength] = 0;
	}
	else
	{
		truncated = 1;
	}

	return !truncated;
}

// Parses an overlay in place. Returns 1 on success, 0 when the file is
// malformed (the caller discards anything parsed so far).
static int L10n_ParseOverlay( const char *path, const char *data, int length, int *truncated )
{
	char	name[L10N_MAX_KEY];
	char	text[L10N_MAX_TEXT];
	int	textLength = 0;
	int	haveName = 0;
	int	inText = 0;
	int	pos = 0;
	int	lineNum = 0;

	name[0] = 0;
	text[0] = 0;

	// Accept a UTF-8 BOM before the first name.
	if( length >= 3 && (unsigned char)data[0] == 0xEF &&
		(unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF )
	{
		pos = 3;
	}

	while( pos < length )
	{
		int	lineStart = pos;
		int	lineEnd = pos;
		int	contentEnd;
		int	next;
		int	ts, te;

		while( lineEnd < length && data[lineEnd] != '\n' )
			lineEnd++;

		next = ( lineEnd < length ) ? lineEnd + 1 : length;
		lineNum++;

		contentEnd = lineEnd;
		if( contentEnd > lineStart && data[contentEnd - 1] == '\r' )
			contentEnd--;

		L10n_Trim( data + lineStart, contentEnd - lineStart, &ts, &te );

		if( !inText )
		{
			if( ts == te )
			{
				// blank line
			}
			else if( data[lineStart + ts] == '/' && ts + 1 < te && data[lineStart + ts + 1] == '/' )
			{
				// comment
			}
			else if( data[lineStart + ts] == '$' )
			{
				// Engine directive; an overlay holds text only.
			}
			else if( data[lineStart + ts] == '{' )
			{
				if( !haveName )
				{
					gEngfuncs.Con_DPrintf( "L10n: %s: text block without a name at line %d, overlay discarded\n", path, lineNum );
					return 0;
				}

				inText = 1;
				textLength = 0;
				text[0] = 0;
			}
			else if( data[lineStart + ts] == '}' )
			{
				gEngfuncs.Con_DPrintf( "L10n: %s: unexpected '}' at line %d, overlay discarded\n", path, lineNum );
				return 0;
			}
			else
			{
				int	nameLen = te - ts;

				if( nameLen >= L10N_MAX_KEY )
				{
					nameLen = L10N_MAX_KEY - 1;
					*truncated = 1;
				}

				memcpy( name, data + lineStart + ts, nameLen );
				name[nameLen] = 0;
				haveName = 1;
			}
		}
		else
		{
			if( ts < te && data[lineStart + ts] == '{' )
			{
				gEngfuncs.Con_DPrintf( "L10n: %s: unexpected '{' at line %d, overlay discarded\n", path, lineNum );
				return 0;
			}
			else if( ts < te && data[lineStart + ts] == '}' )
			{
				// Drop the newline that precedes the closing brace; the
				// engine excludes it from the title text too.
				if( textLength > 0 && text[textLength - 1] == '\n' )
					text[--textLength] = 0;

				if( !L10n_AddEntry( name, text ) )
				{
					gEngfuncs.Con_DPrintf( "L10n: %s: too many entries or out of memory at line %d\n", path, lineNum );
					return 0;
				}

				inText = 0;
				haveName = 0;
				name[0] = 0;
				textLength = 0;
				text[0] = 0;
			}
			else
			{
				if( !L10n_AppendTextLine( text, &textLength, data + lineStart, contentEnd - lineStart ) )
					*truncated = 1;
			}
		}

		pos = next;
	}

	if( inText )
		gEngfuncs.Con_DPrintf( "L10n: %s: unterminated text block at end of file, ignored\n", path );

	return 1;
}

void L10n_Reload( void )
{
	char		path[L10N_PATH_SIZE];
	const char	*lang = "english";
	int		length = 0;
	char		*file;
	int		truncated = 0;

	// Drop the previous table first: every branch below either installs a new
	// one or leaves the overlay empty, so a failed or missing load can never
	// resurrect stale keys and no entry outlives its language
	// (tests/features/test_l10n_parser.py checks both).
	L10n_FreeEntries();

	if( s_languageCvar && s_languageCvar->string )
		lang = s_languageCvar->string;

	if( !lang[0] || !L10n_StriCmp( lang, "english" ) )
	{
		strlcpy( s_language, "english", sizeof( s_language ) );
		gEngfuncs.Con_DPrintf( "L10n: language 'english', 0 overlay keys (base titles.txt only)\n" );
		return;
	}

	if( !L10n_ValidLanguage( lang ) )
	{
		strlcpy( s_language, "english", sizeof( s_language ) );
		gEngfuncs.Con_DPrintf( "L10n: invalid cl_language '%s', 0 overlay keys (English fallback)\n", lang );
		return;
	}

	strlcpy( s_language, lang, sizeof( s_language ) );
	safe_snprintf( path, sizeof( path ), "resource/l10n/%s/titles.txt", s_language );

	file = (char *)gEngfuncs.COM_LoadFile( path, 5, &length );
	if( !file )
	{
		gEngfuncs.Con_DPrintf( "L10n: language '%s': %s not found, 0 overlay keys (English fallback)\n", s_language, path );
		return;
	}

	if( length <= 0 )
		length = (int)strlen( file );

	if( !L10n_ParseOverlay( path, file, length, &truncated ) )
		L10n_FreeEntries();

	gEngfuncs.COM_FreeFile( file );

	if( truncated )
		gEngfuncs.Con_DPrintf( "L10n: %s: overlay truncated to fit internal limits\n", path );

	gEngfuncs.Con_DPrintf( "L10n: language '%s': %d overlay keys from %s\n", s_language, s_numEntries, path );
}

static void L10n_Reload_f( void )
{
	L10n_Reload();
}

void L10n_Init( void )
{
	if( !s_languageCvar )
	{
		// cl_language: localisation language directory under resource/l10n.
		// The legacy client cvar API has no description field, so this
		// comment is the only place the help text can live.
		s_languageCvar = gEngfuncs.pfnRegisterVariable( "cl_language", "english", FCVAR_ARCHIVE );
		gEngfuncs.pfnAddCommand( "l10n_reload", L10n_Reload_f );
	}

	L10n_Reload();
}

const char *L10n_Lookup( const char *key )
{
	int	i;

	if( !key )
		return NULL;

	if( key[0] == '#' )
		key++;

	if( !key[0] )
		return NULL;

	for( i = 0; i < s_numEntries; i++ )
	{
		if( !L10n_StriCmp( s_entries[i].key, key ) )
			return s_entries[i].text;
	}

	return NULL;
}

const char *L10n_GetLanguage( void )
{
	return s_language;
}
