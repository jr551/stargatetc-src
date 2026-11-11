//
// JK_Botti - be more human!
//
// bot_query_hook_osx.cpp
//
// Patsy/DSH port: macOS builds the bot module into the server dylib. The
// vendored hook rewrites x86 libc entry points (x87/endbr32/atomic asm), which
// does not apply on arm64 macOS, so query hooks are a no-op here. The engine
// still answers UDP server queries itself; only the bot's connection-time
// reordering hook is skipped.
//

#include "bot_query_hook.h"

bool hook_sendto_function(void)
{
   return false;
}

bool unhook_sendto_function(void)
{
   return true;
}
