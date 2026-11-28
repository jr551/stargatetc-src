//
// JK_Botti - be more human!
//
// bot_chat.cpp
//

#include <string.h>

#include <extdll.h>
#include <dllapi.h>
#include <h_export.h>
#include "botti_meta.h"

#include "bot.h"
#include "bot_func.h"
#include "bot_persona.h"


#define NUM_TAGS 24
const char *tag1[NUM_TAGS]={
"[", "*[", "-=","-[","-]","-}","-{","<[","<]","[-","]-","{-","}-","[[","[","{","]","}","<",">","-","|","=","+"};
const char *tag2[NUM_TAGS]={
"]*", "]*", "=-","]-","[-","{-","}-","]>","[>","-]","-[","-}","-{","]]","]","}","[","{",">","<","-","|","=","+"};


int bot_chat_count;
int bot_taunt_count;
int bot_whine_count;
int bot_endgame_count;

bot_chat_t bot_chat[MAX_BOT_CHAT];
bot_chat_t bot_taunt[MAX_BOT_CHAT];
bot_chat_t bot_whine[MAX_BOT_CHAT];
bot_chat_t bot_endgame[MAX_BOT_CHAT];

int recent_bot_chat[5];
int recent_bot_taunt[5];
int recent_bot_whine[5];
int recent_bot_endgame[5];

int player_count;
char player_names[32][33];  // 32 players max, 32 chars + null

extern int bot_chat_tag_percent;
extern int bot_chat_drop_percent;
extern int bot_chat_swap_percent;
extern int bot_chat_lower_percent;


// SGTC: shared pacing so a room of bots does not talk over each other. Each
// message takes the next free slot, 2.5 s after the previous one.
static float g_chat_next_slot = 0.0f;
static float g_chat_next_hear = 0.0f;
static int recent_persona[BOT_PERSONA_SECTIONS][5];

static void LoadBotChatAddMessage(bot_chat_t *array, int *count, const char *buffer, int length);

static void LoadBotChatPersona(bot_chat_t *array, int *count, int section)
{
   if (*count > 0)
      return;

   for (int n = 0; n < BotPersona_LineCount(section); n++)
   {
      const char *line = BotPersona_Line(section, n);

      if (line)
         LoadBotChatAddMessage(array, count, line, (int)strlen(line));
   }
}

static void LoadBotChatAddMessage(bot_chat_t *array, int *count, const char *buffer, int length)
{
   if ((length > 0) && !(buffer[0] == '!' && length == 1) &&
       (*count < MAX_BOT_CHAT))
   {
      if (buffer[0] == '!')
      {
         safe_strcopy(array[*count].text, sizeof(array[*count].text), &buffer[1]);
         array[*count].can_modify = FALSE;
      }
      else
      {
         safe_strcopy(array[*count].text, sizeof(array[*count].text), buffer);
         array[*count].can_modify = TRUE;
      }

      (*count)++;
   }
}


//
void LoadBotChat(void)
{
   FILE *bfp;
   char filename[256];
   char buffer[256];
   char *stat;
   int section = -1;
   int i, length;

   bot_chat_count = 0;
   bot_taunt_count = 0;
   bot_whine_count = 0;
   bot_endgame_count = 0;

   for (i=0; i < 5; i++)
   {
      recent_bot_chat[i] = -1;
      recent_bot_taunt[i] = -1;
      recent_bot_whine[i] = -1;
      recent_bot_endgame[i] = -1;
   }

   UTIL_BuildFileName_N(filename, sizeof(filename), "addons/jk_botti/jk_botti_chat.txt", NULL);

   bfp = fopen(filename, "r");

   if (bfp != NULL)
      UTIL_ConsolePrintf("Loading %s...\n", filename);

   while (bfp != NULL)
   {
      stat = fgets(buffer, 80, bfp);

      if (stat == NULL)
      {
         fclose(bfp);
         bfp = NULL;
         continue;
      }

      buffer[80] = 0;  // truncate lines longer than 80 characters

      length = strlen(buffer);

      if (buffer[length-1] == '\n')
      {
         buffer[length-1] = 0;  // remove '\n'
         length--;
      }

      if (strcmp(buffer, "[bot_chat]") == 0)
      {
         section = 0;
         continue;
      }

      if (strcmp(buffer, "[bot_taunt]") == 0)
      {
         section = 1;
         continue;
      }

      if (strcmp(buffer, "[bot_whine]") == 0)
      {
         section = 2;
         continue;
      }

      if (strcmp(buffer, "[bot_endgame]") == 0)
      {
         section = 3;
         continue;
      }

      if (section == 0)
         LoadBotChatAddMessage(bot_chat, &bot_chat_count, buffer, length);
      else if (section == 1)
         LoadBotChatAddMessage(bot_taunt, &bot_taunt_count, buffer, length);
      else if (section == 2)
         LoadBotChatAddMessage(bot_whine, &bot_whine_count, buffer, length);
      else if (section == 3)
         LoadBotChatAddMessage(bot_endgame, &bot_endgame_count, buffer, length);
   }

   // SGTC: the install ships no chat file, so the bots never said a word.
   // Whatever section the file left empty is filled from the built-in lines.
   LoadBotChatPersona(bot_chat, &bot_chat_count, BOT_PERSONA_CHAT);
   LoadBotChatPersona(bot_taunt, &bot_taunt_count, BOT_PERSONA_TAUNT);
   LoadBotChatPersona(bot_whine, &bot_whine_count, BOT_PERSONA_WHINE);
   LoadBotChatPersona(bot_endgame, &bot_endgame_count, BOT_PERSONA_ENDGAME);

   for (i=0; i < BOT_PERSONA_SECTIONS; i++)
      for (int k=0; k < 5; k++)
         recent_persona[i][k] = -1;

   g_chat_next_slot = 0.0f;
   g_chat_next_hear = 0.0f;
}


static void BotTrimBlanks(const char *in_string, char *out_string, int sizeof_out_string)
{
   int i, pos;
   char *dest;

   pos=0;
   while ((pos < sizeof_out_string) && (in_string[pos] == ' '))  // skip leading blanks
      pos++;

   dest=&out_string[0];

   while ((pos < sizeof_out_string) && (in_string[pos]))
   {
      *dest++ = in_string[pos];
      pos++;
   }
   *dest = 0;  // store the null

   i = strlen(out_string) - 1;
   while ((i > 0) && (out_string[i] == ' '))  // remove trailing blanks
   {
      out_string[i] = 0;
      i--;
   }
}


static int BotChatTrimTag(const char *original_name, char *out_name, int sizeof_out_name)
{
   int i;
   char *pos1, *pos2, *src, *dest;
   char in_name[80];
   int result = 0;

   safe_strcopy(in_name, sizeof(in_name), original_name);

   for (i=0; i < NUM_TAGS; i++)
   {
      pos1=strstr(in_name, tag1[i]);
      if (pos1)
         pos2=strstr(pos1+strlen(tag1[i]), tag2[i]);
      else
         pos2 = NULL;

      if (pos1 && pos2 && pos1 < pos2)
      {
         src = pos2+strlen(tag2[i]);
         dest = pos1;
         while (*src)
            *dest++ = *src++;
         *dest = *src;  // copy the null;

         result = 1;
      }
   }

   safe_strcopy(out_name, sizeof_out_name, in_name);

   BotTrimBlanks(out_name, in_name, sizeof(in_name));

   if (UTIL_IsStringEmpty(in_name))  // is name just a tag?
   {
      safe_strcopy(in_name, sizeof(in_name), original_name);

      /*
      // strip just the tag part...
      for (i=0; i < NUM_TAGS; i++)
      {
         pos1=strstr(in_name, tag1[i]);
         if (pos1)
            pos2=strstr(pos1+strlen(tag1[i]), tag2[i]);
         else
            pos2 = NULL;

         if (pos1 && pos2 && pos1 < pos2)
         {
            src = pos1 + strlen(tag1[i]);
            dest = pos1;
            while (*src)
               *dest++ = *src++;
            *dest = *src;  // copy the null;

            src = pos2 - strlen(tag2[i]);
            *src = 0; // null out the rest of the string
         }
      }
      */
      result = 0;
   }

   BotTrimBlanks(in_name, out_name, sizeof_out_name);

   out_name[sizeof_out_name-1] = 0;

   return (result);
}


static void BotDropCharacter(const char *in_string, char *out_string, int sizeof_out_string)
{
   int len, pos;
   int count;
   char *src, *dest;
   qboolean is_bad;

   safe_strcopy(out_string, sizeof_out_string, in_string);

   len = strlen(out_string);
   if (len < 2)
      return;

   pos = RANDOM_LONG2(1, len-1);  // don't drop position zero

   is_bad = !isalpha(out_string[pos]) || (out_string[pos-1] == '%');
   for (count = 0; is_bad && count < len && count < 20; count++)
   {
      pos = RANDOM_LONG2(1, len-1);
      is_bad = !isalpha(out_string[pos]) || (out_string[pos-1] == '%');
   }

   if (count < len && count < 20)
   {
      src = &out_string[pos+1];
      dest = &out_string[pos];
      while (*src)
         *dest++ = *src++;
      *dest = *src;  // copy the null;
   }
}


static void BotSwapCharacter(const char *in_string, char *out_string, int sizeof_out_string)
{
   int len, pos;
   int count = 0;
   char temp;
   qboolean is_bad;

   safe_strcopy(out_string, sizeof_out_string, in_string);

   len = strlen(out_string);
   if (len < 3) // must be 3, 1+1 for swap + zero must now swap = 3
      return;

   pos = RANDOM_LONG2(1, len-2);  // don't swap position zero

   is_bad = !isalpha(out_string[pos]) || !isalpha(out_string[pos+1]) || (out_string[pos-1] == '%');
   for (count = 0; is_bad && count < len && count < 20; count++)
   {
      pos = RANDOM_LONG2(1, len-2);
      is_bad = !isalpha(out_string[pos]) || !isalpha(out_string[pos+1]) || (out_string[pos-1] == '%');
   }

   if (count < len && count < 20)
   {
      temp = out_string[pos];
      out_string[pos] = out_string[pos+1];
      out_string[pos+1] = temp;
   }
}


static void BotChatName(const char *original_name, char *out_name, int sizeof_out_name)
{
   int pos;
   char temp_lvlXless_name[80];

   //always remove [lvlX] tag
   if (strncmp(original_name, "[lvl", 4) == 0 && original_name[4] >= '0' && original_name[4] <= '5' && original_name[5] == ']')
   {
      safe_strcopy(temp_lvlXless_name, sizeof(temp_lvlXless_name), &original_name[6]);
      original_name = temp_lvlXless_name;
   }

   if (RANDOM_LONG2(1, 100) <= bot_chat_tag_percent)
   {
      char temp_name[80];

      safe_strcopy(temp_name, sizeof(temp_name), original_name);

      while (BotChatTrimTag(temp_name, out_name, sizeof_out_name))
      {
         safe_strcopy(temp_name, sizeof(temp_name), out_name);
      }
   }
   else
   {
      safe_strcopy(out_name, sizeof_out_name, original_name);
   }

   if (RANDOM_LONG2(1, 100) <= bot_chat_lower_percent)
   {
      pos=0;
      while ((pos < sizeof_out_name) && (out_name[pos]))
      {
         out_name[pos] = tolower(out_name[pos]);
         pos++;
      }
   }
}


static void BotChatText(const char *in_text, char *out_text, int sizeof_out_text)
{
   int pos;
   char temp_text[81];
   int count;

   safe_strcopy(temp_text, sizeof(temp_text), in_text);

   if (RANDOM_LONG2(1, 100) <= bot_chat_drop_percent)
   {
      count = RANDOM_LONG2(1, 3);

      while (count)
      {
         BotDropCharacter(temp_text, out_text, sizeof_out_text);
         safe_strcopy(temp_text, sizeof(temp_text), out_text);
         count--;
      }
   }

   if (RANDOM_LONG2(1, 100) <= bot_chat_swap_percent)
   {
      count = RANDOM_LONG2(1, 2);

      while (count)
      {
         BotSwapCharacter(temp_text, out_text, sizeof_out_text);
         safe_strcopy(temp_text, sizeof(temp_text), out_text);
         count--;
      }
   }

   if (RANDOM_LONG2(1, 100) <= bot_chat_lower_percent)
   {
      pos=0;
      while (temp_text[pos])
      {
         temp_text[pos] = tolower(temp_text[pos]);
         pos++;
      }
   }

   safe_strcopy(out_text, sizeof_out_text, temp_text);
}


static void BotChatGetPlayers(void)
{
   int index;
   const char *pName;

   player_count = 0;

   for (index = 1; index <= gpGlobals->maxClients; index++)
   {
      edict_t *pPlayer = INDEXENT(index);

      // skip invalid players
      if ((pPlayer) && (!pPlayer->free) && !FBitSet(pPlayer->v.flags, FL_PROXY))
      {
         if (pPlayer->v.netname)
         {
            pName = STRING(pPlayer->v.netname);

            if (*pName != 0)
            {
               safe_strcopy(player_names[player_count], sizeof(player_names[player_count]), pName);

               player_count++;
            }
         }
      }
   }
}


static void BotChatFillInName(char *bot_say_msg, int sizeof_msg, const char *chat_text, const char *chat_name, const char *raw_chat_name, const char *bot_name)
{
   char random_name[64];
   int clen = strlen(chat_text);
   int i = 0;
   int o = 0;

   while (i < clen && o < sizeof_msg)
   {
      if (chat_text[i] == '%')
      {
         if (i + 1 < clen)
         {
            if (chat_text[i + 1] == 'n' || chat_text[i + 1] == 'r')
            {
               const char * to_output = chat_name;

               if (chat_text[i + 1] == 'r')
               {
                  BotChatGetPlayers();

                  if (player_count > 0)
                  {
                     // pick a name at random from the list of players...
                     int index = RANDOM_LONG2(0, player_count-1);
                     int count = 0;

                     bool is_bad = (strcmp(player_names[index], raw_chat_name) == 0) ||
                                   (strcmp(player_names[index], bot_name) == 0);

                     while ((is_bad) && (count < 20))
                     {
                        index = RANDOM_LONG2(0, player_count-1);

                        is_bad = (strcmp(player_names[index], raw_chat_name) == 0) ||
                                 (strcmp(player_names[index], bot_name) == 0);

                        count++;
                     }

                     BotChatName(player_names[index], random_name, sizeof(random_name));

                     to_output = random_name;
                  }
               }

               // copy chat name to output
               int nlen = strlen(to_output);
               int n = 0;

               while (n < nlen && o < sizeof_msg)
                  bot_say_msg[o++] = to_output[n++];

               // skip %X
               i+=2;
               continue;
            }
         }
      }

      bot_say_msg[o++] = chat_text[i++];
   }

   if (o < sizeof_msg)
      bot_say_msg[o] = 0;
   else
      bot_say_msg[sizeof_msg - 1] = 0;
}


static int BotChatPickRandom(int msg_count, int *recent)
{
   int index;
   int recent_count = 0;

   while (recent_count < 5)
   {
      index = RANDOM_LONG2(0, msg_count-1);

      qboolean used = FALSE;

      for (int i = 0; i < 5; i++)
      {
         if (recent[i] == index)
            used = TRUE;
      }

      if (used)
         recent_count++;
      else
         break;
   }

   for (int i = 4; i > 0; i--)
      recent[i] = recent[i-1];

   recent[0] = index;

   return index;
}


static void BotChatGetText(const bot_chat_t *msg, char *chat_text, int sizeof_chat_text)
{
   if (msg->can_modify)
      BotChatText(msg->text, chat_text, sizeof_chat_text);
   else
      safe_strcopy(chat_text, sizeof_chat_text, msg->text);
}


// SGTC: queue the message already built in bot_say_msg. The bot takes the next
// free slot (messages from different bots never land closer than 2.5 s) and
// types at a person's pace, 0.12-0.2 s a character after its reaction time.
static void BotChatSchedule(bot_t &pBot, float lead_min, float lead_max)
{
   float t = gpGlobals->time + RANDOM_FLOAT2(lead_min, lead_max) +
      strlen(pBot.bot_say_msg) * RANDOM_FLOAT2(0.12, 0.2);

   // gpGlobals->time restarts on a new map: a slot from the last one would
   // otherwise silence every bot until it came round again
   if (g_chat_next_slot > gpGlobals->time + 60.0f)
      g_chat_next_slot = 0.0f;

   if (t < g_chat_next_slot)
      t = g_chat_next_slot;

   g_chat_next_slot = t + 2.5f;

   pBot.b_bot_say = TRUE;
   pBot.f_bot_say = t;
}


// SGTC: say one of the built-in lines of `section`, optionally addressed to
// `target` (the %n in the line). Returns FALSE when the bot is already about to
// say something.
static qboolean BotChatSayPersona(bot_t &pBot, int section, edict_t *target, float lead_min, float lead_max)
{
   char chat_text[81];
   char chat_name[64];
   char raw_name[64];
   const int count = BotPersona_LineCount(section);

   if (count <= 0)
      return FALSE;

   if (pBot.b_bot_say && pBot.f_bot_say >= gpGlobals->time)
      return FALSE;

   const int index = BotChatPickRandom(count, recent_persona[section]);
   const char *line = BotPersona_Line(section, index);

   if (!line)
      return FALSE;

   BotChatText(line, chat_text, sizeof(chat_text));

   raw_name[0] = 0;
   if (!FNullEnt(target) && target->v.netname)
      safe_strcopy(raw_name, sizeof(raw_name), STRING(target->v.netname));

   if (raw_name[0])
      BotChatName(raw_name, chat_name, sizeof(chat_name));
   else
      safe_strcopy(chat_name, sizeof(chat_name), "all");

   BotChatFillInName(pBot.bot_say_msg, sizeof(pBot.bot_say_msg), chat_text, chat_name, raw_name,
      STRING(pBot.pEdict->v.netname));

   BotChatSchedule(pBot, lead_min, lead_max);

   return TRUE;
}


// taunt on killed player
void BotChatTaunt(bot_t &pBot, edict_t *victim_edict)
{
   char chat_text[81];
   char chat_name[64];
   char temp_name[64];
   const char *bot_name;

   if (pBot.b_bot_say && pBot.f_bot_say >= gpGlobals->time)
      return;

   // are there any taunt messages and should the bot taunt?
   if ((bot_taunt_count > 0) &&
       (RANDOM_LONG2(1,100) <= pBot.taunt_percent))
   {
      int taunt_index = BotChatPickRandom(bot_taunt_count, recent_bot_taunt);

      BotChatGetText(&bot_taunt[taunt_index], chat_text, sizeof(chat_text));

      if (victim_edict->v.netname)
      {
         safe_strcopy(temp_name, sizeof(temp_name), STRING(victim_edict->v.netname));

         BotChatName(temp_name, chat_name, sizeof(chat_name));
      }
      else
         safe_strcopy(chat_name, sizeof(chat_name), "NULL");

      bot_name = STRING(pBot.pEdict->v.netname);

      BotChatFillInName(pBot.bot_say_msg, sizeof(pBot.bot_say_msg), chat_text, chat_name, temp_name, bot_name);

      // queue it: reaction time, then typing, behind any other bot's message
      BotChatSchedule(pBot, 0.5, 1.0);
   }
}


// did another player kill this bot AND bot whine messages loaded AND
void BotChatWhine(bot_t &pBot)
{
   char chat_text[81];
   char chat_name[64];
   char temp_name[64];
   const char *bot_name;

   if (pBot.b_bot_say && pBot.f_bot_say >= gpGlobals->time)
      return;

   edict_t *pEdict = pBot.pEdict;

   // has the bot been alive for at least 15 seconds AND
   if ((pBot.killer_edict != NULL) && (bot_whine_count > 0) &&
       ((pBot.f_bot_spawn_time + 15.0) <= gpGlobals->time))
   {
      if ((RANDOM_LONG2(1,100) <= pBot.whine_percent))
      {
         int whine_index = BotChatPickRandom(bot_whine_count, recent_bot_whine);

         BotChatGetText(&bot_whine[whine_index], chat_text, sizeof(chat_text));

         if (pBot.killer_edict->v.netname)
         {
            safe_strcopy(temp_name, sizeof(temp_name), STRING(pBot.killer_edict->v.netname));

            BotChatName(temp_name, chat_name, sizeof(chat_name));
         }
         else
            safe_strcopy(chat_name, sizeof(chat_name), "NULL");

         bot_name = STRING(pEdict->v.netname);

         BotChatFillInName(pBot.bot_say_msg, sizeof(pBot.bot_say_msg), chat_text, chat_name, temp_name, bot_name);

         // queue it: reaction time, then typing, behind any other bot's message
         BotChatSchedule(pBot, 0.5, 1.0);
      }
   }
}


static void BotChatJoinGreeting(bot_t &pBot);

// just say something
void BotChatTalk(bot_t &pBot)
{
   char chat_text[81];
   char chat_name[64];
   const char *bot_name;

   if (pBot.b_bot_say && pBot.f_bot_say >= gpGlobals->time)
      return;

   edict_t *pEdict = pBot.pEdict;

   // SGTC: the once-per-spawn hello for a person already in the game
   BotChatJoinGreeting(pBot);

   if (pBot.b_bot_say && pBot.f_bot_say >= gpGlobals->time)
      return;

   if ((bot_chat_count > 0) && (pBot.f_bot_chat_time < gpGlobals->time))
   {
      if (RANDOM_LONG2(1,100) <= pBot.chat_percent)
      {
         // a person says something every few minutes, not every 30 seconds
         pBot.f_bot_chat_time = gpGlobals->time + RANDOM_FLOAT2(120.0f, 360.0f);

         int chat_index = BotChatPickRandom(bot_chat_count, recent_bot_chat);

         BotChatGetText(&bot_chat[chat_index], chat_text, sizeof(chat_text));

         safe_strcopy(chat_name, sizeof(chat_name), STRING(pBot.pEdict->v.netname));

         bot_name = STRING(pEdict->v.netname);

         BotChatFillInName(pBot.bot_say_msg, sizeof(pBot.bot_say_msg), chat_text, chat_name, chat_name, bot_name);

         // queue it: reaction time, then typing, behind any other bot's message
         BotChatSchedule(pBot, 0.5, 1.0);
      }
   }
}


// endgame say
void BotChatEndGame(bot_t &pBot)
{
   char chat_text[81];
   char chat_name[64];
   const char *bot_name;

   edict_t *pEdict = pBot.pEdict;

   if ((bot_endgame_count > 0) && RANDOM_LONG2(1,100) <= pBot.endgame_percent)
   {
      int endgame_index = BotChatPickRandom(bot_endgame_count, recent_bot_endgame);

      BotChatGetText(&bot_endgame[endgame_index], chat_text, sizeof(chat_text));

      safe_strcopy(chat_name, sizeof(chat_name), STRING(pBot.pEdict->v.netname));

      bot_name = STRING(pEdict->v.netname);

      BotChatFillInName(pBot.bot_say_msg, sizeof(pBot.bot_say_msg), chat_text, chat_name, chat_name, bot_name);

      // queue it: reaction time, then typing, behind any other bot's message
      BotChatSchedule(pBot, 0.3, 2.0);
   }
}


// SGTC: a bot that spawns while a person is on the server sometimes says hello.
// Called from BotChatTalk once per spawn.
static void BotChatJoinGreeting(bot_t &pBot)
{
   if (pBot.b_chat_greeted)
      return;

   if ((gpGlobals->time - pBot.f_bot_spawn_time) < 3.0f)
      return;

   pBot.b_chat_greeted = TRUE;

   int humans = 0;

   for (int i = 1; i <= gpGlobals->maxClients; i++)
   {
      edict_t *pPlayer = INDEXENT(i);

      if (pPlayer && !pPlayer->free && FBitSet(pPlayer->v.flags, FL_CLIENT) &&
          !FBitSet(pPlayer->v.flags, FL_FAKECLIENT | FL_THIRDPARTYBOT | FL_PROXY))
         humans++;
   }

   if (humans > 0 && RANDOM_LONG2(1, 100) <= 25)
      BotChatSayPersona(pBot, BOT_PERSONA_JOIN, NULL, 2.0f, 8.0f);
}


// SGTC: a person came in. One bot, now and then, says hello to them by name.
void BotChatHumanJoined(edict_t *human)
{
   if (FNullEnt(human))
      return;

   if (gpGlobals->time < g_chat_next_hear)
      return;

   if (RANDOM_LONG2(1, 100) > 60)
      return;

   int pick[32];
   int n = 0;

   for (int i = 0; i < 32; i++)
      if (bots[i].is_used && !FNullEnt(bots[i].pEdict) && !(bots[i].b_bot_say && bots[i].f_bot_say >= gpGlobals->time))
         pick[n++] = i;

   if (n == 0)
      return;

   g_chat_next_hear = gpGlobals->time + 5.0f;

   BotChatSayPersona(bots[pick[RANDOM_LONG2(0, n - 1)]], BOT_PERSONA_GREET, human, 4.0f, 10.0f);
}


// SGTC: a person said `text` in chat. The bots answer the way people do: a
// hello gets a hello, a gg gets a gg, thanks gets "np", and a bot that is
// called by name says something back. One reply per message at most, and not
// more often than every few seconds, so a room of bots never answers in a chorus.
//
// `speaker` NULL stands for a person (sgtc_bot_hear, headless tests).
void BotChatHear(edict_t *speaker, const char *text)
{
   if (!text || !text[0])
      return;

   if (!FNullEnt(speaker) && FBitSet(speaker->v.flags, FL_FAKECLIENT | FL_THIRDPARTYBOT))
      return;

   if (gpGlobals->time < g_chat_next_hear)
      return;

   if (g_chat_next_hear > gpGlobals->time + 60.0f)
      g_chat_next_hear = 0.0f;

   const int topic = BotPersona_Classify(text, NULL);
   int named = -1;
   int pick[32];
   int n = 0;

   for (int i = 0; i < 32; i++)
   {
      if (!bots[i].is_used || FNullEnt(bots[i].pEdict))
         continue;

      if (bots[i].b_bot_say && bots[i].f_bot_say >= gpGlobals->time)
         continue;

      if (BotPersona_Classify(text, STRING(bots[i].pEdict->v.netname)) == BOT_PERSONA_TOPIC_NAMED)
         named = i;

      pick[n++] = i;
   }

   if (n == 0)
      return;

   int section = -1;
   int chance = 100;

   switch (topic)
   {
      case BOT_PERSONA_TOPIC_GREET:  section = BOT_PERSONA_GREET;  chance = 70; break;
      case BOT_PERSONA_TOPIC_GG:     section = BOT_PERSONA_GG;     chance = 70; break;
      case BOT_PERSONA_TOPIC_THANKS: section = BOT_PERSONA_THANKS; chance = 60; break;
      default:                       section = BOT_PERSONA_ASKED;  chance = 90; break;
   }

   // Nobody called out and nothing worth answering.
   if (named < 0 && topic == BOT_PERSONA_TOPIC_NONE)
      return;

   if (RANDOM_LONG2(1, 100) > chance)
      return;

   const int who = (named >= 0) ? named : pick[RANDOM_LONG2(0, n - 1)];

   g_chat_next_hear = gpGlobals->time + 4.0f;

   BotChatSayPersona(bots[who], section, speaker, 1.5f, 5.0f);
}
