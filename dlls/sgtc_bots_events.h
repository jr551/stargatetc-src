// sgtc_bots_events.h - machine-readable bot telemetry for headless playtests.
//
// Contract: every event is ONE line of the form
//
//   SGTCEVENT {"event":"spawn","map":"ctp_amon","time":12.34,"bot":"name",...}
//
// The four leading keys are always present and always in that order; event
// specific keys follow. Values are JSON-escaped by SGTC_Bots_JsonEscape() so a
// line always parses, whatever the bot name or map name contains. The buffer
// formatter refuses to produce a partial line: it returns -1 on overflow and
// the caller must not print anything then (see SGTC_Bots_FormatEvent).
//
// Plain C: the stub-engine harness (tests/stub_engine.c) dlsyms and mirrors
// these declarations directly.
#pragma once
#ifndef SGTC_BOTS_EVENTS_H
#define SGTC_BOTS_EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

#define SGTC_BOT_EVENT_PREFIX "SGTCEVENT "

// Both UTIL_LogPrintf() and the engine's alert sink format into a fixed 1 KiB
// buffer, so a single event line must stay well below that. The status event
// paginates instead of risking a truncated (unparsable) line.
#define SGTC_BOT_EVENT_LINE_MAX 1000

enum
{
	SGTC_BOT_FIELD_STRING = 0, // quoted + escaped string
	SGTC_BOT_FIELD_INT    = 1, // integer
	SGTC_BOT_FIELD_FLOAT2 = 2, // fixed point, 2 decimals
	SGTC_BOT_FIELD_VEC1   = 3, // [x,y,z], 1 decimal each
	SGTC_BOT_FIELD_RAW    = 4  // pre-rendered JSON produced by this module
};

typedef struct
{
	const char *key;
	int kind;
	const char *sval;
	int ival;
	float fval;
	float vec[3];
} sgtc_bot_event_field_t;

// Per-bot summary used by the "status" event's bots_detail array.
typedef struct
{
	const char *name;
	int team;    // SgtcTeam (1 Torri, 2 Goa'uld), 0 unknown
	int cls;     // SgtcClass 1..5, 0 none
	int health;
	int frags;
	float origin[3];
} sgtc_bot_summary_t;

// Write src into dst as the BODY of a JSON string (no surrounding quotes).
// Quotes, backslashes and anything outside printable ASCII become escapes
// (control bytes and bytes >= 0x7f as \u00XX), so the output is always valid
// JSON. Returns the number of bytes written, excluding the NUL, or -1 when dst
// is too small (dst is left NUL-terminated either way).
int SGTC_Bots_JsonEscape( char *dst, int dst_size, const char *src );

// Format one complete event line (no trailing newline).
// Returns the length written (excluding the NUL) or -1 when the line does not
// fit; on -1 the buffer holds no usable line and must not be printed.
int SGTC_Bots_FormatEvent( char *dst, int dst_size, const char *event,
	const char *map, float time_s, const char *bot,
	const sgtc_bot_event_field_t *fields, int field_count );

// Choose the position a `death` event reports. `snapshot` is the pre-park
// origin captured in CBasePlayer::Killed() (SGTC_Bots_CaptureDeathState);
// `last_alive` is the tracker's previous frame; `current` may already be the
// post-death off-map park (SGTC_OFFMAP_COORD), so it is used only when nothing
// older is known. Returns 1 when a pre-death source was used, 0 for `current`
// only.
int SGTC_Bots_ResolveDeathOrigin( int snapshot_valid, const float *snapshot,
	int last_alive_valid, const float *last_alive, const float *current,
	float out[3] );

// Format the bots_detail array: [{"bot":..,"team":..,...},...]. Returns the
// length written or -1 on overflow.
int SGTC_Bots_FormatBotsDetail( char *dst, int dst_size,
	const sgtc_bot_summary_t *items, int count );

#ifdef __cplusplus
}
#endif

#endif // SGTC_BOTS_EVENTS_H
