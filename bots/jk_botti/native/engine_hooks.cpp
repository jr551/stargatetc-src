#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../dlls/extdll.h"
#include "meta_native.h"
#include "../bot.h"
#include "../bot_func.h"

extern unsigned short jkbotti_pfnPrecacheEvent_Post(int type, const char *psz);
extern unsigned short jkbotti_precache_event_native(const char *psz, unsigned short eventindex);
extern void jkbotti_pfnPlaybackEvent(int flags, const edict_t *pInvoker, unsigned short eventindex,
	float delay, float *origin, float *angles, float fparam1, float fparam2, int iparam1, int iparam2,
	int bparam1, int bparam2);
extern void jkbotti_pfnEmitSound(edict_t *entity, int channel, const char *sample, float volume,
	float attenuation, int fFlags, int pitch);
extern void jkbotti_pfnChangeLevel(char *s1, char *s2);
extern void jkbotti_pfnClientCommand(edict_t *pEdict, char *szFmt, ...);
extern void jkbotti_pfnMessageBegin(int msg_dest, int msg_type, const float *pOrigin, edict_t *ed);
extern void jkbotti_pfnMessageEnd(void);
extern void jkbotti_pfnWriteByte(int iValue);
extern void jkbotti_pfnWriteChar(int iValue);
extern void jkbotti_pfnWriteShort(int iValue);
extern void jkbotti_pfnWriteLong(int iValue);
extern void jkbotti_pfnWriteAngle(float flValue);
extern void jkbotti_pfnWriteCoord(float flValue);
extern void jkbotti_pfnWriteString(const char *sz);
extern void jkbotti_pfnWriteEntity(int iValue);
extern void jkbotti_pfnClientPrintf(edict_t *pEdict, PRINT_TYPE ptype, const char *szMsg);
extern const char *jkbotti_pfnCmd_Args(void);
extern const char *jkbotti_pfnCmd_Argv(int argc);
extern int jkbotti_pfnCmd_Argc(void);
extern void jkbotti_pfnSetClientMaxspeed(const edict_t *pEdict, float fNewMaxspeed);
extern int jkbotti_pfnGetPlayerUserId(edict_t *e);

#define WRAP_VOID(name, callargs, origcall) \
	static void wrap_##name callargs { \
		g_jkbotti_meta_globals.mres = MRES_UNSET; \
		jkbotti_##name origcall; \
		if (g_jkbotti_meta_globals.mres == MRES_SUPERCEDE) \
			return; \
		g_jkbotti_engine_original.name origcall; \
	}

#define WRAP_PTR(name, rettype, callargs, origcall) \
	static rettype wrap_##name callargs { \
		g_jkbotti_meta_globals.mres = MRES_UNSET; \
		rettype hook_ret = jkbotti_##name origcall; \
		if (g_jkbotti_meta_globals.mres == MRES_SUPERCEDE) \
			return hook_ret; \
		return g_jkbotti_engine_original.name origcall; \
	}

#define WRAP_INT(name, callargs, origcall) \
	static int wrap_##name callargs { \
		g_jkbotti_meta_globals.mres = MRES_UNSET; \
		int hook_ret = jkbotti_##name origcall; \
		if (g_jkbotti_meta_globals.mres == MRES_SUPERCEDE) \
			return hook_ret; \
		return g_jkbotti_engine_original.name origcall; \
	}

WRAP_VOID(pfnPlaybackEvent,
	(int flags, const edict_t *pInvoker, unsigned short eventindex, float delay, float *origin,
	 float *angles, float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2),
	(flags, pInvoker, eventindex, delay, origin, angles, fparam1, fparam2, iparam1, iparam2, bparam1, bparam2))

WRAP_VOID(pfnEmitSound,
	(edict_t *entity, int channel, const char *sample, float volume, float attenuation, int fFlags, int pitch),
	(entity, channel, sample, volume, attenuation, fFlags, pitch))

WRAP_VOID(pfnChangeLevel, (char *s1, char *s2), (s1, s2))

static void wrap_pfnClientCommand(edict_t *pEdict, char *szFmt, ...)
{
	va_list args;
	char cmd[1024];

	if (!FNullEnt(pEdict)
		&& (FBitSet(pEdict->v.flags, FL_FAKECLIENT) || FBitSet(pEdict->v.flags, FL_THIRDPARTYBOT)))
	{
		g_jkbotti_meta_globals.mres = MRES_UNSET;
		jkbotti_pfnClientCommand(pEdict, szFmt);
		return;
	}

	va_start(args, szFmt);
	vsnprintf(cmd, sizeof(cmd), szFmt, args);
	va_end(args);
	g_jkbotti_engine_original.pfnClientCommand(pEdict, "%s", cmd);
}

WRAP_VOID(pfnMessageBegin, (int msg_dest, int msg_type, const float *pOrigin, edict_t *ed),
	(msg_dest, msg_type, pOrigin, ed))
WRAP_VOID(pfnMessageEnd, (void), ())
WRAP_VOID(pfnWriteByte, (int iValue), (iValue))
WRAP_VOID(pfnWriteChar, (int iValue), (iValue))
WRAP_VOID(pfnWriteShort, (int iValue), (iValue))
WRAP_VOID(pfnWriteLong, (int iValue), (iValue))
WRAP_VOID(pfnWriteAngle, (float flValue), (flValue))
WRAP_VOID(pfnWriteCoord, (float flValue), (flValue))
WRAP_VOID(pfnWriteString, (const char *sz), (sz))
WRAP_VOID(pfnWriteEntity, (int iValue), (iValue))
WRAP_VOID(pfnClientPrintf, (edict_t *pEdict, PRINT_TYPE ptype, const char *szMsg), (pEdict, ptype, szMsg))

WRAP_PTR(pfnCmd_Args, const char *, (void), ())
WRAP_PTR(pfnCmd_Argv, const char *, (int argc), (argc))
WRAP_INT(pfnCmd_Argc, (void), ())

WRAP_VOID(pfnSetClientMaxspeed, (const edict_t *pEdict, float fNewMaxspeed), (pEdict, fNewMaxspeed))
WRAP_INT(pfnGetPlayerUserId, (edict_t *e), (e))

static unsigned short wrap_pfnPrecacheEvent(int type, const char *psz)
{
	unsigned short ret = g_jkbotti_engine_original.pfnPrecacheEvent(type, psz);
	return jkbotti_precache_event_native(psz, ret);
}

// The native build has no metamod to track user message registrations, so
// mirror the ones jk_botti itself resolves by name.  Without this the
// GET_USER_MSG_ID shim always returns 0: UTIL_HostSay then sends SayText as
// svc_bad (id 0), which the message hooks misread as WeaponList and crash.
// The bot-client observers (WeaponList/CurWeapon/...) intentionally stay as
// inert as they were before, so bot play behaviour is unchanged.
static int wrap_pfnRegUserMsg(const char *pszName, int iSize)
{
	int id = g_jkbotti_engine_original.pfnRegUserMsg(pszName, iSize);

	if (pszName && (!strcmp(pszName, "SayText") || !strcmp(pszName, "TextMsg")))
		JK_Botti_NativeRegisterUserMsg(pszName, id, iSize);

	return id;
}

void JK_Botti_InstallEngineHooks(enginefuncs_t *eng)
{
	eng->pfnPlaybackEvent = wrap_pfnPlaybackEvent;
	eng->pfnEmitSound = wrap_pfnEmitSound;
	eng->pfnChangeLevel = wrap_pfnChangeLevel;
	eng->pfnClientCommand = wrap_pfnClientCommand;
	eng->pfnMessageBegin = wrap_pfnMessageBegin;
	eng->pfnMessageEnd = wrap_pfnMessageEnd;
	eng->pfnWriteByte = wrap_pfnWriteByte;
	eng->pfnWriteChar = wrap_pfnWriteChar;
	eng->pfnWriteShort = wrap_pfnWriteShort;
	eng->pfnWriteLong = wrap_pfnWriteLong;
	eng->pfnWriteAngle = wrap_pfnWriteAngle;
	eng->pfnWriteCoord = wrap_pfnWriteCoord;
	eng->pfnWriteString = wrap_pfnWriteString;
	eng->pfnWriteEntity = wrap_pfnWriteEntity;
	eng->pfnClientPrintf = wrap_pfnClientPrintf;
	eng->pfnCmd_Args = wrap_pfnCmd_Args;
	eng->pfnCmd_Argv = wrap_pfnCmd_Argv;
	eng->pfnCmd_Argc = wrap_pfnCmd_Argc;
	eng->pfnSetClientMaxspeed = wrap_pfnSetClientMaxspeed;
	eng->pfnGetPlayerUserId = wrap_pfnGetPlayerUserId;
	eng->pfnPrecacheEvent = wrap_pfnPrecacheEvent;
	eng->pfnRegUserMsg = wrap_pfnRegUserMsg;
}
