// l10n.h — client-side per-language titles overlay.
//
// The engine loads <gamedir>/titles.txt (the English master) and resolves
// every "#Token" string through pfnTextMessageGet. This module layers an
// optional per-language file on top of it:
//
//     resource/l10n/<cl_language>/titles.txt
//
// The format is the engine's titles.txt format (engine/client/titles.c):
// a name line, then '{', then verbatim text (newlines preserved) up to a
// line that begins with '}'. '//' line comments and blank lines are
// ignored; '$' directive lines are tolerated and ignored because an
// overlay carries text only. Overlays may be partial. A UTF-8 BOM and CR
// line endings are accepted; the rest of the file must be UTF-8.
//
// Encoding rule — there are two text consumers in the client:
//   * Engine-drawn text (centreprint, chat, console, HUD strings drawn via
//     pfnDrawString) is decoded as UTF-8 by the engine (cl_charset defaults
//     to "utf-8"). L10n_Lookup returns UTF-8 for those callers.
//   * VGUI1 widgets are byte-oriented: FreeVGUI indexes its 256-cell font
//     sheet by raw char code and never decodes UTF-8. Text handed to a
//     vgui:: widget must therefore be converted with L10n_ToLegacy.
//
// Lookup order for a token: overlay -> engine title -> raw literal.
#pragma once
#if !defined( L10N_H )
#define L10N_H

// Registers cl_language, registers l10n_reload and loads the overlay for
// the current language. Safe to call more than once.
void L10n_Init( void );

// Re-reads the overlay after cl_language changed.
void L10n_Reload( void );

// Returns the overlay text for key (a leading '#' is ignored, matching is
// case-insensitive), or NULL when the overlay does not define the key.
// The result is UTF-8 and stays valid until the next L10n_Reload.
const char *L10n_Lookup( const char *key );

// Returns the language name currently in use ("english" when no overlay).
const char *L10n_GetLanguage( void );

// Converts UTF-8 to the CP1252 byte string VGUI1 widgets expect. ASCII,
// including all English text, passes through byte-for-byte, as do bytes
// that are not valid UTF-8 (already-legacy text). Unmappable characters
// become '?'. The result lives in a small ring of static buffers: use it
// immediately (e.g. as a widget setText argument) and do not hold on to it
// across other L10n_ToLegacy calls.
const char *L10n_ToLegacy( const char *utf8 );

#endif // L10N_H
