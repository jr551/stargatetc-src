//
// JK_Botti - be more human!
//
// waypoint_hpb.h
//
// SGTC: reader for the authored HPB_bot waypoint files (maps/<map>.HPB_wpt)
// that the map makers shipped next to every payload map. Those graphs were laid
// out by people walking the maps, and they carry 55-468 nodes per map where the
// jk_botti file auto-generated from spawn points holds about ten. This module
// only parses and sanity-checks; waypoint.cpp turns the result into jk_botti
// waypoints and paths.
//
// File layout (little endian):
//   header   char filetype[8] = "HPB_bot\0", int version (4), int flags,
//            int number_of_waypoints, char mapname[32]            (52 bytes)
//   nodes    number_of_waypoints x { int flags; float origin[3] } (16 bytes)
//   paths    for every node: short count, then count x short index
//
// Engine free on purpose: tests/host/hpb_waypoint_harness.cpp compiles this
// file on its own.

#ifndef WAYPOINT_HPB_H
#define WAYPOINT_HPB_H

#define HPB_WPT_MAX_NODES 1024

// Semantic flags handed back per node (the caller maps them to its own bits).
#define HPB_SEM_CROUCH (1u << 0)
#define HPB_SEM_LADDER (1u << 1)
#define HPB_SEM_HEALTH (1u << 2)
#define HPB_SEM_ARMOR  (1u << 3)
#define HPB_SEM_AMMO   (1u << 4)

// Error codes from HPB_Parse (0 is success).
#define HPB_ERR_NULL        -1   // null buffer or output
#define HPB_ERR_SHORT       -2   // smaller than the header
#define HPB_ERR_MAGIC       -3   // not an HPB_bot file
#define HPB_ERR_VERSION     -4   // version is not 4
#define HPB_ERR_NODE_COUNT  -5   // zero, negative or above the caller's limit
#define HPB_ERR_TRUNC_NODES -6   // the node table runs past the end
#define HPB_ERR_TRUNC_PATHS -7   // a path table runs past the end
#define HPB_ERR_COORD       -8   // a kept node has a non-finite or absurd origin
#define HPB_ERR_EDGE_CAP    -9   // more edges than the caller's buffer holds
#define HPB_ERR_PATH_COUNT  -10  // a per-node path count is out of range
#define HPB_ERR_MAPNAME     -11  // header names a different map

typedef struct
{
	float origin[3];
	unsigned int sem;     // HPB_SEM_* bits
} hpb_node_t;

typedef struct
{
	short from;           // indexes into the compacted node array
	short to;
} hpb_edge_t;

typedef struct
{
	int nodes;            // kept nodes (deleted and aiming nodes are dropped)
	int edges;            // kept edges (compacted indexes, duplicates not removed)
	int dropped_nodes;    // deleted or aiming
	int dropped_edges;    // self loop, out of range, or touching a dropped node
	int long_edges;       // longer than max_edge_len, dropped
	char mapname[33];     // header map name, always terminated
} hpb_result_t;

// Parse `size` bytes at `data`. `expect_map` (may be NULL or empty) is compared
// case-insensitively with the header map name when that name is not empty.
// `max_edge_len` drops edges longer than that many units (<= 0 keeps them all).
// Returns 0 or an HPB_ERR_* code; `out` is filled either way as far as it got.
int HPB_Parse( const unsigned char *data, int size, const char *expect_map,
	int max_nodes, hpb_node_t *nodes, hpb_edge_t *edges, int max_edges,
	float max_edge_len, hpb_result_t *out );

const char *HPB_ErrorString( int code );

#endif // WAYPOINT_HPB_H
