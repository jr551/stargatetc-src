// sgtc_teamcmds.h — client-side team/class join verbs (#213)
//
// Registers the shipped client's team/class selection verbs (classe, jointeam,
// autopick, spectate, showmenu, showequip, showmotd, chooseteam, changeteam) so
// they reach the server instead of printing "Unknown command".  See
// sgtc_teamcmds.cpp for the routing rationale.

#ifndef SGTC_TEAMCMDS_H
#define SGTC_TEAMCMDS_H

#ifdef __cplusplus
extern "C" {
#endif

// Register the verbs. Called once from HUD_Init (cdll_int.cpp).
void SGTC_TeamCmds_Init( void );

// Print the class/team argument table for the text-only fallback paths.
void SGTC_TeamCmds_PrintJoinHelp( void );

#ifdef __cplusplus
}
#endif

#endif // SGTC_TEAMCMDS_H
