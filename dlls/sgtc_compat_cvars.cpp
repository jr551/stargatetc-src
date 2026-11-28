// sgtc_compat_cvars.cpp - GoldSrc engine cvars that Xash3D FWGS does not have.
//
// The shipped server.cfg, listenserver.cfg and game.cfg set most of these, so every start logged
// "Warning: Unknown command ..." for each of them, and the console now shows the lines.
// Registering them as inert cvars lets the configs apply silently. A build whose engine has
// one of them keeps the engine's cvar (SGTC_RegisterOrAdoptCvar adopts an existing one), and
// none of them has any effect here.

#include "extdll.h"
#include "util.h"

static cvar_t s_compat_mp_logdetail = { "mp_logdetail", "0" };
static cvar_t s_compat_mp_logmessages = { "mp_logmessages", "0" };
static cvar_t s_compat_sv_logecho = { "sv_logecho", "1" };
static cvar_t s_compat_sv_logsdir = { "sv_logsdir", "logs" };
static cvar_t s_compat_sv_sendvelocity = { "sv_sendvelocity", "1" };
static cvar_t s_compat_sv_allow_download = { "sv_allow_download", "1" };
static cvar_t s_compat_sv_allow_upload = { "sv_allow_upload", "1" };
static cvar_t s_compat_timeout = { "timeout", "125" };
static cvar_t s_compat_zombietime = { "zombietime", "2" };
// Set by the game code and by maps on load; Xash prints "Cvar_Set: variable ... not found" for each.
static cvar_t s_compat_room_type = { "room_type", "0" };
static cvar_t s_compat_v_dark = { "v_dark", "0" };

void SGTC_CompatCvarsInit( void )
{
	SGTC_RegisterOrAdoptCvar( &s_compat_mp_logdetail );
	SGTC_RegisterOrAdoptCvar( &s_compat_mp_logmessages );
	SGTC_RegisterOrAdoptCvar( &s_compat_sv_logecho );
	SGTC_RegisterOrAdoptCvar( &s_compat_sv_logsdir );
	SGTC_RegisterOrAdoptCvar( &s_compat_sv_sendvelocity );
	SGTC_RegisterOrAdoptCvar( &s_compat_sv_allow_download );
	SGTC_RegisterOrAdoptCvar( &s_compat_sv_allow_upload );
	SGTC_RegisterOrAdoptCvar( &s_compat_timeout );
	SGTC_RegisterOrAdoptCvar( &s_compat_zombietime );
	SGTC_RegisterOrAdoptCvar( &s_compat_room_type );
	SGTC_RegisterOrAdoptCvar( &s_compat_v_dark );
}
