/***
*
*	triggers.h — CMultiManager (multi_manager), the one and only definition.
*
*	triggers.cpp implements the class; sgtc_round.cpp calls Stop() from its
*	new-round sweep. Include this header instead of re-declaring a partial copy
*	of the class (#97: differing definitions of one class in several translation
*	units are an ODR violation).
*
*	Needs extdll.h, util.h and cbase.h first.
*
***/

#ifndef TRIGGERS_H
#define TRIGGERS_H

#ifndef SF_MULTIMAN_CLONE
#define SF_MULTIMAN_CLONE		0x80000000
#endif
#ifndef SF_MULTIMAN_THREAD
#define SF_MULTIMAN_THREAD		0x00000001
#endif

class CMultiManager : public CBaseToggle
{
public:
	void KeyValue( KeyValueData *pkvd );
	void Spawn( void );
	void EXPORT ManagerThink( void );
	void EXPORT ManagerUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	void Stop( void );	// ref CMultiManager::Stop @nm 0x196e6c — NewRound mid-fire abort

#if _DEBUG
	void EXPORT ManagerReport( void );
#endif
	BOOL HasTarget( string_t targetname );
	
	int ObjectCaps( void ) { return CBaseToggle::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );

	static TYPEDESCRIPTION m_SaveData[];

	int m_cTargets; // the total number of targets in this manager's fire list.
	int m_index;	// Current target
	float m_startTime;// Time we started firing
	string_t m_iTargetName[MAX_MULTI_TARGETS];// list if indexes into global string array
	float m_flTargetDelay[MAX_MULTI_TARGETS];// delay (in seconds) from time of manager fire to target fire
private:
	inline BOOL IsClone( void ) { return ( pev->spawnflags & SF_MULTIMAN_CLONE ) ? TRUE : FALSE; }
	inline BOOL ShouldClone( void )
	{ 
		if( IsClone() )
			return FALSE;

		return ( pev->spawnflags & SF_MULTIMAN_THREAD ) ? TRUE : FALSE; 
	}

	CMultiManager *Clone( void );
};

#endif // TRIGGERS_H
