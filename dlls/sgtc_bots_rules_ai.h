// sgtc_bots_rules_ai.h — build-14 rules-aware bot decisions.
//
// Consumes the SGTC rules API (visibility / TER / shield / weapon knowledge),
// the runtime mechanics registry (SGTC_Mech_* gate/ring/door/iris state) and
// the compiled-in BSP map semantics (game_shared/sgtc_mapmeta_gen.h) to make
// bots obey the mod's gameplay rules instead of only geometry.
//
// Server-side only; compiled under BUILD_JK_BOTTI.
#pragma once

// Called once at worldspawn after the waypoint file and nav links are loaded.
void SGTC_Bots_RulesAI_Init( void );

// Called every server frame after jk_botti has thought.  Applies visibility /
// TER / shield rules and gate/ring/door/hazard routing to every live bot.
void SGTC_Bots_RulesAI_Frame( void );

// Probe helper for sgtc_test (headless verification): fills buf with a compact
// JSON object describing bot iIndex's rule state.  Returns 0 when written.
int SGTC_Bots_RulesAI_Probe( int iIndex, char *buf, int buflen );

// Compact mapmeta summary (objective anchors + teleport graph + gates/rings +
// hazard volumes) for the bot director LLM prompt; empty when no mapmeta.
const char *SGTC_Bots_RulesAI_MapSummary( void );

// True once Init found compiled map metadata for the current map.
int SGTC_Bots_RulesAI_HasMapMeta( void );

// Test-only hook (sgtc_test forcecloak): makes classplay want the cloak for one
// bot index so headless probes can exercise engage -> bounded hold -> release
// through the real weapon_invis toggle path. Inert unless set.
int SGTC_Bots_RulesAI_DebugForceCloak( int iBotIndex, int iOn );
int SGTC_Bots_RulesAI_ForceCloakActive( int iBotIndex );
