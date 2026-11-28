// Host harness for the HPB_bot waypoint reader (bots/jk_botti/waypoint_hpb.cpp).
//
// Build and run from any scratch directory:
//   g++ -std=c++11 -Wall -Wextra -I<repo>/bots/jk_botti <repo>/tests/host/hpb_waypoint_harness.cpp <repo>/bots/jk_botti/waypoint_hpb.cpp -o hpb_waypoint_harness
//   ./hpb_waypoint_harness                       # synthetic cases only
//   ./hpb_waypoint_harness maps/*.HPB_wpt        # plus every real file given
//
// Prints one `CASE <name> PASS/FAIL` line per check and exits nonzero on any
// failure, like the other tests/host harnesses.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "waypoint_hpb.h"

static int s_checks;
static int s_failures;

static void check( const char *name, int ok, const char *detail = "" )
{
	s_checks++;
	if( !ok )
		s_failures++;
	printf( "CASE %-40s %s %s\n", name, ok ? "PASS" : "FAIL", detail );
}

typedef std::vector<unsigned char> bytes;

static void put32( bytes &b, uint32_t v )
{
	for( int i = 0; i < 4; i++ )
		b.push_back( (unsigned char)( ( v >> ( 8 * i ) ) & 0xff ) );
}

static void putf( bytes &b, float f )
{
	uint32_t u;

	memcpy( &u, &f, 4 );
	put32( b, u );
}

static void put16( bytes &b, int v )
{
	b.push_back( (unsigned char)( v & 0xff ) );
	b.push_back( (unsigned char)( ( v >> 8 ) & 0xff ) );
}

struct node_spec
{
	uint32_t flags;
	float x, y, z;
	std::vector<int> paths;
};

static bytes build( const std::vector<node_spec> &nodes, const char *map = "sg_test",
	int version = 4, const char *magic = "HPB_bot" )
{
	bytes b;
	char name[32] = { 0 };

	for( int i = 0; i < 8; i++ )
		b.push_back( i < 7 ? (unsigned char)magic[i] : 0 );
	put32( b, (uint32_t)version );
	put32( b, 0 );
	put32( b, (uint32_t)nodes.size() );
	strncpy( name, map, 31 );
	for( int i = 0; i < 32; i++ )
		b.push_back( (unsigned char)name[i] );

	for( size_t i = 0; i < nodes.size(); i++ )
	{
		put32( b, nodes[i].flags );
		putf( b, nodes[i].x );
		putf( b, nodes[i].y );
		putf( b, nodes[i].z );
	}

	for( size_t i = 0; i < nodes.size(); i++ )
	{
		put16( b, (int)nodes[i].paths.size() );
		for( size_t k = 0; k < nodes[i].paths.size(); k++ )
			put16( b, nodes[i].paths[k] );
	}

	return b;
}

static node_spec N( uint32_t flags, float x, float y, float z, std::vector<int> paths )
{
	node_spec n;

	n.flags = flags;
	n.x = x;
	n.y = y;
	n.z = z;
	n.paths = paths;
	return n;
}

static int parse( const bytes &b, const char *map, hpb_result_t *r, hpb_node_t *nodes,
	hpb_edge_t *edges, int max_edges = 4096, float max_len = 400.0f, int max_nodes = 1024 )
{
	return HPB_Parse( b.empty() ? NULL : &b[0], (int)b.size(), map, max_nodes, nodes, edges,
		max_edges, max_len, r );
}

static hpb_node_t g_nodes[HPB_WPT_MAX_NODES];
static hpb_edge_t g_edges[32768];

static void synthetic( void )
{
	hpb_result_t r;
	std::vector<node_spec> v;

	// 0 plain, 1 deleted, 2 crouch+ladder, 3 aiming, 4 health+armor+ammo
	v.push_back( N( 0, 0, 0, 0, { 2, 1, 0, 3, 4, 99, -1 } ) );
	v.push_back( N( 0x80000000u, 9999, 9999, 9999, { 0 } ) );
	v.push_back( N( ( 1u << 3 ) | ( 1u << 4 ), 100, 0, 0, { 0, 4 } ) );
	v.push_back( N( 1u << 13, 50, 50, 50, { 0 } ) );
	v.push_back( N( ( 1u << 6 ) | ( 1u << 7 ) | ( 1u << 8 ), 200, 0, 0, { 2, 4, 1000 } ) );

	int rc = parse( build( v ), "sg_test", &r, g_nodes, g_edges );
	check( "valid.rc", rc == 0, HPB_ErrorString( rc ) );
	check( "valid.kept_nodes", r.nodes == 3 );
	check( "valid.dropped_nodes", r.dropped_nodes == 2 );
	check( "valid.mapname", strcmp( r.mapname, "sg_test" ) == 0 );

	// Compacted order: old 0 -> 0, old 2 -> 1, old 4 -> 2.
	check( "valid.node_origin", g_nodes[1].origin[0] == 100.0f && g_nodes[2].origin[0] == 200.0f );
	check( "valid.sem_crouch_ladder", g_nodes[1].sem == ( HPB_SEM_CROUCH | HPB_SEM_LADDER ) );
	check( "valid.sem_items", g_nodes[2].sem == ( HPB_SEM_HEALTH | HPB_SEM_ARMOR | HPB_SEM_AMMO ) );
	check( "valid.sem_plain", g_nodes[0].sem == 0 );

	// Node 0 keeps 2 and 4 (-> 1, 2); 1, 3 (dropped), 0 (self), 99 and -1 (range) drop.
	// Node 2 keeps 0 and 4 (-> 0, 2). Node 4 keeps 2 (-> 1); 4 is a self loop; 1000 out of range.
	// Dropped nodes 1 and 3 each list one path, all of which drop.
	check( "valid.kept_edges", r.edges == 5 );
	// node0: 1,3,0,99,-1 = 5; node1: 1; node2: 0; node3: 1; node4: 4,1000 = 2
	check( "valid.dropped_edges", r.dropped_edges == 9 );

	int has01 = 0, has02 = 0, has10 = 0, has12 = 0, has21 = 0;
	for( int i = 0; i < r.edges; i++ )
	{
		has01 += g_edges[i].from == 0 && g_edges[i].to == 1;
		has02 += g_edges[i].from == 0 && g_edges[i].to == 2;
		has10 += g_edges[i].from == 1 && g_edges[i].to == 0;
		has12 += g_edges[i].from == 1 && g_edges[i].to == 2;
		has21 += g_edges[i].from == 2 && g_edges[i].to == 1;
	}
	check( "valid.edge_set", has01 == 1 && has02 == 1 && has10 == 1 && has12 == 1 && has21 == 1 );

	// Long edges.
	std::vector<node_spec> w;
	w.push_back( N( 0, 0, 0, 0, { 1 } ) );
	w.push_back( N( 0, 1000, 0, 0, { 0 } ) );
	rc = parse( build( w ), "sg_test", &r, g_nodes, g_edges );
	check( "long_edge.rc", rc == 0 );
	check( "long_edge.dropped", r.edges == 0 && r.long_edges == 2 );
	rc = parse( build( w ), "sg_test", &r, g_nodes, g_edges, 4096, 0.0f );
	check( "long_edge.unlimited", rc == 0 && r.edges == 2 && r.long_edges == 0 );

	// Header errors.
	bytes good = build( w );
	bytes b = good;
	b[0] = 'X';
	check( "err.magic", parse( b, "sg_test", &r, g_nodes, g_edges ) == HPB_ERR_MAGIC );
	check( "err.version", parse( build( w, "sg_test", 5 ), "sg_test", &r, g_nodes, g_edges ) == HPB_ERR_VERSION );
	b = good;
	b.resize( 30 );
	check( "err.short", parse( b, "sg_test", &r, g_nodes, g_edges ) == HPB_ERR_SHORT );
	check( "err.empty", parse( bytes(), "sg_test", &r, g_nodes, g_edges ) == HPB_ERR_NULL );
	check( "err.null_nodes", HPB_Parse( &good[0], (int)good.size(), NULL, 1024, NULL, g_edges, 10, 400, &r ) == HPB_ERR_NULL );
	check( "err.mapname", parse( good, "sg_other", &r, g_nodes, g_edges ) == HPB_ERR_MAPNAME );
	check( "ok.mapname_case", parse( good, "SG_TEST", &r, g_nodes, g_edges ) == 0 );
	check( "ok.mapname_unchecked", parse( good, NULL, &r, g_nodes, g_edges ) == 0 );
	check( "ok.mapname_empty_expect", parse( good, "", &r, g_nodes, g_edges ) == 0 );
	check( "ok.header_empty_name", parse( build( w, "" ), "sg_test", &r, g_nodes, g_edges ) == 0 );

	// Count errors.
	b = good;
	b[16] = 0; b[17] = 0; b[18] = 0; b[19] = 0;
	check( "err.count_zero", parse( b, "sg_test", &r, g_nodes, g_edges ) == HPB_ERR_NODE_COUNT );
	b = good;
	b[16] = 0xff; b[17] = 0xff; b[18] = 0xff; b[19] = 0xff;
	check( "err.count_negative", parse( b, "sg_test", &r, g_nodes, g_edges ) == HPB_ERR_NODE_COUNT );
	check( "err.count_over_limit", parse( good, "sg_test", &r, g_nodes, g_edges, 4096, 400, 1 ) == HPB_ERR_NODE_COUNT );
	b = good;
	b[16] = 0x00; b[17] = 0x10; // 4096 nodes
	check( "err.count_huge", parse( b, "sg_test", &r, g_nodes, g_edges ) == HPB_ERR_NODE_COUNT );

	// Truncation.
	b = good;
	b.resize( 52 + 16 + 4 );
	check( "err.trunc_nodes", parse( b, "sg_test", &r, g_nodes, g_edges ) == HPB_ERR_TRUNC_NODES );
	b = good;
	b.resize( b.size() - 1 );
	check( "err.trunc_paths", parse( b, "sg_test", &r, g_nodes, g_edges ) == HPB_ERR_TRUNC_PATHS );
	b = good;
	b.resize( 52 + 32 );
	check( "err.no_path_table", parse( b, "sg_test", &r, g_nodes, g_edges ) == HPB_ERR_TRUNC_PATHS );

	// Path count garbage.
	b = good;
	b[84] = 0xff; b[85] = 0xff;
	check( "err.path_count_negative", parse( b, "sg_test", &r, g_nodes, g_edges ) == HPB_ERR_PATH_COUNT );
	b = good;
	b[84] = 0xff; b[85] = 0x7f;
	check( "err.path_count_huge", parse( b, "sg_test", &r, g_nodes, g_edges ) == HPB_ERR_PATH_COUNT );

	// Coordinates.
	std::vector<node_spec> c;
	c.push_back( N( 0, NAN, 0, 0, {} ) );
	check( "err.coord_nan", parse( build( c ), "sg_test", &r, g_nodes, g_edges ) == HPB_ERR_COORD );
	c[0] = N( 0, 0, INFINITY, 0, {} );
	check( "err.coord_inf", parse( build( c ), "sg_test", &r, g_nodes, g_edges ) == HPB_ERR_COORD );
	c[0] = N( 0, 0, 0, 1e9f, {} );
	check( "err.coord_range", parse( build( c ), "sg_test", &r, g_nodes, g_edges ) == HPB_ERR_COORD );
	c[0] = N( 0x80000000u, NAN, NAN, NAN, {} );
	c.push_back( N( 0, 1, 2, 3, {} ) );
	check( "ok.deleted_node_garbage_coords", parse( build( c ), "sg_test", &r, g_nodes, g_edges ) == 0 && r.nodes == 1 );

	// Edge cap.
	std::vector<node_spec> e;
	e.push_back( N( 0, 0, 0, 0, { 1, 1, 1 } ) );
	e.push_back( N( 0, 10, 0, 0, { 0 } ) );
	check( "err.edge_cap", parse( build( e ), "sg_test", &r, g_nodes, g_edges, 3 ) == HPB_ERR_EDGE_CAP );
	check( "ok.edge_cap_exact", parse( build( e ), "sg_test", &r, g_nodes, g_edges, 4 ) == 0 && r.edges == 4 );

	// Every node dropped: success with an empty graph.
	std::vector<node_spec> d;
	d.push_back( N( 0x80000000u, 0, 0, 0, {} ) );
	check( "ok.all_dropped", parse( build( d ), "sg_test", &r, g_nodes, g_edges ) == 0 && r.nodes == 0 );

	// Trailing bytes are ignored.
	b = good;
	b.push_back( 0xaa );
	b.push_back( 0xbb );
	check( "ok.trailing_bytes", parse( b, "sg_test", &r, g_nodes, g_edges ) == 0 );

	// 1024 nodes is the limit.
	std::vector<node_spec> big;
	for( int i = 0; i < 1024; i++ )
		big.push_back( N( 0, (float)( i * 10 ), 0, 0, { ( i + 1 ) % 1024 } ) );
	// The wrap-around edge (1023 -> 0) is 10230 units long and is dropped.
	check( "ok.max_nodes", parse( build( big ), "sg_test", &r, g_nodes, g_edges ) == 0 && r.nodes == 1024 && r.edges == 1023 && r.long_edges == 1 );
	big.push_back( N( 0, 0, 0, 0, {} ) );
	check( "err.over_max_nodes", parse( build( big ), "sg_test", &r, g_nodes, g_edges ) == HPB_ERR_NODE_COUNT );
}

static int find_root( std::vector<int> &p, int x )
{
	while( p[x] != x )
	{
		p[x] = p[p[x]];
		x = p[x];
	}
	return x;
}

static void real_file( const char *path )
{
	FILE *f = fopen( path, "rb" );
	hpb_result_t r;
	bytes data;
	char name[256];

	if( !f )
	{
		check( path, 0, "cannot open" );
		return;
	}

	unsigned char buf[4096];
	size_t n;

	while( ( n = fread( buf, 1, sizeof( buf ), f ) ) > 0 )
		data.insert( data.end(), buf, buf + n );
	fclose( f );

	int rc = HPB_Parse( &data[0], (int)data.size(), NULL, 1024, g_nodes, g_edges, 32768, 400.0f, &r );

	snprintf( name, sizeof( name ), "real.%s", path );
	if( rc != 0 )
	{
		check( name, 0, HPB_ErrorString( rc ) );
		return;
	}

	// Connectivity: weakly connected components over the kept edges.
	std::vector<int> parent( r.nodes );
	for( int i = 0; i < r.nodes; i++ )
		parent[i] = i;
	for( int i = 0; i < r.edges; i++ )
	{
		int a = find_root( parent, g_edges[i].from );
		int b = find_root( parent, g_edges[i].to );
		if( a != b )
			parent[a] = b;
	}
	std::vector<int> sizes( r.nodes, 0 );
	int largest = 0, comps = 0;
	for( int i = 0; i < r.nodes; i++ )
	{
		int root = find_root( parent, i );
		if( sizes[root]++ == 0 )
			comps++;
	}
	for( int i = 0; i < r.nodes; i++ )
		if( sizes[i] > largest )
			largest = sizes[i];

	char detail[256];
	snprintf( detail, sizeof( detail ), "map=%s nodes=%d edges=%d dropped_n=%d dropped_e=%d long_e=%d comps=%d largest=%d",
		r.mapname, r.nodes, r.edges, r.dropped_nodes, r.dropped_edges, r.long_edges, comps, largest );

	// A usable graph: some nodes, and most of them in one connected piece. Maps
	// whose halves are joined by a gate or a ring come out as several pieces here
	// (the gate links are added at run time), so a third is the bar.
	check( name, r.nodes >= 16 && r.edges >= r.nodes && largest * 3 > r.nodes, detail );
}

int main( int argc, char **argv )
{
	synthetic();

	for( int i = 1; i < argc; i++ )
		real_file( argv[i] );

	printf( "%d checks, %d failures\n", s_checks, s_failures );
	return s_failures ? 1 : 0;
}
