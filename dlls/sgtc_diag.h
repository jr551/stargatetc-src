// sgtc_diag.h — opt-in diagnostic logging for build-5 regression hunts.
// Enable with: sgtc_diag_log 1  (server cvar, default 0, no effect otherwise).
#pragma once

#include "extdll.h"

class CBaseEntity;

void SGTC_DiagInit( void );
BOOL SGTC_DiagEnabled( void );
void SGTC_DiagSparks( const char *pszSource, const Vector &vecOrigin );
void SGTC_DiagSpawn( const char *pszClass, CBaseEntity *pOwner, const Vector &vecOrigin,
	float flScale, int iBody, int iSkin, int iSeq );
void SGTC_DiagGrant( CBaseEntity *pPlayer, const char *pszWeapon, int iWeaponBits );
void SGTC_DiagWeaponList( CBaseEntity *pPlayer, int iRecords );
void SGTC_DiagBeam( CBaseEntity *pStart, CBaseEntity *pEnd, int iSprite, int iNoise );
