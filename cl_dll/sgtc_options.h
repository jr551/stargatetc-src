// sgtc_options.h — in-game music options panel + `sgtc_options` command.
//
// The panel writes the same two archived cvars the music manager reads
// (sgtc_music / sgtc_musicvolume), so console and UI cannot disagree. When the
// engine has no VGUI root the command falls back to printing the current
// settings and how to change them from the console.
//
// NEW file — sgtc_* only, no stock edits.
#pragma once

// HUD_Init: registers the client command.
void SgtcOptions_Init( void );

// HUD_Frame (real VGUI only): closes the panel when the console is opened so
// the two input contexts never fight over the cursor.
void SgtcOptions_Frame( void );

// `sgtc_options`: open the panel, or close it when it is already open.
void SgtcOptions_Toggle( void );
