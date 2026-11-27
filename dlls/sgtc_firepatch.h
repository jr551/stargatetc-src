// sgtc_firepatch.h - pure fire-patch cap decision for SGTC_FirePatch_Spawn.
//
// Fire patches (dlls/sgtc_c4fire.cpp) are cosmetic, but each one consumes an
// edict. A burst such as repeated Zat impacts (dlls/sgtc_misc.cpp) or the
// `firepatchburst` test command must not exhaust the server's edict pool.
//
// The decision is a pure function over plain integers so the host-side
// feature test can link it without an engine. The engine wrapper
// (dlls/sgtc_c4fire.cpp) counts live `sgtc_c4_fire` edicts, reads
// NUMBER_OF_ENTITIES()/gpGlobals->maxEntities and does the logging.
#pragma once
#ifndef SGTC_FIREPATCH_H
#define SGTC_FIREPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

// Maximum simultaneously live fire patches.
#define SGTC_FIREPATCH_MAX_ACTIVE 64
// Edicts held back from fire patches so gameplay spawns keep headroom.
#define SGTC_FIREPATCH_EDICT_RESERVE 8

// 1 = allow one more fire patch, 0 = refuse.
//
// Refuses when any input is unusable (active < 0, used < 0,
// max_edicts <= SGTC_FIREPATCH_EDICT_RESERVE), when the active count has
// reached SGTC_FIREPATCH_MAX_ACTIVE, or when `used` has reached
// max_edicts - SGTC_FIREPATCH_EDICT_RESERVE.
int SGTC_FirePatch_Allows( int active, int used, int max_edicts );

#ifdef __cplusplus
}
#endif

#endif // SGTC_FIREPATCH_H
