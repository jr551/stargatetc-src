// Shared CRat declaration (implementation in rat.cpp).
// CSGTCSkull uses CRat AI shape; ref CRat::Think @0x14b50a for monster_skull.
#pragma once
#ifndef RAT_MONSTER_H
#define RAT_MONSTER_H

#include "cbase.h"
#include "monsters.h"

class CRat : public CBaseMonster
{
public:
	void Spawn( void );
	void Precache( void );
	void SetYawSpeed( void );
	int Classify( void );
};

#endif
