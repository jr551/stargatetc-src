// sgtc_wpn_c4.h — weapon_c4 is linked to CSgtcC4 (sgtc_links.cpp). The stock
// CSatchel logic is unchanged; the subclass only adds the shared throw playback.
#pragma once
#include "weapons.h"

class CSgtcC4 : public CSatchel
{
public:
	void Precache( void );
	void PrimaryAttack( void );
};
