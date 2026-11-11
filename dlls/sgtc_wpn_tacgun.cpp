// sgtc_wpn_tacgun.cpp — StargateTC CTacgun (split from sgtc_weapons.cpp; NEW file).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
#include "sgtc_weapons.h"
#include "sgtc_monsters.h"
#include "sgtc_util.h"

// ===========================================================================
// CTacgun - deployable anti-invis sentry (events/tac.sc).
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_tacgun, CTacgun );

// Save/restore (#99). Not saved: m_flSgtcPad200 (only ever cleared) and the file-local event
// handle s_usTacFire.
TYPEDESCRIPTION CTacgun::m_SaveData[] =
{
	DEFINE_FIELD( CTacgun, m_iTacHudSel, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE( CTacgun, CBasePlayerWeapon )

// File-local event handle (the class declares no m_usFire member).
static unsigned short s_usTacFire;

// ref svcrazy @0x213920 / tacdist @0x21345c — registered in sgtc_round.cpp.
extern cvar_t sgtc_svcrazy;	// 0 = density scan + ammo enforced; !=0 free tacs
extern cvar_t sgtc_tacdist;	// tac separation, Endo_intconstrain(50,8000)

// ref Endo_intconstrain( 50, 8000, (int)tacdist ) — per-tac separation limit.
static int SGTC_TacSeparation( void )
{
	int i = (int)sgtc_tacdist.value;
	if( i < 50 ) i = 50;
	if( i > 8000 ) i = 8000;
	return i;
}

// ref PrimaryAttack density scan @0x18e957: returns stacked count. The ref scans
// after Create, so its counts include the new tac (dist 0 → stacked starts at 1).
// The port scans BEFORE Create (so a cancelled placement never creates or
// removes an entity), hence the tac about to be placed is counted in here to
// keep the thresholds identical.
static int SGTC_TacDeployCounts( const Vector &vecDeploy, int &iTotal, int &iNear )
{
	iTotal = 0;
	iNear = 0;
	int iStacked = 0;
	const int iSep = SGTC_TacSeparation();
	CBaseEntity *pEnt = NULL;

	while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "monster_tac" ) ) != NULL )
	{
		iTotal++;
		const int iDist = SGTC_Distance3D( vecDeploy, pEnt->pev->origin );
		if( iDist < iSep )
			iNear++;
		if( iDist <= 9 )
			iStacked++;
	}

	// the candidate tac itself: dist 0 from vecDeploy
	iTotal++;
	iNear++;
	iStacked++;
	return iStacked;
}

// ref vfunc+0x94 on hit entity @0x18ebde (CSGTCTac::IsTac returns TRUE).
static BOOL SGTC_EntityIsTac( CBaseEntity *pEnt )
{
	return pEnt && FClassnameIs( pEnt->pev, "monster_tac" );	// ref IsTac @0x18ebde
}

void CTacgun::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_tacb.mdl" );
	m_iId = SGTC_WPN_TAC;
	m_iClip = -1;
	m_iDefaultAmmo = 3;
	m_iTacHudSel = 0;
	FallInit();
}

void CTacgun::Precache( void )
{
	PRECACHE_MODEL( "models/v_tac.mdl" );
	PRECACHE_MODEL( "models/p_tac.mdl" );
	PRECACHE_MODEL( "models/w_tacb.mdl" );
	PRECACHE_SOUND( "weapons/tac.wav" );
	PRECACHE_SOUND( "weapons/tacs.wav" );
	PRECACHE_SOUND( "weapons/tacp.wav" );
	PRECACHE_SOUND( "weapons/tacact.wav" );
	PRECACHE_SOUND( "weapons/place.wav" );
	PRECACHE_SOUND( "common/null.wav" );

	UTIL_PrecacheOther( "monster_tac" );

	s_usTacFire = PRECACHE_EVENT( 1, "events/tac.sc" );
}

SGTC_GETITEM( CTacgun, SGTC_AMMO_TAC, SGTC_TAC_MAX_CARRY, WEAPON_NOCLIP, 4, 2, SGTC_WPN_TAC, 24, -10 )
SGTC_ADD_TO_PLAYER( CTacgun )

BOOL CTacgun::Deploy( void )
{
	return SGTC_DEPLOY( 2, "models/v_tac.mdl", "models/p_tac.mdl", SGTC_DRAW, "trip" );
}

void CTacgun::Holster( int skiplocal )
{
	// PrimaryAttack/SecondaryAttack arm WeaponIdle as a think. Clear it here so
	// it cannot fire on whatever weapon is deployed next. Must come before
	// DestroyItem below, which arms SUB_Remove.
	SetThink( NULL );
	pev->nextthink = 0;

	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5;

	if( !m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] )
	{
		m_pPlayer->pev->weapons &= ~( 1 << SGTC_WPN_TAC );
		DestroyItem();
	}

	SendWeaponAnim( SGTC_HOLSTER );
	EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "common/null.wav", 1.0, ATTN_NORM );
}

void CTacgun::PrimaryAttack( void )
{
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
		return;
	if( gpGlobals->time < m_pPlayer->m_flGateTravel )
		return;
	// Short placement lock after retrieving a tac so pickup/replace cannot be
	// micro-cycled.
	if( gpGlobals->time < m_pPlayer->m_flSgtcTacPlaceBlock )
		return;
	if( m_flNextPrimaryAttack > UTIL_WeaponTimeBase() )
		return;

	UTIL_MakeVectors( m_pPlayer->pev->v_angle );	// ref PA @0x18e780: pev+0x74 — no punchangle (objdump)

	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecAiming = gpGlobals->v_forward;

	TraceResult tr;
	UTIL_TraceLine( vecSrc, vecSrc + vecAiming * 128, dont_ignore_monsters, ENT( m_pPlayer->pev ), &tr );

	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), s_usTacFire,
		0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0.0, 0.0, 0, 0, 0, 0 );

#ifndef CLIENT_DLL
	if( tr.flFraction < 1.0 )
	{
		CBaseEntity *pEntity = CBaseEntity::Instance( tr.pHit );
		// ref @0x18e8a0: no deploy when hit entity has pev->flags bit 2 set.
		if( pEntity && !( pEntity->pev->flags & 4 ) )
		{
			Vector angles = UTIL_VecToAngles( tr.vecPlaneNormal );
			Vector vecDeploy = tr.vecEndPos + tr.vecPlaneNormal * 8;
			TraceResult trFloor;
			UTIL_TraceLine( vecDeploy + Vector( 0, 0, 8 ), vecDeploy + Vector( 0, 0, -256 ),
				ignore_monsters, m_pPlayer->edict(), &trFloor );
			if( trFloor.fStartSolid || trFloor.fAllSolid )
			{
				m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.5f;
				return;
			}
			vecDeploy.z = trFloor.vecEndPos.z + 12.0f;

			BOOL bCancel = FALSE;
			int iTotal = 0, iNear = 0, iStacked = 0;

			if( sgtc_svcrazy.value == 0.0f )
				iStacked = SGTC_TacDeployCounts( vecDeploy, iTotal, iNear );

			const int iFloorDelta = (int)lrintf( vecDeploy.z - trFloor.vecEndPos.z );

			if( sgtc_svcrazy.value == 0.0f &&
				( iNear > 2 || iStacked > 1 || iTotal > 10 ) )
				bCancel = TRUE;
			else if( iFloorDelta != 12 )
				bCancel = TRUE;
			else if( SGTC_EntityIsTac( pEntity ) )
				bCancel = TRUE;

			// ref prints only via the iNear>2 arm.
			if( iNear > 2 )
				ClientPrint( m_pPlayer->pev, HUD_PRINTCENTER, "#overflow_tacs" );

			if( bCancel )
			{
				// Throttle the retry: returning with no cooldown let a held fire
				// button re-run the traces, the scan and the event every frame.
				m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.5f;
				return;
			}

			CBaseEntity *pTac = CBaseEntity::Create( "monster_tac", vecDeploy, angles,
				m_pPlayer->edict() );
			if( !pTac )
			{
				m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.5f;
				return;
			}

			// ref @0x18ed06: Goa'uld / Torri m_szTeamName (+0xe08) → pev+0x1ac (team latch).
			if( !strncmp( m_pPlayer->m_szTeamName, "Goa'uld", 7 ) )
				pTac->pev->team = SGTC_ENTTEAM_GOAULD;
			else if( !strncmp( m_pPlayer->m_szTeamName, "Torri", 5 ) )
				pTac->pev->team = SGTC_ENTTEAM_TORRI;
			else
				pTac->pev->team = m_pPlayer->pev->team;

			// Health per tac type (0/1/2 = red/yellow/green). The reference
			// build used mode+30; these values give a tac roughly 6-10 rifle
			// hits before it explodes while the tougher types last longer.
			static const float kSgtcTacHealth[3] = { 60.0f, 75.0f, 90.0f };
			int iTacMode = 0;
			if( m_pPlayer->m_iHudInfoMode >= 1 && m_pPlayer->m_iHudInfoMode <= 3 )
				iTacMode = m_pPlayer->m_iHudInfoMode - 1;

			float flTacHealth = kSgtcTacHealth[iTacMode];
			// A retrieved tac keeps the damage it took.
			if( m_pPlayer->m_iSgtcTacCarryHealth[iTacMode] >= 0 )
			{
				flTacHealth = (float)m_pPlayer->m_iSgtcTacCarryHealth[iTacMode];
				m_pPlayer->m_iSgtcTacCarryHealth[iTacMode] = -1;
			}
			if( flTacHealth > kSgtcTacHealth[iTacMode] )
				flTacHealth = kSgtcTacHealth[iTacMode];
			if( flTacHealth < 1.0f )
				flTacHealth = 1.0f;

			pTac->pev->health = flTacHealth;
			pTac->pev->max_health = kSgtcTacHealth[iTacMode];
			// ref @0x18ec91: tac pev->angles.y = player v_angle.y + 90.
			pTac->pev->angles.y = m_pPlayer->pev->v_angle.y + 90.0f;
			// ref @0x18ec9f: tac pev->nextthink = gpGlobals->time + 2.7.
			pTac->pev->nextthink = gpGlobals->time + 2.7f;
			// ref arm switch selects the mode AND model from pev->sequence
			// (30/31/32). The port never set it, so every tac armed as mode 0;
			// set it here so modes, tint and explosion match the chosen type.
			pTac->pev->sequence = 30 + iTacMode;
			((CSGTCTac *)pTac)->m_iTacMode = iTacMode;
			((CSGTCTac *)pTac)->m_bFreeDeploy = ( sgtc_svcrazy.value != 0.0f ) ? TRUE : FALSE;
			// ref primary tail: variant model from HudInfoMode / tac mode (w_tacb/s/p).
			const char *pszModel = "models/w_tacb.mdl";
			switch( m_pPlayer->m_iHudInfoMode )
			{
			case 2: pszModel = "models/w_tacs.mdl"; break;
			case 3: pszModel = "models/w_tacp.mdl"; break;
			default: pszModel = "models/w_tacb.mdl"; break;
			}
			SET_MODEL( ENT( pTac->pev ), pszModel );

			// Owner-only range bubble (sgtc_tac_bubble).
			((CSGTCTac *)pTac)->ShowRangeBubble( m_pPlayer, TRUE );

			// ref @0x18ed8d: ammo decrement only when svcrazy == 0.
			if( sgtc_svcrazy.value == 0.0f )
				m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]--;
			m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
			EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_VOICE, "weapons/place.wav", 1.0, ATTN_NORM ); // ref @0x18c07d chan 2
			// ref @0x18ee00: vcall SendWeaponAnim(this, 2, 1, 0) after deploy EMIT.
			// v_tac has 2 seqs; ref sends literal 2 (engine clamps). Keep 2.
			SendWeaponAnim( 2 );

			if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
			{
				RetireWeapon();
				return;
			}
		}
	}
#endif
	m_flNextPrimaryAttack = GetNextAttackDelay( 0.5 );
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
		UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );

	// ref PA tail: m_pfnThink = WeaponIdle (encoded 0x175), m_pfnTouch = NULL,
	// pev->nextthink = gpGlobals->time + 0.4 (not WeaponTimeBase).
	SetThink( &CTacgun::WeaponIdle );
	SetTouch( NULL );
	pev->nextthink = gpGlobals->time + 0.4f;
	m_flSgtcPad200 = 0.0f;	// ref PA tail @0x18ef14 (rodata 0.0f)
}

// ref @0x18ef22 — cycle tac HUD mode (player+0x434 HudInfoMode 1..3).
void CTacgun::SecondaryAttack( void )
{
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
		return;
	if( m_flNextSecondaryAttack > UTIL_WeaponTimeBase() )
		return;
	if( gpGlobals->time < m_pPlayer->m_flGateTravel )
		return;

	m_iTacHudSel++;
	if( m_iTacHudSel > 2 )
		m_iTacHudSel = 0;

	EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_VOICE, "weapons/tacact.wav", 1.0, ATTN_NORM ); // ref CTac::Think @0x18c838 chan 2

	switch( m_iTacHudSel )
	{
	case 0: m_pPlayer->m_iHudInfoMode = 1; break;
	case 1: m_pPlayer->m_iHudInfoMode = 2; break;
	default: m_pPlayer->m_iHudInfoMode = 3; break;
	}

	// ref SA: all three timers use the 0.55 const (WTB+delay).
	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.55f;
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.55f;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
		UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );

	// ref @0x18efea: SendWeaponAnim(this, 2, 1, 0); SetThink(WeaponIdle) (encoded 0x175).
	SetThink( &CTacgun::WeaponIdle );
	SetTouch( NULL );
	pev->nextthink = gpGlobals->time + 0.55f;
	SendWeaponAnim( 2 );
	m_flSgtcPad200 = 0.0f;	// ref SA tail (same +0x200 clear as PA)
}

SGTC_NO_RELOAD( CTacgun )

// ref @0x18f120 — NOT the stock SGTC_WEAPON_IDLE: sets player m_flNextAttack +0.4,
// then when idle timer elapses: ammo>0 -> SendWeaponAnim(0,1,0) + re-arm 10-15s;
// ammo<=0 -> Holster() + RetireWeapon().
void CTacgun::WeaponIdle( void )
{
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.4f;	// #135 CLIENT_WEAPONS

	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0 )
	{
		SendWeaponAnim( 0 );	// ref anim index 0 (idle), skip-local 1, body 0
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
			UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
	}
	else
	{
		Holster();
		RetireWeapon();
	}
}
