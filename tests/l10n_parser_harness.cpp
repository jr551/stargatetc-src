// l10n_parser_harness.cpp - host-side harness for cl_dll/l10n.cpp.
//
// l10n.cpp reaches the engine only through gEngfuncs (cvar registration,
// command registration, COM_LoadFile/COM_FreeFile and Con_DPrintf), so it
// links against stubs and can be driven without the engine. This file writes
// its own overlays below resource/l10n/<lang>/ in the current directory, then
// checks the documented parser limits and the reload contract:
//
//   * a name line longer than L10N_MAX_KEY is truncated to 255 bytes;
//   * a text block longer than L10N_MAX_TEXT is truncated to 2047 bytes;
//   * L10N_MAX_ENTRIES (1024) entries load, one more discards the overlay;
//   * L10n_Reload drops the previous table before loading a new one.
//
// tests/features/test_l10n_parser.py compiles this with cl_dll/l10n.cpp and
// runs it in a temp directory (fast tier, no engine, no game data).

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cl_dll.h"
#include "cvardef.h"
#include "l10n.h"

cl_enginefunc_t gEngfuncs;

// l10n.h documents the table limits in prose; the harness pins the numbers so
// a silent change to the parser limits fails the test rather than the build.
#define EXPECT_MAX_KEY		256
#define EXPECT_MAX_TEXT		2048
#define EXPECT_MAX_ENTRIES	1024

static cvar_t	s_languageCvar;
static int	s_failures;
static char	s_log[16384];
static size_t	s_logLength;

// glibc before 2.38 has no strlcpy; cl_dll.h only declares it when CMake's
// HAVE_STRLCPY probe says the platform lacks it, and this build does not run
// that probe, so provide the BSD implementation unconditionally.
extern "C" size_t strlcpy( char *dst, const char *src, size_t dsize )
{
	size_t	len = strlen( src );

	if( dsize > 0 )
	{
		size_t	copy = ( len >= dsize ) ? dsize - 1 : len;

		memcpy( dst, src, copy );
		dst[copy] = 0;
	}

	return len;
}

static void LogReset( void )
{
	s_log[0] = 0;
	s_logLength = 0;
}

static void LogAppend( const char *text, size_t length )
{
	size_t	room = sizeof( s_log ) - 1 - s_logLength;

	if( length > room )
		length = room;

	memcpy( s_log + s_logLength, text, length );
	s_logLength += length;
	s_log[s_logLength] = 0;
}

static int LogContains( const char *needle )
{
	return strstr( s_log, needle ) != NULL;
}

static void Check( int condition, const char *what )
{
	if( condition )
		return;

	s_failures++;
	printf( "FAIL: %s\n", what );
}

static void CheckInt( int actual, int expected, const char *what )
{
	if( actual == expected )
		return;

	s_failures++;
	printf( "FAIL: %s (expected %d, got %d)\n", what, expected, actual );
}

// --- engine stubs -------------------------------------------------------

static cvar_t *StubRegisterVariable( const char *name, const char *value, int flags )
{
	s_languageCvar.name = name;
	s_languageCvar.string = value;
	s_languageCvar.flags = flags;
	s_languageCvar.value = 0.0f;
	s_languageCvar.next = NULL;

	return &s_languageCvar;
}

static int StubAddCommand( const char *name, void ( *function )( void ) )
{
	(void)name;
	(void)function;

	return 1;
}

static byte *StubLoadFile( const char *path, int usehunk, int *pLength )
{
	FILE	*file;
	long	size;
	byte	*buffer;

	(void)usehunk;

	file = fopen( path, "rb" );
	if( !file )
		return NULL;

	if( fseek( file, 0, SEEK_END ) != 0 )
	{
		fclose( file );
		return NULL;
	}

	size = ftell( file );
	if( size < 0 )
	{
		fclose( file );
		return NULL;
	}

	rewind( file );

	buffer = (byte *)malloc( (size_t)size + 1 );
	if( !buffer )
	{
		fclose( file );
		return NULL;
	}

	if( size > 0 && fread( buffer, 1, (size_t)size, file ) != (size_t)size )
	{
		free( buffer );
		fclose( file );
		return NULL;
	}

	fclose( file );

	buffer[size] = 0;

	if( pLength )
		*pLength = (int)size;

	return buffer;
}

static void StubFreeFile( void *buffer )
{
	free( buffer );
}

static void StubConDPrintf( const char *fmt, ... )
{
	char	line[1024];
	va_list	args;

	va_start( args, fmt );
	vsnprintf( line, sizeof( line ), fmt, args );
	va_end( args );

	LogAppend( line, strlen( line ) );
}

// --- helpers ------------------------------------------------------------

static void SetLanguage( const char *language )
{
	s_languageCvar.string = language;
	L10n_Reload();
}

static void MakeDirectory( const char *path )
{
	mkdir( path, 0777 );
}

static int WriteOverlay( const char *language, const char *data, size_t length )
{
	char	path[512];
	FILE	*file;

	snprintf( path, sizeof( path ), "resource/l10n/%s", language );
	MakeDirectory( path );

	snprintf( path, sizeof( path ), "resource/l10n/%s/titles.txt", language );

	file = fopen( path, "wb" );
	if( !file )
	{
		printf( "FAIL: cannot write %s\n", path );
		s_failures++;
		return 0;
	}

	if( length > 0 )
		fwrite( data, 1, length, file );

	fclose( file );

	return 1;
}

// Writes "name\n{\nvalue\n}\n" for one entry.
static int WriteSingleEntryOverlay( const char *language, const char *name, const char *value )
{
	char	*data;
	size_t	size = strlen( name ) + strlen( value ) + 8;
	int	ok;

	data = (char *)malloc( size );
	if( !data )
	{
		s_failures++;
		printf( "FAIL: out of memory\n" );
		return 0;
	}

	snprintf( data, size, "%s\n{\n%s\n}\n", name, value );
	ok = WriteOverlay( language, data, strlen( data ) );
	free( data );

	return ok;
}

static char *Repeat( char c, int count )
{
	char	*buffer = (char *)malloc( (size_t)count + 1 );
	int	i;

	if( !buffer )
		return NULL;

	for( i = 0; i < count; i++ )
		buffer[i] = c;

	buffer[count] = 0;

	return buffer;
}

static void WriteEntryCountOverlay( const char *language, int count )
{
	char	*data;
	size_t	size = (size_t)count * 32 + 64;
	size_t	used = 0;
	int	i;

	data = (char *)malloc( size );
	if( !data )
	{
		s_failures++;
		printf( "FAIL: out of memory\n" );
		return;
	}

	for( i = 0; i < count; i++ )
		used += (size_t)snprintf( data + used, size - used, "entry%d\n{\ntext\n}\n", i );

	WriteOverlay( language, data, used );
	free( data );
}

// --- cases --------------------------------------------------------------

static void TestKeyCap( void )
{
	char	*longName = Repeat( 'k', EXPECT_MAX_KEY + 44 );
	char	*storedName = Repeat( 'k', EXPECT_MAX_KEY - 1 );
	const char *text;

	if( !longName || !storedName )
	{
		s_failures++;
		printf( "FAIL: out of memory\n" );
		free( longName );
		free( storedName );
		return;
	}

	WriteSingleEntryOverlay( "keycap", longName, "value" );

	LogReset();
	SetLanguage( "keycap" );

	Check( LogContains( "overlay truncated" ), "key cap: truncation is reported" );

	text = L10n_Lookup( storedName );
	Check( text != NULL, "key cap: the 255-byte prefix is a lookup key" );
	Check( text && !strcmp( text, "value" ), "key cap: prefix maps to the entry text" );
	Check( L10n_Lookup( longName ) == NULL, "key cap: the untruncated name is not a key" );

	free( longName );
	free( storedName );
}

static void TestTextCap( void )
{
	char	*longValue = Repeat( 'v', 5000 );
	const char *text;

	if( !longValue )
	{
		s_failures++;
		printf( "FAIL: out of memory\n" );
		return;
	}

	WriteSingleEntryOverlay( "textcap", "bigvalue", longValue );

	LogReset();
	SetLanguage( "textcap" );

	Check( LogContains( "overlay truncated" ), "text cap: truncation is reported" );

	text = L10n_Lookup( "bigvalue" );
	Check( text != NULL, "text cap: the entry is still loaded" );
	if( text )
	{
		// L10N_MAX_TEXT - 1 bytes fit including the line's newline, and the
		// parser then drops that newline because it precedes the closing '}'.
		CheckInt( (int)strlen( text ), EXPECT_MAX_TEXT - 2, "text cap: value is capped at L10N_MAX_TEXT - 1 bytes" );
		Check( text[EXPECT_MAX_TEXT - 3] == 'v', "text cap: the surviving bytes are the head of the value" );
	}

	free( longValue );
}

static void TestEntryCount( void )
{
	LogReset();
	WriteEntryCountOverlay( "entryok", EXPECT_MAX_ENTRIES );
	SetLanguage( "entryok" );

	Check( L10n_Lookup( "entry0" ) != NULL, "entry cap: first entry loads at the limit" );
	Check( L10n_Lookup( "entry1023" ) != NULL, "entry cap: last entry loads at the limit" );
	Check( !LogContains( "too many entries" ), "entry count: no cap message at exactly L10N_MAX_ENTRIES" );

	LogReset();
	WriteEntryCountOverlay( "entryover", EXPECT_MAX_ENTRIES + 1 );
	SetLanguage( "entryover" );

	Check( L10n_Lookup( "entry0" ) == NULL, "entry cap: an over-limit overlay is discarded" );
	Check( LogContains( "too many entries" ), "entry cap: the over-limit overlay reports the cap" );
}

static void TestReloadDropsTable( void )
{
	WriteSingleEntryOverlay( "keep", "keepme", "kept" );

	LogReset();
	SetLanguage( "keep" );
	Check( L10n_Lookup( "#keepme" ) != NULL, "reload: previous overlay loads (and '#' is optional)" );

	LogReset();
	SetLanguage( "nosuchlanguage" );
	Check( L10n_Lookup( "keepme" ) == NULL, "reload: the previous table is dropped" );
	Check( !strcmp( L10n_GetLanguage(), "nosuchlanguage" ), "reload: language name follows cl_language" );
	Check( LogContains( "not found" ), "reload: a missing overlay falls back to English" );
}

static void TestInvalidLanguage( void )
{
	LogReset();
	SetLanguage( "../escaped" );

	Check( !strcmp( L10n_GetLanguage(), "english" ), "language guard: path-like names fall back to english" );
	Check( LogContains( "invalid cl_language" ), "language guard: the rejection is logged" );
}

int main( void )
{
	gEngfuncs.pfnRegisterVariable = StubRegisterVariable;
	gEngfuncs.pfnAddCommand = StubAddCommand;
	gEngfuncs.COM_LoadFile = StubLoadFile;
	gEngfuncs.COM_FreeFile = StubFreeFile;
	gEngfuncs.Con_DPrintf = StubConDPrintf;

	MakeDirectory( "resource" );
	MakeDirectory( "resource/l10n" );

	L10n_Init();

	TestKeyCap();
	TestTextCap();
	TestEntryCount();
	TestReloadDropsTable();
	TestInvalidLanguage();

	if( s_failures )
	{
		printf( "%d check(s) failed\n", s_failures );
		return 1;
	}

	printf( "l10n parser caps and reload behaviour: all checks passed\n" );
	return 0;
}
