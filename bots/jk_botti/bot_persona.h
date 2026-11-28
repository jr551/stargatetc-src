//
// JK_Botti - be more human!
//
// bot_persona.h
//
// SGTC: built-in bot names and chat lines. The install ships neither
// addons/jk_botti/jk_botti_names.txt nor jk_botti_chat.txt, so without these
// tables bots are named after their model ("hgrunt", "barney") and never say a
// word. When either file exists it still wins; these are only the fallback.
//
// Engine free: tests/host/bot_persona_harness.cpp compiles bot_persona.cpp on
// its own.

#ifndef BOT_PERSONA_H
#define BOT_PERSONA_H

// Sections of the built-in chat table.
#define BOT_PERSONA_CHAT    0   // idle remarks
#define BOT_PERSONA_TAUNT   1   // after a kill, %n is the victim
#define BOT_PERSONA_WHINE   2   // after a death, %n is the killer
#define BOT_PERSONA_ENDGAME 3   // intermission
#define BOT_PERSONA_GREET   4   // replies to a hello from someone else
#define BOT_PERSONA_THANKS  5   // replies to thanks or praise
#define BOT_PERSONA_GG      6   // replies to a gg
#define BOT_PERSONA_ASKED   7   // someone addressed the bot by name
#define BOT_PERSONA_JOIN    8   // a short hello after joining
#define BOT_PERSONA_SECTIONS 9

// Number of built-in names / lines in a section (0 for an unknown section).
int BotPersona_NameCount( void );
int BotPersona_LineCount( int section );

// The n-th name / line (NULL when n is out of range). Every name fits the
// 31 character bot name limit and every line fits the 80 character chat limit.
const char *BotPersona_Name( int n );
const char *BotPersona_Line( int section, int n );

// What a chat message from another player is about.
#define BOT_PERSONA_TOPIC_NONE   0
#define BOT_PERSONA_TOPIC_GREET  1   // hi, hello, hey, yo ...
#define BOT_PERSONA_TOPIC_GG     2   // gg, good game, wp
#define BOT_PERSONA_TOPIC_THANKS 3   // thanks, thx, ty, nice shot
#define BOT_PERSONA_TOPIC_NAMED  4   // the bot's own name appears

// Classify a chat line. `bot_name` may be NULL. A name match wins over the
// other topics so an address like "hey hgrunt" is answered as being asked.
int BotPersona_Classify( const char *text, const char *bot_name );

#endif // BOT_PERSONA_H
