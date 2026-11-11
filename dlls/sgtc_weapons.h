// sgtc_weapons.h - StargateTC server weapon arsenal (NEW file).
//
// Rebuilds the SGTC 2.0 server-side arsenal on the hlsdk-xash3d base.
// Truth sources:
// - weapon_* -> class thunks + C<Weapon>::GetItemInfo disassembly of
//   ref/stc_i386.so (2.0 Linux server, unstripped) for exact iId/pos/
//   maxClip/maxAmmo/flags/weight/ammo strings.
// - view/world models, sounds, events: Spawn/Precache string refs in the
//   same .so + stargatetc/ assets.
// - FireBulletsPlayer bullet-type immediates + rodata cooldown consts for
//   rate/damage notes (see per-weapon comments in the .cpp).
// - Client contract: cl_dll/sgtc_weapons_client.h SGTC_WPN_* ids 0-31.
//   Server m_iId MUST equal those ids (stock HL ids 1-15 collide by design).
//
// Stock files are NEVER edited. Where the reference build reuses stock
// classes (weapon_mp5/weapon_9mmAR -> CMP5, weapon_spas -> CShotgun,
// weapon_handgrenade -> CHandGrenade, weapon_c4 -> CSatchel, weapon_mine ->
// tripmine, monster_mine -> CTripmineGrenade), stock registrations stand.
// Other links use SGTC-specific subclasses with identical link names/ids.
// Projectile/placed entities owned elsewhere (sgtc_entities peer:
// CBigLanceProj, CLanceProj, CReetouProj, CTac, effect_shield) are NOT
// redeclared; staff weapons fire local CGrenade-derived bolts or
// Create() those map names so there is no ODR clash.
#pragma once
#ifndef SGTC_WEAPONS_H
#define SGTC_WEAPONS_H

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "sgtc_gas_grenade.h"
#include "player.h"
#include "gamerules.h"

// Compile-time layout guard. The Linux CI compiles inside the steam-runtime
// scout chroot, whose gcc defaults to gnu++98, so static_assert (C++11) is not
// available there; fall back to the classic negative-array typedef, which is
// standard C++98 and fails just as loudly.
#if __cplusplus >= 201103L
#define SGTC_STATIC_ASSERT( cond, msg ) static_assert( cond, msg )
#else
#define SGTC_SA_CAT_( a, b ) a##b
#define SGTC_SA_CAT( a, b ) SGTC_SA_CAT_( a, b )
#define SGTC_STATIC_ASSERT( cond, msg ) \
	typedef char SGTC_SA_CAT( sgtc_static_assert_, __LINE__ )[ ( cond ) ? 1 : -1 ]
#endif

// SGTC custom skilldata fields (ref gSkillData+0x108..0x138) are written ONLY
// by CHalfLifeMultiplay::RefreshSkillData @0x116b54 from rodata constants
// (no sk_ cvar, no SP init — the port's stock skilldata_t has no slot for
// them). The melee/implanted values used by the weapon Swing paths:
//   +0x120 = 42.0f  -> CLanceJaffa::Swing base addend
//   +0x128 = 54.0f  -> CBatonSupplice/CCoujaf/CCouteau::Swing base damage
//   +0x12c = 54.0f  -> CZombie::Swing (monster_implanted) raw addend
// C++98-compatible: the Linux CI builds inside the steam-runtime scout
// chroot (gcc default gnu++98), where constexpr is unavailable (P167 broke it).
static const float kSgtcSkillLanceDmg = 42.0f;
static const float kSgtcSkillMeleeDmg = 54.0f;
static const float kSgtcSkillImplantDmg = 54.0f;

// Extra ref bullet-type ids from the FireBulletsPlayer jump table @0xcf8c4
// (case index = iBulletType): used by combat.cpp's damage switch.
#define SGTC_BULLET_TYPE9 9	// ref jump-table case 9 (unused by known weapons)
#define SGTC_BULLET_TYPE10 10	// case 10 → 60.0
#define SGTC_BULLET_TYPE11 11	// case 11 → 40.0
#define SGTC_BULLET_TYPE13 13	// == SGTC_BULLET_ZAT (alias for combat.cpp switch)
#define SGTC_BULLET_TYPE17 17	// case 17 → 2.0
#define SGTC_BULLET_TYPE20 20	// case 20 → 0.0

// Server-authoritative SGTC weapon ids. MUST match
// cl_dll/sgtc_weapons_client.h SGTCWeaponId exactly (0-30). The id is also the
// bit index in pev->weapons, so it must stay below WEAPON_SUIT (31, the HEV
// suit flag). weapon_9mmAR shares CMP5/SGTC_WPN_MP5; it has no id of its own.
//
// #69: SGTC_WPN_FLASHBANG is id 0, as in the ref. It cannot be renumbered:
// every id 0-30 is taken (31 is WEAPON_SUIT, MAX_WEAPONS is 32). Id 0 is a
// real weapon here, so code must decide "record in use" by name, not by a
// non-zero id: player.cpp WeaponList tests II.pszName and the client HUD uses
// WeaponsResource::IsRegistered (szName). Ids must also be unique; see
// UTIL_PrecacheOtherWeapon and tests/features/test_weapon_ids.py.
enum SGTCWeaponId
{
	SGTC_WPN_FLASHBANG = 0,
	SGTC_WPN_COUJAF = 1,
	SGTC_WPN_M92S = 2,
	SGTC_WPN_COUTEAU = 3,
	SGTC_WPN_MP5 = 4,
	SGTC_WPN_PSG1 = 5,
	SGTC_WPN_P90 = 6,
	SGTC_WPN_SHOTGUN_STOCK = 7, // link weapon_spas
	SGTC_WPN_TAC = 8,
	SGTC_WPN_INVIS = 9,
	SGTC_WPN_GACGOA = 10,
	SGTC_WPN_M16 = 11,
	SGTC_WPN_HANDGRENADE = 12,
	SGTC_WPN_TRIPMINE = 13, // link weapon_mine
	SGTC_WPN_SATCHEL = 14, // link weapon_c4
	SGTC_WPN_USAS = 15,
	SGTC_WPN_MAIN = 16,
	SGTC_WPN_ZAT = 17,
	SGTC_WPN_BIGLANCE = 18, // link weapon_glstaff
	SGTC_WPN_LANCE = 19, // link weapon_lance
	SGTC_WPN_BATSUP = 20,
	SGTC_WPN_REETOU = 21,
	SGTC_WPN_LARVE = 22,
	SGTC_WPN_ZATARC = 23,
	SGTC_WPN_MEDIC = 24,
	SGTC_WPN_SHIELD = 25, // link weapon_shield (fills client-table gap)
	SGTC_WPN_SMOKEGRENADE = 26,
	SGTC_WPN_NERVEGRENADE = 27,
	SGTC_WPN_NARCOGRENADE = 28,
	SGTC_WPN_MUSTARDGRENADE = 29,
	SGTC_WPN_GOAREGEN = 30,
};

// An SGTC id equal to WEAPON_SUIT would toggle the suit bit when the weapon is
// picked up or dropped (#69). Keep SGTC_WPN_GOAREGEN the highest id.
typedef char SGTC_WPN_ids_must_stay_below_WEAPON_SUIT[ ( SGTC_WPN_GOAREGEN < WEAPON_SUIT ) ? 1 : -1 ];

// SGTC custom bullet types observed at FireBulletsPlayer call sites.
// Stock types 1,2,4 keep stock meaning (1=9mm,2=MP5,4=buckshot);
// the rest are SGTC-extended switch cases in the 2.0 .so.
#define SGTC_BULLET_M92S 11 // ref CM92s::Fire pushes 0xb → case 11 (40 dmg)
#define SGTC_BULLET_MP5 2
#define SGTC_BULLET_SPAS 4
#define SGTC_BULLET_USAS 4
#define SGTC_BULLET_ZAT 13
#define SGTC_BULLET_M16 16
#define SGTC_BULLET_P90 15
#define SGTC_BULLET_ZATARC 18
#define SGTC_BULLET_PSG1 19

// Ammo registry names (exact .rodata strings from GetItemInfo).
#define SGTC_AMMO_9MMC "9mmC"
#define SGTC_AMMO_MP5 "Mp5"
#define SGTC_AMMO_M16 "m16 ammo"
#define SGTC_AMMO_P90 "p90 ammo"
#define SGTC_AMMO_PSG1 "psg1 clip"
#define SGTC_AMMO_BUCKSHOT "buckshot"
#define SGTC_AMMO_ENERGIE "Energie"
#define SGTC_AMMO_ENERGIE2 "Energie2"
#define SGTC_AMMO_ENERGIE3 "Energie3"
#define SGTC_AMMO_ENERGIE5 "Energie5"
#define SGTC_AMMO_ENERGIEGL "EnergieGL"
#define SGTC_AMMO_TER "TER"
#define SGTC_AMMO_LARVE "Larve"
#define SGTC_AMMO_MEDGOO "medgoo"
#define SGTC_AMMO_GOAREGEN "goaregen"
#define SGTC_AMMO_INVIS "Invis"
#define SGTC_AMMO_TAC "Tacgun"
// Tacgun pool maximum. Single source of truth: CTacgun::GetItemInfo publishes
// it as iMaxAmmo1 and the TAC pickup reads it back at runtime, so the anti-dupe
// cap cannot drift from the weapon's ammo max.
#define SGTC_TAC_MAX_CARRY 3
#define SGTC_AMMO_SATCHEL "Satchel Charge"
#define SGTC_AMMO_TRIPMINE "Trip Mine"
#define SGTC_AMMO_HAND "Hand Grenade"
#define SGTC_AMMO_FLASH "Flash Grenade"
#define SGTC_AMMO_SMOKE "Smoke Grenade"
#define SGTC_AMMO_NERVE "Nerve Grenade"
#define SGTC_AMMO_NARCO "Narco Grenade"
#define SGTC_AMMO_MUSTARD "Mustard Grenade"
#define SGTC_AMMO_GACG "GrenadeChoc"

// Save/restore (#99): timestamps taken from UTIL_WeaponTimeBase() are relative
// (base 0) when CLIENT_WEAPONS is on, so they must be saved as plain floats and
// not re-based by FIELD_TIME. Same split as CBasePlayerWeapon::m_SaveData.
// Timestamps taken from gpGlobals->time always use FIELD_TIME.
#if CLIENT_WEAPONS
#define SGTC_FIELD_WTIME FIELD_FLOAT
#else
#define SGTC_FIELD_WTIME FIELD_TIME
#endif

// ref CBasePlayer::WeapDispSet @0x133aca — called at start of each SGTC Deploy.
#define SGTC_DEPLOY( disp, vmdl, pmdl, anim, ext ) \
	( m_pPlayer->SGTC_WeapDispSet( disp ), DefaultDeploy( vmdl, pmdl, anim, ext ) )

// Common SGTC deploy/idle helper: every class uses DefaultDeploy with its
// v_/p_ models and a stock-style animation extension.
#define SGTC_DECLARE_WEAPON() \
	int GetItemInfo( ItemInfo *p ); \
	int AddToPlayer( CBasePlayer *pPlayer ); \
	void Spawn( void ); \
	void Precache( void ); \
	BOOL Deploy( void ); \
	void PrimaryAttack( void ); \
	void SecondaryAttack( void ); \
	void Reload( void ); \
	void WeaponIdle( void ); \
	virtual BOOL UseDecrement( void ) { return FALSE; }

// ---- Hitscan rifles / pistols / energy guns ----

class CM92s : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	int iItemSlot( void ) { return 3; }
	void Holster( int skiplocal = 0 );	// ref @0xd9070: m_flNextAttack+0.5 + anim(3), no base call
	// Reload @0xd8f04 / WeaponIdle @0xd90c0 / Fire @0xd89ee are ref ports;
	// PA/SA arg floats are vestigial (unused in ref body).
	void M92sFire( float flSpread, float flCycleTime, int iFlags );
private:
	unsigned short m_usFire;
	// ref layout beyond CBasePlayerWeapon: +0x200 refire gate (Deploy does NOT
	// arm it; WeaponIdle copies nextPA into it), +0x204 second-idle due,
	// +0x208 last stamp (WTB on Deploy/Fire/Reload tails).
	float m_flSgtcRefireGate;	// ref this+0x200
	float m_flSgtcIdleDue;		// ref this+0x204
	float m_flSgtcStamp;		// ref this+0x208
};

// CSGTCMP5 removed: reference maps weapon_mp5/weapon_9mmAR to stock CMP5.

class CM16 : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	int iItemSlot( void ) { return 1; }
	void Holster( int skiplocal = 0 );	// ref @0x1073ae
private:
	unsigned short m_usFire;
public:
	int m_iSgtcZoomActive;		// ref this+0x208
};

class CP90 : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	int iItemSlot( void ) { return 1; }
	void Holster( int skiplocal = 0 );	// ref @0x129fb6
	float m_flP90Latch204;			// ref +0x204
	float m_flP90Latch208;			// ref +0x208
	char m_bP90SilencerOn;			// ref +0x20c
	int m_iP90SilencerLatch;		// ref +0x210
	unsigned short m_usFire;		// ref +0x214
	unsigned short m_usFireL;		// ref +0x216 events/p90l.sc
};

class CPsg1 : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	int iItemSlot( void ) { return 4; }
	void Holster( int skiplocal = 0 );	// ref @0x14a62c
	void EXPORT SgtcBoltThink( void );	// post-shot bolt-cycle cue (sgtc_wpn_psg1.cpp)
private:
	unsigned short m_usFire;
public:
	int m_iSgtcZoomStage;		// ref this+0x200
};

// CSGTCSpas removed: reference maps weapon_spas to stock CShotgun.

class CUsas : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	int iItemSlot( void ) { return 5; }
public:
	char m_sgtcUsasPadTo204[0x128];	// stock fields end @0xdc → pad to ref +0x204
	int m_iSgtcShellMdl;			// ref this+0x204 — models/shell.mdl precache idx
	char m_sgtcUsasPadTo20c[4];		// +0x208 → +0x20c
	unsigned short m_usFire;		// ref this+0x20c — events/usas.sc
	unsigned short m_usFire2;		// ref this+0x20e — events/usas2.sc
	float m_flSgtcFireLatch;		// ref this+0x210 — WTB+0.05 per-frame fire latch

};

class CZat : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	int iItemSlot( void ) { return 2; }
	BOOL IsUseable( void ) { return TRUE; }	// ref @0x1c5820: unconditional 1
private:
	unsigned short m_usFireA;
	unsigned short m_usFireB;
	int m_iZatWaveSprite;	// ref +0x204 — sprites/zatwave.spr
	int m_iZatCharge;		// ref +0x1fc — zata/zatb event selector
	float m_flZatRegenNext;	// ref +0x200 — +2 ammo / 0.4s regen cadence
};

class CZatarc : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	int iItemSlot( void ) { return 1; }
	void Holster( int skiplocal = 0 );	// ref @0x1c79aa
private:
	unsigned short m_usFire;
	int m_iSgtcZoomStage;	// ref CZatarc+0x204 zoom toggle
	float m_flZatarcRegenNext;	// ref +0x1fc — Reload regen cadence
	int m_bSgtcZarSplash;	// 1 = sprites/zarimpact.spr exists, else zatimpact.spr
	float m_flSgtcRechargeFx;	// next recharge dlight pulse (0.2s blue cell glow)
};

// ---- Staff / projectile weapons (local bolts; map entities owned by entities peer) ----

class CLanceJaffa : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	int iItemSlot( void ) { return 1; }	// ref @0x110892 returns 1 (was 2)
	void Holster( int skiplocal = 0 );	// ref @0x10f53c
	void EXPORT SwingAgain( void );	// ref @0x10ffaa: Swing(0)
	int Swing( int fFirst );		// ref @0x10f5d0: melee + TraceResult store
	void Smack( void );			// ref @0x10f5ca: empty no-op
private:
	unsigned short m_usFire;
	unsigned short m_usFire2;
	int m_iSwing;
	TraceResult m_trLastHit;		// ref this+0x208..0x23b last-hit TraceResult
	int m_iLaserBeam;			// ref this+0x204 — sprites/laserbeam.spr
	float m_flSgtcRegenNext;		// ref this+0x1fc — ammo regen latch
};

class CBigLanceJaffa : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	int iItemSlot( void ) { return 1; }	// ref @0xca9e8 returns 1 (was 5)
	BOOL IsUseable( void );
	void Holster( int skiplocal = 0 );	// ref @0xc9c90
private:
	unsigned short m_usFire;
	int m_iLaserBeam;			// ref this+0x204
	float m_flSgtcRegenNext;		// ref this+0x1fc
};

class CReetou : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	int iItemSlot( void ) { return 1; }	// ref @0x163298 returns 1 (was 4)
	BOOL IsUseable( void ) { return TRUE; }	// ref @0x162846: unconditional 1
	BOOL SGTC_IsCloakScanActive( void ) const { return m_bSgtcCloakOn; }	// build-13 bot rules
	void Holster( int skiplocal = 0 );	// ref @0x1625e6
private:
	unsigned short m_usFire;
	unsigned short m_usFire2;
	float m_flSgtcRegenNext;	// ref CReetou+0x200 ammo-regen latch
	BOOL m_bSgtcCloakOn;		// ref CReetou+0x204 cloak toggle
	BOOL m_bSgtcPinkSaved;		// build-21 pink scanner glow save
	int m_iSgtcSavedRenderFx;
	float m_flSgtcSavedRenderAmt;
	Vector m_vecSgtcSavedRenderColor;
	float m_flSgtcCloakTimer;	// ref CReetou+0x1fc cloak timer
	void ThinkSearchInvisible( void );	// ref SearchInvisible @0x162846 (m_pfnThink)
	void ThinkGuardStub( void );	// ref guard stub @0x175
	void DisarmCloakScan( void );	// ref Holster vcall weapon+0x16c when +0x204 set
};


// ---- Melee ----

class CBatonSupplice : public CBasePlayerWeapon
{
public:
	int GetItemInfo( ItemInfo *p );
	int AddToPlayer( CBasePlayer *pPlayer );
	void Spawn( void );
	void Precache( void );
	BOOL Deploy( void );
	void PrimaryAttack( void );
	void SecondaryAttack( void );
	void Reload( void );
	void WeaponIdle( void );
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	// ref CBatonSupplice::UseDecrement @0xb8662 returns TRUE (mov eax,1; ret),
	// unlike the SGTC_DECLARE_WEAPON default FALSE. ROW-217 rework.
	virtual BOOL UseDecrement( void ) { return TRUE; }
	void EXPORT SwingAgain( void );
	int Swing( int fFirst );
	void Smack( void ); // ref @0xb76d2: DecalGunshot(&m_trHit, 12)
	int iItemSlot( void ) { return 1; }
	void Holster( int skiplocal = 0 );
private:
	// ref layout: +0x200 = last-swing TraceResult (Smack decal + DoSpark point),
	// +0x238/+0x23a = event ids, +0x23c = swing-phase latch byte
	TraceResult m_trHit;
	unsigned short m_usFire;
	unsigned short m_usFire2;
	BOOL m_bSgtcSwingLatch; // ref this+0x23c: set on every swing, cleared Spawn/Holster/WeaponIdle
};

class CCoujaf : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	void Holster( int skiplocal = 0 );	// ref @0xd46e6 — ResetEmptySound + SendWeaponAnim(0,0,*), NO stock viewmodel clear
	void EXPORT Smack( void );		// ref @0xd495c — 5-byte empty body
	void EXPORT SwingAgain( void );
	int Swing( int fFirst );
	int iItemSlot( void ) { return 1; }
	int m_iSwing;
	TraceResult m_trHit;
private:
	unsigned short m_usFire;
public:
	char m_sgtcPadTo1fc[226];	// ref layout: +0x1fc/+0x200 after stock fields
	int m_iSgtcPad1fc;		// ref gap @+0x1fc (unused on coujaf)
	CBaseEntity *m_pSgtcLatchTarget;	// ref @+0x200 — enemy player ptr; PA/Idle latch melee
	TraceResult m_trSgtcRef204;	// ref Swing tail stores hit trace @+0x204 (couteau uses float gate here)
	// 32-bit ref layout pad; on 64-bit TraceResult is larger than 0x3c, so clamp
	// to 1 byte (the ref offsets are 32-bit only, but the 64-bit target must compile).
	char m_sgtcPadTo240[0x3c > (int)sizeof( TraceResult ) ? 0x3c - (int)sizeof( TraceResult ) : 1];
	int m_iSprShockwave;		// ref @+0x240 — sprites/shockwave.spr (PA TE_BEAMCYLINDER)
};

class CCouteau : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	void Holster( int skiplocal = 0 );	// ref @0xd6348 — pad seed only (+ gameplay holster)
	void EXPORT SwingAgain( void );
	int Swing( int fFirst );
	void ThrowIt( void );
	int iItemSlot( void ) { return 1; }
	int m_iSwing;
	TraceResult m_trHit;
private:
	unsigned short m_usFire;
public:
	char m_sgtcPadTo1fc[226];	// shared coujaf/couteau ref layout @+0x1fc/+0x200
	int m_iSgtcPad1fc;
	int m_iSgtcMeleeLatch;	// ref @+0x200 — cleared on Deploy (@0xd6305)
	float m_flSgtcThrowGate204;	// ref @+0x204 — PA/Throwit gate (0 after throw tail)
	float m_flSgtcThrowPad208;	// ref @+0x208 — gpGlobals->time + 0.5 on throw tail
};

// ---- Grenades / thrown ----

class CFlashBang : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	BOOL CanHolster( void );
	void Holster( int skiplocal = 0 );
	int iItemSlot( void ) { return 5; }
};

// CSGTCHandGrenade removed: reference maps weapon_handgrenade to stock CHandGrenade.

class CGrenadeChoc : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	void Holster( int skiplocal = 0 );
	int iItemSlot( void ) { return 5; }	// ref @0xef582 returns 5 (was 4)

	float	m_flChargeBegin;	// ref +0x184 — gpGlobals->time or unset (<0)
	float	m_flChargeLatch;	// ref +0x188 — release timing latch
	int	m_iSgtcGacThrowAlt;	// ref +0x1fc — alt ShootFlash path (secondary hold)
};

class CSmokeGrenade : public CSGTCGasGrenadeWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	BOOL CanHolster( void ) { return SGTC_GasCanHolster(); }
	void Holster( int skiplocal = 0 ) { SGTC_GasHolster( skiplocal, SGTC_WPN_SMOKEGRENADE ); }
	int iItemSlot( void ) { return 2; }
};

class CNerveGrenade : public CSGTCGasGrenadeWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	BOOL CanHolster( void ) { return SGTC_GasCanHolster(); }
	void Holster( int skiplocal = 0 ) { SGTC_GasHolster( skiplocal, SGTC_WPN_NERVEGRENADE ); }
	int iItemSlot( void ) { return 3; }
};

class CNarcoGrenade : public CSGTCGasGrenadeWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	BOOL CanHolster( void ) { return SGTC_GasCanHolster(); }
	void Holster( int skiplocal = 0 ) { SGTC_GasHolster( skiplocal, SGTC_WPN_NARCOGRENADE ); }
	int iItemSlot( void ) { return 4; }
};

class CMustardGrenade : public CSGTCGasGrenadeWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	BOOL CanHolster( void ) { return SGTC_GasCanHolster(); }
	void Holster( int skiplocal = 0 ) { SGTC_GasHolster( skiplocal, SGTC_WPN_MUSTARDGRENADE ); }
	int iItemSlot( void ) { return 5; }
};

// ---- Placeable / special ----

// CSGTCC4 removed: reference maps weapon_c4 to stock CSatchel.

// CSGTCMine (weapon) removed: reference maps weapon_mine to stock tripmine
// class; name collided with sgtc_monsters.h's CSGTCMine monster.

class CTacgun : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	void Holster( int skiplocal = 0 );
	int iItemSlot( void ) { return 2; }
private:
	int m_iTacHudSel;		// ref this+0x1fc — cycles 0..2 (wrap); drives HudInfoMode 1..3
	float m_flSgtcPad200;	// ref this+0x200 — cleared 0.0f on PA/SA epilogue (@0x18ef14)
};

// ---- Device / heal / stealth ----

class CMainDieu : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	int iItemSlot( void ) { return 1; }
	void Holster( int skiplocal = 0 );	// ref @0x119074
private:
	int	m_iSprShockwave;		// ref +0x204 sprites/shockwave.spr
	unsigned short m_usFire;		// ref +0x20c events/main.sc
	unsigned short m_usFireCont;	// ref +0x20e events/mainc.sc
	unsigned short m_usFire2;		// ref +0x210 events/main2.sc
	unsigned short m_usFireStop;	// ref +0x212 events/mains.sc
	int	m_iSprHandTir;			// ref +0x208 sprites/handtir.spr
	BOOL	m_bSgtcMainBeam;		// ref +0x214 hold-fire latch
	BOOL	m_bSgtcMainCont;		// ref +0x215 mainc event sent
	float	m_flSgtcBlastTimer;		// ref +0x1fc regen / secondary latch
	float	m_flSgtcMainAmmoGate;	// ref +0x200 ammo tick gate
};

class CMedicx : public CBasePlayerWeapon
{
public:
	int GetItemInfo( ItemInfo *p );
	int AddToPlayer( CBasePlayer *pPlayer );
	void Spawn( void );
	void Precache( void );
	BOOL Deploy( void );
	void PrimaryAttack( void );
	void SecondaryAttack( void );
	void Reload( void );
	void WeaponIdle( void );
	virtual BOOL UseDecrement( void )
	{
#if CLIENT_WEAPONS
		return TRUE;	// ref @0x10c460 (not stock FALSE)
#else
		return FALSE;	// timers are absolute gpGlobals->time when prediction is off (#30)
#endif
	}
	BOOL IsUseable( void );
	int iItemSlot( void ) { return 1; }
};

class CMedicxgoa : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	BOOL IsUseable( void );
	int iItemSlot( void ) { return 2; }	// ref @0xf706e returns 2 (was 4)
};

class CInvis : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	void EXPORT InvisThink( void );
	void Holster( int skiplocal = 0 );
	int iItemSlot( void ) { return 1; }	// ref @0x10c350 returns 1 (was 3)

	float	m_flInvisArmTime;	// ref weapon +0x200
	float	m_flShieldLoadLatch;	// ref weapon +0x204 — forcefield_loading deadline
	float	m_flSgtcChargeLatch;	// ref weapon +0x1fc — idle ammo mirror latch
private:
	unsigned short m_usInvisFire;	// ref weapon +0x20a
	unsigned short m_usShieldFire;	// ref weapon +0x20c
};

class CLarve : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
	void Holster( int skiplocal = 0 );
	int iItemSlot( void ) { return 5; }	// ref @0x1136d4 returns 5 (was 1)

	float	m_flSgtcLarveFireLatch;	// ref +0x1fc — set 1 on larve spawn, consumed by WeaponIdle
	unsigned short m_usLarveFire;	// ref +0x200 events/larve.sc
};

// weapon_shield: personal shield wall. Spawns the entities peer's
// effect_shield via Create() so no CShield ODR clash.
class CShieldGun : public CBasePlayerWeapon
{
public:
	SGTC_DECLARE_WEAPON();
	int iItemSlot( void ) { return 4; }
};

// ---- Ammo ----

class CPsg1AmmoClip : public CBasePlayerAmmo
{
public:
	void Spawn( void );
	void Precache( void );
	BOOL AddAmmo( CBaseEntity *pOther );
};

// ---------------------------------------------------------------------------
// Shared implementation helpers — used by every sgtc_wpn_*.cpp. Moved here
// from sgtc_weapons.cpp when the bodies were split per-weapon.
// ---------------------------------------------------------------------------

enum sgtc_anim_e
{
	SGTC_IDLE = 0,
	SGTC_DRAW,
	SGTC_FIRE1,
	SGTC_FIRE2,
	SGTC_FIRE3,
	SGTC_RELOAD,
	SGTC_HOLSTER,
	SGTC_LAUNCH,
	SGTC_PUMP
};

// SGTC weapons have no client-side prediction (cl_dll/hl/hl_weapons.cpp only
// knows the stock WEAPON_* ids, which collide with SGTC ids 1-15), so the
// firing client must receive its own events: never use FEV_NOTHOST here, even
// if CLIENT_WEAPONS is ever turned back on (#67). Add FEV_RELIABLE at the call
// site where an event must not be dropped.
#define SGTC_EVENT_FLAGS 0


#define SGTC_ADD_TO_PLAYER( cls )												\
int cls::AddToPlayer( CBasePlayer *pPlayer )								\
{																			\
	if( CBasePlayerWeapon::AddToPlayer( pPlayer ) )							\
	{																		\
		MESSAGE_BEGIN( MSG_ONE, gmsgWeapPickup, NULL, pPlayer->pev );		\
			WRITE_BYTE( m_iId );											\
		MESSAGE_END();														\
		return TRUE;														\
	}																		\
	return FALSE;															\
}

#define SGTC_GETITEM( cls, A1, MA1, MC, SL, PS, ID, FL, WT )					\
int cls::GetItemInfo( ItemInfo *p )											\
{																			\
	p->pszName = STRING( pev->classname );									\
	p->pszAmmo1 = A1;														\
	p->iMaxAmmo1 = MA1;														\
	p->pszAmmo2 = NULL;														\
	p->iMaxAmmo2 = -1;														\
	p->iMaxClip = MC;														\
	p->iSlot = SL;															\
	p->iPosition = PS;														\
	p->iFlags = FL;															\
	p->iId = m_iId = ID;													\
	p->iWeight = WT;														\
	return 1;																\
}

#define SGTC_WEAPON_IDLE( cls )													\
void cls::WeaponIdle( void )												\
{																			\
	ResetEmptySound();														\
	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )						\
		return;																\
	SendWeaponAnim( SGTC_IDLE );											\
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +							\
		UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );			\
}

#define SGTC_WEAPON_RELOAD( cls, MAXCLIP )										\
void cls::Reload( void )													\
{																			\
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 || m_iClip == MAXCLIP )\
		return;																\
	DefaultReload( MAXCLIP, SGTC_RELOAD, 1.5 );								\
}

#define SGTC_NO_RELOAD( cls )													\
void cls::Reload( void )													\
{																			\
}

#define SGTC_NO_SECONDARY( cls )												\
void cls::SecondaryAttack( void )											\
{																			\
}

#endif // SGTC_WEAPONS_H
