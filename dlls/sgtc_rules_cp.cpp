// sgtc_rules_cp.cpp — StargateTC rules: CStargateTeamCP (capture/ticket mode).
// Constructor, spawn spot, userinfo and Think (with the type-4 attack countdown).
// The CP PlayerSpawn is in sgtc_rules_spawn.cpp, ClientCommand in
// sgtc_rules_cmd.cpp, InitHUD/UpdateGameMode in sgtc_rules_msg.cpp,
// FPlayerCanRespawn in sgtc_rules_class.cpp.
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


static int SGTC_FocusedPresenceBucket( void )
{
	// ref CStargateTeamCP::Think @0x1853e8 local_84 from G/T_players_focused.
	if( sgtc_G_players_focused < 1 || sgtc_T_players_focused < 1 )
	{
		if( sgtc_G_players_focused < 1 && sgtc_T_players_focused < 1 )
			return 0;
		return 1;
	}
	return 2;
}

void CStargateTeamCP::SGTC_Type4TicketSideReset( void )
{
	if( sgtc_type != 4 )
		return;
	if( m_iSgtcCPSide == 1 )
		InverseSpawns();
	m_iSgtcCPSide = 0;
	sgtc_G_tickets = 0;
	sgtc_T_tickets = 0;
}

// ref CStargateTeamCP::Think @0x17734a..0x177514 (type 4): while
// roundstart < time <= roundstart+mp_attackcountdown and time >= rules+0x76c
// beep gate, EMIT "ambience/attack_countdown.wav" to every player (chan 6,
// vol 1.0, attn 0.0 @0x177420).
static void SGTC_Type4AttackCountdownSounds( void )
{
	if( sgtc_type != 4 )
		return;

	const float flDur = sgtc_cvar_attackcountdown.value;
	const float flStart = sgtc_roundstart;
	const float flEnd = flStart + flDur;
	const float t = gpGlobals->time;

	if( t <= flStart || t > flEnd )
		return;

	static float s_flBeepGate = 0.0f;
	static float s_flLatchRoundStart = -1.0f;
	if( s_flLatchRoundStart != flStart )
	{
		s_flLatchRoundStart = flStart;
		s_flBeepGate = 0.0f;
	}

	if( s_flBeepGate > 0.0f && t < s_flBeepGate )
		return;

	CBaseEntity *pScan = NULL;
	while( ( pScan = UTIL_FindEntityByClassname( pScan, "player" ) ) != NULL )
	{
		if( !pScan->IsPlayer() )
			continue;
		EMIT_SOUND( ENT( pScan->pev ), CHAN_STATIC, "ambience/attack_countdown.wav",
			1.0f, 0.0f );
	}

	const float flLeft = flEnd - t;
	if( flLeft > flDur / 2.0f )
		s_flBeepGate = t + 2.5f;	// ref @0x177487 rodata +2.5
	else if( flLeft > flDur / 3.0f )
		s_flBeepGate = t + 1.0f;	// ref @0x1774c3
	else if( flLeft > flDur / 4.0f )
		s_flBeepGate = t + 0.5f;	// ref @0x1774ff
	else
		s_flBeepGate = t + 0.25f;
}

void CStargateTeamCP::Think( void )
{
	// ref CStargateTeamCP::Think @0x1853e8 — the .so body IS the shared DM
	// block sequence plus CP-only gates (friendlyfire clamp, +0x758 type
	// latch, +0x75c bucket, +0x768 superguns latch, resetround type-4 tail,
	// round_official reject) ending in the DM tail (recount/mode/timeleft).
	if( g_fGameOver )
	{
		CHalfLifeMultiplay::Think();
		return;
	}

	// P149: prime info_cap_rules before the +0x758 latch flip gate. Latch is
	// -1 until the first Think (ctor sentinel; type range is 0..4) — read cap
	// rules once and align the latch. PlayerSpawn ReadCapInfo (P146b) left
	// latch=0 & type=2 → spurious EndRound storm during listen signon
	// (ctp_crypt/frozen/lastn never reached ShowMenu 11).
	if( m_iSgtcCPTypeLatch < 0 )
	{
		SGTC_Rules_ReadCapInfo();
		m_iSgtcCPTypeLatch = sgtc_type;
	}
	// ref: friendlyfire cvar clamped to 0 when NaN/<=0.
	if( !( friendlyfire.value > 0.0f ) )
		CVAR_SET_FLOAT( "mp_friendlyfire", 0.0f );

	// ref: this+0x758 type latch — EndRound when the map 'type' flips.
	if( m_iSgtcCPTypeLatch != sgtc_type )
		EndRound();

	// ref: m_SendPL TeamNames+wpnC broadcast is handled inside
	// CStargateTeamDM::Think (shared tail) — ref order here matches.
	SGTC_ThinkSendTeamNames();

	// ref: this+0x768 superguns latch — armor HUD refresh while rising.
	if( (float)m_iSgtcCPSupergunsLatch <= sgtc_cvar_superweapons.value )
	{
		SGTC_ThinkResendArmorHud();
	}

	// ref: this+0x75c focused-presence transition (empty-team paths).
	int iBucket = SGTC_FocusedPresenceBucket();
	if( m_iSgtcFocusedBucket != iBucket )
	{
		SGTC_Type4TicketSideReset();
		sgtc_nrcooler = 0.0f;
		EndRound();
	}
	m_iSgtcCPSupergunsLatch = (int)( sgtc_cvar_superweapons.value + 0.5f );
	m_iSgtcFocusedBucket = iBucket;
	m_iSgtcCPTypeLatch = sgtc_type;

	// ref: resetround pulse (score clear at >1; type-4 side+ticket reset;
	// EndRound + cvar reset) — ref FALLS THROUGH after handling.
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
		SGTC_Type4TicketSideReset();
		// P149c: admin reset must run a full NewRound (TicketMode/SetRndTim for
		// observers on type-2 maps). nrcooler from the prior NewRound tail
		// otherwise no-ops EndRound's nested NewRound (p43h ticket=1).
		sgtc_nrcooler = 0.0f;
		EndRound();
		CVAR_SET_FLOAT( "mp_resetround", 0.0f );
	}

	// ref: round_official set but tournament fairness / min-4 reject.
	if( sgtc_round_official != 0 )
	{
		int iAvg = ( sgtc_G_players_focused + sgtc_T_players_focused ) / 2;
		BOOL bFair = sgtc_G_players_focused <= iAvg + 2 &&
			sgtc_T_players_focused <= iAvg + 2;
		if( !bFair || sgtc_G_players_focused < 4 || sgtc_T_players_focused < 4 )
		{
			SGTC_Type4TicketSideReset();
			EndRound();
		}
	}

	SGTC_Type4AttackCountdownSounds();

	// ref tail: shared DM round body (recount cadence is DM-only; CP skips it
	// via SGTC_IsCaptureMode), mp_timelimit gate + mode/timeleft logic.
	SGTC_ThinkRoundTail();
}

// ===========================================================================
// CStargateTeamCP
// ===========================================================================
CStargateTeamCP::CStargateTeamCP( void ) : m_bSpawnsInversed( FALSE )
{
	g_iSgtcMapType = 1;	// ref CP ctor sets MapType=1 (@0x179eb7)
	m_iSgtcCPTypeLatch = -1;	// ref CP +0x758 starts 0; P149: -1 sentinel = "not primed" (first Think reads cap rules, avoids signon EndRound storm)
	m_iSgtcCPSupergunsLatch = 0;	// ref CP +0x768
}

void CStargateTeamCP::InverseSpawns( void )
{
	// .so InverseSpawns: swaps the torri/goauld spawn tables mid-round.
	m_bSpawnsInversed = !m_bSpawnsInversed;
}

edict_t *CStargateTeamCP::GetPlayerSpawnSpot( CBasePlayer *pPlayer )
{
	int team = GetTeamIndex( pPlayer->m_szTeamName );
	if( m_bSpawnsInversed )
		team = 1 - team;
	const char *SpotClass = ( team == 1 ) ? SGTC_TeamSpawnClassname[1] : SGTC_TeamSpawnClassname[0];	// team = GetTeamIndex(): 0 Torri, 1 Goa'uld
	edict_t *spot = SGTC_FindTeamSpawn( SpotClass );
	if( !spot || FNullEnt( spot ) )
		return CStargateTeamDM::GetPlayerSpawnSpot( pPlayer );
	pPlayer->pev->origin = VARS( spot )->origin + Vector( 0, 0, 1 );
	pPlayer->pev->angles = VARS( spot )->angles;
	pPlayer->pev->v_angle = VARS( spot )->angles;
	pPlayer->pev->fixangle = TRUE;
	return spot;
}

void CStargateTeamCP::ClientUserInfoChanged( CBasePlayer *pPlayer, char *infobuffer )
{
	// ref CStargateTeamCP::ClientUserInfoChanged @0x182c02 — same body as the
	// DM variant MINUS the +0x4de "unasteammate" tail and the +0xa3d latch.
	(void)infobuffer;
	SGTC_ApplyUserInfoTeam( pPlayer );

	pPlayer->SendAboutMe( (int)pPlayer->pev->team,
		STRING( pPlayer->pev->netname ), pPlayer->entindex() );
	RecountTeams( FALSE );

	UpdateGameMode( pPlayer );						// ref vtable+0x34 tail — virtual, dispatches CP
}
