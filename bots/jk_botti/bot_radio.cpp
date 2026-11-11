//
// JK_Botti - be more human!
//
// bot_radio.cpp
//
// SGTC: bots and the mod's radio. Two directions:
//
//  * hearing - BotRadio_OnMessage() is called by the server glue for every
//    line sent on the Radio usermessage (human or bot). Lines that carry an
//    order (SGTC_Radio_OrderForMessage) are rolled against the bot's obey
//    chance; the ones that take it get a movement order and answer with the
//    accept line. Refusals sometimes answer "Negative".
//
//  * talking - BotRadio_Think() emits a queued line and volunteers reports
//    ("Watch out Enemy!", "Help, I need backup!") behind the bot_radio cvar,
//    the per-bot rate and a per-team gap so a squad does not talk over itself.
//
// Movement itself is BotRadio_HandleMovement(): it either stands the bot on a
// spot (HOLD/SPREAD) or hands the destination to the existing waypoint router
// (and to the no-waypoint roam fallback), so wall avoidance, ladders and
// doors keep working exactly as they do for ordinary wandering.

#include <extdll.h>
#include <dllapi.h>
#include <h_export.h>

#include "bot.h"
#include "util.h"
#include "bot_func.h"
#include "waypoint.h"
#include "bot_radio.h"

#ifdef JK_BOTTI_NATIVE

extern int num_waypoints;  // number of waypoints currently in use

// Per-team rate gate. Index 0 is unused (spectators never speak); the team
// values are the entity numbering, 1 = Goa'uld and 2 = Torri. These are the
// only radio deadlines that live outside bot_t, so they are cleared on a map
// change (BotRadio_LevelInit) and SGTC_Radio_RateAllows also treats a deadline
// too far ahead to belong to the current map as elapsed.
static float s_radio_team_next_time[3] = { 0.0f, 0.0f, 0.0f };

// Arrival radii. FOLLOW/SUPPORT are about staying with a teammate, HOLD is a
// spot on the floor, everything else just wants to be in the area.
#define SGTC_RADIO_RADIUS_FOLLOW 120.0f
#define SGTC_RADIO_RADIUS_HOLD    80.0f
#define SGTC_RADIO_RADIUS_OTHER  150.0f

// Waypoint goal refresh / lifetime for an order destination.
#define SGTC_RADIO_GOAL_REFRESH   2.0f
#define SGTC_RADIO_GOAL_LIFETIME 12.0f
#define SGTC_RADIO_GOAL_RANGE   2048.0f

// Don't call the same enemy slot again inside this window.
#define SGTC_RADIO_ENEMY_REPORT_GAP 8.0f


static qboolean BotRadio_IsAlive( edict_t *pEdict )
{
   if ( FNullEnt(pEdict) )
      return FALSE;
   if ( pEdict->v.health <= 0 )
      return FALSE;
   if ( pEdict->v.deadflag != DEAD_NO )
      return FALSE;
   return TRUE;
}


static edict_t *BotRadio_EntityFromIndex( int index )
{
   if ( index <= 0 || index >= gpGlobals->maxEntities )
      return NULL;

   return INDEXENT( index );
}


static void BotRadio_ClearOrder( bot_t &pBot )
{
   // An order that handed a destination to the waypoint router may leave the
   // goal behind when it expires; drop it so the bot does not keep walking to
   // a stale hold point.
   if ( pBot.radio_order != SGTC_RADIO_ORDER_NONE &&
        pBot.wpt_goal_type == WPT_GOAL_LOCATION )
   {
      pBot.waypoint_goal = -1;
      pBot.curr_waypoint_index = -1;
      pBot.f_waypoint_goal_time = 0.0f;
   }

   pBot.radio_order = SGTC_RADIO_ORDER_NONE;
   pBot.radio_order_msg = 0;
   pBot.radio_order_target = 0;
   pBot.v_radio_order_origin = Vector(0, 0, 0);
   pBot.f_radio_order_time = 0.0f;
   pBot.f_radio_order_accept = 0.0f;
   pBot.f_radio_next_goal = 0.0f;
   pBot.b_radio_order_arrived = FALSE;
   pBot.b_radio_order_said = FALSE;
}


// Store one order and arm its reaction delay and deadline. `instant` is the
// director path (Bot_RadioOrder): no reaction delay, and the bot is the one
// calling the push. Heard orders answer with the accept line instead, so they
// do not queue a speak-for-order line here.
static void BotRadio_ApplyOrder( bot_t &pBot, int order, edict_t *target,
   const Vector &area, qboolean hasArea, qboolean instant )
{
   if ( order <= SGTC_RADIO_ORDER_NONE || order > SGTC_RADIO_ORDER_SPREAD )
      return;
   if ( FNullEnt(pBot.pEdict) )
      return;

   const float now = gpGlobals->time;

   pBot.radio_order = order;
   pBot.radio_order_msg = 0;  // the hearing path fills in the line it heard
   pBot.radio_order_target = FNullEnt(target) ? 0 : ENTINDEX(target);

   if ( !FNullEnt(target) )
      pBot.v_radio_order_origin = target->v.origin;
   else if ( hasArea )
      pBot.v_radio_order_origin = area;
   else
      pBot.v_radio_order_origin = pBot.pEdict->v.origin;

   if ( order == SGTC_RADIO_ORDER_FALLBACK )
   {
      // "Fall back" means our own side of the map, not the speaker's feet.
      if ( pBot.b_roam_have_spawn )
         pBot.v_radio_order_origin = pBot.v_roam_spawn;
   }
   else if ( order == SGTC_RADIO_ORDER_SPREAD && !FNullEnt(target) )
   {
      // "Spread out" / "Grenade!": pick a spot away from the speaker instead of
      // bunching up on him. Same 250-unit scatter in every direction.
      const float yaw = RANDOM_FLOAT2(0.0f, 360.0f);
      Vector v_away = UTIL_AnglesToForward(Vector(0.0f, yaw, 0.0f));

      pBot.v_radio_order_origin = target->v.origin + v_away * 250.0f;
   }

   const float delay = instant ? 0.0f : SGTC_Radio_AcceptDelay(RANDOM_FLOAT2(0.0f, 1.0f));

   pBot.f_radio_order_accept = now + delay;
   pBot.f_radio_order_time = pBot.f_radio_order_accept +
      SGTC_Radio_OrderDuration(RANDOM_FLOAT2(0.0f, 1.0f));

   pBot.f_radio_next_goal = 0.0f;  // pick the destination waypoint on the next move
   pBot.b_radio_order_arrived = FALSE;
   pBot.b_radio_order_said = FALSE;

   if ( instant )
   {
      const int speak = SGTC_Radio_SpeakForOrder(order, pBot.bot_skill);
      if ( speak != 0 && speak != SGTC_RADIO_IN_POSITION )
         BotRadio_Chatter(pBot, speak);
   }
}


void BotRadio_SpawnInit( bot_t &pBot )
{
   BotRadio_ClearOrder(pBot);

   pBot.radio_pending_msg = -1;  // 0 is SGTC_RADIO_COVER_ME, so -1 is "none"
   pBot.f_radio_pending_time = 0.0f;
   pBot.f_radio_next_time = 0.0f;
   pBot.f_radio_chattiness = 1.0f;
   pBot.radio_last_enemy = -1;
   pBot.f_radio_enemy_report_time = 0.0f;
}


void BotRadio_LevelInit( void )
{
   // gpGlobals->time restarts near 0 on a new map. Bots that stay connected
   // across the change are not re-spawned, so their absolute radio deadlines
   // would be decades in the future on the new map: drop them here.
   for ( int i = 0; i < 3; i++ )
      s_radio_team_next_time[i] = 0.0f;

   enum { MAX_BOTS = sizeof(bots) / sizeof(bots[0]) };

   for ( int i = 0; i < MAX_BOTS; i++ )
   {
      bot_t &pBot = bots[i];

      if ( !pBot.is_used )
         continue;

      BotRadio_ClearOrder(pBot);
      pBot.radio_pending_msg = -1;
      pBot.f_radio_pending_time = 0.0f;
      pBot.f_radio_next_time = 0.0f;
      pBot.radio_last_enemy = -1;
      pBot.f_radio_enemy_report_time = 0.0f;
   }
}


void BotRadio_IssueOrder( bot_t &pBot, int order, edict_t *target,
   const Vector &area, qboolean hasArea )
{
   BotRadio_ApplyOrder(pBot, order, target, area, hasArea, FALSE);
}


void BotRadio_SetChattiness( bot_t &pBot, float chattiness )
{
   if ( chattiness < 0.0f )
      chattiness = 0.0f;

   pBot.f_radio_chattiness = chattiness;
}


int BotRadio_Chatter( bot_t &pBot, int msgId )
{
   if ( msgId < 0 )
      return 0;

   if ( FNullEnt(pBot.pEdict) )
      return 0;

   if ( !SGTC_Radio_ModeAllowsTalk(sgtc_bot_radio_mode) )
      return 0;

   // bot_radio 0/1 keeps the bots quiet; the director can mute a single bot.
   if ( pBot.f_radio_chattiness <= 0.0f )
      return 0;

   const int team = (int)pBot.pEdict->v.team;
   if ( team < 1 || team > 2 )
      return 0;

   const float now = gpGlobals->time;

   if ( !SGTC_Radio_RateAllows(now, pBot.f_radio_next_time, sgtc_bot_radio_rate) )
      return 0;

   if ( !SGTC_Radio_RateAllows(now, s_radio_team_next_time[team], SGTC_RADIO_TEAM_GAP) )
      return 0;

   // chattiness below 1 drops the call; 0 was handled above.
   if ( pBot.f_radio_chattiness < 1.0f &&
        RANDOM_FLOAT2(0.0f, 1.0f) >= pBot.f_radio_chattiness )
      return 0;

   pBot.radio_pending_msg = msgId;
   pBot.f_radio_pending_time = now + RANDOM_FLOAT2(0.3f, 0.8f);
   pBot.f_radio_next_time = SGTC_Radio_NextSlot(now, sgtc_bot_radio_rate);
   s_radio_team_next_time[team] = SGTC_Radio_NextSlot(now, SGTC_RADIO_TEAM_GAP);

   return 1;
}


void BotRadio_OnMessage( edict_t *speaker, int msgId, const Vector &area, qboolean hasArea )
{
   if ( FNullEnt(speaker) )
      return;

   // bot_radio 0: bots neither obey nor talk.
   if ( !SGTC_Radio_ModeAllowsObey(sgtc_bot_radio_mode) )
      return;

   const int order = SGTC_Radio_OrderForMessage(msgId);
   if ( order == SGTC_RADIO_ORDER_NONE )
      return;  // an ack or a status line, not something to act on

   const int speaker_team = (int)speaker->v.team;
   if ( speaker_team < 1 || speaker_team > 2 )
      return;

   enum { MAX_CANDIDATES = sizeof(bots) / sizeof(bots[0]) };

   int candidates[MAX_CANDIDATES];
   float candidate_distance[MAX_CANDIDATES];
   int count = 0;

   for ( int i = 0; i < MAX_CANDIDATES; i++ )
   {
      bot_t &pBot = bots[i];

      if ( !pBot.is_used || FNullEnt(pBot.pEdict) )
         continue;
      if ( !BotRadio_IsAlive(pBot.pEdict) )
         continue;

      const float distance = (pBot.pEdict->v.origin - speaker->v.origin).Length2D();

      if ( !SGTC_Radio_ShouldHear(speaker_team, (int)pBot.pEdict->v.team,
                                  pBot.pEdict == speaker ? 1 : 0, distance) )
         continue;

      candidates[count] = i;
      candidate_distance[count] = distance;
      count++;
   }

   // "Help, I need backup!" is answered by the nearest few only, or every bot
   // on the team would turn around at once.
   if ( msgId == SGTC_RADIO_NEED_BACKUP && count > SGTC_RADIO_NEED_BACKUP_MAX )
   {
      for ( int i = 1; i < count; i++ )
      {
         const int index = candidates[i];
         const float distance = candidate_distance[i];
         int j = i - 1;

         while ( j >= 0 && candidate_distance[j] > distance )
         {
            candidates[j + 1] = candidates[j];
            candidate_distance[j + 1] = candidate_distance[j];
            j--;
         }

         candidates[j + 1] = index;
         candidate_distance[j + 1] = distance;
      }

      count = SGTC_RADIO_NEED_BACKUP_MAX;
   }

   for ( int i = 0; i < count; i++ )
   {
      bot_t &pBot = bots[candidates[i]];

      const int in_combat = ( pBot.pBotEnemy != NULL ||
                              pBot.f_bot_see_enemy_time + 3.0f > gpGlobals->time ) ? 1 : 0;

      const float chance = SGTC_Radio_ObeyChance(pBot.bot_skill, in_combat,
                                                 pBot.b_low_health ? 1 : 0);

      if ( RANDOM_FLOAT2(0.0f, 1.0f) > chance )
      {
         // About a third of the refusals are spoken, so a "Negative" is not
         // noise on every single order.
         if ( RANDOM_FLOAT2(0.0f, 1.0f) < (1.0f / 3.0f) )
            BotRadio_Chatter(pBot, SGTC_RADIO_NEGATIVE);
         continue;
      }

      // FOLLOW/SUPPORT track the speaker; a PUSH line that carries an area
      // (the objective the server glue found) moves to that area instead.
      edict_t *order_target = hasArea ? NULL : speaker;

      BotRadio_IssueOrder(pBot, order, order_target, area, hasArea);
      pBot.radio_order_msg = msgId;

      BotRadio_Chatter(pBot, SGTC_Radio_AcceptReply(pBot.bot_skill));
   }
}


// Called from BotThink right after the random chat: emit what was queued a
// moment ago, expire old orders and volunteer the occasional report.
void BotRadio_Think( bot_t &pBot )
{
   if ( FNullEnt(pBot.pEdict) )
      return;

   const float now = gpGlobals->time;

   if ( pBot.radio_pending_msg >= 0 && pBot.f_radio_pending_time <= now )
   {
      const int msgId = pBot.radio_pending_msg;
      pBot.radio_pending_msg = -1;
      pBot.f_radio_pending_time = 0.0f;

      if ( BotRadio_IsAlive(pBot.pEdict) )
         SGTC_Bots_RadioSend(pBot.pEdict, msgId);
   }

   if ( pBot.radio_order != SGTC_RADIO_ORDER_NONE && pBot.f_radio_order_time <= now )
      BotRadio_ClearOrder(pBot);

   if ( !SGTC_Radio_ModeAllowsTalk(sgtc_bot_radio_mode) )
      return;

   // (a) a new enemy: call it once per enemy slot / 8 s window
   const int enemy_index = FNullEnt(pBot.pBotEnemy) ? 0 : ENTINDEX(pBot.pBotEnemy);

   if ( enemy_index > 0 )
   {
      if ( enemy_index != pBot.radio_last_enemy &&
           pBot.f_radio_enemy_report_time + SGTC_RADIO_ENEMY_REPORT_GAP <= now )
      {
         if ( BotRadio_Chatter(pBot, SGTC_RADIO_ENEMY) )
         {
            pBot.radio_last_enemy = enemy_index;
            pBot.f_radio_enemy_report_time = now;
         }
      }
   }
   else
   {
      // no enemy in sight: forget the report so the same slot can be called
      // again if it is reused by a later enemy
      pBot.radio_last_enemy = -1;
   }

   // (b) hurt and just got shot at
   if ( pBot.b_low_health && pBot.f_last_time_attacked + 5.0f > now )
      BotRadio_Chatter(pBot, SGTC_RADIO_NEED_BACKUP);
}


// Called from the final else of BotThinkHandleNavigation. Returns TRUE when
// the order fully handled movement for this frame.
qboolean BotRadio_HandleMovement( bot_t &pBot )
{
   if ( FNullEnt(pBot.pEdict) )
      return FALSE;

   if ( !SGTC_Radio_ModeAllowsObey(sgtc_bot_radio_mode) )
   {
      if ( pBot.radio_order != SGTC_RADIO_ORDER_NONE )
         BotRadio_ClearOrder(pBot);
      return FALSE;
   }

   if ( pBot.radio_order == SGTC_RADIO_ORDER_NONE )
      return FALSE;

   const float now = gpGlobals->time;

   if ( pBot.f_radio_order_time <= now )
   {
      BotRadio_ClearOrder(pBot);
      return FALSE;
   }

   // Reaction delay: the bot is still finishing what it was doing.
   if ( pBot.f_radio_order_accept > now )
      return FALSE;

   Vector goal = pBot.v_radio_order_origin;

   if ( pBot.radio_order == SGTC_RADIO_ORDER_FOLLOW ||
        pBot.radio_order == SGTC_RADIO_ORDER_SUPPORT )
   {
      edict_t *target = BotRadio_EntityFromIndex(pBot.radio_order_target);
      if ( BotRadio_IsAlive(target) )
      {
         goal = target->v.origin;
         pBot.v_radio_order_origin = goal;  // fallback for when the target dies
      }
   }

   float radius = SGTC_RADIO_RADIUS_OTHER;
   if ( pBot.radio_order == SGTC_RADIO_ORDER_FOLLOW ||
        pBot.radio_order == SGTC_RADIO_ORDER_SUPPORT )
      radius = SGTC_RADIO_RADIUS_FOLLOW;
   else if ( pBot.radio_order == SGTC_RADIO_ORDER_HOLD )
      radius = SGTC_RADIO_RADIUS_HOLD;

   edict_t *pEdict = pBot.pEdict;
   const float distance = (goal - pEdict->v.origin).Length2D();

   if ( distance <= radius )
   {
      pBot.b_radio_order_arrived = TRUE;

      if ( pBot.radio_order == SGTC_RADIO_ORDER_HOLD ||
           pBot.radio_order == SGTC_RADIO_ORDER_SPREAD )
      {
         // Stand on the spot and keep an eye out. Drop the stale waypoint so
         // the door/ladder speed adjustments cannot push the bot off the spot.
         pBot.curr_waypoint_index = -1;
         pBot.waypoint_goal = -1;
         pBot.wpt_goal_type = WPT_GOAL_NONE;
         pBot.f_move_speed = 0.0f;
         pBot.b_not_maxspeed = FALSE;
         pBot.f_strafe_direction = 0.0f;
         pBot.f_strafe_time = now + 0.5f;

         if ( pBot.f_pause_look_time <= now )
         {
            if ( RANDOM_LONG2(1, 100) <= 50 )
               pEdict->v.ideal_yaw += RANDOM_LONG2(15, 45);
            else
               pEdict->v.ideal_yaw -= RANDOM_LONG2(15, 45);

            BotFixIdealYaw(pEdict);

            pBot.f_pause_look_time = now + RANDOM_FLOAT2(1.0f, 2.5f);
         }

         if ( !pBot.b_radio_order_said )
         {
            if ( BotRadio_Chatter(pBot, SGTC_RADIO_IN_POSITION) )
               pBot.b_radio_order_said = TRUE;
         }

         return TRUE;
      }

      // The other orders hand the bot back to its normal brain; the order
      // itself stays valid until it times out.
      return FALSE;
   }

   if ( num_waypoints > 0 )
   {
      // Hand the destination to the waypoint router, which does the steering
      // and wall avoidance; refresh it every couple of seconds because the
      // goal can move (FOLLOW) or the router may have given up.
      if ( pBot.f_radio_next_goal <= now || pBot.waypoint_goal == -1 )
      {
         const int wpt = WaypointFindNearest(goal, pEdict, SGTC_RADIO_GOAL_RANGE);

         if ( wpt != -1 )
         {
            // Only drop the current route when the destination waypoint really
            // changed; resetting it every refresh would re-path twice a second.
            if ( wpt != pBot.waypoint_goal || pBot.wpt_goal_type != WPT_GOAL_LOCATION )
            {
               pBot.waypoint_goal = wpt;
               pBot.wpt_goal_type = WPT_GOAL_LOCATION;
               pBot.curr_waypoint_index = -1;
               pBot.f_waypoint_time = now;
            }

            pBot.f_waypoint_goal_time = now + SGTC_RADIO_GOAL_LIFETIME;
         }

         pBot.f_radio_next_goal = now + SGTC_RADIO_GOAL_REFRESH;
      }

      // Let BotWanderFreeRoam run BotHeadTowardWaypoint this frame.
      pBot.f_look_for_waypoint_time = 0.0f;
      return FALSE;
   }

   // No waypoints on this map: steer with the roam fallback, keeping its
   // target pinned to the order destination.
   pBot.v_roam_target = goal;
   pBot.f_roam_target_time = now + SGTC_RADIO_GOAL_REFRESH;
   pBot.b_roam_target_set = TRUE;

   BotRoamStep(pBot);
   return TRUE;
}


// Director API (dlls/sgtc_bots_radio.cpp): hand one bot an order.
void Bot_RadioOrder( edict_t *botEdict, int order, edict_t *target )
{
   bot_t *pBot = UTIL_GetBotPointer(botEdict);

   if ( pBot == NULL || !pBot->is_used || FNullEnt(pBot->pEdict) )
      return;

   if ( !BotRadio_IsAlive(pBot->pEdict) )
      return;

   Vector area(0, 0, 0);
   qboolean hasArea = FALSE;

   if ( !FNullEnt(target) )
   {
      area = target->v.origin;
      hasArea = TRUE;
   }

   BotRadio_ApplyOrder(*pBot, order, target, area, hasArea, TRUE);
}


// Director API: scale how often one bot uses the radio on its own.
void Bot_RadioChattiness( edict_t *botEdict, float chattiness )
{
   bot_t *pBot = UTIL_GetBotPointer(botEdict);

   if ( pBot == NULL || !pBot->is_used )
      return;

   BotRadio_SetChattiness(*pBot, chattiness);
}

#endif // JK_BOTTI_NATIVE
