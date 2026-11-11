#ifndef JK_BOTTI_META_NATIVE_H
#define JK_BOTTI_META_NATIVE_H

#include "../metamod/meta_api.h"
#include "../metamod/log_meta.h"
#include "../metamod/mutil.h"

extern meta_globals_t g_jkbotti_meta_globals;
extern gamedll_funcs_t g_jkbotti_gamedll_funcs;
extern mutil_funcs_t g_jkbotti_mutil_funcs;
extern enginefuncs_t g_jkbotti_engine_original;

#define gpMetaGlobals (&g_jkbotti_meta_globals)
#define gpGamedllFuncs (&g_jkbotti_gamedll_funcs)
#define gpMetaUtilFuncs (&g_jkbotti_mutil_funcs)

#undef RETURN_META
#undef RETURN_META_VALUE
#define RETURN_META(result) \
	do { gpMetaGlobals->mres = (result); return; } while (0)
#define RETURN_META_VALUE(result, value) \
	do { gpMetaGlobals->mres = (result); return (value); } while (0)

void JK_Botti_InstallEngineHooks(enginefuncs_t *eng);

// User message ids are owned by the engine and have no name lookup, so the
// native build mirrors the registrations jk_botti resolves by name for the
// metamod GET_USER_MSG_ID shim. Called from the pfnRegUserMsg wrapper.
void JK_Botti_NativeRegisterUserMsg(const char *name, int id, int size);

#endif
