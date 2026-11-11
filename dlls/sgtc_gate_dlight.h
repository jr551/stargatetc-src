// sgtc_gate_dlight.h - dynamic light driver for open Stargate event horizons.
//
// Detection is texture based: once per map the driver reads
// maps/<mapname>.bsp, matches event-horizon texture names in the miptex lump
// (horizon, sgeh, eventh, vortex, puddle, wormhole, naquada plus the comma
// separated sgtc_gate_dlight_textures cvar) and treats every brush model with
// matching faces - any classname - as a light source.  Matching world faces
// are clustered into coplanar gate-sized patches, each always on.  Brush
// brightness follows renderamt when the entity has a render mode, is full
// otherwise, and is off for EF_NODRAW or, with a render mode, renderamt 0.
//
// Maps without matching textures fall back to the original
// func_illusionary + kRenderTransTexture + targetname/bbox heuristic.
//
// Server only.  Tunable with "sgtc_gate_dlight" (default 1);
// "sgtc_gate_dlight_textures" (default "") adds extra texture substrings.

#ifndef SGTC_GATE_DLIGHT_H
#define SGTC_GATE_DLIGHT_H

// Register the driver's cvars; called from GameDLLInit().
void SGTC_GateDlightRegisterCvars( void );

// Spawn the per-map driver unless one already exists; called from
// CWorld::Precache().  Safe to call again on save restore.
void SGTC_GateDlightInstall( void );

#endif // SGTC_GATE_DLIGHT_H
