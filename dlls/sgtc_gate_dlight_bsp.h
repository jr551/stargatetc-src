// sgtc_gate_dlight_bsp.h - engine-free GoldSrc BSP v30 event-horizon scan.
//
// Parses the subset of a v30 BSP needed to find event horizons by texture
// name: the miptex name table, texinfo, planes, faces, vertex/edge/surfedge
// polygons and the model (brush) ranges.  Names are read from the BSP itself,
// so a horizon whose pixels live in an external WAD is still detected.
//
// No engine headers and no allocation: the caller loads the file and owns the
// buffer; this header reads only bytes covered by validated lump ranges.  It
// is deliberately standalone so tests/features can compile it with the host
// C++ compiler and feed it synthetic or shipped BSPs.
//
// Output:
//   faces   every matching face (world and brush models) in model order;
//           model 0 faces form the leading prefix used for world clustering.
//   brushes one entry per brush model (index >= 1) with matching faces,
//           carrying the model bounds, matched-face count and an averaged
//           normal (bbox thin axis when the face normals cancel).

#ifndef SGTC_GATE_DLIGHT_BSP_H
#define SGTC_GATE_DLIGHT_BSP_H

#include <math.h>

#include "sgtc_gate_dlight_logic.h"

#define SGTC_GATE_DL_BSP_VERSION 30
#define SGTC_GATE_DL_MAX_BRUSHES 64
#define SGTC_GATE_DL_MAX_TEXTURES 4096

// Lump indices in a v30 header.
#define SGTC_BSP_LUMP_ENTITIES 0
#define SGTC_BSP_LUMP_PLANES 1
#define SGTC_BSP_LUMP_TEXTURES 2
#define SGTC_BSP_LUMP_VERTEXES 3
#define SGTC_BSP_LUMP_VISIBILITY 4
#define SGTC_BSP_LUMP_NODES 5
#define SGTC_BSP_LUMP_TEXINFO 6
#define SGTC_BSP_LUMP_FACES 7
#define SGTC_BSP_LUMP_LIGHTING 8
#define SGTC_BSP_LUMP_CLIPNODES 9
#define SGTC_BSP_LUMP_LEAFS 10
#define SGTC_BSP_LUMP_MARKSURFACES 11
#define SGTC_BSP_LUMP_EDGES 12
#define SGTC_BSP_LUMP_SURFEDGES 13
#define SGTC_BSP_LUMP_MODELS 14
#define SGTC_BSP_LUMP_COUNT 15

typedef struct {
	int model;
	float mins[3];
	float maxs[3];
	float nx, ny, nz;
	int faces;
} SGTCHorizonBrush;

typedef struct {
	SGTCHorizonFace faces[SGTC_GATE_DL_MAX_FACES];
	int face_count;
	SGTCHorizonBrush brushes[SGTC_GATE_DL_MAX_BRUSHES];
	int brush_count;
} SGTCBspScan;

// Little-endian readers.  BSP files are little-endian and so is every GoldSrc
// target; bytes are assembled explicitly so unaligned lump offsets are safe.
static inline int SGTC_BspReadInt16( const unsigned char *p )
{
	const int v = (int)p[0] | ( (int)p[1] << 8 );
	return v >= 0x8000 ? v - 0x10000 : v;
}

static inline int SGTC_BspReadUInt16( const unsigned char *p )
{
	return (int)p[0] | ( (int)p[1] << 8 );
}

static inline int SGTC_BspReadInt32( const unsigned char *p )
{
	const unsigned int v = (unsigned int)p[0] | ( (unsigned int)p[1] << 8 ) |
		( (unsigned int)p[2] << 16 ) | ( (unsigned int)p[3] << 24 );
	return (int)v;
}

// Raw little-endian IEEE-754 bytes copied through a character lvalue; no
// aliasing or alignment assumptions.
static inline float SGTC_BspReadFloat( const unsigned char *p )
{
	float f;
	unsigned char *d = (unsigned char *)&f;

	d[0] = p[0];
	d[1] = p[1];
	d[2] = p[2];
	d[3] = p[3];
	return f;
}

static inline int SGTC_BspLumpInRange( long size, int ofs, int len )
{
	if( ofs < 0 || len < 0 )
		return 0;
	if( (long)ofs > size )
		return 0;
	return (long)len <= size - (long)ofs;
}

// Resolve polygon vertex k of a face through surfedges -> edges -> vertexes.
// Returns 0 when any index is out of range.
static inline int SGTC_BspSurfedgeVertex( const unsigned char *base,
		int vertexOfs, int vertexCount, int edgeOfs, int edgeCount,
		int surfedgeOfs, int surfedgeCount, int firstedge, int k,
		float *vx, float *vy, float *vz )
{
	int se;
	int vi;
	unsigned int ei;
	const unsigned char *ep;

	if( !base || k < 0 || firstedge < 0 )
		return 0;
	if( firstedge > surfedgeCount || k >= surfedgeCount - firstedge )
		return 0;

	se = SGTC_BspReadInt32( base + surfedgeOfs + ( firstedge + k ) * 4 );

	// abs() is avoided so INT_MIN stays a huge unsigned index instead of UB.
	ei = (unsigned int)se;
	if( se < 0 )
		ei = 0u - ei;
	if( ei >= (unsigned int)edgeCount )
		return 0;

	ep = base + edgeOfs + (int)ei * 4;
	vi = ( se >= 0 ) ? SGTC_BspReadUInt16( ep ) : SGTC_BspReadUInt16( ep + 2 );
	if( vi < 0 || vi >= vertexCount )
		return 0;

	*vx = SGTC_BspReadFloat( base + vertexOfs + vi * 12 );
	*vy = SGTC_BspReadFloat( base + vertexOfs + vi * 12 + 4 );
	*vz = SGTC_BspReadFloat( base + vertexOfs + vi * 12 + 8 );
	return 1;
}

static inline void SGTC_GateDlightZeroScan( SGTCBspScan *out )
{
	int i, j;

	out->face_count = 0;
	out->brush_count = 0;

	for( i = 0; i < SGTC_GATE_DL_MAX_FACES; ++i )
	{
		out->faces[i].nx = 0.0f;
		out->faces[i].ny = 0.0f;
		out->faces[i].nz = 0.0f;
		out->faces[i].dist = 0.0f;
		out->faces[i].cx = 0.0f;
		out->faces[i].cy = 0.0f;
		out->faces[i].cz = 0.0f;
		out->faces[i].radius = 0.0f;
		out->faces[i].model = 0;
	}

	for( i = 0; i < SGTC_GATE_DL_MAX_BRUSHES; ++i )
	{
		out->brushes[i].model = 0;
		for( j = 0; j < 3; ++j )
		{
			out->brushes[i].mins[j] = 0.0f;
			out->brushes[i].maxs[j] = 0.0f;
		}
		out->brushes[i].nx = 0.0f;
		out->brushes[i].ny = 0.0f;
		out->brushes[i].nz = 0.0f;
		out->brushes[i].faces = 0;
	}
}

// Parse + match.  Returns 1 on a valid v30 BSP, 0 otherwise (outputs zeroed).
// extra is the sgtc_gate_dlight_textures cvar value; the caller owns data.
static inline int SGTC_GateDlightScanBsp( const void *data, long size,
		const char *extra, SGTCBspScan *out )
{
	const unsigned char *base;
	int lumpOfs[SGTC_BSP_LUMP_COUNT];
	int lumpLen[SGTC_BSP_LUMP_COUNT];
	unsigned char texMatch[SGTC_GATE_DL_MAX_TEXTURES];
	int texCount;
	int planeCount, texinfoCount, faceCount, vertexCount, edgeCount, surfedgeCount;
	int numModels;
	int i, m;

	if( !out )
		return 0;

	SGTC_GateDlightZeroScan( out );

	// version + 15 {fileofs, filelen} pairs.
	if( !data || size < 4 + SGTC_BSP_LUMP_COUNT * 8 )
		return 0;

	base = (const unsigned char *)data;

	if( SGTC_BspReadInt32( base ) != SGTC_GATE_DL_BSP_VERSION )
		return 0;

	for( i = 0; i < SGTC_BSP_LUMP_COUNT; ++i )
	{
		lumpOfs[i] = SGTC_BspReadInt32( base + 4 + i * 8 );
		lumpLen[i] = SGTC_BspReadInt32( base + 4 + i * 8 + 4 );
		if( !SGTC_BspLumpInRange( size, lumpOfs[i], lumpLen[i] ) )
			return 0;
	}

	// Textures: dmiptexlump_t { int count; int offset[count]; }.
	if( lumpLen[SGTC_BSP_LUMP_TEXTURES] < 4 )
		return 0;

	texCount = SGTC_BspReadInt32( base + lumpOfs[SGTC_BSP_LUMP_TEXTURES] );
	if( texCount < 0 )
		return 0;
	if( texCount > ( lumpLen[SGTC_BSP_LUMP_TEXTURES] - 4 ) / 4 )
		return 0;

	for( i = 0; i < SGTC_GATE_DL_MAX_TEXTURES; ++i )
		texMatch[i] = 0;

	for( i = 0; i < texCount && i < SGTC_GATE_DL_MAX_TEXTURES; ++i )
	{
		const int lump = lumpOfs[SGTC_BSP_LUMP_TEXTURES];
		const int entryOfs = SGTC_BspReadInt32( base + lump + 4 + i * 4 );
		const unsigned char *entry;
		char name[17];
		int k;

		if( entryOfs < 0 )
			continue;                     // external WAD: no name in the BSP
		if( entryOfs > lumpLen[SGTC_BSP_LUMP_TEXTURES] - 40 )
			continue;                     // truncated entry: no usable name

		entry = base + lump + entryOfs;
		for( k = 0; k < 16; ++k )
			name[k] = (char)entry[k];
		name[16] = 0;

		if( SGTC_GateDlightTextureMatches( name, extra ) )
			texMatch[i] = 1;
	}

	planeCount = lumpLen[SGTC_BSP_LUMP_PLANES] / 20;
	texinfoCount = lumpLen[SGTC_BSP_LUMP_TEXINFO] / 40;
	faceCount = lumpLen[SGTC_BSP_LUMP_FACES] / 20;
	vertexCount = lumpLen[SGTC_BSP_LUMP_VERTEXES] / 12;
	edgeCount = lumpLen[SGTC_BSP_LUMP_EDGES] / 4;
	surfedgeCount = lumpLen[SGTC_BSP_LUMP_SURFEDGES] / 4;
	numModels = lumpLen[SGTC_BSP_LUMP_MODELS] / 64;

	for( m = 0; m < numModels; ++m )
	{
		const unsigned char *mp = base + lumpOfs[SGTC_BSP_LUMP_MODELS] + m * 64;
		float mins[3];
		float maxs[3];
		int firstface;
		int numfaces;
		int matched = 0;
		float sumx = 0.0f;
		float sumy = 0.0f;
		float sumz = 0.0f;
		int fi;

		// dmodel_t: mins @0, maxs @12, origin @24, headnode @36,
		// visleafs @52, firstface @56, numfaces @60.
		mins[0] = SGTC_BspReadFloat( mp + 0 );
		mins[1] = SGTC_BspReadFloat( mp + 4 );
		mins[2] = SGTC_BspReadFloat( mp + 8 );
		maxs[0] = SGTC_BspReadFloat( mp + 12 );
		maxs[1] = SGTC_BspReadFloat( mp + 16 );
		maxs[2] = SGTC_BspReadFloat( mp + 20 );

		firstface = SGTC_BspReadInt32( mp + 56 );
		numfaces = SGTC_BspReadInt32( mp + 60 );

		if( firstface < 0 || numfaces < 0 )
			continue;
		if( firstface > faceCount || numfaces > faceCount - firstface )
			continue;

		for( fi = firstface; fi < firstface + numfaces; ++fi )
		{
			// dface_t: planenum @0, side @2, firstedge @4, numedges @8,
			// texinfo @10.
			const unsigned char *fp = base + lumpOfs[SGTC_BSP_LUMP_FACES] + fi * 20;
			const int planenum = SGTC_BspReadInt16( fp + 0 );
			const int side = SGTC_BspReadInt16( fp + 2 );
			const int firstedge = SGTC_BspReadInt32( fp + 4 );
			const int numedges = SGTC_BspReadInt16( fp + 8 );
			const int texinfo = SGTC_BspReadInt16( fp + 10 );
			float nx, ny, nz, dist;
			float cx, cy, cz;
			float radius;
			int miptex;
			int k;

			if( planenum < 0 || planenum >= planeCount )
				continue;
			if( texinfo < 0 || texinfo >= texinfoCount )
				continue;

			// texinfo_t: miptex index at +32.
			miptex = SGTC_BspReadInt32( base + lumpOfs[SGTC_BSP_LUMP_TEXINFO] +
				texinfo * 40 + 32 );
			if( miptex < 0 || miptex >= SGTC_GATE_DL_MAX_TEXTURES )
				continue;
			if( !texMatch[miptex] )
				continue;

			if( numedges < 3 || firstedge < 0 )
				continue;
			if( firstedge > surfedgeCount || numedges > surfedgeCount - firstedge )
				continue;

			// dplane_t: normal @0, dist @12; a back face flips both.
			{
				const unsigned char *pp = base + lumpOfs[SGTC_BSP_LUMP_PLANES] +
					planenum * 20;

				nx = SGTC_BspReadFloat( pp + 0 );
				ny = SGTC_BspReadFloat( pp + 4 );
				nz = SGTC_BspReadFloat( pp + 8 );
				dist = SGTC_BspReadFloat( pp + 12 );
			}

			if( side != 0 )
			{
				nx = -nx;
				ny = -ny;
				nz = -nz;
				dist = -dist;
			}

			// The engine stores unit normals; normalise defensively and
			// scale dist with them so n . x == dist still holds.
			{
				const float len = sqrtf( nx * nx + ny * ny + nz * nz );

				if( len < 1e-6f )
					continue;
				nx /= len;
				ny /= len;
				nz /= len;
				dist /= len;
			}

			cx = 0.0f;
			cy = 0.0f;
			cz = 0.0f;
			for( k = 0; k < numedges; ++k )
			{
				float vx, vy, vz;

				if( !SGTC_BspSurfedgeVertex( base,
						lumpOfs[SGTC_BSP_LUMP_VERTEXES], vertexCount,
						lumpOfs[SGTC_BSP_LUMP_EDGES], edgeCount,
						lumpOfs[SGTC_BSP_LUMP_SURFEDGES], surfedgeCount,
						firstedge, k, &vx, &vy, &vz ) )
					break;
				cx += vx;
				cy += vy;
				cz += vz;
			}
			if( k < numedges )
				continue;

			cx /= (float)numedges;
			cy /= (float)numedges;
			cz /= (float)numedges;

			radius = 0.0f;
			for( k = 0; k < numedges; ++k )
			{
				float vx, vy, vz;
				float dx, dy, dz;
				float r;

				if( !SGTC_BspSurfedgeVertex( base,
						lumpOfs[SGTC_BSP_LUMP_VERTEXES], vertexCount,
						lumpOfs[SGTC_BSP_LUMP_EDGES], edgeCount,
						lumpOfs[SGTC_BSP_LUMP_SURFEDGES], surfedgeCount,
						firstedge, k, &vx, &vy, &vz ) )
					break;
				dx = vx - cx;
				dy = vy - cy;
				dz = vz - cz;
				r = sqrtf( dx * dx + dy * dy + dz * dz );
				if( r > radius )
					radius = r;
			}
			if( k < numedges )
				continue;

			++matched;
			sumx += nx;
			sumy += ny;
			sumz += nz;

			if( out->face_count < SGTC_GATE_DL_MAX_FACES )
			{
				SGTCHorizonFace *of = &out->faces[out->face_count++];

				of->nx = nx;
				of->ny = ny;
				of->nz = nz;
				of->dist = dist;
				of->cx = cx;
				of->cy = cy;
				of->cz = cz;
				of->radius = radius;
				of->model = m;
			}
		}

		if( m >= 1 && matched > 0 &&
			out->brush_count < SGTC_GATE_DL_MAX_BRUSHES )
		{
			SGTCHorizonBrush *b = &out->brushes[out->brush_count++];
			const float sumLen = sqrtf( sumx * sumx + sumy * sumy + sumz * sumz );

			b->model = m;
			b->mins[0] = mins[0];
			b->mins[1] = mins[1];
			b->mins[2] = mins[2];
			b->maxs[0] = maxs[0];
			b->maxs[1] = maxs[1];
			b->maxs[2] = maxs[2];
			b->faces = matched;

			if( sumLen < 0.1f )
			{
				// Opposing one-sided faces cancel; use the model's thin
				// axis with a positive sign so the placement trace starts
				// from a stable direction.
				const float ex = maxs[0] - mins[0];
				const float ey = maxs[1] - mins[1];
				const float ez = maxs[2] - mins[2];

				if( ex <= ey && ex <= ez )
				{
					b->nx = 1.0f;
					b->ny = 0.0f;
					b->nz = 0.0f;
				}
				else if( ey <= ez )
				{
					b->nx = 0.0f;
					b->ny = 1.0f;
					b->nz = 0.0f;
				}
				else
				{
					b->nx = 0.0f;
					b->ny = 0.0f;
					b->nz = 1.0f;
				}
			}
			else
			{
				b->nx = sumx / sumLen;
				b->ny = sumy / sumLen;
				b->nz = sumz / sumLen;
			}
		}
	}

	return 1;
}

#endif // SGTC_GATE_DLIGHT_BSP_H
