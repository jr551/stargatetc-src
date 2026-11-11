// sgtc_lanceproj.h — StargateTC energy-bolt projectiles (NEW file).
//
// Clean-room port of ref/stc_i386.so (STC 2.0):
//   CLanceProj    : class "lance_proj"      — staff-blast bolt (big_lance.cpp).
//   CBigLanceProj : class "big_lance_proj"  — deathglider cannon bolt.
//   CReetouProj   : class "reetou_proj"     — reetou energy bolt.
//
// The ref's CCorpseController ("corpse_control") is not ported: nothing in
// this tree ever bound it to a corpse, so it only removed itself (#76).
//
// All three bolts share one skeleton (CSGTCProjBase): CGrenade-derived,
// MOVETYPE_BOUNCE / SOLID_BBOX, zero-size, EffectThink accelerates the bolt
// to 2000 and sweeps a lead trace, kills nearby "monster_shieldemit", and
// explodes with DMG_ALWAYSGIB. The real mod adds CGrenade::LanceTouch —
// the stock base has no such member, so each bolt declares its own
// LanceTouch mirroring ref CGrenade::LanceTouch @0xf4662.
//
// Disasm references (ref/stc_i386.so):
//   CLanceProj::{Spawn 0xfe1c0, Accelere 0xfe3f8, Exploder 0xfe508,
//     ProjTouch 0xfe9d0, EffectThink 0xfeb9c, Precache 0xfefb4,
//     CreateTourrelleProj 0x1000ce, CreateLanceProj 0x1001b8}
//   CGrenade::LanceTouch 0xf4662; DoSpark 0xbf8e6;
//   CBigLanceProj::{Spawn 0xb8804, Accelere 0xb8a36, Exploder 0xb8bfa,
//     ProjTouch 0xb91ae, EffectThink 0xb935e, Precache 0xb976a,
//     CreateTourrelleProj 0xb9d46, CreateBigLanceProj 0xb9e30}
//   CReetouProj::{Spawn 0x15170c, Explodera 0x151984, ProjTouch 0x151e70,
//     EffectThink 0x151fd2, Precache 0x152272, CreateReetouProj 0x152e32}

#ifndef SGTC_LANCEPROJ_H
#define SGTC_LANCEPROJ_H

class CLanceJaffa;
class CBigLanceJaffa;
class CReetou;

// ---------------------------------------------------------------------------
// CSGTCProjBase — shared bolt skeleton for lance_proj / big_lance_proj /
// reetou_proj. Members decode the ref fields used by the shared bodies:
//   m_flDie           : this+0x128 — bolt expiry time (BaseSpawn: now + 5s)
//   m_fExplosionProtection : this+0x130 — suppresses RadiusDamage
//   m_fAttendExplosion     : this+0xd5 — armed/exploding flag
//   m_pOwner         : this+0x3f0 — firing entity (touch filter)
//   m_vecSavedVel / m_flSavedSpeed : this+0x418/0x424 (lance layout)
// ---------------------------------------------------------------------------
class CSGTCProjBase : public CGrenade
{
public:
	void BaseSpawn( const char *pszModel, const char *pszClass, float flSpeed );

	// Accelere/Exploder are called from the shared bodies, so subclass
	// overrides (CBigLanceProj::Accelere, CReetouProj::Exploder) need virtual
	// dispatch. EffectThink is bound as a think pointer: a subclass that has
	// its own EffectThink must SetThink() it in its Spawn (CReetouProj does).
	virtual void Accelere( void );
	virtual void Exploder( TraceResult *pTrace, int bitsDamageType );
	void EXPORT ProjTouch( CBaseEntity *pOther );
	void EXPORT LanceTouch( CBaseEntity *pOther );
	void EXPORT EffectThink( void );

	// per-class knobs
	virtual const char * const *DebrisSounds( void ) = 0;
	virtual float DebrisVol( void ) { return 0.9f; }	// ref CLanceProj Exploder; big/reetou use 1.0

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	float m_flDie;
	BOOL m_fAttendExplosion;
	BOOL m_fExplosionProtection;
	CBaseEntity *m_pWeapon;
	CBaseEntity *m_pOwner;
	Vector m_vecDir;        // impact aim point (factories)
	CBaseEntity *m_pTarget; // homing target (big lance only)
	Vector m_vecSavedVel;
	float m_flSavedSpeed;
};

class CLanceProj : public CSGTCProjBase
{
public:
	void Spawn( void );
	void Precache( void );
	virtual const char * const *DebrisSounds( void );

	static CLanceProj *CreateTourrelleProj( Vector vecOrigin, Vector vecAngles );
	static CLanceProj *CreateLanceProj( Vector vecStart, Vector vecVel,
		Vector vecTarget, CBaseEntity *pOwner, CLanceJaffa *pWeapon );
};

class CBigLanceProj : public CSGTCProjBase
{
public:
	void Spawn( void );
	void Precache( void );
	virtual const char * const *DebrisSounds( void );
	virtual float DebrisVol( void ) { return 1.0f; }
	virtual void Accelere( void );	// shared boost + homing tail

	static CBigLanceProj *CreateTourrelleProj( Vector vecOrigin, Vector vecAngles );
	static CBigLanceProj *CreateBigLanceProj( Vector vecStart, Vector vecVel,
		Vector vecTarget, CBaseEntity *pOwner, CBigLanceJaffa *pWeapon );
};

class CReetouProj : public CSGTCProjBase
{
public:
	void Spawn( void );
	void Precache( void );
	virtual const char * const *DebrisSounds( void );
	virtual float DebrisVol( void ) { return 1.0f; }
	virtual void Exploder( TraceResult *pTrace, int bitsDamageType );	// ref Explodera @0x151984
	void EXPORT EffectThink( void );	// narco-sprite trail only

	static CReetouProj *CreateReetouProj( Vector vecStart, Vector vecAimPoint,
		CBaseEntity *pOwner, CReetou *pWeapon );

	virtual int Save( CSave &save );
	virtual int Restore( CRestore &restore );
	static TYPEDESCRIPTION m_SaveData[];

	float m_flNarcoNext;	// this+0x3f4 — TE_SPRITE trail timer
};

// Sprite index used by the reetou trail + explosion TE (ref: g_sModelIndexNarco
// BSS @0x23827e — file-global in the real mod).
extern int g_sModelIndexNarco;

// ref CLanceJaffa/CBigLanceJaffa primary @0x110358 / @0xc9fea — TE_BEAMENTS
// between owner and spawned bolt (iNoiseByte: 5 lance, 6 glstaff).
void SGTC_LaserBeamEnts( CBaseEntity *pStart, CBaseEntity *pEnd, int iSprite,
	int iNoiseByte );

#endif // SGTC_LANCEPROJ_H
