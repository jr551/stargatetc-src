// sgtc_rules.cpp — StargateTC teams/rules/triggers/env (NEW file).
//
// WHAT THIS IS: clean-room port of the SGTC 2.0 server rules slice onto the
// hlsdk-xash3d base. Bodies reconstructed from ref/stc_i386.so disassembly
// (nm -C / objdump -d -C); exact verified facts are cited inline as
// fileoff/VMA notes. Anything layout-level the client drains opaquely (env
// payload field order) is marked BEST-EFFORT.
//
// STOCK HOOK POINTS (report only — NO stock file is edited by this change):
//   1. hlsdk-xash3d/dlls/gamerules.cpp:310 `InstallGameRules()` — add:
//         SGTC_InstallGameRules( STRING( gpGlobals->mapname ) )
//     which dispatches on the map-name prefix (never returns NULL).
//   2. hlsdk-xash3d/dlls/player.cpp:191 `LinkUserMessages()` — call
//     `SGTC_LinkUserMessages_Prefix()` before stock and `_Suffix()` after.
//   3. hlsdk-xash3d/dlls/player.cpp:2701 `EntSelectSpawnPoint()` — SGTC team
//     spawns are filtered in CStargateTeamDM/CP/Unas::GetPlayerSpawnSpot, so
//     no edit is strictly required; optional: fall through to
//     "info_player_torri"/"info_player_goauld" when no DM spot exists.
//   4. hlsdk-xash3d/dlls/triggers.cpp — no hook needed (SGTC trigger classes
//     derive CBaseToggle and live entirely here; stock CBaseTrigger/
//     CTriggerMultiple are TU-local to triggers.cpp with no header decl).
//   5. Build lists: dlls/CMakeLists.txt lists every source below (dlls/wscript
//      globs dlls/*.cpp). The old single-file layout was split by concern (#94):
//        sgtc_rules.cpp          mode installer, shared helpers, CStargateTeamDM
//                                (team tables, damage/points, recount, spawn
//                                spots, connect/disconnect, Think)
//        sgtc_rules_cp.cpp       CStargateTeamCP (capture mode): ctor, spawn spot,
//                                userinfo, Think + type-4 countdown
//        sgtc_rules_alt.cpp      Unas / Invis / Replicateur rule sets
//        sgtc_rules_spawn.cpp    SetWeaponAllow, class tables, DM/CP PlayerSpawn
//        sgtc_rules_class.cpp    class caps, FPlayerCanRespawn, 'classe' verb
//        sgtc_rules_cmd.cpp      DM/CP ClientCommand verb chains
//        sgtc_rules_msg.cpp      user-message registration, wire helpers, HUD msgs
//        sgtc_entities.cpp       info_*, triggers, item_objective, mode_relay
//        sgtc_entities_env.cpp   env_model/particule/rain/lensflare/fountain/fog
//      Shared declarations: sgtc_rules_priv.h (private to those files).
//
// OUT OF SCOPE (owned by sibling slices): weapon_invis/CInvis (weapons),
// monster_implanted/skull, func_tankgoauld, gate/DHD, votesys/token.

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

// Mode installer. ref InstallGameRules @0x102e7a compares the MAP NAME
// (STRING(gpGlobals->mapname)) against the prefix table, NOT mp_gamemode:
//   sg_*->1 DM, sgi_*->2 DM, ctp_*->3 CP, un_*->5 Unas, inv_*->6 Invis,
//   rep_*->7 Replicateur, else->8 CHalfLifeMultiplay (g_teamplay=0).
// SGTC modes set g_teamplay=1. sgtc_GameModeSet mirrors ref g_mapmode.
// ---------------------------------------------------------------------------
#if !defined( _WIN32 ) && defined( __GNUC__ )
__attribute__(( visibility( "default" ) ))
#endif
CGameRules *SGTC_InstallGameRules( const char *pszMapName )
{
	if( pszMapName && *pszMapName )
	{
		if( !strncmp( pszMapName, "sg_", 3 ) )
		{
			sgtc_GameModeSet = 1;
			g_teamplay = 1;
			return new CStargateTeamDM;
		}
		if( !strncmp( pszMapName, "sgi_", 4 ) )
		{
			sgtc_GameModeSet = 2;
			g_teamplay = 1;
			return new CStargateTeamDM;
		}
		if( !strncmp( pszMapName, "ctp_", 4 ) )
		{
			sgtc_GameModeSet = 3;
			g_teamplay = 1;
			return new CStargateTeamCP;
		}
		if( !strncmp( pszMapName, "un_", 3 ) )
		{
			sgtc_GameModeSet = 5;
			g_teamplay = 1;
			return new CStargateUnas;
		}
		if( !strncmp( pszMapName, "inv_", 4 ) )
		{
			sgtc_GameModeSet = 6;
			g_teamplay = 1;
			return new CStargateInvis;
		}
		if( !strncmp( pszMapName, "rep_", 4 ) )
		{
			sgtc_GameModeSet = 7;
			g_teamplay = 1;
			return new CStargateReplicateur;
		}
	}
	sgtc_GameModeSet = 8;
	g_teamplay = 0;
	return new CHalfLifeMultiplay;
}

// Team spawn classname tables (rodata 0x1d7997 block ordering).
const char *const SGTC_TeamSpawnClassname[SGTC_MAX_SGTC_TEAMS] =
{
	"info_player_torri",
	"info_player_goauld",
};

// ===========================================================================
// CStargateTeamDM
// ===========================================================================
CStargateTeamDM::CStargateTeamDM( void )
{
	// ref ctor @0x188824: base CHalfLifeTeamplay ctor already read mp_teamlist
	// into m_szTeamList ("hgrunt;scientist" from game.cfg); the .so ctor then
	// OVERWRITES this+0x18 (m_szTeamList) with "Torri;Goa'uld", so mp_teamlist
	// never reaches the wire. RecountTeams() strtoks m_szTeamList into the
	// global team_names table (mirrored here as m_szSGTCTeams).
	SGTC_SetTeamList( "Torri;Goa'uld" );	// ref ctor this+0x18
	strncpy( m_szSGTCTeams[0], "Torri", SGTC_TEAMNAME_LEN );
	strncpy( m_szSGTCTeams[1], "Goa'uld", SGTC_TEAMNAME_LEN );
	m_iSGTCTeamCount = SGTC_MAX_SGTC_TEAMS;
	m_iSGTCTeamScores[0] = m_iSGTCTeamScores[1] = 0;
	m_bRoundOver = FALSE;
	m_bDisableDeathMessages = FALSE;	// ref this+0xc
	m_bDisableDeathPenalty = FALSE;	// ref this+0x10
	m_iSgtcModeVote = -1;	// ref +0x770 pending mode vote (-1 none)
	m_flSgtcRoundTime = 0.0f;
	m_flSgtcRoundEnd = 0.0f;
	m_iSgtcRoundsOnMap = 0;
	m_flSgtcCapScanTime = 0.0f;
	m_flSgtcUnasPickTime = 0.0f;
	m_iSgtcRoundTicks = 0;
	m_flSgtcRoundTickStart = 0.0f;
	m_iSgtcWinPending = 0;
	m_iSgtcWinScoreSide = 0;
	m_iSgtcFirstTicks = 0;
	m_iSgtcEmptyTeams = 0;
	m_iSgtcAutoBalance = 0;
	m_bSgtcVoteEqual = 0;
	m_iSgtcPrevGameModeSet = 0;	// ref DM +0x75c latch (fresh object)
	m_iSgtcFocusedBoth = 0;		// ref DM +0x760 latch
	m_iSgtcSupergunsLatch = 0;	// ref DM +0x76c latch
	m_iSgtcFocusedBucket = 0;	// ref CP +0x75c latch (shared member)
	g_iSgtcMapType = 0;		// ref DM ctor sets MapType=0 (@0x188600)

	// ref: INDEXENT(0)->targetname (+0x22c) overrides the team list when the
	// mp_teamoverride cvar is nonzero and the name is non-empty.
	edict_t *pWorld = INDEXENT( 0 );
	if( pWorld && pWorld->v.targetname && CVAR_GET_FLOAT( "mp_teamoverride" ) != 0 )
	{
		const char *tn = STRING( pWorld->v.targetname );
		if( tn && *tn )
		{
			SGTC_SetTeamList( tn );
		}
	}
	// ref this+0x14 already set inside SGTC_SetTeamList (list[0]!=0).
	RecountTeams( FALSE );

	// ref: roundforcing cvar (registered "mp_roundforcing") > 0 -> NewRound() right away.
	if( CVAR_GET_FLOAT( "mp_roundforcing" ) > 0 )
		NewRound();
}

int CStargateTeamBase::GetTeamIndex( const char *pTeamName )
{
	// Exact shape of the .so GetTeamIndex: strcasecmp loop, -1 on miss.
	if( pTeamName && *pTeamName )
	{
		for( int tm = 0; tm < m_iSGTCTeamCount; tm++ )
		{
			if( !stricmp( m_szSGTCTeams[tm], pTeamName ) )
				return tm;
		}
	}
	return -1;
}

const char *CStargateTeamBase::GetIndexedTeamName( int teamIndex )
{
	if( teamIndex < 0 || teamIndex >= m_iSGTCTeamCount )
		return "";
	return m_szSGTCTeams[teamIndex];
}

BOOL CStargateTeamBase::IsValidTeam( const char *pTeamName )
{
	return ( GetTeamIndex( pTeamName ) != -1 ) ? TRUE : FALSE;
}

const char *CStargateTeamBase::GetTeamID( CBaseEntity *pEntity )
{
	if( !pEntity || !pEntity->pev )
		return "";
	return pEntity->TeamID();
}

// PlayerRelationship is inherited from CHalfLifeTeamplay (teamplay_gamerules.cpp
// :429): identical same-team stricmp logic; the SGTC header has no decl, so the
// stock virtual covers all call sites below.

BOOL CStargateTeamBase::FPlayerCanTakeDamage( CBasePlayer *pPlayer, CBaseEntity *pAttacker )
{
	if( pAttacker && PlayerRelationship( pPlayer, pAttacker ) == GR_TEAMMATE )
	{
		if( friendlyfire.value == 0 && pAttacker != pPlayer )
			return FALSE;
	}
	return CHalfLifeMultiplay::FPlayerCanTakeDamage( pPlayer, pAttacker );
}

BOOL CStargateTeamBase::ShouldAutoAim( CBasePlayer *pPlayer, edict_t *target )
{
	CBaseEntity *pTgt = CBaseEntity::Instance( target );
	if( pTgt && pTgt->IsPlayer() && PlayerRelationship( pPlayer, pTgt ) == GR_TEAMMATE )
		return FALSE;
	return CHalfLifeMultiplay::ShouldAutoAim( pPlayer, target );
}

int CStargateTeamBase::IPointsForKill( CBasePlayer *pAttacker, CBasePlayer *pKilled )
{
	if( !pKilled )
		return 0;
	if( !pAttacker )
		return 1;
	if( pAttacker != pKilled && PlayerRelationship( pAttacker, pKilled ) == GR_TEAMMATE )
		return -1;
	return 1;
}

const char *CStargateTeamBase::TeamWithFewestPlayers( void )
{
	int teamCount[SGTC_MAX_SGTC_TEAMS] = { 0, 0 };
	int minPlayers = 9999;
	const char *pName = m_szSGTCTeams[0];
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *plr = UTIL_PlayerByIndex( i );
		if( plr )
		{
			int team = GetTeamIndex( plr->TeamID() );
			if( team >= 0 )
				teamCount[team]++;
		}
	}
	for( int i = 0; i < m_iSGTCTeamCount; i++ )
	{
		if( teamCount[i] < minPlayers )
		{
			minPlayers = teamCount[i];
			pName = m_szSGTCTeams[i];
		}
	}
	return pName;
}

void CStargateTeamDM::RecountTeams( BOOL bResendInfo )
{
	// ref @0x18df82 — full recount engine: rebuild team_names from
	// m_szTeamList, count alive/focused players, tally mode votes,
	// autobalance, then optionally re-broadcast gmsgTeamInfo.
	m_iSGTCTeamCount = 0;
	char listBuf[512];
	strncpy( listBuf, SGTC_GetTeamList(), sizeof( listBuf ) - 1 );
	listBuf[sizeof( listBuf ) - 1] = '\0';
	char *tok = strtok( listBuf, ";" );
	while( tok && *tok )
	{
		if( GetTeamIndex( tok ) < 0 && m_iSGTCTeamCount < SGTC_MAX_SGTC_TEAMS )
		{
			strncpy( m_szSGTCTeams[m_iSGTCTeamCount], tok, SGTC_TEAMNAME_LEN - 1 );
			m_szSGTCTeams[m_iSGTCTeamCount][SGTC_TEAMNAME_LEN - 1] = '\0';
			m_iSGTCTeamCount++;
		}
		tok = strtok( NULL, ";" );
	}
	if( m_iSGTCTeamCount < 2 )
	{
		m_iSGTCTeamCount = 0;
		SGTC_SetTeamLimit( FALSE );	// ref this+0x14
	}

	// Vote tallies (per-player +0x90c in 1..5 -> slot idx-1).
	int votes[5] = { 0, 0, 0, 0, 0 };
	int voteWinners[5] = { 0, 0, 0, 0, 0 };
	int gCount = 0, tCount = 0, voters = 0, aliveNonObs = 0;
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *plr = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( !plr )
			continue;
		// ref: alive && !observer && pev->team >= 1 && !m_bSgtcBadGuy
		if( plr->IsAlive() && !plr->IsObserver() &&
			(int)plr->pev->team >= 1 && !plr->m_bSgtcBadGuy )
		{
			int team = (int)plr->pev->team;
			if( team == SGTC_ENTTEAM_GOAULD ) gCount++;
			if( team == SGTC_ENTTEAM_TORRI ) tCount++;
			if( plr->m_iBombKeyTimer > 0 && plr->m_iBombKeyTimer < 6 )
			{
				voters++;
				votes[plr->m_iBombKeyTimer - 1]++;
			}
		}
		if( !plr->IsObserver() && (int)plr->pev->team > 0 && !plr->m_bSgtcBadGuy )
			aliveNonObs++;
	}

	// ref: winner = slot with max tally; ties collect into voteWinners.
	int best = -1, bestTally = 0, ties = 0;
	for( int s = 0; s < 5; s++ )
	{
		if( votes[s] == bestTally && bestTally > 0 )
		{
			ties++;
			voteWinners[ties] = s;
		}
		if( bestTally < votes[s] )
		{
			bestTally = votes[s];
			best = s;
			voteWinners[0] = s;
			ties = 0;
		}
	}
	if( best < 0 )
		m_iSgtcModeVote = (int)sgtc_gamemodex;	// ref: ROUND(gamemodex)
	else if( ties < 1 )
		m_iSgtcModeVote = best;
	else
		m_iSgtcModeVote = voteWinners[RANDOM_LONG( 0, ties )];
	if( m_iSgtcModeVote == best && bestTally == aliveNonObs && voters > 0 &&
		m_iSgtcModeVote != sgtc_GameModeSet )
		m_bSgtcVoteEqual = 1;					// ref this+0x774
	if( m_iSgtcModeVote == sgtc_GameModeSet )
		m_iSgtcModeVote = -1;

	sgtc_G_players = gCount;	// ref G_players
	sgtc_T_players = tCount;	// ref T_players

	// ref this+0x758 empty-teams state: 1=G empty, 2=T empty, 3=both, 0 populated.
	int empty = ( gCount < 1 ) ? 1 : 0;
	if( tCount < 1 ) empty = 2;
	if( gCount < 1 && tCount < 1 ) empty = 3;
	if( gCount > 0 && tCount > 0 ) empty = 0;
	int playerEnts = 0;
	CBaseEntity *scan = NULL;
	while( ( scan = UTIL_FindEntityByClassname( scan, "player" ) ) != NULL )
		playerEnts++;
	if( playerEnts < 1 ) empty = 0;
	m_iSgtcEmptyTeams = empty;

	// focused counts (m_iSgtcActiveTeam +0x430, not bad-guy).
	sgtc_T_players_focused = 0;
	sgtc_G_players_focused = 0;
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *plr = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( plr && plr->m_bSgtcBadGuy != 1 )
		{
			// NB (#95): m_iSgtcActiveTeam is an SgtcTeam (1 = Torri, 2 = Goa'uld), so
			// sgtc_G_players_focused counts the Torri side and sgtc_T_players_focused
			// the Goa'uld side. sgtc_G_players / sgtc_T_players and the tickets use
			// pev->team (G = Goa'uld). The ref names are kept; see the autobalance
			// gate in SGTC_CommandClasse.
			if( plr->m_iSgtcActiveTeam == SGTC_TEAM_TORRI ) sgtc_G_players_focused++;
			if( plr->m_iSgtcActiveTeam == SGTC_TEAM_GOAULD ) sgtc_T_players_focused++;
		}
	}

	// ref this+0x768 autobalance tri-state (dead while unregistered mp_autobalance==0).
	float autoBal = CVAR_GET_FLOAT( "mp_autobalance" );
	m_iSgtcAutoBalance = 0;
	if( autoBal > 0.0f )
	{
		if( sgtc_G_players_focused > sgtc_T_players_focused &&
			autoBal < (float)( sgtc_G_players_focused - sgtc_T_players_focused ) )
			m_iSgtcAutoBalance = 1;
		if( sgtc_T_players_focused > sgtc_G_players_focused &&
			autoBal < (float)( sgtc_T_players_focused - sgtc_G_players_focused ) )
			m_iSgtcAutoBalance = 2;
	}

	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *plr = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( !plr )
			continue;
		// ref: net clients get +0x926 latch cleared only when a team GREW.
		if( plr->IsNetClient() &&
			!( sgtc_T_players_focused <= sgtc_T_players_last &&
			   sgtc_G_players_focused <= sgtc_G_players_last ) )
			plr->m_bSgtcPortraitLatch = 0;

		const char *teamName = plr->TeamID();
		int idx = GetTeamIndex( teamName );
		if( idx < 0 && !SGTC_TeamLimit() && m_iSGTCTeamCount < SGTC_MAX_SGTC_TEAMS )
		{
			strncpy( m_szSGTCTeams[m_iSGTCTeamCount], teamName, SGTC_TEAMNAME_LEN - 1 );
			m_szSGTCTeams[m_iSGTCTeamCount][SGTC_TEAMNAME_LEN - 1] = '\0';
			m_iSGTCTeamCount++;
		}
		if( bResendInfo && IsValidTeam( teamName ) )
		{
			MESSAGE_BEGIN( MSG_ALL, gmsgTeamInfo );
				WRITE_BYTE( plr->entindex() );
				WRITE_STRING( teamName );
			MESSAGE_END();
		}
	}
	sgtc_T_players_last = sgtc_T_players_focused;
	sgtc_G_players_last = sgtc_G_players_focused;
}

const char *CStargateTeamBase::SetDefaultPlayerTeam( CBasePlayer *pPlayer )
{
	char *mdls = g_engfuncs.pfnInfoKeyValue(
		g_engfuncs.pfnGetInfoKeyBuffer( pPlayer->edict() ), "model" );
	snprintf( pPlayer->m_szTeamName, TEAM_NAME_LENGTH, "%s", mdls );

	if( pPlayer->m_szTeamName[0] == '\0' || !IsValidTeam( pPlayer->m_szTeamName ) )
		snprintf( pPlayer->m_szTeamName, TEAM_NAME_LENGTH, "%s", TeamWithFewestPlayers() );

	return pPlayer->m_szTeamName;
}

void CStargateTeamDM::ChangePlayerTeam( CBasePlayer *pPlayer, const char *pTeamName, BOOL bKill, BOOL bGib )
{
	// Mirrors CHalfLifeTeamplay::ChangePlayerTeam (kill, keyvalues, HUD fan-out).
	int damageFlags = DMG_GENERIC | ( bGib ? DMG_ALWAYSGIB : DMG_NEVERGIB );
	if( bKill )
	{
		// ref @0x1908c4: this+0xc / this+0x10 set around the forced kill —
		// suppresses DeathNotice and the PlayerKilled stock path.
		m_bDisableDeathMessages = TRUE;
		m_bDisableDeathPenalty = TRUE;
		entvars_t *pevWorld = VARS( INDEXENT( 0 ) );
		pPlayer->TakeDamage( pevWorld, pevWorld, 900, damageFlags );
		m_bDisableDeathMessages = FALSE;
		m_bDisableDeathPenalty = FALSE;
	}
	if( pPlayer->m_szTeamName != pTeamName )
		snprintf( pPlayer->m_szTeamName, TEAM_NAME_LENGTH, "%s", pTeamName );

	int clientIndex = pPlayer->entindex();
	g_engfuncs.pfnSetClientKeyValue( clientIndex,
		g_engfuncs.pfnGetInfoKeyBuffer( pPlayer->edict() ), "model", pPlayer->m_szTeamName );
	g_engfuncs.pfnSetClientKeyValue( clientIndex,
		g_engfuncs.pfnGetInfoKeyBuffer( pPlayer->edict() ), "team", pPlayer->m_szTeamName );

	MESSAGE_BEGIN( MSG_ALL, gmsgTeamInfo );
		WRITE_BYTE( clientIndex );
		WRITE_STRING( pPlayer->m_szTeamName );
	MESSAGE_END();

	MESSAGE_BEGIN( MSG_ALL, gmsgScoreInfo );
		WRITE_BYTE( clientIndex );
		WRITE_SHORT( (int)pPlayer->pev->frags );
		WRITE_SHORT( pPlayer->m_iDeaths );		// ref CStargateTeamDM param+0xdf0
		WRITE_SHORT( pPlayer->m_bSnagInvisLatch == 1 ? SGTC_CLASS_UNAS : pPlayer->m_iSgtcPortraitShown );
		WRITE_SHORT( GetTeamIndex( pPlayer->m_szTeamName ) + 1 );
		WRITE_SHORT( (int)pPlayer->pev->team );	// ref *(pev)+0x170
	MESSAGE_END();
}

edict_t *CStargateTeamDM::SGTC_FindTeamSpawn( const char *pszClassname )
{
	CBaseEntity *pSpot = NULL;
	for( int i = RANDOM_LONG( 1, 5 ); i > 0; i-- )
		pSpot = UTIL_FindEntityByClassname( pSpot, (char *)pszClassname );
	if( !pSpot )
		pSpot = UTIL_FindEntityByClassname( pSpot, (char *)pszClassname );
	CBaseEntity *pFirst = pSpot;
	while( pSpot )
	{
		if( pSpot->pev->origin != Vector( 0, 0, 0 ) )
			return pSpot->edict();
		pSpot = UTIL_FindEntityByClassname( pSpot, (char *)pszClassname );
		if( pSpot == pFirst )
			break;
	}
	return NULL;
}

// build-9: SP-style maps (gateroom) have no info_player_deathmatch and often a
// single info_player_start.  Give bots/players a real, spread origin from the
// start entity first, then from path_corner waypoint nodes, so falling back does
// not pile everyone on one spot (and never leaves a fake client at the engine's
// unspawned 4096 sentinel).
static qboolean SGTC_SpawnSpotOccupied( const Vector &vecOrigin, CBasePlayer *pPlayer )
{
	for( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBaseEntity *pOther = UTIL_PlayerByIndex( i );
		if( !pOther || pOther == pPlayer || !pOther->IsPlayer() )
			continue;
		if( !pOther->IsAlive() )
			continue;
		if( ( pOther->pev->origin - vecOrigin ).Length() < 48.0f )
			return TRUE;
	}
	return FALSE;
}

static edict_t *SGTC_FindFallbackSpawnSpot( CBasePlayer *pPlayer )
{
	edict_t *pFirstStart = NULL;
	for( CBaseEntity *pEnt = UTIL_FindEntityByClassname( NULL, "info_player_start" ); pEnt;
		pEnt = UTIL_FindEntityByClassname( pEnt, "info_player_start" ) )
	{
		if( !pFirstStart )
			pFirstStart = pEnt->edict();
		if( !SGTC_SpawnSpotOccupied( pEnt->pev->origin, pPlayer ) )
			return pEnt->edict();
	}

	// Start is occupied (or absent): spread the next player/bot over the
	// map's path_corner waypoint nodes (gateroom has 14).  Pick by player slot,
	// walk to the next free node; only reuse the occupied start when no node
	// is free (single-spot maps).
	CBaseEntity *pFirst = UTIL_FindEntityByClassname( NULL, "path_corner" );
	if( pFirst )
	{
		int iCount = 0;
		for( CBaseEntity *pEnt = pFirst; pEnt; pEnt = UTIL_FindEntityByClassname( pEnt, "path_corner" ) )
			++iCount;
		const int iStart = ( ENTINDEX( pPlayer->edict() ) - 1 ) % ( iCount > 0 ? iCount : 1 );
		CBaseEntity *pPick = pFirst;
		for( int i = 0; i < iStart && pPick; ++i )
			pPick = UTIL_FindEntityByClassname( pPick, "path_corner" );
		for( int i = 0; i < iCount && pPick; ++i )
		{
			if( !SGTC_SpawnSpotOccupied( pPick->pev->origin, pPlayer ) )
				return pPick->edict();
			pPick = UTIL_FindEntityByClassname( pPick, "path_corner" );
		}
	}
	if( pFirstStart )
		return pFirstStart;

	return NULL;
}

// build-9: SP-style maps (map name does not select an SGTC rules class, so the
// stock CHalfLifeMultiplay/base spawn path runs) all pile onto the single
// info_player_start.  Once a player has a spawn origin, move later arrivals to
// a free start/path_corner node so 7 bots do not share one point (and the DM
// team-spawn maps are untouched: IsTeamplay() short-circuits).
void SGTC_SpawnPostAdjust( CBasePlayer *pPlayer )
{
	if( !pPlayer || !pPlayer->pev )
		return;
	if( g_pGameRules && g_pGameRules->IsTeamplay() )
		return;	// SGTC team DM/CP maps use their filtered spawn points
	if( pPlayer->IsObserver() )
		return;
	const Vector vecOrigin = pPlayer->pev->origin;
	if( vecOrigin.x == 0.0f && vecOrigin.y == 0.0f && vecOrigin.z == 0.0f )
		return;
	if( !SGTC_SpawnSpotOccupied( vecOrigin, pPlayer ) )
		return;

	edict_t *spot = SGTC_FindFallbackSpawnSpot( pPlayer );
	if( !spot || FNullEnt( spot ) )
		return;
	pPlayer->pev->origin = VARS( spot )->origin + Vector( 0, 0, 1 );
	pPlayer->pev->angles = VARS( spot )->angles;
	pPlayer->pev->v_angle = VARS( spot )->angles;
	pPlayer->pev->velocity = g_vecZero;
	pPlayer->pev->fixangle = TRUE;
}

edict_t *CStargateTeamDM::GetPlayerSpawnSpot( CBasePlayer *pPlayer )
{
	// Team-filtered spawn (.so Think/PlayerSpawn use info_player_torri /
	// info_player_goauld; stock EntSelectSpawnPoint only knows DM/start).
	int team = GetTeamIndex( pPlayer->m_szTeamName );
	const char *SpotClass = ( team == 1 ) ? SGTC_TeamSpawnClassname[1] : SGTC_TeamSpawnClassname[0];	// team = GetTeamIndex(): 0 Torri, 1 Goa'uld
	edict_t *spot = SGTC_FindTeamSpawn( SpotClass );
	if( !spot || FNullEnt( spot ) )
		spot = SGTC_FindTeamSpawn( "info_player_deathmatch" );
	if( !spot || FNullEnt( spot ) )
		spot = SGTC_FindFallbackSpawnSpot( pPlayer );	// build-9 SP-map fallback
	if( !spot || FNullEnt( spot ) )
		return CGameRules::GetPlayerSpawnSpot( pPlayer );

	pPlayer->pev->origin = VARS( spot )->origin + Vector( 0, 0, 1 );
	pPlayer->pev->angles = VARS( spot )->angles;
	pPlayer->pev->v_angle = VARS( spot )->angles;
	pPlayer->pev->velocity = g_vecZero;
	pPlayer->pev->fixangle = TRUE;
	return spot;
}

CStargateTeamDM *SGTC_GetTeamDMRules( void )
{
	if( !g_pGameRules || !g_pGameRules->IsTeamplay() )
		return NULL;
	const char *pszDesc = g_pGameRules->GetGameDescription();
	if( !pszDesc )
		return NULL;
	if( !strcmp( pszDesc, "StargateTC-DM" ) || !strcmp( pszDesc, "StargateTC-CTP" ) )
		return (CStargateTeamDM *)g_pGameRules;
	return NULL;
}

BOOL CStargateTeamDM::FPlayerCanChangeInfo( CBasePlayer *pPlayer )
{
	(void)pPlayer;
	return TRUE;
}

void CStargateTeamDM::DeathNotice( CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pInflictor )
{
	if( m_bDisableDeathMessages )	// ref this+0xc gate
		return;
	if( pVictim && pKiller && ( pKiller->flags & FL_CLIENT ) )
	{
		CBasePlayer *pk = (CBasePlayer *)CBaseEntity::Instance( pKiller );
		if( pk && pk != pVictim && PlayerRelationship( pVictim, pk ) == GR_TEAMMATE )
		{
			MESSAGE_BEGIN( MSG_ALL, gmsgDeathMsg );
				WRITE_BYTE( ENTINDEX( ENT( pKiller ) ) );
				WRITE_BYTE( ENTINDEX( pVictim->edict() ) );
				WRITE_STRING( "teammate" );
			MESSAGE_END();
			return;
		}
	}
	CHalfLifeMultiplay::DeathNotice( pVictim, pKiller, pInflictor );
}

void CStargateTeamDM::ClientDisconnected( edict_t *pClient )
{
	CBasePlayer *pPlayer = (CBasePlayer *)CBaseEntity::Instance( pClient );
	if( pPlayer )
		SGTC_ResetPlayerOnDisconnect( pPlayer );
	CHalfLifeMultiplay::ClientDisconnected( pClient );
	if( gmsgSpectator )	// ref @0x1179f2 sends Spectator on disconnect
	{
		MESSAGE_BEGIN( MSG_ALL, gmsgSpectator );
			WRITE_BYTE( ENTINDEX( pClient ) );
			WRITE_BYTE( 0 );
		MESSAGE_END();
	}
	RecountTeams( TRUE );
}

// Team and model half of ClientUserInfoChanged, shared by DM and CP (the code was
// written out in both): an observer drops pev->team; unless the info lock or the
// invis latch is set, pev->team and the "model" key follow m_szTeamName and the
// class in play.
void SGTC_ApplyUserInfoTeam( CBasePlayer *pPlayer )
{
	if( pPlayer->IsObserver() )
		pPlayer->pev->team = 0;						// ref pev+0x1ac = 0

	if( pPlayer->m_iSgtcInfoLock != 1 &&			// ref +0xdc != 1
		pPlayer->m_bSnagInvisLatch != 1 )			// ref +0x4de != 1
	{
		char *infobuf = g_engfuncs.pfnGetInfoKeyBuffer( pPlayer->edict() );	// ref DAT_00238fa8
		if( !strncmp( pPlayer->m_szTeamName, "Goa'uld", 7 ) )
		{
			pPlayer->pev->team = SGTC_ENTTEAM_GOAULD;
			switch( pPlayer->m_iSgtcPortraitShown )				// ref +0xa40
			{
			case SGTC_CLASS_SYSLORD:	g_engfuncs.pfnSetClientKeyValue( pPlayer->entindex(), infobuf, "model", "systemlord" );	break;
			case SGTC_CLASS_ANUBIS:	g_engfuncs.pfnSetClientKeyValue( pPlayer->entindex(), infobuf, "model", "anubis" );		break;
			case SGTC_CLASS_JAFFA:	g_engfuncs.pfnSetClientKeyValue( pPlayer->entindex(), infobuf, "model", "goauld3" );		break;
			case SGTC_CLASS_SERPENT:	g_engfuncs.pfnSetClientKeyValue( pPlayer->entindex(), infobuf, "model", "snakegoauld" );	break;
			case SGTC_CLASS_HORUS:	g_engfuncs.pfnSetClientKeyValue( pPlayer->entindex(), infobuf, "model", "horus" );		break;
			}
		}
		else if( !strncmp( pPlayer->m_szTeamName, "Torri", 5 ) )
		{
			pPlayer->pev->team = SGTC_ENTTEAM_TORRI;
			switch( pPlayer->m_iSgtcPortraitShown )
			{
			case SGTC_CLASS_ONEILL:	g_engfuncs.pfnSetClientKeyValue( pPlayer->entindex(), infobuf, "model", "oneill" );		break;
			case SGTC_CLASS_CARTER:	g_engfuncs.pfnSetClientKeyValue( pPlayer->entindex(), infobuf, "model", "carter" );		break;
			case SGTC_CLASS_JACKSON:	g_engfuncs.pfnSetClientKeyValue( pPlayer->entindex(), infobuf, "model", "jackson" );	break;
			case SGTC_CLASS_TEALC:	g_engfuncs.pfnSetClientKeyValue( pPlayer->entindex(), infobuf, "model", "tealc" );		break;
			case SGTC_CLASS_COMMANDER:	g_engfuncs.pfnSetClientKeyValue( pPlayer->entindex(), infobuf, "model", "commander" );	break;
			}
		}
	}
}

void CStargateTeamDM::ClientUserInfoChanged( CBasePlayer *pPlayer, char *infobuffer )
{
	// ref CStargateTeamDM::ClientUserInfoChanged @0x190288.
	// NOT the stock teamplay body — no IsValidTeam/"Can't change team"
	// rejection; the ref derives pev->team + the "model" key from
	// m_szTeamName/m_iSgtcPortraitShown.
	(void)infobuffer;
	SGTC_ApplyUserInfoTeam( pPlayer );

	// DM-only tail: invis latch (+0x4de) pushes "unasteammate" (DAT_001ebc6c)
	// when the info lock (+0xdc) isn't forced.
	if( pPlayer->m_bSnagInvisLatch != 0 && pPlayer->m_iSgtcInfoLock != 1 )
		g_engfuncs.pfnSetClientKeyValue( pPlayer->entindex(),
			g_engfuncs.pfnGetInfoKeyBuffer( pPlayer->edict() ), "model", "unasteammate" );

	pPlayer->SendAboutMe( (int)pPlayer->pev->team,
		STRING( pPlayer->pev->netname ), pPlayer->entindex() );	// ref: pev+0x1ac, STRING(netname), pev+0x1d0
	RecountTeams( FALSE );

	if( !pPlayer->IsObserver() )
		pPlayer->m_bSgtcPadA3D = 1;					// ref +0xa3d = 1 (DM only)

	UpdateGameMode( pPlayer );						// ref vtable+0x34 tail
}

int CStargateTeamDM::DeadPlayerWeapons( CBasePlayer *pPlayer )
{
	(void)pPlayer;
	return GR_PLR_DROP_GUN_ACTIVE; // .so DeadPlayerWeapons: keep active gun
}

int CStargateTeamDM::DeadPlayerAmmo( CBasePlayer *pPlayer )
{
	(void)pPlayer;
	return GR_PLR_DROP_AMMO_ACTIVE; // .so DeadPlayerAmmo path
}

// Goal / EndRound / NewRound moved to sgtc_round.cpp (ref nm 0x17b650,
// 0x17b77c, 0x184f42) — the stub bodies here were superseded by the full
// round-lifecycle port.

// Think steps shared by the DM and CP Think (the code was written out in both).

// ref: when m_SendPL is armed (both InitHUDs set it) the rules broadcast TeamNames
// with the 28 wpnC cost bytes and disarm the flag.
void CStargateTeamDM::SGTC_ThinkSendTeamNames( void )
{
	if( sgtc_SendPL )
	{
		MESSAGE_BEGIN( MSG_ALL, gmsgTeamNames );
			WRITE_BYTE( m_iSGTCTeamCount );
			for( int i = 0; i < m_iSGTCTeamCount; i++ )
				WRITE_STRING( m_szSGTCTeams[i] );
			for( int i = 0; i < SGTC_NUM_WEAPON_SLOTS; i++ )
				WRITE_BYTE( SGTC_ClampWireByte( (int)( sgtc_wpnC[i]->value + 0.5f ) ) );
		MESSAGE_END();
		sgtc_SendPL = 0;	// ref m_SendPL = 0
	}
}

// Re-send the armor HUD row when the wire bytes or mp_superweapons latch change (#130).
void CStargateTeamDM::SGTC_ThinkResendArmorHud( void )
{
	const int iSuperWire = SGTC_ClampWireByte( (int)( sgtc_cvar_superweapons.value + 0.5f ) );
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pl = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( pl && pl->IsAlive() )
		{
			const int iBar = SGTC_ArmorHudWireByte( pl );
			if( iBar == pl->m_iArmorHudSent && iSuperWire == pl->m_iSupergunsSent )
				continue;
			MESSAGE_BEGIN( MSG_ONE, gmsgArmor, NULL, pl->pev );
				WRITE_BYTE( iBar );
				WRITE_BYTE( iSuperWire );
				WRITE_BYTE( 0 );
				WRITE_BYTE( 0 );
			MESSAGE_END();
			pl->m_iArmorHudSent = iBar;
			pl->m_iSupergunsSent = iSuperWire;
		}
	}
}

// Tail of Think: the mp_timelimit gate, then PlayMusic / endtround rollover /
// timeleft logic, or the intermission when the map limit has elapsed.
void CStargateTeamDM::SGTC_ThinkRoundTail( void )
{
	if( m_bRoundOver )
		return;

	// build-12: ctp_/sg_ maps ship mp_roundforcing 0, so no NewRound runs at map
	// start and the HUD round timer is never armed (SetHudRoundTime is only sent
	// from the NewRound loop / join path).  Start the first round ourselves once
	// at least one player is present; later rounds still go through EndRound.
	static BOOL s_bSgtcFirstRoundStarted = FALSE;
	static char s_szSgtcAutoRoundMap[64] = {0};
	const char *pszAutoRoundMap = STRING( gpGlobals->mapname );
	if( pszAutoRoundMap && strncmp( pszAutoRoundMap, s_szSgtcAutoRoundMap, sizeof( s_szSgtcAutoRoundMap ) - 1 ) )
	{
		strlcpy( s_szSgtcAutoRoundMap, pszAutoRoundMap, sizeof( s_szSgtcAutoRoundMap ) );
		s_bSgtcFirstRoundStarted = FALSE;
	}
	if( !s_bSgtcFirstRoundStarted && m_flSgtcRoundTime <= 0.0f && !g_fGameOver &&
		gpGlobals->time > 1.0f && sgtc_GameModeSet > 0 )
	{
		int nPlayers = 0;
		for( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			if( UTIL_PlayerByIndex( i ) )
			{
				nPlayers = 1;
				break;
			}
		}
		if( nPlayers )
		{
			s_bSgtcFirstRoundStarted = TRUE;
			NewRound();
			return;
		}
	}

	// ref Think @0x1929ce: mp_timelimit outer gate — skip fraglimit, PlayMusic,
	// ticket logic, endtround rollover, and timeleft while map limit elapsed
	// and (mode 0 or post-stinger: mode>=1, hasplayed, time>endtround).
	if( SGTC_ThinkRunsMainRoundBody() )
	{
		if( SGTC_ThinkPlayMusic() )	// mode 0 fraglimit may call +0x108
			return;
		SGTC_ThinkEndtroundRollover();
		SGTC_ThinkRefreshTimeleftCvar();	// ref @0x1929ce local_bc / Think()::last_time
	}
	else
	{
		SGTC_ThinkMpTimelimitResetTickets();
		GoToIntermission();
	}
}

// ref DM Think mid-block (DM-only): superguns armor HUD, GameModeSet latch,
// focused-both latch, +0x774 clear, VoiceGameMgr.Update. Returns TRUE when the
// caller should keep running the shared tail (never FALSE here — reserved).
BOOL CStargateTeamDM::SGTC_ThinkDMCoreLatches( void )
{
	// Per-player dirty check inside SGTC_ThinkResendArmorHud (#130); still run each Think for armor damage.
	SGTC_ThinkResendArmorHud();
	if( sgtc_GameModeSet < 0 )
		sgtc_GameModeSet = (int)( sgtc_gamemodex + 0.5f );
	if( m_iSgtcPrevGameModeSet != sgtc_GameModeSet )
		EndRound();	// ref vfun+0x38
	m_iSgtcSupergunsLatch = (int)( sgtc_cvar_superweapons.value + 0.5f );
	m_iSgtcPrevGameModeSet = sgtc_GameModeSet;

	const int iBothFocused =
		( sgtc_G_players_focused < 1 || sgtc_T_players_focused < 1 ) ? 0 : 1;
	if( m_iSgtcFocusedBoth == 0 && iBothFocused == 1 )
	{
		sgtc_nrcooler = 0;
		EndRound();	// ref vfun+0x38
	}
	if( m_bSgtcVoteEqual != 0 )
	{
		m_bSgtcVoteEqual = 0;
		EndRound();	// ref vfun+0x38 — unanimous mode vote (Think, not NewRound-only)
	}
	m_iSgtcFocusedBoth = iBothFocused;

	// ref: CVoiceGameMgr::Update(gpGlobals->frametime) — DM Think carries the
	// voice-manager tick itself (no CHalfLifeTeamplay::Think in ref).
	g_VoiceGameMgr.Update( gpGlobals->frametime );
	return TRUE;
}

// ref DM Think @0x1934a5 (DM-only): timed key/objective sweep — key-holder
// state -> BOMBCARRIER / LastState text, respawn the objective when no key
// holder and no zone, or when >1 holder (reset all +0xaec, respawn at
// firstplace). Runs while KeyInPlay && KeyTime < now.
void CStargateTeamDM::SGTC_ThinkKeyBlock( void )
{
	if( !( sgtc_KeyInPlay == 1 && sgtc_KeyTime < gpGlobals->time ) )
		return;

	if( sgtc_masterkey == g_vecZero )
	{
		sgtc_KeyTime = gpGlobals->time + 2.0f;
		return;
	}

	// ref DM Think @0x1934a5 — key held (masterkey != 0).
	if( sgtc_GlobalBombState > 0 && sgtc_LastStateI[0] != sgtc_GlobalBombState &&
		sgtc_GlobalBombState == 2 )
		GlobalText( 0, 5 );

	char keyHeld = 0;
	int nKeyHolders = 0;
	char keyCharging = 0;
	CBaseEntity *pScan = NULL;
	while( ( pScan = UTIL_FindEntityByClassname( pScan, "player" ) ) != NULL )
	{
		CBasePlayer *pl = (CBasePlayer *)pScan;
		if( pl->m_iInBombZone <= 0 )
			continue;
		keyHeld = 1;
		nKeyHolders++;
		if( pl->m_iNukeCharge > 5 && pl->m_iNukeCharge < 100 )
			keyCharging = 1;
	}

	if( sgtc_KeyChargeLatch != keyCharging && keyCharging != 1 &&
		( sgtc_LastStateI[0] == sgtc_GlobalBombState || sgtc_GlobalBombState != 2 ) )
		GlobalText( 0, 4 );
	if( sgtc_KeyChargeLatch != keyCharging && keyCharging != 0 )
		GlobalText( 0, 3 );

	sgtc_LastStateI[0] = sgtc_GlobalBombState;
	sgtc_KeyChargeLatch = keyCharging;
	sgtc_GlobalBombState = 0;

	if( (char)sgtc_LastState[0] != keyHeld && keyHeld != 0 )
		GlobalText( 0, 1 );
	if( sgtc_Elmo != 0 )
		GlobalText( 0, 2 );
	sgtc_Elmo = 0;
	sgtc_LastState[0] = keyHeld;

	if( keyHeld != 1 )
	{
		CBaseEntity *pObj = NULL;
		while( ( pObj = UTIL_FindEntityByClassname( pObj, "item_objective" ) ) != NULL )
			keyHeld = 1;
	}
	if( keyHeld != 1 && sgtc_bomby != 1 )
		CBaseEntity::Create( "item_objective", sgtc_LastKeyPlace, g_vecZero, NULL );

	if( nKeyHolders > 1 )
	{
		ALERT( at_console, "GameRule: More than one player has a key!! Total %d\n", nKeyHolders );
		pScan = NULL;
		while( ( pScan = UTIL_FindEntityByClassname( pScan, "player" ) ) != NULL )
		{
			CBasePlayer *pl = (CBasePlayer *)pScan;
			pl->m_iInBombZone = 0;
		}
		CBaseEntity::Create( "item_objective", sgtc_firstplace, g_vecZero, NULL );
	}

	sgtc_KeyTime = gpGlobals->time + 2.0f;
}

// ref DM Think recount block (DM-only): RecountTeams cadence + per-player
// observerless-entity sweep + spawn-camp warn.
void CStargateTeamDM::SGTC_ThinkRecountCamperSweep( void )
{
	if( sgtc_recounttime >= gpGlobals->time )
		return;
	RecountTeams( FALSE );
	sgtc_recounttime = gpGlobals->time + 1.0f;

	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pl = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( !( pl && pl->IsAlive() ) )
			continue;

		// ref: alive, non-observer, no focused team (+0x430 == 0) -> the entity
		// is a stale body: snap it to SGTC_OFFMAP_COORD, clear movetype, remove.
		if( !pl->IsObserver() && pl->m_iSgtcActiveTeam == 0 )
		{
			pl->StartObserver( pl->pev->origin, pl->pev->v_angle );
		}

		// ref camper warn: alive, non-observer, team >= 1, GameModeSet >= 1 and
		// != 2, near the opposing team's info_player_* spawn.
		if( pl->IsObserver() )
			continue;
		if( sgtc_GameModeSet == 2 || pl->pev->team < 1 || sgtc_GameModeSet < 1 )
			continue;

		// Camp near the opposing team's spawn (#115).
		const char *pszSpawn = ( pl->pev->team == SGTC_ENTTEAM_GOAULD )
			? "info_player_torri" : "info_player_goauld";
		CBaseEntity *pSpot = NULL;
		while( ( pSpot = UTIL_FindEntityByClassname( pSpot, pszSpawn ) ) != NULL )
		{
			const int iDist = SGTC_Distance3D( pl->pev->origin, pSpot->pev->origin );
			// vertical distance: |spawn z - player z| (not the larger raw z)
			const int iZ = abs( (int)( pSpot->pev->origin.z + 0.5f ) -
				(int)( pl->pev->origin.z + 0.5f ) );
			if( !( (float)iDist < sgtc_cvar_camperdist.value &&
				(float)iZ < sgtc_cvar_camperdist.value ) )
				continue;

			if( pl->m_bCampZoneLatch != 1 )
			{
				pl->m_bCampZoneLatch = 1;
				pl->m_flCampWarnDeadline =
					gpGlobals->time + (float)(int)( sgtc_cvar_camptime.value + 0.5f );
				pl->m_flCampNextWarn = 0;
			}
			if( pl->m_bCampZoneLatch != 0 &&
				pl->m_flCampWarnDeadline < gpGlobals->time &&
				pl->m_flCampNextWarn < gpGlobals->time )
			{
				pl->CampWarn( 1 );
				pl->m_flCampRearm = gpGlobals->time + 5.0f;
				pl->m_flCampNextWarn = gpGlobals->time + 3.0f;
			}
		}
	}
}

void CStargateTeamDM::Think( void )
{
	// ref CStargateTeamDM::Think @0x1929ce — the .so body is self-contained:
	// no CHalfLifeTeamplay::Think call (voice/timer work is inlined below).
	if( g_fGameOver )
	{
		CHalfLifeMultiplay::Think();
		return;
	}

	// ref: when m_SendPL is armed (both InitHUDs set it) the rules broadcast
	// TeamNames with the 28 wpnC cost bytes and disarm the flag.
	SGTC_ThinkSendTeamNames();	// ref: m_SendPL armed -> TeamNames + wpnC cost bytes

	const BOOL bDM = !SGTC_IsCaptureMode();

	if( bDM )
	{
		// ref: round_official set but tournament fairness / min-4 reject —
		// DM calls its own virtual (+0x120); CP runs its variant post-resetround.
		if( sgtc_round_official != 0 )
		{
			int iAvg = ( sgtc_G_players_focused + sgtc_T_players_focused ) / 2;
			BOOL bFair = sgtc_G_players_focused <= iAvg + 2 &&
				sgtc_T_players_focused <= iAvg + 2;
			if( !bFair || sgtc_G_players_focused < 4 || sgtc_T_players_focused < 4 )
				NewRound();	// ref DM vfun+0x120 (tournament reject — no EndRound score pass)
		}

		SGTC_ThinkDMCoreLatches();
	}

	// ref: resetround pulse (score clear at >1, then EndRound + cvar reset).
	// NOTE: the ref FALLS THROUGH after handling; it does not return.
	float flResetRound = CVAR_GET_FLOAT( "mp_resetround" );
	if( flResetRound > 0.0f )
	{
		if( flResetRound > 1.0f )
		{
			int iTorri = GetTeamIndex( "Torri" );
			int iGoauld = GetTeamIndex( "Goa'uld" );
			if( iTorri >= 0 )
				m_iSGTCTeamScores[iTorri] = 0;
			if( iGoauld >= 0 )
				m_iSGTCTeamScores[iGoauld] = 0;
		}
		sgtc_nrcooler = 0.0f; // P149c — full NewRound on admin reset (p43h)
		EndRound();
		CVAR_SET_FLOAT( "mp_resetround", 0.0f );
	}

	if( bDM )
	{
		SGTC_ThinkKeyBlock();				// ref @0x1934a5
		if( sgtc_classchecktime < gpGlobals->time )
		{
			UpdateClassCount();				// ref vfun+0x118
			sgtc_classchecktime = gpGlobals->time + 5.0f;
		}
		SGTC_ThinkRecountCamperSweep();		// ref recounttime cadence + camper warn
	}

	SGTC_ThinkRoundTail();
}
