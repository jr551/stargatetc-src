/***
*
*	satchel.h — CSatchelCharge (monster_satchel), the one and only definition.
*
*	satchel.cpp implements the class; sgtc_misc.cpp (teleport) and
*	sgtc_round.cpp (new-round sweep) call into it. Include this header instead
*	of re-declaring a partial copy of the class (#97: differing definitions of
*	one class in several translation units are an ODR violation).
*
*	Needs extdll.h, util.h, cbase.h, monsters.h and weapons.h (CGrenade) first.
*
***/

#ifndef SATCHEL_H
#define SATCHEL_H

class CSatchelCharge : public CGrenade
{
	Vector m_lastBounceOrigin;
	BOOL m_bStuck;		// build-21: first surface contact attaches the charge	// Used to fix a bug in engine: when object isn't moving, but its speed isn't 0 and on ground isn't set
	void Spawn( void );
	void Precache( void );
	void BounceSound( void );

	void EXPORT SatchelSlide( CBaseEntity *pOther );
	void EXPORT SatchelThink( void );

public:
	void Deactivate( void );
	void Unstick( void );	// ref @0x1579a4 — teleport satchel path
};

#endif // SATCHEL_H
