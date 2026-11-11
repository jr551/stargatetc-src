// Patsy/DSH port: macOS has no <malloc.h>; jk_botti includes it directly.
// Forward to the standard header so the vendored sources build unmodified.
// Also provides alloca() for compilers whose <stdlib.h> does not (MinGW picks
// this shim up through -I${JK_BOTTI_ROOT}, shadowing its own malloc.h).
#ifndef SGTC_COMPAT_MALLOC_H
#define SGTC_COMPAT_MALLOC_H
#include <stdlib.h>
#if defined(__GNUC__) && !defined(__clang__) && !defined(alloca)
#define alloca __builtin_alloca
#endif
#endif
