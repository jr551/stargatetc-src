// sgtc_rea_debug.h — build-8 "give everyone every weapon" test mode.
//
// sgtc_rea_debug 1 (default 0): every human/bot gets the complete shipped
// weapon set with full ammo, and the class/team/pack loadout restrictions are
// inert.  Self-contained so the whole mode can be removed by deleting this
// file plus the few marked hooks.
#pragma once

#include "extdll.h"

class CBasePlayer;

void SGTC_ReaDebugInit( void );
BOOL SGTC_ReaDebugActive( void );
void SGTC_ReaDebug_Tick( void );
void SGTC_ReaDebug_ArmPlayer( CBasePlayer *pPlayer );
void SGTC_ReaDebug_CycleWeapon( CBasePlayer *pPlayer, int iDirection );
void SGTC_ReaDebug_OnPlayerSpawn( CBasePlayer *pPlayer );
