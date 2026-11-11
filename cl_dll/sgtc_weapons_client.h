// sgtc_weapons_client.h — StargateTC client weapon prediction table (NEW file).
//
// Client half of the SGTC arsenal: weapon IDs, slots, ammo names, view/world
// models, and client-predictable fire events. Mirrors the stock pattern in
// cl_dll/hl/hl_weapons.cpp (HUD_PrepEntity + HUD_WeaponsPostThink switch):
// the integrator adds SGTC cases to that switch; this file only owns the
// SGTC data (IDs/slots/ammo/models/events) plus the registration helper.
//
// Truth sources:
//   - Weapon iId/slot/pos/maxClip/maxAmmo: GetItemInfo disassembly of
//     ref/stc_i386.so (2.0 Linux server, unstripped). Melee IDs from the
//     direct ItemInfo writes (batsup=20, couteau=3, coujaf=1).
//   - Link entity -> class: weapon_* thunks in ref/stc_i386.so
//     (weapon_c4 = CSatchel alias, weapon_mine = CTripmine).
//   - View/world models + fire sounds: stargatetc/ string tables +
//     inventory_client_dll.json.
//   - Events: sgtc_protocol.cpp SGTC_EV_LIST (same .sc names).
// Stock files are NEVER edited; see yield report for hook lines.
#pragma once

// SGTC weapon IDs (server-authoritative, from GetItemInfo disasm).
// Stock HL IDs 1-15 collide by design: SGTC reuses the boththewire slots
// (server sends m_iId; client switch must map SGTC IDs, not stock classes).
enum SGTCWeaponId
{
	SGTC_WPN_FLASHBANG = 0,
	SGTC_WPN_COUJAF = 1,
	SGTC_WPN_M92S = 2,
	SGTC_WPN_COUTEAU = 3,
	SGTC_WPN_MP5 = 4,
	SGTC_WPN_PSG1 = 5,
	SGTC_WPN_P90 = 6,
	SGTC_WPN_SHOTGUN_STOCK = 7, // stock CShotgun slot; SGTC spas/usas below
	SGTC_WPN_TAC = 8,
	SGTC_WPN_INVIS = 9,
	SGTC_WPN_GACGOA = 10, // CGrenadeChoc class, link weapon_gacgoa
	SGTC_WPN_M16 = 11,
	SGTC_WPN_HANDGRENADE = 12,
	SGTC_WPN_TRIPMINE = 13, // link weapon_mine = CTripmine
	SGTC_WPN_SATCHEL = 14, // link weapon_c4 = CSatchel alias
	SGTC_WPN_USAS = 15,
	SGTC_WPN_MAIN = 16, // CMainDieu "hand device"
	SGTC_WPN_ZAT = 17,
	SGTC_WPN_BIGLANCE = 18, // CBigLanceJaffa (deathglider)
	SGTC_WPN_LANCE = 19, // CLanceJaffa (staff weapon)
	SGTC_WPN_BATSUP = 20, // CBatonSupplice (melee)
	SGTC_WPN_REETOU = 21,
	SGTC_WPN_LARVE = 22,
	SGTC_WPN_ZATARC = 23,
	SGTC_WPN_MEDIC = 24, // CMedicx (human)
	SGTC_WPN_SHIELD = 25, // weapon_shield (CShieldGun; v_/w_/p_ models are asset-gapped)
	SGTC_WPN_SMOKEGRENADE = 26,
	SGTC_WPN_NERVEGRENADE = 27,
	SGTC_WPN_NARCOGRENADE = 28,
	SGTC_WPN_MUSTARDGRENADE = 29,
	SGTC_WPN_GOAREGEN = 30, // CMedicxgoa (goauld regen)
	// weapon_9mmAR aliases weapon_mp5's CMP5 class (both link thunks call
	// GetClassPtr<CMP5>) and its ItemInfo reports iId = 4, so there is no
	// separate 9mmAR id; use SGTC_WPN_MP5 for that link.
};

// One row per client-linkable weapon (31 links in client.dll; weapon_s is
// the generic slot alias, weapon_c4 covered by SATCHEL row).
struct SGTCClientWeaponInfo
{
	const char *linkName; // weapon_* entity name
	int iId; // SGTC_WPN_* (matches server m_iId)
	int iSlot; // HUD bucket (GetItemInfo iSlot)
	int iPos; // bucket position (GetItemInfo iPosition)
	const char *pszAmmo1; // primary ammo registry name
	int iMaxAmmo1;
	const char *pszAmmo2; // secondary ammo registry name (only weapon_invis uses it)
	int iMaxAmmo2;
	int iMaxClip;
	const char *pszViewModel; // models/v_*.mdl
	const char *pszWorldModel; // models/p_*.mdl (w_ in table below)
	const char *pszEvent; // events/*.sc (NULL = silent/melee)
};

// Full table, in link-name order. All ItemInfo fields are REA-verified
// against ref/stc_i386.so GetItemInfo decompiles (ammo names are the exact
// .rodata registry strings); prediction compiles without them
// (Deploy/Holster/Idle only); fire-effect strings come from events.
extern const SGTCClientWeaponInfo g_SGTCClientWeapons[];
extern const int g_SGTCClientWeaponCount;

// Called from CHudAmmo::VidInit (cl_dll/ammo.cpp), after gWR.Init() and
// before gWR.LoadAllWeaponSprites(), so every SGTC weapon gets a HUD
// slot/bucket even if a WeaponList message is missed (late join). Rows the
// server already sent are left untouched; the server remains authoritative
// for ammo types. Does NOT allocate prediction entities — the stock
// HUD_WeaponsPostThink switch only knows the stock WEAPON_* ids, so SGTC
// weapons stay server-authoritative and are handled by the viewmodel fallback
// below.
void SGTC_ClientWeaponsInit( void );

// Lookup by server m_iId (for the HUD_WeaponsPostThink switch).
const SGTCClientWeaponInfo *SGTC_ClientWeaponById( int iId );

// First-person viewmodel for a server m_iId, or NULL if the id is not an SGTC
// weapon. Used as a fallback when the server reports an SGTC weapon as active
// but the client has no viewmodel index for it.
const char *SGTC_ClientViewModelForId( int iId );
