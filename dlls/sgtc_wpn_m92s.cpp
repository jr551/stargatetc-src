// sgtc_wpn_m92s.cpp — StargateTC CM92s (split from sgtc_weapons.cpp; NEW file).

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
// CM92s - "M92S" heavy pistol.
// Event: events/eagle.sc.
//
// ref/stc_i386.so addresses (nm == Ghidra - 0x10000):
//   Spawn      @0xd86cc   Precache  @0xd8772   Deploy @0xd8986
//   Fire       @0xd89ee   PA        @0xd8eac   Reload @0xd8f04
//   Holster    @0xd9070   WeaponIdle @0xd90c0
//
// Layout beyond CBasePlayerWeapon (ref offsets):
//   +0x200 refire gate: armed WTB+0.05 at the TOP of Fire when expired (that
//   call then returns = first press after idle is a dry click); a live gate
//   lets the shot through. Fire/Reload tails re-arm it to WTB+0.1 / +2.0.
//   +0x204 second idle-anim deadline (WTB+rand(10,15) on Fire/Reload;
//   WeaponIdle re-arms +3.0 or rand(10,15)).
//   +0x208 stamp = WTB written on Deploy / Fire tail / Reload tail.
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_m92s, CM92s );

// Save/restore (#99). Weapon-time gates only. Not saved: m_usFire (event handle, Precache()
// re-runs after restore) and m_flSgtcStamp (written, never read).
TYPEDESCRIPTION CM92s::m_SaveData[] =
{
	DEFINE_FIELD( CM92s, m_flSgtcRefireGate, SGTC_FIELD_WTIME ),
	DEFINE_FIELD( CM92s, m_flSgtcIdleDue, SGTC_FIELD_WTIME ),
};

IMPLEMENT_SAVERESTORE( CM92s, CBasePlayerWeapon )

void CM92s::Spawn( void )	// ref @0xd86cc
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_m92s.mdl" );
	m_iId = SGTC_WPN_M92S;
	m_iDefaultAmmo = 60;	// ref writes +0x1f8 = 0x3c
	FallInit();
}

void CM92s::Precache( void )	// ref @0xd8772 (model/sound lists match)
{
	PRECACHE_MODEL( "models/v_m92s.mdl" );
	PRECACHE_MODEL( "models/w_m92s.mdl" );
	PRECACHE_MODEL( "models/p_m92s.mdl" );
	PRECACHE_MODEL( "models/pshell.mdl" );

	PRECACHE_SOUND( "items/9mmclip1.wav" );
	PRECACHE_SOUND( "items/9mmclip2.wav" );
	PRECACHE_SOUND( "weapons/pl_gun1.wav" );
	PRECACHE_SOUND( "weapons/pl_gun2.wav" );
	PRECACHE_SOUND( "weapons/pl_gun3.wav" );
	PRECACHE_SOUND( "weapons/pistol-01.wav" );
	PRECACHE_SOUND( "weapons/pistol-02.wav" );
	PRECACHE_SOUND( "weapons/pistol-03.wav" );

	m_usFire = PRECACHE_EVENT( 1, "events/eagle.sc" );
}

SGTC_GETITEM( CM92s, SGTC_AMMO_9MMC, 60, 12, 1, 3, SGTC_WPN_M92S, 0, 10 )
SGTC_ADD_TO_PLAYER( CM92s )

BOOL CM92s::Deploy( void )	// ref @0xd8986
{
	// ref: WeapDispSet(2) + DefaultDeploy (SGTC_DEPLOY macro covers both) and
	// +0x208 := WTB afterwards.
	BOOL bRet = SGTC_DEPLOY( 2, "models/v_m92s.mdl", "models/p_m92s.mdl", SGTC_DRAW, "onehanded" );
	m_flSgtcStamp = UTIL_WeaponTimeBase();	// ref +0x208
	return bRet;
}

void CM92s::PrimaryAttack( void )	// ref @0xd8eac
{
	// ref: gate-travel deadline gates the whole body; NO waterlevel check.
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )
		return;

	// ref pushes (0.001, 0.2, 0) — the floats are vestigial in the ref Fire.
	M92sFire( 0.001f, 0.2f, 0 );
}

SGTC_NO_SECONDARY( CM92s )	// ref has no CM92s::SecondaryAttack

void CM92s::M92sFire( float flSpread, float flCycleTime, int iFlags )	// ref @0xd89ee
{
	(void)flSpread; (void)flCycleTime; (void)iFlags;	// vestigial in ref

	// ref: expired refire gate re-arms WTB+0.05 and returns (dry click).
	if( m_flSgtcRefireGate <= UTIL_WeaponTimeBase() )
	{
		m_flSgtcRefireGate = UTIL_WeaponTimeBase() + 0.05f;
		return;
	}

	if( m_iClip <= 0 )
	{
		if( m_fFireOnEmpty )	// ref this+0x1c8 (stock field)
		{
			PlayEmptySound();
			m_flNextPrimaryAttack = UTIL_WeaponTimeBase();
		}
		return;
	}
	m_iClip--;

	// ref: EF_MUZZLEFLASH on the PLAYER's pev (pev+0x118), not the weapon's.
	m_pPlayer->pev->effects = (int)( m_pPlayer->pev->effects ) | EF_MUZZLEFLASH;

	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );	// ref arg 6 == stock PLAYER_ATTACK1

#ifndef CLIENT_DLL
	m_pPlayer->SnagInvis();	// ref
#endif

	// ref: quiet branch reads the WEAPON's pev->body (+0x114) == 1 →
	// 0xc8/0x80 vs stand 0x258/0x100. No writer on either side (parity-dead).
	if( pev->body == 1 )	// ref pev+0x114 == 1
	{
		m_pPlayer->m_iWeaponVolume = 0xc8;
		m_pPlayer->m_iWeaponFlash = 0x80;
	}
	else
	{
		m_pPlayer->m_iWeaponVolume = 0x258;
		m_pPlayer->m_iWeaponFlash = 0x100;
	}

	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecAiming = m_pPlayer->GetAutoaimVector( AUTOAIM_5DEGREES );

	// ref: spread keyed off punchangle sum — 0.07 flat when punch is zero,
	// 0.035 otherwise (i.e. ref kicks the aim AWAY from the punched view).
	Vector vecSpread;
	if( m_pPlayer->pev->punchangle.x + m_pPlayer->pev->punchangle.y
			+ m_pPlayer->pev->punchangle.z == 0.0f )
		vecSpread = Vector( 0.07f, 0.07f, 0.07f );
	else
		vecSpread = Vector( 0.035f, 0.035f, 0.035f );

	Vector vecDir = m_pPlayer->FireBulletsPlayer( 1, vecSrc, vecAiming,
		vecSpread, 8192, SGTC_BULLET_M92S, 0, 0,
		m_pPlayer->pev, m_pPlayer->random_seed );	// ref pushes type 0xb, iDamage 0 → case-11 TraceAttack(40,DMG_BULLET), no sound/decal

	// ref: PLAYBACK_EVENT_FULL(flags=1, edict, m_usFire, 0.0, g_vecZero,
	//   g_vecZero, vecDir.x, vecDir.y, 0, 0, m_iClip==0, 0)
	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFire,
		0.0, (float *)&g_vecZero, (float *)&g_vecZero, vecDir.x, vecDir.y,
		0, 0, ( m_iClip == 0 ) ? 1 : 0, 0 );

	// ref tail: nextPA = WTB+0.1; idleDue = WTB+rand(10,15);
	// idle = WTB+0.1; gate = WTB+0.1; stamp = WTB.
	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.1f;
	m_flSgtcIdleDue = UTIL_WeaponTimeBase() +
		UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.1f;
	m_flSgtcRefireGate = UTIL_WeaponTimeBase() + 0.1f;
	m_flSgtcStamp = UTIL_WeaponTimeBase();
}

void CM92s::Reload( void )	// ref @0xd8f04
{
	// ref guards: rgAmmo>0, m_iClip != 12, m_flTimeWeaponIdle <= WTB
	// (idle-due, NOT nextPA like stock DefaultReload callers use).
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 || m_iClip == 12 )
		return;
	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;

	if( DefaultReload( 12, 4, 0.8f ) )	// ref DefaultReload(0xc, 4, 0.8, 0)
	{
		m_flSgtcIdleDue = UTIL_WeaponTimeBase() +
			UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );
		m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 2.0f;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.0f;
		m_flSgtcRefireGate = UTIL_WeaponTimeBase() + 2.0f;
		m_flSgtcStamp = UTIL_WeaponTimeBase();
		m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 2.0f;
		m_pPlayer->SetAnimation( (PLAYER_ANIM)8 );	// ref literal 8 (stock enum caps at 5)
	}
}

void CM92s::Holster( int skiplocal )	// ref @0xd9070
{
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;
	SendWeaponAnim( 3, 1, 0 );	// ref vcall+0x160 anim 3; NO base Holster
}

void CM92s::WeaponIdle( void )	// ref @0xd90c0
{
	// ref: ResetEmptySound (vcall+0x158) + GetAutoaimVector(0.1736) side effect.
	ResetEmptySound();
	m_pPlayer->GetAutoaimVector( 0.1736f );

	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;

	// ref: nextPA = WTB and the refire gate copies it (arms the 0.05s
	// dry-click gate for the next press).
	m_flNextPrimaryAttack = UTIL_WeaponTimeBase();
	m_flSgtcRefireGate = m_flNextPrimaryAttack;

	// ref dead branch kept for parity: bFirst can never be true because it
	// requires +0x1d4 > WTB immediately after writing +0x1d4 = WTB.
	BOOL bFirst = ( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] != 0
		&& m_flNextPrimaryAttack > UTIL_WeaponTimeBase() );
	int iAnim;
	if( bFirst
		&& UTIL_SharedRandomFloat( m_pPlayer->random_seed, 0.0f, 1.0f ) > 0.75f
		&& m_flSgtcIdleDue <= UTIL_WeaponTimeBase() )
	{
		iAnim = 0;
		m_flSgtcIdleDue = UTIL_WeaponTimeBase() +
			UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );
	}
	else
	{
		iAnim = 7;
		m_flSgtcIdleDue = UTIL_WeaponTimeBase() + 3.0f;
	}
	SendWeaponAnim( iAnim, 1, 0 );	// ref vcall+0x160
}

// Reference (ref/stc_i386.so) maps weapon_mp5 and weapon_9mmAR to stock
// CMP5 (mp5.cpp) and weapon_handgrenade to stock CHandGrenade; stock
// registrations stand, so CSGTCMP5 is removed (was duplicate factory).
