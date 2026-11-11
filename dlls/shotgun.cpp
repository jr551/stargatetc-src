/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "nodes.h"
#include "player.h"
#include "gamerules.h"
#include "sgtc_weapons.h"

// SGTC weapon_spas uses stock CShotgun (ref/stc_i386.so link table).
#define SGTC_SPAS_MAX_CLIP 8
#if !defined( CLIENT_WEAPONS )
#define SPAS_EVENT_FLAGS SGTC_EVENT_FLAGS
#else
#define SPAS_EVENT_FLAGS FEV_NOTHOST
#endif

// special deathmatch shotgun spreads
#define VECTOR_CONE_DM_SHOTGUN	Vector( 0.08716, 0.04362, 0.00 )// 10 degrees by 5 degrees
#define VECTOR_CONE_DM_DOUBLESHOTGUN Vector( 0.17365, 0.04362, 0.00 ) // 20 degrees by 5 degrees

enum shotgun_e
{
	SHOTGUN_IDLE = 0,
	SHOTGUN_FIRE,
	SHOTGUN_FIRE2,
	SHOTGUN_RELOAD,
	SHOTGUN_PUMP,
	SHOTGUN_START_RELOAD,
	SHOTGUN_DRAW,
	SHOTGUN_HOLSTER,
	SHOTGUN_IDLE4,
	SHOTGUN_IDLE_DEEP
};

// SGTC: weapon_shotgun unregistered — ref/stc_i386.so link table has no weapon_shotgun

void CShotgun::Spawn()
{
	Precache();
	m_iId = SGTC_WPN_SHOTGUN_STOCK;
	SET_MODEL( ENT( pev ), "models/w_spas.mdl" );

	m_iDefaultAmmo = 60;

	FallInit();// get ready to fall
}

void CShotgun::Precache( void )
{
	PRECACHE_MODEL( "models/v_spas.mdl" );
	PRECACHE_MODEL( "models/w_spas.mdl" );
	PRECACHE_MODEL( "models/p_spas.mdl" );

	m_iShell = PRECACHE_MODEL( "models/shell.mdl" );

	PRECACHE_SOUND( "items/9mmclip1.wav" );
	PRECACHE_SOUND( "weapons/reload1.wav" );
	PRECACHE_SOUND( "weapons/reload3.wav" );
	PRECACHE_SOUND( "weapons/spastir.wav" );
	PRECACHE_SOUND( "weapons/spaspomp.wav" );
	PRECACHE_SOUND( "weapons/357_cock1.wav" );

	m_usSingleFire = PRECACHE_EVENT( 1, "events/spas.sc" );
	m_usDoubleFire = PRECACHE_EVENT( 1, "events/spas2.sc" );
}

int CShotgun::AddToPlayer( CBasePlayer *pPlayer )
{
	if( CBasePlayerWeapon::AddToPlayer( pPlayer ) )
	{
		MESSAGE_BEGIN( MSG_ONE, gmsgWeapPickup, NULL, pPlayer->pev );
			WRITE_BYTE( m_iId );
		MESSAGE_END();
		return TRUE;
	}
	return FALSE;
}

int CShotgun::GetItemInfo( ItemInfo *p )
{
	p->pszName = STRING( pev->classname );
	p->pszAmmo1 = SGTC_AMMO_BUCKSHOT;
	p->iMaxAmmo1 = 125;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = SGTC_SPAS_MAX_CLIP;
	p->iSlot = 2;
	p->iPosition = 3;
	p->iFlags = 0;
	p->iId = m_iId = SGTC_WPN_SHOTGUN_STOCK;
	p->iWeight = 14;

	return 1;
}

BOOL CShotgun::Deploy()
{
#ifndef CLIENT_WEAPONS
#if defined( CLIENT_DLL )
	// SGTC_DEPLOY calls the server-only CBasePlayer::SGTC_WeapDispSet, which the
	// client does not link. The client only needs the draw animation.
	return DefaultDeploy( "models/v_spas.mdl", "models/p_spas.mdl", 6, "mp5" );
#else
	return SGTC_DEPLOY( 2, "models/v_spas.mdl", "models/p_spas.mdl", 6, "mp5" );
#endif
#else
	return DefaultDeploy( "models/v_spas.mdl", "models/p_spas.mdl", SHOTGUN_DRAW, "mp5" );
#endif
}

void CShotgun::PrimaryAttack()
{
	// don't fire underwater
	if( m_pPlayer->pev->waterlevel == 3 )
	{
		PlayEmptySound();
		m_flNextPrimaryAttack = GetNextAttackDelay( 0.15f );
		return;
	}

	if( m_iClip <= 0 )
	{
		Reload();
		if( m_iClip == 0 )
			PlayEmptySound();
		return;
	}

	m_pPlayer->m_iWeaponVolume = LOUD_GUN_VOLUME;
	m_pPlayer->m_iWeaponFlash = NORMAL_GUN_FLASH;

	m_iClip--;

	m_pPlayer->pev->effects = (int)( m_pPlayer->pev->effects ) | EF_MUZZLEFLASH;

	// player "shoot" animation
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );

	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecAiming = m_pPlayer->GetAutoaimVector( AUTOAIM_5DEGREES );

	Vector vecDir = m_pPlayer->FireBulletsPlayer( 8, vecSrc, vecAiming, VECTOR_CONE_DM_SHOTGUN,
		2048, SGTC_BULLET_SPAS, 0, 4, m_pPlayer->pev, m_pPlayer->random_seed );

	PLAYBACK_EVENT_FULL( SPAS_EVENT_FLAGS, m_pPlayer->edict(), m_usSingleFire,
		0.0, (float *)&g_vecZero, (float *)&g_vecZero, vecDir.x, vecDir.y, 0, 0, 0, 0 );

#if !defined( CLIENT_WEAPONS ) && !defined( CLIENT_DLL )
	m_pPlayer->SnagInvis();
#endif

	if( !m_iClip && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
		// HEV suit - indicate out of ammo condition
		m_pPlayer->SetSuitUpdate( "!HEV_AMO0", FALSE, 0 );

	//if( m_iClip != 0 )
		m_flPumpTime = gpGlobals->time + 0.5f;

	m_flNextPrimaryAttack = GetNextAttackDelay( 0.75f );
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.75f;
	if( m_iClip != 0 )
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 5.0f;
	else
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.75f;
	m_fInSpecialReload = 0;
}

void CShotgun::SecondaryAttack( void )
{
	// ref CShotgun has no SecondaryAttack override (stock HL double-tap removed).
}

void CShotgun::Reload( void )
{
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 || m_iClip == SGTC_SPAS_MAX_CLIP )
		return;

	// don't reload until recoil is done
	if( m_flNextPrimaryAttack > UTIL_WeaponTimeBase() )
		return;

	// check to see if we're ready to reload
	if( m_fInSpecialReload == 0 )
	{
		SendWeaponAnim( SHOTGUN_START_RELOAD );
		m_fInSpecialReload = 1;
		m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.6f;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.6f;
		m_flNextPrimaryAttack = GetNextAttackDelay( 1.0f );
		m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 1.0f;
		return;
	}
	else if( m_fInSpecialReload == 1 )
	{
		if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
			return;
		// was waiting for gun to move to side
		m_fInSpecialReload = 2;

		if( RANDOM_LONG( 0, 1 ) )
			EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM, "weapons/reload1.wav", 1, ATTN_NORM, 0, 85 + RANDOM_LONG( 0, 0x1f ) );
		else
			EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM, "weapons/reload3.wav", 1, ATTN_NORM, 0, 85 + RANDOM_LONG( 0, 0x1f ) );

		SendWeaponAnim( SHOTGUN_RELOAD );

		m_flNextReload = UTIL_WeaponTimeBase() + 0.5f;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.5f;
	}
	else
	{
		// Add them to the clip
		m_iClip += 1;
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] -= 1;
		m_fInSpecialReload = 1;
	}
}

void CShotgun::ItemPostFrame( void )
{
	if( m_flPumpTime && m_flPumpTime < gpGlobals->time )
	{
		PLAYBACK_EVENT_FULL( SPAS_EVENT_FLAGS, m_pPlayer->edict(), m_usDoubleFire,
			0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0, 0, 0, 0, 0, 0 );
		EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM, "weapons/spaspomp.wav", 1, ATTN_NORM, 0, 95 + RANDOM_LONG( 0, 0x1f ) );
		m_flPumpTime = 0;
	}

	CBasePlayerWeapon::ItemPostFrame();
}

void CShotgun::WeaponIdle( void )
{
	ResetEmptySound();

	m_pPlayer->GetAutoaimVector( AUTOAIM_5DEGREES );

	if( m_flTimeWeaponIdle <  UTIL_WeaponTimeBase() )
	{
		if( m_iClip == 0 && m_fInSpecialReload == 0 && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] )
		{
			Reload();
		}
		else if( m_fInSpecialReload != 0 )
		{
			if( m_iClip != SGTC_SPAS_MAX_CLIP && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] )
			{
				Reload();
			}
			else
			{
				// reload debounce has timed out
				SendWeaponAnim( SHOTGUN_PUMP );
				
				PLAYBACK_EVENT_FULL( SPAS_EVENT_FLAGS, m_pPlayer->edict(), m_usDoubleFire,
					0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0, 0, 0, 0, 0, 0 );
				EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM, "weapons/spaspomp.wav", 1, ATTN_NORM, 0, 95 + RANDOM_LONG( 0, 0x1f ) );
				m_fInSpecialReload = 0;
				m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 1.5f;
			}
		}
		else
		{
			int iAnim;
			float flRand = UTIL_SharedRandomFloat( m_pPlayer->random_seed, 0, 1 );
			if( flRand <= 0.8f )
			{
				iAnim = SHOTGUN_IDLE_DEEP;
				m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + ( 60.0f / 12.0f );// * RANDOM_LONG( 2, 5 );
			}
			else if( flRand <= 0.95f )
			{
				iAnim = SHOTGUN_IDLE;
				m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + ( 20.0f / 9.0f );
			}
			else
			{
				iAnim = SHOTGUN_IDLE4;
				m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + ( 20.0f / 9.0f );
			}
			SendWeaponAnim( iAnim );
		}
	}
}

class CShotgunAmmo : public CBasePlayerAmmo
{
	void Spawn( void )
	{ 
		Precache();
		SET_MODEL( ENT( pev ), "models/w_shotbox.mdl" );
		CBasePlayerAmmo::Spawn();
	}
	void Precache( void )
	{
		PRECACHE_MODEL( "models/w_shotbox.mdl" );
		PRECACHE_SOUND( "items/9mmclip1.wav" );
	}
	BOOL AddAmmo( CBaseEntity *pOther ) 
	{ 
		if( pOther->GiveAmmo( AMMO_BUCKSHOTBOX_GIVE, "buckshot", BUCKSHOT_MAX_CARRY ) != -1 )
		{
			EMIT_SOUND( ENT( pev ), CHAN_ITEM, "items/9mmclip1.wav", 1, ATTN_NORM );
			return TRUE;
		}
		return FALSE;
	}
};

LINK_ENTITY_TO_CLASS( ammo_buckshot, CShotgunAmmo )
