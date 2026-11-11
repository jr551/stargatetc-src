// hlsdk-xash3d/dlls/sgtc_monsters.h
//
// StargateTC custom server monsters — see file header in sgtc_monsters.cpp.

#pragma once
#ifndef SGTC_MONSTERS_H
#define SGTC_MONSTERS_H

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "soundent.h"
#include "skill.h"
#include "zombie_monster.h"
#include "rat_monster.h"

#ifndef CLIENT_DLL

class CSGTCReplic : public CGrenade
{
public:
	void Spawn( void );
	void Precache( void );
	int Classify( void ) { return CLASS_ALIEN_BIOWEAPON; }

	void EXPORT HuntThink( void );
	void EXPORT SuperBounceTouch( CBaseEntity *pOther );
	void Detonate( void );
	void Killed( entvars_t *pevAttacker, int iGib );

	float m_flDie;
	float m_flNextHunt;
	float m_flNextSound;
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
};

class CSGTCLarve : public CGrenade
{
public:
	void Spawn( void );
	void Precache( void );
	int Classify( void ) { return CLASS_ALIEN_BIOWEAPON; }

	void EXPORT HuntThink( void );
	void EXPORT SuperBounceTouch( CBaseEntity *pOther );
	void Detonate( void );
	void Killed( entvars_t *pevAttacker, int iGib );
	virtual CBaseEntity *BestVisibleEnemy( void );

	float m_flDie;			// ref +0x3ec
	Vector m_vecTarget;		// ref +0x3f0
	float m_flNextHunt;		// ref +0x3fc
	float m_flNextHit;		// ref +0x400 (touch/bounce gate)
	Vector m_posPrev;		// ref +0x404
	EHANDLE m_hOwner;		// ref +0x410
	int m_iSgtcFlag;		// ref +0xd8 — armed by weapon larve spawn @0x145c1f
	float m_flSgtcTouchGate;	// ref +0x39c — post-touch cooldown stamp
	char m_bSgtcLarveDetLatch;	// ref +0x41c — CLarve::SecondaryAttack arm
	char m_bSgtcLarveHuntLatch;	// ref +0x41d — HuntThink tail / BestVisibleEnemy gate
	static float m_flNextBounceSoundTime;	// ref B @0x229044
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
};

class CSGTCReplicateur : public CBaseMonster
{
public:
	void Spawn( void );
	void Precache( void );
	int Classify( void ) { return CLASS_ALIEN_MONSTER; }
	void SetYawSpeed( void ) { pev->yaw_speed = 120; }
	Vector Center( void );
	Vector BodyTarget( const Vector &posSrc );

	void EXPORT ReplicateurThink( void );
	void Killed( entvars_t *pevAttacker, int iGib );
};

// ref CZombie::Spawn @0x1b7ffe — full RunAI via CZombie base.
class CSGTCImplanted : public CZombie
{
public:
	void Spawn( void );
	void Precache( void );
};

// ref CRat::Think @0x14b50a (monster_skull factory).
class CSGTCSkull : public CRat
{
public:
	void Spawn( void );
	void Precache( void );
	void EXPORT SkullThink( void );
	void Killed( entvars_t *pevAttacker, int iGib );

	float m_flNextSound;		// ref +0x3f0 skull attack/idle sound gate
	CBasePlayer *m_pSkullOwner;	// ref +0xc0
	int m_iSkullAxis;		// ref +0x3e0
	int m_iSkullPlane;		// ref +0x3e4
	int m_iSkullLane;		// ref +0x3e8
	int m_iSkullMode;		// ref +0x3f8 (=3)
	float m_flSkullBeamGate;	// ref +0x3ec TE_BEAMPOINTS throttle
	float m_flSkullThinkGate;	// ref +0x3f4
	byte m_bSkullFrateRamp;		// ref +0x400 pev->framerate ramp state
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
};

class CSGTCTac : public CBaseMonster
{
public:
	void Spawn( void );
	void Precache( void );
	int Classify( void ) { return CLASS_MACHINE; }
	BOOL IsTac( void ) { return TRUE; }

	void EXPORT TacThink( void );
	int TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType );
	void Killed( entvars_t *pevAttacker, int iGib );
	int ObjectCaps( void );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );

	// Range bubble (owner only) + damage feedback.
	CBasePlayer *Deployer( void );
	void ShowRangeBubble( CBasePlayer *pViewer, BOOL bStaged );
	void SendBubbleShell( CBasePlayer *pViewer, int iBrightness, int iLifeTenths );
	void EmitDamageSmoke( void );
	void Explode( entvars_t *pevAttacker );

	float m_flNextAction;		// ref +0x404 combat gate
	float m_flNextSweep;		// ref sweep / action scheduling
	EHANDLE m_hLockTarget;		// ref +0x408 trace latch
	Vector m_vecLockDelta;		// ref +0x40c..+0x414
	BOOL m_bLockDeltaSet;		// ref +0x418
	BOOL m_bTacArmed;		// ref +0x3ec
	int m_iTacMode;			// ref +0x3f0 (0/1/2)
	int m_iTacAnimStep;		// ref +0x3f4
	EHANDLE m_hDeployer;		// ref +0x400 owner edict
	int m_iGammaWaveSpr;		// ref +0x3f8
	int m_iGammaWaveSpr2;		// ref +0x3fc
	unsigned short m_usTacEvent;	// ref +0x41a events/tac.sc

	// Gameplay additions (not present in the reference build).
	float m_flNextPickup;		// deploy grace before +use may retrieve it
	float m_flLastDamage;		// last damage time — pickup is locked out under fire
	float m_flNextSmoke;		// damage smoke cadence
	float m_flNextHurt;		// hurt-sound cadence
	float m_flNextHint;		// owner bubble/hint throttle
	float m_flBubbleNext;		// staged bubble shell time
	EHANDLE m_hBubbleViewer;	// client that receives the staged shells
	int m_iBubbleShells;		// staged shells still to send
	BOOL m_bFreeDeploy;		// deployed under sv_crazy (no ammo spent): not retrievable
	BOOL m_bGone;			// picked up or exploding: nothing may act on it again

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
};

class CSGTCBodyPart : public CBaseMonster
{
public:
	void Spawn( void );
	void Precache( void );
	int Classify( void ) { return CLASS_ALIEN_MONSTER; }

	void EXPORT IdleThink( void );
	void Killed( entvars_t *pevAttacker, int iGib );
};

class CSGTCZatBoom : public CBaseMonster
{
public:
	void Spawn( void );
	void Precache( void );
	int Classify( void ) { return CLASS_MACHINE; }

	void EXPORT ChargeThink( void );
	void Discharge( void );
	void Killed( entvars_t *pevAttacker, int iGib );

	float m_flBlastTime;
	char m_bSgtcZatLatch;	// ref +0x3f4 — onground weapondrop latch
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
};

class CSGTCTrapBox : public CBaseMonster
{
public:
	void Spawn( void );
	void Precache( void );
	int Classify( void ) { return CLASS_MACHINE; }
	int TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType );
	void Killed( entvars_t *pevAttacker, int iGib );

	void EXPORT TrapThink( void );
	void Trigger( void );
	Vector m_vecPlace;
	int m_iPlaceSlot;
	int m_iSgtcPlasmaSpr;
	BOOL m_bTriggered;		// Trigger() re-entrancy guard
};

class CSGTCShieldEmit : public CBaseMonster
{
public:
	void Spawn( void );
	void Precache( void );
	int Classify( void ) { return CLASS_MACHINE; }

	void EXPORT EmitThink( void );
	void Killed( entvars_t *pevAttacker, int iGib );
	CBasePlayer *m_pSgtcOwner;
	int m_iSgtcMode;
	CBaseEntity *m_pSgtcEffect;
	float m_flSgtcNextFx;
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
};

class CSGTCLockDown : public CBaseEntity
{
public:
	void Spawn( void );
	void Precache( void );
	int Classify( void ) { return CLASS_MACHINE; }
	int TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType );
	void EXPORT LockThink( void );

	EHANDLE m_hLockTarget;		// ref +0xcc — owner player (pev->owner)
	BOOL m_bLockAnchored;		// ref +0x3e0
	Vector m_vecLockOffset;		// ref +0x3ec..+0x3f4
	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
};

#endif // CLIENT_DLL

#endif // SGTC_MONSTERS_H
