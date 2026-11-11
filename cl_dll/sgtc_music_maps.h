// sgtc_music_maps.h — parser for the mod's music/maps.txt track table.
//
// Deliberately free of engine dependencies: the client music manager
// (sgtc_music.cpp) hands it the bytes gEngfuncs.COM_LoadFile returned, and the
// host feature test (tests/features/test_music_maps.py) includes this header
// directly to exercise the parser without the engine.
//
// File format, one record per line:
//   <mapname> <slug>    mapname without "maps/" or ".bsp", slug without
//                       extension (the client prepends "music/")
//   # ...               comment: whole line or trailing
//
// Only the maps listed here get the mod's ambient track. A map without a line
// has no music of ours at all: the manager starts nothing and leaves whatever
// the engine, the server or the map itself is playing alone. There is no
// fallback: a `default <slug>` line is reserved and ignored (tools/check_music.py
// in the game-data repository rejects one outright).
//
// Blank lines and malformed lines are ignored. Duplicate map entries: the last
// one wins. Slugs are restricted to [A-Za-z0-9_-] so they can be pasted into a
// console command without quoting concerns.
//
// NEW file — sgtc_* only, no stock edits.
#pragma once

#include <string.h>

#define SGTC_MUSIC_MAX_ENTRIES 64
#define SGTC_MUSIC_NAME_LEN    48
#define SGTC_MUSIC_SLUG_LEN    48

struct sgtc_music_entry_t
{
	char map[SGTC_MUSIC_NAME_LEN];
	char slug[SGTC_MUSIC_SLUG_LEN];
};

struct sgtc_music_maps_t
{
	sgtc_music_entry_t entries[SGTC_MUSIC_MAX_ENTRIES];
	int count;
};

inline char SgtcMusic_LowerCase( char c )
{
	return ( c >= 'A' && c <= 'Z' ) ? (char)( c - 'A' + 'a' ) : c;
}

inline bool SgtcMusic_IsMapChar( char c )
{
	return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' )
		|| ( c >= '0' && c <= '9' ) || c == '_' || c == '-' || c == '.';
}

inline bool SgtcMusic_IsSlugChar( char c )
{
	return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' )
		|| ( c >= '0' && c <= '9' ) || c == '_' || c == '-';
}

inline bool SgtcMusic_IsValidSlug( const char *slug )
{
	if ( !slug || !slug[0] )
		return false;
	for ( const char *p = slug; *p; p++ )
	{
		if ( !SgtcMusic_IsSlugChar( *p ) )
			return false;
	}
	return true;
}

// "maps/sg_temple.bsp" -> "sg_temple". Accepts a bare name too. out is always
// NUL-terminated; an empty input (level not loaded yet) yields an empty string.
inline void SgtcMusic_NormalizeLevelName( const char *level, char *out, int outlen )
{
	if ( !out || outlen <= 0 )
		return;
	out[0] = '\0';
	if ( !level )
		return;

	const char *name = level;
	for ( const char *p = level; *p; p++ )
	{
		if ( *p == '/' || *p == '\\' )
			name = p + 1;
	}

	int n = 0;
	while ( n < outlen - 1 && name[n] )
	{
		out[n] = SgtcMusic_LowerCase( name[n] );
		n++;
	}
	out[n] = '\0';

	if ( n > 4 && !strcmp( out + n - 4, ".bsp" ) )
		out[n - 4] = '\0';
}

// Reads one whitespace-delimited token, stopping at whitespace or '#'.
// Returns false when the line has no (further) token.
inline bool SgtcMusic_ReadToken( const char **pp, char *out, int outlen, bool lower )
{
	const char *p = *pp;
	while ( *p == ' ' || *p == '\t' || *p == '\r' )
		p++;

	if ( !*p || *p == '\n' || *p == '#' )
	{
		*pp = p;
		return false;
	}

	int n = 0;
	bool overflow = false;
	while ( *p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '#' )
	{
		if ( n < outlen - 1 )
			out[n++] = lower ? SgtcMusic_LowerCase( *p ) : *p;
		else
			overflow = true;
		p++;
	}
	out[n] = '\0';
	*pp = p;
	return n > 0 && !overflow;
}

inline void SgtcMusic_SkipLine( const char **pp )
{
	const char *p = *pp;
	while ( *p && *p != '\n' )
		p++;
	if ( *p == '\n' )
		p++;
	*pp = p;
}

inline void SgtcMusic_ParseMaps( const char *text, sgtc_music_maps_t *maps )
{
	if ( !maps )
		return;

	maps->count = 0;
	if ( !text )
		return;

	const char *p = text;
	char map[SGTC_MUSIC_NAME_LEN];
	char slug[SGTC_MUSIC_SLUG_LEN];

	while ( *p )
	{
		// Blank lines and whole-line comments.
		while ( *p == ' ' || *p == '\t' || *p == '\r' )
			p++;
		if ( *p == '\n' )
		{
			p++;
			continue;
		}
		if ( *p == '#' )
		{
			SgtcMusic_SkipLine( &p );
			continue;
		}

		if ( !SgtcMusic_ReadToken( &p, map, sizeof( map ), true )
			|| !SgtcMusic_ReadToken( &p, slug, sizeof( slug ), false ) )
		{
			SgtcMusic_SkipLine( &p );	// malformed line
			continue;
		}

		// Anything after the second token is ignored (trailing comment).
		SgtcMusic_SkipLine( &p );

		if ( !SgtcMusic_IsValidSlug( slug ) )
			continue;

		// "default" is reserved: the table has no fallback, so the line is
		// dropped rather than turned into a map entry that could never match.
		if ( !strcmp( map, "default" ) )
			continue;

		bool valid_map = true;
		for ( const char *q = map; *q; q++ )
		{
			if ( !SgtcMusic_IsMapChar( *q ) )
			{
				valid_map = false;
				break;
			}
		}
		if ( !valid_map )
			continue;

		int slot = maps->count;
		for ( int i = 0; i < maps->count; i++ )
		{
			if ( !strcmp( maps->entries[i].map, map ) )
			{
				slot = i;	// duplicate: last one wins
				break;
			}
		}
		if ( slot >= SGTC_MUSIC_MAX_ENTRIES )
			continue;	// table full: keep whatever parsed so far

		if ( slot == maps->count )
			maps->count++;
		strncpy( maps->entries[slot].map, map, sizeof( maps->entries[slot].map ) - 1 );
		maps->entries[slot].map[sizeof( maps->entries[slot].map ) - 1] = '\0';
		strncpy( maps->entries[slot].slug, slug, sizeof( maps->entries[slot].slug ) - 1 );
		maps->entries[slot].slug[sizeof( maps->entries[slot].slug ) - 1] = '\0';
	}
}

// Returns the slug mapped to mapname, or "" when the map is not listed. The
// returned pointer is owned by the table.
inline const char *SgtcMusic_FindSlug( const sgtc_music_maps_t *maps, const char *mapname )
{
	if ( !maps )
		return "";

	char key[SGTC_MUSIC_NAME_LEN];
	SgtcMusic_NormalizeLevelName( mapname, key, sizeof( key ) );

	for ( int i = 0; i < maps->count; i++ )
	{
		if ( !strcmp( maps->entries[i].map, key ) )
			return maps->entries[i].slug;
	}

	return "";
}
