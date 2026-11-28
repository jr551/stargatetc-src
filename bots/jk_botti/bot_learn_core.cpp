//
// JK_Botti - be more human!
//
// bot_learn_core.cpp
//
// SGTC: the traffic heat map, see bot_learn_core.h. No engine headers.
//

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bot_learn_core.h"

// ---------------------------------------------------------------------------
// heat map
// ---------------------------------------------------------------------------

static unsigned int heat_hash( int x, int y, int z )
{
	unsigned int h = (unsigned int)x * 73856093u ^ (unsigned int)y * 19349663u ^
		(unsigned int)z * 83492791u;

	h ^= h >> 15;
	return h & ( HEAT_HASH_SIZE - 1 );
}

static int heat_cell_coord( float v )
{
	return (int)floorf( v / HEAT_CELL_SIZE );
}

static int heat_find( const heat_map_t *h, int x, int y, int z )
{
	unsigned int slot = heat_hash( x, y, z );

	for( int probes = 0; probes < HEAT_HASH_SIZE; probes++ )
	{
		const int entry = h->table[slot];

		if( entry == 0 )
			return -1;

		const heat_cell_t &c = h->cells[entry - 1];

		if( c.x == x && c.y == y && c.z == z )
			return entry - 1;

		slot = ( slot + 1 ) & ( HEAT_HASH_SIZE - 1 );
	}

	return -1;
}

static void heat_table_insert( heat_map_t *h, int index )
{
	const heat_cell_t &c = h->cells[index];
	unsigned int slot = heat_hash( c.x, c.y, c.z );

	while( h->table[slot] != 0 )
		slot = ( slot + 1 ) & ( HEAT_HASH_SIZE - 1 );

	h->table[slot] = (short)( index + 1 );
}

static void heat_rebuild_table( heat_map_t *h )
{
	memset( h->table, 0, sizeof( h->table ) );

	for( int i = 0; i < h->count; i++ )
		heat_table_insert( h, i );
}

void Heat_Clear( heat_map_t *h )
{
	memset( h, 0, sizeof( *h ) );
}

void Heat_Add( heat_map_t *h, float x, float y, float z, float w )
{
	if( !( w > 0.0f ) || !( fabsf( x ) < 1.0e6f && fabsf( y ) < 1.0e6f && fabsf( z ) < 1.0e6f ) )
		return;

	const int cx = heat_cell_coord( x );
	const int cy = heat_cell_coord( y );
	const int cz = heat_cell_coord( z );
	const int found = heat_find( h, cx, cy, cz );

	h->dirty = 1;

	if( found >= 0 )
	{
		h->cells[found].w += w;
		if( h->cells[found].w > HEAT_CELL_CAP )
			h->cells[found].w = HEAT_CELL_CAP;
		return;
	}

	if( h->count < HEAT_MAX_CELLS )
	{
		heat_cell_t &c = h->cells[h->count];

		c.x = cx;
		c.y = cy;
		c.z = cz;
		c.w = w > HEAT_CELL_CAP ? HEAT_CELL_CAP : w;
		heat_table_insert( h, h->count );
		h->count++;
		return;
	}

	// Full: the new cell takes the place of the weakest one if it is worth more.
	int weakest = 0;

	for( int i = 1; i < h->count; i++ )
		if( h->cells[i].w < h->cells[weakest].w )
			weakest = i;

	if( w > h->cells[weakest].w )
	{
		h->cells[weakest].x = cx;
		h->cells[weakest].y = cy;
		h->cells[weakest].z = cz;
		h->cells[weakest].w = w > HEAT_CELL_CAP ? HEAT_CELL_CAP : w;
		heat_rebuild_table( h );
	}
}

float Heat_At( const heat_map_t *h, float x, float y, float z )
{
	static const int dx[6] = { 1, -1, 0, 0, 0, 0 };
	static const int dy[6] = { 0, 0, 1, -1, 0, 0 };
	static const int dz[6] = { 0, 0, 0, 0, 1, -1 };
	const int cx = heat_cell_coord( x );
	const int cy = heat_cell_coord( y );
	const int cz = heat_cell_coord( z );
	float sum = 0.0f;
	int i = heat_find( h, cx, cy, cz );

	if( i >= 0 )
		sum += h->cells[i].w;

	for( int k = 0; k < 6; k++ )
	{
		i = heat_find( h, cx + dx[k], cy + dy[k], cz + dz[k] );
		if( i >= 0 )
			sum += 0.25f * h->cells[i].w;
	}

	return sum;
}

void Heat_Decay( heat_map_t *h, float factor, float floor )
{
	int out = 0;

	for( int i = 0; i < h->count; i++ )
	{
		heat_cell_t c = h->cells[i];

		c.w *= factor;

		if( c.w >= floor )
			h->cells[out++] = c;
	}

	if( out != h->count )
		h->dirty = 1;

	h->count = out;
	heat_rebuild_table( h );
}

int Heat_Serialize( const heat_map_t *h, char *buf, int size )
{
	int used = 0;
	int n;

	n = snprintf( buf + used, (size_t)( size - used ), "sgtc_heat 1\n" );
	if( n < 0 || n >= size - used )
		return -1;
	used += n;

	for( int i = 0; i < h->count; i++ )
	{
		n = snprintf( buf + used, (size_t)( size - used ), "c %d %d %d %.2f\n",
			h->cells[i].x, h->cells[i].y, h->cells[i].z, h->cells[i].w );
		if( n < 0 || n >= size - used )
			return -1;
		used += n;
	}

	return used;
}

int Heat_Parse( heat_map_t *h, const char *text )
{
	Heat_Clear( h );

	if( !text || strncmp( text, "sgtc_heat 1", 11 ) != 0 )
		return -1;

	const char *line = strchr( text, '\n' );

	if( !line )
		return 0;

	line++;

	while( *line )
	{
		int x, y, z;
		float w;

		if( line[0] == 'c' && line[1] == ' ' && sscanf( line + 2, "%d %d %d %f", &x, &y, &z, &w ) == 4 &&
			x > -100000 && x < 100000 && y > -100000 && y < 100000 && z > -100000 && z < 100000 &&
			w > 0.0f && w <= HEAT_CELL_CAP * 2.0f && h->count < HEAT_MAX_CELLS &&
			heat_find( h, x, y, z ) < 0 )
		{
			heat_cell_t &c = h->cells[h->count];

			c.x = x;
			c.y = y;
			c.z = z;
			c.w = w > HEAT_CELL_CAP ? HEAT_CELL_CAP : w;
			heat_table_insert( h, h->count );
			h->count++;
		}

		const char *nl = strchr( line, '\n' );

		if( !nl )
			break;

		line = nl + 1;
	}

	h->dirty = 0;
	return h->count;
}

int Heat_PickIndex( const float *weights, int n, float rnd01 )
{
	float total = 0.0f;

	if( !weights || n <= 0 )
		return -1;

	for( int i = 0; i < n; i++ )
		if( weights[i] > 0.0f )
			total += weights[i];

	if( !( total > 0.0f ) )
		return -1;

	float target = rnd01 * total;

	if( target < 0.0f )
		target = 0.0f;

	int last = -1;

	for( int i = 0; i < n; i++ )
	{
		if( !( weights[i] > 0.0f ) )
			continue;

		last = i;

		if( target < weights[i] )
			return i;

		target -= weights[i];
	}

	return last;
}
