//
// JK_Botti - be more human!
//
// waypoint_hpb.cpp
//
// SGTC: HPB_bot waypoint file reader, see waypoint_hpb.h. No engine headers.
//

#include <math.h>
#include <string.h>

#include "waypoint_hpb.h"

#define HPB_HDR_SIZE   52
#define HPB_NODE_SIZE  16
#define HPB_MAX_PATHS_PER_NODE 4096
#define HPB_COORD_LIMIT 65536.0f

// HPB_bot flag bits (bot_template waypoint.h).
#define HPB_FL_CROUCH  (1u << 3)
#define HPB_FL_LADDER  (1u << 4)
#define HPB_FL_HEALTH  (1u << 6)
#define HPB_FL_ARMOR   (1u << 7)
#define HPB_FL_AMMO    (1u << 8)
#define HPB_FL_AIMING  (1u << 13)
#define HPB_FL_DELETED (1u << 31)

static unsigned int hpb_u32( const unsigned char *p )
{
	return (unsigned int)p[0] | ( (unsigned int)p[1] << 8 ) |
		( (unsigned int)p[2] << 16 ) | ( (unsigned int)p[3] << 24 );
}

static int hpb_i32( const unsigned char *p )
{
	return (int)hpb_u32( p );
}

static int hpb_i16( const unsigned char *p )
{
	return (short)( (unsigned int)p[0] | ( (unsigned int)p[1] << 8 ) );
}

static float hpb_f32( const unsigned char *p )
{
	const unsigned int u = hpb_u32( p );
	float f;

	memcpy( &f, &u, sizeof( f ) );
	return f;
}

static int hpb_lower( int c )
{
	return ( c >= 'A' && c <= 'Z' ) ? c + ( 'a' - 'A' ) : c;
}

static int hpb_names_match( const char *a, const char *b )
{
	while( *a && *b )
	{
		if( hpb_lower( (unsigned char)*a ) != hpb_lower( (unsigned char)*b ) )
			return 0;
		a++;
		b++;
	}

	return *a == *b;
}

const char *HPB_ErrorString( int code )
{
	switch( code )
	{
	case 0: return "ok";
	case HPB_ERR_NULL: return "null buffer";
	case HPB_ERR_SHORT: return "file shorter than the header";
	case HPB_ERR_MAGIC: return "not an HPB_bot waypoint file";
	case HPB_ERR_VERSION: return "unsupported HPB waypoint version";
	case HPB_ERR_NODE_COUNT: return "bad waypoint count";
	case HPB_ERR_TRUNC_NODES: return "waypoint table is truncated";
	case HPB_ERR_TRUNC_PATHS: return "path table is truncated";
	case HPB_ERR_COORD: return "waypoint origin is not a sane position";
	case HPB_ERR_EDGE_CAP: return "too many paths for the edge buffer";
	case HPB_ERR_PATH_COUNT: return "bad path count";
	case HPB_ERR_MAPNAME: return "waypoint file is for another map";
	default: return "unknown error";
	}
}

int HPB_Parse( const unsigned char *data, int size, const char *expect_map,
	int max_nodes, hpb_node_t *nodes, hpb_edge_t *edges, int max_edges,
	float max_edge_len, hpb_result_t *out )
{
	int old2new[HPB_WPT_MAX_NODES];
	int count;
	int kept = 0;
	int pos;

	if( out )
		memset( out, 0, sizeof( *out ) );

	if( !data || !nodes || !edges || !out )
		return HPB_ERR_NULL;

	if( size < HPB_HDR_SIZE )
		return HPB_ERR_SHORT;

	if( memcmp( data, "HPB_bot", 8 ) != 0 )
		return HPB_ERR_MAGIC;

	if( hpb_i32( data + 8 ) != 4 )
		return HPB_ERR_VERSION;

	memcpy( out->mapname, data + 20, 32 );
	out->mapname[32] = '\0';

	if( expect_map && expect_map[0] && out->mapname[0] &&
		!hpb_names_match( out->mapname, expect_map ) )
		return HPB_ERR_MAPNAME;

	count = hpb_i32( data + 16 );

	if( max_nodes > HPB_WPT_MAX_NODES )
		max_nodes = HPB_WPT_MAX_NODES;

	if( count <= 0 || count > max_nodes )
		return HPB_ERR_NODE_COUNT;

	if( size - HPB_HDR_SIZE < count * HPB_NODE_SIZE )
		return HPB_ERR_TRUNC_NODES;

	for( int i = 0; i < count; i++ )
	{
		const unsigned char *rec = data + HPB_HDR_SIZE + i * HPB_NODE_SIZE;
		const unsigned int flags = hpb_u32( rec );

		old2new[i] = -1;

		// Deleted slots keep stale coordinates; aiming nodes are look-at
		// hints, not places to walk to.
		if( flags & ( HPB_FL_DELETED | HPB_FL_AIMING ) )
		{
			out->dropped_nodes++;
			continue;
		}

		float x = hpb_f32( rec + 4 );
		float y = hpb_f32( rec + 8 );
		float z = hpb_f32( rec + 12 );

		if( !( fabsf( x ) <= HPB_COORD_LIMIT && fabsf( y ) <= HPB_COORD_LIMIT &&
			fabsf( z ) <= HPB_COORD_LIMIT ) )
			return HPB_ERR_COORD;

		nodes[kept].origin[0] = x;
		nodes[kept].origin[1] = y;
		nodes[kept].origin[2] = z;
		nodes[kept].sem = 0;

		if( flags & HPB_FL_CROUCH ) nodes[kept].sem |= HPB_SEM_CROUCH;
		if( flags & HPB_FL_LADDER ) nodes[kept].sem |= HPB_SEM_LADDER;
		if( flags & HPB_FL_HEALTH ) nodes[kept].sem |= HPB_SEM_HEALTH;
		if( flags & HPB_FL_ARMOR ) nodes[kept].sem |= HPB_SEM_ARMOR;
		if( flags & HPB_FL_AMMO ) nodes[kept].sem |= HPB_SEM_AMMO;

		old2new[i] = kept++;
	}

	out->nodes = kept;
	pos = HPB_HDR_SIZE + count * HPB_NODE_SIZE;

	for( int i = 0; i < count; i++ )
	{
		int paths;

		if( size - pos < 2 )
			return HPB_ERR_TRUNC_PATHS;

		paths = hpb_i16( data + pos );
		pos += 2;

		if( paths < 0 || paths > HPB_MAX_PATHS_PER_NODE )
			return HPB_ERR_PATH_COUNT;

		if( ( size - pos ) / 2 < paths )
			return HPB_ERR_TRUNC_PATHS;

		for( int k = 0; k < paths; k++ )
		{
			const int to_old = hpb_i16( data + pos + k * 2 );

			if( old2new[i] < 0 || to_old < 0 || to_old >= count ||
				to_old == i || old2new[to_old] < 0 )
			{
				out->dropped_edges++;
				continue;
			}

			const hpb_node_t &a = nodes[old2new[i]];
			const hpb_node_t &b = nodes[old2new[to_old]];
			const float dx = a.origin[0] - b.origin[0];
			const float dy = a.origin[1] - b.origin[1];
			const float dz = a.origin[2] - b.origin[2];

			if( max_edge_len > 0.0f &&
				dx * dx + dy * dy + dz * dz > max_edge_len * max_edge_len )
			{
				out->long_edges++;
				continue;
			}

			if( out->edges >= max_edges )
				return HPB_ERR_EDGE_CAP;

			edges[out->edges].from = (short)old2new[i];
			edges[out->edges].to = (short)old2new[to_old];
			out->edges++;
		}

		pos += paths * 2;
	}

	return 0;
}
