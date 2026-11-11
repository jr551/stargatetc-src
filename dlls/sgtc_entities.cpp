// sgtc_entities.cpp — StargateTC map entities that belong to the rules: spawn
// points, info_flash, info_cap_rules (+ SGTC_Rules_ReadCapInfo), the trigger_*
// classes, item_objective and mode_relay. SGTC_InitTrigger is the shared
// brush-trigger setup.
//
// Split out of sgtc_rules.cpp (#94); code moved as it was. The env_* entities
// are in sgtc_entities_env.cpp.

#include "extdll.h"
#include "util.h"
#include "shake.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "gamerules.h"
#include "teamplay_gamerules.h"
#include "game.h"
#include "saverestore.h"
#include "sgtc_rules.h"
#include "items.h"
#include "voice_gamemgr.h"
#include "sgtc_util.h"
#include "sgtc_rules_priv.h"


// Brush-trigger setup shared by the SGTC trigger classes. Replicates
// CBaseTrigger::InitTrigger (triggers.cpp) — InitTrigger itself is TU-local
// to triggers.cpp and cannot be called from here.
void SGTC_InitTrigger( CBaseToggle *pTrigger )
{
	entvars_t *pev = pTrigger->pev;
	if( pev->angles != g_vecZero )
		SetMovedir( pev );
	pev->solid = SOLID_TRIGGER;
	pev->movetype = MOVETYPE_NONE;
	SET_MODEL( ENT( pev ), STRING( pev->model ) );
	if( CVAR_GET_FLOAT( "showtriggers" ) == 0 )
		SetBits( pev->effects, EF_NODRAW );
}

// ===========================================================================
// Spawn points + info_* point entities
// ===========================================================================
void CSGTCPlayerStart::Spawn( void )
{
	CPointEntity::Spawn();
}
LINK_ENTITY_TO_CLASS( info_player_torri, CSGTCPlayerStart );
LINK_ENTITY_TO_CLASS( info_player_marine, CSGTCPlayerStart );
LINK_ENTITY_TO_CLASS( info_player_goauld, CSGTCPlayerStart );
LINK_ENTITY_TO_CLASS( info_player_unas, CSGTCPlayerStart );

LINK_ENTITY_TO_CLASS( info_marker, CPointEntity );
LINK_ENTITY_TO_CLASS( info_radar_block, CPointEntity );
LINK_ENTITY_TO_CLASS( info_observer, CPointEntity );

void CFlash::Spawn( void )
{
	// ref CFlash::Spawn @0xfcbb6 — CItem::Spawn + UTIL_SetSize (rodata 400 / 0 / -400) + nextthink +0.1.
	Precache();
	m_pFlashThrower = NULL;
	CItem::Spawn();
	UTIL_SetSize( pev, Vector( -400, -400, -400 ), Vector( 400, 400, 400 ) );
	pev->nextthink = gpGlobals->time + 0.1f;
}

void CFlash::Precache( void )
{
	// ref @0xfcdb6 — empty body.
}

BOOL CFlash::MyTouch( CBasePlayer *pPlayer )
{
	// ref CFlash::MyTouch @0xfcdbc — trace (IGNORE_MONSTERS), white fade, stamina latch; no wav here.
	if( !pPlayer->IsAlive() )
		return FALSE;

	if( pPlayer->pev->flags & FL_FAKECLIENT )
		return FALSE;

	if( pev->team && pPlayer->pev->team == pev->team )
	{
		if( !m_pFlashThrower || m_pFlashThrower != pPlayer )
			return FALSE;
	}

	Vector vecFlash = pev->origin;
	Vector vecView = pPlayer->pev->origin + pPlayer->pev->view_ofs;
	TraceResult tr;
	edict_t *pentOwner = NULL;
	if( m_pFlashThrower )
		pentOwner = m_pFlashThrower->edict();
	else if( pev->owner )
		pentOwner = pev->owner;
	UTIL_TraceLine( vecFlash, vecView, ignore_monsters, pentOwner ? pentOwner : ENT( pev ), &tr );
	if( tr.flFraction < 1.0f )
	{
		CBaseEntity *pHit = CBaseEntity::Instance( tr.pHit );
		if( !pHit || pHit != pPlayer )
			return FALSE;
	}

	UTIL_ScreenFade( pPlayer, Vector( 255, 255, 255 ), 3.0f, 10.0f, 255, FFADE_OUT );
	pPlayer->SGTC_UpdateStamina();
	pev->nextthink = gpGlobals->time + 1.5f;
	return TRUE;
}

void CFlash::Think( void )
{
	// ref CFlash::Think @0xfd0a6 — UTIL_Remove only.
	UTIL_Remove( this );
}
LINK_ENTITY_TO_CLASS( info_flash, CFlash );

TYPEDESCRIPTION CCapInfo::m_SaveData[] =
{
	DEFINE_FIELD( CCapInfo, m_iWinType, FIELD_INTEGER ),
	DEFINE_FIELD( CCapInfo, m_iTeam, FIELD_INTEGER ),
};
IMPLEMENT_SAVERESTORE( CCapInfo, CPointEntity );

void CCapInfo::Spawn( void )
{
	// ref CCapInfo::Spawn @0x19576a: CBaseEntity::Think() (vtable call only,
	// no body) + pev->nextthink = gpGlobals->time + 0.1. KeyValue runs before
	// Spawn — do not clear m_iWinType / m_bWinTypeFromMap set from map "type".
	if( !m_bWinTypeFromMap )
	{
		m_iWinType = 0;
		m_iTeam = 0;
	}
	Think();
	pev->nextthink = gpGlobals->time + 0.1f;
}

void CCapInfo::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "type" ) )
	{
		m_iWinType = atoi( pkvd->szValue );
		m_bWinTypeFromMap = TRUE;
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "m_team" ) )
	{
		m_iTeam = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CPointEntity::KeyValue( pkvd );
}
LINK_ENTITY_TO_CLASS( info_cap_rules, CCapInfo );

// SGTC_Rules_ReadCapInfo — ref CP NewRound @0x187b34 info_cap_rules walk:
// teamed = CCapInfo +0x19c (m_team), type = CCapInfo +0x1a0 ("type").
void SGTC_Rules_ReadCapInfo( void )
{
	BOOL bFoundCapRules = FALSE;
	BOOL bAnyExplicitType = FALSE;
	CBaseEntity *pScan = NULL;
	while( ( pScan = UTIL_FindEntityByClassname( pScan, "info_cap_rules" ) ) != NULL )
	{
		CCapInfo *pInfo = (CCapInfo *)pScan;
		bFoundCapRules = TRUE;
		if( pInfo->m_bWinTypeFromMap )
			bAnyExplicitType = TRUE;
		sgtc_teamed = pInfo->m_iTeam;
		sgtc_type = pInfo->m_iWinType;
	}
	if( !bAnyExplicitType )
	{
		const char *pszMap = STRING( gpGlobals->mapname );
		if( !strncmp( pszMap, "ctp_amon", 8 ) || !strncmp( pszMap, "ctp_osiris", 10 ) )
			sgtc_type = 0; // ticket-dock maps (.rea/rndtim_arm_rea.md)
		else if( !strncmp( pszMap, "ctp_", 4 ) )
			sgtc_type = 2; // hold-timer CTP (crypt/frozen/lastn)
		(void)bFoundCapRules;
	}
}

// ===========================================================================
// Triggers (CBaseToggle-based; see header note)
// ===========================================================================
TYPEDESCRIPTION CTriggerTeleportAdvanced::m_SaveData[] =
{
	DEFINE_FIELD( CTriggerTeleportAdvanced, m_flTimeTel, FIELD_FLOAT ),
	DEFINE_FIELD( CTriggerTeleportAdvanced, m_flArmTime, FIELD_TIME ),
	DEFINE_FIELD( CTriggerTeleportAdvanced, m_hTeleportTarget, FIELD_EHANDLE ),
};
IMPLEMENT_SAVERESTORE( CTriggerTeleportAdvanced, CBaseToggle );

void CTriggerTeleportAdvanced::Spawn( void )
{
	SGTC_InitTrigger( this );
	SetTouch( &CTriggerTeleportAdvanced::AdvTeleportTouch );
	SetThink( &CTriggerTeleportAdvanced::TeleportThink );
	Reset();
}

void CTriggerTeleportAdvanced::KeyValue( KeyValueData *pkvd )
{
	// Verified key: "time_tel" (rodata 0x1ddc14), atof.
	if( FStrEq( pkvd->szKeyName, "time_tel" ) )
	{
		m_flTimeTel = (float)atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseToggle::KeyValue( pkvd );
}

void CTriggerTeleportAdvanced::Reset( void )
{
	m_hTeleportTarget = NULL;
	// .so Reset: nextthink = now + delay, ltime = now - delay.
	pev->nextthink = gpGlobals->time + m_flTimeTel;
	pev->ltime = gpGlobals->time - m_flTimeTel;
	m_flArmTime = 0;
}

void CTriggerTeleportAdvanced::AdvTeleportTouch( CBaseEntity *pOther )
{
	// .so AdvTeleportTouch: arms only if now - ltime >= delay; stores the
	// toucher and its trigger time for TeleportThink.
	if( gpGlobals->time - pev->ltime < m_flTimeTel )
		return;
	if( !pOther )
		return;
	m_hTeleportTarget = pOther;
	m_flArmTime = gpGlobals->time;
	pev->ltime = gpGlobals->time;
}

void CTriggerTeleportAdvanced::TeleportThink( void )
{
	// Fires the armed teleport: move the stored entity to our target
	// (info_teleport_destination / info_marker, FGD "special in" chain).
	pev->nextthink = gpGlobals->time + 0.1f;
	CBaseEntity *pOther = m_hTeleportTarget;
	if( !pOther )
		return;
	if( gpGlobals->time - m_flArmTime < m_flTimeTel )
		return;
	m_hTeleportTarget = NULL;
	edict_t *pentTarget = FIND_ENTITY_BY_TARGETNAME( NULL, STRING( pev->target ) );
	if( FNullEnt( pentTarget ) )
		return;
	Vector tmp = VARS( pentTarget )->origin;
	if( pOther->IsPlayer() )
		tmp.z -= pOther->pev->mins.z;
	tmp.z++;
	pOther->pev->flags &= ~FL_ONGROUND;
	UTIL_SetOrigin( pOther->pev, tmp );
	pOther->pev->angles = pentTarget->v.angles;
	if( pOther->IsPlayer() )
		pOther->pev->v_angle = pentTarget->v.angles;
	pOther->pev->fixangle = TRUE;
	pOther->pev->velocity = pOther->pev->basevelocity = g_vecZero;
}
LINK_ENTITY_TO_CLASS( trigger_adv_teleport, CTriggerTeleportAdvanced );

TYPEDESCRIPTION CTriggerGoal::m_SaveData[] =
{
	DEFINE_FIELD( CTriggerGoal, m_iTeam, FIELD_INTEGER ),
};
IMPLEMENT_SAVERESTORE( CTriggerGoal, CBaseToggle );

void CTriggerGoal::Spawn( void )
{
	// .so Spawn: solid trigger, touch = GoalTouch.
	SGTC_InitTrigger( this );
	SetTouch( &CTriggerGoal::GoalTouch );
}

void CTriggerGoal::KeyValue( KeyValueData *pkvd )
{
	// Verified key: "m_team" atoi (FGD 1=Tau'ri 2=Goa'uld).
	if( FStrEq( pkvd->szKeyName, "m_team" ) )
	{
		m_iTeam = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseToggle::KeyValue( pkvd );
}

void CTriggerGoal::GoalTouch( CBaseEntity *pOther )
{
	// .so GoalTouch: master check -> team-mismatch mapping (1<->2) ->
	// alive+player check -> same-team guard -> g_pGameRules->Goal(mapped).
	if( !UTIL_IsMasterTriggered( m_sMaster, pOther ) )
		return;
	if( g_fGameOver )
		return;
	if( sgtc_GameModeSet == 2 )
		return;
	// m_iTeam is the FGD "m_team" key, an SgtcTeam (1 = Tau'ri, 2 = Goa'uld);
	// Goal() takes an SgtcEntTeam, hence the single conversion (0 = no team).
	int mapped = SGTC_TeamToEntTeam( m_iTeam );
	if( !mapped || !pOther || !pOther->IsPlayer() || !pOther->IsAlive() )
		return;
	CBasePlayer *pl = (CBasePlayer *)pOther;
	if( !g_pGameRules || !g_pGameRules->IsTeamplay() )
		return;
	// pev->team (SgtcEntTeam) equals m_iTeam (SgtcTeam) exactly when the player
	// is on the OTHER team than the goal's m_team side (the numberings are
	// opposite), and those players do not score it.
	if( pl->pev->team == m_iTeam )
		return;
	CStargateTeamDM *sg = SGTC_GetTeamDMRules();
	if( sg )
		sg->Goal( mapped );	// ref GoalTouch @0x16e4aa pushes mapped 1/2, not mapped-1
}
LINK_ENTITY_TO_CLASS( trigger_goal, CTriggerGoal );

TYPEDESCRIPTION CTriggerRandom::m_SaveData[] =
{
	DEFINE_FIELD( CTriggerRandom, m_flMinPeriod, FIELD_FLOAT ),
	DEFINE_FIELD( CTriggerRandom, m_flMaxPeriod, FIELD_FLOAT ),
};
IMPLEMENT_SAVERESTORE( CTriggerRandom, CBaseToggle );

void CTriggerRandom::Spawn( void )
{
	SGTC_InitTrigger( this );
	SetTouch( &CTriggerRandom::RandomTouch );
	SetUse( &CTriggerRandom::RandomUse );
	SetThink( &CTriggerRandom::RandomThink );
	pev->nextthink = gpGlobals->time + RANDOM_FLOAT( m_flMinPeriod, m_flMaxPeriod );
}

void CTriggerRandom::KeyValue( KeyValueData *pkvd )
{
	// Verified keys: "minperiod"/"maxperiod" (rodata 0x1d9b44/0x1d9b4e).
	if( FStrEq( pkvd->szKeyName, "minperiod" ) )
	{
		m_flMinPeriod = (float)atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "maxperiod" ) )
	{
		m_flMaxPeriod = (float)atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseToggle::KeyValue( pkvd );
}

void CTriggerRandom::RandomTouch( CBaseEntity *pOther )
{
	// Own MultiTouch equivalent (stock MultiTouch is TU-local): clients /
	// allowed monsters / pushables, master-gated, then fire.
	entvars_t *pevToucher = pOther->pev;
	if( ( ( pevToucher->flags & FL_CLIENT ) && !( pev->spawnflags & SF_TRIGGER_NOCLIENTS ) ) ||
		( ( pevToucher->flags & FL_MONSTER ) && ( pev->spawnflags & SF_TRIGGER_ALLOWMONSTERS ) ) ||
		( ( pev->spawnflags & SF_TRIGGER_PUSHABLES ) && FClassnameIs( pevToucher, "func_pushable" ) ) )
	{
		if( !UTIL_IsMasterTriggered( m_sMaster, pOther ) )
			return;
		SUB_UseTargets( pOther, USE_TOGGLE, 0 );
	}
}

void CTriggerRandom::RandomUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// Own ToggleUse equivalent (stock ToggleUse is TU-local).
	if( pev->solid == SOLID_NOT )
	{
		pev->solid = SOLID_TRIGGER;
		gpGlobals->force_retouch++;
	}
	else
	{
		pev->solid = SOLID_NOT;
	}
	UTIL_SetOrigin( pev, pev->origin );
	(void)pActivator; (void)pCaller; (void)useType; (void)value;
}

void CTriggerRandom::RandomThink( void )
{
	// .so RandomThink: optional noise, SUB_UseTargets, reschedule by
	// RANDOM(min,max) added to now.
	if( !FStringNull( pev->noise ) )
		EMIT_SOUND( ENT( pev ), CHAN_VOICE, STRING( pev->noise ), 1, ATTN_NORM );
	SUB_UseTargets( this, USE_TOGGLE, 0 );
	float lo = m_flMinPeriod, hi = m_flMaxPeriod;
	if( hi < lo )
		hi = lo;
	pev->nextthink = gpGlobals->time + RANDOM_FLOAT( lo, hi );
	SetThink( &CTriggerRandom::RandomThink );
}
LINK_ENTITY_TO_CLASS( trigger_random, CTriggerRandom );
// trigger_secret: string-only in all 3 binaries (rodata 0x1dd920), no
// factory/class — maps use it as a one-shot random trigger. Alias it.
LINK_ENTITY_TO_CLASS( trigger_secret, CTriggerRandom );

// ===========================================================================
// item_objective (CItemSecurity behaviour + models/key.mdl)
// ===========================================================================
void CItemObjective::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/key.mdl" );
	pev->team = 2;	// ref CItemSecurity::Spawn @0xfd68d pev+0x1ac
	CItem::Spawn();
}

void CItemObjective::Precache( void )
{
	PRECACHE_MODEL( "models/key.mdl" );
	PRECACHE_SOUND( "items/gunpickup2.wav" );
}

BOOL CItemObjective::MyTouch( CBasePlayer *pPlayer )
{
	// ref CItemSecurity::MyTouch @0xfd7a4 — team gate, gunpickup snd, +0xaec only.
	if( pev->team && pPlayer->pev->team != pev->team )
		return FALSE;
	EMIT_SOUND( ENT( pPlayer->pev ), CHAN_ITEM, "items/gunpickup2.wav", 1, ATTN_NORM );
	pPlayer->m_iInBombZone++;
	return TRUE;
}
LINK_ENTITY_TO_CLASS( item_objective, CItemObjective );

// ===========================================================================
// mode_relay
// ===========================================================================
TYPEDESCRIPTION CModeRelay::m_SaveData[] =
{
	DEFINE_ARRAY( CModeRelay, m_iModes, FIELD_INTEGER, 6 ),
	DEFINE_FIELD( CModeRelay, m_iTriggerState, FIELD_INTEGER ),
};
IMPLEMENT_SAVERESTORE( CModeRelay, CPointEntity );

void CModeRelay::Spawn( void )
{
	pev->solid = SOLID_NOT;
}

void CModeRelay::KeyValue( KeyValueData *pkvd )
{
	// Verified keys: mode_a..mode_f + triggerstate (rodata 0x1dd3e0..).
	static const char *const keys[6] =
		{ "mode_a", "mode_b", "mode_c", "mode_d", "mode_e", "mode_f" };
	for( int i = 0; i < 6; i++ )
	{
		if( FStrEq( pkvd->szKeyName, keys[i] ) )
		{
			m_iModes[i] = atoi( pkvd->szValue );
			pkvd->fHandled = TRUE;
			return;
		}
	}
	if( FStrEq( pkvd->szKeyName, "triggerstate" ) )
	{
		m_iTriggerState = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CPointEntity::KeyValue( pkvd );
}

void CModeRelay::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// Fires targets when the active game mode index matches a set mode_* value;
	// with no mode_* keys set it behaves as a plain relay.
	const int iActiveMode = sgtc_GameModeSet;
	BOOL anyMode = FALSE;
	BOOL match = FALSE;
	for( int i = 0; i < 6; i++ )
	{
		if( !m_iModes[i] )
			continue;
		anyMode = TRUE;
		if( m_iModes[i] == iActiveMode )
			match = TRUE;
	}
	if( !anyMode || match )
		SUB_UseTargets( pActivator, USE_TOGGLE, 0 );
	(void)pCaller; (void)useType; (void)value;
}
LINK_ENTITY_TO_CLASS( mode_relay, CModeRelay );
