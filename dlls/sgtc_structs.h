#ifndef SGTC_STRUCTS_H
#define SGTC_STRUCTS_H

class CGouStruct : public CBaseEntity
{
public:
	void Spawn( void );
	void Think( void );
	int Classify( void ) { return CLASS_NONE; }
};

class CTauStruct : public CBaseEntity
{
public:
	void Spawn( void );
	void Think( void );
	int Classify( void ) { return CLASS_NONE; }
};

class CGouStructActual : public CBaseMonster
{
public:
	void Spawn( void );
	void Precache( void );
	void Think( void );
	int Classify( void ) { return CLASS_HUMAN_MILITARY; }
	int TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType );
	void TraceAttack( entvars_t *pevAttacker, float flDamage, Vector vecDir, TraceResult *ptr, int bitsDamageType );

	int m_iLastHealthSnapshot;	// ref this+0x3e0
	int m_iMaxHP;			// ref this+0x3e4
	int m_iHealReadyFlag;		// ref this+0x3e8
	int m_iGlassGibsModel;		// ref this+0x3f0
	float m_flHealScheduleTime;	// ref this+0x3f4
	BOOL m_bDeathHandled;		// ref this+0x3ec
};

class CTauStructActual : public CBaseMonster
{
public:
	void Spawn( void );
	void Precache( void );
	void Think( void );
	int Classify( void ) { return CLASS_HUMAN_MILITARY; }
	int TakeDamage( entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType );
	void TraceAttack( entvars_t *pevAttacker, float flDamage, Vector vecDir, TraceResult *ptr, int bitsDamageType );

	int m_iLastHealthSnapshot;	// ref this+0x3e0
	int m_iMaxHP;			// ref this+0x3e4
	int m_iHealReadyFlag;		// ref this+0x3e8
	int m_iGlassGibsModel;		// ref this+0x3f0
	float m_flHealScheduleTime;	// ref this+0x3f4
	BOOL m_bDeathHandled;		// ref this+0x3ec
};

class CGouStructActualsb : public CBaseEntity
{
public:
	void Spawn( void );
	int Classify( void ) { return CLASS_HUMAN_MILITARY; }
};

class CTauStructActualsb : public CBaseEntity
{
public:
	void Spawn( void );
	int Classify( void ) { return CLASS_HUMAN_MILITARY; }
};

#endif
