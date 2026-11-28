//
// JK_Botti - be more human!
//
// bot_persona.cpp
//
// SGTC: built-in bot names and chat lines, see bot_persona.h. No engine
// headers.
//

#include <string.h>

#include "bot_persona.h"

// Gamer handles: a mix of cases, digits, underscores and the odd Stargate nod,
// so a scoreboard of bots does not read like a list of model names. All of
// them are 31 characters or shorter and use letters, digits and underscores.
static const char *const s_names[] =
{
	"Kreeper", "darkmatter", "ZatMan", "ringo_", "chevron7", "gate_keeper",
	"JaffaJoe", "tokra_tim", "SGC_rookie", "iris_closed", "DHD_dan", "kree_kree",
	"naquadah_nick", "Vex", "shol_va", "t3chno", "Marauder", "fragmaster_k",
	"Wraith9", "Lowlight", "xX_Anubis_Xx", "bigmike", "Hoodoo", "pixel_pete",
	"nomad", "Ghostwalker", "rustbucket", "Tanya", "Quillon", "ratchet",
	"Dr_Frag", "sk8rboi", "Snipe", "mugwump", "oldschool", "Grim",
	"Lt_Blaze", "cobra_77", "Zed", "OneShot", "Tinker", "hammerfall",
	"blue_moon", "Nightowl", "KillSwitch", "dusty", "Baron", "Cutter",
	"lazer_pointer", "StaffMaster", "gatecrasher", "Hex", "Mongoose", "salty_sam",
	"Bravo6", "wolfpack", "Ironside", "jinx", "Rocco", "Sparks",
	"Phantom_J", "tango", "Yeti", "ZeroCool"
};

static const char *const s_chat[] =
{
	"where is everyone",
	"this map is huge",
	"anyone got a medic",
	"i need ammo",
	"watch the gate",
	"gate room is a deathtrap",
	"is the iris closed",
	"i keep dying at the rings lol",
	"nice map",
	"cant find anyone",
	"lag?",
	"my aim is trash today",
	"did anyone else hear that",
	"hold the gate",
	"jaffa on the left",
	"someone heal me pls",
	"on my way",
	"cover me",
	"holding here",
	"good game so far",
	"this round is close",
	"who took my kit",
	"does the tac thing actually work",
	"that staff weapon hurts",
	"i miss the p90",
	"anyone seen the enemy",
	"careful they are pushing",
	"im out of zat",
	"nobody is covering the rings",
	"push now",
	"fall back for a sec",
	"brb",
	"thats a lot of naquadah",
	"enemy at the gate",
	"who has the objective",
	"too many of them here",
	"need backup",
	"stay together",
	"i go left you go right",
	"clear here"
};

static const char *const s_taunt[] =
{
	"%n got rekt",
	"nice try %n",
	"ez",
	"sit down %n",
	"ha %n",
	"too slow %n",
	"gg %n",
	"thanks for the kill %n",
	"that was close",
	"lol",
	"%n you ok?",
	"bye %n",
	"oops %n",
	"kree!",
	"shol'va %n",
	"got him",
	"one down",
	"easy",
	"%n walked into that",
	"not today %n"
};

static const char *const s_whine[] =
{
	"%n is cheating",
	"wow %n",
	"lucky shot",
	"lag",
	"bs",
	"how did you see me",
	"wtf",
	"ffs",
	"i had him",
	"nooo",
	"nice one %n",
	"ow",
	"%n camper",
	"come on",
	"that was my kill",
	"thats not fair",
	"where did that come from",
	"ok %n you win that one",
	"unreal",
	"my shield was down"
};

static const char *const s_endgame[] =
{
	"gg",
	"gg wp",
	"good game",
	"gg all",
	"well played",
	"gg ez",
	"that was fun",
	"rematch?",
	"close one",
	"gg next map",
	"gg guys",
	"wp everyone"
};

static const char *const s_greet[] =
{
	"hey",
	"hi",
	"hello",
	"yo",
	"sup",
	"hey there",
	"o/",
	"hi all"
};

static const char *const s_thanks[] =
{
	"np",
	"thanks",
	"ty",
	"thx",
	"no problem",
	"cheers",
	"anytime",
	"you too"
};

static const char *const s_gg[] =
{
	"gg",
	"gg wp",
	"gg all",
	"good game",
	"wp",
	"ggs"
};

static const char *const s_asked[] =
{
	"what",
	"huh",
	"?",
	"yeah?",
	"what is it",
	"im here",
	"lol",
	"busy rn",
	"say again",
	"one sec"
};

static const char *const s_join[] =
{
	"hi",
	"hey all",
	"hello",
	"yo",
	"hi guys",
	"o/"
};

#define COUNT_OF( a ) ( (int)( sizeof( a ) / sizeof( a[0] ) ) )

struct section_t
{
	const char *const *lines;
	int count;
};

static const section_t s_sections[BOT_PERSONA_SECTIONS] =
{
	{ s_chat, COUNT_OF( s_chat ) },
	{ s_taunt, COUNT_OF( s_taunt ) },
	{ s_whine, COUNT_OF( s_whine ) },
	{ s_endgame, COUNT_OF( s_endgame ) },
	{ s_greet, COUNT_OF( s_greet ) },
	{ s_thanks, COUNT_OF( s_thanks ) },
	{ s_gg, COUNT_OF( s_gg ) },
	{ s_asked, COUNT_OF( s_asked ) },
	{ s_join, COUNT_OF( s_join ) }
};

int BotPersona_NameCount( void )
{
	return COUNT_OF( s_names );
}

int BotPersona_LineCount( int section )
{
	if( section < 0 || section >= BOT_PERSONA_SECTIONS )
		return 0;

	return s_sections[section].count;
}

const char *BotPersona_Name( int n )
{
	if( n < 0 || n >= COUNT_OF( s_names ) )
		return NULL;

	return s_names[n];
}

const char *BotPersona_Line( int section, int n )
{
	if( section < 0 || section >= BOT_PERSONA_SECTIONS )
		return NULL;

	if( n < 0 || n >= s_sections[section].count )
		return NULL;

	return s_sections[section].lines[n];
}

// ---------------------------------------------------------------------------
// classification of other players' chat
// ---------------------------------------------------------------------------

static int pers_lower( int c )
{
	return ( c >= 'A' && c <= 'Z' ) ? c + ( 'a' - 'A' ) : c;
}

static int pers_word_char( int c )
{
	return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) ||
		( c >= '0' && c <= '9' ) || c == '_';
}

// True when `needle` appears in `text` as a whole word, ignoring case.
static int pers_has_word( const char *text, const char *needle )
{
	const int nlen = (int)strlen( needle );

	if( nlen == 0 )
		return 0;

	for( int i = 0; text[i]; i++ )
	{
		int k = 0;

		if( i > 0 && pers_word_char( (unsigned char)text[i - 1] ) )
			continue;

		while( k < nlen && text[i + k] &&
			pers_lower( (unsigned char)text[i + k] ) == pers_lower( (unsigned char)needle[k] ) )
			k++;

		if( k == nlen && !pers_word_char( (unsigned char)text[i + k] ) )
			return 1;
	}

	return 0;
}

static int pers_has_any( const char *text, const char *const *words, int count )
{
	for( int i = 0; i < count; i++ )
		if( pers_has_word( text, words[i] ) )
			return 1;

	return 0;
}

int BotPersona_Classify( const char *text, const char *bot_name )
{
	static const char *const gg_words[] = { "gg", "ggs", "ggwp", "wp", "gg wp", "good game", "well played" };
	static const char *const thanks_words[] = { "thanks", "thx", "ty", "thank you", "nice shot", "nice one",
		"good shot", "ns", "nice kill", "well done" };
	static const char *const greet_words[] = { "hi", "hello", "hey", "yo", "sup", "hiya", "howdy", "hallo",
		"o/", "hi all", "hi guys" };

	if( !text || !text[0] )
		return BOT_PERSONA_TOPIC_NONE;

	if( pers_has_any( text, gg_words, (int)( sizeof( gg_words ) / sizeof( gg_words[0] ) ) ) )
		return BOT_PERSONA_TOPIC_GG;

	if( pers_has_any( text, thanks_words, (int)( sizeof( thanks_words ) / sizeof( thanks_words[0] ) ) ) )
		return BOT_PERSONA_TOPIC_THANKS;

	if( pers_has_any( text, greet_words, (int)( sizeof( greet_words ) / sizeof( greet_words[0] ) ) ) )
		return BOT_PERSONA_TOPIC_GREET;

	if( bot_name && bot_name[0] && pers_has_word( text, bot_name ) )
		return BOT_PERSONA_TOPIC_NAMED;

	return BOT_PERSONA_TOPIC_NONE;
}
