// sgtc_throwfx.h — shared "throw" playback for SGTC throwables (events/throw.sc).
#pragma once
#include "extdll.h"
class CBasePlayer;
void SGTC_ThrowFxPrecache( void );
void SGTC_PlayThrowFx( CBasePlayer *pPlayer, const Vector &vecOrigin, float flStrength, int iWeaponId );
// Once per server frame (client.cpp StartFrame). Plays the throw for stock handgrenade throws.
void SGTC_ThrowFx_Frame( void );
