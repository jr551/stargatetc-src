// sgtc_env.h — CClientEnv: SGTC environment particle client element.
// ref client.dll FUN_1006fadd: pfnHookUserMsg block (ClearEnv..ChgPartSt),
// env slots at this+0x14 count / this+0x1c array, env obj size 0x110.
#pragma once
#if !defined( SGTC_ENV_H )
#define SGTC_ENV_H

#include "hud.h"

#define SGTC_MAX_ENV_PARTICLES 64

// ref env record @ new(0x110): coords are 4-byte floats (FUN_10065280),
// color bytes scaled /255, id byte at +0xac, state byte at +0xb0.
class CSgtcEnvParticle
{
public:
	float min[3];		// +0x28..+0x33
	float max[3];		// +0x34..+0x3f
	float vel[3];		// +0x40..+0x4b
	float acc[3];		// +0x4c..+0x57
	float parms[6];		// +0x58..+0x70 (density, life, friction…)
	float msec[3];		// +0x74..+0x7f
	float radius;		// +0x80
	int color[4];		// +0x60/64/68/6c bytes /255
	int flags;		// +0x84 (EnvFount: the wire type byte; unused otherwise)
	int type;		// +0x88 (EnvFull: wire type byte; Rain 1 / Fount 2 / Lens 3 / Ring 4 tag the kind)
	short spr;		// +0x8c (sprite index, wire BYTE)
	int count;		// wire SHORT "nombre"
	int id;			// +0xac
	int state;		// +0xb0 (ChgPartSt target)
	int active;
};

// Wire layouts, one place. The server writers are in dlls/sgtc_entities_env.cpp
// (SGTC_WriteEnvParticulePayload / SGTC_WriteEnvRainPayload /
// SGTC_WriteEnvFountainPayload); the readers are in sgtc_env.cpp and must
// consume exactly these bytes in this order. COORD is a 2 byte short (x / 8).
//
//   EnvFull  47 bytes, registered 47
//     15 COORD  origin, oldorigin, accel, v0, spread  (min, max, vel, acc, msec slots)
//      4 BYTE   colour r, g, b, a
//      4 COORD  size.x, size.y, life, bounce          (parms[0..2], radius)
//      1 BYTE   sprite   -> spr
//      1 BYTE   type     -> type
//      1 SHORT  count    -> count
//      1 BYTE   id       -> id
//   EnvRain  15 bytes, registered 15
//      6 COORD  origin, oldorigin                     (min, max)
//      1 BYTE   sprite   -> spr
//      1 SHORT  count    -> count
//   EnvFount 43 bytes, registered 43
//     15 COORD  origin, minRadius, maxRadius, origine, minAngle, maxAngle,
//               minVel, maxVel, directeur             (min, max, vel, acc, msec slots)
//      4 BYTE   colour r, g, b, a
//      4 BYTE   size.x, size.y, life, bounce (rounded) (parms[0..2], radius)
//      1 BYTE   sprite   -> spr
//      1 BYTE   type     -> flags
//      1 SHORT  count    -> count
//      1 BYTE   id       -> id
//   ChgPartSt 2 bytes: id, state.
//
class CClientEnv : public CHudBase
{
public:
	int Init( void );
	int VidInit( void );
	int Draw( float flTime );
	void Reset( void );

	int MsgFunc_ClearEnv( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_EnvFull( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_EnvRain( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_EnvFount( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_NewLens( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_AddRing( const char *pszName, int iSize, void *pbuf );
	int MsgFunc_ChgPartSt( const char *pszName, int iSize, void *pbuf );

	int m_iEnvCount;	// ref this+0x14
	CSgtcEnvParticle m_Env[SGTC_MAX_ENV_PARTICLES]; // ref this+0x1c
};

extern CClientEnv g_ClientEnv;

#endif
