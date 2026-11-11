// sgtc_rea_debug.cpp — build-8 debug cvar: give everyone every weapon and make
// the class/team/pack loadout restrictions inert.
//
// sgtc_rea_debug 0 (default): this file does nothing except register the cvar.
// sgtc_rea_debug 1: on enable, on every PlayerSpawn, on the bot loadout path and
// after SGTC_GiveFromMenu every player gets the full shipped SGTC weapon table
// (31 weapon names, 30 unique classes - weapon_9mmAR aliases weapon_mp5) with
// every ammo pool/clip filled.  The pack sanitizer and pack-auto timer are
// skipped in this mode, so a pack selection cannot strip the debug kit.
//
// Everything is opt-in and easy to remove: delete this file+header, the
// SGTC_ReaDebug* call sites (grep -n "ReaDebug") and the CMake entry.

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "gamerules.h"
#include "sgtc_rea_debug.h"
#include "sgtc_bots.h"

cvar_t sgtc_rea_debug = { "sgtc_rea_debug", "0", FCVAR_SERVER };
// Test-only: when non-zero, each newly armed player is cycled through the full
// 31-entry table once (logs SGTC_READEBUG cycle lines for headless acceptance).
static cvar_t sgtc_rea_debug_cycleprobe = { "sgtc_rea_debug_cycleprobe", "0", FCVAR_SERVER };

#define SGTC_READEBUG_AMMO 500	// large but HUD-safe reserve per ammo pool

// Keep in sync with the shipped table in cl_dll/sgtc_weapons_client.cpp
// (weapon_9mmAR is the same CMP5/id as weapon_mp5, so it is not repeated here).
static const char *s_sgtcReaWeapons[] =
{
	"weapon_batsup", "weapon_coujaf", "weapon_couteau", "weapon_m92s",
	"weapon_flashbang", "weapon_gacgoa", "weapon_glstaff", "weapon_goaregen",
	"weapon_handgrenade", "weapon_invis", "weapon_lance", "weapon_larve",
	"weapon_m16", "weapon_main", "weapon_medic", "weapon_mine",
	"weapon_mp5", "weapon_mustardgrenade", "weapon_narcogrenade",
	"weapon_nervegrenade", "weapon_p90", "weapon_psg1", "weapon_reetou",
	"weapon_shield", "weapon_smokegrenade", "weapon_spas", "weapon_tacgun",
	"weapon_usas", "weapon_zat", "weapon_zatarc", "weapon_c4",
};

// Full shipped weapon table in client-table order (31 names).  weapon_9mmAR
// aliases weapon_mp5's CMP5/id; it is included here so the cycle visits every
// shipped entry, and the cycle prefers a genuinely different carried item.
static const char *s_sgtcReaCycleWeapons[] =
{
	"weapon_batsup", "weapon_coujaf", "weapon_couteau", "weapon_m92s",
	"weapon_flashbang", "weapon_gacgoa", "weapon_glstaff", "weapon_goaregen",
	"weapon_handgrenade", "weapon_invis", "weapon_lance", "weapon_larve",
	"weapon_m16", "weapon_main", "weapon_medic", "weapon_mine",
	"weapon_mp5", "weapon_mustardgrenade", "weapon_narcogrenade",
	"weapon_nervegrenade", "weapon_p90", "weapon_psg1", "weapon_reetou",
	"weapon_shield", "weapon_smokegrenade", "weapon_spas", "weapon_tacgun",
	"weapon_usas", "weapon_zat", "weapon_zatarc", "weapon_c4",
};	// 31 distinct weapon ids (weapon_9mmAR aliases weapon_mp5)

static BOOL s_reaWasActive = FALSE;
static BOOL s_reaCycleProbed[33];
static BOOL s_reaArmed[33];
static int  s_reaArmedCount = 0;

void SGTC_ReaDebugInit( void )
{
	SGTC_RegisterOrAdoptCvar( &sgtc_rea_debug );
	SGTC_RegisterOrAdoptCvar( &sgtc_rea_debug_cycleprobe );
}

BOOL SGTC_ReaDebugActive( void )
{
	// Read the live engine value: a command line/config may create the cvar
	// before the DLL registers it (pfnCVarGetFloat is unreliable there).
	cvar_t *pCvar = CVAR_GET_POINTER( "sgtc_rea_debug" );
	return ( pCvar && pCvar->value > 0.0f ) ? TRUE : FALSE;
}

static BOOL SGTC_ReaDebug_HasWeapon( CBasePlayer *pPlayer, const char *pszClassname )
{
	for( int i = 0; i < MAX_ITEM_TYPES; ++i )
	{
		for( CBasePlayerItem *pItem = pPlayer->m_rgpPlayerItems[i]; pItem; pItem = pItem->m_pNext )
		{
			const char *pszHave = STRING( pItem->pev->classname );
			if( !strcmp( pszHave, pszClassname ) )
				return TRUE;
			// weapon_9mmAR and weapon_mp5 are the same CMP5 (and id).
			if( !strcmp( pszClassname, "weapon_mp5" ) && !strcmp( pszHave, "weapon_9mmAR" ) )
				return TRUE;
		}
	}
	return FALSE;
}

static int SGTC_ReaDebug_CountWeapons( CBasePlayer *pPlayer )
{
	int iCount = 0;
	for( int i = 0; i < MAX_ITEM_TYPES; ++i )
	{
		for( CBasePlayerItem *pItem = pPlayer->m_rgpPlayerItems[i]; pItem; pItem = pItem->m_pNext )
			++iCount;
	}
	return iCount;
}

static void SGTC_ReaDebug_FillAmmo( CBasePlayer *pPlayer )
{
	for( int i = 0; i < MAX_AMMO_SLOTS; ++i )
		pPlayer->m_rgAmmo[i] = SGTC_READEBUG_AMMO;

	for( int i = 0; i < MAX_ITEM_TYPES; ++i )
	{
		for( CBasePlayerItem *pItem = pPlayer->m_rgpPlayerItems[i]; pItem; pItem = pItem->m_pNext )
		{
			ItemInfo info;
			memset( &info, 0, sizeof( info ) );
			if( pItem->GetItemInfo( &info ) && info.iMaxClip > 0 )
			{
				CBasePlayerWeapon *pWeapon = (CBasePlayerWeapon *)pItem;
				pWeapon->m_iClip = info.iMaxClip;
			}
			pItem->UpdateClientData( pPlayer );
		}
	}
	pPlayer->SendAmmoUpdate();
}

static BOOL s_reaProbed[33];

// Fire probe (debug mode only): once the player's team is known, switch to a
// weapon of the OTHER team and fire it, so a headless run can prove the
// class/team restriction is inert.  Logs clip/ammo delta and "fired".
static void SGTC_ReaDebug_FireProbe( CBasePlayer *pPlayer )
{
	if( !pPlayer || !pPlayer->pev )
		return;

	const char *pszOtherWeapon = ( (int)pPlayer->pev->team == 2 )
		? "weapon_zat"		// Torri player firing a Goa'uld weapon
		: "weapon_m16";		// Goa'uld player firing a Torri weapon
	for( int i = 0; i < MAX_ITEM_TYPES; ++i )
	{
		CBasePlayerItem *pProbe = NULL;
		for( CBasePlayerItem *pItem = pPlayer->m_rgpPlayerItems[i]; pItem; pItem = pItem->m_pNext )
		{
			if( !strcmp( STRING( pItem->pev->classname ), pszOtherWeapon ) )
			{ pProbe = pItem; break; }
		}
		if( !pProbe )
			continue;

		int iAmmoBefore = 0;
		for( int a = 0; a < MAX_AMMO_SLOTS; ++a )
			iAmmoBefore += pPlayer->m_rgAmmo[a];

		pPlayer->SwitchWeapon( pProbe );
		int iClipBefore = 0;
		if( pPlayer->m_pActiveItem )
		{
			CBasePlayerWeapon *pWeapon = (CBasePlayerWeapon *)pPlayer->m_pActiveItem;
			iClipBefore = pWeapon->m_iClip;
			iAmmoBefore += ( iClipBefore > 0 ) ? iClipBefore : 0;
			pWeapon->m_flNextPrimaryAttack = 0.0f;	// deploy cooldown: force the probe shot
			pWeapon->PrimaryAttack();
		}

		int iAmmoAfter = 0;
		for( int a = 0; a < MAX_AMMO_SLOTS; ++a )
			iAmmoAfter += pPlayer->m_rgAmmo[a];
		int iClipAfter = 0;
		if( pPlayer->m_pActiveItem )
		{
			iClipAfter = ((CBasePlayerWeapon *)pPlayer->m_pActiveItem)->m_iClip;
			iAmmoAfter += ( iClipAfter > 0 ) ? iClipAfter : 0;
		}

		UTIL_LogPrintf(
			"SGTC_READEBUG {\"cmd\":\"fireprobe\",\"player\":\"%s\",\"team\":%d,\"weapon\":\"%s\","
			"\"active\":\"%s\",\"ammo_before\":%d,\"ammo_after\":%d,\"clip_before\":%d,\"clip_after\":%d,\"fired\":%d}\n",
			STRING( pPlayer->pev->netname ), (int)pPlayer->pev->team, pszOtherWeapon,
			pPlayer->m_pActiveItem ? STRING( pPlayer->m_pActiveItem->pev->classname ) : "",
			iAmmoBefore, iAmmoAfter, iClipBefore, iClipAfter, ( iAmmoAfter < iAmmoBefore ) ? 1 : 0 );
		break;
	}
}

static CBasePlayerItem *SGTC_ReaDebug_FindWeaponItem( CBasePlayer *pPlayer, const char *pszClassname )
{
	for( int i = 0; i < MAX_ITEM_TYPES; ++i )
	{
		for( CBasePlayerItem *pItem = pPlayer->m_rgpPlayerItems[i]; pItem; pItem = pItem->m_pNext )
		{
			const char *pszHave = STRING( pItem->pev->classname );
			if( !strcmp( pszHave, pszClassname ) )
				return pItem;
			// weapon_9mmAR and weapon_mp5 are the same CMP5 (and id).
			if( ( !strcmp( pszClassname, "weapon_mp5" ) && !strcmp( pszHave, "weapon_9mmAR" ) ) ||
				( !strcmp( pszClassname, "weapon_9mmAR" ) && !strcmp( pszHave, "weapon_mp5" ) ) )
				return pItem;
		}
	}
	return NULL;
}

// sgtc_next / sgtc_prev: cycle the calling rea-debug player through the full
// shipped weapon table, ignoring inventory slots/packs.  Active only while
// sgtc_rea_debug is 1 (the command path checks that; this re-checks).
void SGTC_ReaDebug_CycleWeapon( CBasePlayer *pPlayer, int iDirection )
{
	if( !pPlayer || !pPlayer->pev || !SGTC_ReaDebugActive() )
		return;
	if( !pPlayer->IsAlive() || pPlayer->IsObserver() )
		return;
	if( iDirection == 0 )
		iDirection = 1;

	static BOOL s_bSgtcCycleLogged = FALSE;
	if( !s_bSgtcCycleLogged )
	{
		s_bSgtcCycleLogged = TRUE;
		UTIL_LogPrintf( "[sgtc_rea_debug] weapon cycle active (sgtc_next/sgtc_prev)\n" );
	}

	const int iCount = (int)( sizeof( s_sgtcReaCycleWeapons ) / sizeof( s_sgtcReaCycleWeapons[0] ) );
	int iCurrent = -1;
	CBasePlayerItem *pActive = pPlayer->m_pActiveItem;
	if( pActive )
	{
		const char *pszActiveClass = STRING( pActive->pev->classname );
		// CMP5's entity classname is weapon_9mmAR (weapon_mp5 aliases it); use the
		// mp5 entry as the current index so forward cycling continues 17..30
		// instead of treating the alias as a distinct weapon.
		if( !strcmp( pszActiveClass, "weapon_9mmAR" ) )
			pszActiveClass = "weapon_mp5";
		for( int i = 0; i < iCount; ++i )
		{
			if( !strcmp( pszActiveClass, s_sgtcReaCycleWeapons[i] ) )
			{
				iCurrent = i;
				break;
			}
		}
	}

	for( int iStep = 1; iStep <= iCount; ++iStep )
	{
		int iNext = ( iCurrent + iDirection * iStep ) % iCount;
		if( iNext < 0 )
			iNext += iCount;
		CBasePlayerItem *pNext = SGTC_ReaDebug_FindWeaponItem( pPlayer, s_sgtcReaCycleWeapons[iNext] );
		if( !pNext || pNext == pActive )
			continue;
		pPlayer->SwitchWeapon( pNext );
		UTIL_LogPrintf(
			"SGTC_READEBUG {\"cmd\":\"cycle\",\"player\":\"%s\",\"dir\":%d,\"index\":%d,"
			"\"weapon\":\"%s\",\"active\":\"%s\"}\n",
			STRING( pPlayer->pev->netname ), iDirection, iNext, s_sgtcReaCycleWeapons[iNext],
			pPlayer->m_pActiveItem ? STRING( pPlayer->m_pActiveItem->pev->classname ) : "" );
		return;
	}
}

void SGTC_ReaDebug_ArmPlayer( CBasePlayer *pPlayer )
{
	if( !pPlayer || !pPlayer->pev || !SGTC_ReaDebugActive() )
		return;
	if( !pPlayer->IsAlive() || pPlayer->IsObserver() )
		return;

	// build-20: bots are never given the 31-weapon debug kit.  On sg_stargate
	// (7 bots + rea_debug) arming every bot pushed the engine into
	// Host_Error: SV_AllocEdict; bots keep their normal class loadout instead.
	// Use the bot registry: FL_FAKECLIENT is not necessarily set when the bot
	// loadout/rea grant runs, which is why the earlier flag test never fired.
	if( SGTC_Bots_IsBotPlayer( pPlayer ) )
	{
		UTIL_LogPrintf( "SGTCEVENT {\"event\":\"edict_budget\",\"player\":\"%s\","
			"\"reason\":\"bot_rea_skip\",\"used\":%d,\"max\":%d}\n",
			STRING( pPlayer->pev->netname ), (int)NUMBER_OF_ENTITIES(), gpGlobals->maxEntities );
		return;
	}

	int iGranted = 0;
	const int iWeapons = (int)( sizeof( s_sgtcReaWeapons ) / sizeof( s_sgtcReaWeapons[0] ) );
	for( int i = 0; i < iWeapons; ++i )
	{
		if( SGTC_ReaDebug_HasWeapon( pPlayer, s_sgtcReaWeapons[i] ) )
			continue;
		// build-20: never let the debug grant itself drive the engine past
		// MAX_EDICTS (Host_Error: SV_AllocEdict is fatal).  Trim the last items
		// and leave a clear replay marker instead.
		if( (int)NUMBER_OF_ENTITIES() >= gpGlobals->maxEntities - 3 )
		{
			UTIL_LogPrintf( "SGTCEVENT {\"event\":\"edict_budget\",\"player\":\"%s\","
				"\"reason\":\"rea_debug_grant\",\"used\":%d,\"max\":%d,\"granted\":%d,"
				"\"skipped\":\"%s\"}\n",
				STRING( pPlayer->pev->netname ), (int)NUMBER_OF_ENTITIES(), gpGlobals->maxEntities,
				iGranted, s_sgtcReaWeapons[i] );
			break;
		}
		pPlayer->GiveNamedItem( s_sgtcReaWeapons[i] );
		++iGranted;
	}

	SGTC_ReaDebug_FillAmmo( pPlayer );

	const int iIndex = ENTINDEX( pPlayer->edict() );
	if( iIndex > 0 && iIndex < (int)( sizeof( s_reaArmed ) / sizeof( s_reaArmed[0] ) ) && !s_reaArmed[iIndex] )
	{
		s_reaArmed[iIndex] = TRUE;
		++s_reaArmedCount;
		UTIL_LogPrintf(
			"SGTC_READEBUG {\"cmd\":\"arm\",\"player\":\"%s\",\"team\":%d,\"class\":%d,"
			"\"granted\":%d,\"weapons\":%d,\"items\":%d,\"ammo\":%d}\n",
			STRING( pPlayer->pev->netname ), (int)pPlayer->pev->team,
			(int)pPlayer->m_iSgtcPortraitShown, iGranted, (int)pPlayer->pev->weapons,
			SGTC_ReaDebug_CountWeapons( pPlayer ), SGTC_READEBUG_AMMO );
	}
}

void SGTC_ReaDebug_OnPlayerSpawn( CBasePlayer *pPlayer )
{
	if( !SGTC_ReaDebugActive() )
		return;
	SGTC_ReaDebug_ArmPlayer( pPlayer );
}

static void SGTC_ReaDebug_ArmAll( void )
{
	int iPlayers = 0;
	for( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		edict_t *e = INDEXENT( i );
		if( !e || e->free || !e->pvPrivateData )
			continue;
		CBasePlayer *pPlayer = (CBasePlayer *)CBaseEntity::Instance( e );
		if( !pPlayer || !pPlayer->pev )
			continue;
		if( !pPlayer->IsAlive() || pPlayer->IsObserver() )
			continue;
		SGTC_ReaDebug_ArmPlayer( pPlayer );
		++iPlayers;
	}
	if( iPlayers > 0 )
		UTIL_LogPrintf( "[sgtc_rea_debug] all weapons granted to %d player(s)\n", iPlayers );
}

void SGTC_ReaDebug_Tick( void )
{
	const BOOL bActive = SGTC_ReaDebugActive();

	if( bActive != s_reaWasActive )
	{
		s_reaWasActive = bActive;
		memset( s_reaArmed, 0, sizeof( s_reaArmed ) );
		memset( s_reaProbed, 0, sizeof( s_reaProbed ) );
		memset( s_reaCycleProbed, 0, sizeof( s_reaCycleProbed ) );
		s_reaArmedCount = 0;

		if( bActive )
		{
			UTIL_LogPrintf( "[sgtc_rea_debug] enabled\n" );
			SGTC_ReaDebug_ArmAll();
		}
		else
		{
			UTIL_LogPrintf( "[sgtc_rea_debug] disabled\n" );
		}
	}

	if( !bActive || !g_pGameRules )
		return;

	// Re-arm players that spawned/became alive since the last sample.
	static double s_flNextArm = 0.0;
	if( gpGlobals->time < s_flNextArm )
		return;
	s_flNextArm = gpGlobals->time + 0.5;

	for( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		edict_t *e = INDEXENT( i );
		if( !e || e->free || !e->pvPrivateData )
			continue;
		CBasePlayer *pPlayer = (CBasePlayer *)CBaseEntity::Instance( e );
		if( !pPlayer || !pPlayer->pev )
			continue;
		if( i < (int)( sizeof( s_reaArmed ) / sizeof( s_reaArmed[0] ) ) && s_reaArmed[i] )
		{
			if( !s_reaProbed[i] && pPlayer->pev->team > 0 && pPlayer->IsAlive() )
			{
				s_reaProbed[i] = TRUE;
				SGTC_ReaDebug_FireProbe( pPlayer );
			}
			// Headless acceptance helper: drive the full 31-entry cycle once and
			// log every step (same function the sgtc_next/sgtc_prev commands use).
			if( !s_reaCycleProbed[i] && sgtc_rea_debug_cycleprobe.value > 0.0f && pPlayer->IsAlive() )
			{
				s_reaCycleProbed[i] = TRUE;
				const int iCount = (int)( sizeof( s_sgtcReaCycleWeapons ) / sizeof( s_sgtcReaCycleWeapons[0] ) );
				for( int n = 0; n < iCount; ++n )
					SGTC_ReaDebug_CycleWeapon( pPlayer, 1 );
				UTIL_LogPrintf( "[sgtc_rea_debug] cycle probe done for %s (%d steps)\n",
					STRING( pPlayer->pev->netname ), iCount );
			}
			continue;
		}
		if( !pPlayer->IsAlive() || pPlayer->IsObserver() )
			continue;
		SGTC_ReaDebug_ArmPlayer( pPlayer );
	}
}
