// sgtc_zones.cpp — SGTC brush zones, REA-verified against ref/stc_i386.so
// (Ghidra addrs): CTriggerShelter::Spawn 0x1a9920,
// CTriggerGTravel::Spawn 0x1aa51a / CBaseTrigger::GTravelTouch 0x1aa40c,
// CTriggerCapture::{Precache 0x1a9fe2, Spawn 0x1aa056, CaptureTouch 0x1a9efa,
// Think 0x1a9964}, CTriggerBomb::{Spawn 0x1aa36a, Think 0x1aa10c,
// BombZoneTouch 0x1aa25e}.
// pev field map (ref entvars): +0x104 nextthink, +0x160 health (state marker).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "gamerules.h"
#include "weapons.h"
#include "effects.h"
#include "sgtc_rules.h"

extern void SGTC_InitTrigger( CBaseToggle *pTrigger );

// ref BSS globals defined in sgtc_token.cpp / game.cpp.
extern int sgtc_GlobalBombState;	// ref 0x23710c
extern int sgtc_bomby;			// ref 0x2368c4
extern int sgtc_type;			// ref 0x236588 (game "type", 2 = CP)

// ---------------------------------------------------------------------------
// zone_shelter — "nukeproof zone" (FGD). ref Spawn: InitTrigger + m_pfnTouch/Use
// pointer setup only; entities inside survive the game_nuke pass.
// ---------------------------------------------------------------------------
class CTriggerShelter : public CBaseToggle
{
public:
	void Spawn( void );
	void EXPORT ShelterTouch( CBaseEntity *pOther );
};

void CTriggerShelter::Spawn( void )
{
	SGTC_InitTrigger( this );
	// ref +0x140 = 0 (no use callback). Touch marks the player for shelter.
	SetTouch( &CTriggerShelter::ShelterTouch );
}

void CTriggerShelter::ShelterTouch( CBaseEntity *pOther )
{
	if( !pOther || !pOther->IsPlayer() )
		return;
	((CBasePlayer *)pOther)->m_flNukeGrace = gpGlobals->time + 0.25f;
}

LINK_ENTITY_TO_CLASS( zone_shelter, CTriggerShelter );

// ---------------------------------------------------------------------------
// zone_gatetravel — ref CBaseTrigger::GTravelTouch: virtual gate (vtable+0xa8,
// IsPlayer-equivalent) then sets player+0x408 travel deadline = now+0.25,
// clears pev+0x50/+0x74 vectors (velocity/avelocity), sets pev+0xa0 flag=1
// (deadflag-side marker) and player+0x458 = (int)zone pev+0x1f8 (team float).
// ---------------------------------------------------------------------------
class CTriggerGTravel : public CBaseToggle
{
public:
	void Spawn( void );
	void EXPORT GateTravelTouch( CBaseEntity *pOther );
};

void CTriggerGTravel::Spawn( void )
{
	SGTC_InitTrigger( this );
	SetTouch( &CTriggerGTravel::GateTravelTouch );
}

void CTriggerGTravel::GateTravelTouch( CBaseEntity *pOther )
{
	// ref calls vtable+0xa8 — behaves like IsPlayer here (players only travel).
	if( !pOther || !pOther->IsPlayer() )
		return;

	CBasePlayer *pPlayer = (CBasePlayer *)pOther;
	pPlayer->m_flGateTravel = gpGlobals->time + 0.25f;	// ref +0x408
	pPlayer->pev->angles = g_vecZero;			// ref pev+0x50
	pPlayer->pev->v_angle = g_vecZero;			// ref pev+0x74
	pPlayer->pev->fixangle = 1;				// ref pev+0xa0
	pPlayer->m_iGateTeam = (int)pev->speed;			// ref +0x458 = ROUND(pev+0x1f8)
}

LINK_ENTITY_TO_CLASS( zone_gatetravel, CTriggerGTravel );

// ---------------------------------------------------------------------------
// zone_capture — flag capture zone (ref CTriggerCapture).
// 0x240 current flag state (0 draw, 1 tau, 2 goa, 3 dispute)
// 0x244 pending flag state this think, 0x248 count team2, 0x24c count team1,
// 0x250 last pending state, 0x254 zone id (RANDOM_LONG 500..99999999),
// 0x258 flag-state switch deadline, 0x25c sprite-created flag.
// Player side: m_iZoneId (+0x4cc), m_flZoneTime (+0x498 = now+0.3),
// pev->team (+0x1ac) 1/2.
// ---------------------------------------------------------------------------
class CTriggerCapture : public CBaseToggle
{
public:
	void Spawn( void );
	void Precache( void );
	void EXPORT CaptureTouch( CBaseEntity *pOther );
	void EXPORT CaptureThink( void );

	int m_iFlagState;	// +0x240
	int m_iPending;		// +0x244
	int m_iCount2;		// +0x248 (pev->team==2)
	int m_iCount1;		// +0x24c (pev->team==1)
	int m_iLastPending;	// +0x250
	int m_iZoneId;		// +0x254
	float m_flSwitch;	// +0x258
	EHANDLE m_hSprite;	// flag sprite (ref tracks via "env_sprite" walk)
	BOOL m_bSpriteMade;	// +0x25c
};

void CTriggerCapture::Precache( void )
{
	// ref @0x1a9fe2.
	PRECACHE_MODEL( "sprites/flag_draw.spr" );
	PRECACHE_MODEL( "sprites/flag_tau.spr" );
	PRECACHE_MODEL( "sprites/flag_goa.spr" );
	PRECACHE_MODEL( "sprites/flag_dispute.spr" );
}

void CTriggerCapture::Spawn( void )
{
	// ref @0x1aa056: InitTrigger + touch/think hooks + id.
	Precache();
	SGTC_InitTrigger( this );
	SetTouch( &CTriggerCapture::CaptureTouch );
	SetThink( &CTriggerCapture::CaptureThink );
	pev->nextthink = gpGlobals->time + 0.1f;
	m_iFlagState = 0;
	m_iPending = 0;
	m_iCount2 = 0;
	m_iCount1 = 0;
	m_iLastPending = 0;
	m_iZoneId = 0;
	m_flSwitch = 0;
	m_bSpriteMade = FALSE;
	pev->health = 100.0f;		// +0x160 = 0x42c80000
}

void CTriggerCapture::CaptureTouch( CBaseEntity *pOther )
{
	// ref @0x1a9efa: find matching "player" entity; bind zone id + window;
	// alive players get capture fields cleared.
	if( !pOther )
		return;
	CBaseEntity *pEnt = NULL;
	while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "player" ) ) != NULL )
	{
		if( pEnt != pOther )
			continue;
		CBasePlayer *pl = (CBasePlayer *)pEnt;
		pl->m_iZoneId = m_iZoneId;
		pl->m_flZoneTime = gpGlobals->time + 0.3f;
		if( pl->IsPlayer() )	// ref vtable+0xa8
		{
			pl->m_bZoneFlag = 0;
			pl->m_flSpawnCampTouch = 0.0f;
			pl->m_flCampWarnDeadline = 0;	// ref +0x40c
			pl->m_iCapFlags2 = 0;
		}
	}
}

void CTriggerCapture::CaptureThink( void )
{
	m_iLastPending = m_iPending;
	m_iPending = 0;
	m_iCount2 = 0;
	m_iCount1 = 0;

	if( m_iZoneId == 0 )
		m_iZoneId = RANDOM_LONG( 500, 99999999 );

	// One-time flag sprite creation at the named target's origin.
	if( m_bSpriteMade != TRUE )
	{
		Vector vecPos( 0, 0, 0 );
		CBaseEntity *pTarget = NULL;
		while( ( pTarget = UTIL_FindEntityByTargetname( pTarget,
				STRING( pev->target ) ) ) != NULL )
		{
			m_bSpriteMade = TRUE;
			vecPos = pTarget->pev->origin;
		}
		if( m_bSpriteMade )
		{
			CSprite *pSprite = CSprite::SpriteCreate(
				"sprites/flag_draw.spr", vecPos, FALSE );
			pSprite->SetScale( 0.5 );
			pSprite->SetTransparency( kRenderTransAlpha, 255, 255, 255,
				255, 0xe );
			// zone id is carried by m_hSprite; do not disturb sprite spawnflags
			m_hSprite = pSprite;
		}
	}

	// health <= 69 -> reset counters (ref 0x42c80000 write to +0x160).
	if( pev->health <= 69.0f )
	{
		m_iFlagState = 0;
		m_flSwitch = 0;
		pev->health = 100.0f;
	}

	// Count players bound to this zone within their touch window.
	CBaseEntity *pEnt = NULL;
	while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "player" ) ) != NULL )
	{
		CBasePlayer *pl = (CBasePlayer *)pEnt;
		if( pl->m_iZoneId == m_iZoneId &&
		    gpGlobals->time < pl->m_flZoneTime )
		{
			if( pl->pev->team == SGTC_ENTTEAM_GOAULD )
				m_iCount1++;
			else if( pl->pev->team == SGTC_ENTTEAM_TORRI )
				m_iCount2++;
		}
	}

	if( m_iCount2 < m_iCount1 )
		m_iPending = 1;
	else if( m_iCount1 < m_iCount2 )
		m_iPending = 2;
	else if( m_iCount1 == m_iCount2 )
		m_iPending = 3;
	if( m_iCount2 == 0 && m_iCount1 == 0 )
		m_iPending = 0;

	if( m_iLastPending != m_iPending )
		m_flSwitch = gpGlobals->time + 3.0f;

	if( m_iLastPending > 0 && m_iPending == 0 )
	{
		if( sgtc_type == 2 )
			m_flSwitch = gpGlobals->time + 10.0f;
		else
			m_flSwitch = gpGlobals->time + CVAR_GET_FLOAT( "resetflagtime" );
	}

	if( m_flSwitch < gpGlobals->time )
	{
		m_iFlagState = m_iPending;
		// Swap flag sprite on the bound sprite entity (ref walks
		// "env_sprite" matching zone id; our sprite EHANDLE is equivalent).
		CBaseEntity *pSprite = m_hSprite;
		if( pSprite )
		{
			const char *psz = NULL;
			switch( m_iFlagState )
			{
			case 0: psz = "sprites/flag_draw.spr"; break;
			case 1: psz = "sprites/flag_goa.spr"; break;	// ref state 1 = Goa'uld
			case 2: psz = "sprites/flag_tau.spr"; break;	// ref state 2 = Tau'ri
			case 3: psz = "sprites/flag_dispute.spr"; break;
			}
			if( psz )
			{
				pSprite->pev->model = MAKE_STRING( psz ); // ref writes pev+0xb8
				// ref calls sprite vtable+0 (ObjectCaps?) — Spawn-side
				// model refresh; re-issue SpriteInit equivalent.
				pSprite->pev->modelindex = MODEL_INDEX( psz );
			}
		}
	}

	pev->nextthink = gpGlobals->time + 0.3f;
}

LINK_ENTITY_TO_CLASS( zone_capture, CTriggerCapture );

// ---------------------------------------------------------------------------
// zone_bomb_target — ref CTriggerBomb. Members: +0x241 satchel-present flag,
// +0x242 targets-fired flag, +0x248 satchel-out deadline (now+10), +0x24c
// satchel-in window (now+0.2). Satchel pev+0x160 (health) drives state.
// ---------------------------------------------------------------------------
class CTriggerBomb : public CBaseToggle
{
public:
	void Spawn( void );
	void EXPORT BombZoneTouch( CBaseEntity *pOther );
	void EXPORT BombThink( void );
	void ResetForNewRound( void );

	BOOL m_fSatchelIn;	// +0x241
	BOOL m_fFired;		// +0x242
	float m_flOutTime;	// +0x248
	float m_flInWindow;	// +0x24c
};

void CTriggerBomb::Spawn( void )
{
	// ref @0x1aa36a: InitTrigger + touch+think hooks; +0x134 = 0xc5 marker.
	SGTC_InitTrigger( this );
	SetTouch( &CTriggerBomb::BombZoneTouch );
	SetThink( &CTriggerBomb::BombThink );
	pev->takedamage = DAMAGE_NO;	// ref Spawn @0x19a36a pev+0x16c
	pev->nextthink = gpGlobals->time + 0.1f;
	m_fSatchelIn = FALSE;
	m_fFired = FALSE;
	m_flOutTime = 0;
	m_flInWindow = 0;
	pev->health = 100.0f;
}

void CTriggerBomb::BombZoneTouch( CBaseEntity *pOther )
{
	// ref @0x1aa25e: scan monster_satchel ents; on match, arm window + health.
	if( !pOther )
		return;
	CBaseEntity *pSatchel = NULL;
	while( ( pSatchel = UTIL_FindEntityByClassname( pSatchel,
			"monster_satchel" ) ) != NULL )
	{
		if( pOther != pSatchel )
			continue;
		if( m_fSatchelIn != TRUE )
		{
			m_fSatchelIn = TRUE;
			pSatchel->pev->health = 99.0f;	// ref +0x160 = 0x42c60000
			m_flOutTime = gpGlobals->time + 10.0f;
		}
		if( pSatchel->pev->health == 99.0f || pSatchel->pev->health <= 22.0f )
			m_flInWindow = gpGlobals->time + 0.2f;
	}
}

void CTriggerBomb::BombThink( void )
{
	// ref @0x1aa10c.
	if( pev->health <= 69.0f )
	{
		m_fSatchelIn = FALSE;
		m_fFired = FALSE;
		pev->health = 100.0f;
	}
	if( m_fFired != TRUE && m_fSatchelIn != FALSE &&
	    m_flInWindow < gpGlobals->time )
	{
		if( m_flOutTime <= 0.0f || gpGlobals->time <= m_flOutTime )
		{
			SUB_UseTargets( this, USE_TOGGLE, 0 );
			m_fFired = TRUE;
		}
		else
		{
			m_fSatchelIn = FALSE;
			m_flOutTime = 0;
		}
	}
	pev->nextthink = gpGlobals->time + 0.1f;
}

void CTriggerBomb::ResetForNewRound( void )
{
	// ref CStargateTeamDM::NewRound @0x194f42 zone_bomb_target walk.
	pev->health = 69.0f;	// 0x428a0000 — triggers BombThink reset block
	BombThink();
}

LINK_ENTITY_TO_CLASS( zone_bomb_target, CTriggerBomb );

int SGTC_Zones_ObjectiveState( CBaseEntity *pEnt )
{
	if( !pEnt || !pEnt->pev )
		return -1;
	const char *pszClass = STRING( pEnt->pev->classname );
	if( pszClass && !strcmp( pszClass, "zone_capture" ) )
	{
		CTriggerCapture *pZone = (CTriggerCapture *)pEnt;
		return pZone->m_iFlagState;	// 0 draw, 1 tau, 2 goa, 3 dispute
	}
	if( pszClass && !strcmp( pszClass, "zone_bomb_target" ) )
	{
		CTriggerBomb *pBomb = (CTriggerBomb *)pEnt;
		if( pBomb->m_fFired )
			return 2;	// target fired
		return pBomb->m_fSatchelIn ? 1 : 0;	// satchel in zone
	}
	return -1;
}

void SGTC_Zones_ResetBombTargets( void )
{
	// Advance from the current entity; restarting from NULL would find the
	// same zone forever (ResetForNewRound leaves it in place).
	CBaseEntity *pScan = UTIL_FindEntityByClassname( NULL, "zone_bomb_target" );
	while( pScan )
	{
		CBaseEntity *pNext = UTIL_FindEntityByClassname( pScan, "zone_bomb_target" );
		((CTriggerBomb *)pScan)->ResetForNewRound();
		pScan = pNext;
	}
}

// SGTC_Zones_ResetCaptureZones — ref NewRound @0x187b34 zone_capture walk
// (double pass, net effect): +0x244/+0x240/+0x250 = 0,
// pev->health = 0x428a0000 = 69.0f (matches the CaptureThink reset
// threshold), +0x258 = now-1.0f. +0x248/+0x24c/+0x254 untouched.
void SGTC_Zones_ResetCaptureZones( void )
{
	CBaseEntity *pScan = NULL;
	while( ( pScan = UTIL_FindEntityByClassname( pScan, "zone_capture" ) ) != NULL )
	{
		CTriggerCapture *pZone = (CTriggerCapture *)pScan;
		pZone->m_iFlagState = 0;
		pZone->m_iPending = 0;
		pZone->m_iLastPending = 0;
		pZone->pev->health = 69.0f; // ref 0x428a0000
		pZone->m_flSwitch = gpGlobals->time - 1.0f;
	}
}
