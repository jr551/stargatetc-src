// sp_shims.cpp — single-player shims for stock Half-Life entities that the SGTC
// single-player maps still place but the StargateTC reference build never
// registers.
//
// Background
// ----------
// The SG2 Alpha3 SP maps were authored against the stock Half-Life game DLL and
// place stock HL weapon/ammo entities (weapon_9mmhandgun, weapon_shotgun,
// weapon_crossbow, weapon_rpg, weapon_satchel, weapon_crowbar, ammo_crossbow,
// ammo_rpgclip, ammo_ARgrenades, ammo_9mmbox). The SGTC reference build
// (stc_i386.so) has no spawn function for those class names (see the #111 note
// in glock.cpp: the ref link table has neither weapon_9mmhandgun nor
// weapon_glock), so the engine drops those entities with:
//
//   Error: No spawn function for "weapon_9mmhandgun"
//
// and the maps are left without their pickups/weapons. This file adds the
// missing spawn functions as SP-only shims that substitute the closest SGTC
// implementation for each stock class, so the stock entities keep their
// authored placement (origin etc.) and grant usable SGTC gear.
//
// Mapping (stock -> SGTC substitute / rationale)
// ----------------------------------------------
//   weapon_9mmhandgun  -> CM92s          mod sidearm pistol ("M92S")
//   weapon_shotgun     -> CShotgun       the class weapon_spas already maps to
//   weapon_crossbow    -> CPsg1          scoped precision weapon (PSG1)
//   weapon_rpg         -> CLanceJaffa    projectile launcher (Jaffa lance)
//   weapon_satchel     -> CSatchel       the class weapon_c4 already maps to
//   weapon_crowbar     -> CBatonSupplice melee (crowbar is its stated analogue)
//   ammo_crossbow      -> 10x "psg1 clip" (same grant as the ammo_psg1 pickup)
//   ammo_rpgclip       -> 10x "Energie3"  (CLanceJaffa ammo, regen weapon)
//   ammo_ARgrenades    -> 1x "Hand Grenade" (CHandGrenade ammo, cap 3)
//   ammo_9mmbox        -> 50x "9mmC"      (CM92s ammo, cap 60)
//
// Multiplayer is deliberately untouched
// -------------------------------------
// The reference build drops these entities in MP and the MP map set still
// places some of them (e.g. gateroom: 4x weapon_9mmhandgun, 2x weapon_shotgun,
// 2x weapon_satchel). To keep MP behaviour byte-identical to the reference,
// every shim removes itself when CGameRules::IsMultiplayer() is true — i.e. it
// reproduces exactly "entity not spawned". Only CHalfLifeRules (the
// !deathmatch single-player rules path, see gamerules.cpp) sees real pickups.
//
// These shims are additive and are NOT part of the reference link table, hence
// they live here rather than in sgtc_links.cpp (which mirrors aliases that the
// reference binary itself resolves).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "sgtc_weapons.h"

// ---------------------------------------------------------------------------
// SP gate.
//
// "Genuine single-player" = the non-multiplayer rules object (deathmatch 0, so
// CHalfLifeRules from gamerules.cpp) AND a one-client server. g_pGameRules is
// created in CWorld::Precache(), i.e. before any other map entity spawns, so
// it is valid here; gpGlobals->maxClients is fixed by the engine from
// maxplayers before the map load.
//
// The maxClients<=1 half matters for the dedicated bot soaks (deathmatch 0,
// maxplayers 8): those must keep the reference behaviour, not gain pickups.
// ---------------------------------------------------------------------------
static bool SPShimsActive( void )
{
	return g_pGameRules != NULL
		&& !g_pGameRules->IsMultiplayer()
		&& gpGlobals->maxClients <= 1;
}

// ---------------------------------------------------------------------------
// Weapon shims. The shim only overrides Spawn(): when multiplayer is active it
// removes the entity (matching the reference "no spawn function" outcome),
// otherwise it runs the substitute class's own Spawn/Precache so the pickup is
// a fully functional SGTC weapon (models, ammo pool, fire modes, save/restore
// data all inherited).
// ---------------------------------------------------------------------------
#define SP_WEAPON_SHIM( shimClass, substituteClass )		\
	class shimClass : public substituteClass				\
	{														\
	public:													\
		void Spawn( void );									\
	};														\
	void shimClass::Spawn( void )							\
	{														\
		if( !SPShimsActive() )								\
		{													\
			UTIL_Remove( this );							\
			return;											\
		}													\
		substituteClass::Spawn();							\
	}

SP_WEAPON_SHIM( CSpShimPistol, CM92s )
SP_WEAPON_SHIM( CSpShimShotgun, CShotgun )
SP_WEAPON_SHIM( CSpShimCrossbow, CPsg1 )
SP_WEAPON_SHIM( CSpShimRpg, CLanceJaffa )
SP_WEAPON_SHIM( CSpShimSatchel, CSatchel )
SP_WEAPON_SHIM( CSpShimCrowbar, CBatonSupplice )

// ---------------------------------------------------------------------------
// Ammo shims. One class handles all four stock ammo class names; the table
// below maps the spawned entity's classname to the SGTC ammo pool it should
// fill. Kept data-driven so the stock->SGTC substitution is obvious in one
// place and easy to re-tune.
// ---------------------------------------------------------------------------
struct spAmmoShim_t
{
	const char	*pszClassname;	// stock HL map entity class
	const char	*pszModel;		// world model used for the pickup
	const char	*pszAmmoName;	// SGTC ammo pool name (GetItemInfo pszAmmo1)
	int			iGive;			// rounds granted per pickup
	int			iMaxCarry;		// SGTC pool cap (the weapon's iMaxAmmo1)
};

static const spAmmoShim_t g_spAmmoShims[] =
{
	// stock "9mm box" -> CM92s 9mmC pool (CM92s::GetItemInfo: max 60)
	{ "ammo_9mmbox", "models/w_9mm.mdl", SGTC_AMMO_9MMC, 50, 60 },
	// stock crossbow bolts -> CPsg1 "psg1 clip" (same grant as ammo_psg1 pickup)
	{ "ammo_crossbow", "models/w_9mm.mdl", SGTC_AMMO_PSG1, 10, 50 },
	// stock RPG rockets -> CLanceJaffa "Energie3" (regen weapon, cap 22)
	{ "ammo_rpgclip", "models/w_9mm.mdl", SGTC_AMMO_ENERGIE3, 10, 22 },
	// stock M203 grenades -> CHandGrenade "Hand Grenade" (cap 3)
	{ "ammo_ARgrenades", "models/w_grenade.mdl", SGTC_AMMO_HAND, 1, 3 },
};

class CSpShimAmmo : public CBasePlayerAmmo
{
public:
	void Spawn( void );
	void Precache( void );
	BOOL AddAmmo( CBaseEntity *pOther );

private:
	const spAmmoShim_t *m_pShim;
};

void CSpShimAmmo::Spawn( void )
{
	m_pShim = NULL;
	const char *pszClassname = STRING( pev->classname );
	for( int i = 0; i < (int)( sizeof( g_spAmmoShims ) / sizeof( g_spAmmoShims[0] ) ); ++i )
	{
		if( FStrEq( pszClassname, g_spAmmoShims[i].pszClassname ) )
		{
			m_pShim = &g_spAmmoShims[i];
			break;
		}
	}

	if( !m_pShim || !SPShimsActive() )
	{
		UTIL_Remove( this );
		return;
	}

	Precache();
	SET_MODEL( ENT( pev ), m_pShim->pszModel );
	CBasePlayerAmmo::Spawn();
}

void CSpShimAmmo::Precache( void )
{
	if( !m_pShim )
		return;

	PRECACHE_MODEL( (char *)m_pShim->pszModel );
	PRECACHE_SOUND( "items/9mmclip1.wav" );
}

BOOL CSpShimAmmo::AddAmmo( CBaseEntity *pOther )
{
	if( !m_pShim )
		return FALSE;

	if( pOther->GiveAmmo( m_pShim->iGive, m_pShim->pszAmmoName, m_pShim->iMaxCarry ) != -1 )
	{
		EMIT_SOUND( ENT( pev ), CHAN_ITEM, "items/9mmclip1.wav", 1, ATTN_NORM );
		return TRUE;
	}
	return FALSE;
}

// ---------------------------------------------------------------------------
// Registrations. Only stock Half-Life class names that SGTC SP maps place and
// the reference build does not provide.
// ---------------------------------------------------------------------------
LINK_ENTITY_TO_CLASS( weapon_9mmhandgun, CSpShimPistol )
LINK_ENTITY_TO_CLASS( weapon_shotgun, CSpShimShotgun )
LINK_ENTITY_TO_CLASS( weapon_crossbow, CSpShimCrossbow )
LINK_ENTITY_TO_CLASS( weapon_rpg, CSpShimRpg )
LINK_ENTITY_TO_CLASS( weapon_satchel, CSpShimSatchel )
LINK_ENTITY_TO_CLASS( weapon_crowbar, CSpShimCrowbar )

LINK_ENTITY_TO_CLASS( ammo_9mmbox, CSpShimAmmo )
LINK_ENTITY_TO_CLASS( ammo_crossbow, CSpShimAmmo )
LINK_ENTITY_TO_CLASS( ammo_rpgclip, CSpShimAmmo )
LINK_ENTITY_TO_CLASS( ammo_ARgrenades, CSpShimAmmo )
