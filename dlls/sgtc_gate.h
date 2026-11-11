/***
*
*	sgtc_gate.h — StargateTC gate / world entities.
*
*	Clean-room re-implementation of ref/stc_i386.so (STC 2.0):
*	  - CMultiGate        : parameterized multi_manager clone. 49 subclasses
*	                        (CMultigate1 .. CMultigate49) each carry a
*	                        hard-coded default fire list seeded in Spawn().
*	  - CFuncTankGoauld   : func_tank variant ("Tourelle Goauld") that spawns
*	                        CLanceProj energy bolts instead of hitscan bullets.
*
*	Disasm references: CMultigateN::{Spawn,KeyValue,ManagerThink,ManagerUse,
*	Clone,IsClone,ShouldClone,ObjectCaps,Save,Restore},
*	CFuncTankGoauld::Fire @ 0x000eca62,
*	CLanceProj::CreateTourrelleProj @ 0x001000ce.
*
*	Note: game_player_equip / game_player_hurt / game_player_team are
*	byte-for-byte identical to the stock maprules.cpp classes already
*	registered in this tree (verified vs stc_i386.so @ 0x10a77a/0x10a48a/
*	0x10aa6a). They are NOT re-registered here — a second
*	LINK_ENTITY_TO_CLASS would collide on the exported "game_*" symbols.
*
***/

#ifndef SGTC_GATE_H
#define SGTC_GATE_H

// multi_gate_N spawnflags (same semantic as multi_manager)
#ifndef SF_MULTIMAN_CLONE
#define SF_MULTIMAN_CLONE	0x80000000	// flag: this is a clone for a threaded execution
#endif
#ifndef SF_MULTIMAN_THREAD
#define SF_MULTIMAN_THREAD	0x00000001	// flag: multithreaded — clone & fire in parallel
#endif

// CFuncTank's declaration lives in func_tank.h (shared with func_tank.cpp) so
// CFuncTankGoauld below can inherit from it.
#include "func_tank.h"

// func_tankgoauld bullet(4) "Energie" — the stock TANKBULLET enum stops at 3.
#define SGTC_TANK_BULLET_ENERGIE	((TANKBULLET)4)

// ---------------------------------------------------------------------------
// CMultiGate — the real STC mod ships 49 nearly-identical CMultigateN classes
// whose only difference is a per-class default fire list written into the
// first N slots of m_iTargetName/m_flTargetDelay during Spawn().
// One parameterized base reproduces all of them; each subclass supplies its
// own list through DefaultTargets().
//
// FGD keys (stargate.fgd): targetname + spawnflag 1 "multithreaded".
// The class ALSO accepts the multi_manager style keys on load:
//   "wait"            -> m_flWait  (float seconds)
//   <any other key>   -> target name to fire; its value is the delay.
// ---------------------------------------------------------------------------

class CMultiGate : public CBaseToggle
{
public:
	void		KeyValue( KeyValueData *pkvd );
	void		Spawn( void );
	void		EXPORT ManagerThink( void );
	void		EXPORT ManagerUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	void		Stop( void );	// ref CMultigateN::Stop @0x94cac (gate 1)

	BOOL		HasTarget( string_t targetname );

	int			ObjectCaps( void ) { return CBaseToggle::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );

	static TYPEDESCRIPTION m_SaveData[];

	int			m_cTargets;						// total number of targets in the fire list
	int			m_index;						// current target
	float		m_startTime;					// time we started firing
	string_t	m_iTargetName[MAX_MULTI_TARGETS];	// list of indexes into global string array
	float		m_flTargetDelay[MAX_MULTI_TARGETS];	// delay from manager fire to target fire

private:
	// NULL-terminated list of target names this gate always fires,
	// in order, with 0.0 delay. Provided by each generated subclass.
	virtual const char * const *DefaultTargets( void ) const = 0;

	inline BOOL IsClone( void ) { return ( pev->spawnflags & SF_MULTIMAN_CLONE ) ? TRUE : FALSE; }
	inline BOOL ShouldClone( void )
	{
		if( IsClone() )
			return FALSE;
		return ( pev->spawnflags & SF_MULTIMAN_THREAD ) ? TRUE : FALSE;
	}

	CMultiGate *Clone( void );
};

// Generates a concrete gate subclass, its default-target list, and the
// entity factory. Produces class CMultigate<N> registered as multi_gate_<N>.
#define SGTC_DEFINE_MULTIGATE( N, ... )											\
	class CMultigate##N : public CMultiGate										\
	{																			\
	public:																		\
		const char * const *DefaultTargets( void ) const						\
		{																		\
			static const char * const s_szTargets[] = { __VA_ARGS__, NULL };	\
			return s_szTargets;													\
		}																		\
	};																			\
	LINK_ENTITY_TO_CLASS( multi_gate_##N, CMultigate##N )

// ---------------------------------------------------------------------------
// CFuncTankGoauld — "Tourelle Goauld" (func_tankgoauld).
// Extends CFuncTank; overrides Fire so bullet(4) "Energie" launches
// CLanceProj projectiles (as the real mod does) instead of hitscan tracers.
// bullet 0..3 fire the stock hitscan rounds (CFuncTank::Fire itself only
// handles sprites and targets, so the override repeats CFuncTankGun's loop).
// At most 4 rounds are fired per call.
// ---------------------------------------------------------------------------

class CFuncTankGoauld : public CFuncTank
{
public:
	virtual void Fire( const Vector &barrelEnd, const Vector &forward, entvars_t *pevAttacker );
};

#endif // SGTC_GATE_H
