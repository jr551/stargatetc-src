// sgtc_music.cpp — map-aware ambient music manager (client DLL).
//
// Engine contract (Xash3D FWGS, verified in engine sources):
//   `music <intro> <loop>` with 3 args calls S_StartBackgroundTrack(intro, loop)
//   directly, so passing the same name twice loops the track; the sound loader
//   tries mp3, wav, ogg and opus and also looks under media/, so "music/<slug>"
//   resolves to stargatetc/music/<slug>.ogg.
//   `cd stop` stops the background track.
//   MP3Volume (FCVAR_ARCHIVE|FCVAR_FILTERABLE) is the background-music volume.
//   HUD_VidInit is called from CL_ParseServerData on every connect and
//   changelevel, but before clgame.mapname is parsed, so pfnGetLevelName() is
//   empty there; HUD_Frame (called once per host frame) retries the lookup.
//
// Behaviour:
//   - music/maps.txt maps "<mapname> <slug>" and nothing else; read once per
//     level. Only listed maps get a track: for a map without a line this
//     manager starts nothing and stops nothing, so engine, server and
//     map-triggered audio keep working exactly as before. A missing file is
//     reported once and disables the feature.
//   - sgtc_music 1: the map's track starts on the first frame that knows the
//     level name, and MP3Volume is set to sgtc_musicvolume while that track
//     of ours is playing.
//   - sgtc_music 0: stop the track only if we started it, and put the player's
//     own MP3Volume back. MP3Volume is never zeroed: it also drives the mod's
//     menu music (stargatetc/media/gamestartup.mp3) and any map/server track.
//   - Toggling sgtc_music or changing sgtc_musicvolume at runtime is applied on
//     the next HUD_Frame; the options panel calls the setters below, which
//     apply immediately.
//   - One start per level entry, and a track that is already playing is never
//     restarted: HUD_VidInit also fires for video-mode changes and changelevel
//     keeps the stream, so the previous slug is kept until the engine reports
//     no level (disconnect stops the stream) or a map wants a different slug.
//     A server-issued `music` command afterwards is left alone instead of
//     being fought every frame.
//
// NEW file — sgtc_* only, no stock edits.
// Headers used: hud.h, cl_util.h, sgtc_music.h, sgtc_music_maps.h

#include "hud.h"
#include "cl_util.h"
#include "sgtc_music.h"
#include "sgtc_music_maps.h"

#include <stdio.h>
#include <string.h>

// Enough for the shipped table (44 maps) plus future maps; the parser drops
// anything past the cap instead of overflowing.
static sgtc_music_maps_t s_Maps;

static cvar_t *s_pMusicCvar = NULL;
static cvar_t *s_pVolumeCvar = NULL;
static bool s_bRegistered = false;

static bool s_bMapsReload = true;	// read maps.txt again on the next resolve
static bool s_bMapsMissing = false;	// the last load found no music/maps.txt
static bool s_bMapsMissingLogged = false;
static bool s_bLevelResolved = false;
static bool s_bMapApplied = false;

static char s_szMap[SGTC_MUSIC_NAME_LEN];		// normalized map name
static char s_szDesiredSlug[SGTC_MUSIC_SLUG_LEN];	// maps.txt result for the map
static char s_szActiveSlug[SGTC_MUSIC_SLUG_LEN];	// track we started
static bool s_bActiveTrack = false;

static int s_iLastEnabled = -1;			// -1: not applied yet for this map

// MP3Volume ownership. We only ever move this cvar while a track *we* started
// is playing, and we put the player's own value back when that track stops:
// MP3Volume also drives the menu music and any server/map track.
static bool s_bVolumeOwned = false;		// MP3Volume currently carries our volume
static float s_flSavedVolume = 1.0f;		// the player's own MP3Volume
static float s_flAppliedVolume = -1.0f;		// last value we wrote (-1: none)

// ---------------------------------------------------------------------------
// cvar helpers
// ---------------------------------------------------------------------------

void SgtcMusic_Init( void )
{
	if ( s_bRegistered )
		return;

	// Cvar_Get (behind pfnRegisterVariable) returns the existing cvar when one
	// is already registered, so re-running this on every connect preserves the
	// player's archived value.
	s_pMusicCvar = CVAR_CREATE( "sgtc_music", "1", FCVAR_ARCHIVE );
	s_pVolumeCvar = CVAR_CREATE( "sgtc_musicvolume", "0.35", FCVAR_ARCHIVE );
	s_bRegistered = true;
}

static float ReadMusicCvar( void )
{
	if ( s_pMusicCvar )
		return s_pMusicCvar->value;
	return gEngfuncs.pfnGetCvarFloat( "sgtc_music" );
}

static float ReadVolumeCvar( void )
{
	if ( s_pVolumeCvar )
		return s_pVolumeCvar->value;
	return gEngfuncs.pfnGetCvarFloat( "sgtc_musicvolume" );
}

int SgtcMusic_GetEnabled( void )
{
	if ( !s_bRegistered )
		SgtcMusic_Init();
	return ReadMusicCvar( ) != 0.0f;
}

float SgtcMusic_GetVolume( void )
{
	if ( !s_bRegistered )
		SgtcMusic_Init();

	float volume = ReadVolumeCvar( );
	if ( volume < 0.0f )
		volume = 0.0f;
	else if ( volume > 1.0f )
		volume = 1.0f;
	return volume;
}

// MP3Volume is the engine's cvar, so it is written through the engine rather
// than registered here. It is only touched while a track this client started
// is playing: the player's own value is snapshotted first and restored
// afterwards, and a value the player changed by hand meanwhile is kept.
static float ReadEngineVolume( void )
{
	float volume = gEngfuncs.pfnGetCvarFloat( "MP3Volume" );
	if ( volume < 0.0f )
		volume = 0.0f;
	else if ( volume > 1.0f )
		volume = 1.0f;
	return volume;
}

static void WriteEngineVolume( float volume )
{
	if ( s_flAppliedVolume >= 0.0f )
	{
		float delta = s_flAppliedVolume - volume;
		if ( delta > -0.0001f && delta < 0.0001f )
			return;
	}

	char value[16];
	snprintf( value, sizeof( value ), "%.3f", volume );
	gEngfuncs.Cvar_Set( "MP3Volume", value );
	s_flAppliedVolume = volume;
}

// Remember the player's value before the first write of a run of ours. A track
// change while our volume is already in place keeps the original snapshot.
static void TakeEngineVolume( void )
{
	if ( s_bVolumeOwned )
		return;
	s_flSavedVolume = ReadEngineVolume( );
	s_bVolumeOwned = true;
	s_flAppliedVolume = -1.0f;
}

// Apply sgtc_musicvolume, but only while one of our tracks is playing. Writing
// is idempotent, so this is safe to call every frame.
static void ApplyEngineVolume( void )
{
	if ( !s_bActiveTrack )
		return;

	TakeEngineVolume( );
	WriteEngineVolume( SgtcMusic_GetVolume( ) );
}

// Give MP3Volume back when our track stops. If the player moved the slider
// since our last write, their value is current and is left alone.
static void RestoreEngineVolume( void )
{
	if ( !s_bVolumeOwned )
		return;

	float current = ReadEngineVolume( );
	if ( s_flAppliedVolume >= 0.0f && current < s_flAppliedVolume + 0.0005f
		&& current > s_flAppliedVolume - 0.0005f )
	{
		WriteEngineVolume( s_flSavedVolume );
	}

	s_bVolumeOwned = false;
	s_flAppliedVolume = -1.0f;
}

// ---------------------------------------------------------------------------
// track control
// ---------------------------------------------------------------------------

const char *SgtcMusic_GetMapName( void )
{
	return s_szMap;
}

const char *SgtcMusic_GetActiveSlug( void )
{
	return s_bActiveTrack ? s_szActiveSlug : "";
}

static void PlayTrack( const char *slug )
{
	if ( !slug || !slug[0] )
		return;

	// Guard against a duplicate start inside the same level: the per-frame
	// path and a setter can both reach here. A new level clears the active
	// slug in SgtcMusic_LevelInit, so each level entry reissues the command.
	if ( s_bActiveTrack && !strcmp( s_szActiveSlug, slug ) )
	{
		ApplyEngineVolume( );
		return;
	}

	// Snapshot the player's MP3Volume before the first write of this run.
	TakeEngineVolume( );

	// Three-argument form: intro == loop, so the engine loops it seamlessly.
	char command[128];
	snprintf( command, sizeof( command ), "music \"music/%s\" \"music/%s\"", slug, slug );
	gEngfuncs.pfnClientCmd( command );

	strncpy( s_szActiveSlug, slug, sizeof( s_szActiveSlug ) - 1 );
	s_szActiveSlug[sizeof( s_szActiveSlug ) - 1] = '\0';
	s_bActiveTrack = true;

	ApplyEngineVolume( );

	gEngfuncs.Con_Printf( "[music] playing music/%s for %s\n", slug,
		s_szMap[0] ? s_szMap : "current map" );
}

// Stop the track this client started. `cd stop` and `cd` in general act on the
// one engine background track, which may also be the mod's menu music
// (stargatetc/media/gamestartup.mp3) or a map/server `music` command, so the
// command is only issued when the active track is ours. The player's own
// MP3Volume is restored either way.
static void StopTrack( bool log )
{
	bool ours = s_bActiveTrack;

	if ( ours )
		gEngfuncs.pfnClientCmd( "cd stop" );

	s_bActiveTrack = false;
	s_szActiveSlug[0] = '\0';
	RestoreEngineVolume( );

	if ( log && ours )
		gEngfuncs.Con_Printf( "[music] disabled, background track stopped\n" );
}

// ---------------------------------------------------------------------------
// maps.txt + level name
// ---------------------------------------------------------------------------

static void LoadMapsFile( void )
{
	s_Maps.count = 0;

	int length = 0;
	byte *data = gEngfuncs.COM_LoadFile( "music/maps.txt", 5, &length );
	if ( !data )
	{
		s_bMapsMissing = true;
		s_bMapsReload = false;
		if ( !s_bMapsMissingLogged )
		{
			s_bMapsMissingLogged = true;
			gEngfuncs.Con_Printf( "[music] music/maps.txt not found, ambient music off\n" );
		}
		return;
	}

	s_bMapsMissing = false;

	// COM_LoadFile appends a NUL in FWGS, but the HL SDK only guarantees the
	// length, so copy into a bounded buffer and terminate ourselves.
	char text[8192];
	int copy = length;
	if ( copy < 0 )
		copy = 0;
	if ( copy > (int)sizeof( text ) - 1 )
		copy = sizeof( text ) - 1;
	if ( copy > 0 )
		memcpy( text, data, copy );
	text[copy] = '\0';
	gEngfuncs.COM_FreeFile( data );

	SgtcMusic_ParseMaps( text, &s_Maps );
	s_bMapsReload = false;
}

static void ResolveLevel( const char *level )
{
	char map[SGTC_MUSIC_NAME_LEN];
	SgtcMusic_NormalizeLevelName( level, map, sizeof( map ) );
	if ( !map[0] )
		return;	// engine has not parsed the map name yet

	strncpy( s_szMap, map, sizeof( s_szMap ) - 1 );
	s_szMap[sizeof( s_szMap ) - 1] = '\0';

	if ( s_bMapsReload )
		LoadMapsFile( );

	const char *slug = SgtcMusic_FindSlug( &s_Maps, s_szMap );
	if ( slug && slug[0] )
	{
		strncpy( s_szDesiredSlug, slug, sizeof( s_szDesiredSlug ) - 1 );
		s_szDesiredSlug[sizeof( s_szDesiredSlug ) - 1] = '\0';
	}
	else
	{
		s_szDesiredSlug[0] = '\0';
	}

	s_bLevelResolved = true;
}

// ---------------------------------------------------------------------------
// per-map application + per-frame cvar tracking
// ---------------------------------------------------------------------------

void SgtcMusic_LevelInit( void )
{
	// HUD_VidInit fires on connects, changelevels AND video-mode changes
	// (SCR_VidInit). Keep s_bActiveTrack/s_szActiveSlug: a vid-mode change or
	// a changelevel whose new map wants the same slug must not restart the
	// track that is still playing. ApplyForMap -> PlayTrack skips the start
	// when the desired slug is already the active one; a different map starts
	// its own track, and SgtcMusic_Frame clears the active state while the
	// engine reports no level (CL_Disconnect stopped the track), so a
	// reconnect to the same map still reissues it.
	//
	// MP3Volume ownership is kept too: the engine carries the background track
	// across a changelevel, so the value we wrote is still in force and the
	// player's original snapshot must not be overwritten with our own value.
	s_bMapsReload = true;
	s_bMapsMissing = false;	// the file may have been installed since last level
	s_bLevelResolved = false;
	s_bMapApplied = false;
	s_iLastEnabled = -1;
	s_szMap[0] = '\0';
	s_szDesiredSlug[0] = '\0';
}

static void ApplyForMap( int enabled )
{
	if ( !s_szDesiredSlug[0] )
	{
		// The map is not listed in maps.txt: no track of ours belongs here and
		// the engine's, the server's or the map's own audio is left untouched.
		// Only a track this client started for a previous map is stopped (that
		// one is ours) and the player's MP3Volume comes back with it.
		if ( s_bActiveTrack )
			StopTrack( false );

		if ( !s_bMapsMissing )
			gEngfuncs.Con_Printf( "[music] no ambient track for %s, map audio left alone\n",
				s_szMap[0] ? s_szMap : "this map" );
		return;
	}

	if ( !enabled )
	{
		// Listed map, feature off: stop only what we started, never the
		// engine's own background track.
		StopTrack( true );
		return;
	}

	PlayTrack( s_szDesiredSlug );
}

void SgtcMusic_Frame( void )
{
	if ( !s_bRegistered )
		SgtcMusic_Init( );

	// No level means not connected: CL_Disconnect stops the background track,
	// so there is nothing of ours left to `cd stop`. Give MP3Volume back to the
	// player so the menu music plays at their own volume.
	const char *level = gEngfuncs.pfnGetLevelName( );
	if ( !level || !level[0] )
	{
		s_bActiveTrack = false;
		s_szActiveSlug[0] = '\0';
		RestoreEngineVolume( );
		return;
	}

	if ( !s_bLevelResolved )
	{
		ResolveLevel( level );
		if ( !s_bLevelResolved )
			return;	// retry on the next frame
	}

	int enabled = SgtcMusic_GetEnabled( );

	if ( !s_bMapApplied )
	{
		ApplyForMap( enabled );
		s_bMapApplied = true;
		s_iLastEnabled = enabled;
		return;
	}

	// Volume slider / console changes while one of our tracks is playing.
	if ( enabled )
		ApplyEngineVolume( );

	// Runtime on/off toggle.
	if ( enabled != s_iLastEnabled )
	{
		if ( enabled )
		{
			if ( s_szDesiredSlug[0] )
				PlayTrack( s_szDesiredSlug );
			else
				gEngfuncs.Con_Printf( "[music] no ambient track for %s, map audio left alone\n",
					s_szMap[0] ? s_szMap : "this map" );
		}
		else
		{
			StopTrack( true );
		}
		s_iLastEnabled = enabled;
	}
}

// ---------------------------------------------------------------------------
// setters used by the options panel
// ---------------------------------------------------------------------------

void SgtcMusic_SetEnabled( int enabled )
{
	if ( !s_bRegistered )
		SgtcMusic_Init( );

	gEngfuncs.Cvar_Set( "sgtc_music", enabled ? "1" : "0" );

	// Apply on the spot, not on the next frame: the options panel is a modal
	// VGUI menu and the player expects the track to change under the click.
	SgtcMusic_Frame( );
}

void SgtcMusic_SetVolume( float volume )
{
	if ( !s_bRegistered )
		SgtcMusic_Init( );

	if ( volume < 0.0f )
		volume = 0.0f;
	else if ( volume > 1.0f )
		volume = 1.0f;

	char value[16];
	snprintf( value, sizeof( value ), "%.3f", volume );
	gEngfuncs.Cvar_Set( "sgtc_musicvolume", value );

	SgtcMusic_Frame( );
}
