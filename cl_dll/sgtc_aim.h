// sgtc_aim.h - client-side scoped aim drift, hold-breath stamina and the
// outgoing usercmd button bit that mirrors it.
//
// Driven from cl_dll/input.cpp CL_CreateMove:
//   SGTC_Aim_AdjustViewAngles( frametime, viewangles )  right after CL_AdjustAngles
//   SGTC_Aim_AddButtons( cmd )                          after cmd->buttons is assigned
// Event handlers call SGTC_Aim_OnLocalFire() when the local player's sniper/zat
// fires.  The remaining accessors are read-only probes for the HUD / FX code.
#ifndef SGTC_AIM_H
#define SGTC_AIM_H

struct usercmd_s;

void SGTC_Aim_Init( void );

// Subtracts the offset applied last frame, evaluates a fresh one and applies
// it.  Eases the offset out over ~0.15s when the player unscopes.
void SGTC_Aim_AdjustViewAngles( float frametime, float *viewangles );

// Local player fired a sniper/zat: bumps the fire sway window.
void SGTC_Aim_OnLocalFire( void );

// True while the local player holds a sniper/zat and is zoomed in.
bool SGTC_Aim_IsScoped( void );

// Hold-breath stamina in [0,1] (1 = full lung).
float SGTC_Aim_Stamina01( void );

// True while hold-breath is active (input held and lungs not empty).
bool SGTC_Aim_HoldingBreath( void );

// Currently applied sway offset in degrees.
void SGTC_Aim_GetOffset( float *pitchDeg, float *yawDeg );

// Adds IN_ALT1 to the outgoing command while hold-breath is engaged.
void SGTC_Aim_AddButtons( struct usercmd_s *cmd );

#endif // SGTC_AIM_H
