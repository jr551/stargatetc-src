// sgtc_protocol.h — StargateTC client protocol registrations (NEW file, VGUI-less).
//
// Covers the SGTC delta over the xash stock client:
//   - 27 custom user messages (7 env/particle + 19 HUD + the Concuss override)
//   - 33 events/*.sc scripts present on disk (plus stock overlap notes)
//   - SGTC HUD/effect sprite + custom weapon/radio sound tables
// Stock files are NEVER edited; see yield report for one-line hook points.
#pragma once

// Called once from HUD_Init (after stock CHud::Init).
void SGTC_ClientProtocol_InitMessages( void );
void SGTC_ClientHUD_Init( void );
void SGTC_ClientHUD_VidInit( void );
// Called from HUD_Frame (every host frame) — music level resolve/cvar tracking
// and the music options panel's console-close check.
void SGTC_ClientHUD_Frame( void );
// Called from the event-hook chain (after stock Game_HookEvents).
void SGTC_ClientProtocol_HookEvents( void );
// Called from HUD_VidInit (after stock CHud::VidInit) — SPR_Loads SGTC sprites.
void SGTC_ClientProtocol_VidInit( void );

// Counts for acceptance / coverage checks.
int SGTC_CustomMessageCount( void );
int SGTC_CustomEventCount( void );
