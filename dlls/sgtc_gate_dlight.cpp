// sgtc_gate_dlight.cpp - dynamic blue-white light for open event horizons.
//
// Detection (texture based)
//   Once per map, the driver reads maps/<mapname>.bsp and scans every model's
//   faces for event-horizon textures: horizon, sgeh, eventh, vortex, puddle,
//   wormhole, naquada, plus the comma separated extra substrings in the
//   sgtc_gate_dlight_textures cvar.  Names are read from the BSP miptex lump,
//   so detection is independent of entity classnames, targetnames and of
//   whether the pixels live in an external WAD.  Animated frames (+0horizon)
//   and liquid/scroll markers (!, {, ~, *, #) are normalised before matching.
//
//   Every brush model (*N) with matching faces is a light source, whatever
//   its classname (func_illusionary, func_wall, func_door, ...).  Matching
//   world faces are clustered into coplanar gate-sized patches; each patch is
//   an always-on horizon.  Brush brightness follows the bound entity's
//   renderamt when it has a render mode and is full otherwise; EF_NODRAW or
//   renderamt 0 (with a render mode) switch that brush off.  Coincident
//   one-sided brushes (several maps build one puddle from two models) are
//   merged so the same horizon does not emit two stacked lights.
//
//   A map whose BSP yields no surfaces falls back to the old
//   func_illusionary + kRenderTransTexture + targetname/bbox heuristic.
//
// Light
//   Every 0.1 s, for each surface with radius byte > 0, one TE_DLIGHT
//   (SVC_TEMPENTITY) is emitted from the live bbox/patch centre pushed ~20
//   units off the plane on the more open side.  At most eight messages per
//   tick go to the candidates nearest any client, and nothing is emitted when
//   no client is connected.  Colour is (150,180,255); radius is proportional
//   to the surface half-diagonal (clamped 200..600) and scaled by brightness
//   and a two-sine flicker sampled on the 10 fps horizon animation clock.
//   Life 0.2 s and decay 150 units/s keep consecutive updates overlapping
//   without a visible pulse.
//
// Engine behaviour (FWGS, read-only reference):
//   engine/client/cl_tent.c, TE_DLIGHT: radius = byte*10, life = byte*0.1,
//   decay = byte*10 per second.  ref/common/ref_light.c R_EntityDynamicLight
//   accumulates gp_dlights into alias-model lighting and R_PushDlights/
//   R_MarkLights mark brush surfaces, so one dlight also lights players,
//   monsters and other models - no renderer change is needed.

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "sgtc_gate_dlight_logic.h"
#include "sgtc_gate_dlight_bsp.h"
#include "sgtc_gate_dlight.h"

// Shared cvars for the whole feature.
cvar_t sgtc_cvar_gate_dlight = { "sgtc_gate_dlight", "1", FCVAR_SERVER, 0.0f, NULL };
cvar_t sgtc_cvar_gate_dlight_textures = { "sgtc_gate_dlight_textures", "", FCVAR_SERVER, 0.0f, NULL };

#define SGTC_GATE_DL_TICK 0.1f
#define SGTC_GATE_DL_RESCAN 2.0f
#define SGTC_GATE_DL_EMPTY_SCANS_BEFORE_REMOVE 4
#define SGTC_GATE_DL_PLANE_TRACE 128.0f
#define SGTC_GATE_DL_OFFSET 20.0f
#define SGTC_GATE_DL_EMIT_MAX 8
#define SGTC_GATE_DL_MAX_SURFACES 64
#define SGTC_GATE_DL_MAX_FALLBACK 8
#define SGTC_GATE_DL_MAPNAME_MAX 128
#define SGTC_GATE_DL_MERGE_DIST 24.0f
#define SGTC_GATE_DL_MAX_CLIENTS 32

// One light source discovered from the BSP.  Brush surfaces carry a BSP model
// index and are bound to a live edict every scan; world patches are static.
struct SGTCGateSurface
{
	int model;          // BSP model index (0 for a clustered world patch)
	int world;          // 1 = world patch, no edict
	Vector normal;      // BSP-derived plane normal (placement basis)
	Vector dir;         // normal after choosing the more open side
	Vector scanCenter;  // BSP bbox/patch centre, used to bind the edict
	Vector center;      // live bbox centre or patch centre
	float radius;       // world units, brightness-independent
	float offset;       // push distance along dir
	int index;          // bound edict index (brush only)
	int serial;         // bound edict serial
};

// Legacy fallback keeps the previous edict-based puddle description.
struct SGTCGatePuddle
{
	int index;        // edict index, revalidated every tick
	int serial;       // edict serial, catches index reuse
	Vector center;    // bbox centre (world)
	Vector normal;    // puddle plane normal, points to the more open side
	float radius;     // world units, brightness-independent
	float offset;     // push distance along normal
};

struct SGTCGateCandidate
{
	int radiusByte;
	Vector origin;
	float dist;       // nearest client distance
};

// Parsed BSP and clustered patches are one map each; keep them off the stack.
static SGTCBspScan s_BspScan;
static SGTCHorizonPatch s_BspPatches[SGTC_GATE_DL_MAX_PATCHES];

class CGateDlight : public CBaseEntity
{
public:
	void Spawn( void );
	void DriverThink( void );
	int ObjectCaps( void ) { return CBaseEntity::ObjectCaps() | FCAP_DONT_SAVE; }

private:
	void BuildFromBsp( void );
	void ScanSurfaces( void );
	void ScanFallback( void );
	void EmitSurfaces( float time, const Vector *clients, int clientCount );
	void EmitFallback( float time );
	void EmitPuddle( const SGTCGatePuddle &puddle, float time );
	void EmitMessage( const Vector &origin, int radiusByte );
	int PuddleStillValid( const SGTCGatePuddle &puddle ) const;

	SGTCGateSurface m_Surfaces[SGTC_GATE_DL_MAX_SURFACES];
	int m_iSurfaces;
	SGTCGatePuddle m_Fallback[SGTC_GATE_DL_MAX_FALLBACK];
	int m_iFallback;
	int m_bFallback;
	int m_bBuilt;
	float m_flNextScan;
	int m_iEmptyScans;
};

LINK_ENTITY_TO_CLASS( sgtc_gate_dlight, CGateDlight )

// "foo" -> "maps/foo.bsp"; "foo.bsp" -> "maps/foo.bsp"; a value that already
// carries a path is kept and only gains the extension when missing.
static void SGTC_GateDlightAppend( char *out, int outSize, int &len, const char *text )
{
	while( text && *text && len < outSize - 1 )
		out[len++] = *text++;
	out[len] = 0;
}

static int SGTC_GateDlightEndsWithBsp( const char *s )
{
	const int len = (int)strlen( s );

	if( len < 4 )
		return 0;

	return SGTC_GateDlightLower( s[len-4] ) == '.' &&
		SGTC_GateDlightLower( s[len-3] ) == 'b' &&
		SGTC_GateDlightLower( s[len-2] ) == 's' &&
		SGTC_GateDlightLower( s[len-1] ) == 'p';
}

static void SGTC_GateDlightMapPath( char *out, int outSize )
{
	const char *map = STRING( gpGlobals->mapname );
	char base[96];
	int len = 0;
	int i;

	out[0] = 0;
	if( !map || !*map )
		map = "unknown";

	if( strchr( map, '/' ) || strchr( map, '\\' ) )
	{
		SGTC_GateDlightAppend( out, outSize, len, map );
		if( !SGTC_GateDlightEndsWithBsp( out ) )
			SGTC_GateDlightAppend( out, outSize, len, ".bsp" );
		return;
	}

	// Engine mapname is normally bare; tolerate "foo.bsp" too.
	i = 0;
	while( i < (int)sizeof( base ) - 1 && map[i] )
	{
		base[i] = map[i];
		++i;
	}
	base[i] = 0;
	if( SGTC_GateDlightEndsWithBsp( base ) )
		base[i-4] = 0;

	SGTC_GateDlightAppend( out, outSize, len, "maps/" );
	SGTC_GateDlightAppend( out, outSize, len, base );
	SGTC_GateDlightAppend( out, outSize, len, ".bsp" );
}

// Several shipped maps build one puddle from two coincident one-sided brush
// models (opposite normals).  Merge them, keeping the larger radius; the
// placement trace below chooses the open side either way.
static void SGTC_GateDlightMergeCoincidentBrushes( SGTCGateSurface *surfaces, int &count )
{
	for( int i = 0; i < count; ++i )
	{
		if( surfaces[i].world )
			continue;

		for( int j = i + 1; j < count; )
		{
			if( surfaces[j].world ||
				( surfaces[i].scanCenter - surfaces[j].scanCenter ).Length() >= SGTC_GATE_DL_MERGE_DIST )
			{
				++j;
				continue;
			}

			if( surfaces[j].radius > surfaces[i].radius )
				surfaces[i] = surfaces[j];

			surfaces[j] = surfaces[--count];
		}
	}
}

// Best live edict for a brush model: any classname, preferring a drawn
// instance and, among those, the one nearest the BSP model's centre.
static edict_t *SGTC_GateDlightFindModelEdict( int model, const Vector &scanCenter )
{
	edict_t *best = NULL;
	float bestScore = 0.0f;

	for( int i = 1; i < gpGlobals->maxEntities; ++i )
	{
		edict_t *e = INDEXENT( i );
		float score;

		if( !e || e->free )
			continue;
		if( SGTC_GateDlightBrushModelIndex( STRING( e->v.model ) ) != model )
			continue;

		score = ( ( e->v.absmin + e->v.absmax ) * 0.5f - scanCenter ).Length();
		if( e->v.effects & EF_NODRAW )
			score += 1.0e6f;          // still bind if nothing is drawn

		if( !best || score < bestScore )
		{
			best = e;
			bestScore = score;
		}
	}

	return best;
}

// Choose the light direction and how far to push it off the surface.  Both
// directions are traced; the side with more empty space wins and a tie keeps
// the BSP normal so the result is deterministic.
static void SGTC_GateDlightPlace( edict_t *ignore, const Vector &center,
	const Vector &basis, Vector &dir, float &offset )
{
	TraceResult hitPlus;
	TraceResult hitMinus;

	UTIL_TraceLine( center, center + basis * SGTC_GATE_DL_PLANE_TRACE,
		ignore_monsters, ignore, &hitPlus );
	UTIL_TraceLine( center, center - basis * SGTC_GATE_DL_PLANE_TRACE,
		ignore_monsters, ignore, &hitMinus );

	const float plus = SGTC_GATE_DL_PLANE_TRACE * hitPlus.flFraction;
	const float minus = SGTC_GATE_DL_PLANE_TRACE * hitMinus.flFraction;
	float freeDist;

	if( minus > plus + 0.05f * SGTC_GATE_DL_PLANE_TRACE )
	{
		dir = -basis;
		freeDist = minus;
	}
	else
	{
		dir = basis;
		freeDist = plus;
	}

	if( freeDist <= 0.5f )
		offset = 0.0f;                       // both sides blocked: stay in plane
	else if( freeDist < 48.0f )
		offset = freeDist * 0.4f;
	else
		offset = SGTC_GATE_DL_OFFSET;
}

static int SGTC_GateDlightClientOrigins( Vector *out, int maxOut )
{
	int count = 0;
	int limit = gpGlobals->maxClients;

	// Some engines report 0 here; fall back to the entity array so the light
	// still works, and never index past it.
	if( limit <= 0 || limit >= gpGlobals->maxEntities )
		limit = gpGlobals->maxEntities - 1;

	if( limit > maxOut )
		limit = maxOut;

	for( int i = 1; i <= limit; ++i )
	{
		edict_t *e = INDEXENT( i );

		if( !e || e->free || !( e->v.flags & FL_CLIENT ) )
			continue;

		out[count++] = e->v.origin;
	}

	return count;
}

static float SGTC_GateDlightClientDistance( const Vector &origin,
	const Vector *clients, int clientCount )
{
	float best = 0.0f;

	for( int i = 0; i < clientCount; ++i )
	{
		const float dist = ( origin - clients[i] ).Length();

		if( i == 0 || dist < best )
			best = dist;
	}

	return best;
}

void CGateDlight::BuildFromBsp( void )
{
	char path[SGTC_GATE_DL_MAPNAME_MAX];
	const char *extra = CVAR_GET_STRING( "sgtc_gate_dlight_textures" );
	int length = 0;
	unsigned char *data;
	int valid;
	int i;

	SGTC_GateDlightMapPath( path, (int)sizeof( path ) );

	data = (unsigned char *)LOAD_FILE_FOR_ME( path, &length );
	if( !data || length <= 0 )
	{
		if( data )
			FREE_FILE( data );
		m_bFallback = 1;
		return;
	}

	valid = SGTC_GateDlightScanBsp( data, (long)length, extra, &s_BspScan );
	FREE_FILE( data );

	if( !valid )
	{
		m_bFallback = 1;
		return;
	}

	// Brush models first: one surface per model with matching faces.
	for( i = 0; i < s_BspScan.brush_count && m_iSurfaces < SGTC_GATE_DL_MAX_SURFACES; ++i )
	{
		const SGTCHorizonBrush &b = s_BspScan.brushes[i];
		const Vector mins( b.mins[0], b.mins[1], b.mins[2] );
		const Vector maxs( b.maxs[0], b.maxs[1], b.maxs[2] );
		const Vector size = maxs - mins;
		SGTCGateSurface &s = m_Surfaces[m_iSurfaces++];

		s.model = b.model;
		s.world = 0;
		s.normal = Vector( b.nx, b.ny, b.nz );
		s.dir = s.normal;
		s.scanCenter = ( mins + maxs ) * 0.5f;
		s.center = s.scanCenter;
		s.radius = SGTC_GateDlightRadius( size.Length() * 0.5f );
		s.offset = 0.0f;
		s.index = 0;
		s.serial = 0;
	}

	SGTC_GateDlightMergeCoincidentBrushes( m_Surfaces, m_iSurfaces );

	// Model 0 (world) faces are the leading prefix of the scan output.
	{
		int worldFaces = 0;
		int patchCount;

		while( worldFaces < s_BspScan.face_count && s_BspScan.faces[worldFaces].model == 0 )
			++worldFaces;

		patchCount = SGTC_GateDlightClusterWorldFaces( s_BspScan.faces, worldFaces,
			s_BspPatches, SGTC_GATE_DL_MAX_PATCHES );

		for( i = 0; i < patchCount && m_iSurfaces < SGTC_GATE_DL_MAX_SURFACES; ++i )
		{
			const SGTCHorizonPatch &p = s_BspPatches[i];
			const Vector center( p.cx, p.cy, p.cz );
			SGTCGateSurface &s = m_Surfaces[m_iSurfaces++];

			s.model = 0;
			s.world = 1;
			s.normal = Vector( p.nx, p.ny, p.nz );
			s.dir = s.normal;
			s.scanCenter = center;
			s.center = center;
			s.radius = SGTC_GateDlightRadius( p.radius );
			s.offset = 0.0f;
			s.index = 0;
			s.serial = 0;
		}
	}

	if( m_iSurfaces == 0 )
		m_bFallback = 1;
}

void CGateDlight::ScanSurfaces( void )
{
	for( int i = 0; i < m_iSurfaces; ++i )
	{
		SGTCGateSurface &s = m_Surfaces[i];
		Vector dir;

		if( s.world )
		{
			s.index = 0;
			s.serial = 0;
			SGTC_GateDlightPlace( NULL, s.center, s.normal, dir, s.offset );
			s.dir = dir;
			continue;
		}

		edict_t *e = SGTC_GateDlightFindModelEdict( s.model, s.scanCenter );

		if( !e )
		{
			s.index = 0;
			s.serial = 0;
			s.dir = s.normal;
			s.offset = 0.0f;
			continue;
		}

		s.index = ENTINDEX( e );
		s.serial = e->serialnumber;
		s.center = ( e->v.absmin + e->v.absmax ) * 0.5f;
		SGTC_GateDlightPlace( e, s.center, s.normal, dir, s.offset );
		s.dir = dir;
	}
}

void CGateDlight::ScanFallback( void )
{
	m_iFallback = 0;

	for( int i = 1; i < gpGlobals->maxEntities; ++i )
	{
		edict_t *e = INDEXENT( i );

		if( !e || e->free )
			continue;
		if( !FClassnameIs( &e->v, "func_illusionary" ) )
			continue;
		const char *model = STRING( e->v.model );
		if( !model || model[0] != '*' )
			continue;
		if( e->v.rendermode != kRenderTransTexture )
			continue;
		if( !SGTC_GateDlightNameMatches( STRING( e->v.targetname ) ) )
			continue;

		const Vector size = e->v.absmax - e->v.absmin;
		if( !SGTC_GatePuddleSizeValid( size.x, size.y, size.z ) )
			continue;

		const Vector center = ( e->v.absmin + e->v.absmax ) * 0.5f;
		const int axis = SGTC_GateThinAxis( size.x, size.y, size.z );
		const float radius = SGTC_GateDlightRadius( size.Length() * 0.5f );

		// Gates often carry two coincident one-sided planes (vortex plus
		// vortex_x / vortex_iris / vortex_2b).  One light per physical puddle.
		int merged = -1;
		for( int j = 0; j < m_iFallback; ++j )
		{
			if( ( m_Fallback[j].center - center ).Length() < 24.0f )
			{
				merged = j;
				break;
			}
		}

		if( merged >= 0 )
		{
			if( radius > m_Fallback[merged].radius )
				m_Fallback[merged].radius = radius;
			continue;
		}

		if( m_iFallback >= SGTC_GATE_DL_MAX_FALLBACK )
			continue;

		SGTCGatePuddle &p = m_Fallback[m_iFallback++];
		p.index = i;
		p.serial = e->serialnumber;
		p.center = center;
		p.radius = radius;

		Vector basis( 0.0f, 0.0f, 0.0f );
		if( axis == 0 )
			basis = Vector( 1.0f, 0.0f, 0.0f );
		else if( axis == 1 )
			basis = Vector( 0.0f, 1.0f, 0.0f );
		else
			basis = Vector( 0.0f, 0.0f, 1.0f );

		SGTC_GateDlightPlace( e, center, basis, p.normal, p.offset );
	}
}

int CGateDlight::PuddleStillValid( const SGTCGatePuddle &puddle ) const
{
	edict_t *e = INDEXENT( puddle.index );

	if( !e || e->free || e->serialnumber != puddle.serial )
		return 0;
	if( !FClassnameIs( &e->v, "func_illusionary" ) )
		return 0;
	if( e->v.rendermode != kRenderTransTexture )
		return 0;
	const char *model = STRING( e->v.model );
	if( !model || model[0] != '*' )
		return 0;
	if( !SGTC_GateDlightNameMatches( STRING( e->v.targetname ) ) )
		return 0;

	return 1;
}

void CGateDlight::EmitMessage( const Vector &origin, int radiusByte )
{
	MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, origin );
		WRITE_BYTE( TE_DLIGHT );
		WRITE_COORD( origin.x );
		WRITE_COORD( origin.y );
		WRITE_COORD( origin.z );
		WRITE_BYTE( radiusByte );
		WRITE_BYTE( SGTC_GATE_DLIGHT_COLOR_R );
		WRITE_BYTE( SGTC_GATE_DLIGHT_COLOR_G );
		WRITE_BYTE( SGTC_GATE_DLIGHT_COLOR_B );
		WRITE_BYTE( SGTC_GATE_DLIGHT_LIFE_BYTE );
		WRITE_BYTE( SGTC_GATE_DLIGHT_DECAY_BYTE );
	MESSAGE_END();
}

void CGateDlight::EmitSurfaces( float time, const Vector *clients, int clientCount )
{
	SGTCGateCandidate candidates[SGTC_GATE_DL_MAX_SURFACES];
	int used[SGTC_GATE_DL_MAX_SURFACES];
	int candidateCount = 0;
	int emitted = 0;

	if( clientCount <= 0 )
		return;

	for( int i = 0; i < m_iSurfaces; ++i )
	{
		const SGTCGateSurface &s = m_Surfaces[i];
		float brightness;
		Vector center;

		if( s.world )
		{
			brightness = SGTC_GateDlightBrightness( 255.0f, time );
			center = s.center;
		}
		else
		{
			edict_t *e;

			if( s.index <= 0 )
				continue;
			e = INDEXENT( s.index );
			if( !e || e->free || e->serialnumber != s.serial )
				continue;

			if( e->v.effects & EF_NODRAW )
				brightness = 0.0f;
			else if( e->v.rendermode != kRenderNormal )
				brightness = SGTC_GateDlightBrightness( e->v.renderamt, time );
			else
				brightness = SGTC_GateDlightBrightness( 255.0f, time );

			center = ( e->v.absmin + e->v.absmax ) * 0.5f;
		}

		const int radiusByte = SGTC_GateDlightRadiusByte( s.radius, brightness );

		// Closed (renderamt 0 under a render mode), hidden (EF_NODRAW) and
		// sub-step brightness all produce a zero radius byte: no light.
		if( radiusByte <= 0 )
			continue;

		SGTCGateCandidate &c = candidates[candidateCount];

		c.radiusByte = radiusByte;
		c.origin = center + s.dir * s.offset;
		c.dist = SGTC_GateDlightClientDistance( c.origin, clients, clientCount );
		++candidateCount;
	}

	for( int i = 0; i < candidateCount; ++i )
		used[i] = 0;

	// Emit only the eight nearest candidates; a map can carry many horizons
	// but clients only ever see the closest light sources meaningfully.
	while( emitted < SGTC_GATE_DL_EMIT_MAX && emitted < candidateCount )
	{
		int best = -1;

		for( int i = 0; i < candidateCount; ++i )
		{
			if( used[i] )
				continue;
			if( best < 0 || candidates[i].dist < candidates[best].dist )
				best = i;
		}

		if( best < 0 )
			break;

		used[best] = 1;
		EmitMessage( candidates[best].origin, candidates[best].radiusByte );
		++emitted;
	}
}

void CGateDlight::EmitPuddle( const SGTCGatePuddle &puddle, float time )
{
	edict_t *e = INDEXENT( puddle.index );

	if( !e )
		return;

	const float brightness = SGTC_GateDlightBrightness( e->v.renderamt, time );
	const int radiusByte = SGTC_GateDlightRadiusByte( puddle.radius, brightness );

	if( radiusByte <= 0 )
		return;

	EmitMessage( puddle.center + puddle.normal * puddle.offset, radiusByte );
}

void CGateDlight::EmitFallback( float time )
{
	for( int i = 0; i < m_iFallback; ++i )
	{
		if( !PuddleStillValid( m_Fallback[i] ) )
		{
			// Drop the whole cache next tick rather than trust a stale index.
			m_flNextScan = gpGlobals->time;
			continue;
		}
		EmitPuddle( m_Fallback[i], time );
	}
}

void CGateDlight::DriverThink( void )
{
	const float now = gpGlobals->time;
	Vector clients[SGTC_GATE_DL_MAX_CLIENTS];

	if( CVAR_GET_FLOAT( "sgtc_gate_dlight" ) == 0.0f )
	{
		// Disabled: idle cheaply but keep the entity so a live cvar change
		// starts the lights without a map restart.
		pev->nextthink = now + 0.5f;
		return;
	}

	// The BSP scan is done on the first tick rather than in Spawn so the
	// engine's file layer is fully up when the map file is read.
	if( !m_bBuilt )
	{
		BuildFromBsp();
		m_bBuilt = 1;
	}

	const int clientCount = SGTC_GateDlightClientOrigins( clients, SGTC_GATE_DL_MAX_CLIENTS );

	if( m_bFallback )
	{
		if( now >= m_flNextScan )
		{
			ScanFallback();
			m_flNextScan = now + SGTC_GATE_DL_RESCAN;

			if( m_iFallback == 0 )
			{
				if( ++m_iEmptyScans >= SGTC_GATE_DL_EMPTY_SCANS_BEFORE_REMOVE )
				{
					UTIL_Remove( this );
					return;
				}
			}
			else
			{
				m_iEmptyScans = 0;
			}
		}

		if( clientCount > 0 )
			EmitFallback( now );
	}
	else
	{
		if( now >= m_flNextScan )
		{
			ScanSurfaces();
			m_flNextScan = now + SGTC_GATE_DL_RESCAN;
		}

		EmitSurfaces( now, clients, clientCount );
	}

	pev->nextthink = now + SGTC_GATE_DL_TICK;
}

void CGateDlight::Spawn( void )
{
	pev->classname = MAKE_STRING( "sgtc_gate_dlight" );
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	pev->effects |= EF_NODRAW;

	m_iSurfaces = 0;
	m_iFallback = 0;
	m_bFallback = 0;
	m_bBuilt = 0;
	m_flNextScan = 0.0f;
	m_iEmptyScans = 0;

	SetThink( &CGateDlight::DriverThink );
	pev->nextthink = gpGlobals->time + 0.5f;
}

void SGTC_GateDlightRegisterCvars( void )
{
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_gate_dlight );
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_gate_dlight_textures );
}

// The edict array outlives map changes, so this is a safe identity check:
// a freed edict keeps free == true until it is reused for something else.
static edict_t *s_pGateDlightDriver = NULL;

void SGTC_GateDlightInstall( void )
{
	if( s_pGateDlightDriver && !s_pGateDlightDriver->free &&
		FClassnameIs( &s_pGateDlightDriver->v, "sgtc_gate_dlight" ) )
		return;

	// Fallback for a DLL reload (statics reset): look for a restored driver.
	for( int i = 1; i < gpGlobals->maxEntities; ++i )
	{
		edict_t *e = INDEXENT( i );
		if( e && !e->free && FClassnameIs( &e->v, "sgtc_gate_dlight" ) )
		{
			s_pGateDlightDriver = e;
			return;
		}
	}

	CGateDlight *pDriver = GetClassPtr( (CGateDlight *)NULL );
	if( pDriver )
	{
		pDriver->Spawn();
		s_pGateDlightDriver = ENT( pDriver->pev );
	}
}
