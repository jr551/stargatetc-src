/***
*
*	sgtc_gate.cpp — StargateTC gate / world entities.
*
*	Clean-room re-implementation of ref/stc_i386.so (STC 2.0):
*	  multi_gate_1..49 : 49 "Auto MultiTarget Manager" aliases. The real mod
*	                      ships 49 nearly-identical CMultigateN classes
*	                      (decorated, copyright*.cpp-era code); the only
*	                      behavioral difference between them is the default
*	                      fire list each Spawn() seeds. One parameterized
*	                      CMultiGate base reproduces all of them.
*	  func_tankgoauld  : "Tourelle Goauld" (func_tank with bullet=4 "Energie");
*	                      Fire() launches CLanceProj energy bolts.
*
*	Keyvalues (per stargate.fgd + ref disasm):
*	  multi_gate_N    : targetname, spawnflag 1 "multithreaded".
*	                    Also honors multi_manager style runtime keys:
*	                    "wait" -> m_flWait; any other key -> target name,
*	                    its value is the delay in seconds (up to 16 targets).
*	  func_tankgoauld : full func_tank keyvalue set (yawrange/yawrate/
*	                    yawtolerance/pitchrange/pitchrate/pitchtolerance/
*	                    firerate/barrel/barrely/barrelz/spritescale/
*	                    spritesmoke/spriteflash/rotatesound/persistence/
*	                    bullet (4 = Energie)/bullet_damage/firespread/
*	                    minRange/maxRange/master) — inherited, unchanged.
*
*	Disasm references:
*	  CMultigateN::{Spawn,KeyValue,ManagerThink,ManagerUse} (+Stop/Clone/
*	    HasTarget/IsClone/ShouldClone/ObjectCaps/Save/Restore)
*	  CFuncTankGoauld::Fire @ 0x000eca62
*	  CLanceProj::CreateTourrelleProj(Vector, Vector) @ 0x001000ce
*
*	Cross-file contract: CLanceProj::CreateTourrelleProj(Vector, Vector) is
*	implemented by the lance weapon file (real mod: big_lance.cpp ->
*	CLanceProj). The declaration below pins the exact mangled signature
*	_ZN10CLanceProj19CreateTourrelleProjE6VectorS0_ seen in stc_i386.so.
*
***/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "sgtc_gate.h"

// ---------------------------------------------------------------------------
// CMultiGate — shared implementation
// ---------------------------------------------------------------------------

TYPEDESCRIPTION CMultiGate::m_SaveData[] =
{
	DEFINE_FIELD( CMultiGate, m_cTargets, FIELD_INTEGER ),
	DEFINE_FIELD( CMultiGate, m_index, FIELD_INTEGER ),
	DEFINE_FIELD( CMultiGate, m_startTime, FIELD_TIME ),
	DEFINE_ARRAY( CMultiGate, m_iTargetName, FIELD_STRING, MAX_MULTI_TARGETS ),
	DEFINE_ARRAY( CMultiGate, m_flTargetDelay, FIELD_FLOAT, MAX_MULTI_TARGETS ),
};

IMPLEMENT_SAVERESTORE( CMultiGate, CBaseToggle )

void CMultiGate::KeyValue( KeyValueData *pkvd )
{
	// Same shape as CMultiManager::KeyValue: "wait" sets m_flWait, every
	// other key is treated as a target name whose value is the delay.
	if( FStrEq( pkvd->szKeyName, "wait" ) )
	{
		m_flWait = atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
	{
		// this assumes additional fields are targetnames and their values
		// are delay values.
		if( m_cTargets < MAX_MULTI_TARGETS )
		{
			char tmp[128];

			UTIL_StripToken( pkvd->szKeyName, tmp, sizeof( tmp ) );
			m_iTargetName[m_cTargets] = ALLOC_STRING( tmp );
			m_flTargetDelay[m_cTargets] = atof( pkvd->szValue );
			m_cTargets++;
			pkvd->fHandled = TRUE;
		}
	}
}

void CMultiGate::Spawn( void )
{
	pev->solid = SOLID_NOT;
	SetUse( &CMultiGate::ManagerUse );
	SetThink( &CMultiGate::ManagerThink );

	// Seed this gate's default fire list (each target fires with 0.0 delay).
	// This mirrors the real mod, whose per-class Spawn() writes a fixed
	// list of target strings straight into m_iTargetName[...].
	// Append defaults; KeyValue targets loaded before Spawn are kept.
	for( const char * const *ppsz = DefaultTargets(); ppsz && *ppsz && m_cTargets < MAX_MULTI_TARGETS; ppsz++ )
	{
		m_iTargetName[m_cTargets] = ALLOC_STRING( *ppsz );
		m_flTargetDelay[m_cTargets] = 0.0f;
		m_cTargets++;
	}
}

BOOL CMultiGate::HasTarget( string_t targetname )
{
	for( int i = 0; i < m_cTargets; i++ )
		if( FStrEq( STRING( targetname ), STRING( m_iTargetName[i] ) ) )
			return TRUE;

	return FALSE;
}

void CMultiGate::Stop( void )
{
	// ref CMultigate1::Stop @0x94cac — drop think; re-arm use or remove clone.
	SetThink( NULL );
	pev->nextthink = 0;
	if( IsClone() )
		UTIL_Remove( this );
	else
		SetUse( &CMultiGate::ManagerUse );
}

// Fires targets whose delay has elapsed since ManagerUse().
void CMultiGate::ManagerThink( void )
{
	float time = gpGlobals->time - m_startTime;

	while( m_index < m_cTargets && m_flTargetDelay[m_index] <= time )
	{
		FireTargets( STRING( m_iTargetName[m_index] ), m_hActivator, this, USE_TOGGLE, 0 );
		m_index++;
	}

	if( m_index >= m_cTargets ) // have we fired all targets?
	{
		SetThink( NULL );
		if( IsClone() )
		{
			UTIL_Remove( this );
			return;
		}
		SetUse( &CMultiGate::ManagerUse ); // allow manager re-use
	}
	else
		pev->nextthink = m_startTime + m_flTargetDelay[m_index];
}

CMultiGate *CMultiGate::Clone( void )
{
	// GetClassPtr<CMultiGate> cannot be used here: CMultiGate is abstract
	// (pure DefaultTargets()), so placement-new on the base won't compile.
	// Instead spawn a fresh entity of this object's concrete classname
	// (multi_gate_N) so the engine instantiates the matching CMultigateN
	// with its own default target list; the fire list is overwritten
	// below anyway.
	edict_t *pent = CREATE_NAMED_ENTITY( pev->classname );
	if( FNullEnt( pent ) )
		return NULL;

	CMultiGate *pMulti = (CMultiGate *)CBaseEntity::Instance( pent );
	if( pMulti == NULL )
		return NULL;

	edict_t *pEdict = pMulti->pev->pContainingEntity;
	memcpy( pMulti->pev, pev, sizeof(*pev) );
	pMulti->pev->pContainingEntity = pEdict;

	pMulti->pev->spawnflags |= SF_MULTIMAN_CLONE;
	pMulti->m_cTargets = m_cTargets;
	memcpy( pMulti->m_iTargetName, m_iTargetName, sizeof( m_iTargetName ) );
	memcpy( pMulti->m_flTargetDelay, m_flTargetDelay, sizeof( m_flTargetDelay ) );

	return pMulti;
}

// The USE function builds the time table and starts the entity thinking.
void CMultiGate::ManagerUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// if on a threaded (multi_use) manager, make a clone to do the work.
	if( ShouldClone() )
	{
		ALERT( at_console, "good map : %s \n", STRING( gpGlobals->mapname ) );
		CMultiGate *pClone = Clone();
		if( pClone )
			pClone->ManagerUse( pActivator, pCaller, useType, value );
		return;
	}

	m_hActivator = pActivator;
	m_index = 0;
	m_startTime = gpGlobals->time;

	SetUse( NULL );
	SetThink( &CMultiGate::ManagerThink );
	pev->nextthink = gpGlobals->time;
}

// ---------------------------------------------------------------------------
// multi_gate_1 .. multi_gate_49 aliases.
// Default fire lists decoded from ref/stc_i386.so CMultigateN::Spawn bodies
// (verified byte-for-byte against every one of the 49 Spawn functions).
// ---------------------------------------------------------------------------

SGTC_DEFINE_MULTIGATE( 1, "chevronsnd", "dial1change1", "dhdglyph1_on", "ch1relayon" );
SGTC_DEFINE_MULTIGATE( 2, "chevronsnd", "dial1change2", "dhdglyph2_on", "ch2relayon" );
SGTC_DEFINE_MULTIGATE( 3, "chevronsnd", "dial1change3", "dhdglyph3_on", "ch3relayon" );
SGTC_DEFINE_MULTIGATE( 4, "chevronsnd", "dial1change4", "dhdglyph4_on", "ch4relayon" );
SGTC_DEFINE_MULTIGATE( 5, "chevronsnd", "dial1change5", "dhdglyph5_on", "ch5relayon" );
SGTC_DEFINE_MULTIGATE( 6, "chevronsnd", "dial1change6", "dhdglyph6_on", "ch6relayon" );
SGTC_DEFINE_MULTIGATE( 7, "chevronsnd", "dial1change7", "dhdglyph7_on", "ch7relayon" );
SGTC_DEFINE_MULTIGATE( 8, "2chmm", "dhdglyph8_on", "2dhdchevronsoff", "dial1off", "closing_but_on", "gateclose_tr_on_tr_on", "2chevronsnd", "stage8b", "sonvortex", "hurt2x", "opening", "g1tele_x_on", "wormtelex_2", "vortex5" );
SGTC_DEFINE_MULTIGATE( 9, "dial2off", "insplash_tr1_on", "outsplash_tr2_on", "opening_spr", "gxtele_1_on_tr_on1", "gxtele_2_on_tr_on1", "gxtele_3_on_tr_on1", "gxtele_4_on_tr_on1", "gxtele_5_on_tr_on1" );
SGTC_DEFINE_MULTIGATE( 10, "2chevronsnd", "dial2change1", "2dhdglyph1_on", "2ch1relayon" );
SGTC_DEFINE_MULTIGATE( 11, "2chevronsnd", "dial2change2", "2dhdglyph2_on", "2ch2relayon" );
SGTC_DEFINE_MULTIGATE( 12, "2chevronsnd", "dial2change3", "2dhdglyph3_on", "2ch3relayon" );
SGTC_DEFINE_MULTIGATE( 13, "2chevronsnd", "dial2change4", "2dhdglyph4_on", "2ch4relayon" );
SGTC_DEFINE_MULTIGATE( 14, "2chevronsnd", "dial2change5", "2dhdglyph5_on", "2ch5relayon" );
SGTC_DEFINE_MULTIGATE( 15, "2chevronsnd", "dial2change6", "2dhdglyph6_on", "2ch6relayon" );
SGTC_DEFINE_MULTIGATE( 16, "2chevronsnd", "dial2change7", "2dhdglyph7_on", "2ch7relayon" );
SGTC_DEFINE_MULTIGATE( 17, "chmm", "2dhdglyph8_on", "dhdchevronsoff", "dial2off", "closing_butb_on", "gateclose_tr_on_trb_on", "2chevronsnd", "2stage8b", "sonvortex", "hurt1x", "opening", "g2tele_x_on", "wormtelex_1", "vortex5" );
SGTC_DEFINE_MULTIGATE( 18, "dial1off", "insplash_tr2_on", "outsplash_tr1_on", "opening_spr", "gxtele_1_on_tr_on2", "gxtele_2_on_tr_on2", "gxtele_3_on_tr_on2", "gxtele_4_on_tr_on2", "gxtele_5_on_tr_on2" );
SGTC_DEFINE_MULTIGATE( 19, "vo1", "vo2", "vortexlight", "hurt1_on", "2hurt1_on", "vo3", "vo4", "vo5", "openingb" );
SGTC_DEFINE_MULTIGATE( 20, "vo6", "vo7", "vo8", "vo9", "eh", "hurt1_off", "2hurt1_off", "closing_but" );
SGTC_DEFINE_MULTIGATE( 21, "hurt2x", "sonvortexferme", "vortex0", "eh", "closingwhite_spr", "g1tele_x_off", "closing_spr", "closingwhite_spr#1", "vortexlight", "chmmoff", "2chmmoff", "dhdchevronsoff", "dial1on", "dial2on" );
SGTC_DEFINE_MULTIGATE( 22, "sonvortexferme", "hurt1x", "vortex0", "eh", "g2tele_x_off", "closingwhite_spr", "closing_spr", "closingwhite_spr#1", "vortexlight", "2chmmoff", "chmmoff", "2dhdchevronsoff", "dial1on", "dial2on" );
SGTC_DEFINE_MULTIGATE( 23, "gateclose_tr_off", "closing_but2_on", "closing_but2" );
SGTC_DEFINE_MULTIGATE( 24, "gateclose_tr_off", "closing_but2b_on", "closing_but2" );
SGTC_DEFINE_MULTIGATE( 25, "ch1relayon", "ch2relayon", "ch3relayon", "ch4relayon", "ch5relayon", "ch6relayon", "ch7relayon" );
SGTC_DEFINE_MULTIGATE( 26, "2ch1relayon", "2ch2relayon", "2ch3relayon", "2ch4relayon", "2ch5relayon", "2ch6relayon", "2ch7relayon" );
SGTC_DEFINE_MULTIGATE( 27, "ch1relayoff", "ch2relayoff", "ch3relayoff", "ch4relayoff", "ch5relayoff", "ch6relayoff", "ch7relayoff" );
SGTC_DEFINE_MULTIGATE( 28, "2ch1relayoff", "2ch2relayoff", "2ch3relayoff", "2ch4relayoff", "2ch5relayoff", "2ch6relayoff", "2ch7relayoff" );
SGTC_DEFINE_MULTIGATE( 29, "dhdglyph1_off", "dhdglyph2_off", "dhdglyph3_off", "dhdglyph4_off", "dhdglyph5_off", "dhdglyph6_off", "dhdglyph7_off", "dhdglyph8_off" );
SGTC_DEFINE_MULTIGATE( 30, "2dhdglyph1_off", "2dhdglyph2_off", "2dhdglyph3_off", "2dhdglyph4_off", "2dhdglyph5_off", "2dhdglyph6_off", "2dhdglyph7_off", "2dhdglyph8_off" );
SGTC_DEFINE_MULTIGATE( 31, "g1tele_1_on", "g1tele_2_on", "g1tele_3_on", "g1tele_4_on", "g1tele_5_on" );
SGTC_DEFINE_MULTIGATE( 32, "g2tele_1_on", "g2tele_2_on", "g2tele_3_on", "g2tele_4_on", "g2tele_5_on" );
SGTC_DEFINE_MULTIGATE( 33, "g1tele_1_off", "g1tele_2_off", "g1tele_3_off", "g1tele_4_off", "g1tele_5_off" );
SGTC_DEFINE_MULTIGATE( 34, "g2tele_1_off", "g2tele_2_off", "g2tele_3_off", "g2tele_4_off", "g2tele_5_off" );
SGTC_DEFINE_MULTIGATE( 35, "wormtele1_1", "wormtele2_1", "wormtele3_1", "wormtele4_1", "wormtele5_1" );
SGTC_DEFINE_MULTIGATE( 36, "wormtele1_2", "wormtele2_2", "wormtele3_2", "wormtele4_2", "wormtele5_2" );
SGTC_DEFINE_MULTIGATE( 37, "g1tele_1_off", "g2tele_1_off", "wormtrain1" );
SGTC_DEFINE_MULTIGATE( 38, "g1tele_2_off", "g2tele_2_off", "wormtrain2" );
SGTC_DEFINE_MULTIGATE( 39, "g1tele_3_off", "g2tele_3_off", "wormtrain3" );
SGTC_DEFINE_MULTIGATE( 40, "g1tele_4_off", "g2tele_4_off", "wormtrain4" );
SGTC_DEFINE_MULTIGATE( 41, "g1tele_5_off", "g2tele_5_off", "wormtrain5" );
SGTC_DEFINE_MULTIGATE( 42, "worm_tele_master1", "worm_tele_master1#1" );
SGTC_DEFINE_MULTIGATE( 43, "worm_tele_master2", "worm_tele_master2#1" );
SGTC_DEFINE_MULTIGATE( 44, "worm_tele_master3", "worm_tele_master3#1" );
SGTC_DEFINE_MULTIGATE( 45, "worm_tele_master4", "worm_tele_master4#1" );
SGTC_DEFINE_MULTIGATE( 46, "worm_tele_master5", "worm_tele_master5#1" );
SGTC_DEFINE_MULTIGATE( 47, "gate_push1", "splash1", "gate_push1#1" );
SGTC_DEFINE_MULTIGATE( 48, "gate_push2", "splash2", "gate_push2#1" );
SGTC_DEFINE_MULTIGATE( 49, "chmmoff", "2chmmoff", "closingwhite_spr_off" );

// ---------------------------------------------------------------------------
// CFuncTankGoauld — "Tourelle Goauld"
// ---------------------------------------------------------------------------

// Energy-bolt factory — implemented in sgtc_lanceproj.cpp (pins the exact
// ref/stc_i386.so signature so the Fire() body below links unchanged).
#include "sgtc_lanceproj.h"

LINK_ENTITY_TO_CLASS( func_tankgoauld, CFuncTankGoauld );

// Rounds per Fire() call are derived from map data (firerate) and the time since
// the last shot, so a high rate or a server stall could ask for hundreds. Each
// energy bolt is an edict, so bound it.
#define SGTC_TANK_MAX_ROUNDS_PER_FIRE	4

// Same values as gTankSpread in func_tank.cpp, which is static there.
static const Vector s_vecTankSpread[] =
{
	Vector( 0, 0, 0 ),		// perfect
	Vector( 0.025, 0.025, 0.025 ),	// small cone
	Vector( 0.05, 0.05, 0.05 ),	// medium cone
	Vector( 0.1, 0.1, 0.1 ),	// large cone
	Vector( 0.25, 0.25, 0.25 ),	// extra-large cone
};

// Same timing shape as CFuncTankGun::Fire. Bullet types 0..3 fire the stock
// hitscan rounds (copied from CFuncTankGun::Fire, whose class is private to
// func_tank.cpp); bullet(4) "Energie" spawns a CLanceProj per round and imparts
// its velocity instead. The velocity math is decoded from
// CFuncTankGoauld::Fire @ 0x000eca62:
//   pProj->pev->velocity = ( pProj->pev->velocity + gpGlobals->v_up * -112.0f ) * 6.0f
void CFuncTankGoauld::Fire( const Vector &barrelEnd, const Vector &forward, entvars_t *pevAttacker )
{
	if( m_fireLast != 0 )
	{
		// Figure out how many rounds got fired since the last frame.
		UTIL_MakeAimVectors( pev->angles );
		int bulletCount = (int)( ( gpGlobals->time - m_fireLast ) * m_fireRate );

		if( bulletCount > 0 )
		{
			if( bulletCount > SGTC_TANK_MAX_ROUNDS_PER_FIRE )
				bulletCount = SGTC_TANK_MAX_ROUNDS_PER_FIRE;

			const int iBulletType = (int)m_bulletType;
			int iSpread = m_spread;
			if( iSpread < 0 || iSpread >= (int)ARRAYSIZE( s_vecTankSpread ) )
				iSpread = 0;

			for( int i = 0; i < bulletCount; i++ )
			{
				if( iBulletType == (int)SGTC_TANK_BULLET_ENERGIE )
				{
					CLanceProj *pProj = CLanceProj::CreateTourrelleProj( barrelEnd, forward );

					if( pProj )
					{
						pProj->pev->velocity = ( pProj->pev->velocity + gpGlobals->v_up * -112.0f ) * 6.0f;
					}
				}
				else if( iBulletType == (int)TANK_BULLET_9MM )
					FireBullets( 1, barrelEnd, forward, s_vecTankSpread[iSpread], 4096, BULLET_MONSTER_9MM, 1, m_iBulletDamage, pevAttacker );
				else if( iBulletType == (int)TANK_BULLET_MP5 )
					FireBullets( 1, barrelEnd, forward, s_vecTankSpread[iSpread], 4096, BULLET_MONSTER_MP5, 1, m_iBulletDamage, pevAttacker );
				else if( iBulletType == (int)TANK_BULLET_12MM )
					FireBullets( 1, barrelEnd, forward, s_vecTankSpread[iSpread], 4096, BULLET_MONSTER_12MM, 1, m_iBulletDamage, pevAttacker );
				// TANK_BULLET_NONE and unknown types fire nothing, as in the stock tank.
			}
			CFuncTank::Fire( barrelEnd, forward, pevAttacker );
		}
	}
	else
		CFuncTank::Fire( barrelEnd, forward, pevAttacker );
}
