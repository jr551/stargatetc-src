// sgtc_wpn_lance.cpp — StargateTC CLanceJaffa (split from sgtc_weapons.cpp; NEW file).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
#include "sgtc_weapons.h"
#include "sgtc_lanceproj.h"
#include "sgtc_util.h"

// ===========================================================================
// CLanceJaffa - "Lance Jaffa" staff weapon. Analogue: rpg.cpp (projectile).
// Events: events/lance.sc (bolt) / events/lance2.sc (melee bash).
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_lance, CLanceJaffa );

// Save/restore (#99). Not saved: m_usFire/m_usFire2/m_iLaserBeam (precache handles), m_trLastHit
// (TraceResult holds an edict pointer and per-swing scratch) and m_iSwing
// (unused here).
TYPEDESCRIPTION CLanceJaffa::m_SaveData[] =
{
	DEFINE_FIELD( CLanceJaffa, m_flSgtcRegenNext, FIELD_TIME ),
};

IMPLEMENT_SAVERESTORE( CLanceJaffa, CBasePlayerWeapon )

void CLanceJaffa::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_lancegoa.mdl" );
	m_iId = SGTC_WPN_LANCE;
	m_iDefaultAmmo = 22;
	m_flSgtcRegenNext = 0.0f;
	FallInit();
}

void CLanceJaffa::Precache( void )
{
	PRECACHE_MODEL( "models/v_lancegoa.mdl" );
	PRECACHE_MODEL( "models/w_lancegoa.mdl" );
	PRECACHE_MODEL( "models/p_lancegoa.mdl" );
	PRECACHE_SOUND( "weapons/tirlancegoa.wav" );
	PRECACHE_SOUND( "weapons/lancegoavide.wav" );
	PRECACHE_SOUND( "weapons/cbar_hit1.wav" );
	PRECACHE_SOUND( "weapons/cbar_hit2.wav" );
	PRECACHE_SOUND( "weapons/hitbody1.wav" );
	PRECACHE_SOUND( "weapons/hitbody2.wav" );
	PRECACHE_SOUND( "weapons/hitbody3.wav" );
	PRECACHE_SOUND( "weapons/swing1.wav" );
	PRECACHE_SOUND( "weapons/swing2.wav" );
	PRECACHE_SOUND( "weapons/swing3.wav" );

	m_usFire = PRECACHE_EVENT( 1, "events/lance.sc" );
	m_usFire2 = PRECACHE_EVENT( 1, "events/lance2.sc" );
	m_iLaserBeam = PRECACHE_MODEL( "sprites/laserbeam.spr" );	// ref +0x204
	UTIL_PrecacheOther( "lance_proj" );	// ref @0x10f254 — proj owns model/sfx precache
}

SGTC_GETITEM( CLanceJaffa, SGTC_AMMO_ENERGIE3, 22, WEAPON_NOCLIP, 3, 2, SGTC_WPN_LANCE, 5, 15 )
SGTC_ADD_TO_PLAYER( CLanceJaffa )

BOOL CLanceJaffa::Deploy( void )
{
	return SGTC_DEPLOY( 1, "models/v_lancegoa.mdl", "models/p_lancegoa.mdl", 3, "rpg" );
}

void CLanceJaffa::Holster( int skiplocal )	// ref @0x10f53c
{
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;	// ref player +0x39c
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] == 0 )
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]++;
	SendWeaponAnim( 5, 1, 0 );	// ref anim 5
}

void CLanceJaffa::Reload( void )	// ref @0x10f364
{
	// ref cap 0x16, +1 round per 2.0s
	SGTC_RegenAmmo( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType], m_flSgtcRegenNext,
		22, 1, 2.0f );
}

void CLanceJaffa::WeaponIdle( void )	// ref @0x10f40c
{
	ResetEmptySound();
	Reload();
	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;

	if( UTIL_SharedRandomFloat( m_pPlayer->random_seed, 0.0f, 1.0f ) <= 0.75f )
	{
		SendWeaponAnim( SGTC_IDLE, 1, 0 );
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
			UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
	}
	else
	{
		SendWeaponAnim( 4, 1, 0 );	// ref anim 4
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.5f;
	}
}

void CLanceJaffa::PrimaryAttack( void )	// ref @0x110358
{
	// ref: gate + ammo>0 in one branch; no PlayEmptySound on dry fire.
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )	// ref player +0x408
		return;
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
		return;

	float flTalentTrim = 0.0f;

	// ref order: event -> SnagInvis -> anim 6 -> MakeVectors -> trace -> bolt.
	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFire,
		0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0.0f, 0.0f,
		0, 0, 0, 0 );

	m_pPlayer->SnagInvis();
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );	// ref anim 6

#ifndef CLIENT_DLL
	UTIL_MakeVectors( m_pPlayer->pev->v_angle );	// ref +0x74 — no punchangle
	Vector vecDir = gpGlobals->v_forward;
	Vector vecSrc = m_pPlayer->GetGunPosition();
	TraceResult tr;
	UTIL_TraceLine( vecSrc, vecSrc + vecDir * 8192.0f, dont_ignore_monsters,
		m_pPlayer->edict(), &tr );

	CLanceProj *pBolt = CLanceProj::CreateLanceProj( vecSrc + vecDir * 16.0f,
		m_pPlayer->pev->velocity, tr.vecEndPos, m_pPlayer, this );

	if( pBolt )
	{
		// ref @0x110358: if time < player+0xa2c stamp bolt+0xd5; copy pev+0x74.
		if( gpGlobals->time < m_pPlayer->m_flSgtcShieldTime )
			pBolt->m_fAttendExplosion = TRUE;
		pBolt->pev->v_angle = m_pPlayer->pev->v_angle;

		for( int i = 1; i < 3; i++ )
			SGTC_LaserBeamEnts( m_pPlayer, pBolt, m_iLaserBeam, 5 );
	}
#endif

	if( gpGlobals->time < m_pPlayer->m_flTalentCooldown &&
		m_pPlayer->pev->team == SGTC_ENTTEAM_TORRI &&
		m_pPlayer->m_iSgtcPortraitShown == SGTC_CLASS_TEALC )
		flTalentTrim = 0.5f;	// ref local_1c = 0.5 (+0x494 / team / +0xa40==4)

	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.6f - flTalentTrim;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
		UTIL_SharedRandomFloat( m_pPlayer->random_seed, 7.0f, 10.0f );
	m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]--;
	m_flSgtcRegenNext = gpGlobals->time + 0.8f - flTalentTrim;	// ref +0x1fc
}

void CLanceJaffa::SecondaryAttack( void )	// ref @0x10ffd2
{
	// ref gate: m_flGateTravel (+0x408) <= gpGlobals->time
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )
		return;

	// ref: if( Swing(1) || WTB <= m_flNextSecondaryAttack ) return;
	// else SwingAgain(); pev->nextthink = time + 0.1; SnagInvis().
	if( Swing( 1 ) || UTIL_WeaponTimeBase() <= m_flNextSecondaryAttack )
		return;

	SwingAgain();
	pev->nextthink = gpGlobals->time + 0.1f;
	m_pPlayer->SnagInvis();
}

void CLanceJaffa::SwingAgain( void )	// ref @0x10ffaa
{
	Swing( 0 );
}

int CLanceJaffa::Swing( int fFirst )	// ref @0x10f5d0
{
	int fDidHit = FALSE;
	TraceResult tr;
	float flVol = 1.0f;
	int fHitWorld = TRUE;

	UTIL_MakeVectors( m_pPlayer->pev->v_angle );
	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecEnd = vecSrc + gpGlobals->v_forward * 32.0f;

	UTIL_TraceLine( vecSrc, vecEnd, dont_ignore_monsters, ENT( m_pPlayer->pev ), &tr );

	SGTC_MeleeHullFallback( ENT( m_pPlayer->pev ), vecSrc, tr, vecEnd );

	m_pPlayer->SetAnimation( (PLAYER_ANIM)7 );
	SendWeaponAnim( SGTC_FIRE2, 1, 0 );

	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFire2,
		0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0, 0, 0, 0, 0, 0 );

	if( tr.flFraction >= 1.0f )
	{
		if( fFirst )
			m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.5f;
	}
	else
	{
		fDidHit = TRUE;

#ifndef CLIENT_DLL
		CBaseEntity *pEntity = CBaseEntity::Instance( tr.pHit );

		ClearMultiDamage();

		int iTalentAdd = 0;
		if( gpGlobals->time < m_pPlayer->m_flTalentCooldown &&
			m_pPlayer->pev->team == SGTC_ENTTEAM_TORRI &&
			m_pPlayer->m_iSgtcPortraitShown == SGTC_CLASS_TEALC )
			iTalentAdd = 0x46;

		// ref gSkillData+0x120 = 42.0f (MP hardcode @0x116d50) — not a stock field.
		const float flFull = (float)iTalentAdd + kSgtcSkillLanceDmg;
		const float flHalf = (float)iTalentAdd + kSgtcSkillLanceDmg * 0.5f;

		if( pEntity )
		{
			if( ( m_flNextSecondaryAttack + 1.0f < UTIL_WeaponTimeBase() ) ||
				g_pGameRules->IsMultiplayer() )
			{
				pEntity->TraceAttack( m_pPlayer->pev, flFull,
					gpGlobals->v_forward, &tr, DMG_CLUB );
			}
			else
			{
				pEntity->TraceAttack( m_pPlayer->pev, flHalf,
					gpGlobals->v_forward, &tr, DMG_CLUB );
			}
		}

		ApplyMultiDamage( m_pPlayer->pev, m_pPlayer->pev );

		if( pEntity && pEntity->Classify() != CLASS_NONE &&
			pEntity->Classify() != CLASS_MACHINE )
		{
			switch( RANDOM_LONG( 0, 2 ) )
			{
			case 0: EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_ITEM,
				"weapons/hitbody1.wav", 1.0f, 0.8f ); break; // ref @0xffbfa chan 3
			case 1: EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_ITEM,
				"weapons/hitbody2.wav", 1.0f, 0.8f ); break;
			case 2: EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_ITEM,
				"weapons/hitbody3.wav", 1.0f, 0.8f ); break;
			}
			m_pPlayer->m_iWeaponVolume = 128;
			if( !pEntity->IsAlive() )
				return TRUE;
			flVol = 0.1f;
			fHitWorld = FALSE;
		}
#endif

		if( fHitWorld )
		{
			flVol = TEXTURETYPE_PlaySound( &tr, vecSrc,
				vecSrc + ( vecEnd - vecSrc ) * 2.0f, BULLET_PLAYER_CROWBAR );
			if( g_pGameRules && g_pGameRules->IsMultiplayer() )
				flVol = 1.0f;
			switch( RANDOM_LONG( 0, 1 ) )
			{
			case 0:
				EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM,
					"weapons/cbar_hit1.wav", flVol, 0.8f, 0,
					98 + RANDOM_LONG( 0, 3 ) );
				break;
			default:
				EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM,
					"weapons/cbar_hit2.wav", flVol, 0.8f, 0,
					98 + RANDOM_LONG( 0, 3 ) );
				break;
			}
			m_trLastHit = tr;
		}

		m_pPlayer->m_iWeaponVolume = (int)( flVol * 512.0f );
		m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.25f;
		if( gpGlobals->time < m_pPlayer->m_flTalentCooldown &&
			m_pPlayer->pev->team == SGTC_ENTTEAM_TORRI &&
			m_pPlayer->m_iSgtcPortraitShown == SGTC_CLASS_TEALC )
			m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.15f;

		Smack();
		pev->nextthink = UTIL_WeaponTimeBase() + 0.2f;
	}

	return fDidHit;
}

void CLanceJaffa::Smack( void )	// ref @0x10f5ca — empty no-op
{
}
