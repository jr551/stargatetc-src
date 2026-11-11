// tests/host/director_core_harness.cpp - offline assertions for the director
// core. Plain int main(), no test framework: prints one line per case
//   CASE <name> PASS
//   CASE <name> FAIL <detail>
// and exits nonzero when any case fails. tests/test_director_core.py builds
// and runs this on the host.

#include <stdio.h>
#include <string.h>

#include "sgtc_director_core.h"

static int g_fail;
static int g_cases;

static void check( const char *name, int ok, const char *detail )
{
	g_cases++;
	if( ok )
	{
		printf( "CASE %s PASS\n", name );
	}
	else
	{
		g_fail++;
		printf( "CASE %s FAIL %s\n", name, detail ? detail : "(no detail)" );
	}
}

static int jslen( const char *s )
{
	return s ? (int)strlen( s ) : 0;
}

static int streq( const char *a, const char *b )
{
	return a && b && strcmp( a, b ) == 0;
}

static int nearf( float a, float b, float eps )
{
	float d = a - b;

	if( d < 0.0f )
		d = -d;
	return d <= eps;
}

static int parse_plan( const char *json, sgtc_dir_plan_t *plan, char *err )
{
	return SGTC_Dir_ParsePlan( json, jslen( json ), plan, err, SGTC_DIR_ERR_MAX );
}

static void make_rec( char *buf, int len, int index )
{
	int i;

	snprintf( buf, 16, "%03d", index );
	for( i = 3; i < len; i++ )
		buf[i] = 'a';
	buf[len] = '\0';
}

// ---------------------------------------------------------------------------

static void test_clamp( void )
{
	check( "clamp_int", SGTC_Dir_ClampInt( -3, 0, 10 ) == 0
		&& SGTC_Dir_ClampInt( 13, 0, 10 ) == 10
		&& SGTC_Dir_ClampInt( 7, 0, 10 ) == 7, "clamp int" );
	check( "clamp_float", nearf( SGTC_Dir_ClampFloat( -1.5f, 0.0f, 2.0f ), 0.0f, 0.0001f )
		&& nearf( SGTC_Dir_ClampFloat( 3.5f, 0.0f, 2.0f ), 2.0f, 0.0001f )
		&& nearf( SGTC_Dir_ClampFloat( 1.5f, 0.0f, 2.0f ), 1.5f, 0.0001f ), "clamp float" );

	{
		float zero = 0.0f;
		float nan = zero / zero;
		check( "clamp_float_nan", nearf( SGTC_Dir_ClampFloat( nan, 1.0f, 2.0f ), 1.0f, 0.0001f ),
			"NaN should clamp to lo" );
	}
}

static void test_sanitize( void )
{
	char buf[64];
	char src[128];
	char raw[4];
	int n;

	n = SGTC_Dir_SanitizeChat( buf, (int)sizeof( buf ), "  Hello\t\tWorld\r\n" );
	check( "sanitize_trim_collapse", n == 11 && streq( buf, "Hello World" ), buf );

	n = SGTC_Dir_SanitizeChat( buf, (int)sizeof( buf ), "a\x01" "b\x7f" "c" );
	check( "sanitize_controls", n == 5 && streq( buf, "a b c" ), buf );

	raw[0] = 'x';
	raw[1] = (char)0xff;
	raw[2] = 'y';
	raw[3] = '\0';
	n = SGTC_Dir_SanitizeChat( buf, (int)sizeof( buf ), raw );
	check( "sanitize_high_bytes", n == 3 && streq( buf, "x y" ), buf );

	n = SGTC_Dir_SanitizeChat( buf, 5, "abcdef" );
	check( "sanitize_truncate", n == 4 && streq( buf, "abcd" ), buf );

	check( "sanitize_bad_args", SGTC_Dir_SanitizeChat( NULL, 8, "x" ) == -1
		&& SGTC_Dir_SanitizeChat( buf, 0, "x" ) == -1, "NULL/cap 0" );

	n = SGTC_Dir_SanitizeChat( buf, (int)sizeof( buf ), NULL );
	check( "sanitize_null_src", n == 0 && buf[0] == '\0', "NULL source" );

	memset( src, 'x', 100 );
	src[100] = '\0';
	n = SGTC_Dir_SanitizeName( buf, (int)sizeof( buf ), src );
	check( "sanitize_name_cap", n == SGTC_DIR_NAME_MAX - 1, "name capped at 32" );
}

static void test_rle( void )
{
	sgtc_dir_rle_t r;
	char label[8];
	char out[64];

	SGTC_Dir_RleInit( &r );
	check( "rle_init", r.n == 0, "empty plan" );

	SGTC_Dir_RlePush( &r, 3 );
	SGTC_Dir_RlePush( &r, 3 );
	SGTC_Dir_RlePush( &r, 7 );
	SGTC_Dir_RlePush( &r, 7 );
	SGTC_Dir_RlePush( &r, 7 );
	SGTC_Dir_RlePush( &r, 1 );
	check( "rle_merge", r.n == 3 && r.area[0] == 3 && r.count[0] == 2
		&& r.area[1] == 7 && r.count[1] == 3
		&& r.area[2] == 1 && r.count[2] == 1, "merge consecutive samples" );

	check( "rle_format", SGTC_Dir_RleFormat( &r, out, (int)sizeof( out ) ) == 9
		&& streq( out, "Cx2,Gx3,A" ), out );

	check( "rle_area_label_a", SGTC_Dir_AreaLabel( 1, label, (int)sizeof( label ) ) == 0
		&& streq( label, "A" ), label );
	check( "rle_area_label_z", SGTC_Dir_AreaLabel( 26, label, (int)sizeof( label ) ) == 0
		&& streq( label, "Z" ), label );
	check( "rle_area_label_aa", SGTC_Dir_AreaLabel( 27, label, (int)sizeof( label ) ) == 0
		&& streq( label, "AA" ), label );
	check( "rle_area_label_ab", SGTC_Dir_AreaLabel( 28, label, (int)sizeof( label ) ) == 0
		&& streq( label, "AB" ), label );
	check( "rle_area_label_ba", SGTC_Dir_AreaLabel( 53, label, (int)sizeof( label ) ) == 0
		&& streq( label, "BA" ), label );
	check( "rle_area_label_bad", SGTC_Dir_AreaLabel( 0, label, (int)sizeof( label ) ) == -1
		&& SGTC_Dir_AreaLabel( 10000, label, (int)sizeof( label ) ) == -1, "range" );

	{
		int i;

		SGTC_Dir_RleInit( &r );
		for( i = 1; i <= SGTC_DIR_RLE_LEGS; i++ )
			SGTC_Dir_RlePush( &r, i );
		SGTC_Dir_RlePush( &r, 25 );
		check( "rle_oldest_dropped", r.n == SGTC_DIR_RLE_LEGS
			&& r.area[0] == 2 && r.area[r.n - 1] == 25, "oldest leg dropped" );

		SGTC_Dir_RlePush( &r, 25 );
		check( "rle_merge_at_capacity", r.n == SGTC_DIR_RLE_LEGS
			&& r.area[r.n - 1] == 25 && r.count[r.n - 1] == 2, "merge at capacity" );
	}

	check( "rle_format_small", SGTC_Dir_RleFormat( &r, out, 4 ) == -1 && out[0] == '\0',
		"overflow must fail and clear dst" );

	SGTC_Dir_RleInit( &r );
	SGTC_Dir_RlePush( &r, 2 );
	check( "rle_single_leg", SGTC_Dir_RleFormat( &r, out, (int)sizeof( out ) ) == 1
		&& streq( out, "B" ), out );
	check( "rle_bad_args", SGTC_Dir_RlePush( NULL, 1 ) == -1
		&& SGTC_Dir_RleFormat( &r, NULL, 8 ) == -1, "NULL arguments" );
}

static void test_doc( void )
{
	sgtc_dir_doc_t doc;
	char rec[1024];
	char out[128];
	int i;

	SGTC_Dir_DocInit( &doc );
	for( i = 0; i < 11; i++ )
	{
		make_rec( rec, 600, i );
		SGTC_Dir_DocAdd( &doc, rec );
	}
	check( "doc_transcript_cap", doc.total <= SGTC_DIR_TRANSCRIPT_MAX
		&& SGTC_Dir_DocLength( &doc ) == 6010
		&& doc.count == 10, "cap keeps newest records" );
	check( "doc_oldest_dropped", doc.seg_len[0] == 601
		&& doc.seg[0][0] == '0' && doc.seg[0][1] == '0' && doc.seg[0][2] == '1'
		&& doc.seg[0][600] == '\n', "oldest record dropped" );
	check( "doc_newest_kept", doc.seg[doc.count - 1][0] == '0'
		&& doc.seg[doc.count - 1][1] == '1' && doc.seg[doc.count - 1][2] == '0'
		&& doc.seg[doc.count - 1][600] == '\n', "newest record kept" );

	SGTC_Dir_DocInit( &doc );
	for( i = 0; i < 100; i++ )
	{
		snprintf( rec, 16, "%010d", i );
		SGTC_Dir_DocAdd( &doc, rec );
	}
	check( "doc_count_cap", doc.count == SGTC_DIR_SEG_MAX && doc.total == 1056,
		"record count capped" );
	check( "doc_count_cap_oldest", doc.seg_len[0] == 11
		&& strncmp( doc.seg[0], "0000000004", 10 ) == 0
		&& doc.seg[0][10] == '\n', doc.seg[0] );

	SGTC_Dir_DocInit( &doc );
	for( i = 0; i < 630; i++ )
		rec[i] = 'a';
	rec[630] = ' ';
	for( i = 631; i < 700; i++ )
		rec[i] = 'b';
	rec[700] = '\0';
	SGTC_Dir_DocAdd( &doc, rec );
	check( "doc_word_boundary", doc.count == 1 && doc.seg_len[0] == 631
		&& doc.seg[0][629] == 'a' && doc.seg[0][630] == '\n',
		"truncated on the last space" );

	SGTC_Dir_DocInit( &doc );
	for( i = 0; i < 700; i++ )
		rec[i] = 'c';
	rec[700] = '\0';
	SGTC_Dir_DocAdd( &doc, rec );
	check( "doc_hard_truncate", doc.count == 1 && doc.seg_len[0] == SGTC_DIR_SEG_LEN - 1
		&& doc.seg[0][SGTC_DIR_SEG_LEN - 2] == '\n',
		"no boundary: hard limit" );

	SGTC_Dir_DocInit( &doc );
	SGTC_Dir_DocAdd( &doc, "ab" );
	SGTC_Dir_DocAdd( &doc, "cd" );
	check( "doc_render", SGTC_Dir_DocRender( &doc, out, (int)sizeof( out ) ) == 6
		&& streq( out, "ab\ncd\n" )
		&& SGTC_Dir_DocLength( &doc ) == 6, out );
	check( "doc_render_small", SGTC_Dir_DocRender( &doc, out, 6 ) == -1 && out[0] == '\0',
		"render overflow" );
	check( "doc_render_exact", SGTC_Dir_DocRender( &doc, out, 7 ) == 6
		&& streq( out, "ab\ncd\n" ), out );
	check( "doc_bad_args", SGTC_Dir_DocAdd( NULL, "x" ) == -1
		&& SGTC_Dir_DocAdd( &doc, NULL ) == -1
		&& SGTC_Dir_DocRender( NULL, out, 8 ) == -1, "NULL arguments" );
}

static void test_elo( void )
{
	check( "elo_even", SGTC_Dir_EloExpected( 1500, 1500 ) == 500, "even is 500" );
	check( "elo_symmetric",
		SGTC_Dir_EloExpected( 1600, 1500 ) + SGTC_Dir_EloExpected( 1500, 1600 ) == 1000,
		"expected scores sum to 1000" );
	check( "elo_favourite", SGTC_Dir_EloExpected( 1600, 1500 ) > 550
		&& SGTC_Dir_EloExpected( 1600, 1500 ) < 565, "stronger side ~557" );
	check( "elo_extreme", SGTC_Dir_EloExpected( 1200, 2400 ) > 50
		&& SGTC_Dir_EloExpected( 1200, 2400 ) < 70, "1200 vs 2400 ~59" );

	check( "elo_update_even_win", SGTC_Dir_EloUpdate( 1500, 1500, 1 ) == 1516, "+16" );
	check( "elo_update_even_loss", SGTC_Dir_EloUpdate( 1500, 1500, 0 ) == 1484, "-16" );
	check( "elo_update_upset_win", SGTC_Dir_EloUpdate( 1500, 1600, 1 ) == 1518,
		"win against a stronger player gains more" );
	check( "elo_update_expected_loss", SGTC_Dir_EloUpdate( 1500, 1600, 0 ) == 1486,
		"loss against a stronger player costs less" );
	check( "elo_update_symmetric_gain",
		SGTC_Dir_EloUpdate( 1500, 1600, 1 ) - 1500
		> SGTC_Dir_EloUpdate( 1500, 1500, 1 ) - 1500, "upset gain > even gain" );
}

static void test_human( void )
{
	sgtc_dir_human_t h;

	SGTC_Dir_HumanInit( &h, 0.0f );
	check( "human_init", h.rating == SGTC_DIR_ELO_DEFAULT && h.intensity == 0
		&& h.boredom == 0 && h.last_area == 0, "defaults" );

	SGTC_Dir_HumanTick( &h, 0.0f, 5, 0, 0, 0 );
	SGTC_Dir_HumanTick( &h, 1.0f, 5, 100, 0, 0 );
	check( "intensity_damage_rise", h.intensity == 600, "100 damage -> 600" );

	SGTC_Dir_HumanTick( &h, 9.0f, 5, 0, 0, 0 );
	check( "intensity_half_life_8s", nearf( (float)h.intensity, 300.0f, 1.0f ),
		"8 s halves intensity" );

	SGTC_Dir_HumanTick( &h, 17.0f, 5, 0, 0, 0 );
	check( "intensity_half_life_16s", nearf( (float)h.intensity, 150.0f, 1.0f ),
		"16 s quarters intensity" );

	SGTC_Dir_HumanTick( &h, 33.0f, 5, 0, 0, 0 );
	check( "intensity_half_life_32s", nearf( (float)h.intensity, 38.0f, 1.0f ),
		"32 s is four half-lives" );

	SGTC_Dir_HumanTick( &h, 1000.0f, 5, 0, 0, 0 );
	check( "intensity_long_gap", h.intensity == 0, "very long gap decays to zero" );

	SGTC_Dir_HumanInit( &h, 0.0f );
	SGTC_Dir_HumanTick( &h, 0.0f, 5, 0, 0, 0 );
	SGTC_Dir_HumanTick( &h, 1.0f, 5, 0, 50, 2 );
	check( "intensity_taken_near_miss", h.intensity == 250,
		"50 taken + 2 near misses" );
	check( "human_flush", ( SGTC_Dir_HumanFlushInterval( &h ), h.damage_taken == 0
		&& h.damage_dealt == 0 && h.iter_since_flush == 0 ), "counters cleared" );

	SGTC_Dir_HumanInit( &h, 0.0f );
	SGTC_Dir_HumanTick( &h, 1.0f, 5, 100000, 0, 0 );
	check( "intensity_clamp", h.intensity == 3000, "clamped at 3000" );

	SGTC_Dir_HumanInit( &h, 0.0f );
	SGTC_Dir_HumanTick( &h, 0.0f, 5, 0, 0, 0 );
	SGTC_Dir_HumanTick( &h, 6.0f, 5, 0, 0, 0 );
	check( "boredom_6s", h.boredom == 1000, "idle+area at 6 s" );
	SGTC_Dir_HumanTick( &h, 15.0f, 5, 0, 0, 0 );
	check( "boredom_15s", h.boredom == 2500, "idle+area at 15 s" );
	SGTC_Dir_HumanTick( &h, 30.0f, 5, 0, 0, 0 );
	check( "boredom_clamp", h.boredom == 5000, "clamped at 5000" );

	SGTC_Dir_HumanTick( &h, 31.0f, 5, 1, 0, 0 );
	check( "boredom_fight_reset", h.boredom == 0 && h.intensity == 6,
		"fight resets boredom" );

	SGTC_Dir_HumanTick( &h, 40.0f, 6, 0, 0, 0 );
	check( "boredom_area_reset", h.boredom == 0, "area change resets boredom" );
	SGTC_Dir_HumanTick( &h, 46.0f, 6, 0, 0, 0 );
	check( "boredom_after_area_change", h.boredom == 1000,
		"restarts after the change" );

	SGTC_Dir_HumanInit( &h, 0.0f );
	SGTC_Dir_HumanTick( &h, 0.0f, 0, 0, 0, 0 );
	SGTC_Dir_HumanTick( &h, 10.0f, 0, 0, 0, 0 );
	check( "boredom_unknown_area", h.boredom == 1000,
		"area 0 gets only the idle rate" );

	SGTC_Dir_HumanInit( &h, 0.0f );
	SGTC_Dir_HumanTick( &h, 1.0f, 5, 7, 3, 0 );
	check( "human_accumulators", h.damage_dealt == 7 && h.damage_taken == 3
		&& h.iter_since_flush == 1, "interval accumulator" );
}

static void test_pacing( void )
{
	check( "pacing_build_start", SGTC_Dir_PacingPhase( 0.0f, 100.0f ) == SGTC_DIR_PACING_BUILD,
		"0%" );
	check( "pacing_build_edge", SGTC_Dir_PacingPhase( 34.9f, 100.0f ) == SGTC_DIR_PACING_BUILD,
		"34.9%" );
	check( "pacing_peak_start", SGTC_Dir_PacingPhase( 35.0f, 100.0f ) == SGTC_DIR_PACING_PEAK,
		"35%" );
	check( "pacing_peak_edge", SGTC_Dir_PacingPhase( 69.999f, 100.0f ) == SGTC_DIR_PACING_PEAK,
		"69.999%" );
	check( "pacing_relax_start", SGTC_Dir_PacingPhase( 70.0f, 100.0f ) == SGTC_DIR_PACING_RELAX,
		"70%" );
	check( "pacing_relax_edge", SGTC_Dir_PacingPhase( 99.999f, 100.0f ) == SGTC_DIR_PACING_RELAX,
		"99.999%" );
	check( "pacing_wrap", SGTC_Dir_PacingPhase( 100.0f, 100.0f ) == SGTC_DIR_PACING_BUILD,
		"next cycle" );
	check( "pacing_cycle2", SGTC_Dir_PacingPhase( 135.0f, 100.0f ) == SGTC_DIR_PACING_PEAK,
		"second cycle peak" );
	check( "pacing_default_period", SGTC_Dir_PacingPhase( 63.0f, 0.0f ) == SGTC_DIR_PACING_PEAK,
		"period 0 -> 180" );

	check( "target_build_start", SGTC_Dir_TargetIntensity( 0.0f, 100.0f ) == 200, "200" );
	check( "target_build_edge", SGTC_Dir_TargetIntensity( 34.9f, 100.0f ) == 898, "898" );
	check( "target_peak_start", SGTC_Dir_TargetIntensity( 35.0f, 100.0f ) == 1600, "1600" );
	check( "target_peak_mid", SGTC_Dir_TargetIntensity( 52.5f, 100.0f ) == 2100, "2100" );
	check( "target_relax_start", SGTC_Dir_TargetIntensity( 70.0f, 100.0f ) == 900, "900" );
	check( "target_relax_edge", SGTC_Dir_TargetIntensity( 99.9f, 100.0f ) == 402, "402" );
	check( "target_wrap", SGTC_Dir_TargetIntensity( 100.0f, 100.0f ) == 200, "200 again" );

	check( "target_skill_map", SGTC_Dir_TargetSkill( 1200, 0, 0 ) == 1
		&& SGTC_Dir_TargetSkill( 1500, 0, 0 ) == 2
		&& SGTC_Dir_TargetSkill( 1800, 0, 0 ) == 3
		&& SGTC_Dir_TargetSkill( 2100, 0, 0 ) == 4
		&& SGTC_Dir_TargetSkill( 2400, 0, 0 ) == 5, "rating bands" );
	check( "target_skill_bored", SGTC_Dir_TargetSkill( 1500, 3000, 0 ) == 3,
		"bored -> harder" );
	check( "target_skill_intense", SGTC_Dir_TargetSkill( 1500, 0, 2500 ) == 1,
		"intense -> easier" );
	check( "target_skill_both", SGTC_Dir_TargetSkill( 1500, 3000, 2500 ) == 2,
		"nudges cancel" );
	check( "target_skill_clamp_hi", SGTC_Dir_TargetSkill( 2400, 0, 2500 ) == 4, "5-1" );
	check( "target_skill_clamp_lo", SGTC_Dir_TargetSkill( 900, 0, 2500 ) == 1, "1-1" );
}

static void test_chat( void )
{
	sgtc_dir_chat_state_t c;

	SGTC_Dir_ChatReset( &c );
	check( "chat_initial_allowed", SGTC_Dir_ChatAllowed( &c, 0, 0.0f ) == 1,
		"first line allowed" );

	SGTC_Dir_ChatMark( &c, 0, 0.0f );
	check( "chat_bot_gap_just_under", SGTC_Dir_ChatAllowed( &c, 0, 19.99f ) == 0,
		"19.99 s" );
	check( "chat_bot_gap_exact", SGTC_Dir_ChatAllowed( &c, 0, 20.0f ) == 1,
		"exactly 20 s" );

	SGTC_Dir_ChatMark( &c, 1, 20.0f );
	check( "chat_global_just_under", SGTC_Dir_ChatAllowed( &c, 2, 24.99f ) == 0,
		"4.99 s global" );
	check( "chat_global_exact", SGTC_Dir_ChatAllowed( &c, 2, 25.0f ) == 1,
		"exactly 5 s global" );

	check( "chat_bad_index", SGTC_Dir_ChatAllowed( &c, -1, 100.0f ) == 0
		&& SGTC_Dir_ChatAllowed( &c, SGTC_DIR_MAX_BOTS, 100.0f ) == 0, "range" );

	SGTC_Dir_ChatMark( &c, -1, 100.0f );
	SGTC_Dir_ChatMark( NULL, 0, 0.0f );
	check( "chat_mark_bad_args", SGTC_Dir_ChatAllowed( NULL, 0, 100.0f ) == 0,
		"NULL/invalid are no-ops" );

	SGTC_Dir_ChatReset( &c );
	check( "chat_reset_reallows", SGTC_Dir_ChatAllowed( &c, 0, 0.0f ) == 1,
		"reset clears both gates" );
}

static void test_plan_ok( void )
{
	static const char *json =
		R"json({"bots":[{"bot":1,"team":2,"class":3,"skill":4,"role":"hunt","target_area":7,"target_player":2,"aggression":0.7,"accuracy":1.1,"weapons":["weapon_p90","weapon_zat"],"chat":["Hold the gate!","Go go"]},{"bot":2}],"pacing":"peak","add_bots":2,"kick_bots":-1,"rationale":"focus mid"})json";
	sgtc_dir_plan_t plan;
	char err[SGTC_DIR_ERR_MAX];
	int rc;

	rc = parse_plan( json, &plan, err );
	check( "plan_parse_ok", rc == 0 && plan.bot_count == 2, err );
	check( "plan_bot_fields", plan.bots[0].bot == 1 && plan.bots[0].team == 2
		&& plan.bots[0].cls == 3 && plan.bots[0].skill == 4
		&& plan.bots[0].role == SGTC_DIR_ROLE_HUNT
		&& plan.bots[0].target_area == 7 && plan.bots[0].target_player == 2,
		"explicit bot fields" );
	check( "plan_bot_scaling", plan.bots[0].aggression_x100 == 70
		&& plan.bots[0].accuracy_x100 == 110, "0.7 -> 70, 1.1 -> 110" );
	check( "plan_weapons", streq( plan.bots[0].weapons[0], "weapon_p90" )
		&& streq( plan.bots[0].weapons[1], "weapon_zat" )
		&& plan.bots[0].weapons[2][0] == '\0', "two weapons" );
	check( "plan_chat", streq( plan.bots[0].chat[0], "Hold the gate!" )
		&& streq( plan.bots[0].chat[1], "Go go" )
		&& plan.bots[0].chat[1][0] == 'G', "two chat lines" );
	check( "plan_defaults", plan.bots[1].skill == 0 && plan.bots[1].role == SGTC_DIR_ROLE_NONE
		&& plan.bots[1].aggression_x100 == 100 && plan.bots[1].accuracy_x100 == 100
		&& plan.bots[1].weapons[0][0] == '\0' && plan.bots[1].chat[0][0] == '\0',
		"missing fields keep defaults" );
	check( "plan_top_fields", plan.pacing == SGTC_DIR_PACING_PEAK
		&& plan.add_bots == 2 && plan.kick_bots == -1
		&& streq( plan.rationale, "focus mid" ), "top-level fields" );
	check( "plan_usable", SGTC_Dir_PlanIsUsable( &plan ) == 1, "has instructions" );

	rc = parse_plan( "{\"bots\":[{\"bot\":1,\"role\":\"none\"}]}", &plan, err );
	check( "plan_role_none", rc == 0 && plan.bots[0].role == SGTC_DIR_ROLE_NONE
		&& plan.bots[0].bot == 1, err );

	{
		sgtc_dir_plan_t empty;
		char e2[SGTC_DIR_ERR_MAX];

		rc = parse_plan( "{}", &empty, e2 );
		check( "plan_empty_object", rc == 0 && empty.bot_count == 0
			&& empty.pacing == SGTC_DIR_PACING_NONE
			&& SGTC_Dir_PlanIsUsable( &empty ) == 0, e2 );

		rc = parse_plan( "{\"bots\":[]}", &empty, e2 );
		check( "plan_empty_bots", rc == 0 && empty.bot_count == 0
			&& SGTC_Dir_PlanIsUsable( &empty ) == 0, e2 );

		rc = parse_plan( "{\"pacing\":\"relax\"}", &empty, e2 );
		check( "plan_pacing_only_usable", rc == 0 && empty.pacing == SGTC_DIR_PACING_RELAX
			&& SGTC_Dir_PlanIsUsable( &empty ) == 1, e2 );
	}
}

static void test_plan_clamps( void )
{
	static const char *high =
		R"json({"bots":[{"bot":1,"team":99,"class":99,"skill":99,"target_area":99999,"target_player":999,"aggression":5.0,"accuracy":0.01,"weapons":["a","","b","c","d"],"chat":["one","","two","three"]}],"add_bots":99,"kick_bots":-99})json";
	static const char *low =
		R"json({"bots":[{"bot":2,"team":-5,"class":-5,"skill":-5,"target_area":-5,"target_player":-5,"aggression":-1.0,"accuracy":0.1}],"add_bots":-99,"kick_bots":99})json";
	sgtc_dir_plan_t plan;
	char err[SGTC_DIR_ERR_MAX];
	int rc;

	rc = parse_plan( high, &plan, err );
	check( "plan_clamp_high", rc == 0 && plan.bots[0].team == 2 && plan.bots[0].cls == 5
		&& plan.bots[0].skill == 5 && plan.bots[0].target_area == 0
		&& plan.bots[0].target_player == 0
		&& plan.bots[0].aggression_x100 == 100 && plan.bots[0].accuracy_x100 == 25
		&& plan.add_bots == 4 && plan.kick_bots == -4, err );
	check( "plan_array_limits", streq( plan.bots[0].weapons[0], "a" )
		&& streq( plan.bots[0].weapons[1], "b" ) && streq( plan.bots[0].weapons[2], "c" )
		&& streq( plan.bots[0].chat[0], "one" ) && streq( plan.bots[0].chat[1], "two" ),
		"extras ignored" );

	rc = parse_plan( low, &plan, err );
	check( "plan_clamp_low", rc == 0 && plan.bots[0].team == 0 && plan.bots[0].cls == 0
		&& plan.bots[0].skill == 1 && plan.bots[0].target_area == 0
		&& plan.bots[0].target_player == 0 && plan.bots[0].aggression_x100 == 0
		&& plan.bots[0].accuracy_x100 == 25
		&& plan.add_bots == -4 && plan.kick_bots == 4, err );
}

static void test_plan_strings( void )
{
	char json[512];
	char longw[64];
	char longc[160];
	sgtc_dir_plan_t plan;
	char err[SGTC_DIR_ERR_MAX];
	int rc;

	memset( longw, 'w', 30 );
	longw[30] = '\0';
	snprintf( json, (int)sizeof( json ),
		"{\"bots\":[{\"bot\":1,\"weapons\":[\"%s\"]}]}", longw );
	rc = parse_plan( json, &plan, err );
	check( "plan_weapon_truncate", rc == 0 && plan.bots[0].weapons[0][0] == 'w'
		&& (int)strlen( plan.bots[0].weapons[0] ) == SGTC_DIR_WEAPON_LEN - 1, err );

	memset( longc, 'c', 120 );
	longc[120] = '\0';
	snprintf( json, (int)sizeof( json ),
		"{\"bots\":[{\"bot\":1,\"chat\":[\"%s\"]}]}", longc );
	rc = parse_plan( json, &plan, err );
	check( "plan_chat_truncate", rc == 0
		&& (int)strlen( plan.bots[0].chat[0] ) == SGTC_DIR_CHAT_LINE_MAX - 1, err );

	rc = parse_plan(
		"{\"bots\":[{\"bot\":1,\"chat\":[\"Hold\\nThe\\tGate!\",\"a\\u0001b\"]}]}",
		&plan, err );
	check( "plan_chat_sanitized", rc == 0
		&& streq( plan.bots[0].chat[0], "Hold The Gate!" )
		&& streq( plan.bots[0].chat[1], "a b" ), err );

	rc = parse_plan(
		"{\"bots\":[{\"bot\":1}],\"rationale\":\"line1\\nline2\"}", &plan, err );
	check( "plan_rationale_sanitized", rc == 0 && streq( plan.rationale, "line1 line2" ),
		err );

	{
		char longr[300];

		memset( longr, 'r', 250 );
		longr[250] = '\0';
		snprintf( json, (int)sizeof( json ), "{\"bots\":[{\"bot\":1}],\"rationale\":\"%s\"}",
			longr );
		rc = parse_plan( json, &plan, err );
		check( "plan_rationale_truncate", rc == 0
			&& (int)strlen( plan.rationale ) == SGTC_DIR_RATIONALE_MAX - 1, err );
	}

	rc = parse_plan( "{\"bots\":[{\"bot\":1,\"chat\":[\"\\u0041\\u00e9\"]}]}", &plan, err );
	check( "plan_unicode_ascii", rc == 0 && streq( plan.bots[0].chat[0], "A?" ), err );
}

static void test_plan_errors( void )
{
	sgtc_dir_plan_t plan;
	char err[SGTC_DIR_ERR_MAX];
	int rc;

	rc = parse_plan( "the model said nothing", &plan, err );
	check( "plan_empty_reply", rc == SGTC_DIR_PARSE_EMPTY && err[0] != '\0', err );

	rc = parse_plan( "{\"bots\":", &plan, err );
	check( "plan_truncated_open", rc == SGTC_DIR_PARSE_TRUNCATED
		&& strstr( err, "truncated" ) != NULL
		&& strstr( err, "unknown plan key" ) == NULL, err );

	rc = parse_plan( "{\"plan\":{\"bots\":[{\"bot\":1,", &plan, err );
	check( "plan_truncated_nested", rc == SGTC_DIR_PARSE_TRUNCATED
		&& strstr( err, "truncated" ) != NULL
		&& strstr( err, "unknown plan key" ) == NULL, err );

	rc = parse_plan( "{\"bots\": [ {\"bot\": 1, } ] }", &plan, err );
	check( "plan_trailing_comma", rc == SGTC_DIR_PARSE_SYNTAX, err );

	rc = parse_plan( "{\"bots\": [1]}", &plan, err );
	check( "plan_bot_not_object", rc == SGTC_DIR_PARSE_RANGE, err );

	rc = parse_plan( "{\"bots\":[{\"bot\":1},]}", &plan, err );
	check( "plan_bots_trailing_comma", rc == SGTC_DIR_PARSE_SYNTAX, err );

	rc = parse_plan( "{\"bots\":[{\"bot\":1,\"chat\":[\"a\",]}]}", &plan, err );
	check( "plan_array_trailing_comma", rc == SGTC_DIR_PARSE_SYNTAX, err );

	rc = parse_plan( "{\"bots\":[],\"model\":\"x\"}", &plan, err );
	check( "plan_unknown_top_key", rc == SGTC_DIR_PARSE_UNKNOWN, err );

	rc = parse_plan( "{\"bots\":[{\"bot\":1,\"speed\":5}]}", &plan, err );
	check( "plan_unknown_bot_key", rc == SGTC_DIR_PARSE_UNKNOWN, err );

	rc = parse_plan( "{\"bots\":[{\"bot\":1,\"role\":\"sniper\"}]}", &plan, err );
	check( "plan_bad_role", rc == SGTC_DIR_PARSE_RANGE, err );

	rc = parse_plan( "{\"bots\":[],\"pacing\":\"sprint\"}", &plan, err );
	check( "plan_bad_pacing", rc == SGTC_DIR_PARSE_RANGE, err );

	rc = parse_plan( "{\"bots\":[{\"skill\":3}]}", &plan, err );
	check( "plan_missing_bot_id", rc == SGTC_DIR_PARSE_RANGE, err );

	rc = parse_plan( "{\"bots\":[{\"bot\":0}]}", &plan, err );
	check( "plan_bot_id_low", rc == SGTC_DIR_PARSE_RANGE, err );

	rc = parse_plan( "{\"bots\":[{\"bot\":33}]}", &plan, err );
	check( "plan_bot_id_high", rc == SGTC_DIR_PARSE_RANGE, err );

	rc = parse_plan( "{\"bots\":[{\"bot\":1},{\"bot\":1}]}", &plan, err );
	check( "plan_duplicate_bot", rc == SGTC_DIR_PARSE_RANGE, err );

	rc = parse_plan( "{\"bots\":5}", &plan, err );
	check( "plan_bots_wrong_type", rc == SGTC_DIR_PARSE_RANGE, err );

	rc = parse_plan( "{\"add_bots\":\"x\"}", &plan, err );
	check( "plan_int_wrong_type", rc == SGTC_DIR_PARSE_RANGE, err );

	rc = parse_plan( "{\"add_bots\":1.5}", &plan, err );
	check( "plan_int_fraction", rc == SGTC_DIR_PARSE_RANGE, err );

	rc = parse_plan( "{\"rationale\":[]}", &plan, err );
	check( "plan_string_wrong_type", rc == SGTC_DIR_PARSE_RANGE, err );

	rc = parse_plan( "{\"add_bots\":NaN}", &plan, err );
	check( "plan_nan_rejected", rc == SGTC_DIR_PARSE_SYNTAX, err );

	rc = parse_plan( "{\"add_bots\":Infinity}", &plan, err );
	check( "plan_infinity_rejected", rc == SGTC_DIR_PARSE_SYNTAX, err );

	rc = parse_plan( "{\"add_bots\":1e999}", &plan, err );
	check( "plan_huge_number_rejected", rc == SGTC_DIR_PARSE_SYNTAX, err );

	rc = parse_plan( "{\"bots\":[{\"bot\":1,\"chat\":[\"a\nb\"]}]}", &plan, err );
	check( "plan_raw_control_rejected", rc == SGTC_DIR_PARSE_SYNTAX, err );

	rc = parse_plan( "{\"bots\":[{\"bot\":1}],\"bots\":[]}", &plan, err );
	check( "plan_duplicate_top_key", rc == SGTC_DIR_PARSE_RANGE, err );

	{
		char big[4096];
		int pos = snprintf( big, (int)sizeof( big ), "{\"bots\":[" );
		int i;

		for( i = 1; i <= 33; i++ )
		{
			int n = snprintf( big + pos, (size_t)( (int)sizeof( big ) - pos ),
				"%s{\"bot\":%d}", i > 1 ? "," : "", i );
			if( n > 0 )
				pos += n;
		}
		snprintf( big + pos, (size_t)( (int)sizeof( big ) - pos ), "]}" );
		rc = parse_plan( big, &plan, err );
		check( "plan_too_many_bots", rc == SGTC_DIR_PARSE_TOO_BIG, err );
	}
}

static void test_extract( void )
{
	const char *prose = "Here is the plan:\n```json\n"
		"{\"bots\":[{\"bot\":1,\"role\":\"roam\"}],\"pacing\":\"build\"}\n```\nDone.";
	const char *tricky = "prefix {\"s\":\"a}b{c\",\"n\":1} suffix";
	const char *tricky_obj = "{\"s\":\"a}b{c\",\"n\":1}";
	const char *escaped = "x {\"s\":\"q\\\"} still?\",\"n\":1} y";
	const char *escaped_obj = "{\"s\":\"q\\\"} still?\",\"n\":1}";
	int start = -1;
	int len = -1;
	sgtc_dir_plan_t plan;
	char err[SGTC_DIR_ERR_MAX];
	int rc;

	rc = parse_plan( prose, &plan, err );
	check( "extract_prose_plan", rc == 0 && plan.bot_count == 1
		&& plan.bots[0].role == SGTC_DIR_ROLE_ROAM
		&& plan.pacing == SGTC_DIR_PACING_BUILD, err );

	check( "extract_prose_object",
		SGTC_Dir_ExtractJsonObject( prose, jslen( prose ), &start, &len ) == 0
		&& start > 0 && len > 2 && prose[start] == '{'
		&& prose[start + len - 1] == '}', "finds the fenced object" );

	check( "extract_braces_in_string",
		SGTC_Dir_ExtractJsonObject( tricky, jslen( tricky ), &start, &len ) == 0
		&& start == jslen( "prefix " ) && len == jslen( tricky_obj )
		&& strncmp( tricky + start, tricky_obj, (size_t)len ) == 0,
		"braces inside strings are ignored" );

	check( "extract_escaped_quote",
		SGTC_Dir_ExtractJsonObject( escaped, jslen( escaped ), &start, &len ) == 0
		&& len == jslen( escaped_obj )
		&& strncmp( escaped + start, escaped_obj, (size_t)len ) == 0,
		"escaped quotes do not end the string" );

	check( "extract_truncated_outer",
		SGTC_Dir_ExtractJsonObject( "{\"plan\":{\"bots\":[{\"bot\":1,", 26,
			&start, &len ) == -2, "truncated reply is -2" );
	check( "extract_truncated_unbalanced",
		SGTC_Dir_ExtractJsonObject( "{\"a\":1", 6, &start, &len ) == -2,
		"unterminated object is -2" );
	check( "extract_truncated_fenced",
		SGTC_Dir_ExtractJsonObject( "Sure! ```json\n{\"a\":1}\n```",
			jslen( "Sure! ```json\n{\"a\":1}\n```" ), &start, &len ) == 0
		&& len == 7, "complete fenced object" );
	check( "extract_prose_after_stray_brace",
		SGTC_Dir_ExtractJsonObject( "} oops {\"a\":1} tail",
			jslen( "} oops {\"a\":1} tail" ), &start, &len ) == 0
		&& start == 7 && len == 7, "prose before the object" );
	check( "extract_none",
		SGTC_Dir_ExtractJsonObject( "hello", 5, &start, &len ) == -1, "no object" );
	check( "extract_bad_args",
		SGTC_Dir_ExtractJsonObject( NULL, 0, &start, &len ) == -1, "NULL" );
}

static void test_openai( void )
{
	static const char *body =
		R"json({"id":"cmpl-1","choices":[{"index":0,"message":{"role":"assistant","content":"{\"a\":1}"}}],"usage":{"total_tokens":12}})json";
	char out[512];
	int n;

	n = SGTC_Dir_OpenAIExtractContent( body, jslen( body ), out, (int)sizeof( out ) );
	check( "openai_content", n == 7 && streq( out, "{\"a\":1}" ), out );

	{
		const char *escaped_body = "{\"choices\":[{\"message\":{\"content\":\"line1\\nline2\"}}]}";

		n = SGTC_Dir_OpenAIExtractContent( escaped_body, jslen( escaped_body ), out,
			(int)sizeof( out ) );
	}
	check( "openai_escape_newline", n == 11 && streq( out, "line1\nline2" ), "escapes" );

	{
		const char *quoted_body = "{\"choices\":[{\"message\":{\"content\":\"say \\\"hi\\\"\"}}]}";

		n = SGTC_Dir_OpenAIExtractContent( quoted_body, jslen( quoted_body ), out,
			(int)sizeof( out ) );
	}
	check( "openai_escape_quote", n == 8 && streq( out, "say \"hi\"" ), out );

	{
		const char *unicode_body = "{\"choices\":[{\"message\":{\"content\":\"A\\u0042C\"}}]}";

		n = SGTC_Dir_OpenAIExtractContent( unicode_body, jslen( unicode_body ), out,
			(int)sizeof( out ) );
	}
	check( "openai_unicode_ascii", n == 3 && streq( out, "ABC" ), out );

	{
		const char *empty_body = "{\"choices\":[{\"message\":{\"content\":\"\"}}]}";

		n = SGTC_Dir_OpenAIExtractContent( empty_body, jslen( empty_body ), out,
			(int)sizeof( out ) );
	}
	check( "openai_empty_content", n == 0 && out[0] == '\0', "empty string is content" );

	{
		const char *missing = "{\"choices\":[{\"message\":{\"role\":\"assistant\"}}]}";

		n = SGTC_Dir_OpenAIExtractContent( missing, jslen( missing ), out,
			(int)sizeof( out ) );
		check( "openai_missing_content_fallback", n == jslen( missing )
			&& streq( out, missing ), "raw body fallback" );
	}

	{
		const char *null_content = "{\"choices\":[{\"message\":{\"content\":null}}]}";

		n = SGTC_Dir_OpenAIExtractContent( null_content, jslen( null_content ), out,
			(int)sizeof( out ) );
		check( "openai_null_content_fallback", n == jslen( null_content )
			&& streq( out, null_content ), "raw body fallback" );
	}

	{
		const char *number_content = "{\"choices\":[{\"message\":{\"content\":42}}]}";

		n = SGTC_Dir_OpenAIExtractContent( number_content, jslen( number_content ), out,
			(int)sizeof( out ) );
		check( "openai_number_content_fallback", n == jslen( number_content )
			&& streq( out, number_content ), "raw body fallback" );
	}

	{
		const char *malformed = "{\"choices\":}";

		n = SGTC_Dir_OpenAIExtractContent( malformed, jslen( malformed ), out,
			(int)sizeof( out ) );
		check( "openai_malformed_fallback", n == jslen( malformed )
			&& streq( out, malformed ), "raw body fallback" );
	}

	{
		const char *empty_choices = "{\"choices\":[]}";

		n = SGTC_Dir_OpenAIExtractContent( empty_choices, jslen( empty_choices ), out,
			(int)sizeof( out ) );
		check( "openai_empty_choices_fallback", n == jslen( empty_choices )
			&& streq( out, empty_choices ), "raw body fallback" );
	}

	{
		const char *six = "{\"choices\":[{\"message\":{\"content\":\"abcdef\"}}]}";

		n = SGTC_Dir_OpenAIExtractContent( six, jslen( six ), out, 4 );
		check( "openai_too_small", n == -1, "content does not fit" );
	}

	check( "openai_bad_args",
		SGTC_Dir_OpenAIExtractContent( body, jslen( body ), NULL, 16 ) == -1
		&& SGTC_Dir_OpenAIExtractContent( NULL, 0, out, (int)sizeof( out ) ) == -1
		&& SGTC_Dir_OpenAIExtractContent( body, jslen( body ), out, 0 ) == -1,
		"NULL/cap 0" );

	{
		const char *plain = "not json at all";
		char tiny[8];

		n = SGTC_Dir_OpenAIExtractContent( plain, jslen( plain ), tiny, (int)sizeof( tiny ) );
		check( "openai_fallback_too_small", n == -1, "raw body does not fit" );
	}
}

static void test_blend_backoff( void )
{
	check( "blend_skill_mid", SGTC_Dir_BlendSkill( 2, 4, 50 ) == 3, "2/4 at 50%" );
	check( "blend_skill_edges", SGTC_Dir_BlendSkill( 2, 4, 0 ) == 2
		&& SGTC_Dir_BlendSkill( 2, 4, 100 ) == 4, "0/100" );
	check( "blend_skill_round_up", SGTC_Dir_BlendSkill( 1, 2, 50 ) == 2, "1.5 -> 2" );
	check( "blend_skill_round_down_sym", SGTC_Dir_BlendSkill( 2, 1, 50 ) == 2, "1.5 -> 2" );
	check( "blend_skill_quarter", SGTC_Dir_BlendSkill( 5, 1, 25 ) == 4, "5 to 1 at 25%" );
	check( "blend_skill_three_quarter", SGTC_Dir_BlendSkill( 1, 5, 75 ) == 4, "1 to 5 at 75%" );
	check( "blend_skill_clamp", SGTC_Dir_BlendSkill( 1, 5, 99 ) == 5
		&& SGTC_Dir_BlendSkill( 5, 1, 1 ) == 5, "clamped 1..5" );

	check( "blend_ramp_start", SGTC_Dir_BlendRamp( -1.0f ) == 0
		&& SGTC_Dir_BlendRamp( 0.0f ) == 0, "0" );
	check( "blend_ramp_half", SGTC_Dir_BlendRamp( 1.0f ) == 50, "50 at 1 s" );
	check( "blend_ramp_almost", SGTC_Dir_BlendRamp( 1.99f ) == 99, "99 just before 2 s" );
	check( "blend_ramp_full", SGTC_Dir_BlendRamp( 2.0f ) == 100
		&& SGTC_Dir_BlendRamp( 100.0f ) == 100, "100 at 2 s" );

	check( "backoff_zero", nearf( SGTC_Dir_BackoffNext( 0.0f, 300.0f ), 1.0f, 0.0001f ),
		"0 -> 1" );
	check( "backoff_doubles", nearf( SGTC_Dir_BackoffNext( 1.0f, 300.0f ), 2.0f, 0.0001f )
		&& nearf( SGTC_Dir_BackoffNext( 2.0f, 300.0f ), 4.0f, 0.0001f )
		&& nearf( SGTC_Dir_BackoffNext( 128.0f, 300.0f ), 256.0f, 0.0001f ),
		"doubling" );
	check( "backoff_cap", nearf( SGTC_Dir_BackoffNext( 256.0f, 300.0f ), 300.0f, 0.0001f )
		&& nearf( SGTC_Dir_BackoffNext( 300.0f, 300.0f ), 300.0f, 0.0001f ),
		"clamped at cap" );
	check( "backoff_default_cap",
		nearf( SGTC_Dir_BackoffNext( 100.0f, 0.0f ), 200.0f, 0.0001f )
		&& nearf( SGTC_Dir_BackoffNext( 200.0f, 0.0f ), 300.0f, 0.0001f )
		&& nearf( SGTC_Dir_BackoffNext( 200.0f, -5.0f ), 300.0f, 0.0001f ),
		"cap <= 0 -> 300" );
	check( "backoff_negative", nearf( SGTC_Dir_BackoffNext( -5.0f, 300.0f ), 1.0f, 0.0001f ),
		"non-positive -> 1" );
	check( "backoff_reset", nearf( SGTC_Dir_BackoffReset(), 0.0f, 0.0001f ), "reset 0" );
}

static void test_fallback( void )
{
	sgtc_dir_plan_t plan;

	SGTC_Dir_PlanSetFallback( &plan, "timeout waiting for the model" );
	check( "fallback_reason", plan.bot_count == 0 && plan.pacing == SGTC_DIR_PACING_NONE
		&& plan.add_bots == 0 && plan.kick_bots == 0
		&& streq( plan.rationale, "timeout waiting for the model" )
		&& SGTC_Dir_PlanIsUsable( &plan ) == 0, plan.rationale );

	SGTC_Dir_PlanSetFallback( &plan, NULL );
	check( "fallback_default_reason", plan.rationale[0] != '\0'
		&& SGTC_Dir_PlanIsUsable( &plan ) == 0, plan.rationale );

	SGTC_Dir_PlanSetFallback( &plan, "  bad\n news\t" );
	check( "fallback_sanitized", streq( plan.rationale, "bad news" ), plan.rationale );

	SGTC_Dir_PlanSetFallback( NULL, "x" );
	check( "fallback_null_plan", 1, "no crash" );
}

static void test_weapons( void )
{
	check( "weapon_slot_index",
		SGTC_Dir_WeaponSlotForName( "weapon_glstaff" ) == 0
		&& SGTC_Dir_WeaponSlotForName( "weapon_p90" ) == 6
		&& SGTC_Dir_WeaponSlotForName( "weapon_handgrenade" ) == 15
		&& SGTC_Dir_WeaponSlotForName( "weapon_goaregen" ) == 27,
		"WPLIST order" );

	check( "weapon_slot_unknown",
		SGTC_Dir_WeaponSlotForName( "weapon_bogus" ) == -1
		&& SGTC_Dir_WeaponSlotForName( "weapon_p90 " ) == -1
		&& SGTC_Dir_WeaponSlotForName( "Weapon_p90" ) == -1
		&& SGTC_Dir_WeaponSlotForName( NULL ) == -1, "unknown names" );

	check( "weapon_slot_names",
		streq( SGTC_Dir_WeaponSlotName( 0 ), "weapon_glstaff" )
		&& streq( SGTC_Dir_WeaponSlotName( 27 ), "weapon_goaregen" )
		&& SGTC_Dir_WeaponSlotName( -1 ) == NULL
		&& SGTC_Dir_WeaponSlotName( SGTC_DIR_WEAPON_SLOTS ) == NULL, "slot names" );

	// Zat (slot 10) is allowed for Torri class 4 and Goa'uld class 1. The game
	// tables also allow it for Torri class 1 (slot 9, weapon_main, is the
	// blocked one there), so assert the source truth rather than the prompt's
	// class summary.
	check( "weapon_allowed_zat",
		SGTC_Dir_WeaponAllowed( 1, 4, 10 ) == 1
		&& SGTC_Dir_WeaponAllowed( 2, 1, 10 ) == 1
		&& SGTC_Dir_WeaponAllowed( 1, 1, 10 ) == 1
		&& SGTC_Dir_WeaponAllowed( 1, 1, 9 ) == 0
		&& SGTC_Dir_WeaponAllowed( 1, 5, 10 ) == 0, "zat by class" );

	// p90 is fine for Torri class 1, blocked for Torri class 4 and both
	// Goa'uld classes that have it in the disallow list.
	check( "weapon_allowed_p90",
		SGTC_Dir_WeaponAllowed( 1, 1, 6 ) == 1
		&& SGTC_Dir_WeaponAllowed( 1, 4, 6 ) == 0
		&& SGTC_Dir_WeaponAllowed( 2, 2, 6 ) == 0, "p90 by class" );

	check( "weapon_allowed_lists",
		SGTC_Dir_WeaponAllowed( 1, 1, 27 ) == 0		// goaregen
		&& SGTC_Dir_WeaponAllowed( 1, 2, 11 ) == 0	// lance
		&& SGTC_Dir_WeaponAllowed( 1, 2, 10 ) == 1	// zat
		&& SGTC_Dir_WeaponAllowed( 1, 3, 16 ) == 0	// mine
		&& SGTC_Dir_WeaponAllowed( 1, 3, 10 ) == 1	// zat
		&& SGTC_Dir_WeaponAllowed( 1, 5, 0 ) == 0	// glstaff
		&& SGTC_Dir_WeaponAllowed( 2, 1, 0 ) == 0	// glstaff
		&& SGTC_Dir_WeaponAllowed( 2, 1, 22 ) == 1	// medic
		&& SGTC_Dir_WeaponAllowed( 2, 3, 9 ) == 0	// main
		&& SGTC_Dir_WeaponAllowed( 2, 3, 12 ) == 1	// reetou
		&& SGTC_Dir_WeaponAllowed( 2, 4, 0 ) == 1	// glstaff
		&& SGTC_Dir_WeaponAllowed( 2, 4, 13 ) == 0	// zatarc
		&& SGTC_Dir_WeaponAllowed( 2, 5, 19 ) == 0	// gacgoa
		&& SGTC_Dir_WeaponAllowed( 2, 5, 21 ) == 0	// larve is blocked for cls 5
		&& SGTC_Dir_WeaponAllowed( 2, 5, 22 ) == 1,	// medic
		"disallow lists" );

	check( "weapon_allowed_bad_args",
		SGTC_Dir_WeaponAllowed( 0, 1, 0 ) == -1
		&& SGTC_Dir_WeaponAllowed( 3, 1, 0 ) == -1
		&& SGTC_Dir_WeaponAllowed( 1, 0, 0 ) == -1
		&& SGTC_Dir_WeaponAllowed( 1, 6, 0 ) == -1
		&& SGTC_Dir_WeaponAllowed( 1, 1, -1 ) == -1
		&& SGTC_Dir_WeaponAllowed( 1, 1, SGTC_DIR_WEAPON_SLOTS ) == -1,
		"team/class/slot range" );
}

static void test_class_names( void )
{
	static const char *torri[5] =
	{
		"oneill", "carter", "jackson", "tealc", "commander"
	};
	static const char *goauld[5] =
	{
		"systemlord", "anubis", "goauld3", "snakegoauld", "horus"
	};
	char buf[32];
	int i;
	int ok = 1;

	for( i = 1; i <= 5; i++ )
	{
		if( SGTC_Dir_ClassName( 1, i, buf, (int)sizeof( buf ) ) != 0
			|| !streq( buf, torri[i - 1] ) )
			ok = 0;
		if( SGTC_Dir_ClassName( 2, i, buf, (int)sizeof( buf ) ) != 0
			|| !streq( buf, goauld[i - 1] ) )
			ok = 0;
	}
	check( "class_names_all", ok, "all ten model keys" );

	check( "class_names_spot",
		SGTC_Dir_ClassName( 1, 4, buf, (int)sizeof( buf ) ) == 0
		&& streq( buf, "tealc" )
		&& SGTC_Dir_ClassName( 2, 5, buf, (int)sizeof( buf ) ) == 0
		&& streq( buf, "horus" ), buf );

	check( "class_names_bad_args",
		SGTC_Dir_ClassName( 0, 1, buf, (int)sizeof( buf ) ) == -1
		&& SGTC_Dir_ClassName( 3, 1, buf, (int)sizeof( buf ) ) == -1
		&& SGTC_Dir_ClassName( 1, 0, buf, (int)sizeof( buf ) ) == -1
		&& SGTC_Dir_ClassName( 1, 6, buf, (int)sizeof( buf ) ) == -1
		&& SGTC_Dir_ClassName( 1, 1, NULL, 8 ) == -1
		&& SGTC_Dir_ClassName( 1, 1, buf, 0 ) == -1, "bad input" );

	check( "class_names_cap",
		SGTC_Dir_ClassName( 2, 4, buf, 4 ) == -1
		&& SGTC_Dir_ClassName( 1, 4, buf, 6 ) == 0
		&& streq( buf, "tealc" ), buf );
}

static void test_finish_reason( void )
{
	char out[64];
	int n;

	{
		const char *body =
			"{\"choices\":[{\"index\":0,\"message\":{\"content\":\"finish_reason\"},"
			"\"finish_reason\":\"length\"}]}";

		n = SGTC_Dir_OpenAIFinishReason( body, jslen( body ), out, (int)sizeof( out ) );
	}
	check( "finish_reason_length", n == SGTC_DIR_FINISH_LENGTH
		&& streq( out, "length" ), out );

	{
		const char *body =
			"{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"content\":\"x\"}}]}";

		n = SGTC_Dir_OpenAIFinishReason( body, jslen( body ), out, (int)sizeof( out ) );
	}
	check( "finish_reason_stop", n == SGTC_DIR_FINISH_STOP
		&& streq( out, "stop" ), out );

	{
		const char *body =
			"{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"ok\"}}]}";

		n = SGTC_Dir_OpenAIFinishReason( body, jslen( body ), out, (int)sizeof( out ) );
	}
	check( "finish_reason_absent", n == SGTC_DIR_FINISH_STOP && out[0] == '\0', out );

	{
		const char *body = "{\"choices\":[{\"finish_reason\":\"content_filter\"}]}";

		n = SGTC_Dir_OpenAIFinishReason( body, jslen( body ), out, (int)sizeof( out ) );
	}
	check( "finish_reason_other", n == SGTC_DIR_FINISH_OTHER
		&& streq( out, "content_filter" ), out );

	{
		// Nested and escaped values first: only the choice's own field counts.
		const char *body =
			"{\"choices\":[{\"meta\":{\"finish_reason\":\"length\"},"
			"\"message\":{\"content\":\"a}b\\\"c\"},\"finish_reason\":\"stop\"}]}";

		n = SGTC_Dir_OpenAIFinishReason( body, jslen( body ), out, (int)sizeof( out ) );
	}
	check( "finish_reason_nested", n == SGTC_DIR_FINISH_STOP
		&& streq( out, "stop" ), out );

	check( "finish_reason_no_choices",
		SGTC_Dir_OpenAIFinishReason( "{\"id\":\"x\"}", 10, out, (int)sizeof( out ) ) == -1
		&& SGTC_Dir_OpenAIFinishReason( "{\"choices\":[]}", 14, out,
			(int)sizeof( out ) ) == -1
		&& SGTC_Dir_OpenAIFinishReason( "{\"choices\":[1]}", 15, out,
			(int)sizeof( out ) ) == -1
		&& SGTC_Dir_OpenAIFinishReason( "{\"choices\":{}}", 14, out,
			(int)sizeof( out ) ) == -1, "not a completions body" );

	{
		const char *body = "{\"choices\":[{\"finish_reason\":null}]}";

		n = SGTC_Dir_OpenAIFinishReason( body, jslen( body ), out, (int)sizeof( out ) );
	}
	check( "finish_reason_wrong_type", n == -1 && out[0] == '\0', out );

	{
		const char *body = "{\"choices\":[{\"finish_reason\":\"length\"}]}";

		n = SGTC_Dir_OpenAIFinishReason( body, jslen( body ), out, 3 );
	}
	check( "finish_reason_too_small", n == -1 && out[0] == '\0', out );

	check( "finish_reason_bad_args",
		SGTC_Dir_OpenAIFinishReason( NULL, 0, out, (int)sizeof( out ) ) == -1
		&& SGTC_Dir_OpenAIFinishReason( "{}", 2, NULL, 8 ) == -1
		&& SGTC_Dir_OpenAIFinishReason( "{}", 2, out, 0 ) == -1, "NULL/cap 0" );
}

static void test_prompt( void )
{
	const char *a = SGTC_Dir_SystemPrompt();
	const char *b = SGTC_Dir_SystemPrompt();

	check( "prompt_stable", a && b && strcmp( a, b ) == 0, "byte-identical" );
	check( "prompt_facts",
		a && strstr( a, "TEAMS" ) != NULL
		&& strstr( a, "tealc" ) != NULL
		&& strstr( a, "systemlord" ) != NULL
		&& strstr( a, "weapon_zat" ) != NULL
		&& strstr( a, "max_tokens" ) == NULL, "class and team facts" );
	check( "prompt_rules",
		a && strstr( a, "team balance" ) != NULL
		&& strstr( a, "weapon_mustardgrenade" ) != NULL
		&& strstr( a, "Reply with exactly one JSON object and nothing else." ) != NULL
		&& a[0] == 'Y', "reply rules" );
}

static void test_max_tokens( void )
{
	check( "max_tokens_per_bot", SGTC_Dir_MaxTokensForBots( 16, 700 ) == 1110,
		"16 bots raises 700 to 1110" );
	check( "max_tokens_default", SGTC_Dir_MaxTokensForBots( 0, 0 ) == 1500,
		"<= 0 configured falls back to 1500" );
	check( "max_tokens_oversized", SGTC_Dir_MaxTokensForBots( 100, 0 ) == 2070,
		"beyond 32 bots uses the ceiling" );
	check( "max_tokens_clamped",
		SGTC_Dir_MaxTokensForBots( 32, 0 ) == 2070
		&& SGTC_Dir_MaxTokensForBots( 4, 9000 ) == 8192
		&& SGTC_Dir_MaxTokensForBots( 4, 5000 ) == 5000
		&& SGTC_Dir_MaxTokensForBots( -3, 0 ) == 1500, "clamp and default" );
}

static void test_request( void )
{
	char body[4096];
	char exact[4096];
	char small[65];
	int n;
	int m;

	n = SGTC_Dir_BuildRequestJson( "deepseek", "system text", "user text",
		1110, 35, NULL, body, (int)sizeof( body ) );
	check( "request_shape", n > 0 && n == (int)strlen( body )
		&& strstr( body, "\"model\":\"deepseek\"" ) != NULL
		&& strstr( body, "\"messages\":[{\"role\":\"system\",\"content\":\"system text\"},"
			"{\"role\":\"user\",\"content\":\"user text\"}]" ) != NULL
		&& strstr( body, "\"response_format\":{\"type\":\"json_object\"}" ) != NULL
		&& strstr( body, "\"temperature\":0.35" ) != NULL
		&& strstr( body, "\"max_tokens\":1110" ) != NULL
		&& body[n - 1] == '}' && body[n] == '\0', body );

	m = SGTC_Dir_BuildRequestJson( "deepseek", "system text", "user text",
		1110, 35, NULL, exact, (int)sizeof( exact ) );
	check( "request_exact_fit", m == n
		&& SGTC_Dir_BuildRequestJson( "deepseek", "system text", "user text",
			1110, 35, NULL, exact, m + 1 ) == m
		&& streq( exact, body ), "cap == length + 1" );

	check( "request_temperature_formats",
		SGTC_Dir_BuildRequestJson( "m", "s", "u", 700, 100, NULL,
			body, (int)sizeof( body ) ) > 0
		&& strstr( body, "\"temperature\":1," ) != NULL
		&& SGTC_Dir_BuildRequestJson( "m", "s", "u", 700, 50, NULL,
			body, (int)sizeof( body ) ) > 0
		&& strstr( body, "\"temperature\":0.5," ) != NULL
		&& SGTC_Dir_BuildRequestJson( "m", "s", "u", 700, 5, NULL,
			body, (int)sizeof( body ) ) > 0
		&& strstr( body, "\"temperature\":0.05," ) != NULL
		&& SGTC_Dir_BuildRequestJson( "m", "s", "u", 700, 0, NULL,
			body, (int)sizeof( body ) ) > 0
		&& strstr( body, "\"temperature\":0," ) != NULL, body );

	n = SGTC_Dir_BuildRequestJson( "m", "s", "u", 700, 35,
		"\"thinking\":{\"type\":\"disabled\"}", body, (int)sizeof( body ) );
	check( "request_extra", n > 0
		&& strstr( body, "\"max_tokens\":700,\"thinking\":{\"type\":\"disabled\"}}" ) != NULL,
		body );

	n = SGTC_Dir_BuildRequestJson( "m", "s", "say \"hi\"\nnow", 700, 35, NULL,
		body, (int)sizeof( body ) );
	check( "request_escape", n > 0
		&& strstr( body, "\"content\":\"say \\\"hi\\\"\\nnow\"}" ) != NULL, body );

	n = SGTC_Dir_BuildRequestJson( "m", "s", "a\x01" "b", 700, 35, NULL,
		body, (int)sizeof( body ) );
	check( "request_control_char", n > 0
		&& strstr( body, "\"content\":\"a\\u0001b\"}" ) != NULL, body );

	check( "request_clamps_tokens",
		SGTC_Dir_BuildRequestJson( "m", "s", "u", 0, 35, NULL,
			body, (int)sizeof( body ) ) > 0
		&& strstr( body, "\"max_tokens\":1}" ) != NULL
		&& SGTC_Dir_BuildRequestJson( "m", "s", "u", 99999, 35, NULL,
			body, (int)sizeof( body ) ) > 0
		&& strstr( body, "\"max_tokens\":8192}" ) != NULL, body );

	n = SGTC_Dir_BuildRequestJson( "m", SGTC_Dir_SystemPrompt(), "u", 700, 35,
		NULL, small, (int)sizeof( small ) );
	check( "request_small_cap", n == -1 && small[sizeof( small ) - 1] == '\0',
		"64-byte cap cannot hold the prompt" );

	check( "request_bad_args",
		SGTC_Dir_BuildRequestJson( "m", "s", "u", 700, 35, NULL, NULL, 64 ) == -1
		&& SGTC_Dir_BuildRequestJson( "m", "s", "u", 700, 35, NULL, small, 0 ) == -1,
		"NULL/cap 0" );
}

static void test_team_rules( void )
{
	sgtc_dir_plan_t plan;
	int cleared;

	memset( &plan, 0, sizeof( plan ) );
	plan.bot_count = 3;
	plan.bots[0].team = 2;
	plan.bots[1].team = 0;
	plan.bots[2].team = 1;
	cleared = SGTC_Dir_EnforceTeamRules( &plan, 0, 2 );
	check( "team_rules_dm", cleared == 2
		&& plan.bots[0].team == 0 && plan.bots[1].team == 0
		&& plan.bots[2].team == 0, "dm clears every team" );

	memset( &plan, 0, sizeof( plan ) );
	plan.bot_count = 4;
	plan.bots[0].team = 2;
	plan.bots[1].team = 1;
	plan.bots[2].team = 2;
	plan.bots[3].team = 1;
	cleared = SGTC_Dir_EnforceTeamRules( &plan, 1, 2 );
	check( "team_rules_two_switches", cleared == 2
		&& plan.bots[0].team == 2 && plan.bots[1].team == 1
		&& plan.bots[2].team == 0 && plan.bots[3].team == 0,
		"at most two team changes survive" );

	cleared = SGTC_Dir_EnforceTeamRules( &plan, 1, -1 );
	check( "team_rules_negative_limit", cleared == 2
		&& plan.bots[0].team == 0 && plan.bots[1].team == 0,
		"negative limit clears the rest" );

	check( "team_rules_null", SGTC_Dir_EnforceTeamRules( NULL, 1, 2 ) == 0, "NULL" );
}

static void test_prune( void )
{
	sgtc_dir_plan_t plan;

	memset( &plan, 0, sizeof( plan ) );
	plan.bot_count = 6;

	// 0: no change at all (the neutral defaults) -> dropped.
	plan.bots[0].bot = 1;
	plan.bots[0].aggression_x100 = 100;
	plan.bots[0].accuracy_x100 = 100;

	// 1..5: one change each.
	plan.bots[1].bot = 2;
	plan.bots[1].team = 2;
	plan.bots[1].aggression_x100 = 100;
	plan.bots[1].accuracy_x100 = 100;

	plan.bots[2].bot = 3;
	plan.bots[2].skill = 4;
	plan.bots[2].aggression_x100 = 100;
	plan.bots[2].accuracy_x100 = 100;

	plan.bots[3].bot = 4;
	plan.bots[3].aggression_x100 = 100;
	plan.bots[3].accuracy_x100 = 100;
	strcpy( plan.bots[3].weapons[0], "weapon_p90" );

	plan.bots[4].bot = 5;
	plan.bots[4].aggression_x100 = 100;
	plan.bots[4].accuracy_x100 = 125;
	strcpy( plan.bots[4].chat[0], "Hold the gate" );

	plan.bots[5].bot = 6;
	plan.bots[5].role = SGTC_DIR_ROLE_DEFEND;
	plan.bots[5].aggression_x100 = 100;
	plan.bots[5].accuracy_x100 = 100;

	check( "prune_noop", SGTC_Dir_PruneNoOpBots( &plan ) == 5
		&& plan.bot_count == 5
		&& plan.bots[0].bot == 2 && plan.bots[1].bot == 3
		&& plan.bots[2].bot == 4 && plan.bots[3].bot == 5
		&& plan.bots[4].bot == 6, "compacted in place" );

	memset( &plan, 0, sizeof( plan ) );
	plan.bot_count = 1;
	plan.bots[0].aggression_x100 = 100;
	plan.bots[0].accuracy_x100 = 100;
	check( "prune_all_noop", SGTC_Dir_PruneNoOpBots( &plan ) == 0
		&& plan.bot_count == 0, "empty plan" );

	check( "prune_null", SGTC_Dir_PruneNoOpBots( NULL ) == 0, "NULL" );
}

int main( void )
{
	test_clamp();
	test_sanitize();
	test_rle();
	test_doc();
	test_elo();
	test_human();
	test_pacing();
	test_chat();
	test_plan_ok();
	test_plan_clamps();
	test_plan_strings();
	test_plan_errors();
	test_extract();
	test_openai();
	test_finish_reason();
	test_weapons();
	test_class_names();
	test_prompt();
	test_max_tokens();
	test_request();
	test_team_rules();
	test_prune();
	test_blend_backoff();
	test_fallback();

	printf( "SUMMARY %d cases, %d failures\n", g_cases, g_fail );
	return g_fail ? 1 : 0;
}
