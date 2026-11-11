// sgtc_music.h — map-aware ambient music for the SGTC client DLL.
//
// The engine has no per-map music concept: the server's map-start `music`
// command is the only stock path, and it cannot be switched off by the player.
// This module owns that job in the client instead:
//
//   music/maps.txt (mod dir)   map name -> track slug, listed maps only
//   sgtc_music                 1 = ambient music on (archived, default "1")
//   sgtc_musicvolume           0..1 (archived, default "0.35")
//   MP3Volume                  engine background-music volume, ours while we play
//
// On every level the client DLL resolves the map name (gEngfuncs.pfnGetLevelName
// is empty during HUD_VidInit, so the lookup is retried from HUD_Frame), reads
// the table once, and issues `music "music/<slug>" "music/<slug>"` once — the
// two-argument form loops the track. Only maps listed in the table get a track:
// for any other map the manager starts nothing and stops nothing, so the
// engine's menu music and map/server tracks keep working. Turning the feature
// off stops the track only if this client started it and restores the player's
// own MP3Volume; it is never set to zero.
//
// NEW file — sgtc_* only, no stock edits.
#pragma once

// HUD_Init: registers the two archived cvars.
void SgtcMusic_Init( void );

// HUD_VidInit / level change: forget the resolved map so the next HUD_Frame
// re-reads maps.txt and applies the new map's track.
void SgtcMusic_LevelInit( void );

// HUD_Frame: resolves the level name once available, applies the per-map track
// once, and follows live cvar changes (on/off toggle, volume slider/console).
void SgtcMusic_Frame( void );

// Accessors for the options panel (and any future UI). Values are kept within
// the documented ranges and the setters apply the change immediately.
int SgtcMusic_GetEnabled( void );
void SgtcMusic_SetEnabled( int enabled );
float SgtcMusic_GetVolume( void );
void SgtcMusic_SetVolume( float volume );

// Resolved map name (empty until the engine reports a level) and the slug of
// the track the client started, if any (empty when music is off).
const char *SgtcMusic_GetMapName( void );
const char *SgtcMusic_GetActiveSlug( void );
