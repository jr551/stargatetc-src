//
// JK_Botti - be more human!
//
// bot_trace.cpp
//

#include <string.h>
#include <stdarg.h>

#include <extdll.h>
#include <dllapi.h>
#include <h_export.h>
#include "botti_meta.h"

#include "bot.h"
#include "bot_weapons.h"
#include "bot_trace.h"
#include "safe_snprintf.h"

extern bot_weapon_t weapon_defs[MAX_WEAPONS];
extern int submod_id;

extern qboolean is_team_play;

int bot_trace_level = BOT_TRACE_OFF;

static cvar_t jk_botti_trace_cvar = { "jk_botti_trace", "0", FCVAR_EXTDLL|FCVAR_SERVER, 0, NULL};
static qboolean trace_cvar_registered = FALSE;


void BotTraceRegisterCvar(void)
{
   CVAR_REGISTER(&jk_botti_trace_cvar);
   trace_cvar_registered = TRUE;
}


void BotTraceUpdateCache(void)
{
   if (!trace_cvar_registered)
      return;

   int val = (int)CVAR_GET_FLOAT("jk_botti_trace");

   if (val < BOT_TRACE_OFF)
      val = BOT_TRACE_OFF;
   if (val > BOT_TRACE_SAY)
      val = BOT_TRACE_SAY;

   bot_trace_level = val;
}


static void BotTraceLog(bot_t &pBot, const char *message)
{
   edict_t *pEdict = pBot.pEdict;

   if (is_team_play)
   {
      char teamstr[MAX_TEAMNAME_LENGTH];

      UTIL_GetTeam(pEdict, teamstr, sizeof(teamstr));

      UTIL_LogPrintf( "\"%s<%i><%s><%s>\" say \"[TRACE] %s\"\n",
         STRING( pEdict->v.netname ),
         GETPLAYERUSERID( pEdict ),
         (*g_engfuncs.pfnGetPlayerAuthId)( pEdict ),
         teamstr,
         message );
   }
   else
   {
      UTIL_LogPrintf( "\"%s<%i><%s><%i>\" say \"[TRACE] %s\"\n",
         STRING( pEdict->v.netname ),
         GETPLAYERUSERID( pEdict ),
         (*g_engfuncs.pfnGetPlayerAuthId)( pEdict ),
         GETPLAYERUSERID( pEdict ),
         message );
   }
}


static void BotTraceSay(bot_t &pBot, const char *message)
{
   char say_msg[128];

   safevoid_snprintf(say_msg, sizeof(say_msg), "[TRACE] %s", message);

   UTIL_HostSay(pBot.pEdict, 0, say_msg);
}


const char *BotTraceGoalTypeName(int goal_type)
{
   switch (goal_type)
   {
      case WPT_GOAL_NONE:        return "none";
      case WPT_GOAL_HEALTH:      return "health";
      case WPT_GOAL_ARMOR:       return "armor";
      case WPT_GOAL_WEAPON:      return "weapon";
      case WPT_GOAL_AMMO:        return "ammo";
      case WPT_GOAL_ITEM:        return "item";
      case WPT_GOAL_LOCATION:    return "location";
      case WPT_GOAL_TRACK_SOUND: return "track_sound";
      case WPT_GOAL_ENEMY:       return "enemy";
      default:                   return "unknown";
   }
}


static const char *BotTraceWeaponShortName(int iId)
{
   // StargateTC ids are their own numbering (1 = weapon_coujaf, not the
   // crowbar), so the stock switch below would lie for every slot.
   if (submod_id == SUBMOD_SGTC)
   {
      switch (iId)
      {
         case SGTC_WEAPON_COUJAF:       return "cj";
         case SGTC_WEAPON_M92S:         return "m9";
         case SGTC_WEAPON_COUTEAU:      return "ct";
         case SGTC_WEAPON_MP5:          return "mp";
         case SGTC_WEAPON_PSG1:         return "ps";
         case SGTC_WEAPON_P90:          return "p9";
         case SGTC_WEAPON_SPAS:         return "sp";
         case SGTC_WEAPON_TACGUN:       return "tc";
         case SGTC_WEAPON_INVIS:        return "iv";
         case SGTC_WEAPON_GACGOA:       return "gc";
         case SGTC_WEAPON_M16:          return "m16";
         case SGTC_WEAPON_HANDGRENADE:  return "hg";
         case SGTC_WEAPON_MINE:         return "mn";
         case SGTC_WEAPON_C4:           return "c4";
         case SGTC_WEAPON_USAS:         return "us";
         case SGTC_WEAPON_MAIN:         return "ma";
         case SGTC_WEAPON_ZAT:          return "za";
         case SGTC_WEAPON_GLSTAFF:      return "gs";
         case SGTC_WEAPON_LANCE:        return "la";
         case SGTC_WEAPON_BATSUP:       return "bs";
         case SGTC_WEAPON_REETOU:       return "re";
         case SGTC_WEAPON_LARVE:        return "lv";
         case SGTC_WEAPON_ZATARC:       return "zr";
         case SGTC_WEAPON_MEDIC:        return "md";
         case SGTC_WEAPON_SHIELD:       return "sh";
         case SGTC_WEAPON_SMOKEGRENADE: return "sm";
         case SGTC_WEAPON_NERVEGRENADE: return "nv";
         case SGTC_WEAPON_NARCOGRENADE: return "nr";
         case SGTC_WEAPON_MUSTARDGRENADE: return "mu";
         case SGTC_WEAPON_GOAREGEN:     return "gr";
         default:                       return "?";
      }
   }

   switch (iId)
   {
      case VALVE_WEAPON_CROWBAR:         return "cw";
      case VALVE_WEAPON_GLOCK:           return "gl";
      case VALVE_WEAPON_PYTHON:          return "py";
      case VALVE_WEAPON_MP5:             return "mp";
      case VALVE_WEAPON_CHAINGUN:        return "cg";
      case VALVE_WEAPON_CROSSBOW:        return "xb";
      case VALVE_WEAPON_SHOTGUN:         return "sg";
      case VALVE_WEAPON_RPG:             return "rp";
      case VALVE_WEAPON_GAUSS:           return "ga";
      case VALVE_WEAPON_EGON:            return "eg";
      case VALVE_WEAPON_HORNETGUN:       return "hr";
      case VALVE_WEAPON_HANDGRENADE:     return "hg";
      case VALVE_WEAPON_TRIPMINE:        return "tp";
      case VALVE_WEAPON_SATCHEL:         return "sa";
      case VALVE_WEAPON_SNARK:           return "sk";
      case GEARBOX_WEAPON_GRAPPLE:       return "gp";
      case GEARBOX_WEAPON_EAGLE:         return "ea";
      case GEARBOX_WEAPON_PIPEWRENCH:    return "pw";
      case GEARBOX_WEAPON_M249:          return "mw";
      case GEARBOX_WEAPON_DISPLACER:     return "di";
      case GEARBOX_WEAPON_SHOCKRIFLE:    return "sh";
      case GEARBOX_WEAPON_SPORELAUNCHER: return "sp";
      case GEARBOX_WEAPON_SNIPERRIFLE:   return "sn";
      case GEARBOX_WEAPON_KNIFE:         return "kn";
      // ID 26 shared: GEARBOX_WEAPON_PENGUIN (OP4) / ARENA_WEAPON_9MMSILENCED (Arena)
      case GEARBOX_WEAPON_PENGUIN:       return submod_id == SUBMOD_ARENA ? "si" : "pn";
      case ARENA_WEAPON_AUTOSHOTGUN:     return "as";
      case ARENA_WEAPON_BURSTRIFLE:      return "br";
      default:                           return "?";
   }
}


void BotTraceAmmoSummary(bot_t &pBot, char *buf, size_t bufsize)
{
   size_t pos = 0;
   buf[0] = '\0';

   for (int i = 0; weapon_select[i].iId && i < NUM_OF_WEAPON_SELECTS; i++)
   {
      int id = weapon_select[i].iId;

      // does the bot have this weapon?
      if (!(pBot.pEdict->v.weapons & (1u << id)))
         continue;

      // skip melee weapons (always usable, not interesting for no_weapon diagnosis)
      if (weapon_select[i].type & WEAPON_MELEE)
         continue;

      // skip weapons that don't use ammo
      if (weapon_defs[id].iAmmo1 < 0)
         continue;

      int ammo1 = pBot.m_rgAmmo[weapon_defs[id].iAmmo1];
      const char *name = BotTraceWeaponShortName(id);

      safevoid_snprintf(buf + pos, bufsize - pos, "%s%d", name, ammo1);

      size_t newpos = strlen(buf);
      if (newpos == pos)
         break;
      pos = newpos;
   }
}


void BotTracePrintf(bot_t &pBot, const char *fmt, ...)
{
   va_list argptr;
   char message[256];

   va_start(argptr, fmt);
   safevoid_vsnprintf(message, sizeof(message), fmt, argptr);
   va_end(argptr);

   if (bot_trace_level == BOT_TRACE_SAY)
      BotTraceSay(pBot, message);
   else if (bot_trace_level == BOT_TRACE_LOG)
      BotTraceLog(pBot, message);
}
