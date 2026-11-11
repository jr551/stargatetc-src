// sgtc_teamcmds.cpp — client-side team/class join verbs (#213)
//
// The shipped StargateTC client.dll registers the team/class selection verbs as
// *client* commands (ref strings "classe %d %d", "jointeam %d", "autopick",
// "spectate", "showmenu"); this port never registered them, with two visible
// consequences:
//
//   * typing "classe"/"jointeam" at the console printed "Unknown command", and
//   * the VGUI "Identifiez-vous" team/class panel's class cells fire their
//     command through pfnFilteredClientCmd (see CMenuHandler_StringCommand in
//     cl_dll/vgui_TeamFortressViewport.h).  With no client-side verb the line
//     only went through the engine's filtered command buffer, and the forward
//     to the server never materialised, so the menu stayed open and the player
//     stayed team 0.
//
// Registering the verbs here fixes both paths: the console line and the
// panel's filtered line both execute the local handler, which forwards the
// complete command line to the server with pfnServerCmd (clc_stringcmd) — the
// exact channel a console-typed unregistered command uses.  Server-side handling
// is unchanged (dlls/sgtc_rules_cmd.cpp):
//
//   classe <class> <team>   join: class 1..5, team 1 = Tau'ri, 2 = Goa'uld
//   autopick                auto team + class
//   spectate                spectator
//   showmenu                (re)open the "Identifiez-vous" panel (VGUIMenu 11)
//   showequip / showmotd    weapon loadout / MOTD screens
//
// The VGUI-less fallback (no panel in the viewport chain) keeps working: the
// help text printed by SGTC_TeamCmds_PrintJoinHelp() tells the player exactly
// which verbs/arguments to type.

#include "hud.h"
#include "cl_util.h"
#include "sgtc_teamcmds.h"

#include <string.h>
#include <stdio.h>

// Server-side team numbers (game_shared/sgtc_enums.h SgtcTeam).
#define SGTC_TEAM_TORRI   1
#define SGTC_TEAM_GOAULD  2

/*
=================
SGTC_TeamCmds_Forward_f

Rebuild "<verb> <args...>" from the engine tokenizer and send it to the server
as a string command.
=================
*/
static void SGTC_TeamCmds_Forward_f( void )
{
	const char *verb = gEngfuncs.Cmd_Argv( 0 );
	char cmd[128];

	if( !verb || !verb[0] )
		return;

	strlcpy( cmd, verb, sizeof( cmd ));

	for( int i = 1; i < 8; i++ )
	{
		const char *arg = gEngfuncs.Cmd_Argv( i );
		size_t len;

		if( !arg || !arg[0] )
			break;

		len = strlen( cmd );
		if( len + strlen( arg ) + 2 > sizeof( cmd ))
			break;

		cmd[len] = ' ';
		strlcpy( cmd + len + 1, arg, sizeof( cmd ) - len - 1 );
	}

	gEngfuncs.pfnServerCmd( cmd );
}

/*
=================
SGTC_TeamCmds_PrintJoinHelp

Text-only fallback for a client whose viewport has no team/class panel: print
the argument table the server understands.
=================
*/
void SGTC_TeamCmds_PrintJoinHelp( void )
{
	gEngfuncs.Con_Printf(
		"[sgtc] Team/class select — join with:  classe <class> <team>\n"
		"        team %d = Tau'ri    class 1 O'Neill, 2 Carter, 3 Jackson, 4 Teal'c, 5 Commander\n"
		"        team %d = Goa'uld   class 1 System Lord, 2 Anubis, 3 Jaffa, 4 Serpent Guard, 5 Horus\n"
		"        autopick = auto team/class, spectate = spectator, showmenu = reopen the menu\n",
		SGTC_TEAM_TORRI, SGTC_TEAM_GOAULD );
}

/*
=================
SGTC_TeamCmds_ChooseTeam_f

TFC "chooseteam"/"changeteam" verbs: the server owns the panel (ShowVGUIMenu
11), so ask it to resend the menu and print the text fallback for clients that
cannot draw it.
=================
*/
static void SGTC_TeamCmds_ChooseTeam_f( void )
{
	gEngfuncs.pfnServerCmd( "showmenu" );
	SGTC_TeamCmds_PrintJoinHelp();
}

/*
=================
SGTC_TeamCmds_Init

Register the client-side verbs. Called once from HUD_Init (cdll_int.cpp).
=================
*/
void SGTC_TeamCmds_Init( void )
{
	static const struct
	{
		const char *verb;
		void (*pfn)( void );
	} verbs[] =
	{
		{ "classe",      SGTC_TeamCmds_Forward_f },
		{ "jointeam",    SGTC_TeamCmds_Forward_f },
		{ "autopick",    SGTC_TeamCmds_Forward_f },
		{ "spectate",    SGTC_TeamCmds_Forward_f },
		{ "showmenu",    SGTC_TeamCmds_Forward_f },
		{ "showequip",   SGTC_TeamCmds_Forward_f },
		{ "showmotd",    SGTC_TeamCmds_Forward_f },
		{ "chooseteam",  SGTC_TeamCmds_ChooseTeam_f },
		{ "changeteam",  SGTC_TeamCmds_ChooseTeam_f },
	};

	for( int i = 0; i < (int)( sizeof( verbs ) / sizeof( verbs[0] )); i++ )
		gEngfuncs.pfnAddCommand( verbs[i].verb, verbs[i].pfn );
}
