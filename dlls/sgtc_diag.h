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

// Developer JSON lines (SGTC_PACKAUTO, SGTC_BOT_LOADOUT, SGTC_SP_ENEMIES). The headless
// runs read them from the console log, so they stay on for sgtc_test_enable and
// sgtc_diag_log runs. In normal play ALERT( at_console ) text lands in the on-screen
// notify area, so they stay silent there.
#if defined( __GNUC__ )
void SGTC_DevLog( const char *pszFormat, ... ) __attribute__(( format( printf, 1, 2 ) ));
#else
void SGTC_DevLog( const char *pszFormat, ... );
#endif
