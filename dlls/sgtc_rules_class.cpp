// sgtc_rules_class.cpp — StargateTC rules: class caps and re-roll, the class
// consume step of FPlayerCanRespawn (DM and CP), the 'classe' verb body and the
// per-class player counts.
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


// ref CStargateTeamDM::FPlayerCanRespawn @0x18ba08 (CP @0x17ea2a, identical
// except the round-over field: DM +0x740 / CP +0x73c — see
// rea_dump/CStargateTeam{DM,CP}__FPlayerCanRespawn.c):
//   1. if (m_iSgtcSnapClass != m_iSgtcPortraitShown || 0 < m_iSgtcPortraitSel)
//      { UpdateClassCount(); cap-check the effective class (pending else
//      shown) against the +0x438 pick's team; on a cap hit re-roll among
//      uncapped classes, preferring unlimited (max_* == 0) slots (#128). }
//   2. if (0 < +0xa44) +0xa40 = +0xa44 (consume pending -> shown);
//   3. +0xa44 = 0 (always clear the pending selection);
//   4. +0x480 = +0xa40; +0x484 = pev->team (snapshot for the change detect);
//   5. return the round gate: roundOver || (no-player && ?) || (early-round
//      grace of mp_roundtime + (now - roundEnd) < 20 — ref +0x3e3 is only
//      ever cleared (InitHUD/spawn), never set, so those clauses are dead).
// Cap mapping: 1=syslord/oneill, 2=anubis(vs count_prime)/carter,
// 3=jaffa/jackson, 4=serpent/tealc, 5=horus/soldier(vs count_commander).
// iTeam is an SgtcTeam (pick numbering), NOT a pev->team value. The case
// labels are the class slot numbers shared by both teams (SgtcClass).
static BOOL SGTC_ClassCapped( int iTeam, int iClass )
{
	float flMax, flCount;
	switch( iClass )
	{
	case 1:
		flMax = ( iTeam == SGTC_TEAM_GOAULD ) ? sgtc_cvar_max_syslord.value : sgtc_cvar_max_oniell.value;
		flCount = (float)( ( iTeam == SGTC_TEAM_GOAULD ) ? sgtc_count_syslord : sgtc_count_oniell );
		break;
	case 2:
		flMax = ( iTeam == SGTC_TEAM_GOAULD ) ? sgtc_cvar_max_anubis.value : sgtc_cvar_max_carter.value;
		flCount = (float)( ( iTeam == SGTC_TEAM_GOAULD ) ? sgtc_count_prime : sgtc_count_carter );
		break;
	case 3:
		flMax = ( iTeam == SGTC_TEAM_GOAULD ) ? sgtc_cvar_max_jaffa.value : sgtc_cvar_max_jackson.value;
		flCount = (float)( ( iTeam == SGTC_TEAM_GOAULD ) ? sgtc_count_jaffa : sgtc_count_jackson );
		break;
	case 4:
		flMax = ( iTeam == SGTC_TEAM_GOAULD ) ? sgtc_cvar_max_serpent.value : sgtc_cvar_max_tealc.value;
		flCount = (float)( ( iTeam == SGTC_TEAM_GOAULD ) ? sgtc_count_serpent : sgtc_count_tealc );
		break;
	case 5:
		flMax = ( iTeam == SGTC_TEAM_GOAULD ) ? sgtc_cvar_max_horus.value : sgtc_cvar_max_commander.value;
		flCount = (float)( ( iTeam == SGTC_TEAM_GOAULD ) ? sgtc_count_horus : sgtc_count_commander );
		break;
	default:
		return FALSE;
	}
	// ref FPU-status idiom: capped iff max <= count && max != 0 (0 = unlimited).
	return ( flMax <= flCount && flMax != 0.0f ) ? TRUE : FALSE;
}

static BOOL SGTC_ClassUnlimited( int iTeam, int iClass )
{
	float flMax;
	switch( iClass )
	{
	case 1:
		flMax = ( iTeam == SGTC_TEAM_GOAULD ) ? sgtc_cvar_max_syslord.value : sgtc_cvar_max_oniell.value;
		break;
	case 2:
		flMax = ( iTeam == SGTC_TEAM_GOAULD ) ? sgtc_cvar_max_anubis.value : sgtc_cvar_max_carter.value;
		break;
	case 3:
		flMax = ( iTeam == SGTC_TEAM_GOAULD ) ? sgtc_cvar_max_jaffa.value : sgtc_cvar_max_jackson.value;
		break;
	case 4:
		flMax = ( iTeam == SGTC_TEAM_GOAULD ) ? sgtc_cvar_max_serpent.value : sgtc_cvar_max_tealc.value;
		break;
	case 5:
		flMax = ( iTeam == SGTC_TEAM_GOAULD ) ? sgtc_cvar_max_horus.value : sgtc_cvar_max_commander.value;
		break;
	default:
		return FALSE;
	}
	return flMax == 0.0f;
}

static int SGTC_PickUncappedClass( int iTeam )
{
	if( iTeam != SGTC_TEAM_TORRI && iTeam != SGTC_TEAM_GOAULD )
		return 0;

	int iUnlimited[SGTC_NUM_CLASSES];
	int nUnlimited = 0;
	for( int c = SGTC_CLASS_FIRST; c <= SGTC_CLASS_LAST; c++ )
	{
		if( !SGTC_ClassCapped( iTeam, c ) && SGTC_ClassUnlimited( iTeam, c ) )
			iUnlimited[nUnlimited++] = c;
	}
	if( nUnlimited > 0 )
		return iUnlimited[RANDOM_LONG( 0, nUnlimited - 1 )];

	for( int c = SGTC_CLASS_FIRST; c <= SGTC_CLASS_LAST; c++ )
	{
		if( !SGTC_ClassCapped( iTeam, c ) )
			return c;
	}
	return 0;
}

static void SGTC_ClassReroll( int iTeam, int &iPending )
{
	iPending = SGTC_PickUncappedClass( iTeam );
}

// Shared body of the DM and CP FPlayerCanRespawn. The two ref bodies are identical
// except for the round-over field the return gate reads: DM +0x740 (m_bRoundOver),
// CP +0x73c (m_iSgtcRoundTicks); each caller passes its own value as iRoundOver.
BOOL CStargateTeamDM::SGTC_CanRespawn( CBasePlayer *pPlayer, int iRoundOver )
{
	if( pPlayer->m_iSgtcSnapClass != pPlayer->m_iSgtcPortraitShown ||
		0 < pPlayer->m_iSgtcPortraitSel )
	{
		UpdateClassCount();	// ref vtable+0x118

		int iClass = ( pPlayer->m_iSgtcPortraitSel != 0 )
			? pPlayer->m_iSgtcPortraitSel : pPlayer->m_iSgtcPortraitShown;
		int iTeam = pPlayer->m_iSgtcTeam;	// ref +0x438 — SgtcTeam pick numbering (1=Torri, 2=Goa'uld)

		if( SGTC_ClassCapped( iTeam, iClass ) )
		{
			pPlayer->m_iSgtcPortraitSel = 0;
			SGTC_ClassReroll( iTeam, pPlayer->m_iSgtcPortraitSel );
			if( pPlayer->m_iSgtcPortraitSel == 0 )
			{
				if( pPlayer->m_iSgtcPortraitShown != 0 &&
					!SGTC_ClassCapped( iTeam, pPlayer->m_iSgtcPortraitShown ) )
					pPlayer->m_iSgtcPortraitSel = pPlayer->m_iSgtcPortraitShown;
				else
					return FALSE;
			}
		}
	}

	if( 0 < pPlayer->m_iSgtcPortraitSel )
		pPlayer->m_iSgtcPortraitShown = pPlayer->m_iSgtcPortraitSel;
	pPlayer->m_iSgtcPortraitSel = 0;
	pPlayer->m_iSgtcSnapClass = pPlayer->m_iSgtcPortraitShown;	// +0x480
	pPlayer->m_iSgtcSnapTeam = (int)pPlayer->pev->team;		// +0x484

	// return gate (ref DM FPlayerCanRespawn @0x18ba08,
	// rea_dump/CStargateTeamDM__FPlayerCanRespawn.c line 21; CP @0x17ea2a line 20):
	// (round-over==1) || (round-over==0 && +0x3e3!=0) ||
	// (+0x3e3==1 && +0x728 + (now - +0x724) < 20)
	return ( iRoundOver == 1 ||
		( iRoundOver == 0 && pPlayer->m_bSgtcPad3e3 != 0 ) ||
		( pPlayer->m_bSgtcPad3e3 == 1 &&
			m_flSgtcRoundTime + ( gpGlobals->time - m_flSgtcRoundEnd ) < 20.0f ) )
		? TRUE : FALSE;
}

BOOL CStargateTeamDM::FPlayerCanRespawn( CBasePlayer *pPlayer )
{
	return SGTC_CanRespawn( pPlayer, m_bRoundOver );	// ref DM round-over field +0x740
}

BOOL CStargateTeamCP::FPlayerCanRespawn( CBasePlayer *pPlayer )
{
	// ref CP FPlayerCanRespawn @0x17ea2a — identical body to DM @0x18ba08 but
	// the round-over field is CP +0x73c (this port's m_iSgtcRoundTicks). Same
	// consume/cap/reroll/snapshot steps; see rea_dump/CStargateTeamCP__FPlayerCanRespawn.c
	return SGTC_CanRespawn( pPlayer, m_iSgtcRoundTicks );
}

// ---------------------------------------------------------------------------
// SGTC_CommandClasse — shared body of the ref 'classe' verb
// (DM @0x191336 tail / CP @0x183b88 tail). argv(1) = class 1..5, argv(2) =
// team 1|2 (an SgtcTeam: 1 = Torri, 2 = Goa'uld — the pending +0x438 numbering,
// opposite of pev->team, an SgtcEntTeam, where 1 = Goa'uld, 2 = Torri; see
// game_shared/sgtc_enums.h). The pending fields land in
// +0x438/+0xa44 and PlayerThink consumes them on the 1s dead tick.
// bCaptureMode selects the CP-only type-4 side gate (ref rules+0x734).
// ---------------------------------------------------------------------------
void CStargateTeamDM::SGTC_CommandClasse( CBasePlayer *pPlayer, BOOL bCaptureMode )
{
	pPlayer->m_bSgtcBadGuy = 0;		// ref +0x462 = 0
	pPlayer->m_iSgtcTeam = 0;		// ref +0x438 = 0
	RecountTeams( FALSE );
	UpdateClassCount();				// ref vtable+0x118

	int iTeamArg = atoi( CMD_ARGV( 2 ) );
	if( iTeamArg != SGTC_TEAM_NONE &&
		( iTeamArg < SGTC_TEAM_TORRI || iTeamArg > SGTC_TEAM_GOAULD ) )
	{
		ClientPrint( pPlayer->pev, HUD_PRINTCONSOLE,
			"Invalid team (use 1 or 2)\n" );
		return;
	}

	// ref DM gate: during an official round an EXPLICIT team arg is refused
	// (auto-pick path below still runs — ref allows classe <class> to slip
	// through with a count-based pick). CP skips this gate entirely.
	if( !bCaptureMode && sgtc_round_official == 1 && iTeamArg >= SGTC_TEAM_TORRI )
		return;	// ref return 1 — pick silently refused

	if( iTeamArg >= SGTC_TEAM_TORRI )
	{
		if( bCaptureMode && sgtc_type == 4 )
		{
			// ref CP type-4: refuse when the player is already on the requested
			// team (SgtcTeam arg converted to pev->team) AND that team is the
			// current attacker side (m_iSgtcCPSide 0 → Torri attacks, else Goa'uld).
			// Wording in older audits said "swap INTO attacker"; the code matches
			// "already on that side while it is the attacker" (#117).
			const int iSide = ((CStargateTeamCP *)this)->m_iSgtcCPSide;
			if( ( ( iSide == 0 && iTeamArg == SGTC_TEAM_TORRI ) ||
				( iSide != 0 && iTeamArg == SGTC_TEAM_GOAULD ) ) &&
				pPlayer->pev->team == SGTC_TeamToEntTeam( iTeamArg ) )
				return;	// ref return 1 — pick silently refused
		}

		if( sgtc_cvar_autobalance.value <= 0.0f )
		{
			pPlayer->m_iSgtcTeam = iTeamArg;
		}
		else
		{
			// Autobalance: refuse joining the larger focused team past mp_autobalance (#114).
			BOOL bAllow1 = TRUE, bAllow2 = TRUE;
			if( sgtc_G_players_focused > sgtc_T_players_focused &&
				sgtc_cvar_autobalance.value < (float)( sgtc_G_players_focused - sgtc_T_players_focused ) )
				bAllow1 = FALSE;
			if( sgtc_T_players_focused > sgtc_G_players_focused &&
				sgtc_cvar_autobalance.value < (float)( sgtc_T_players_focused - sgtc_G_players_focused ) )
				bAllow2 = FALSE;
			if( ( iTeamArg == SGTC_TEAM_TORRI && bAllow1 ) ||
				( iTeamArg == SGTC_TEAM_GOAULD && bAllow2 ) )
				pPlayer->m_iSgtcTeam = iTeamArg;
			else
			{
				pPlayer->m_iSgtcTeam = ( iTeamArg == SGTC_TEAM_TORRI ) ? SGTC_TEAM_GOAULD : SGTC_TEAM_TORRI;
				ClientPrint( pPlayer->pev, HUD_PRINTCONSOLE, "Teams Unbalanced\n" );
			}
		}
	}

	if( pPlayer->m_iSgtcTeam == SGTC_TEAM_NONE )
	{
		if( sgtc_T_players_focused < sgtc_G_players_focused )
			pPlayer->m_iSgtcTeam = SGTC_TEAM_GOAULD;
		else if( sgtc_G_players_focused < sgtc_T_players_focused )
			pPlayer->m_iSgtcTeam = SGTC_TEAM_TORRI;
		else
			pPlayer->m_iSgtcTeam = RANDOM_LONG( SGTC_TEAM_TORRI, SGTC_TEAM_GOAULD );
	}

	if( CMD_ARGC() > 0 )
	{
		pPlayer->m_iSgtcPortraitSel = 0;							// ref +0xa44
		pPlayer->m_iSgtcPortraitPrev = pPlayer->m_iSgtcPortraitShown;	// ref +0xa38
		if( sgtc_cvar_classlimitmodel.value != 0.0f )
			pPlayer->m_iSgtcPortraitShown = 0;					// ref +0xa40

		int iClass = atoi( CMD_ARGV( 1 ) );
		if( iClass < SGTC_CLASS_FIRST || iClass > SGTC_CLASS_LAST )
		{
			ClientPrint( pPlayer->pev, HUD_PRINTCONSOLE,
				"Invalid class (use 1..5)\n" );
			return;
		}
		int iTeam = pPlayer->m_iSgtcTeam;
		RecountTeams( FALSE );
		UpdateClassCount();	// ref vtable+0x118 — fresh counts for the caps

		// Same cap test FPlayerCanRespawn uses (max_* == 0 = unlimited). iTeam is
		// SGTC_TEAM_TORRI or SGTC_TEAM_GOAULD here: it was set from the validated
		// team argument or the focused-count / random pick above, and iClass was
		// range-checked 1..5, so every class has a cap entry.
		const BOOL bCapped = SGTC_ClassCapped( iTeam, iClass );

		if( !bCapped && iClass >= SGTC_CLASS_FIRST && iClass <= SGTC_CLASS_LAST )
		{
			pPlayer->m_iSgtcPortraitSel = iClass;
			return;
		}

		pPlayer->m_iSgtcPortraitSel = SGTC_PickUncappedClass( iTeam );
		if( pPlayer->m_iSgtcPortraitSel == 0 )
		{
			ClientPrint( pPlayer->pev, HUD_PRINTCONSOLE,
				"All classes are full on that team\n" );
			pPlayer->m_iSgtcTeam = SGTC_TEAM_NONE;
			pPlayer->m_iSgtcPortraitShown = pPlayer->m_iSgtcPortraitPrev;
			return;
		}
	}
}

// ref CStargateTeamDM::UpdateClassCount @0x18c2e2 (CP @0x17f308, identical body)
// — counts each alive-teamed player's SHOWN class (+0xa40) into the per-class
// globals by pev->team (1=Goa'uld, 2=Torri). vtable+0xa8 = TeamID() non-null
// check; the +0xa44!=0 && classlimitmode!=0 gate skips players mid-pick unless
// class limiting is off. The classe verb calls this via vtable+0x118.
void CStargateTeamDM::UpdateClassCount( void )
{
	sgtc_count_oniell = sgtc_count_carter = sgtc_count_jackson =
		sgtc_count_tealc = sgtc_count_commander = 0;
	sgtc_count_syslord = sgtc_count_prime = sgtc_count_jaffa =
		sgtc_count_serpent = sgtc_count_horus = 0;

	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *plr = (CBasePlayer *)UTIL_PlayerByIndex( i );
		if( !plr || !plr->TeamID() )
			continue;
		if( plr->m_iSgtcPortraitShown <= 0 || plr->pev->team <= 0 )
			continue;
		if( plr->m_iSgtcPortraitSel != 0 &&
			sgtc_cvar_classlimitmodel.value != 0.0f )
			continue;
		if( plr->pev->team == SGTC_ENTTEAM_GOAULD )
		{
			switch( plr->m_iSgtcPortraitShown )
			{
			case SGTC_CLASS_SYSLORD: sgtc_count_syslord++;	break;
			case SGTC_CLASS_ANUBIS:  sgtc_count_prime++;	break;
			case SGTC_CLASS_JAFFA:   sgtc_count_jaffa++;	break;
			case SGTC_CLASS_SERPENT: sgtc_count_serpent++;	break;
			case SGTC_CLASS_HORUS:   sgtc_count_horus++;	break;
			}
		}
		else if( plr->pev->team == SGTC_ENTTEAM_TORRI )
		{
			switch( plr->m_iSgtcPortraitShown )
			{
			case SGTC_CLASS_ONEILL:    sgtc_count_oniell++;		break;
			case SGTC_CLASS_CARTER:    sgtc_count_carter++;		break;
			case SGTC_CLASS_JACKSON:   sgtc_count_jackson++;	break;
			case SGTC_CLASS_TEALC:     sgtc_count_tealc++;		break;
			case SGTC_CLASS_COMMANDER: sgtc_count_commander++;	break;
			}
		}
	}
}
