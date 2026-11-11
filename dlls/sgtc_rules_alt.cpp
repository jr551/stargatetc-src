// sgtc_rules_alt.cpp — StargateTC rules: the alternate game modes. CStargateUnas,
// CStargateInvis (Unas / Invisible) and CStargateReplicateur.
//
// Split out of sgtc_rules.cpp (#94); code moved as it was.

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


// ===========================================================================
// CStargateUnas / CStargateInvis
// ===========================================================================
CStargateUnas::CStargateUnas( void )
{
	// .so Unas ctor teamlist "Unas;Marine" (fileoff 0x1dc5f9).
	strncpy( m_szSGTCTeams[0], "Unas", SGTC_TEAMNAME_LEN );
	strncpy( m_szSGTCTeams[1], "Marine", SGTC_TEAMNAME_LEN );
	m_iSGTCTeamCount = SGTC_MAX_SGTC_TEAMS;
	RecountTeams( FALSE );
}

void CStargateUnas::RecountTeams( BOOL bResendInfo )
{
	if( !bResendInfo )
		return;
	MESSAGE_BEGIN( MSG_ALL, gmsgTeamNames );
		WRITE_BYTE( m_iSGTCTeamCount );
		for( int i = 0; i < m_iSGTCTeamCount; i++ )
			WRITE_STRING( m_szSGTCTeams[i] );
	MESSAGE_END();
}

void CStargateUnas::ChangePlayerTeam( CBasePlayer *pPlayer, const char *pTeamName, BOOL bKill, BOOL bGib )
{
	// ref @0x198e7a: bKill → world TakeDamage( 900, bGib ? ALWAYSGIB : NEVERGIB );
	// bGib also forces pev->frags = -100. Then m_szTeamName copy, gmsgTeamInfo
	// broadcast, and gmsgScoreInfo {entindex, frags, m_iDeaths, +0xa40, GetTeamIndex+1, 0}.
	int damageFlags = DMG_GENERIC | ( bGib ? DMG_ALWAYSGIB : DMG_NEVERGIB );
	if( bKill )
	{
		entvars_t *pevWorld = VARS( INDEXENT( 0 ) );
		if( bGib )
			pPlayer->pev->frags = -100.0f;
		pPlayer->TakeDamage( pevWorld, pevWorld, 900, damageFlags );
	}
	if( pPlayer->m_szTeamName != pTeamName )
		snprintf( pPlayer->m_szTeamName, TEAM_NAME_LENGTH, "%s", pTeamName );

	MESSAGE_BEGIN( MSG_ALL, gmsgTeamInfo );
		WRITE_BYTE( pPlayer->entindex() );
		WRITE_STRING( pPlayer->m_szTeamName );
	MESSAGE_END();

	MESSAGE_BEGIN( MSG_ALL, gmsgScoreInfo );
		WRITE_BYTE( pPlayer->entindex() );
		WRITE_SHORT( (int)pPlayer->pev->frags );
		WRITE_SHORT( pPlayer->m_iDeaths );		// ref param+0xdf0
		WRITE_SHORT( pPlayer->m_iSgtcPortraitShown );	// ref param+0xa40
		WRITE_SHORT( GetTeamIndex( pPlayer->m_szTeamName ) + 1 );
		WRITE_SHORT( 0 );						// ref param+0x170
	MESSAGE_END();
}

void CStargateUnas::UpdateGameMode( CBasePlayer *pPlayer )
{
	MESSAGE_BEGIN( MSG_ONE, gmsgGameMode, NULL, pPlayer->edict() );
		WRITE_BYTE( 1 );
	MESSAGE_END();
}

edict_t *CStargateUnas::SGTC_FindTeamSpawn( const char *pszClassname )
{
	CBaseEntity *pSpot = NULL;
	for( int i = RANDOM_LONG( 1, 5 ); i > 0; i-- )
		pSpot = UTIL_FindEntityByClassname( pSpot, (char *)pszClassname );
	if( !pSpot )
		pSpot = UTIL_FindEntityByClassname( pSpot, (char *)pszClassname );
	return pSpot ? pSpot->edict() : NULL;
}

void CStargateUnas::PlayerSpawn( CBasePlayer *pPlayer )
{
	// ref @0x19924c spot-check (rea_dump/CStargateUnas__PlayerSpawn.c):
	// "Unas" branch — health/max_health 250.0f, lance+invis, +0x3b0 = 0xc3
	// (BLOOD_COLOR_YELLOW), "Vous etes le Unas, bonne boucherie :-)\n"; NO
	// maxspeed write. "Marine" branch (stricmp, not blanket else) —
	// 9mmhandgun/9mmAR/main/lance/zat/flashbang, GiveAmmo(100,"9mm",48),
	// GiveAmmo(2,"ARgrenades",10), health/max_health 100.0f, +0x3b0 = 0xf7
	// (BLOOD_COLOR_RED), "Vous etes un Marine, bonne chance...\n".
	CHalfLifeMultiplay::PlayerSpawn( pPlayer );
	if( !stricmp( pPlayer->m_szTeamName, "Unas" ) )
	{
		pPlayer->pev->health = 250.0f;		// ref pev+0x160 = 0x437a0000
		pPlayer->pev->max_health = 250.0f;	// ref pev+0x1b0
		pPlayer->GiveNamedItem( "weapon_lance" );
		pPlayer->GiveNamedItem( "weapon_invis" );
		pPlayer->m_iBloodColor = BLOOD_COLOR_YELLOW;	// ref +0x3b0 = 0xc3
		UTIL_SayText( "Vous etes le Unas, bonne boucherie :-)\n", pPlayer );
	}
	else if( !stricmp( pPlayer->m_szTeamName, "Marine" ) )	// ref "Marine", 6-char
	{
		// ref grants weapon_9mmhandgun here, which has no export in the ref
		// (a NULL-ent no-op there). The port gives the M92S sidearm instead.
		pPlayer->GiveNamedItem( "weapon_m92s" );
		pPlayer->GiveNamedItem( "weapon_9mmAR" );
		pPlayer->GiveNamedItem( "weapon_main" );
		pPlayer->GiveNamedItem( "weapon_lance" );
		pPlayer->GiveNamedItem( "weapon_zat" );
		pPlayer->GiveNamedItem( "weapon_flashbang" );
		pPlayer->GiveAmmo( 100, "9mm", 48 );		// ref vcall+0x6c (100, "9mm", 0x30); "9mm" is unregistered (no glock, #111), so a no-op as in the ref
		pPlayer->GiveAmmo( 2, "ARgrenades", 10 );	// ref vcall+0x6c (2, "ARgrenades", 10)
		pPlayer->pev->health = 100.0f;		// ref pev+0x160 = 0x42c80000
		pPlayer->pev->max_health = 100.0f;	// ref pev+0x1b0
		pPlayer->m_iBloodColor = BLOOD_COLOR_RED;	// ref +0x3b0 = 0xf7
		UTIL_SayText( "Vous etes un Marine, bonne chance...\n", pPlayer );
	}
}

void CStargateUnas::Think( void )
{
	CHalfLifeTeamplay::Think();
}

void CStargateUnas::PlayerKilled( CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pInflictor )
{
	CHalfLifeMultiplay::PlayerKilled( pVictim, pKiller, pInflictor );
	RecountTeams( TRUE );
}

void CStargateUnas::DeathNotice( CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pInflictor )
{
	CHalfLifeMultiplay::DeathNotice( pVictim, pKiller, pInflictor );
}

// ref Unas ClientCommand @0x19a33e / Invis @0x16d4c4 — identical shape:
// voice hop -> menuselect (argc>=2 eats arg) -> jointeam (arg1: 1->team0,
// else team1; ChangePlayerTeam vfun+0xec + RecountTeams) -> showmenu
// (Unas: ShowVGUIMenu(10), Invis: ShowVGUIMenu(0xb)) -> return 0.
// One body for both (#124); only the team names and the menu id differ.
BOOL CStargateUnas::SGTC_AltClientCommand( CBasePlayer *pPlayer, const char *pcmd,
	const char *pszTeam1, const char *pszTeamElse, int iShowMenu )
{
	if( g_VoiceGameMgr.ClientCommand( pPlayer, pcmd ) )
		return TRUE;

	if( FStrEq( pcmd, "menuselect" ) )
	{
		if( CMD_ARGC() > 1 )
			(void)atoi( CMD_ARGV( 1 ) );	// ref consumes arg 1 (no effect)
		return TRUE;
	}
	if( FStrEq( pcmd, "jointeam" ) )
	{
		if( CMD_ARGC() > 0 )
		{
			if( atoi( CMD_ARGV( 1 ) ) == 1 )
				strncpy( pPlayer->m_szTeamName, pszTeam1, 16 );
			else
				strncpy( pPlayer->m_szTeamName, pszTeamElse, 16 );
			ChangePlayerTeam( pPlayer, pPlayer->m_szTeamName, 1, 0 );
			RecountTeams( FALSE );
		}
		return TRUE;
	}
	if( FStrEq( pcmd, "showmenu" ) )
	{
		pPlayer->ShowVGUIMenu( iShowMenu );
		return TRUE;
	}
	return FALSE;	// ref: unknown verbs fall out as 0 (no base-class chain)
}

BOOL CStargateUnas::ClientCommand( CBasePlayer *pPlayer, const char *pcmd )
{
	// ref @0x19a33e: jointeam {1->Unas, else->Marine}, showmenu -> ShowVGUIMenu(10).
	return SGTC_AltClientCommand( pPlayer, pcmd, "Unas", "Marine", 10 );
}

BOOL CStargateInvis::ClientCommand( CBasePlayer *pPlayer, const char *pcmd )
{
	// ref @0x16d4c4: jointeam {1->Torri, else->Invisible}, showmenu -> ShowVGUIMenu(0xb).
	return SGTC_AltClientCommand( pPlayer, pcmd, "Torri", "Invisible", 11 );
}

void CStargateUnas::ClientUserInfoChanged( CBasePlayer *pPlayer, char *infobuffer )
{
	char *mdls = g_engfuncs.pfnInfoKeyValue( infobuffer, "model" );
	if( !stricmp( mdls, pPlayer->m_szTeamName ) )
		return;
	if( !IsValidTeam( mdls ) )
	{
		int clientIndex = pPlayer->entindex();
		g_engfuncs.pfnSetClientKeyValue( clientIndex,
			g_engfuncs.pfnGetInfoKeyBuffer( pPlayer->edict() ), "model", pPlayer->m_szTeamName );
		return;
	}
	ChangePlayerTeam( pPlayer, mdls, TRUE, TRUE );
	RecountTeams( TRUE );
}

CStargateInvis::CStargateInvis( void )
{
	// .so Invis ctor teamlist "Torri;Invisible" (fileoff 0x1dad91).
	strncpy( m_szSGTCTeams[0], "Torri", SGTC_TEAMNAME_LEN );
	strncpy( m_szSGTCTeams[1], "Invisible", SGTC_TEAMNAME_LEN );
}

void CStargateInvis::PlayerSpawn( CBasePlayer *pPlayer )
{
	// ref @0x17c44e spot-check (rea_dump/CStargateInvis__PlayerSpawn.c):
	// "Invisible" branch (checked FIRST) — maxspeed 1000.0f (0x447a0000);
	// ToggleVis only when +0x4e9 != 1; "Vous etes invisible... :-)\n".
	// "Torri" branch — ToggleVis when +0x4e9 != 0; maxspeed 400.0f
	// (0x43c80000); 9mmhandgun/m92s/handgrenade/flashbang/lance/zat;
	// GiveAmmo(68,"9mm",48) + GiveAmmo(2,"Hand Grenade",3);
	// "Trouvez l invisible...\n". NO trailing +0x4de=0/extra ToggleVis.
	CHalfLifeMultiplay::PlayerSpawn( pPlayer );
	if( !stricmp( pPlayer->m_szTeamName, "Invisible" ) )
	{
		pPlayer->pev->maxspeed = 1000.0f;	// ref pev+0x210 = 0x447a0000
		if( pPlayer->m_bShieldSprint != 1 )	// ref +0x4e9 != 1
			pPlayer->SGTC_ToggleVis();		// ref CBasePlayer::ToggleVis @0x136454
		UTIL_SayText( "Vous etes invisible... :-)\n", pPlayer );
	}
	else if( !stricmp( pPlayer->m_szTeamName, "Torri" ) )
	{
		if( pPlayer->m_bShieldSprint != 0 )		// ref +0x4e9 != 0
			pPlayer->SGTC_ToggleVis();
		pPlayer->pev->maxspeed = 400.0f;	// ref pev+0x210 = 0x43c80000
		// ref grants weapon_9mmhandgun then weapon_m92s; the first has no
		// export in the ref (a NULL-ent no-op), so only the M92S is given.
		// Granting the M92S twice would only add a second default clip.
		pPlayer->GiveNamedItem( "weapon_m92s" );
		pPlayer->GiveNamedItem( "weapon_handgrenade" );
		pPlayer->GiveNamedItem( "weapon_flashbang" );
		pPlayer->GiveNamedItem( "weapon_lance" );
		pPlayer->GiveNamedItem( "weapon_zat" );
		pPlayer->GiveAmmo( 68, "9mm", 48 );		// ref vcall+0x6c (0x44, "9mm", 0x30); no-op as in the ref (#111)
		pPlayer->GiveAmmo( 2, "Hand Grenade", 3 );	// ref vcall+0x6c (2, "Hand Grenade", 3)
		UTIL_SayText( "Trouvez l invisible...\n", pPlayer );
	}
}

CStargateReplicateur::CStargateReplicateur( void )
{
	// ref CStargateReplicateur::CStargateReplicateur @0x17e364
	m_bDisableDeathMessages = FALSE;
	m_bDisableDeathPenalty = FALSE;
	m_iRepTeamCount = 0;
	memset( m_szRepTeams, 0, sizeof( m_szRepTeams ) );
	memset( m_iRepScores, 0, sizeof( m_iRepScores ) );
	strncpy( m_szRepTeamList, "Replic;Torri", sizeof( m_szRepTeamList ) - 1 );
	m_szRepTeamList[sizeof( m_szRepTeamList ) - 1] = '\0';
	edict_t *pWorld = INDEXENT( 0 );
	if( pWorld && pWorld->v.targetname && CVAR_GET_FLOAT( "mp_teamoverride" ) != 0 )
	{
		const char *pList = STRING( pWorld->v.targetname );
		if( pList && pList[0] )
		{
			strncpy( m_szRepTeamList, pList, sizeof( m_szRepTeamList ) - 1 );
			m_szRepTeamList[sizeof( m_szRepTeamList ) - 1] = '\0';
		}
	}
	m_teamLimit = ( m_szRepTeamList[0] != 0 );
	RecountTeams( FALSE );
}

// ref @0x17de78 — parse m_szRepTeamList into m_szRepTeams, then rescan players.
void CStargateReplicateur::RecountTeams( BOOL bResendInfo )
{
	m_iRepTeamCount = 0;
	char teamlist[512];
	strncpy( teamlist, m_szRepTeamList, sizeof( teamlist ) - 1 );
	teamlist[sizeof( teamlist ) - 1] = 0;
	for( char *pName = strtok( teamlist, ";" ); pName && *pName;
		 pName = strtok( NULL, ";" ) )
	{
		if( GetTeamIndex( pName ) < 0 && m_iRepTeamCount < 8 )
		{
			strncpy( m_szRepTeams[m_iRepTeamCount], pName, 15 );
			m_szRepTeams[m_iRepTeamCount][15] = '\0';
			m_iRepTeamCount++;
		}
	}
	if( m_iRepTeamCount < 2 )
	{
		m_iRepTeamCount = 0;
		m_teamLimit = FALSE;
	}
	memset( m_iRepScores, 0, sizeof( m_iRepScores ) );

	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *plr = UTIL_PlayerByIndex( i );
		if( !plr )
			continue;
		const char *pTeam = plr->TeamID();
		int idx = GetTeamIndex( pTeam );
		if( idx < 0 && !m_teamLimit && m_iRepTeamCount < 8 )
		{
			idx = m_iRepTeamCount;
			m_iRepScores[idx] = 0;
			strncpy( m_szRepTeams[m_iRepTeamCount], pTeam, 15 );
			m_szRepTeams[m_iRepTeamCount][15] = '\0';
			m_iRepTeamCount++;
		}
		if( idx >= 0 )
			m_iRepScores[idx] += (int)plr->pev->frags;
		if( bResendInfo && IsValidTeam( pTeam ) )
		{
			MESSAGE_BEGIN( MSG_ALL, gmsgTeamInfo );
				WRITE_BYTE( plr->entindex() );
				WRITE_STRING( pTeam );
			MESSAGE_END();
		}
	}
}

int CStargateReplicateur::GetTeamIndex( const char *pTeamName )
{
	if( pTeamName && *pTeamName )
		for( int t = 0; t < m_iRepTeamCount; t++ )
			if( !stricmp( m_szRepTeams[t], pTeamName ) )
				return t;
	return -1;
}

BOOL CStargateReplicateur::IsValidTeam( const char *pTeamName )
{
	if( !m_teamLimit )
		return TRUE;
	return GetTeamIndex( pTeamName ) != -1;
}

// ref @0x17d998.
void CStargateReplicateur::ChangePlayerTeam( CBasePlayer *pPlayer,
	const char *pTeamName, BOOL bKill, BOOL bGib )
{
	int clientIndex = pPlayer->entindex();
	int damageFlags = DMG_GENERIC | ( bGib ? DMG_ALWAYSGIB : DMG_NEVERGIB );
	if( bKill )
	{
		m_bDisableDeathMessages = TRUE;	// ref this+0xc
		m_bDisableDeathPenalty = TRUE;	// ref this+0x10
		entvars_t *pevWorld = VARS( INDEXENT( 0 ) );
		pPlayer->TakeDamage( pevWorld, pevWorld, 900, damageFlags ); // 0x44610000
		m_bDisableDeathMessages = FALSE;
		m_bDisableDeathPenalty = FALSE;
	}
	snprintf( pPlayer->m_szTeamName, TEAM_NAME_LENGTH, "%s", pTeamName );
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
		WRITE_SHORT( pPlayer->m_iDeaths );		// ref param_1+0xdf0
		WRITE_SHORT( 0 );							// ref param_1+0xa40
		WRITE_SHORT( GetTeamIndex( pPlayer->m_szTeamName ) + 1 );
		WRITE_SHORT( (int)pPlayer->pev->team );	// ref *(param_1+4)+0x170
	MESSAGE_END();
}

// ref @0x17e19a — menuselect drains, jointeam: 1=Replic else Torri.
BOOL CStargateReplicateur::ClientCommand( CBasePlayer *pPlayer, const char *pcmd )
{
	if( SGTC_HandleClientCommand( pPlayer, pcmd ) )
		return TRUE;

	if( g_VoiceGameMgr.ClientCommand( pPlayer, pcmd ) )
		return TRUE;
	if( FStrEq( pcmd, "menuselect" ) )
	{
		if( CMD_ARGC() >= 2 )
			pPlayer->SGTC_MenuselectBombKey( atoi( CMD_ARGV( 1 ) ) );
		return TRUE;
	}
	if( FStrEq( pcmd, "jointeam" ) )
	{
		if( CMD_ARGC() > 0 )
		{
			int team = atoi( CMD_ARGV( 1 ) );
			snprintf( pPlayer->m_szTeamName, TEAM_NAME_LENGTH, "%s", team == 1 ? "Replic" : "Torri" );
			char text[64];
			snprintf( text, sizeof( text ), "you joined the %s team\n", pPlayer->m_szTeamName );
			ClientPrint( pPlayer->pev, HUD_PRINTNOTIFY, text );
			ChangePlayerTeam( pPlayer, pPlayer->m_szTeamName, TRUE, FALSE );
			RecountTeams( FALSE );
		}
		return TRUE;
	}
	if( FStrEq( pcmd, SGTC_CMD_RADIO ) )	// ref TeamDM @0x18149b / TeamCP @0x173fe4: MSG_ONE Radio + CMD_ARGV(1)
	{
		if( CMD_ARGC() > 1 )
		{
			int iMsg = atoi( CMD_ARGV( 1 ) );
			if( iMsg < 0 )
				iMsg = 0;
			if( iMsg > 255 )
				iMsg = 255;
			MESSAGE_BEGIN( MSG_ONE, gmsgRadio, NULL, pPlayer->edict() );
				WRITE_BYTE( iMsg );
				WRITE_BYTE( pPlayer->entindex() );
			MESSAGE_END();
		}
		return TRUE;
	}
	return FALSE;
}


void CStargateReplicateur::PlayerSpawn( CBasePlayer *pPlayer )
{
	// ref @0x17dc7e (rea_dump/CStargateReplicateur__PlayerSpawn.c): base spawn,
	// +0xca8 &= ~0xb, then shrink the "Replic" team hull.
	CHalfLifeMultiplay::PlayerSpawn( pPlayer );
	pPlayer->m_iSgtcFlagsCa8 &= ~0xB;	// ref param_1+0xca8 &= 0xfffffff4
	if( !stricmp( pPlayer->m_szTeamName, "Replic" ) ) // ref rodata 0x1eade0
	{
		pPlayer->pev->view_ofs = Vector( 0, 0, 6 );	// +0x174
		pPlayer->pev->fov = 110.0f;			// +0x214 = 0x42dc0000
		pPlayer->pev->movetype = 14;			// +0x108 (SGTC custom)
		UTIL_SetSize( pPlayer->pev,
			Vector( -16, -16, -20 ), Vector( 16, 16, 20 ) );
	}
	else
	{
		pPlayer->pev->view_ofs = Vector( 0, 0, 28 );	// standard eye height
		pPlayer->pev->fov = 0;				// +0x214 = 0
		pPlayer->pev->movetype = MOVETYPE_WALK;		// +0x108 = 3
		UTIL_SetSize( pPlayer->pev,
			VEC_HULL_MIN, VEC_HULL_MAX );
	}
}
