// sgtc_gate_dlight_logic.h - pure math/string logic for the gate puddle light.
//
// Deliberately free of engine headers so the unit test in
// tests/features/test_gate_dlight_logic.py can compile and exercise the exact
// functions the server DLL uses (no duplicated reference implementation).
//
// Model: an open event horizon gets a TE_DLIGHT whose radius is
//   clamp(2 * bbox half-diagonal, 200, 600) * brightness
// where brightness is (renderamt / 255) * flicker * 0.92.  For brush-model
// horizons that carry a render mode, renderamt is the bound entity's own
// render amount, ramped 0..255 by the map's env_render entities, so 0
// (closed) produces no light and the opening ramp flares naturally; without a
// render mode the brush is full, and clustered world faces are always full.
// The flicker is two sines sampled on the 10 fps (1 second, 10 frame) horizon
// animation clock, so it stays in step with the puddle texture instead of
// drifting against it.
//
// Detection lives in the BSP scan header (sgtc_gate_dlight_bsp.h), which uses
// the texture matcher and world-face clustering below.  The string/geometry
// helpers are kept independent of the engine so the feature tests can compile
// this header directly.

#ifndef SGTC_GATE_DLIGHT_LOGIC_H
#define SGTC_GATE_DLIGHT_LOGIC_H

#include <math.h>

// Blue-white event horizon colour.  The blue channel is dominant and the red
// channel never reaches 255, so the light is never pure white.
#define SGTC_GATE_DLIGHT_COLOR_R 150
#define SGTC_GATE_DLIGHT_COLOR_G 180
#define SGTC_GATE_DLIGHT_COLOR_B 255

// TE_DLIGHT wire units (FWGS engine/client/cl_tent.c, TE_DLIGHT case):
//   radius = byte * 10 units, life = byte * 0.1 s, decay = byte * 10 units/s.
#define SGTC_GATE_DLIGHT_RADIUS_UNIT 10.0f
#define SGTC_GATE_DLIGHT_LIFE_BYTE 2      // 0.2 s: two 0.1 s driver ticks overlap
#define SGTC_GATE_DLIGHT_DECAY_BYTE 15    // 150 units/s: ~15 units between updates

#define SGTC_GATE_DLIGHT_RADIUS_MIN 200.0f
#define SGTC_GATE_DLIGHT_RADIUS_MAX 600.0f
#define SGTC_GATE_DLIGHT_PEAK 0.92f       // ceiling: never a hard white flash

// BSP horizon scan limits.  A map contributes at most 512 matching faces and
// 64 clustered world patches; brushes are capped separately in the scan header.
#define SGTC_GATE_DL_MAX_FACES 512
#define SGTC_GATE_DL_MAX_PATCHES 64

// World-face clustering tolerances.  Faces join a patch only when their
// normals agree to ~10 degrees, their planes agree within 8 units and their
// centroids are within max(96, patch radius + face radius + 24).
#define SGTC_GATE_DL_CLUSTER_DOT 0.985f
#define SGTC_GATE_DL_CLUSTER_PLANE_TOL 8.0f
#define SGTC_GATE_DL_CLUSTER_NEAR 96.0f
#define SGTC_GATE_DL_CLUSTER_NEAR_MARGIN 24.0f

typedef struct {
	float nx, ny, nz;   /* unit face normal after side application */
	float dist;         /* plane distance, n . x == dist */
	float cx, cy, cz;   /* face centroid (world) */
	float radius;       /* max distance from centroid to a face vertex */
	int   model;        /* BSP model index (0 = world) */
} SGTCHorizonFace;

typedef struct {
	float nx, ny, nz;
	float cx, cy, cz;
	float radius;
	int   faces;
} SGTCHorizonPatch;

static inline char SGTC_GateDlightLower( char c )
{
	return ( c >= 'A' && c <= 'Z' ) ? (char)( c - 'A' + 'a' ) : c;
}

// Case-insensitive "does s contain needle (already lowercase)".
static inline int SGTC_GateDlightContains( const char *s, const char *needle )
{
	if( !s || !needle )
		return 0;

	for( ; *s; ++s )
	{
		const char *a = s;
		const char *b = needle;
		while( *a && *b && SGTC_GateDlightLower( *a ) == *b )
		{
			++a;
			++b;
		}
		if( !*b )
			return 1;
	}
	return 0;
}

// Puddle-looking target name.  Verified against the nine shipped gate maps:
// matches vortex, vortex_x, vortex_iris, vortex_2b, 2vortex, 3vortex and
// naquada; does not match ring_ill.  Name alone is not sufficient - the
// caller also requires func_illusionary + kRenderTransTexture + a flat,
// gate-sized bbox, which excludes the maps' vortexlight (light_spot) and
// sonvortex (ambient_generic) entities.
static inline int SGTC_GateDlightNameMatches( const char *targetname )
{
	if( !targetname || !*targetname )
		return 0;

	if( SGTC_GateDlightContains( targetname, "vortex" ) )
		return 1;
	if( SGTC_GateDlightContains( targetname, "puddle" ) )
		return 1;
	if( SGTC_GateDlightContains( targetname, "horizon" ) )
		return 1;
	if( SGTC_GateDlightContains( targetname, "naquada" ) )
		return 1;

	return 0;
}

// Strip a GoldSrc animation prefix (+0..+9, +A..+J, -0..-9, -A..-J) and any
// leading liquid/scroll markers (!, {, ~, *, #).  "+0sgeh" and "{sgeh" both
// reduce to "sgeh"; the returned pointer aliases the input.
static inline const char *SGTC_GateDlightTextureBase( const char *name )
{
	if( !name )
		return 0;

	for( ;; )
	{
		int moved = 0;

		if( ( name[0] == '+' || name[0] == '-' ) && name[1] )
		{
			const char c = SGTC_GateDlightLower( name[1] );

			if( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'j' ) )
			{
				name += 2;
				moved = 1;
			}
		}

		while( *name == '!' || *name == '{' || *name == '~' ||
			*name == '*' || *name == '#' )
		{
			++name;
			moved = 1;
		}

		if( !moved )
			return name;
	}
}

// Case-insensitive "does s contain the token [tok, tok + len)".  Used for the
// arbitrary-case substrings of the sgtc_gate_dlight_textures cvar.
static inline int SGTC_GateDlightContainsSpan( const char *s, const char *tok, int len )
{
	int i;

	if( !s || !tok || len <= 0 )
		return 0;

	for( ; *s; ++s )
	{
		for( i = 0; i < len; ++i )
		{
			if( !s[i] || SGTC_GateDlightLower( s[i] ) != SGTC_GateDlightLower( tok[i] ) )
				break;
		}
		if( i >= len )
			return 1;
	}
	return 0;
}

// Texture-name horizon test.  extra is the sgtc_gate_dlight_textures cvar
// value: comma separated extra substrings supplied by mappers/servers.
static inline int SGTC_GateDlightTextureMatches( const char *name, const char *extra )
{
	static const char *builtin[] = {
		"horizon", "sgeh", "eventh", "vortex",
		"puddle", "wormhole", "naquada"
	};
	const char *base;
	const char *p;
	unsigned i;

	if( !name || !*name )
		return 0;

	base = SGTC_GateDlightTextureBase( name );
	if( !base || !*base )
		return 0;

	for( i = 0; i < sizeof( builtin ) / sizeof( builtin[0] ); ++i )
	{
		if( SGTC_GateDlightContains( base, builtin[i] ) )
			return 1;
	}

	if( !extra )
		return 0;

	for( p = extra; *p; )
	{
		const char *start;
		const char *end;

		while( *p == ',' || *p == ' ' || *p == '\t' )
			++p;
		start = p;
		while( *p && *p != ',' )
			++p;
		end = p;
		while( end > start && ( end[-1] == ' ' || end[-1] == '\t' ) )
			--end;

		if( end > start && SGTC_GateDlightContainsSpan( base, start, (int)( end - start ) ) )
			return 1;
	}

	return 0;
}

// "*N" -> N (N >= 1).  Returns -1 for anything else, including "*0" and NULL.
static inline int SGTC_GateDlightBrushModelIndex( const char *model )
{
	unsigned value = 0;
	int digits = 0;

	if( !model || model[0] != '*' )
		return -1;

	++model;
	if( !*model )
		return -1;

	while( *model )
	{
		if( *model < '0' || *model > '9' )
			return -1;
		if( digits >= 6 )
			return -1;            /* overflow guard; real models are < 4096 */
		value = value * 10u + (unsigned)( *model - '0' );
		++digits;
		++model;
	}

	if( value < 1u )
		return -1;
	return (int)value;
}

// A puddle brush is a thin plane with two gate-sized axes.  Thin <= 32 units
// admits 1..5 unit brushes (all shipped puddles) while rejecting large
// illusionary boxes such as sg_stargate's 158x158x343 ring_ill.
static inline int SGTC_GatePuddleSizeValid( float sx, float sy, float sz )
{
	float a[3];
	float t;

	a[0] = sx;
	a[1] = sy;
	a[2] = sz;

	if( a[0] > a[1] ) { t = a[0]; a[0] = a[1]; a[1] = t; }
	if( a[1] > a[2] ) { t = a[1]; a[1] = a[2]; a[2] = t; }
	if( a[0] > a[1] ) { t = a[0]; a[0] = a[1]; a[1] = t; }

	return a[0] > 0.0f && a[0] <= 32.0f && a[1] >= 64.0f && a[2] >= 64.0f;
}

// Index of the bbox axis that is the puddle plane normal.
static inline int SGTC_GateThinAxis( float sx, float sy, float sz )
{
	if( sx <= sy && sx <= sz )
		return 0;
	if( sy <= sz )
		return 1;
	return 2;
}

static inline float SGTC_GateDlightFrac( float renderamt )
{
	if( renderamt <= 0.0f )
		return 0.0f;
	if( renderamt >= 255.0f )
		return 1.0f;
	return renderamt / 255.0f;
}

// Two-sine flicker on the horizon animation clock.  frame is the 10 fps
// animation frame index, so sin(frame * 2pi/10) is exactly 1 Hz and
// sin(frame * 2pi*0.55/10) is 0.55 Hz; both re-phase with the 1 s/10 frame
// texture loop instead of drifting against gpGlobals->time.
static inline float SGTC_GateDlightFlicker( float time )
{
	const float frame = floorf( time * 10.0f );
	const float tau = 6.28318530718f;

	return 0.82f + 0.12f * sinf( frame * ( tau / 10.0f ) )
	             + 0.06f * sinf( frame * ( tau * 0.55f / 10.0f ) );
}

// 0 when closed, flickering ramp-scaled brightness otherwise.
static inline float SGTC_GateDlightBrightness( float renderamt, float time )
{
	const float frac = SGTC_GateDlightFrac( renderamt );
	if( frac <= 0.0f )
		return 0.0f;

	return frac * SGTC_GateDlightFlicker( time ) * SGTC_GATE_DLIGHT_PEAK;
}

// Radius in world units, before brightness scaling.
static inline float SGTC_GateDlightRadius( float halfdiag )
{
	float r = halfdiag * 2.0f;

	if( r < SGTC_GATE_DLIGHT_RADIUS_MIN )
		r = SGTC_GATE_DLIGHT_RADIUS_MIN;
	if( r > SGTC_GATE_DLIGHT_RADIUS_MAX )
		r = SGTC_GATE_DLIGHT_RADIUS_MAX;
	return r;
}

// TE_DLIGHT radius byte: rounds, clamps, 10 world units per step.
static inline int SGTC_GateDlightRadiusByte( float radius, float brightness )
{
	float units = radius * brightness / SGTC_GATE_DLIGHT_RADIUS_UNIT;
	int v;

	if( units <= 0.0f )
		return 0;
	if( units > 255.0f )
		return 255;

	v = (int)( units + 0.5f );
	return v > 255 ? 255 : v;
}

// Greedy clustering of world faces into gate-sized patches.  Deterministic:
// faces are consumed in order, a patch absorbs at most one face per pass
// (eight passes) and recomputes its centroid/radius after every absorb.
// Later unmatched faces seed later patches; no patch is created after max_out.
static inline int SGTC_GateDlightClusterWorldFaces( const SGTCHorizonFace *faces, int count,
		SGTCHorizonPatch *out, int max_out )
{
	int used[SGTC_GATE_DL_MAX_FACES];
	int patchCount = 0;
	int i;

	if( !faces || !out || max_out <= 0 || count <= 0 )
		return 0;

	if( count > SGTC_GATE_DL_MAX_FACES )
		count = SGTC_GATE_DL_MAX_FACES;

	for( i = 0; i < count; ++i )
		used[i] = 0;

	for( i = 0; i < count; ++i )
	{
		SGTCHorizonPatch *patch;
		float patchDist;
		int pass;

		if( used[i] )
			continue;
		if( patchCount >= max_out )
			break;

		patch = &out[patchCount];
		patch->nx = faces[i].nx;
		patch->ny = faces[i].ny;
		patch->nz = faces[i].nz;
		patch->cx = faces[i].cx;
		patch->cy = faces[i].cy;
		patch->cz = faces[i].cz;
		patch->radius = faces[i].radius;
		patch->faces = 1;
		patchDist = faces[i].dist;
		used[i] = patchCount + 1;

		for( pass = 0; pass < 8; ++pass )
		{
			int found = -1;
			int j;

			for( j = 0; j < count; ++j )
			{
				float dot;
				float dx, dy, dz;
				float nearLimit;

				if( used[j] )
					continue;

				dot = patch->nx * faces[j].nx + patch->ny * faces[j].ny +
					patch->nz * faces[j].nz;
				if( dot < 0.0f ? -dot < SGTC_GATE_DL_CLUSTER_DOT :
					dot < SGTC_GATE_DL_CLUSTER_DOT )
					continue;

				if( dot > 0.0f )
				{
					if( patchDist - faces[j].dist > SGTC_GATE_DL_CLUSTER_PLANE_TOL ||
						faces[j].dist - patchDist > SGTC_GATE_DL_CLUSTER_PLANE_TOL )
						continue;
				}
				else
				{
					if( patchDist + faces[j].dist > SGTC_GATE_DL_CLUSTER_PLANE_TOL ||
						-( patchDist + faces[j].dist ) > SGTC_GATE_DL_CLUSTER_PLANE_TOL )
						continue;
				}

				dx = patch->cx - faces[j].cx;
				dy = patch->cy - faces[j].cy;
				dz = patch->cz - faces[j].cz;
				nearLimit = patch->radius + faces[j].radius +
					SGTC_GATE_DL_CLUSTER_NEAR_MARGIN;
				if( nearLimit < SGTC_GATE_DL_CLUSTER_NEAR )
					nearLimit = SGTC_GATE_DL_CLUSTER_NEAR;
				if( dx * dx + dy * dy + dz * dz > nearLimit * nearLimit )
					continue;

				found = j;
				break;
			}

			if( found < 0 )
				break;

			{
				const float nf = (float)( patch->faces + 1 );
				float maxRadius = 0.0f;
				float cx, cy, cz;

				cx = ( patch->cx * (float)patch->faces + faces[found].cx ) / nf;
				cy = ( patch->cy * (float)patch->faces + faces[found].cy ) / nf;
				cz = ( patch->cz * (float)patch->faces + faces[found].cz ) / nf;

				patch->faces++;
				patch->cx = cx;
				patch->cy = cy;
				patch->cz = cz;
				used[found] = patchCount + 1;

				for( j = 0; j < count; ++j )
				{
					float dx, dy, dz;
					float r;

					if( used[j] != patchCount + 1 )
						continue;
					dx = faces[j].cx - patch->cx;
					dy = faces[j].cy - patch->cy;
					dz = faces[j].cz - patch->cz;
					r = sqrtf( dx * dx + dy * dy + dz * dz ) + faces[j].radius;
					if( r > maxRadius )
						maxRadius = r;
				}
				patch->radius = maxRadius;
			}
		}

		++patchCount;
	}

	return patchCount;
}

#endif // SGTC_GATE_DLIGHT_LOGIC_H
