//
// JK_Botti - be more human!
//
// bot_learn.cpp
//
// SGTC: see bot_learn.h.
//

#include <stdio.h>
#include <string.h>

#include <extdll.h>
#include <dllapi.h>
#include <h_export.h>
#include "botti_meta.h"

#include "bot.h"
#include "bot_func.h"
#include "waypoint.h"
#include "bot_learn.h"
#include "bot_learn_core.h"
#include "bot_trace.h"

// Cvars mirrored into plain ints by dlls/sgtc_bots.cpp.
extern int sgtc_bot_learn_enabled;
extern int sgtc_bot_learn_test;

extern int num_waypoints;
extern WAYPOINT waypoints[MAX_WAYPOINTS];

static heat_map_t s_people;      // where people go and fight (saved per map)
static heat_map_t s_visits;      // where bots have walked (this map only)

static float s_next_sample = 0.0f;
static float s_next_save = 0.0f;
static float s_visit_next[32];
static int s_people_loaded = 0;
static int s_goal_picks = 0;

// ---------------------------------------------------------------------------
// who is a person
// ---------------------------------------------------------------------------

static qboolean BotLearnIsPerson(edict_t *pEdict)
{
   if (FNullEnt(pEdict) || pEdict->free || !FBitSet(pEdict->v.flags, FL_CLIENT))
      return FALSE;

   if (FBitSet(pEdict->v.flags, FL_PROXY))
      return FALSE;

   // The test switch lets bots stand in for people so a headless run can
   // exercise the whole path.
   if (sgtc_bot_learn_test)
      return TRUE;

   return !FBitSet(pEdict->v.flags, FL_FAKECLIENT | FL_THIRDPARTYBOT);
}

// ---------------------------------------------------------------------------
// the heat file
// ---------------------------------------------------------------------------

static void BotLearnHeatFilename(char *filename, int size)
{
   char name[64];

   safevoid_snprintf(name, sizeof(name), "%s.heat", STRING(gpGlobals->mapname));
   UTIL_BuildFileName_N(filename, size, "addons/jk_botti/waypoints", name);
}

void BotLearn_LevelInit(void)
{
   char filename[256];
   FILE *fp;

   Heat_Clear(&s_people);
   Heat_Clear(&s_visits);
   memset(s_visit_next, 0, sizeof(s_visit_next));
   s_next_sample = 0.0f;
   s_next_save = 0.0f;
   s_people_loaded = 0;

   BotLearnHeatFilename(filename, sizeof(filename));

   fp = fopen(filename, "rb");

   if (fp != NULL)
   {
      static char text[HEAT_MAX_CELLS * 40 + 64];
      const size_t got = fread(text, 1, sizeof(text) - 1, fp);

      fclose(fp);
      text[got] = 0;

      const int cells = Heat_Parse(&s_people, text);

      if (cells >= 0)
      {
         // each session ages what came before it
         Heat_Decay(&s_people, 0.9f, 0.5f);
         s_people.dirty = 0;
         s_people_loaded = s_people.count;

         UTIL_ConsolePrintf("[learn] %s: %d hot cells from earlier play\n", filename, s_people.count);
      }
      else
      {
         UTIL_ConsolePrintf("[learn] %s is not a heat file, starting empty\n", filename);
      }
   }
}

void BotLearn_Save(void)
{
   char filename[256];
   char tmpname[272];

   if (!s_people.dirty)
      return;

   static char text[HEAT_MAX_CELLS * 40 + 64];

   const int n = Heat_Serialize(&s_people, text, sizeof(text));

   if (n <= 0)
      return;

   BotLearnHeatFilename(filename, sizeof(filename));
   safevoid_snprintf(tmpname, sizeof(tmpname), "%s.tmp", filename);

   FILE *fp = fopen(tmpname, "wb");

   if (fp == NULL)
      return;   // no waypoint directory here: nothing to keep, nothing lost

   const size_t put = fwrite(text, 1, (size_t)n, fp);

   fclose(fp);

   if (put != (size_t)n || rename(tmpname, filename) != 0)
   {
      remove(tmpname);
      return;
   }

   s_people.dirty = 0;
}

// ---------------------------------------------------------------------------
// per frame
// ---------------------------------------------------------------------------

void BotLearn_Frame(void)
{
   if (!sgtc_bot_learn_enabled)
      return;

   if (gpGlobals->time < s_next_sample)
      return;

   // a new map restarts the clock
   if (s_next_sample > gpGlobals->time + 10.0f)
      s_next_save = 0.0f;

   s_next_sample = gpGlobals->time + 1.0f;


   for (int i = 1; i <= gpGlobals->maxClients; i++)
   {
      edict_t *pPlayer = INDEXENT(i);

      if (!BotLearnIsPerson(pPlayer))
         continue;

      if (!IsAlive(pPlayer))
         continue;

      // Time spent counts, moving or not, but a person standing at a spawn
      // for ten minutes should not paint it as the place everyone goes.
      const float speed = pPlayer->v.velocity.Length2D();

      Heat_Add(&s_people, pPlayer->v.origin.x, pPlayer->v.origin.y, pPlayer->v.origin.z,
         speed > 40.0f ? 1.0f : 0.15f);
   }

   // keep what has been learned even if the server is stopped hard
   if (s_next_save == 0.0f || s_next_save > gpGlobals->time + 300.0f)
      s_next_save = gpGlobals->time + 120.0f;

   if (gpGlobals->time >= s_next_save)
   {
      s_next_save = gpGlobals->time + 120.0f;
      BotLearn_Save();
   }
}

void BotLearn_OnKill(edict_t *victim, edict_t *attacker)
{
   if (!sgtc_bot_learn_enabled || FNullEnt(victim))
      return;

   // Where people fight: every death a person is part of.
   if (BotLearnIsPerson(victim) || BotLearnIsPerson(attacker))
      Heat_Add(&s_people, victim->v.origin.x, victim->v.origin.y, victim->v.origin.z, 8.0f);
}

// ---------------------------------------------------------------------------
// using it
// ---------------------------------------------------------------------------

int BotLearn_PickGoal(bot_t &pBot)
{
   static float weights[MAX_WAYPOINTS];

   if (!sgtc_bot_learn_enabled || s_people.count < 3)
      return -1;

   // Mostly follow the traffic, now and then wander as before.
   if (RANDOM_LONG2(1, 100) > 70)
      return -1;

   edict_t *pEdict = pBot.pEdict;
   float total = 0.0f;

   for (int i = 0; i < MAX_WAYPOINTS; i++)
   {
      weights[i] = 0.0f;

      if (i >= num_waypoints)
         continue;

      if (waypoints[i].flags & (W_FL_DELETED | W_FL_AIMING | W_FL_LIFT_END))
         continue;

      if (i == pBot.curr_waypoint_index)
         continue;

      const Vector &o = waypoints[i].origin;

      // Not the node the bot is standing next to.
      if ((o - pEdict->v.origin).Length() < 350.0f)
         continue;

      // Somewhere the route matrix can actually take it.
      if (pBot.curr_waypoint_index >= 0 &&
          WaypointDistanceFromTo(pBot.curr_waypoint_index, i) >= (float)WAYPOINT_MAX_DISTANCE)
         continue;

      const float w = Heat_At(&s_people, o.x, o.y, o.z);

      if (w > 0.0f)
      {
         weights[i] = w;
         total += w;
      }
   }

   if (!(total > 0.0f))
      return -1;

   const int pick = Heat_PickIndex(weights, num_waypoints, RANDOM_FLOAT2(0.0f, 0.9999f));

   if (pick >= 0)
   {
      s_goal_picks++;
      BotTrace(pBot, "goal set: people heat wpt=%d heat=%.1f", pick, weights[pick]);
   }

   return pick;
}

float BotLearn_VisitWeight(const Vector &where)
{
   return Heat_At(&s_visits, where.x, where.y, where.z);
}

void BotLearn_NoteVisit(bot_t &pBot)
{
   const int slot = (int)(&pBot - &bots[0]);

   if (slot < 0 || slot >= 32 || FNullEnt(pBot.pEdict))
      return;

   if (gpGlobals->time < s_visit_next[slot])
      return;

   s_visit_next[slot] = gpGlobals->time + 0.5f;

   Heat_Add(&s_visits, pBot.pEdict->v.origin.x, pBot.pEdict->v.origin.y, pBot.pEdict->v.origin.z, 1.0f);
}

// ---------------------------------------------------------------------------
// console
// ---------------------------------------------------------------------------

void BotLearn_Command(void)
{
   const char *sub = (CMD_ARGC() > 1) ? CMD_ARGV(1) : "status";

   if (!strcmp(sub, "save"))
   {
      s_people.dirty = 1;
      BotLearn_Save();
   }
   else if (!strcmp(sub, "clear"))
   {
      Heat_Clear(&s_people);
      Heat_Clear(&s_visits);
   }

   UTIL_ConsolePrintf("[learn] enabled=%d test=%d people_cells=%d (loaded %d) visit_cells=%d heat_goals=%d\n",
      sgtc_bot_learn_enabled, sgtc_bot_learn_test, s_people.count, s_people_loaded, s_visits.count, s_goal_picks);
}
