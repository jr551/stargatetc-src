#ifndef SGTC_ZONES_H
#define SGTC_ZONES_H

// sgtc_zones.cpp exposes its TU-local zone classes to peers via free helpers.
// NewRound drives zone_capture state resets through this entry point
// (ref NewRound @0x187b34 zone_capture walk).
void SGTC_Zones_ResetCaptureZones( void );
void SGTC_Zones_ResetBombTargets( void );

#endif // SGTC_ZONES_H
