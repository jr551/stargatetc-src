// sgtc_sniper.h - server side of PSG1 scope sway, hit feedback and bot aim
// compensation.
//
// Call sites (owned by the weapon / rules files):
//   SGTC_Sniper_Init              GameDLLInit, next to SGTC_TestInit
//   SGTC_Sniper_RegisterMessages  SGTC_LinkUserMessages_Suffix (usermsg slots
//                                 are assigned at link time)
//   SGTC_Sniper_SwayAimVector     sniper weapons, right before the aim vector
//                                 is used (fake clients only)
//   SGTC_Sniper_NoteFire/NoteHurt weapon fire / player damage
//   SGTC_Sniper_NotifyBulletHit   after a sniper hits somebody
//   SGTC_Sniper_NotifyKill        after a sniper kill is scored
//   SGTC_Sniper_BotCompensate     SGTC_Bots_CmdStart (fire frames only)
//
// The header only needs extdll.h (edict_t / cvar_t / TraceResult / Vector);
// CBasePlayer and CBaseEntity are forward declared so it can be included in
// any order relative to cbase.h/player.h/weapons.h.
#ifndef SGTC_SNIPER_H
#define SGTC_SNIPER_H

#include "extdll.h"

struct usercmd_s;

class CBasePlayer;
class CBaseEntity;

// "sgtc_sniper_sway" - human scope drift 0..1 (1 = retail feel).
extern cvar_t sgtc_cvar_sniper_sway;
// "sgtc_sniper_sway_bots" - fraction of the sway bots suffer (0..1).
extern cvar_t sgtc_cvar_sniper_sway_bots;
// "sgtc_sniper_deathpunch" - corpse shove + view punch on scoped kills.
extern cvar_t sgtc_cvar_sniper_deathpunch;

void SGTC_Sniper_Init( void );
void SGTC_Sniper_RegisterMessages( void );

// Fake clients only: rotates *vecAiming by the current sway offset in place.
void SGTC_Sniper_SwayAimVector( CBasePlayer *pPlayer, Vector *vecAiming );

void SGTC_Sniper_NoteFire( CBasePlayer *pPlayer );
void SGTC_Sniper_NoteHurt( CBasePlayer *pPlayer );

void SGTC_Sniper_NotifyBulletHit( CBasePlayer *pAttacker, CBaseEntity *pHit, TraceResult *tr, int iBulletType );
void SGTC_Sniper_NotifyKill( CBasePlayer *pKiller, CBasePlayer *pVictim );

// Fire-time compensation for fake clients: the bot's command is inspected and,
// on frames where it pulls the trigger with a sniper weapon, the sway the
// weapon is about to add is removed from the aim so the shot lands on the
// bot's intended target. Runs from SGTC_Bots_CmdStart, before the weapon fires.
void SGTC_Sniper_BotCompensate( edict_t *pEdict, const struct usercmd_s *cmd );

#endif // SGTC_SNIPER_H
