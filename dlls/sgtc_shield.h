// sgtc_shield.h — personal shield wall entity (ref CShield @0x162388 / effect_shield).

#ifndef SGTC_SHIELD_H
#define SGTC_SHIELD_H

class CShield : public CBaseEntity
{
public:
	void Spawn( void );
	void Precache( void );
	void EXPORT Think( void );
	int Classify( void );	// ref @0x162210 — returns 0

	float m_flExpire;	// ref +0x19c — think gate vs gpGlobals->time
};

void SGTC_ShieldEmitArmImpactFlash( CBaseEntity *pShieldEmit );

#endif // SGTC_SHIELD_H
