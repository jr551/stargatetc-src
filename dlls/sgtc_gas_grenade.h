// sgtc_gas_grenade.h — cook-and-throw gas grenades + cloud thinks (ref P106 audit).
#pragma once
#ifndef SGTC_GAS_GRENADE_H
#define SGTC_GAS_GRENADE_H

#include "cbase.h"
#include "weapons.h"

enum SGTC_GasType
{
	SGTC_GAS_MUSTARD = 0,
	SGTC_GAS_NARCO,
	SGTC_GAS_NERVE,
	SGTC_GAS_SMOKE,
};

extern short g_sModelIndexSgtcMustard;
extern short g_sModelIndexSgtcNarco;
extern short g_sModelIndexSgtcNerve;
extern short g_sModelIndexSgtcGasSmoke;

void SGTC_PrecacheGasSprites( void );

class CSGTCGasCloud : public CGrenade
{
public:
	int m_iGasType;
	float m_flCloudRadius;
	float m_flExpireTime;
	int m_iThrowerTeam;

	void Spawn( void );
	void EXPORT CloudThink( void );
	void Detonate( void );	// ref: becomes cloud, not HE burst
	void EXPORT TumbleThink( void );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];
};

CSGTCGasCloud *SGTC_ShootGasGrenade( entvars_t *pevOwner, Vector vecStart,
	Vector vecVelocity, float time, const char *pszWorldModel, SGTC_GasType gasType,
	float flDmg );

// Shared weapon body (ref cook fields at weapon+0x1fc/+0x200/+0x204).
class CSGTCGasGrenadeWeapon : public CBasePlayerWeapon
{
public:
	float m_flGasStartThrow;	// ref +0x1fc, 0 = not primed
	float m_flGasReleaseThrow;	// ref +0x200
	float m_flGasDeployIdle;	// ref +0x204

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	BOOL SGTC_GasCanHolster( void );
	void SGTC_GasHolster( int skiplocal, int iWeaponId );
	BOOL SGTC_GasDeploy( int disp, const char *pszView, const char *pszPlayer );
	void SGTC_GasPinPull( void );
	void SGTC_GasWeaponIdle( int iWeaponId, SGTC_GasType gasType, const char *pszWorldModel,
		float flDmg );
};

#endif
