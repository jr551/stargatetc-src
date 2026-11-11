// sgtc_bots_events.cpp - the single JSON-lines formatter used by every bot
// telemetry call site. See sgtc_bots_events.h for the wire contract.
//
// No engine calls live here: the stub-engine harness links this translation
// unit's functions directly and asserts the produced lines parse.

#include <stdio.h>
#include <string.h>

#include "sgtc_bots_events.h"

typedef struct
{
	char *buf;
	int size;
	int len;
	int overflow;
} sgtc_json_writer_t;

static void jw_init( sgtc_json_writer_t *w, char *buf, int size )
{
	w->buf = buf;
	w->size = ( size > 0 ) ? size : 0;
	w->len = 0;
	w->overflow = 0;
	if( w->size < 1 )
	{
		w->overflow = 1;
		return;
	}
	w->buf[0] = '\0';
}

static void jw_char( sgtc_json_writer_t *w, char c )
{
	if( w->overflow )
		return;
	if( w->len + 1 >= w->size )
	{
		w->overflow = 1;
		return;
	}
	w->buf[w->len++] = c;
	w->buf[w->len] = '\0';
}

static void jw_raw( sgtc_json_writer_t *w, const char *s )
{
	if( !s )
		s = "";
	while( *s )
		jw_char( w, *s++ );
}

// JSON string value (with quotes). Everything outside printable ASCII is
// escaped, which is what keeps the produced line valid even for a hostile or
// truncated bot name.
static void jw_escaped( sgtc_json_writer_t *w, const char *s )
{
	static const char hex[] = "0123456789abcdef";
	const unsigned char *p;

	jw_char( w, '"' );

	for( p = (const unsigned char *)( s ? s : "" ); *p; p++ )
	{
		unsigned char c = *p;

		switch( c )
		{
		case '"':  jw_raw( w, "\\\"" ); break;
		case '\\': jw_raw( w, "\\\\" ); break;
		case '\b': jw_raw( w, "\\b" ); break;
		case '\f': jw_raw( w, "\\f" ); break;
		case '\n': jw_raw( w, "\\n" ); break;
		case '\r': jw_raw( w, "\\r" ); break;
		case '\t': jw_raw( w, "\\t" ); break;
		default:
			if( c < 0x20 || c >= 0x7f )
			{
				jw_raw( w, "\\u00" );
				jw_char( w, hex[( c >> 4 ) & 0x0f] );
				jw_char( w, hex[c & 0x0f] );
			}
			else
			{
				jw_char( w, (char)c );
			}
			break;
		}
	}

	jw_char( w, '"' );
}

static void jw_int( sgtc_json_writer_t *w, int v )
{
	char tmp[24];

	snprintf( tmp, sizeof( tmp ), "%d", v );
	jw_raw( w, tmp );
}

static void jw_float( sgtc_json_writer_t *w, float v, int decimals )
{
	char fmt[8];
	char tmp[40];

	// NaN / +/-Inf / absurd values would produce invalid JSON tokens.
	if( !( v == v ) || v > 1.0e9f || v < -1.0e9f )
		v = 0.0f;

	snprintf( fmt, sizeof( fmt ), "%%.%df", decimals );
	snprintf( tmp, sizeof( tmp ), fmt, (double)v );
	jw_raw( w, tmp );
}

static void jw_vec1( sgtc_json_writer_t *w, const float vec[3] )
{
	jw_char( w, '[' );
	jw_float( w, vec[0], 1 );
	jw_char( w, ',' );
	jw_float( w, vec[1], 1 );
	jw_char( w, ',' );
	jw_float( w, vec[2], 1 );
	jw_char( w, ']' );
}

int SGTC_Bots_JsonEscape( char *dst, int dst_size, const char *src )
{
	sgtc_json_writer_t w;

	jw_init( &w, dst, dst_size );
	jw_escaped( &w, src );

	if( w.overflow )
	{
		// Leave a valid (if useless) string body behind so callers that ignore
		// the return value cannot emit a broken line.
		if( dst && dst_size > 0 )
			dst[0] = '\0';
		return -1;
	}

	return w.len;
}

int SGTC_Bots_FormatEvent( char *dst, int dst_size, const char *event,
	const char *map, float time_s, const char *bot,
	const sgtc_bot_event_field_t *fields, int field_count )
{
	sgtc_json_writer_t w;
	int i;

	jw_init( &w, dst, dst_size );

	jw_raw( &w, SGTC_BOT_EVENT_PREFIX );
	jw_char( &w, '{' );

	jw_raw( &w, "\"event\":" );
	jw_escaped( &w, event );
	jw_raw( &w, ",\"map\":" );
	jw_escaped( &w, map );
	jw_raw( &w, ",\"time\":" );
	jw_float( &w, time_s, 2 );
	jw_raw( &w, ",\"bot\":" );
	jw_escaped( &w, bot );

	for( i = 0; i < field_count; i++ )
	{
		if( !fields[i].key || !fields[i].key[0] )
			continue;

		jw_char( &w, ',' );
		jw_escaped( &w, fields[i].key );
		jw_char( &w, ':' );

		switch( fields[i].kind )
		{
		case SGTC_BOT_FIELD_INT:
			jw_int( &w, fields[i].ival );
			break;
		case SGTC_BOT_FIELD_FLOAT2:
			jw_float( &w, fields[i].fval, 2 );
			break;
		case SGTC_BOT_FIELD_VEC1:
			jw_vec1( &w, fields[i].vec );
			break;
		case SGTC_BOT_FIELD_RAW:
			jw_raw( &w, fields[i].sval );
			break;
		case SGTC_BOT_FIELD_STRING:
		default:
			jw_escaped( &w, fields[i].sval );
			break;
		}
	}

	jw_char( &w, '}' );

	if( w.overflow )
		return -1;

	return w.len;
}

int SGTC_Bots_ResolveDeathOrigin( int snapshot_valid, const float *snapshot,
	int last_alive_valid, const float *last_alive, const float *current,
	float out[3] )
{
	const float *src = current;
	int pre_death = 0;

	// The tracker runs after the victim's post-think, and SGTC_SgtcThink parks
	// every dead player at SGTC_OFFMAP_COORD. Prefer the CBasePlayer::Killed
	// capture, then the last frame the tracker saw the bot alive; both are
	// inside the world. `current` is only a last resort (e.g. a death that
	// bypassed the capture, where the first observed frame is already dead).
	if( snapshot_valid && snapshot )
	{
		src = snapshot;
		pre_death = 1;
	}
	else if( last_alive_valid && last_alive )
	{
		src = last_alive;
		pre_death = 1;
	}

	if( src )
	{
		out[0] = src[0];
		out[1] = src[1];
		out[2] = src[2];
	}
	else
	{
		out[0] = out[1] = out[2] = 0.0f;
	}

	return pre_death;
}

int SGTC_Bots_FormatBotsDetail( char *dst, int dst_size,
	const sgtc_bot_summary_t *items, int count )
{
	sgtc_json_writer_t w;
	int i;

	jw_init( &w, dst, dst_size );

	jw_char( &w, '[' );

	for( i = 0; i < count; i++ )
	{
		if( i )
			jw_char( &w, ',' );

		jw_char( &w, '{' );
		jw_raw( &w, "\"bot\":" );
		jw_escaped( &w, items[i].name );
		jw_raw( &w, ",\"team\":" );
		jw_int( &w, items[i].team );
		jw_raw( &w, ",\"class\":" );
		jw_int( &w, items[i].cls );
		jw_raw( &w, ",\"health\":" );
		jw_int( &w, items[i].health );
		jw_raw( &w, ",\"frags\":" );
		jw_int( &w, items[i].frags );
		jw_raw( &w, ",\"origin\":" );
		jw_vec1( &w, items[i].origin );
		jw_char( &w, '}' );
	}

	jw_char( &w, ']' );

	if( w.overflow )
		return -1;

	return w.len;
}
