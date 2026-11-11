// sgtc_pack_auto.cpp — automatic weapon-pack selection for players who never
// answer the loadout menu.
//
// The pack panel (mp_wepselmode 2, client menu 60) and the budget panel
// (mp_wepselmode 1, client menu 12) are client-side VGUI; the server only sees
// the resulting "gond4h9wp <28 slots>" command.  So the timer is armed at the
// point the menu is expected: a classed player finished PlayerSpawn without a
// granted loadout.  It is cancelled by the menu Apply (gond4h9wp), by
// SGTC_GiveFromMenu (the menu grant) and by the next spawn.
//
// On expiry: pick a random *available* pack for the player's team/class from
// the shared preset table (game_shared/sgtc_packs.h), write the same gond[]
// slots the menu Apply writes, and run the same grant path so the result is
// identical to selecting that pack by hand.  With the budget menu (mode 1)
// there is no pack preset to pick, so the first valid pack preset is used as
// the class default kit and SGTC_SanitizeGondLoadout trims it to the budget.

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "gamerules.h"
#include "sgtc_packs.h"
#include "sgtc_rules.h"
#include "sgtc_enums.h"
#include "sgtc_rea_debug.h"
#include "sgtc_weapons.h"

// Owned by sgtc_round.cpp / sgtc_player.cpp (no shared prototypes in the port).
extern cvar_t sgtc_cvar_wepselmode;
void SGTC_GiveFromMenu( CBasePlayer *pPlayer );

cvar_t sgtc_cvar_pack_autoselect_time = { "sgtc_pack_autoselect_time", "60", FCVAR_SERVER };

void SGTC_PackAutoInit( void )
{
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_pack_autoselect_time );
}

// build-14: true only when the player already has a chosen/non-base loadout.
// The unconditional team melee (couteau/coujaf) and WEAPON_SUIT do not count.
BOOL SGTC_PackAuto_HasChosenLoadout( CBasePlayer *pPlayer )
{
	if( !pPlayer || !pPlayer->pev )
		return FALSE;
	const unsigned int iBaseMeleeBits = ( 1u << SGTC_WPN_COUTEAU ) | ( 1u << SGTC_WPN_COUJAF );
	const unsigned int iWeaponBits = (unsigned int)pPlayer->pev->weapons;
	return ( iWeaponBits & ~iBaseMeleeBits & ~( 1u << 31 ) ) ? TRUE : FALSE;
}

static float PackAutoDelay( void )
{
	float flTime = sgtc_cvar_pack_autoselect_time.value;
	if( flTime < 0.0f )
		flTime = 0.0f;
	if( flTime > 600.0f )
		flTime = 600.0f;
	return flTime;
}

void SGTC_PackAuto_Start( CBasePlayer *pPlayer )
{
	if( !pPlayer || !pPlayer->pev )
		return;
	if( FBitSet( pPlayer->pev->flags, FL_FAKECLIENT ) )
		return;	// bots get their kit directly (SGTC_Bots_GiveSpawnLoadout)
	if( PackAutoDelay() <= 0.0f )
		return;
	if( sgtc_cvar_wepselmode.value < 1.0f )
		return;
	if( !pPlayer->IsAlive() || pPlayer->IsObserver() )
		return;
	if( SGTC_ReaDebugActive() )
		return;	// build-8 debug holds the loadout menu open? no: all weapons are already granted

	const int iTeam = pPlayer->m_iSgtcTeam;
	const int iClass = pPlayer->m_iSgtcPortraitShown;
	if( iTeam != SGTC_TEAM_TORRI && iTeam != SGTC_TEAM_GOAULD )
		return;
	if( iClass < SGTC_CLASS_FIRST || iClass > SGTC_CLASS_LAST )
		return;

	pPlayer->m_flSgtcPackDeadline = gpGlobals->time + PackAutoDelay();
	ALERT( at_console, "SGTC_PACKAUTO {\"cmd\":\"arm\",\"player\":\"%s\",\"team\":%d,\"class\":%d,\"deadline\":%.3f}\n",
		STRING( pPlayer->pev->netname ), iTeam, iClass, pPlayer->m_flSgtcPackDeadline );
}

void SGTC_PackAuto_Cancel( CBasePlayer *pPlayer )
{
	if( !pPlayer )
		return;
	if( pPlayer->m_flSgtcPackDeadline > 0.0f )
	{
		ALERT( at_console, "SGTC_PACKAUTO {\"cmd\":\"cancel\",\"player\":\"%s\"}\n",
			STRING( pPlayer->pev->netname ) );
	}
	pPlayer->m_flSgtcPackDeadline = 0.0f;
}

static int PackAutoPickRandomPack( int iTeam, int iClassIdx )
{
	int iPackIds[SGTC_PACKSEL_COUNT];
	int iValid = 0;
	int iSlots[SGTC_PACK_MAX_SLOTS];

	for( int iPack = 1; iPack <= SGTC_PACKSEL_COUNT; ++iPack )
	{
		if( SGTC_PackGetSlots( iTeam, iClassIdx, iPack, iSlots ) > 0 )
			iPackIds[iValid++] = iPack;
	}
	if( iValid <= 0 )
		return 0;
	return iPackIds[RANDOM_LONG( 0, iValid - 1 )];
}

static int PackAutoBuildGond( int iTeam, int iClassIdx, int iPack, int *pGond )
{
	int iPreset[SGTC_PACK_MAX_SLOTS];
	const int iCount = SGTC_PackGetSlots( iTeam, iClassIdx, iPack, iPreset );
	if( iCount <= 0 )
		return 0;
	for( int i = 0; i < SGTC_NUM_WEAPON_SLOTS; ++i )
		pGond[i] = 0;
	for( int i = 0; i < iCount; ++i )
	{
		if( iPreset[i] >= 0 && iPreset[i] < SGTC_NUM_WEAPON_SLOTS )
			pGond[iPreset[i]] = 1;
	}
	return iCount;
}

void SGTC_PackAuto_Tick( void )
{
	if( !g_pGameRules )
		return;
	if( SGTC_ReaDebugActive() )
	{
		// build-8: no loadout/auto-pack timers in debug mode
		for( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *pPlayer = (CBasePlayer *)CBaseEntity::Instance( INDEXENT( i ) );
			if( pPlayer )
				pPlayer->m_flSgtcPackDeadline = 0.0f;
		}
		return;
	}

	for( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		edict_t *e = INDEXENT( i );
		if( !e || e->free || !e->pvPrivateData )
			continue;
		CBasePlayer *pPlayer = (CBasePlayer *)CBaseEntity::Instance( e );
		if( !pPlayer || pPlayer->m_flSgtcPackDeadline <= 0.0f )
			continue;
		if( FBitSet( pPlayer->pev->flags, FL_FAKECLIENT ) )
		{
			// bots are armed directly (SGTC_Bots_GiveSpawnLoadout)
			pPlayer->m_flSgtcPackDeadline = 0.0f;
			continue;
		}
		// A spawn-granted loadout cancels the pending auto-select: if the player
		// already has weapons, stop the timer instead of overriding their kit.
		// build-14: the unconditional team melee (weapon_couteau / weapon_coujaf)
		// is NOT a chosen loadout — it is granted by every spawn, so counting it
		// cancelled the auto-select immediately and the player stayed knife-only.
		if( SGTC_PackAuto_HasChosenLoadout( pPlayer ) )
		{
			ALERT( at_console, "SGTC_PACKAUTO {\"cmd\":\"cancel\",\"player\":\"%s\",\"reason\":\"armed\"}\n",
				STRING( pPlayer->pev->netname ) );
			pPlayer->m_flSgtcPackDeadline = 0.0f;
			continue;
		}

		// Honour the deadline. The original chain had an empty if-body here, so
		// the random pack was granted on the first tick after arming instead of
		// after sgtc_pack_autoselect_time (which also raced the loadout panel:
		// menu 60 opened after the grant, so sgtc_loadout_done had already run).
		if( gpGlobals->time < pPlayer->m_flSgtcPackDeadline )
			continue;

		pPlayer->m_flSgtcPackDeadline = 0.0f;

		if( !pPlayer->IsAlive() || pPlayer->IsObserver() )
			continue;

		const int iTeam = pPlayer->m_iSgtcTeam;
		const int iClass = pPlayer->m_iSgtcPortraitShown;
		if( iTeam != SGTC_TEAM_TORRI && iTeam != SGTC_TEAM_GOAULD )
			continue;
		if( iClass < SGTC_CLASS_FIRST || iClass > SGTC_CLASS_LAST )
			continue;

		const int iClassIdx = iClass - SGTC_CLASS_FIRST;
		int iChosenPack = 0;
		int gond[SGTC_NUM_WEAPON_SLOTS];

		if( sgtc_cvar_wepselmode.value == 2.0f )
		{
			iChosenPack = PackAutoPickRandomPack( iTeam, iClassIdx );
			if( iChosenPack <= 0 )
				continue;
		}
		else
		{
			// Budget menu: no pack preset was being shown.  The first valid
			// class pack is the default kit; sanitising trims it to the budget.
			iChosenPack = 1;
		}

		if( PackAutoBuildGond( iTeam, iClassIdx, iChosenPack, gond ) <= 0 )
			continue;

		for( int s = 0; s < SGTC_NUM_WEAPON_SLOTS; ++s )
			pPlayer->m_iSgtcGondArgs[s] = gond[s];

		SGTC_GiveFromMenu( pPlayer );

		// The pack/briefing panels may still be open when the 60s timer fires;
		// they keep keyboard focus, so slot1..slot5/invnext never reach the
		// weapon-switch path. Ask the client to dismiss them now that the pack
		// has been granted server-side.
		CLIENT_COMMAND( pPlayer->edict(), "sgtc_loadout_done\n" );

		if( sgtc_cvar_wepselmode.value == 2.0f )
			ClientPrint( pPlayer->pev, HUD_PRINTTALK,
				UTIL_VarArgs( "[SGTC] No pack chosen in time - random Pack %d selected for you.\n", iChosenPack ) );
		else
			ClientPrint( pPlayer->pev, HUD_PRINTTALK,
				"[SGTC] No loadout chosen in time - a default loadout was selected for you.\n" );

		ALERT( at_console, "SGTC_PACKAUTO {\"cmd\":\"select\",\"player\":\"%s\",\"team\":%d,\"class\":%d,\"pack\":%d,\"mode\":%.0f}\n",
			STRING( pPlayer->pev->netname ), iTeam, iClass, iChosenPack, sgtc_cvar_wepselmode.value );
	}
}
