// sgtc_sniper_sway.h - shared PSG1 scope sway evaluator.
//
// Header-only on purpose: the client (cl_dll) and the server (dlls) must agree
// on the exact same numbers for bot compensation, so both translation units
// compile the one implementation instead of linking a shared object.
// No engine headers here: plain C++98 + <math.h> so it stays includable
// anywhere (including the stub-engine test harness).
//
// Model: a deterministic low-frequency Lissajous figure-eight.
//   pitch: primary sine at SWAY_BASE_HZ (0.45 Hz)
//   yaw:   sine at twice that rate (the "eight")
//   both get a smaller second-octave term so the drift does not look like a
//   clean machine sine.
// Phases come from SGTC_Sway_Hash01(seed,k) so every player drifts differently
// while staying reproducible for a given seed.
//
// Amplitude: at strength 1 with every other input 0 the peak excursion is
// 0.30 deg pitch / 0.42 deg yaw.  The extra inputs scale that peak:
//   move     up to +120% pitch / +140% yaw (linear in move, clamped to [0,1])
//   airborne +160% pitch / +190% yaw
//   hurt     +90% pitch / +100% yaw
//   fire     +70% pitch / +60% yaw
//   crouch   x0.55 total (lerp from 1.0 for crouch 0 to 0.55 for crouch 1)
//   breath   x(1 - 0.9*breath): a relaxed breath holds 90% of the sway down
//   strength global multiplier; strength <= 0 returns exactly 0, 0
// Result is clamped to |pitch| <= 3.0 and |yaw| <= 4.0 degrees.

#pragma once

#include <math.h>

struct SgtcSwayInputs
{
	float move;		// 0..1 - ground speed / 320
	float airborne;		// 0..1 - 1 while off the ground
	float crouch;		// 0..1 - 1 while ducked
	float hurt;		// 0..1 - 1 for a short window after taking damage
	float fire;		// 0..1 - 1 for a short window after firing
	float breath;		// 0..1 - 1 = holding breath (sway nearly gone)
	float strength;		// global amplitude multiplier; 0 disables sway
};

// Deterministic [0,1) hash: integer mixing only, so every caller (MSVC, GCC,
// MinGW, 32-bit) produces the same phase for the same seed/salt.
inline float SGTC_Sway_Hash01( unsigned int seed, unsigned int salt )
{
	unsigned int h = ( seed * 2654435761u ) ^ ( salt * 2246822519u );

	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	h *= 3266489917u;
	h ^= h >> 16;

	return (float)( h & 0x00ffffffu ) * ( 1.0f / 16777216.0f );
}

// sin() through double: <math.h> under strict C++98 does not promise sinf.
inline float SGTC_Sway_Sin( float x )
{
	return (float)sin( (double)x );
}

inline float SGTC_Sway_Clamp01( float x )
{
	if( x < 0.0f )
		return 0.0f;
	if( x > 1.0f )
		return 1.0f;
	return x;
}

// Evaluates the sway offset in degrees.  Both outs are always written and are
// bit-identical for identical arguments.
inline void SGTC_Sway_Eval( unsigned int seed, float time, const SgtcSwayInputs &in,
	float *outPitchDeg, float *outYawDeg )
{
	float strength = in.strength;

	if( !( strength > 0.0f ) )	// also rejects NaN
	{
		if( outPitchDeg )
			*outPitchDeg = 0.0f;
		if( outYawDeg )
			*outYawDeg = 0.0f;
		return;
	}

	const float SWAY_PI = 3.14159265358979f;
	const float SWAY_TAU = 2.0f * SWAY_PI;
	const float SWAY_BASE_HZ = 0.45f;	// primary pitch rate

	float move = SGTC_Sway_Clamp01( in.move );
	float airborne = SGTC_Sway_Clamp01( in.airborne );
	float crouch = SGTC_Sway_Clamp01( in.crouch );
	float hurt = SGTC_Sway_Clamp01( in.hurt );
	float fire = SGTC_Sway_Clamp01( in.fire );
	float breath = SGTC_Sway_Clamp01( in.breath );

	// Primary amplitudes.  Both harmonics are summed and then divided by
	// (1 + weight), so the combined excursion measures the documented 0.30 deg
	// pitch / 0.42 deg yaw at strength 1 with every other input 0 (sampled over
	// a full phase span; the harmonics can add a few percent when they align).
	const float SWAY_PITCH_AMP = 0.34f;
	const float SWAY_YAW_AMP = 0.44f;
	const float SWAY_PITCH_OCT2 = 0.30f;
	const float SWAY_YAW_OCT2 = 0.28f;

	float ampPitch = SWAY_PITCH_AMP * ( 1.0f + 1.20f * move + 1.60f * airborne
		+ 0.90f * hurt + 0.70f * fire );
	float ampYaw = SWAY_YAW_AMP * ( 1.0f + 1.40f * move + 1.90f * airborne
		+ 1.00f * hurt + 0.60f * fire );

	float crouchMul = 1.0f - 0.45f * crouch;	// 1.0 .. 0.55
	float breathMul = 1.0f - 0.90f * breath;	// 1.0 .. 0.10

	ampPitch *= crouchMul * breathMul * strength;
	ampYaw *= crouchMul * breathMul * strength;

	// Per-seed phase offsets; salt 0 is left free for callers that want a
	// second independent stream later.
	float phasePitch = SGTC_Sway_Hash01( seed, 1u ) * SWAY_TAU;
	float phasePitch2 = SGTC_Sway_Hash01( seed, 2u ) * SWAY_TAU;
	float phaseYaw = SGTC_Sway_Hash01( seed, 3u ) * SWAY_TAU;
	float phaseYaw2 = SGTC_Sway_Hash01( seed, 4u ) * SWAY_TAU;

	float w = SWAY_TAU * SWAY_BASE_HZ;

	float pitch = ampPitch * ( SGTC_Sway_Sin( time * w + phasePitch )
		+ SWAY_PITCH_OCT2 * SGTC_Sway_Sin( time * w * 2.0f + phasePitch2 ) )
		/ ( 1.0f + SWAY_PITCH_OCT2 );
	float yaw = ampYaw * ( SGTC_Sway_Sin( time * w * 2.0f + phaseYaw )
		+ SWAY_YAW_OCT2 * SGTC_Sway_Sin( time * w * 4.0f + phaseYaw2 ) )
		/ ( 1.0f + SWAY_YAW_OCT2 );

	if( pitch > 3.0f )
		pitch = 3.0f;
	else if( pitch < -3.0f )
		pitch = -3.0f;

	if( yaw > 4.0f )
		yaw = 4.0f;
	else if( yaw < -4.0f )
		yaw = -4.0f;

	if( outPitchDeg )
		*outPitchDeg = pitch;
	if( outYawDeg )
		*outYawDeg = yaw;
}
