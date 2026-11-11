// sgtc_director_core.h - engine-free half of the bot director.
//
// Everything declared here is a pure function over plain integers, floats and
// fixed-size buffers: no engine headers, no globals, no allocation, no I/O.
// dlls/sgtc_director.cpp gathers live game state and calls into this module;
// tests/test_director_core.py compiles it on the host and exercises it
// directly (see tests/host/director_core_harness.cpp).
//
// Money rules encoded here and nowhere else:
//   * a transcript never exceeds SGTC_DIR_TRANSCRIPT_MAX bytes: the oldest
//     records are dropped first, the newest are always kept;
//   * every value that comes back from the model is clamped to the documented
//     range, and unknown object keys are rejected instead of guessed at;
//   * nothing returned by the model is ever interpreted as a command.
#pragma once
#ifndef SGTC_DIRECTOR_CORE_H
#define SGTC_DIRECTOR_CORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SGTC_DIR_MAX_BOTS 32
#define SGTC_DIR_MAX_PLAYERS 32
#define SGTC_DIR_NAME_MAX 33            /* sanitised display name, NUL included */
#define SGTC_DIR_WEAPONS_PER_BOT 3
#define SGTC_DIR_WEAPON_LEN 24
#define SGTC_DIR_CHAT_LINES_PER_BOT 2
#define SGTC_DIR_CHAT_LINE_MAX 101      /* <= 100 characters + NUL */
#define SGTC_DIR_RATIONALE_MAX 201

#define SGTC_DIR_TRANSCRIPT_MAX 6144    /* hard cap for the per-interval data */
#define SGTC_DIR_SEG_MAX 96             /* transcript records kept in total */
#define SGTC_DIR_SEG_LEN 640            /* one transcript record, NUL included */
#define SGTC_DIR_ERR_MAX 128

#define SGTC_DIR_RLE_LEGS 24

#define SGTC_DIR_ELO_SCALE 1000
#define SGTC_DIR_ELO_DEFAULT 1500       /* a new human starts here */
#define SGTC_DIR_ELO_K 32

/* Target band for a human's predicted duel win chance, in percent. The local
 * controller steers bot skill so a human's estimate stays inside it. */
#define SGTC_DIR_WIN_BAND_LOW 45
#define SGTC_DIR_WIN_BAND_HIGH 55

/* Scaling used by the intensity/boredom integrators: 1000 == 1.0. */
#define SGTC_DIR_ONE 1000

/* --------------------------------------------------------------------- */
/* small helpers                                                          */
/* --------------------------------------------------------------------- */

int SGTC_Dir_ClampInt( int v, int lo, int hi );
float SGTC_Dir_ClampFloat( float v, float lo, float hi );

// Sanitise untrusted player text: control bytes (including CR/LF and DEL) and
// bytes >= 0x7f become spaces, runs of whitespace collapse to one space, and
// leading/trailing spaces are stripped. Writes at most cap-1 bytes plus NUL.
// Returns the resulting length, or -1 when dst is NULL or cap <= 0.
int SGTC_Dir_SanitizeChat( char *dst, int cap, const char *src );

// Copy at most SGTC_DIR_NAME_MAX-1 bytes of a player name, sanitised the same
// way. Returns the length, or -1 on bad arguments.
int SGTC_Dir_SanitizeName( char *dst, int cap, const char *src );

/* --------------------------------------------------------------------- */
/* run-length movement encoding: legs of "area id, repetitions"           */
/* --------------------------------------------------------------------- */

typedef struct
{
	int area[SGTC_DIR_RLE_LEGS];   // area id, 1..9999 (0 = unknown)
	int count[SGTC_DIR_RLE_LEGS];  // repetitions, >= 1
	int n;                         // number of valid legs
} sgtc_dir_rle_t;

void SGTC_Dir_RleInit( sgtc_dir_rle_t *r );

// Append one sample. Consecutive equal area ids merge into the last leg; when
// the leg list is full the OLDEST leg is dropped (the newest movement always
// survives). Returns 0 on success, -1 on bad arguments.
int SGTC_Dir_RlePush( sgtc_dir_rle_t *r, int area );

// Render as "A3x4,A7x2,G1" (short area labels, optional "x<count>"). Area ids
// map to labels A..Z then AA, AB, ... so the wire form stays short.
// Returns the length written, or -1 when dst/cap are invalid or the text does
// not fit (dst is always NUL-terminated when cap > 0).
int SGTC_Dir_RleFormat( const sgtc_dir_rle_t *r, char *dst, int cap );

// Short label for an area id (1..9999): "A", "B", ..., "Z", "AA", "AB", ...
// Returns 0 on success, -1 on bad arguments.
int SGTC_Dir_AreaLabel( int area, char *dst, int cap );

/* --------------------------------------------------------------------- */
/* transcript document with a hard size cap                               */
/* --------------------------------------------------------------------- */

typedef struct
{
	char seg[SGTC_DIR_SEG_MAX][SGTC_DIR_SEG_LEN];
	int seg_len[SGTC_DIR_SEG_MAX];
	int count;
	int total;   // sum of seg_len
} sgtc_dir_doc_t;

void SGTC_Dir_DocInit( sgtc_dir_doc_t *doc );

// Append one record. Records are whole lines/fields that must survive or be
// dropped intact (never truncated in the middle). While total would exceed
// SGTC_DIR_TRANSCRIPT_MAX the oldest record is dropped. A record longer than
// SGTC_DIR_SEG_LEN-1 is truncated on a word boundary if possible.
// Returns the number of bytes stored, or -1 on bad arguments.
int SGTC_Dir_DocAdd( sgtc_dir_doc_t *doc, const char *text );

int SGTC_Dir_DocLength( const sgtc_dir_doc_t *doc );

// Render all records in order into dst. Returns bytes written, or -1 when the
// text does not fit in cap (dst NUL-terminated when cap > 0).
int SGTC_Dir_DocRender( const sgtc_dir_doc_t *doc, char *dst, int cap );

/* --------------------------------------------------------------------- */
/* engagement model (deterministic integer maths)                         */
/* --------------------------------------------------------------------- */

typedef struct
{
	int rating;            // Elo rating, starts at SGTC_DIR_ELO_DEFAULT
	int intensity;         // x1000, clamped 0..3000
	int boredom;           // x1000, clamped 0..5000
	int last_area;         // area id of the last sample, 0 = none
	float last_fight;      // gpGlobals->time of the last damage exchanged
	float area_since;      // time the player entered last_area
	float last_sample;     // time of the previous tick
	int damage_dealt;      // accumulated since the last interval flush
	int damage_taken;
	int iter_since_flush;  // number of ticks folded into the accumulators
} sgtc_dir_human_t;

void SGTC_Dir_HumanInit( sgtc_dir_human_t *h, float now );

// Expected score of a (x1000) given ratings; 500 == even. Pure, no clamping of
// the ratings themselves.
int SGTC_Dir_EloExpected( int rating_a, int rating_b );

// New rating for `rating` after a duel against `opponent`; won != 0 means the
// rated player won. Uses SGTC_DIR_ELO_K and the logistic curve.
int SGTC_Dir_EloUpdate( int rating, int opponent, int won );

// Advance one sample. `dmg_dealt`/`dmg_taken` are events since the previous
// call (0 when nothing happened); `near_miss` is a 0/1 count of shots that
// missed a human by a small margin (approximated by the caller).
// intensity rises on damage and decays with a ~8 s half-life;
// boredom rises while no fight happened and while the player stays in one
// area, and resets when a fight happens or the area changes.
void SGTC_Dir_HumanTick( sgtc_dir_human_t *h, float now, int area, int dmg_dealt,
	int dmg_taken, int near_miss );

// Clear the per-interval accumulators after the transcript was built.
void SGTC_Dir_HumanFlushInterval( sgtc_dir_human_t *h );

// Pacing phase for a map at `map_time`, cycling build -> peak -> relax over
// `period` seconds (period <= 0 falls back to 180). Returns
// SGTC_DIR_PACING_BUILD / _PEAK / _RELAX.
enum
{
	SGTC_DIR_PACING_NONE = 0,
	SGTC_DIR_PACING_BUILD = 1,
	SGTC_DIR_PACING_PEAK = 2,
	SGTC_DIR_PACING_RELAX = 3
};
int SGTC_Dir_PacingPhase( float map_time, float period );

// The intensity the pacing curve asks for at `map_time`, x1000 (build ramps
// 200..900, peak 1600..2600, relax 400..900).
int SGTC_Dir_TargetIntensity( float map_time, float period );

// Bot skill 1..5 (1 = easy) for a human: strong humans face stronger bots.
// Maps the human rating (1200..2400 typical) to 1..5 and nudges by one step
// when the human is bored (harder) or over-intense (easier), staying 1..5.
int SGTC_Dir_TargetSkill( int human_rating, int boredom, int intensity );

/* --------------------------------------------------------------------- */
/* chat rate limiting                                                     */
/* --------------------------------------------------------------------- */

typedef struct
{
	float last_bot[SGTC_DIR_MAX_BOTS];
	float last_any;
} sgtc_dir_chat_state_t;

void SGTC_Dir_ChatReset( sgtc_dir_chat_state_t *c );

// True when bot_index may speak at `now`: its own line is at least 20 s old
// and no bot spoke server-wide in the last 5 s.
int SGTC_Dir_ChatAllowed( const sgtc_dir_chat_state_t *c, int bot_index, float now );

void SGTC_Dir_ChatMark( sgtc_dir_chat_state_t *c, int bot_index, float now );

/* --------------------------------------------------------------------- */
/* plan schema                                                            */
/* --------------------------------------------------------------------- */

enum
{
	SGTC_DIR_ROLE_NONE = 0,
	SGTC_DIR_ROLE_ATTACK = 1,
	SGTC_DIR_ROLE_DEFEND = 2,
	SGTC_DIR_ROLE_ROAM = 3,
	SGTC_DIR_ROLE_ESCORT = 4,
	SGTC_DIR_ROLE_HUNT = 5,
	SGTC_DIR_ROLE_SUPPORT = 6
};

typedef struct
{
	int bot;                  // 1..32 (plan slot id, not an entity index)
	int team;                 // 0 keep, 1 Torri, 2 Goa'uld
	int cls;                  // 0 keep, 1..5
	int skill;                // 1..5
	int role;                 // SGTC_DIR_ROLE_*
	int target_area;          // 0 none
	int target_player;        // 0 none, 1..SGTC_DIR_MAX_PLAYERS
	int aggression_x100;      // 0..100
	int accuracy_x100;        // 25..200 (100 = unchanged)
	char weapons[SGTC_DIR_WEAPONS_PER_BOT][SGTC_DIR_WEAPON_LEN];
	char chat[SGTC_DIR_CHAT_LINES_PER_BOT][SGTC_DIR_CHAT_LINE_MAX];
} sgtc_dir_bot_plan_t;

typedef struct
{
	int bot_count;
	sgtc_dir_bot_plan_t bots[SGTC_DIR_MAX_BOTS];
	int pacing;               // SGTC_DIR_PACING_*
	int add_bots;             // -4..4
	int kick_bots;            // -4..4
	char rationale[SGTC_DIR_RATIONALE_MAX];
} sgtc_dir_plan_t;

enum
{
	SGTC_DIR_PARSE_OK = 0,
	SGTC_DIR_PARSE_EMPTY = 1,     // no JSON object found in the reply
	SGTC_DIR_PARSE_SYNTAX = 2,    // not valid JSON
	SGTC_DIR_PARSE_UNKNOWN = 3,   // unknown object key at a known position
	SGTC_DIR_PARSE_RANGE = 4,     // structurally valid but unusable overall
	SGTC_DIR_PARSE_TOO_BIG = 5,   // more bots than SGTC_DIR_MAX_BOTS
	SGTC_DIR_PARSE_TRUNCATED = 6  // the outermost object never closed
};

// Find the first complete JSON object in src[0..len) and return its extent in
// *start/*out_len. Leading prose and trailing text are ignored (models that
// ignore response_format wrap the JSON in a code fence or a sentence).
// Returns 0 on success; -2 when the first object starts but runs to the end of
// the input without closing (a truncated reply, never reported as "unknown
// key"); -1 when no balanced object is present.
int SGTC_Dir_ExtractJsonObject( const char *src, int len, int *start, int *out_len );

// Pull the assistant text out of an OpenAI-compatible chat/completions
// response: body -> choices[0].message.content. When content is absent or not
// a string, the raw body is copied instead (the caller may be pointed at a
// model that answers with the object directly). Returns the copied length, or
// -1 when dst/cap are invalid or the reply does not fit.
int SGTC_Dir_OpenAIExtractContent( const char *body, int body_len, char *dst, int cap );

// Parse a plan. Accepts exactly the schema in docs/BOT-DIRECTOR.md:
//   {"bots":[{"bot":1,"team":2,"class":3,"skill":4,"role":"hunt",
//             "target_area":7,"target_player":0,"aggression":0.7,
//             "accuracy":1.1,"weapons":["weapon_p90"],
//             "chat":["Hold the gate!"]}],
//    "pacing":"peak","add_bots":0,"kick_bots":0,"rationale":"..."}
// Unknown keys are rejected (SGTC_DIR_PARSE_UNKNOWN). Every accepted value is
// clamped; a missing bot field keeps its default (team/class 0 = "keep engine
// choice", skill 0 = "leave alone", role 0 = none). On failure the human
// readable reason is written to err (which may be NULL). Returns 0 when the
// plan is usable, otherwise one of the SGTC_DIR_PARSE_* codes.
// `out` is fully initialised before any parsing happens.
int SGTC_Dir_ParsePlan( const char *json, int len, sgtc_dir_plan_t *out,
	char *err, int errcap );

// True when the plan contains at least one instruction (used to decide whether
// a reply is worth applying).
int SGTC_Dir_PlanIsUsable( const sgtc_dir_plan_t *plan );

// Fill an empty plan whose rationale explains why the director is running
// without a model (endpoint off, timeout, malformed reply). This is what the
// local controller blends against when there is no LLM plan.
void SGTC_Dir_PlanSetFallback( sgtc_dir_plan_t *plan, const char *reason );

/* --------------------------------------------------------------------- */
/* weapons, classes, prompt and request body                              */
/* --------------------------------------------------------------------- */

#define SGTC_DIR_WEAPON_SLOTS 28

// Index of `name` in the game's 28-entry weapon grant table (WPLIST, the same
// table PlayerSpawn and SGTC_GiveFromMenu use), or -1 when it is not one of
// them. Matching is exact and case-sensitive.
int SGTC_Dir_WeaponSlotForName( const char *name );

// Registered classname for slot 0..27, or NULL outside that range.
const char *SGTC_Dir_WeaponSlotName( int slot );

// 1 when the team (1 Torri, 2 Goa'uld) and class (1..5) may use the slot
// (the complement of the game's SetWeaponAllow disallow list), 0 when it is
// blocked, -1 when team/class are out of range (caller decides).
int SGTC_Dir_WeaponAllowed( int team, int cls, int slot );

// Model key ("tealc", "horus", ...) for a team (1 Torri, 2 Goa'uld) and class
// 1..5. Returns 0 on success, -1 on bad input.
int SGTC_Dir_ClassName( int team, int cls, char *dst, int cap );

enum
{
	SGTC_DIR_FINISH_STOP = 0,   // "stop" or no finish_reason at all
	SGTC_DIR_FINISH_LENGTH = 1, // the provider cut the reply at max_tokens
	SGTC_DIR_FINISH_OTHER = 2   // content_filter or anything else
};

// Read choices[0].finish_reason from an OpenAI-compatible response. Writes the
// raw value to dst (may be empty) and returns one of SGTC_DIR_FINISH_*; -1
// when the body is not a readable completions response.
int SGTC_Dir_OpenAIFinishReason( const char *body, int body_len, char *dst, int cap );

// The system prompt. Byte-identical on every call (provider prompt caches key
// on the exact bytes), and documented in docs/BOT-DIRECTOR.md.
const char *SGTC_Dir_SystemPrompt( void );

// Effective max_tokens: `configured` raised to about 60 tokens per bot + 150
// (reasoning models spend tokens before the JSON), capped at 8192.
int SGTC_Dir_MaxTokensForBots( int bot_count, int configured );

// Build the chat/completions request body. temperature_x100 is hundredths of
// a degree (35 = 0.35). extra_json is an optional raw JSON fragment merged
// into the top-level object (e.g. "thinking":{"type":"disabled"}); it must
// already be valid JSON and must not contain a top-level close brace.
// Returns the length written, or -1 when it does not fit in cap.
int SGTC_Dir_BuildRequestJson( const char *model, const char *system,
	const char *user, int max_tokens, int temperature_x100,
	const char *extra_json, char *dst, int cap );

// Drop plan rules that do not fit the current game mode: with teamplay == 0
// every bot team becomes 0, otherwise at most max_switches explicit team
// changes survive (the rest become 0, so the model cannot reshuffle the whole
// roster at once). Returns the number of team values cleared.
int SGTC_Dir_EnforceTeamRules( sgtc_dir_plan_t *plan, int teamplay, int max_switches );

// Remove bot entries that ask for no change at all (no team/class/skill/role,
// no target, default aggression and accuracy, no weapons or chat). Returns the
// number of entries kept; the array is compacted in place.
int SGTC_Dir_PruneNoOpBots( sgtc_dir_plan_t *plan );

// Blend two per-bot skills with an integer weight in percent (0 = all a,
// 100 = all b), rounding to nearest. Returns the blended skill, clamped 1..5.
// weight <= 0 keeps a; both are treated as already valid 1..5 values.
int SGTC_Dir_BlendSkill( int a, int b, int weight_pct );

// Rate-limit between plan generations so bots never visibly flip: how much of
// the new plan may be applied after `dt` seconds since the last plan arrived.
// Returns 0..100 (percent of the new plan). Reaching 100 after 2 s.
int SGTC_Dir_BlendRamp( float dt );

/* --------------------------------------------------------------------- */
/* backoff                                                                */
/* --------------------------------------------------------------------- */

// Exponential backoff for a failing endpoint: 0 -> 1, then doubles, capped at
// `cap` seconds (cap <= 0 falls back to 300). A successful request calls
// SGTC_Dir_BackoffReset.
float SGTC_Dir_BackoffNext( float current, float cap );
float SGTC_Dir_BackoffReset( void );

#ifdef __cplusplus
}
#endif

#endif // SGTC_DIRECTOR_CORE_H
