#ifndef JK_BOTTI_NATIVE_API_H
#define JK_BOTTI_NATIVE_API_H

#include "../dlls/extdll.h"

#ifdef __cplusplus
extern "C" {
#endif

void JK_Botti_NativeInit(DLL_FUNCTIONS *gameApi, enginefuncs_t *engineApi);

void JK_Botti_GameInit(void);
int JK_Botti_OnSpawn(edict_t *pent);
int JK_Botti_OnSpawnPost(edict_t *pent);
void JK_Botti_OnKeyValuePost(edict_t *pent, KeyValueData *pkvd);

BOOL JK_Botti_ClientConnect(edict_t *pEntity, const char *pszName, const char *pszAddress, char szRejectReason[128]);
void JK_Botti_ClientPutInServer(edict_t *pEntity);
void JK_Botti_ClientDisconnect(edict_t *pEntity);
void JK_Botti_ClientCommand(edict_t *pEntity);
void JK_Botti_StartFrame(void);
void JK_Botti_ServerDeactivate(void);
void JK_Botti_PM_Move(struct playermove_s *ppmove, int server);
void JK_Botti_CmdStart(const edict_t *player, const struct usercmd_s *cmd, unsigned int random_seed);
void JK_Botti_PlayerPostThinkPost(edict_t *pEdict);
void JK_Botti_ServerCommand(void);

// Director glue: speak one line through the bot chat path (UTIL_HostSay) and
// report how many live bots exist. Both are plain C so dlls/sgtc_director.cpp
// never includes jk_botti/util.h itself.
void JK_Botti_Say(edict_t *pEdict, const char *text);
int JK_Botti_Count(void);

#ifdef __cplusplus
}
#endif

#endif
