//
// JK_Botti - be more human!
//
// bot_learn_core.h
//
// SGTC: what the bots learn from the people playing about the map. Small and
// engine free (tests/host/bot_learn_harness.cpp compiles bot_learn_core.cpp
// alone); bot_learn.cpp does the engine side.
//
// A per-map count of where people spend time and where they fight, in coarse
// cells. A bot with nothing to do walks toward the places people actually use
// instead of a random node, and the count is saved per map so it is still
// there next session. (How hard the bots are is the director's job, see
// dlls/sgtc_director.cpp: it rates each person and steers bot skill.)

#ifndef BOT_LEARN_CORE_H
#define BOT_LEARN_CORE_H

// ---------------------------------------------------------------------------
// heat map
// ---------------------------------------------------------------------------

#define HEAT_MAX_CELLS 1536
#define HEAT_CELL_SIZE 160.0f
#define HEAT_CELL_CAP 5000.0f
#define HEAT_HASH_SIZE 4096     // power of two, at least 2 x HEAT_MAX_CELLS

typedef struct
{
	int x, y, z;
	float w;
} heat_cell_t;

typedef struct
{
	heat_cell_t cells[HEAT_MAX_CELLS];
	short table[HEAT_HASH_SIZE];   // cell index + 1, 0 = empty
	int count;
	int dirty;                     // changed since the last save
} heat_map_t;

void Heat_Clear( heat_map_t *h );

// Add `w` to the cell holding the point. A full map replaces its weakest cell
// when the new weight is larger.
void Heat_Add( heat_map_t *h, float x, float y, float z, float w );

// Weight around a point: its own cell plus a quarter of each of the six
// neighbouring cells, so a waypoint just across a cell border still feels the
// traffic next to it.
float Heat_At( const heat_map_t *h, float x, float y, float z );

// Multiply every cell by `factor` and drop those that fall below `floor`.
void Heat_Decay( heat_map_t *h, float factor, float floor );

// Text form, newline separated ("sgtc_heat 1", then "c x y z weight" lines).
// Returns the bytes written (no terminator counted) or -1 if `size` is too
// small.
int Heat_Serialize( const heat_map_t *h, char *buf, int size );

// Replace the map with the cells in `text`. Returns the number loaded, or -1
// when the header is wrong (the map is then left empty). Bad lines are skipped.
int Heat_Parse( heat_map_t *h, const char *text );

// Weighted choice from `n` non-negative weights; `rnd01` in [0, 1). Returns -1
// when every weight is zero or n is not positive.
int Heat_PickIndex( const float *weights, int n, float rnd01 );

#endif // BOT_LEARN_CORE_H
