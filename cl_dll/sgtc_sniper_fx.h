// sgtc_sniper_fx.h — StargateTC client-side sniper/energy-weapon feedback
// (NEW file). Scope vignette, exponentially smoothed zoom and the SgtcHit
// hit-marker/sound layer.
//
// Everything here is client-local: nothing is sent to the server and the
// stock client does not depend on it (hud.cpp / cdll_int.cpp / sgtc_protocol.cpp
// are the only call sites).
#pragma once

// Registers the client cvars (sgtc_sniper_scope / sgtc_sniper_hitmark). Safe
// to call on every connect: CVAR_CREATE returns the existing cvar_t.
void SGTC_SniperFx_Init( void );

// Called once per frame from HUD_Redraw, after gHUD.Redraw. Draws the scope
// vignette while gHUD.m_inScope and the hit markers, both with plain FillRGBA
// primitives (no sprites are authored or loaded here).
void SGTC_SniperFx_Draw( void );

// Zoom state. The target is the value carried by the SetFOV usermsg (0 means
// "no zoom, use default_fov"), the smoothed value eases toward it.
void SGTC_SniperFx_SetFovTarget( int newfov );
float SGTC_SniperFx_GetSmoothedFov( float frametime );

// "SgtcHit" (2 bytes: flags 1=hit 2=headshot 4=kill, byte 1 = damage).
int SGTC_SniperFx_MsgFunc_SgtcHit( const char *pszName, int iSize, void *pbuf );
