// Host harness for the bots' built-in names and chat lines
// (bots/jk_botti/bot_persona.cpp).
//
//   g++ -std=c++11 -Wall -Wextra -I<repo>/bots/jk_botti <repo>/tests/host/bot_persona_harness.cpp <repo>/bots/jk_botti/bot_persona.cpp -o bot_persona_harness
//   ./bot_persona_harness

#include <stdio.h>
#include <string.h>

#include "bot_persona.h"

static int s_checks;
static int s_failures;

static void check( const char *name, int ok, const char *detail = "" )
{
	s_checks++;
	if( !ok )
		s_failures++;
	printf( "CASE %-44s %s %s\n", name, ok ? "PASS" : "FAIL", detail );
}

static int safe_name( const char *n )
{
	if( !n || !n[0] )
		return 0;

	for( int i = 0; n[i]; i++ )
	{
		const char c = n[i];

		if( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) )
			return 0;
	}

	return 1;
}

int main( void )
{
	char detail[160];

	// names
	{
		const int n = BotPersona_NameCount();
		int too_long = 0, unsafe = 0, dupes = 0, lower_dupes = 0;

		for( int i = 0; i < n; i++ )
		{
			const char *a = BotPersona_Name( i );

			if( !a || strlen( a ) > 31 )
				too_long++;
			if( !safe_name( a ) )
				unsafe++;

			for( int k = i + 1; k < n; k++ )
			{
				const char *b = BotPersona_Name( k );

				if( a && b && !strcmp( a, b ) )
					dupes++;
				if( a && b && !strcasecmp( a, b ) )
					lower_dupes++;
			}
		}

		snprintf( detail, sizeof( detail ), "%d names", n );
		check( "names.enough_for_a_full_server", n >= 32, detail );
		check( "names.fit_the_limit", too_long == 0 );
		check( "names.plain_characters", unsafe == 0 );
		check( "names.unique", dupes == 0 && lower_dupes == 0 );
		check( "names.out_of_range", BotPersona_Name( -1 ) == NULL && BotPersona_Name( n ) == NULL );

		// none of the old model-name look-alikes
		int modelish = 0;
		static const char *const bad[] = { "hgrunt", "barney", "scientist", "gina", "robo", "superman", "bot" };

		for( int i = 0; i < n; i++ )
			for( size_t k = 0; k < sizeof( bad ) / sizeof( bad[0] ); k++ )
				if( !strcasecmp( BotPersona_Name( i ), bad[k] ) )
					modelish++;

		check( "names.none_look_like_models", modelish == 0 );
	}

	// lines
	{
		static const int need[BOT_PERSONA_SECTIONS] = { 20, 10, 10, 6, 4, 4, 3, 4, 3 };
		static const char *const label[BOT_PERSONA_SECTIONS] = { "chat", "taunt", "whine", "endgame", "greet",
			"thanks", "gg", "asked", "join" };

		for( int sec = 0; sec < BOT_PERSONA_SECTIONS; sec++ )
		{
			const int n = BotPersona_LineCount( sec );
			int too_long = 0, empty = 0, dupes = 0;

			for( int i = 0; i < n; i++ )
			{
				const char *a = BotPersona_Line( sec, i );

				if( !a || !a[0] )
				{
					empty++;
					continue;
				}
				if( strlen( a ) > 80 )
					too_long++;

				for( int k = i + 1; k < n; k++ )
					if( !strcmp( a, BotPersona_Line( sec, k ) ) )
						dupes++;
			}

			char name[64];

			snprintf( name, sizeof( name ), "lines.%s.count", label[sec] );
			snprintf( detail, sizeof( detail ), "%d lines", n );
			check( name, n >= need[sec], detail );
			snprintf( name, sizeof( name ), "lines.%s.clean", label[sec] );
			check( name, empty == 0 && too_long == 0 && dupes == 0 );
		}

		check( "lines.bad_section", BotPersona_LineCount( -1 ) == 0 && BotPersona_LineCount( BOT_PERSONA_SECTIONS ) == 0 );
		check( "lines.bad_index", BotPersona_Line( 0, -1 ) == NULL && BotPersona_Line( 0, 9999 ) == NULL &&
			BotPersona_Line( 99, 0 ) == NULL );

		// %n only where a person is named; idle chat and endgame never use it
		int stray = 0;

		for( int i = 0; i < BotPersona_LineCount( BOT_PERSONA_CHAT ); i++ )
			if( strstr( BotPersona_Line( BOT_PERSONA_CHAT, i ), "%" ) )
				stray++;
		for( int i = 0; i < BotPersona_LineCount( BOT_PERSONA_ENDGAME ); i++ )
			if( strstr( BotPersona_Line( BOT_PERSONA_ENDGAME, i ), "%" ) )
				stray++;
		check( "lines.no_stray_percent", stray == 0 );

		int pct_bad = 0;

		for( int sec = 0; sec < BOT_PERSONA_SECTIONS; sec++ )
			for( int i = 0; i < BotPersona_LineCount( sec ); i++ )
			{
				const char *p = BotPersona_Line( sec, i );

				while( ( p = strchr( p, '%' ) ) != NULL )
				{
					if( p[1] != 'n' )
						pct_bad++;
					p++;
				}
			}
		check( "lines.percent_is_always_n", pct_bad == 0 );
	}

	// classification
	{
		check( "classify.empty", BotPersona_Classify( "", "Kreeper" ) == BOT_PERSONA_TOPIC_NONE );
		check( "classify.null", BotPersona_Classify( NULL, "Kreeper" ) == BOT_PERSONA_TOPIC_NONE );
		check( "classify.hi", BotPersona_Classify( "hi", NULL ) == BOT_PERSONA_TOPIC_GREET );
		check( "classify.hello_all", BotPersona_Classify( "Hello everyone!", NULL ) == BOT_PERSONA_TOPIC_GREET );
		check( "classify.hey_caps", BotPersona_Classify( "HEY", NULL ) == BOT_PERSONA_TOPIC_GREET );
		check( "classify.gg", BotPersona_Classify( "gg", NULL ) == BOT_PERSONA_TOPIC_GG );
		check( "classify.good_game", BotPersona_Classify( "good game guys", NULL ) == BOT_PERSONA_TOPIC_GG );
		check( "classify.gg_wp", BotPersona_Classify( "gg wp", NULL ) == BOT_PERSONA_TOPIC_GG );
		check( "classify.thanks", BotPersona_Classify( "thanks for the heal", NULL ) == BOT_PERSONA_TOPIC_THANKS );
		check( "classify.thx", BotPersona_Classify( "thx", NULL ) == BOT_PERSONA_TOPIC_THANKS );
		check( "classify.nice_shot", BotPersona_Classify( "nice shot", NULL ) == BOT_PERSONA_TOPIC_THANKS );
		check( "classify.named", BotPersona_Classify( "Kreeper where are you", "Kreeper" ) == BOT_PERSONA_TOPIC_NAMED );
		check( "classify.named_case", BotPersona_Classify( "oi kreeper", "Kreeper" ) == BOT_PERSONA_TOPIC_NAMED );
		check( "classify.name_inside_word_ignored", BotPersona_Classify( "Kreepers are everywhere", "Kreeper" ) == BOT_PERSONA_TOPIC_NONE );
		check( "classify.name_with_underscore", BotPersona_Classify( "hold on gate_keeper", "gate_keeper" ) == BOT_PERSONA_TOPIC_NAMED );
		check( "classify.greeting_to_named_bot", BotPersona_Classify( "hey Kreeper", "Kreeper" ) == BOT_PERSONA_TOPIC_GREET );
		check( "classify.not_hi_inside_words", BotPersona_Classify( "this is high up", NULL ) == BOT_PERSONA_TOPIC_NONE );
		check( "classify.not_gg_inside_words", BotPersona_Classify( "eggs", NULL ) == BOT_PERSONA_TOPIC_NONE );
		check( "classify.not_yo_inside_words", BotPersona_Classify( "you there", NULL ) == BOT_PERSONA_TOPIC_NONE );
		check( "classify.plain_chat", BotPersona_Classify( "the gate is open", NULL ) == BOT_PERSONA_TOPIC_NONE );
		check( "classify.name_not_given", BotPersona_Classify( "Kreeper", NULL ) == BOT_PERSONA_TOPIC_NONE );
		check( "classify.gg_beats_greeting", BotPersona_Classify( "hi gg", NULL ) == BOT_PERSONA_TOPIC_GG );
	}

	printf( "%d checks, %d failures\n", s_checks, s_failures );
	return s_failures ? 1 : 0;
}
