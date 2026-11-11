// sgtc_wpn_usas.cpp — StargateTC CUsas (split from sgtc_weapons.cpp; NEW file).

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
// CUsas - "USAS" automatic shotgun. Analogue: shotgun.cpp.
// Event: events/usas.sc.
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_usas, CUsas );

// Save/restore (#99). Not saved: m_iSgtcShellMdl / m_usFire / m_usFire2 (precache handles,
// rebuilt by Precache()) and the layout padding.
TYPEDESCRIPTION CUsas::m_SaveData[] =
{
	DEFINE_FIELD( CUsas, m_flSgtcFireLatch, SGTC_FIELD_WTIME ),
};

IMPLEMENT_SAVERESTORE( CUsas, CBasePlayerWeapon )

void CUsas::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_usas.mdl" );
	m_iId = SGTC_WPN_USAS;
	m_iDefaultAmmo = 120;
	FallInit();
}

void CUsas::Precache( void )
{
	PRECACHE_MODEL( "models/v_usas.mdl" );
	PRECACHE_MODEL( "models/w_usas.mdl" );
	PRECACHE_MODEL( "models/p_usas.mdl" );
	m_iSgtcShellMdl = PRECACHE_MODEL( "models/shell.mdl" );	// ref this+0x204 (PRECACHE_MODEL result stored)

	PRECACHE_SOUND( "items/9mmclip1.wav" );
	PRECACHE_SOUND( "weapons/dbarrel1.wav" );
	PRECACHE_SOUND( "weapons/sbarrel1.wav" );
	PRECACHE_SOUND( "weapons/reload1.wav" );
	PRECACHE_SOUND( "weapons/reload3.wav" );
	PRECACHE_SOUND( "weapons/spaspomp.wav" );
	PRECACHE_SOUND( "weapons/spastir.wav" );
	PRECACHE_SOUND( "weapons/spump.wav" );

	m_usFire = PRECACHE_EVENT( 1, "events/usas.sc" );	// ref this+0x20c
	// ref keeps a second event-sized handle at this+0x20e (the static assert below
	// pins the layout), but the reference PRECACHE_EVENT list (33 scripts) has no
	// usas2 script and nothing ever passes this handle to PLAYBACK_EVENT_FULL.
	// Registering it would add a script the reference server does not register,
	// so keep the field (layout parity) and leave it unset.
	m_usFire2 = 0;
}

SGTC_GETITEM( CUsas, SGTC_AMMO_BUCKSHOT, 125, 10, 2, 5, SGTC_WPN_USAS, 0, 20 )	// ref @0x1a55ac: slot 2, pos 5, weight 20 (iSlot/iPosition are the slot-grid cell 0..5)
SGTC_ADD_TO_PLAYER( CUsas )

BOOL CUsas::Deploy( void )
{
	m_flSgtcFireLatch = 0.0f;	// ref @0x1a5658 clears this+0x210 on deploy
	return SGTC_DEPLOY( 2, "models/v_usas.mdl", "models/p_usas.mdl", SGTC_DRAW, "mp5" );
}
#if defined( __GNUC__ ) && defined( __i386__ ) && !defined( CLIENT_DLL )
SGTC_STATIC_ASSERT( offsetof( CUsas, m_usFire ) == 0x20c, "CUsas m_usFire must sit at ref +0x20c" );
SGTC_STATIC_ASSERT( offsetof( CUsas, m_usFire2 ) == 0x20e, "CUsas m_usFire2 must sit at ref +0x20e" );
SGTC_STATIC_ASSERT( offsetof( CUsas, m_flSgtcFireLatch ) == 0x210, "CUsas +0x210 latch must sit at ref +0x210" );
SGTC_STATIC_ASSERT( offsetof( CUsas, m_iSgtcShellMdl ) == 0x204, "CUsas +0x204 shell idx must sit at ref +0x204" );
#endif

void CUsas::PrimaryAttack( void )	// ref @0x1a56c0 — custom usas body (not stock shotgun)
{
	// ref: player +0x408 gate-travel must be past-due before any fire.
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )
		return;

	// ref +0x210 frame latch: when it equals WTB, re-arm +0.05 and bail.
	if( m_flSgtcFireLatch == UTIL_WeaponTimeBase() )
	{
		m_flSgtcFireLatch = UTIL_WeaponTimeBase() + 0.05f;
		return;
	}

	// ref pev+0x1c0 waterlevel == 3 → dry sound + +0x1d4 = WTB+0.15.
	if( m_pPlayer->pev->waterlevel == 3 )
	{
		PlayEmptySound();
		m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.15f;
		return;
	}

	// ref this+0x1e8 clip <= 0 → Reload(); still empty → PlayEmptySound.
	// Ref returns either way (no shot on the first press that triggers reload).
	if( m_iClip <= 0 )
	{
		Reload();
		if( m_iClip <= 0 )
			PlayEmptySound();
		return;
	}

	m_pPlayer->m_iWeaponVolume = LOUD_GUN_VOLUME;
	m_pPlayer->m_iWeaponFlash = NORMAL_GUN_FLASH;

	// ref: when NOT shield-large — SnagInvis + stamina −12 + UpdateStamina;
	// remaining stamina < 0x13 clamps to 0.
	if( m_pPlayer->m_bShieldLarge != 1 )
	{
		m_pPlayer->SnagInvis();
		m_pPlayer->m_iStamina -= 12;
		m_pPlayer->SGTC_UpdateStamina();
		if( m_pPlayer->m_iStamina <= 19 )
			m_pPlayer->m_iStamina = 0;
	}

	m_pPlayer->pev->effects |= EF_MUZZLEFLASH;
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );	// ref pushes anim 6 (sgtc range)

	UTIL_MakeVectors( m_pPlayer->pev->v_angle );	// ref pev+0x74 — v_angle only, NO punchangle
	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecAiming = m_pPlayer->GetAutoaimVector( AUTOAIM_5DEGREES );	// ref 0x3dcccccd

	SendWeaponAnim( 5, 1, 0 );	// ref vtable+0x160, anim 5

	// ref: 8 pellets, cone 0.08716 (5°), distance 1024.0, SGTC_BULLET_USAS(4),
	// no tracer, dmg group 0; vecDir out = bullet dir → event fparams.
	Vector vecDir = m_pPlayer->FireBulletsPlayer( 8, vecSrc, vecAiming,
		Vector( 0.08716f, 0.08716f, 0.08716f ), 1024.0f,
		SGTC_BULLET_USAS, 0, 0, m_pPlayer->pev, m_pPlayer->random_seed );

	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFire,
		0.0f, (float *)&g_vecZero, (float *)&g_vecZero,
		vecDir.x, vecDir.y, 0, 0, 0, 0 );

	// ref: clip decrement gated on !shield-large (cheat keeps full clip).
	if( m_pPlayer->m_bShieldLarge != 1 )
		m_iClip--;

	// ref tail: +0x1dc/+0x1d4/+0x210 ALL = WTB+0.1.
	// (Ref has two dead +0x1dc writes above — clip>0 → +6.0, clip==0 → +0.9 —
	//  immediately overwritten by the +0.1; omitted here.)
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.1f;
	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.1f;
	m_flSgtcFireLatch = UTIL_WeaponTimeBase() + 0.1f;
}

SGTC_NO_SECONDARY( CUsas )
// ref CUsas::Reload @0x1a5b50: clip 10, 1.0 s reload anim.
void CUsas::Reload( void )
{
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 || m_iClip == 10 )
		return;
	DefaultReload( 10, 1, 1.0f );
}
SGTC_WEAPON_IDLE( CUsas )
