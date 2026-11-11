// sgtc_muzzlefx.h — build-19 muzzle flash light + smoke.
#pragma once
#include "extdll.h"
class CBaseEntity;
void SGTC_MuzzleFxInit( void );
void SGTC_MuzzleFx( entvars_t *pev, const Vector &vecSrc, int iBulletType );
void SGTC_DiagMuzzle( CBaseEntity *pPlayer, int iBulletType );
