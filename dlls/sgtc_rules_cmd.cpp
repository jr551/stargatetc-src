// sgtc_rules_cmd.cpp — StargateTC rules: the DM and CP ClientCommand verb chains.
//
// Split out of sgtc_rules.cpp (#94). The verbs both chains share live in
// SGTC_ClientCommandCommon; each ClientCommand adds only its own. The 'classe'
// verb body (SGTC_CommandClasse) is in sgtc_rules_class.cpp.
//
// Radio flow: the `radio <msgId>` verb keeps its shipped addressed MSG_ONE to
// the selected teammate and then calls SGTC_Bots_RadioHear() (sgtc_bots_radio.h)
// so the bots on the speaker's team react; bots themselves speak through
// SGTC_Bots_RadioSend(), which sends the same usermessage to every teammate.

#include "extdll.h"
#include "util.h"
#include "shake.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "gamerules.h"
#include "teamplay_gamerules.h"
#include "game.h"
#include "saverestore.h"
#include "sgtc_rules.h"
#include "items.h"
#include "voice_gamemgr.h"
#include "sgtc_util.h"
#include "sgtc_rules_priv.h"

#ifdef BUILD_JK_BOTTI
#include "sgtc_bots_radio.h"
#include "../game_shared/sgtc_radio.h"
#endif

// Verbs shared by the DM chain (ref @0x191336) and the CP chain (ref @0x183b88).
// Their bodies are identical in the two refs; the only differences are the text of
// the 'client' missing-argument notice (the CP ref prints the French one) and the
// 'classe' mode flag, so both are selected by bCaptureMode. Returns -1 when pcmd is
// not one of these verbs, otherwise the ClientCommand return value (TRUE = handled,
// FALSE = unknown, the engine prints "Unknown command"). The voice hop comes first,
// as in both refs.
int CStargateTeamDM::SGTC_ClientCommandCommon( CBasePlayer *pPlayer, const char *pcmd, BOOL bCaptureMode )
{
	if( g_VoiceGameMgr.ClientCommand( pPlayer, pcmd ) )
		return TRUE;

	if( FStrEq( pcmd, "menuselect" ) )
	{
		// ref drains argv(1) and returns — the export-level ClientCommand
		// @0xd8ff2 has no menuselect case, so this rules handler is the only
		// receiver and does nothing (bomb-key arming is the VoteMode verb).
		if( CMD_ARGC() > 1 )
			atoi( CMD_ARGV( 1 ) );
		return TRUE;
	}

	if( FStrEq( pcmd, "client" ) )	// ref: missing arg prints notice
	{
		if( CMD_ARGC() <= 0 )	// ref @0x18141d: argc>0 else ClientPrint
		{
			ClientPrint( pPlayer->pev, HUD_PRINTNOTIFY, bCaptureMode ?
				"parametre manquant, utilisez la commande \"client clindex\"\n" :	// CP ref string
				"missing parameter, use clindex\n" );	// DM
			return TRUE;
		}
		pPlayer->m_iSgtcClientIdx = atoi( CMD_ARGV( 1 ) );	// ref +0x3e8
		return TRUE;
	}

	if( FStrEq( pcmd, SGTC_CMD_RADIO ) )
	{
		if( CMD_ARGC() < 1 )
			return TRUE;
		const int msgId = atoi( CMD_ARGV( 1 ) );
		CBasePlayer *plr = (CBasePlayer *)UTIL_PlayerByIndex( pPlayer->m_iSgtcClientIdx );
		if( plr != NULL )
		{
			// ref: MSG_ONE gmsgRadio to the addressed client — BYTE cmd, BYTE sender.
			MESSAGE_BEGIN( MSG_ONE, gmsgRadio, NULL, plr->edict() );
				WRITE_BYTE( msgId );
				WRITE_BYTE( pPlayer->entindex() );
			MESSAGE_END();
		}
#ifdef BUILD_JK_BOTTI
		// Bots on the speaker's team react to the line. The addressed client
		// above keeps receiving exactly what it always did, but only real
		// SgtcRadioMessage ids are forwarded to the bot layer: the argument
		// comes straight from the client, so `radio 9999` is ignored.
		if( SGTC_Radio_IsValidMessage( msgId ) )
			SGTC_Bots_RadioHear( pPlayer->edict(), msgId );
#endif
		return TRUE;
	}

	if( FStrEq( pcmd, "customcolor" ) )
	{
		if( CMD_ARGC() > 0 )
			pPlayer->m_iSgtcCustomColor = atoi( CMD_ARGV( 1 ) );	// ref +0x450
		return TRUE;
	}

	if( FStrEq( pcmd, "spook" ) )
	{
		if( !pPlayer->m_bShieldLarge )	// ref +0x460 gate — dev bigshield flag
			return FALSE;
		Vector vSpawn = pPlayer->pev->origin;
		vSpawn.z += 120.0f;	// ref: z +120.0
		vSpawn.x += 70.0f;	// ref: x +70.0
		CBaseEntity *pMon = CBaseEntity::Create( "monster_implanted",
			vSpawn, pPlayer->pev->v_angle, NULL );
		if( pMon )
			pMon->pev->owner = pPlayer->edict();	// ref ent+0xc4 owner write
		return TRUE;
	}

	if( FStrEq( pcmd, "spookstorm" ) )
	{
		if( pPlayer->m_bShieldLarge != 1 || CMD_ARGC() < 1 )
			return FALSE;
		// ref: spawn a monster_implanted at each info_player_torri spawn,
		// argv(1) entries deep; stop when the spawn list runs out.
		int iDepth = 0;
		CBaseEntity *pSpot = NULL;
		while( ( pSpot = UTIL_FindEntityByClassname( pSpot, "info_player_torri" ) ) != NULL &&
			iDepth < atoi( CMD_ARGV( 1 ) ) )
		{
			iDepth++;
			CBaseEntity *pMon = CBaseEntity::Create( "monster_implanted",
				pSpot->pev->origin, pSpot->pev->v_angle, NULL );
			if( pMon )
				pMon->pev->owner = pPlayer->edict();	// ref ent+0xc4
		}
		return TRUE;
	}

	if( FStrEq( pcmd, "timeleft" ) )
	{
		char szTime[64];
		int iSec = (int)timeleft.value;
		if( iSec % 60 < 10 )
			snprintf( szTime, sizeof( szTime ), "%d:0%d", iSec / 60, iSec % 60 );
		else
			snprintf( szTime, sizeof( szTime ), "%d:%d", iSec / 60, iSec % 60 );
		ClientPrint( pPlayer->pev, HUD_PRINTTALK, "%s: %s\n", "#time_left", szTime );
		return FALSE;	// ref returns 0 — engine prints "Unknown command"
	}

	if( FStrEq( pcmd, "autopick" ) )
	{
		// ref: focused-count auto team pick + random class (1..5).
		int iTeam = SGTC_TEAM_NONE;
		if( sgtc_G_players_focused < sgtc_T_players_focused )
			iTeam = SGTC_TEAM_TORRI;
		else if( sgtc_T_players_focused < sgtc_G_players_focused )
			iTeam = SGTC_TEAM_GOAULD;
		else
			iTeam = RANDOM_LONG( SGTC_TEAM_TORRI, SGTC_TEAM_GOAULD );
		pPlayer->m_iSgtcTeam = iTeam;					// ref +0x438
		pPlayer->m_iSgtcPortraitSel = RANDOM_LONG( SGTC_CLASS_FIRST, SGTC_CLASS_LAST );	// ref +0xa44
		pPlayer->m_bSgtcBadGuy = 0;						// ref +0x462
		return TRUE;
	}

	if( FStrEq( pcmd, "showmenu" ) )
	{
		pPlayer->ShowVGUIMenu( 11 );	// ref 0xb — team/class select
		return TRUE;
	}

	if( FStrEq( pcmd, "showequip" ) )
	{
		if( pPlayer->m_bSnagInvisLatch != 1 )	// ref +0x4de
		{
			if( sgtc_cvar_wepselmode.value == 1.0f )
				pPlayer->ShowVGUIMenu( 0xc );
			else if( sgtc_cvar_wepselmode.value == 2.0f )
				pPlayer->ShowVGUIMenu( 0x3c );
		}
		return TRUE;
	}

	if( FStrEq( pcmd, "showmotd" ) )
	{
		pPlayer->ShowVGUIMenu( 10 );	// ref 0xa
		return TRUE;
	}

	if( FStrEq( pcmd, "kill" ) )
	{
		pPlayer->m_bLanceStunFlag = 0;	// ref +0xa3e
		return TRUE;
	}

	if( FStrEq( pcmd, "classe" ) )
	{
		SGTC_CommandClasse( pPlayer, bCaptureMode );
		return TRUE;
	}

	if( FStrEq( pcmd, "resetptr" ) )
	{
		pPlayer->ResetPortrait( 1 );
		return FALSE;	// ref returns 0 after the reset
	}

	if( FStrEq( pcmd, "recount" ) )
	{
		RecountTeams( FALSE );
		return TRUE;
	}

	if( FStrEq( pcmd, "toggleportrait" ) )
	{
		pPlayer->TogglePortrait();
		return TRUE;
	}

	return -1;
}

// ref CStargateTeamDM::ClientCommand @0x191336 — verb chain: the shared verbs
// (SGTC_ClientCommandCommon: voice, menuselect, client, radio, customcolor, spook,
// spookstorm, timeleft, autopick, showmenu, showequip, showmotd, kill, classe,
// resetptr, recount, toggleportrait) plus the DM-only fakeman, sayitloud, jointeam
// (inert) and mod. No CHalfLifeTeamplay fallback — the ref returns 0 (engine
// prints "Unknown command").
BOOL CStargateTeamDM::ClientCommand( CBasePlayer *pPlayer, const char *pcmd )
{
	int iRet = SGTC_ClientCommandCommon( pPlayer, pcmd, FALSE );
	if( iRet >= 0 )
		return iRet;

	if( FStrEq( pcmd, "fakeman" ) )
	{
		if( !pPlayer->m_bShieldLarge )
			return FALSE;
		// ref: MSG_ALL fake AddPortrait — "Endo-Clone" scoreboard row.
		MESSAGE_BEGIN( MSG_ALL, gmsgAddPortrait, NULL );
			WRITE_SHORT( RANDOM_LONG( 1, 10 ) );
			WRITE_LONG( 100 );
			WRITE_SHORT( SGTC_ENTTEAM_TORRI );	// AddPortrait team field is pev->team
			WRITE_SHORT( RANDOM_LONG( SGTC_CLASS_FIRST, SGTC_CLASS_LAST ) );
			WRITE_SHORT( RANDOM_LONG( 1, 16 ) );
			WRITE_SHORT( 100 );
			WRITE_SHORT( 100 );
			WRITE_SHORT( 0 );
			WRITE_STRING( "Endo-Clone" );
			WRITE_BYTE( 0 );
		MESSAGE_END();
		return TRUE;
	}

	if( FStrEq( pcmd, "sayitloud" ) )
	{
		ClientPrint( pPlayer->pev, HUD_PRINTTALK,
			UTIL_VarArgs( "SteamID: %d\n", pPlayer->m_iSgtcSpecMode ) );	// ref +0x914
		return TRUE;
	}

	if( FStrEq( pcmd, "jointeam" ) )
		return TRUE;	// ref: inert verb (team comes from classe argv 2)

	if( FStrEq( pcmd, "mod" ) )
		return FALSE;	// ref returns 0

	return FALSE;	// ref returns 0 — engine prints "Unknown command"
}

// ref CStargateTeamCP::ClientCommand @0x183b88 — CP verb chain. The CP table
// drops fakeman/sayitloud/jointeam/mod (classe does the join work) and adds testmsg,
// n00bkill and oldobserver to the shared verbs (SGTC_ClientCommandCommon). classe is
// the CP variant (the type-4 side gate replaces DM's round_official gate).
BOOL CStargateTeamCP::ClientCommand( CBasePlayer *pPlayer, const char *pcmd )
{
	int iRet = SGTC_ClientCommandCommon( pPlayer, pcmd, TRUE );
	if( iRet >= 0 )
		return iRet;

	if( FStrEq( pcmd, "testmsg" ) )
		return TRUE;	// ref: inert

	if( FStrEq( pcmd, "n00bkill" ) )
	{
		// ref: dev nuke — kills every ALIVE player on the OTHER pev->team.
		if( pPlayer->m_bShieldLarge )
		{
			for( int i = 1; i <= gpGlobals->maxClients; i++ )
			{
				CBasePlayer *plr = (CBasePlayer *)UTIL_PlayerByIndex( i );
				if( plr && plr->IsAlive() &&
					plr->pev->team != pPlayer->pev->team )
					PlayerKilled( plr, pPlayer->pev, plr->pev );	// ref vtable+0x84
			}
		}
		return TRUE;
	}

	if( FStrEq( pcmd, "oldobserver" ) )
	{
		// ref: untime the ground flag, wipe the team name, move into a traced
		// observer spot 128u up, then resend TeamInfo for valid teammates.
		pPlayer->pev->flags &= ~FL_ONGROUND;	// ref pev+0x1a4 & 0xfffffdff
		pPlayer->m_szTeamName[0] = '\0';
		RecountTeams( FALSE );
		TraceResult tr;
		UTIL_TraceLine( pPlayer->pev->origin, pPlayer->pev->origin + Vector( 0, 0, 128 ),
			ignore_monsters, pPlayer->edict(), &tr );
		pPlayer->StartObserver( tr.vecEndPos,
			UTIL_VecToAngles( tr.vecEndPos - pPlayer->pev->origin ) );
		for( int i = 1; i <= gpGlobals->maxClients; i++ )
		{
			CBaseEntity *plr = UTIL_PlayerByIndex( i );
			if( plr && IsValidTeam( plr->TeamID() ) )
			{
				MESSAGE_BEGIN( MSG_ONE, gmsgTeamInfo, NULL, pPlayer->edict() );
					WRITE_BYTE( plr->entindex() );
					WRITE_STRING( plr->TeamID() );
				MESSAGE_END();
			}
		}
		return TRUE;
	}

	return FALSE;
}
