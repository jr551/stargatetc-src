// sgtc_wpn_invis.cpp — StargateTC CInvis (split from sgtc_weapons.cpp; NEW file).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
#include "sgtc_weapons.h"

// ===========================================================================
// CInvis - personal cloaking device. Analogue: gauss.cpp (charge device).
// Events: events/invis.sc (activate) / events/shield.sc (shield effect).
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_invis, CInvis );

// Save/restore (#99). Not saved: m_flInvisArmTime / m_flSgtcChargeLatch (written, never read),
// m_usInvisFire/m_usShieldFire (event handles). The cloak itself lives in the
// player (m_bShieldSprint).
TYPEDESCRIPTION CInvis::m_SaveData[] =
{
	DEFINE_FIELD( CInvis, m_flShieldLoadLatch, FIELD_TIME ),
};

IMPLEMENT_SAVERESTORE( CInvis, CBasePlayerWeapon )

void CInvis::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_invis.mdl" );
	m_iId = SGTC_WPN_INVIS;
	m_iClip = -1;
	m_iDefaultAmmo = 10;
	m_flInvisArmTime = 0.0f;
	m_flShieldLoadLatch = 0.0f;
	m_flSgtcChargeLatch = 0.0f;
	FallInit();
}

void CInvis::Precache( void )
{
	PRECACHE_MODEL( "models/w_invis.mdl" );
	PRECACHE_MODEL( "models/v_invis.mdl" );
	PRECACHE_MODEL( "models/p_invis.mdl" );
	PRECACHE_SOUND( "weapons/invis.wav" );
	PRECACHE_SOUND( "empty.wav" );	// ref precaches root "empty.wav", not weapons/
	PRECACHE_SOUND( "forcefield/forcefield_fail.wav" );
	PRECACHE_SOUND( "forcefield/forcefield_loading.wav" );
	PRECACHE_SOUND( "forcefield/forcefield_unloading.wav" );

	m_usInvisFire = PRECACHE_EVENT( 1, "events/invis.sc" );
	m_usShieldFire = PRECACHE_EVENT( 1, "events/shield.sc" );
}

int CInvis::GetItemInfo( ItemInfo *p )
{
	p->pszName = STRING( pev->classname );
	p->pszAmmo1 = SGTC_AMMO_INVIS;
	p->iMaxAmmo1 = 10;
	p->pszAmmo2 = SGTC_AMMO_INVIS;
	p->iMaxAmmo2 = 10;
	p->iMaxClip = WEAPON_NOCLIP;
	p->iSlot = 4;
	p->iPosition = 3;
	p->iFlags = 5;
	p->iId = m_iId = SGTC_WPN_INVIS;
	p->iWeight = -10;
	return 1;
}

SGTC_ADD_TO_PLAYER( CInvis )

BOOL CInvis::Deploy( void )
{
	// The cloak lives on the player (m_bShieldSprint) and survives weapon
	// switches, so there is no weapon-side copy to reset here.
	m_flShieldLoadLatch = 0.0f;
	return SGTC_DEPLOY( 2, "models/v_invis.mdl", "models/p_invis.mdl", 2, "python" );	// ref @0x10ba04 anim idx 2, not SGTC_DRAW
}

// ref @0x10ba76
void CInvis::Holster( int skiplocal )
{
	if( !m_pPlayer )
		return;

	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.3f;	// ref player +0x39c

	if( m_pPlayer->m_bShieldSprint )
	{
		EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "weapons/invis.wav",
			1.0f, 0.8f );
	}

	SetThink( NULL );	// ref zeroes +0x134/+0x138 only — does NOT clear +0x200/+0x204

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] == 0 )
		pev->nextthink = gpGlobals->time + 0.1f;

	// ref @0xfba76: no CBasePlayerWeapon::Holster — no viewmodel/weaponmodel clear.
}

// ref @0x10bb62
void CInvis::PrimaryAttack( void )
{
	if( !m_pPlayer )
		return;
	if( m_pPlayer->m_bShieldWall )
		return;
	if( m_pPlayer->m_iSgtcGondState == 2 )	// ref player +0x4f4
		return;
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 3 )
		return;

	EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "weapons/invis.wav", 1.0f, 0.8f );

	m_pPlayer->SGTC_ToggleVis();

	// Read the cloak state back from the player after the toggle.
	if( m_pPlayer->m_bShieldSprint )
	{
		m_flInvisArmTime = gpGlobals->time;
		SetThink( &CInvis::InvisThink );
		pev->nextthink = gpGlobals->time + 0.1f;
	}
	else
	{
		SetThink( NULL );	// ref zeroes +0x134/+0x138; +0x200 left stale
	}

	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.5f;	// ref this+0x1dc
	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usInvisFire,
		0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0, 0, 0, 0, 0, 0 );
	SendWeaponAnim( 1 );	// ref vcall+0x160(this, 1, 1, 0)

	m_flSgtcChargeLatch = gpGlobals->time;	// ref this+0x1fc
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;	// ref player +0x39c
}

// ref @0x10bd92 — forcefield_loading / fail / unload (not cloak toggle).
void CInvis::SecondaryAttack( void )
{
	if( !m_pPlayer )
		return;

	// ref gate: skip when cloak active OR gate-travel pending OR on ladder.
	if( m_pPlayer->m_bShieldSprint )
		return;
	if( gpGlobals->time < m_pPlayer->m_flGateTravel )	// ref player +0x408
		return;
	if( m_pPlayer->IsOnLadder() )
		return;

	if( m_pPlayer->m_iSgtcGondState == 1 )	// ref player +0x4f4 != 1 required
		return;
	if( m_pPlayer->m_flNextAttack > UTIL_WeaponTimeBase() )	// ref player +0x39c <= WTB
		return;

	// ref pev+0x1a4 colormap bit 9; player +0x48c → m_flSgtcLatch48c shield-load cooldown.
	const BOOL bColormapGate = ( ( m_pPlayer->pev->colormap >> 9 ) & 1 ) != 0;

	if( !m_pPlayer->m_bShieldWall && bColormapGate )
	{
		// ref player +0x48c < gpGlobals->time && ammo > 2
		if( m_pPlayer->m_flSgtcLatch48c < gpGlobals->time &&
			m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 2 )
		{
			EMIT_SOUND( ENT( pev ), CHAN_ITEM,
				"forcefield/forcefield_loading.wav", 1.0f, 0.8f );
			m_flShieldLoadLatch = gpGlobals->time;	// ref this+0x204
			SendWeaponAnim( 1 );	// ref vcall+0x160(this, 1, 1, 0)
		}
	}
	else if( !m_pPlayer->m_bShieldWall )
	{
		EMIT_SOUND( ENT( pev ), CHAN_ITEM,
			"forcefield/forcefield_fail.wav", 1.0f, 0.8f );
		SendWeaponAnim( 1 );	// ref vcall+0x160(this, 1, 1, 0)
	}
	else
	{
		EMIT_SOUND( ENT( pev ), CHAN_ITEM,
			"forcefield/forcefield_unloading.wav", 1.0f, 0.8f );
		m_pPlayer->m_bShieldWall = 0;	// ref player+0x3e0 = 0
		SendWeaponAnim( 1 );	// ref vcall+0x160(this, 1, 1, 0)
	}

	m_flTimeWeaponIdle = UTIL_WeaponTimeBase();	// ref this+0x1dc = WTB
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 1.0f;	// ref player +0x39c
}

// ref @0x10c036
void CInvis::InvisThink( void )
{
	if( !m_pPlayer )
	{
		SetThink( NULL );
		return;
	}

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] < 1 )
	{
		// Cloak off. The shield thinker may already have cleared it when the
		// charge hit zero; toggling then would switch it back on.
		if( m_pPlayer->m_bShieldSprint )
			m_pPlayer->SGTC_ToggleVis();
		EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "empty.wav", 0.8f, 0.8f );
		Holster();
		m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 1.0f;
		m_flSgtcChargeLatch = gpGlobals->time;
		SetThink( NULL );
	}
	pev->nextthink = gpGlobals->time + 0.1f;
}

// ref @0x10c154
void CInvis::WeaponIdle( void )
{
	if( !m_pPlayer )
		return;

	m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = m_pPlayer->m_iShieldCharge;	// ref +0x158

	if( m_flShieldLoadLatch != 0.0f && m_flShieldLoadLatch < gpGlobals->time - 0.5f )
	{
		m_pPlayer->m_bShieldWall = 1;	// ref player+0x3e0 = 1
		m_flShieldLoadLatch = 0.0f;		// ref this+0x204 = 0; NO event emitted
	}

	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;

	float flRoll = UTIL_SharedRandomFloat( m_pPlayer->random_seed, 0.0f, 1.0f );
	int iAnim = ( flRoll <= 0.75f ) ? SGTC_IDLE : 3;
	float flIdle = ( iAnim == SGTC_IDLE )
		? UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f )
		: 2.5f;

	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + flIdle;
	SendWeaponAnim( iAnim );
	m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = m_pPlayer->m_iShieldCharge;	// ref +0x158 mirror
	pev->nextthink = gpGlobals->time + 0.25f;
}

SGTC_NO_RELOAD( CInvis )
