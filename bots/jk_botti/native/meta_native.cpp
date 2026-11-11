#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../dlls/extdll.h"
#include "../metamod/plinfo.h"
#include "../metamod/meta_api.h"
#include "meta_native.h"
#include "../bot.h"
#include "../util.h"

plugin_info_t Plugin_info = {
	META_INTERFACE_VERSION,
	"JK_Botti",
	"native-sgtc",
	__DATE__,
	"Jussi Kivilinna",
	"https://github.com/Bots-United/jk_botti",
	"JK_BOTTI",
	PT_STARTUP,
	PT_ANYTIME,
};

meta_globals_t g_jkbotti_meta_globals;
gamedll_funcs_t g_jkbotti_gamedll_funcs;
mutil_funcs_t g_jkbotti_mutil_funcs;

enginefuncs_t g_jkbotti_engine_original;

static void native_log_console(plid_t plid, const char *fmt, ...)
{
	(void)plid;
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	UTIL_ConsolePrintf("%s", buf);
}

static void native_log_message(plid_t plid, const char *fmt, ...)
{
	(void)plid;
	(void)fmt;
}

static void native_log_error(plid_t plid, const char *fmt, ...)
{
	(void)plid;
	(void)fmt;
}

static void native_log_developer(plid_t plid, const char *fmt, ...)
{
	(void)plid;
	(void)fmt;
}

static qboolean native_call_game_entity(plid_t plid, const char *entStr, entvars_t *pev)
{
	(void)plid;
	(void)entStr;
	(void)pev;
	return 0;
}

// The engine owns the user message ids: pfnRegUserMsg hands them out and there
// is no name lookup.  Metamod normally tracks registrations for the
// GET_USER_MSG_ID shim; the native build has no metamod, so keep the same
// registry here and fill it from the pfnRegUserMsg wrapper in engine_hooks.cpp.
#define NATIVE_USER_MSG_MAX       128
#define NATIVE_USER_MSG_NAME_LEN  32

typedef struct
{
	char name[NATIVE_USER_MSG_NAME_LEN];
	int  id;
	int  size;
} native_user_msg_t;

static native_user_msg_t native_user_msgs[NATIVE_USER_MSG_MAX];
static int native_user_msg_count;

void JK_Botti_NativeRegisterUserMsg(const char *name, int id, int size)
{
	int i;

	if (!name || !name[0] || id <= 0)
		return;

	for (i = 0; i < native_user_msg_count; i++)
	{
		if (!strcmp(native_user_msgs[i].name, name))
		{
			native_user_msgs[i].id = id;
			native_user_msgs[i].size = size;
			return;
		}
	}

	if (native_user_msg_count >= NATIVE_USER_MSG_MAX)
		return;

	i = native_user_msg_count++;
	snprintf(native_user_msgs[i].name, sizeof(native_user_msgs[i].name), "%s", name);
	native_user_msgs[i].id = id;
	native_user_msgs[i].size = size;
}

static int native_get_user_msg_id(plid_t plid, const char *msgname, int *size)
{
	int i;

	(void)plid;

	if (msgname && msgname[0])
	{
		for (i = 0; i < native_user_msg_count; i++)
		{
			if (!strcmp(native_user_msgs[i].name, msgname))
			{
				if (size)
					*size = native_user_msgs[i].size;
				return native_user_msgs[i].id;
			}
		}
	}

	if (size)
		*size = 0;
	return 0;
}

JK_BOTTI_LINKAGE void JK_Botti_NativeInit(DLL_FUNCTIONS *gameApi, enginefuncs_t *engineApi)
{
	memset(&g_jkbotti_meta_globals, 0, sizeof(g_jkbotti_meta_globals));
	memset(&g_jkbotti_gamedll_funcs, 0, sizeof(g_jkbotti_gamedll_funcs));
	memset(&g_jkbotti_mutil_funcs, 0, sizeof(g_jkbotti_mutil_funcs));

	g_jkbotti_gamedll_funcs.dllapi_table = gameApi;
	g_jkbotti_gamedll_funcs.newapi_table = NULL;

	g_jkbotti_mutil_funcs.pfnLogConsole = native_log_console;
	g_jkbotti_mutil_funcs.pfnLogMessage = native_log_message;
	g_jkbotti_mutil_funcs.pfnLogError = native_log_error;
	g_jkbotti_mutil_funcs.pfnLogDeveloper = native_log_developer;
	g_jkbotti_mutil_funcs.pfnCallGameEntity = native_call_game_entity;
	g_jkbotti_mutil_funcs.pfnGetUserMsgID = native_get_user_msg_id;

	memcpy(&g_jkbotti_engine_original, engineApi, sizeof(enginefuncs_t));
	JK_Botti_InstallEngineHooks(engineApi);
}

// Director glue (dlls/sgtc_director.cpp). Chat goes through the normal bot say
// path so it is formatted, rate limited and delivered exactly like jk_botti's
// own lines; the director never touches engine message calls directly.
JK_BOTTI_LINKAGE void JK_Botti_Say(edict_t *pEdict, const char *text)
{
	if (!pEdict || !text || !text[0])
		return;
	UTIL_HostSay(pEdict, 0, (char *)text);
}

JK_BOTTI_LINKAGE int JK_Botti_Count(void)
{
	int count = 0;
	for (int i = 0; i < 32; i++)
	{
		if (bots[i].is_used && bots[i].pEdict && !FNullEnt(bots[i].pEdict))
			count++;
	}
	return count;
}
