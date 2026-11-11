// sgtc_rules_msg.cpp — StargateTC rules: user-message registration, wire
// helpers and the HUD/scoreboard message writers (TeamNames, InitHUD,
// UpdateGameMode, GlobalText, AddPortrait).
//
// Split out of sgtc_rules.cpp (#94); code moved as it was. The env_* particle
// payload writers stay with their entities in sgtc_entities_env.cpp.

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
#include "sgtc_sniper.h"	// SGTC_Sniper_RegisterMessages (SgtcHit)


#if !defined( _WIN32 ) && defined( __GNUC__ )
#pragma GCC visibility push( default )
#endif

// ---------------------------------------------------------------------------
// User messages. Header-owned (env/radar) + file-global HUD/round messages the
// rules classes send. Names are the exact BSS gmsg* symbols of
// ref/stc_i386.so (verified with `nm -C --defined-only ref/stc_i386.so`);
// they are deliberately NOT static so hl.so exports them, exactly as the
// reference does. Wire names + sizes are decoded from the ref
// LinkUserMessages() REG_USER_MSG table @0x132d44 (push imm before each
// `call *%edx`): -1 = variable-length, 0..n = fixed byte size. Exception:
// EnvFount (43) is the port writer's count, not re-read from the ref (#123).
// ---------------------------------------------------------------------------
int gmsgRadarBlip = 0;
int gmsgEnvParticule = 0;
int gmsgEnvRain = 0;
int gmsgEnvFountain = 0;
int gmsgEnvNewLens = 0;
int gmsgEnvAddRing = 0;
int gmsgChangeParticuleState = 0;
int gmsgProgress = 0;
int gmsgGtickets = 0;
int gmsgTtickets = 0;
int gmsgTicketMode = 0;
int gmsgHudInfoMode = 0;
int gmsgConcuss = 0;
int gmsgHasKey = 0;
int g_iSgtcMapType = 0;
int gmsgStamina = 0;
int gmsgSubModel = 0;
int gmsgArmor = 0;
int gmsgSpectator = 0;
int gmsgAddPortrait = 0;
int gmsgAboutMe = 0;
int gmsgTogglePortrait = 0;
int gmsgReset = 0;
int gmsgNewRnd = 0;
int gmsgSetHudRoundTime = 0;
int gmsgVGUIMenu = 0;
int gmsgHideVGUI = 0;
int gmsgZatTouch = 0;
int gmsgRadio = 0;

#if !defined( _WIN32 ) && defined( __GNUC__ )
#pragma GCC visibility pop
#endif

void SGTC_LinkUserMessages_Prefix( void )
{
	if( gmsgNewRnd )
		return;
	gmsgRadarBlip = REG_USER_MSG( "Blip", -1 );
	gmsgProgress = REG_USER_MSG( "Progress", -1 );
	gmsgGtickets = REG_USER_MSG( "Gtickets", 1 );
	gmsgTtickets = REG_USER_MSG( "Ttickets", 1 );
	gmsgTicketMode = REG_USER_MSG( "TicketMode", 1 );
	gmsgHudInfoMode = REG_USER_MSG( "HudInfoMode", 1 );
	gmsgConcuss = REG_USER_MSG( "Concuss", 2 );
	gmsgHasKey = REG_USER_MSG( "HasKey", 2 );
	gmsgStamina = REG_USER_MSG( "Stamina", 1 );
	gmsgSubModel = REG_USER_MSG( "SubModel", 1 );
	gmsgArmor = REG_USER_MSG( "Armor", 4 );
	gmsgSpectator = REG_USER_MSG( "Spectator", 2 );
	gmsgAddPortrait = REG_USER_MSG( "AddPortrait", -1 );
	gmsgAboutMe = REG_USER_MSG( "AboutMe", 5 );
	gmsgTogglePortrait = REG_USER_MSG( "TogglePor", 0 );
	gmsgReset = REG_USER_MSG( "Reset", -1 );
	gmsgNewRnd = REG_USER_MSG( "NewRnd", 0 );
}

void SGTC_LinkUserMessages_Suffix( void )
{
	if( gmsgRadio )
		return;
	gmsgVGUIMenu = REG_USER_MSG( "VGUIMenu", 1 );
	gmsgHideVGUI = REG_USER_MSG( "HideVGUI", 0 );
	gmsgSetHudRoundTime = REG_USER_MSG( "SetRndTim", 3 );
	gmsgZatTouch = REG_USER_MSG( "ZatTouch", 0 );
	gmsgEnvParticule = REG_USER_MSG( "EnvFull", 47 );
	gmsgEnvRain = REG_USER_MSG( "EnvRain", 15 );
	gmsgEnvFountain = REG_USER_MSG( "EnvFount", 43 );	// #123: 43 is the port writer's count, not read from the ref (older notes said 45)
	gmsgEnvNewLens = REG_USER_MSG( "NewLens", 10 );
	gmsgEnvAddRing = REG_USER_MSG( "AddRing", -1 );
	gmsgChangeParticuleState = REG_USER_MSG( "ChgPartSt", 2 );
	gmsgRadio = REG_USER_MSG( "Radio", 2 );

	// "SgtcHit" (2 bytes) lives with the sniper server module; it registers
	// behind the same once-only guard as the messages above.
	SGTC_Sniper_RegisterMessages();
}

int SGTC_ClampWireByte( int v )
{
	if( v < 0 )
		v = 0;
	if( v > 255 )
		v = 255;
	return v;
}

int SGTC_Rules_roundf( float v )	// ref (int)ROUND(float) byte conversion
{
	return (int)( v >= 0.0f ? v + 0.5f : v - 0.5f );
}

int SGTC_ArmorHudWireByte( CBasePlayer *pPlayer )
{
	int iCap = pPlayer->m_iSnagArmorCap;
	if( iCap <= 0 )
		iCap = 1;
	return SGTC_ClampWireByte( SGTC_Rules_roundf(
		pPlayer->pev->armorvalue * 100.0f / (float)iCap ) );
}

// Portrait/model names from InitHUD disasm (oneill..unas, 11 names).
static const char *const s_SGTCPortraits[] =
{
	"oneill", "carter", "jackson", "tealc", "commander",
	"systemlord", "anubis", "goauld3", "snakegoauld", "horus", "unas",
};
#define SGTC_NUM_PORTRAITS ( sizeof( s_SGTCPortraits ) / sizeof( s_SGTCPortraits[0] ) )

void CStargateTeamBase::SGTC_SendTeamNames( CBasePlayer *pPlayer )
{
	MESSAGE_BEGIN( MSG_ONE, gmsgTeamNames, NULL, pPlayer->edict() );
		WRITE_BYTE( m_iSGTCTeamCount );
		for( int i = 0; i < m_iSGTCTeamCount; i++ )
			WRITE_STRING( m_szSGTCTeams[i] );
		for( int z = 0; z < SGTC_NUM_WEAPON_SLOTS; z++ )
			WRITE_BYTE( 0 );	// ref: TeamNames carries a 28-byte pad tail
	MESSAGE_END();
}

static int SGTC_PortraitSprId( CBasePlayer *pPlayer );
static void SGTC_EmitAddPortrait( int msgDest, edict_t *to, CBasePlayer *subject );

void CStargateTeamDM::InitHUD( CBasePlayer *pPlayer )
{
	// ref @0x190b54 — zero the two 5-slot tables, clear portrait sel, team +
	// team-name, stock Multiplay InitHUD, then MSG_ONE TeamNames with 28
	// trailing zero bytes, RecountTeams(0), ShowVGUIMenu(11),
	// SetHudRoundTime(0,0), per-player TeamInfo/ScoreInfo to the joiner.
	static int s_SgtcHudInitA[5];	// ref this+0x438/+0xa44-adjacent tables
	static int s_SgtcHudInitB[5];
	for( int t = 0; t < 5; t++ ) { s_SgtcHudInitA[t] = 0; s_SgtcHudInitB[t] = 0; }
	pPlayer->m_iSgtcPortraitSel = 0;	// ref +0xa44
	pPlayer->m_iSgtcTeam = 0;			// ref +0x438
	pPlayer->m_iSgtcInfoLock = 0;		// ref +0xdc — info lock off at connect
	pPlayer->m_szTeamName[0] = '\0';	// ref +0xe08 strncpy
	SGTC_ResetPlayerOnConnect( pPlayer );
	CHalfLifeMultiplay::InitHUD( pPlayer );

	// ref: TeamNames = BYTE num_teams + STRING names + 28 zero bytes.
	SGTC_SendTeamNames( pPlayer );

	RecountTeams( FALSE );
	pPlayer->ShowVGUIMenu( 11 );	// ref DM InitHUD menu id
	// build-12: a joiner during a live round must see the remaining HUD round
	// timer; remaining seconds (not the full round length) so the client's
	// "received now" start timestamp shows the right countdown.
	if( m_flSgtcRoundTime > 0.0f && gpGlobals->time < m_flSgtcRoundEnd )
		pPlayer->SetHudRoundTime( (int)( m_flSgtcRoundEnd - gpGlobals->time ), 1 );
	else
		pPlayer->SetHudRoundTime( 0, 0 );
	pPlayer->m_flSgtcMenuDelay = gpGlobals->time + 3.0f;	// ref rodata +3.0

	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *plr = UTIL_PlayerByIndex( i );
		if( !plr || !IsValidTeam( plr->TeamID() ) )
			continue;
		MESSAGE_BEGIN( MSG_ONE, gmsgTeamInfo, NULL, pPlayer->edict() );
			WRITE_BYTE( plr->entindex() );
			WRITE_STRING( plr->TeamID() );
		MESSAGE_END();
		// ref gmsgScoreInfo (InitHUD variant): frags/deaths sent as 0.
		CBasePlayer *pPlr = (CBasePlayer *)plr;
		MESSAGE_BEGIN( MSG_ONE, gmsgScoreInfo, NULL, pPlayer->edict() );
			WRITE_BYTE( plr->entindex() );
			WRITE_SHORT( 0 );
			WRITE_SHORT( 0 );
			WRITE_SHORT( pPlr->m_bSnagInvisLatch == 1 ? SGTC_CLASS_UNAS : pPlr->m_iSgtcPortraitShown );
			WRITE_SHORT( GetTeamIndex( plr->TeamID() ) + 1 );
			WRITE_SHORT( (int)plr->pev->team );
		MESSAGE_END();
		// ref AddHudPortrait: full client layout, about this player, to the joiner.
		SGTC_EmitAddPortrait( MSG_ONE, pPlayer->edict(), (CBasePlayer *)plr );
	}
	if( sgtc_G_players_focused + sgtc_T_players_focused < 1 )
		pPlayer->StartDeathCam();
	sgtc_SendPL = 1;	// ref m_SendPL = 1 (DM InitHUD @0x191238)
}

void CStargateTeamCP::InitHUD( CBasePlayer *pPlayer )
{
	// ref @0x1833f2 — DM InitHUD variant minus ShowVGUIMenu: clears +0x3e3,
	// portrait-shown=1, portrait-sel/team/env-stage=0, empty team-name, stock
	// Multiplay InitHUD, MSG_ONE TeamNames + 28 pad bytes, RecountTeams(0),
	// SetHudRoundTime(0,0), per-player TeamInfo, self ScoreInfo, portrait
	// fan-out, StartDeathCam + mode-check when unfocused, menu delay +3s.
	pPlayer->m_bSgtcPad3e3 = 0;			// ref +0x3e3
	// Connect-observer latch reset: ClientPutInServer->Spawn() leaves one
	// DEAD_NO (alive) frame before InitHUD->StartDeathCam flips to observer,
	// and PlayerThink latches m_bSgtcRoundAlive=1 (+0x3e4) off it. That
	// permanently suppresses the menu-11 dead-tick emit (PlayerThink gate
	// `m_bSgtcRoundAlive != 1`). The ref connect is a pure observer (no alive
	// frame), so the latch must be cleared here — a fresh connect was never
	// alive this round.
	pPlayer->m_bSgtcRoundAlive = 0;	// ref +0x3e4 seen-alive latch
	SGTC_ResetPlayerOnConnect( pPlayer );
	pPlayer->m_iSgtcPortraitShown = 1;	// ref +0xa40
	pPlayer->m_iSgtcPortraitSel = 0;	// ref +0xa44
	pPlayer->m_iSgtcTeam = 0;			// ref +0x438
	pPlayer->m_iSgtcEnvStage = 0;		// ref +0xc90
	pPlayer->m_szTeamName[0] = '\0';	// ref +0xe08 strncpy
	CHalfLifeMultiplay::InitHUD( pPlayer );

	SGTC_SendTeamNames( pPlayer );	// MSG_ONE TeamNames + 28 pad bytes

	RecountTeams( FALSE );
	// build-12: mid-round joiners get the remaining HUD round timer (mode 1).
	if( m_flSgtcRoundTime > 0.0f && gpGlobals->time < m_flSgtcRoundEnd )
		pPlayer->SetHudRoundTime( (int)( m_flSgtcRoundEnd - gpGlobals->time ), 1 );
	else
		pPlayer->SetHudRoundTime( 0, 0 );
	// P149d: TicketMode after cap type is known (connect before first NewRound).
	if( m_iSgtcCPTypeLatch < 0 )
	{
		SGTC_Rules_ReadCapInfo();
		m_iSgtcCPTypeLatch = sgtc_type;
	}
	MESSAGE_BEGIN( MSG_ONE, gmsgTicketMode, NULL, pPlayer->edict() );
		WRITE_BYTE( ( sgtc_type < 2 || sgtc_type == 4 ) ? 1 : 0 );
	MESSAGE_END();

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

	// ref: ScoreInfo for the JOINER only (self row, zeroed frags/deaths).
	MESSAGE_BEGIN( MSG_ONE, gmsgScoreInfo, NULL, pPlayer->edict() );
		WRITE_BYTE( pPlayer->entindex() );
		WRITE_SHORT( 0 );
		WRITE_SHORT( 0 );
		WRITE_SHORT( pPlayer->m_iSgtcPortraitShown );
		WRITE_SHORT( GetTeamIndex( pPlayer->m_szTeamName ) + 1 );
		WRITE_SHORT( (int)pPlayer->pev->team );
	MESSAGE_END();

	// ref AddHudPortrait(param6=1): MSG_ONE to the JOINER carrying each
	// valid team player's portrait (entindex + team name) — same as DM.
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *plr = UTIL_PlayerByIndex( i );
		if( plr && IsValidTeam( plr->TeamID() ) )
		{
			SGTC_EmitAddPortrait( MSG_ONE, pPlayer->edict(), (CBasePlayer *)plr );
		}
	}

	pPlayer->StartDeathCam();
	// ref: if( T_players_focused + G_players_focused < 1 ) calls gamerules
	// vtable+0x38 (mode-check); port has no mapped equivalent and the gate
	// only fires before any client joins, so it is inert in play.
	if( sgtc_T_players_focused + sgtc_G_players_focused < 1 )
	{
	}
	pPlayer->m_flSgtcMenuDelay = gpGlobals->time + 3.0f;	// ref +0x4b0 = now+3.0
	sgtc_SendPL = 1;	// ref m_SendPL
}

void CStargateTeamDM::UpdateGameMode( CBasePlayer *pPlayer )
{
	MESSAGE_BEGIN( MSG_ONE, gmsgGameMode, NULL, pPlayer->edict() );
		WRITE_BYTE( 1 ); // teamplay
	MESSAGE_END();
	MESSAGE_BEGIN( MSG_ONE, gmsgTicketMode, NULL, pPlayer->edict() );
		WRITE_BYTE( 0 ); // DM: no tickets
	MESSAGE_END();
}

void CStargateTeamCP::UpdateGameMode( CBasePlayer *pPlayer )
{
	// ref CP tail uses PlayerSpawn TicketMode byte (type<2 || type==4), not DM's 0.
	SGTC_Rules_ReadCapInfo(); // P150: keep sgtc_type fresh (post-NewRound zero+read)
	MESSAGE_BEGIN( MSG_ONE, gmsgGameMode, NULL, pPlayer->edict() );
		WRITE_BYTE( 1 );
	MESSAGE_END();
	MESSAGE_BEGIN( MSG_ONE, gmsgTicketMode, NULL, pPlayer->edict() );
		WRITE_BYTE( ( sgtc_type < 2 || sgtc_type == 4 ) ? 1 : 0 );
	MESSAGE_END();
}

void CStargateTeamDM::GlobalText( int iTeam, int iText )
{
	// ref @0x17c222: alive players only (vtable+0xa8 == IsAlive);
	// iTeam==0 -> all alive; else pev->team (+0x1ac) == iTeam;
	// send via SendGlobalTextIndex (not a SayText string).
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( !pPlayer || !pPlayer->IsAlive() )
			continue;
		if( iTeam != 0 && (int)pPlayer->pev->team != iTeam )
			continue;
		pPlayer->SendGlobalTextIndex( iText );
	}
}

// ref client MsgFunc_AddPortrait @0x100768d4 reads
// SHORT spr, LONG hpPct, SHORT team, SHORT aux, SHORT ent,
// SHORT hp0, SHORT hp1, SHORT hp2, STRING name, BYTE colorId.
// Team 2 tints the radar green; any other value is red.
static int SGTC_PortraitSprId( CBasePlayer *pPlayer )
{
	int cls = pPlayer->m_iSgtcPortraitShown;
	if( cls < SGTC_CLASS_FIRST || cls > SGTC_CLASS_LAST )
		cls = SGTC_CLASS_FIRST;
	// ref client FUN_10070a66 / DrawEntry FUN_100736d0: avatar ids are 1-based
	// (gate 0 < id < 0xb); case 1..5 Tau'ri, 6..10 Goa'uld (class 1 → oneill).
	return ( (int)pPlayer->pev->team == SGTC_ENTTEAM_GOAULD ) ? cls + SGTC_NUM_CLASSES : cls;
}

static void SGTC_EmitAddPortrait( int msgDest, edict_t *to, CBasePlayer *subject )
{
	int hp = (int)subject->pev->health;
	if( hp < 0 )
		hp = 0;
	if( hp > 100 )
		hp = 100;
	const char *name = STRING( subject->pev->netname );
	if( !name || !name[0] )
		name = subject->m_szTeamName[0] ? subject->m_szTeamName : "player";

	int sprId = SGTC_PortraitSprId( subject );	// ref own-cell + slot avatar id

	MESSAGE_BEGIN( msgDest, gmsgAddPortrait, NULL, to );
		WRITE_SHORT( sprId );
		WRITE_LONG( hp );
		WRITE_SHORT( (int)subject->pev->team );
		WRITE_SHORT( subject->m_iSgtcPortraitShown );
		WRITE_SHORT( subject->entindex() );
		WRITE_SHORT( hp );
		WRITE_SHORT( 100 );
		// ref hp2 is the packed portrait colour/effect id (zat/drugs/snag tints).
		// The port never derived that packing; sending armorvalue here made the
		// client decode armour as effect bits and draw a stray magenta 'drugs'
		// (syringe) overlay.  Armour is carried by the Armor usermsg instead.
		WRITE_SHORT( 0 );
		WRITE_STRING( name );
		WRITE_BYTE( sprId );
	MESSAGE_END();
}

void CStargateTeamDM::AddPortrait( CBasePlayer *pPlayer )
{
	SGTC_EmitAddPortrait( MSG_ONE, pPlayer->edict(), pPlayer );
}

void CStargateTeamDM::StartPortrait( CBasePlayer *pPlayer )
{
	SGTC_EmitAddPortrait( MSG_ONE, pPlayer->edict(), pPlayer );
}

void CStargateTeamDM::RedrawPortrait( CBasePlayer *pPlayer )
{
	// ref CStargateTeamCP::RedrawPortrait @0x17fc58 (DM @0x18cc54): when
	// +0x462 (m_bSgtcBadGuy) is clear, map client "model" to portrait id 1..12
	// and AddHudPortrait(MSG_ALL).
	if( pPlayer->m_bSgtcBadGuy )
		return;

	char *infobuf = g_engfuncs.pfnGetInfoKeyBuffer( pPlayer->edict() );
	const char *model = g_engfuncs.pfnInfoKeyValue( infobuf, "model" );
	int sprId = 12;
	if( model && model[0] )
	{
		for( int i = 0; i < (int)SGTC_NUM_PORTRAITS; i++ )
		{
			if( !strcmp( model, s_SGTCPortraits[i] ) )
			{
				sprId = i;	// client avatar table is 0-based (0 = oneill)
				break;
			}
		}
	}

	int hp = (int)pPlayer->pev->health;
	if( hp < 0 )
		hp = 0;
	if( hp > 100 )
		hp = 100;
	const char *name = STRING( pPlayer->pev->netname );
	if( !name || !name[0] )
		name = pPlayer->m_szTeamName[0] ? pPlayer->m_szTeamName : "player";

	MESSAGE_BEGIN( MSG_ALL, gmsgAddPortrait, NULL );
		WRITE_SHORT( sprId );
		WRITE_LONG( hp );
		WRITE_SHORT( (int)pPlayer->pev->team );
		WRITE_SHORT( pPlayer->m_iSgtcPortraitShown );
		WRITE_SHORT( pPlayer->entindex() );
		WRITE_SHORT( hp );
		WRITE_SHORT( 100 );
		WRITE_SHORT( (int)pPlayer->pev->armorvalue );
		WRITE_STRING( name );
		WRITE_BYTE( sprId );
	MESSAGE_END();
}
