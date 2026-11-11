// sgtc_rules_spawn.cpp — StargateTC rules: weapon allow-list, class model/stat
// tables, weapon-grant table and the DM / CP PlayerSpawn bodies.
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
#include "sgtc_rea_debug.h"

extern void SGTC_PackAuto_Start( CBasePlayer *pPlayer );
extern void SGTC_PackAuto_Cancel( CBasePlayer *pPlayer );
#ifdef BUILD_JK_BOTTI
#include "sgtc_bots.h"
#endif


// ---------------------------------------------------------------------------
// SetWeaponAllow — ref @0x18a26c (nm 0x17a26c). Zeroes the 28-entry
// Progress-allow table (CBasePlayer m_iProgressTeam, ref +0x93c) for the
// weapons the player's pev->team/class (+0xa40) may NOT pick up; other teams
// keep every slot at 1. Slot i lives at +0x93c + i*4 (an SgtcWeaponSlot).
// Indexes below are (ref 0x9NN - 0x93c) / 4. Copy verbatim from
// rea_dump/SetWeaponAllow.c, including the Torri (team==2) case-5
// fallthrough. pev->team is an SgtcEntTeam: 2 = Torri, 1 = Goa'uld.
// ---------------------------------------------------------------------------
void SetWeaponAllow( CBasePlayer *pPlayer )
{
	int team = (int)pPlayer->pev->team;
	int cls = pPlayer->m_iSgtcPortraitShown;
	// Slot lists re-read from ref SetWeaponAllow @0x18a26c (Ghidra pseudo-code,
	// offsets +0x93c..+0x9a8 zeroed per team/class). The earlier hand-typed
	// tables were off; these are the ref's exact zeroed-slot sets (slot =
	// (offset-0x93c)/4). Cross-check: the orig's menu-12 capture shows exactly
	// the 15 slots allowed by Torri / class 1.
	static const int g_wa_torri[SGTC_NUM_CLASSES][21] = {
		// cls 1 (13)
		{ 0,2,7,9,11,12,13,17,18,19,20,21,27,-1 },
		// cls 2 (16)
		{ 0,1,2,5,7,8,9,11,12,13,16,17,18,19,20,21,-1 },
		// cls 3 (20) — ref case 3 @SetWeaponAllow: 0x10 (mine), not 0x0f (hand grenade)
		{ 0,1,2,7,8,9,11,12,13,14,16,17,18,19,20,21,23,24,25,27,-1 },
		// cls 4 (13)
		{ 2,5,6,9,12,13,17,18,19,21,24,26,27,-1 },
		// cls 5 (17; ref case has no break — falls out of the switch)
		{ 0,2,9,10,11,12,13,14,16,17,18,19,20,21,24,26,27,-1 },
	};
	static const int g_wa_goauld[SGTC_NUM_CLASSES][26] = {
		// cls 1 (18)
		{ 0,1,3,4,5,6,7,8,11,12,15,16,19,20,23,24,25,26,-1 },
		// cls 2 (21)
		{ 0,1,3,4,5,6,7,8,9,12,15,16,17,18,19,20,23,24,25,26,27,-1 },
		// cls 3 (22)
		{ 0,1,2,3,4,5,6,7,8,9,11,13,15,16,17,19,20,21,23,24,25,26,-1 },
		// cls 4 (18)
		{ 1,3,4,5,6,7,8,9,12,13,15,16,17,20,23,24,25,26,-1 },
		// cls 5 (22) — ref SetWeaponAllow Goa'uld (pev->team==1) case 5 (horus/esclave)
		{ 0,1,3,4,5,6,7,8,9,10,11,13,15,16,18,19,20,21,23,24,25,26,-1 },
	};
	if( team == SGTC_ENTTEAM_TORRI && cls >= SGTC_CLASS_FIRST && cls <= SGTC_CLASS_LAST )
	{
		const int *t = g_wa_torri[cls - SGTC_CLASS_FIRST];
		for( int k = 0; k < 21; k++ )
		{
			if( t[k] < 0 )
				break;
			pPlayer->m_iProgressTeam[t[k]] = 0;
		}
	}
	if( team == SGTC_ENTTEAM_GOAULD && cls >= SGTC_CLASS_FIRST && cls <= SGTC_CLASS_LAST )
	{
		const int *t = g_wa_goauld[cls - SGTC_CLASS_FIRST];
		for( int k = 0; k < 26; k++ )
		{
			if( t[k] < 0 )
				break;
			pPlayer->m_iProgressTeam[t[k]] = 0;
		}
	}
	if( team == SGTC_ENTTEAM_NONE )
	{
		for( int i = 0; i < SGTC_NUM_WEAPON_SLOTS; i++ )	// ref 0x1c entries
			pPlayer->m_iProgressTeam[i] = 0;
	}
}

// ---------------------------------------------------------------------------
// Class model/stat tables — ref DM::PlayerSpawn @0x18e7e2 Goa'uld/Torri
// switches on +0xa40 (m_iSgtcPortraitShown). Class 4 cloak: Goa'uld always,
// Torri only for tealc. Model key is written via pfnSetClientKeyValue.
// ---------------------------------------------------------------------------
struct SGTCSClassDef
{
	const char *model;
	float maxspeed;		// pev+0x210
	int snagArmorCap;	// +0x908 — armor HUD scale cap
	float armorvalue;	// pev+0x1bc
	int cloak;		// +0x448 cloak tick arm
};
static const SGTCSClassDef s_SgtcGoauldClass[SGTC_NUM_CLASSES + 1] =
{
	{ NULL, 0.0f, 0, 0.0f, 0 },				// class 0: no override
	{ "systemlord",  250.0f, 35,  35.0f, 1 },	// ref +0x908=0x23, armor 0x420c0000
	{ "anubis",      250.0f, 80,  80.0f, 1 },	// +0x908=0x50, armor 0x42a00000
	{ "goauld3",     325.0f, 40,  40.0f, 1 },	// +0x908=0x28, armor 0x42200000
	{ "snakegoauld", 275.0f, 80,  80.0f, 1 },	// maxspeed 0x43898000
	{ "horus",       325.0f, 30,  30.0f, 1 },	// +0x908=0x1e, armor 0x41f00000
};
static const SGTCSClassDef s_SgtcTorriClass[SGTC_NUM_CLASSES + 1] =
{
	{ NULL, 0.0f, 0, 0.0f, 0 },
	{ "oneill",      250.0f, 70,  70.0f, 0 },	// +0x908=0x46, armor 0x428c0000
	{ "carter",      275.0f, 40,  40.0f, 0 },
	{ "jackson",     225.0f, 70,  70.0f, 0 },	// maxspeed 0x43610000
	{ "tealc",       275.0f, 25,  25.0f, 1 },	// +0x908=0x19, cloak on
	{ "commander",   250.0f, 40,  40.0f, 0 },
};
// CP table differs from DM only in snakegoauld speed: 270.0f (0x43870000).
static const SGTCSClassDef s_SgtcCPGoauldClass[SGTC_NUM_CLASSES + 1] =
{
	{ NULL, 0.0f, 0, 0.0f, 0 },
	{ "systemlord",  250.0f, 35,  35.0f, 1 },
	{ "anubis",      250.0f, 80,  80.0f, 1 },
	{ "goauld3",     325.0f, 40,  40.0f, 1 },
	{ "snakegoauld", 270.0f, 80,  80.0f, 1 },
	{ "horus",       325.0f, 30,  30.0f, 1 },
};

// Weapon-grant table — ref WPLIST (nm 0x220c40) == WPLISTB (nm 0x220a60):
// identical 28 entries; CP spawn uses WPLISTB, DM spawn WPLIST, both the same
// names, and so does SGTC_GiveFromMenu (the gond4h9wp menu grant, sgtc_player.cpp):
// one table, declared in sgtc_rules.h. WPLISTB @0x220a60 = Ghidra 0x230a60; verified
// 2026-10-14 by read_bytes: the 28 pointers 0x1eae70..0x1eafdc form a contiguous string
// chain weapon_glstaff .. weapon_goaregen with NO NULL entries (the earlier "first 4
// entries NULL" read was wrong and shifted every loadout slot by 4).

const char * const SGTC_WeaponSlotClassname[SGTC_NUM_WEAPON_SLOTS] =
{
	"weapon_glstaff",	"weapon_usas",		"weapon_batsup",
	"weapon_m92s",		"weapon_mp5",		"weapon_psg1",
	"weapon_p90",		"weapon_spas",		"weapon_m16",
	"weapon_main",		"weapon_zat",		"weapon_lance",
	"weapon_reetou",	"weapon_zatarc",	"weapon_tacgun",
	"weapon_handgrenade","weapon_mine",		"weapon_invis",
	"weapon_shield",	"weapon_gacgoa",	"weapon_c4",
	"weapon_larve",		"weapon_medic",		"weapon_smokegrenade",
	"weapon_nervegrenade","weapon_narcogrenade","weapon_mustardgrenade",
	"weapon_goaregen"
};

// Sub-steps shared by the DM and CP PlayerSpawn. Each block below was written out
// twice (the class override four times) with the same code; only the class table
// differs between the callers.

// Class override of a spawning Goa'uld/Torri player: model key, speed, armor cap and
// value and cloak tick from pTable (indexed by class 1..5; DM and CP use different
// Goa'uld tables). Skipped for a "superman" player and for a class outside 1..5.
void SGTC_SpawnApplyClassStatsAlive( CBasePlayer *pPlayer );

static void SGTC_SpawnApplyClassDef( CBasePlayer *pPlayer, const SGTCSClassDef *pTable )
{
	if( pPlayer->m_bSgtcSuperman != 1 )		// ref +0x924 != 1
	{
		int cls = pPlayer->m_iSgtcPortraitShown;	// ref +0xa40
		if( cls >= SGTC_CLASS_FIRST && cls <= SGTC_CLASS_LAST )
		{
			const SGTCSClassDef &d = pTable[cls];
			g_engfuncs.pfnSetClientKeyValue( pPlayer->entindex(),
				g_engfuncs.pfnGetInfoKeyBuffer( pPlayer->edict() ), "model", d.model );
			pPlayer->pev->maxspeed = d.maxspeed;
			// The class run speed must also become the SgtcThink maxspeed latch,
			// otherwise the 10 Hz maxspeed restore rewrites pev->maxspeed back to
			// the pre-class value (200) on the first tick. A fresh player also
			// starts with m_iStamina == 0, which the low-stamina clamp turns into
			// kLowStaminaSpeed (10 u/s) - spawn with full stamina instead.
			pPlayer->m_flMaxSpeedLatch = d.maxspeed;
			pPlayer->pev->fuser2 = pPlayer->m_flMaxSpeedLatch;	// ref latch mirror
			pPlayer->m_iStamina = (int)pPlayer->pev->health;
			pPlayer->m_iSnagArmorCap = d.snagArmorCap;
			pPlayer->pev->armorvalue = d.armorvalue;
			pPlayer->m_bSgtcCloakTick = (char)d.cloak;
		}
	}
}

// build-9: bots whose class/team is resolved after CBasePlayer::Spawn (the normal
// first-life path for fake clients on maps without team spawn points) never ran the
// team branch above, so they kept maxspeed 0 / armor 0 and could not move.  Re-apply
// the class override when the bot loadout resolves while alive.  Idempotent.
void SGTC_SpawnApplyClassStatsAlive( CBasePlayer *pPlayer )
{
	if( !pPlayer || !pPlayer->pev )
		return;
	const int iTeam = (int)pPlayer->pev->team;
	if( iTeam == SGTC_ENTTEAM_GOAULD )
		SGTC_SpawnApplyClassDef( pPlayer, ( sgtc_type == 4 ) ? s_SgtcCPGoauldClass : s_SgtcGoauldClass );
	else if( iTeam == SGTC_ENTTEAM_TORRI )
		SGTC_SpawnApplyClassDef( pPlayer, s_SgtcTorriClass );
}

// Class/team changed since the last spawn: resend Progress, unlock the buy menu, wipe
// the grant copy.
static void SGTC_SpawnResendProgress( CBasePlayer *pPlayer )
{
	if( pPlayer->m_iSgtcPrevClass != pPlayer->m_iSgtcPortraitShown ||
		pPlayer->m_iSgtcPrevPeTeam != (int)pPlayer->pev->team )
	{
		MESSAGE_BEGIN( MSG_ONE, gmsgProgress, NULL, pPlayer->pev );
			WRITE_BYTE( 0 );
			WRITE_BYTE( SGTC_ClampWireByte( pPlayer->m_iNukeBonus ) );
			for( int i = 0; i < SGTC_NUM_WEAPON_SLOTS; i++ )
				WRITE_BYTE( SGTC_ClampWireByte( pPlayer->m_iProgressTeam[i] ) );
			WRITE_BYTE( 1 );
		MESSAGE_END();
		pPlayer->m_bSgtcMenuLock = 0;				// ref +0x4e5
		for( int i = 0; i < SGTC_NUM_WEAPON_SLOTS; i++ )
			pPlayer->m_iSgtcGondArgs[i] = 0;		// ref +0x9ac[i] = 0
	}
}

// Spawn tail: pick the submodel from pev->team, then settle the observer state
// (stop observing when alive, restore render state and the in-world flag for a
// non-observer, clear pev->team for an observer).
static void SGTC_SpawnSubmodelAndObserver( CBasePlayer *pPlayer )
{
	if( pPlayer->pev->team == SGTC_ENTTEAM_GOAULD )
		pPlayer->m_iSgtcSubmodelIdx = 0;			// ref +0x440
	else if( pPlayer->pev->team == SGTC_ENTTEAM_TORRI )
		pPlayer->m_iSgtcSubmodelIdx = 1;
	else
		pPlayer->m_iSgtcSubmodelIdx = 2;
	pPlayer->ChangeSubmodel();						// ref @0x153e88

	// ref: vcall+0x94 (IsAlive) && StopObserver — verbatim dispatch.
	if( pPlayer->IsAlive() )
		pPlayer->StopObserver();
	if( !pPlayer->IsObserver() )
	{
		pPlayer->pev->renderamt = 255.0f;			// pev+0x14c = 0x437f0000
		pPlayer->pev->renderfx = 0;				// pev+0x15c
		pPlayer->pev->rendermode = 0;				// pev+0x148
		pPlayer->m_bSgtcInWorld = 1;				// ref +0x44b
	}
	if( pPlayer->IsObserver() )
		pPlayer->pev->team = 0;						// ref pev+0x1ac = 0
}

// ref +0x4f4 shield-slot state machine, one step per granted INVIS or SHIELD slot
// (neither is given as an item directly): 0 none, 1 invis, 2 shield, 3 both. Also
// used by SGTC_GiveFromMenu (declared in sgtc_rules.h).
void SGTC_GondSlotState( CBasePlayer *pPlayer, int iSlot )
{
	if( pPlayer->m_iSgtcGondState == 0 && iSlot == SGTC_WSLOT_INVIS )
		pPlayer->m_iSgtcGondState = 1;
	if( pPlayer->m_iSgtcGondState == 1 && iSlot == SGTC_WSLOT_SHIELD )
		pPlayer->m_iSgtcGondState = 3;
	if( pPlayer->m_iSgtcGondState == 0 && iSlot == SGTC_WSLOT_SHIELD )
		pPlayer->m_iSgtcGondState = 2;
	if( pPlayer->m_iSgtcGondState == 2 && iSlot == SGTC_WSLOT_INVIS )
		pPlayer->m_iSgtcGondState = 3;
}

void CStargateTeamDM::PlayerSpawn( CBasePlayer *pPlayer )
{
	// ref CStargateTeamDM::PlayerSpawn @0x18e7e2 (rea_dump/CStargateTeamDM__PlayerSpawn.c).
	// Engfuncs-mirror DAT_00238fd4 = enginefuncs[117] = pfnGetPlayerWONId
	// (returns -1 for an unfound edict — the "+0x914 locked" sentinel).
	pPlayer->m_iSgtcSpecMode = (int)g_engfuncs.pfnGetPlayerWONId( pPlayer->edict() );
	pPlayer->m_bSgtcPad3e3 = 0;					// ref +0x3e3 = 0
	pPlayer->m_iSgtcFlagsCa8 &= ~0xB;				// ref +0xca8 &= 0xfffffff4
	pPlayer->StopObserver();
	CHalfLifeMultiplay::PlayerSpawn( pPlayer );
	SGTC_PackAuto_Cancel( pPlayer );	// a new spawn owns the loadout again
	SGTC_ReaDebug_OnPlayerSpawn( pPlayer );	// build-8 debug kit (no-op when cvar 0)
	if( sgtc_GameModeSet != 3 )
	{
		pPlayer->m_bSnagInvisFlag = 0;			// ref +0x4df = 0
		pPlayer->m_bSnagInvisLatch = pPlayer->m_bSnagInvisFlag;	// +0x4de = +0x4df
	}
	pPlayer->m_bNukeViewSnap = 0;					// ref +0x463 = 0

	if( !strncmp( pPlayer->m_szTeamName, "Goa'uld", 7 ) )	// ref DAT_001ebb90, 7-char
	{
		pPlayer->pev->team = SGTC_ENTTEAM_GOAULD;					// ref pev+0x1ac = 1
		pPlayer->m_bSgtcCloakTick = 0;			// ref +0x448 = 0
		SGTC_SpawnApplyClassDef( pPlayer, s_SgtcGoauldClass );
	}
	else if( !strncmp( pPlayer->m_szTeamName, "Torri", 5 ) )	// ref "Torri", 5-char
	{
		pPlayer->pev->team = SGTC_ENTTEAM_TORRI;					// ref pev+0x1ac = 2
		pPlayer->m_bSgtcCloakTick = 0;
		SGTC_SpawnApplyClassDef( pPlayer, s_SgtcTorriClass );
	}

	for( int i = 0; i < SGTC_NUM_WEAPON_SLOTS; i++ )				// ref +0x93c[0x1c] = 1
		pPlayer->m_iProgressTeam[i] = 1;
	SetWeaponAllow( pPlayer );					// ref @0x17a26c

	SGTC_SpawnResendProgress( pPlayer );

	BOOL bHadLoadout = FALSE;					// ref local_29
	pPlayer->m_bSgtcMenuLockLatch = 0;			// ref +0x4e7
	pPlayer->m_iSgtcGondState = 0;				// ref +0x4f4

	// ref grant gate: vcall+0x94 (IsAlive) && !IsObserver && +0x4de == 0.
	if( pPlayer->IsAlive() && !pPlayer->IsObserver() && !pPlayer->m_bSnagInvisLatch )
	{
#ifdef BUILD_JK_BOTTI
		SGTC_Bots_PrepareSpawnLoadout( pPlayer );	// fake clients: same buy rules, preference order
#endif
		// client selection is untrusted: re-check at grant time, and grant
		// nothing from a list that could not be checked (#34)
		const BOOL bGondChecked = SGTC_SanitizeGondLoadout( pPlayer );
		for( int i = 0; i < SGTC_NUM_WEAPON_SLOTS; i++ )
		{
			if( bGondChecked && pPlayer->m_iSgtcGondArgs[i] == 1 )	// ref +0x9ac[i] == 1
			{
				pPlayer->m_bSgtcMenuLockLatch = 1;	// ref +0x4e7 = 1
				bHadLoadout = TRUE;
				if( i == SGTC_WSLOT_INVIS || i == SGTC_WSLOT_SHIELD )
				{
					SGTC_GondSlotState( pPlayer, i );	// ref +0x4f4 shield-slot state machine
				}
				else
					pPlayer->GiveNamedItem( SGTC_WeaponSlotClassname[i] );
			}
		}
		// ref: armed shield state + !(pev->weapons bit 9) grants WPLIST[0x11].
		if( pPlayer->m_iSgtcGondState != 0 &&
			( ( pPlayer->pev->weapons >> 9 ) & 1 ) != 1 )
			pPlayer->GiveNamedItem( SGTC_WeaponSlotClassname[SGTC_WSLOT_INVIS] );	// WPLIST[17] weapon_invis
		if( pPlayer->pev->team == SGTC_ENTTEAM_GOAULD )
			pPlayer->GiveNamedItem( "weapon_coujaf" );	// Goa'uld melee
		else
			pPlayer->GiveNamedItem( "weapon_couteau" );	// Torri melee
	}

	if( !bHadLoadout )
		SGTC_PackAuto_Start( pPlayer );	// loadout menu pending

	MESSAGE_BEGIN( MSG_ONE, gmsgArmor, NULL, pPlayer->pev );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 4 );
		WRITE_BYTE( 0 );
	MESSAGE_END();

	if( pPlayer->m_bSnagInvisLatch )				// ref +0x4de != 0
	{
		// Invis-class override: hidden model, 430 speed, 180 cap/armor.
		g_engfuncs.pfnSetClientKeyValue( pPlayer->entindex(),
			g_engfuncs.pfnGetInfoKeyBuffer( pPlayer->edict() ), "model", "invis" );	// DAT_001ebc6c
		pPlayer->pev->maxspeed = 430.0f;			// 0x43d70000
		pPlayer->m_iSnagArmorCap = 0xb4;			// +0x908 = 180
		pPlayer->pev->armorvalue = 180.0f;			// 0x43340000
		pPlayer->m_iNukeBonus = 0;					// +0x934 = 0
		pPlayer->m_bSgtcCloakTick = 1;
		pPlayer->GiveNamedItem( "weapon_coujaf" );
		pPlayer->GiveNamedItem( "weapon_lance" );
		pPlayer->GiveNamedItem( "weapon_zat" );
		MESSAGE_BEGIN( MSG_ONE, gmsgArmor, NULL, pPlayer->pev );
			WRITE_BYTE( 0 );
			WRITE_BYTE( 0 );
			WRITE_BYTE( 5 );
			WRITE_BYTE( 0 );
		MESSAGE_END();
	}

	// ref: pev->flags bit14 == FL_DUCKING selects the ducked hull.
	if( ( ( pPlayer->pev->flags >> 14 ) & 1 ) == 0 )
		UTIL_SetSize( pPlayer->pev, Vector( -16, -16, -36 ), Vector( 16, 16, 36 ) );
	else
		UTIL_SetSize( pPlayer->pev, Vector( -16, -16, -20 ), Vector( 16, 16, 20 ) );
	pPlayer->pev->view_ofs = Vector( 0, 0, 28 );	// ref pev+0x174

	pPlayer->m_iSgtcPrevClass = pPlayer->m_iSgtcPortraitShown;	// +0x500 = +0xa40
	pPlayer->m_iSgtcPrevPeTeam = (int)pPlayer->pev->team;		// +0x504 = pev->team

	if( pPlayer->m_bSgtcPePreset )					// ref +0x465 PE preset
	{
		pPlayer->pev->maxspeed = 400.0f;			// 0x43c80000
		pPlayer->pev->armorvalue = 100.0f;			// 0x42c80000
		pPlayer->pev->renderfx = 0x13;				// pev+0x15c
		pPlayer->pev->renderamt = 2.0f;				// pev+0x14c = 0x40000000
		pPlayer->pev->rendercolor = Vector( 255.0f, 0.0f, 0.0f );	// pev+0x150/+0x154/+0x158
	}

	int clientIndex = pPlayer->entindex();
	SGTC_SpawnSubmodelAndObserver( pPlayer );

	MESSAGE_BEGIN( MSG_ONE, gmsgTicketMode, NULL, pPlayer->pev );
		WRITE_BYTE( sgtc_GameModeSet > 0 ? 1 : 0 );	// ref (0 < GameModeSet)
	MESSAGE_END();

	MESSAGE_BEGIN( MSG_ALL, gmsgScoreInfo );
		WRITE_BYTE( clientIndex );
		WRITE_SHORT( SGTC_Rules_roundf( pPlayer->pev->frags ) );	// ref ROUND(pev+0x164)
		WRITE_SHORT( pPlayer->m_iDeaths );			// ref +0xdf0
		WRITE_SHORT( pPlayer->m_bSnagInvisLatch == 1 ? SGTC_CLASS_UNAS : pPlayer->m_iSgtcPortraitShown );
		WRITE_SHORT( g_pGameRules->GetTeamIndex( pPlayer->m_szTeamName ) + 1 );
		WRITE_SHORT( 0 );
	MESSAGE_END();

	MESSAGE_BEGIN( MSG_ONE, gmsgArmor, NULL, pPlayer->pev );
		WRITE_BYTE( SGTC_ArmorHudWireByte( pPlayer ) );
		WRITE_BYTE( SGTC_ClampWireByte( SGTC_Rules_roundf( sgtc_cvar_superweapons.value ) ) );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 0 );
	MESSAGE_END();

	pPlayer->SendAboutMe( (int)pPlayer->pev->team,
		STRING( pPlayer->pev->netname ), clientIndex );	// ref @0x1262b8
	RecountTeams( FALSE );

	if( !bHadLoadout && pPlayer->m_bSnagInvisLatch != 1 )
	{
		if( sgtc_cvar_wepselmode.value == 1.0f )		// ref weaponsel == 1.0
			pPlayer->ShowVGUIMenu( 0xc );
		else if( sgtc_cvar_wepselmode.value == 2.0f )	// ref weaponsel == 2.0
			pPlayer->ShowVGUIMenu( 0x3c );
	}
	pPlayer->m_bSgtcLatchA9c = 0;					// ref +0xa9c = 0
	AddPortrait( pPlayer );							// ref vtable+0x110
	if( pPlayer->IsAlive() && !pPlayer->IsObserver() )
		StartPortrait( pPlayer );					// ref vtable+0x114
}

void CStargateTeamCP::PlayerSpawn( CBasePlayer *pPlayer )
{
	// ref CStargateTeamCP::PlayerSpawn @0x1814a6
	// (rea_dump/CStargateTeamCP__PlayerSpawn.c). This is a DM-shaped spawn
	// with CP deltas: no GameModeSet!=3 invis-latch clear, +0x463 = 0 kept,
	// snakegoauld 270.0f, type/teamed 50hp tails, freeguns override, solid
	// gate on the melee give, type-4 +0x4ac clear, TicketMode byte from type,
	// ScoreInfo without the invis==6 override, tail order
	// SendAboutMe -> AddPortrait -> RecountTeams, no +0xa9c latch clear,
	// type-4 attack deadline.
	pPlayer->m_iSgtcSpecMode = (int)g_engfuncs.pfnGetPlayerWONId( pPlayer->edict() );
	pPlayer->m_bSgtcPad3e3 = 0;					// ref +0x3e3 = 0
	pPlayer->m_iSgtcFlagsCa8 &= ~0xB;				// ref +0xca8 &= 0xfffffff4
	pPlayer->StopObserver();
	CHalfLifeMultiplay::PlayerSpawn( pPlayer );
	SGTC_PackAuto_Cancel( pPlayer );	// a new spawn owns the loadout again
	SGTC_ReaDebug_OnPlayerSpawn( pPlayer );	// build-8 debug kit (no-op when cvar 0)
	pPlayer->m_bNukeViewSnap = 0;					// ref +0x463 = 0

	if( !strncmp( pPlayer->m_szTeamName, "Goa'uld", 7 ) )	// ref DAT_001eb14c, 7-char
	{
		pPlayer->pev->team = SGTC_ENTTEAM_GOAULD;
		pPlayer->m_bSgtcCloakTick = 0;
		SGTC_SpawnApplyClassDef( pPlayer, s_SgtcCPGoauldClass );
		if( ( sgtc_type == 2 && sgtc_teamed == 2 ) ||
			( sgtc_type == 4 && m_iSgtcCPSide == 0 ) )
		{
			// ref: this side plays the weakened "50hp" shell.
			pPlayer->pev->armorvalue = 0;
			pPlayer->pev->health = 50.0f;		// pev+0x160 = 0x42480000
			pPlayer->pev->max_health = 50.0f;	// pev+0x1b0
		}
		else
		{
			pPlayer->m_bSgtcCloakTick = 0;		// ref +0x448 = 0
			pPlayer->m_iSnagArmorCap = 100;		// ref +0x908 = 100
			pPlayer->pev->armorvalue = 100.0f;	// 0x42c80000
			// #301: this field is the menu-12 "Total". The shipped original
			// shows "Total 6" (docs/screenshots/row260_orig_m12_tealc.png,
			// ctp_amon) and the reference client renders the Progress byte
			// verbatim (client.dll: store @0x10076c88, "#Total %d" @0x10028f70),
			// so the spawn value has to be the menu's base budget 6 (the same 6
			// the Spawn/SgtcThink floors install). Both ref servers do contain a
			// spawn-time 200 write (stc_i386.so 0x171956/0x171d5b,
			// sgtc2_dll/hl.dll 0x100b8a0b/0x100b8cfa); sending it made the port's
			// menu read 200 against the original's 6. 200 is now only the debug
			// "cashmeup" grant (sgtc_player.cpp).
			pPlayer->m_iNukeBonus = 6;		// +0x934 = menu base budget (orig Total 6)
		}
	}
	else if( !strncmp( pPlayer->m_szTeamName, "Torri", 5 ) )
	{
		pPlayer->pev->team = SGTC_ENTTEAM_TORRI;
		pPlayer->m_bSgtcCloakTick = 0;
		SGTC_SpawnApplyClassDef( pPlayer, s_SgtcTorriClass );
		if( ( sgtc_type == 2 && sgtc_teamed == 1 ) ||
			( sgtc_type == 4 && m_iSgtcCPSide != 0 ) )
		{
			pPlayer->pev->armorvalue = 0;
			pPlayer->pev->health = 50.0f;
			pPlayer->pev->max_health = 50.0f;
		}
		else
		{
			pPlayer->m_bSgtcCloakTick = 0;
			pPlayer->m_iSnagArmorCap = 100;
			pPlayer->pev->armorvalue = 100.0f;
			// #301: same menu-12 budget as the Goa'uld arm — the original's
			// menu-12 "Total" is 6 on both sides (see the note above).
			pPlayer->m_iNukeBonus = 6;		// +0x934 = menu base budget (orig Total 6)
		}
	}

	for( int i = 0; i < SGTC_NUM_WEAPON_SLOTS; i++ )				// ref +0x93c[0x1c] = 1
		pPlayer->m_iProgressTeam[i] = 1;
	SetWeaponAllow( pPlayer );

	SGTC_SpawnResendProgress( pPlayer );

	BOOL bHadLoadout = FALSE;					// ref bVar8
	pPlayer->m_iSgtcGondState = 0;				// ref +0x4f4
	// ref CP grant gate: vcall+0x94 (IsAlive) && !IsObserver — NO +0x4de check.
	// Grants on +0x9ac[i]==1 or freeguns > 1.0 (dump line 67).
	if( pPlayer->IsAlive() && !pPlayer->IsObserver() )
	{
#ifdef BUILD_JK_BOTTI
		SGTC_Bots_PrepareSpawnLoadout( pPlayer );	// fake clients: same buy rules, preference order
#endif
		// client selection is untrusted: re-check at grant time, and grant
		// nothing from a list that could not be checked (#34)
		const BOOL bGondChecked = SGTC_SanitizeGondLoadout( pPlayer );
		for( int i = 0; i < SGTC_NUM_WEAPON_SLOTS; i++ )
		{
			if( ( bGondChecked && pPlayer->m_iSgtcGondArgs[i] == 1 ) ||
				sgtc_cvar_freeguns.value > 1.0f )
			{
				bHadLoadout = TRUE;
				if( i == SGTC_WSLOT_INVIS || i == SGTC_WSLOT_SHIELD )
				{
					SGTC_GondSlotState( pPlayer, i );	// ref +0x4f4 shield-slot state machine
				}
				else
					pPlayer->GiveNamedItem( SGTC_WeaponSlotClassname[i] );	// ref WPLISTB[i]
			}
		}
		// ref: armed shield state + !(pev->weapons bit 9) grants WPLISTB[0x11].
		if( pPlayer->m_iSgtcGondState != 0 &&
			( ( pPlayer->pev->weapons >> 9 ) & 1 ) != 1 )
			pPlayer->GiveNamedItem( SGTC_WeaponSlotClassname[SGTC_WSLOT_INVIS] );	// WPLISTB[17] weapon_invis
		// ref: melee only when solid && !observer.
		if( !pPlayer->IsObserver() && pPlayer->pev->solid != 0 )
		{
			if( pPlayer->pev->team == SGTC_ENTTEAM_GOAULD )
				pPlayer->GiveNamedItem( "weapon_coujaf" );
			else
				pPlayer->GiveNamedItem( "weapon_couteau" );
		}
	}

	if( sgtc_type == 4 )
		pPlayer->m_iSgtcPad4ac = 0;				// ref CP spawn line 81: +0x4ac = 0 only

	pPlayer->m_iSgtcPrevClass = pPlayer->m_iSgtcPortraitShown;
	pPlayer->m_iSgtcPrevPeTeam = (int)pPlayer->pev->team;

	int clientIndex = pPlayer->entindex();
	SGTC_SpawnSubmodelAndObserver( pPlayer );

	MESSAGE_BEGIN( MSG_ONE, gmsgTicketMode, NULL, pPlayer->pev );
		WRITE_BYTE( ( sgtc_type < 2 || sgtc_type == 4 ) ? 1 : 0 );	// ref (type<2 || type==4)
	MESSAGE_END();

	MESSAGE_BEGIN( MSG_ALL, gmsgScoreInfo );
		WRITE_BYTE( clientIndex );
		WRITE_SHORT( SGTC_Rules_roundf( pPlayer->pev->frags ) );	// ref ROUND(pev+0x164)
		WRITE_SHORT( pPlayer->m_iDeaths );
		WRITE_SHORT( pPlayer->m_iSgtcPortraitShown );	// ref: no invis==6 override in CP
		WRITE_SHORT( g_pGameRules->GetTeamIndex( pPlayer->m_szTeamName ) + 1 );
		WRITE_SHORT( 0 );
	MESSAGE_END();

	if( !bHadLoadout )
		SGTC_PackAuto_Start( pPlayer );	// loadout menu pending

	MESSAGE_BEGIN( MSG_ONE, gmsgArmor, NULL, pPlayer->pev );
		WRITE_BYTE( SGTC_ArmorHudWireByte( pPlayer ) );
		WRITE_BYTE( SGTC_ClampWireByte( SGTC_Rules_roundf( sgtc_cvar_superweapons.value ) ) );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 0 );
	MESSAGE_END();

	pPlayer->SendAboutMe( (int)pPlayer->pev->team,
		STRING( pPlayer->pev->netname ), clientIndex );
	AddPortrait( pPlayer );					// ref vtable+0x110 — CP override
	RecountTeams( FALSE );

	if( !bHadLoadout )
	{
		if( sgtc_cvar_wepselmode.value == 1.0f )
			pPlayer->ShowVGUIMenu( 0xc );
		else if( sgtc_cvar_wepselmode.value == 2.0f )
			pPlayer->ShowVGUIMenu( 0x3c );
	}
	// NOTE: ref CP has no +0xa9c (m_bSgtcLatchA9c) clear here.
	if( pPlayer->IsAlive() && !pPlayer->IsObserver() )
		StartPortrait( pPlayer );			// ref vtable+0x114 — CP override

	if( sgtc_type == 4 )
	{
		// ref: the non-side team arms its attack deadline.
		if( m_iSgtcCPSide == 0 )
		{
			if( pPlayer->pev->team == SGTC_ENTTEAM_GOAULD )
				pPlayer->m_flSgtcAttackEnd = sgtc_cvar_attackcountdown.value + sgtc_roundstart;
			else
				pPlayer->m_flSgtcAttackEnd = 0.0f;
		}
		else
		{
			if( pPlayer->pev->team == SGTC_ENTTEAM_TORRI )
				pPlayer->m_flSgtcAttackEnd = sgtc_cvar_attackcountdown.value + sgtc_roundstart;
			else
				pPlayer->m_flSgtcAttackEnd = 0.0f;
		}
	}
}
