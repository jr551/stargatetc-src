// sgtc_misc.h — declarations for entities defined in sgtc_misc.cpp that other
// translation units need.
//
// CEntSpawn — factory `entity_spawn_round` (ref CEntSpawn @0x195bea Spawn /
// @0x195ba6 Event in ref/stc_i386.so). CStargateTeamDM::NewRound() walks every
// "entity_spawn_round" entity and invokes Event() on each one so the map's
// round-scoped spawn points re-create their named entity
// (ref NewRound @0x184f42: UTIL_FindEntityByClassname(...,"entity_spawn_round")
// -> CEntSpawn::Event(pEnt)).

#ifndef SGTC_MISC_H
#define SGTC_MISC_H

class CEntSpawn : public CBaseEntity
{
public:
	void Spawn( void );
	void Event( void );

	// ref +0x19c — STRING( pev->message ) resolved to a char* and cached in
	// Spawn; Event() re-creates this classname at the marker's origin/angles.
	const char *m_pSpawnClass;
};

class CZat;
class CZatProj : public CBaseEntity
{
public:
	void Spawn( void );
	void Precache( void );
	void EXPORT ProjTouch( CBaseEntity *pOther );
	void EXPORT Think( void );
	static CZatProj *CreateZatProj( const Vector &vecOrigin, const Vector &vecAngles,
		CBaseEntity *pOwner, CZat *pWeapon );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	CBaseEntity *m_pZatOwner;	// ref +0x3f8
	float m_flLifeEnd;			// port life cap (SGTC_ZATPROJ_LIFETIME); ref Think reads +0x128 here, meaning unverified (#123)
	char m_bAllyShield;			// ref +0xd5
	char m_bBeamMode;			// ref +0x40c — oldzat long-beam mode (trace 8050u)
	int m_iTouchPass;			// ref +0xfc — bounce pass-through arm
	float m_flTouchPassTime;	// ref +0x404
	int m_iZatWaveSprite;		// ref +0x3ec — Precache sprites/zatwave.spr
	int m_iGammaWaveSprite;		// ref +0x408 — Precache sprites/gammawave.spr
	Vector m_vecBeamEnd;		// ref +0x410 — beam TE anchor (spawn origin)
};

#endif // SGTC_MISC_H
