// sgtc_wpn_larve.cpp — StargateTC CLarve (split from sgtc_weapons.cpp; NEW file).

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

// ref-derived members now on the real classes:
//   weapon +0x1fc → CLarve::m_flSgtcLarveFireLatch (set 1 on larve spawn, consumed by WeaponIdle)
//   player +0x904 → CBasePlayer::m_iSgtcAmmoLatch904 — shared SGTC ammo-transfer latch:
//   PA/SA/WeaponIdle add it to m_rgAmmo[m_iPrimaryAmmoType] then zero it, and if
//   player pev->viewmodel (+0xbc) is NULL set ALLOC_STRING("models/v_larve.mdl") + anim(4).

#define SGTC_LARVE_DRAIN_AMMO_LATCH()											\
	if( m_pPlayer->m_iSgtcAmmoLatch904 > 0 )									\
	{																		\
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] += m_pPlayer->m_iSgtcAmmoLatch904;	\
		m_pPlayer->m_iSgtcAmmoLatch904 = 0;									\
		if( m_pPlayer->pev->viewmodel == 0 )								\
		{																	\
			m_pPlayer->pev->viewmodel =										\
				ALLOC_STRING( "models/v_larve.mdl" );						\
			SendWeaponAnim( 4 );	/* ref vcall+0x160(this, 4, 1, 0) */		\
		}																	\
	}

// ===========================================================================
// CLarve - "Larve" bioweapon launcher. Analogue: squeakgrenade.cpp.
// Event: events/larve.sc.
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_larve, CLarve );

// Save/restore (#99). m_flSgtcLarveFireLatch is a 0/1 flag held in a float, not a time.
// Not saved: m_usLarveFire (event handle, rebuilt by Precache()).
TYPEDESCRIPTION CLarve::m_SaveData[] =
{
	DEFINE_FIELD( CLarve, m_flSgtcLarveFireLatch, FIELD_FLOAT ),
};

IMPLEMENT_SAVERESTORE( CLarve, CBasePlayerWeapon )

void CLarve::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_larve.mdl" );
	m_iId = SGTC_WPN_LARVE;
	m_iClip = -1;
	m_iDefaultAmmo = 2;
	FallInit();
}

void CLarve::Precache( void )
{
	PRECACHE_MODEL( "models/w_larve.mdl" );
	PRECACHE_MODEL( "models/v_larve.mdl" );
	PRECACHE_MODEL( "models/p_larve.mdl" );
	PRECACHE_SOUND( "weapons/larve1.wav" );
	PRECACHE_SOUND( "weapons/larve2.wav" );
	PRECACHE_SOUND( "common/null.wav" );

	UTIL_PrecacheOther( "monster_larve" );

	m_usLarveFire = PRECACHE_EVENT( 1, "events/larve.sc" );
}

SGTC_GETITEM( CLarve, SGTC_AMMO_LARVE, 2, WEAPON_NOCLIP, 5, 1, SGTC_WPN_LARVE, 5, 10 )
SGTC_ADD_TO_PLAYER( CLarve )

BOOL CLarve::Deploy( void )	// ref @0x112968
{
	if( RANDOM_FLOAT( 0.0f, 1.0f ) <= 0.5f )
		EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "weapons/larve1.wav", 1.0, 0.8, 0, 100 );
	else
		EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "weapons/larve2.wav", 1.0, 0.8, 0, 100 );

	m_pPlayer->m_iWeaponVolume = 200;	// ref +0xaac
	return SGTC_DEPLOY( 2, "models/v_larve.mdl", "models/p_larve.mdl", 4, "squeak" );
}

void CLarve::Holster( int skiplocal )	// ref @0x112a86
{
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;	// ref player +0x39c

	if( !m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] )
	{
		// ref: player pev+0x168 (weapons) &= ~bit22 → clear our weapon bit
		m_pPlayer->pev->weapons &= ~( 1 << SGTC_WPN_LARVE );
		// ref: pfnThink = CBasePlayerItem::DestroyItem; thinktime 0; pev+0x104 = time+0.1
		SetThink( &CBasePlayerItem::DestroyItem );
		pev->nextthink = gpGlobals->time + 0.1f;
	}
	else
		EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "common/null.wav", 1.0, ATTN_NORM );
}

// ref @0x112b82 — trace origin +0..+15u along v_forward (point trace), spawn
// monster_larve at tr.vecEndPos when fraction > 0.25, then drain +0x904 latch.
void CLarve::PrimaryAttack( void )
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )	// ref player +0x408
		return;

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] != 0 )
	{
		UTIL_MakeVectors( m_pPlayer->pev->v_angle );	// ref pev+0x74 (v_angle only)

		TraceResult tr;
		Vector vecStart = m_pPlayer->pev->origin;	// ref: pev+0x8
		if( ( ( m_pPlayer->pev->flags >> 0xe ) & 1 ) != 0 )	// ref pev+0x1a4 bit14 (FL_DUCKING)
			vecStart = vecStart - Vector( 0, 0, 16 );	// ref (-16,-16,-20)−(−16,−16,−36) hull delta

		// ref @0x102d6b: TraceLine( vecStart + v_forward*15.0, vecStart,
		//      dont_ignore_monsters(0), NULL, &tr ) — backward 15u point trace.
		UTIL_TraceLine( vecStart + gpGlobals->v_forward * 15.0f, vecStart,
			dont_ignore_monsters, NULL, &tr );

		m_pPlayer->SnagInvis();
		PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usLarveFire,
			0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0, 0, 0, 0, 0, 0 );

		if( !tr.fStartSolid && !tr.fAllSolid && tr.flFraction > 0.25f )
		{
			m_pPlayer->SetAnimation( (PLAYER_ANIM)6 );	// ref SetAnimation(6) @0x102e6e

#ifndef CLIENT_DLL
			// ref Create( "monster_larve", tr.vecEndPos, pev+0x74 v_angle, player edict )
			CBaseEntity *pLarve = CBaseEntity::Create( "monster_larve", tr.vecEndPos,
				m_pPlayer->pev->v_angle, m_pPlayer->edict() );
			if( pLarve )
			{
				// ref: larve velocity = player pev+0x20 (velocity) + v_forward*0.0
				pLarve->pev->velocity = m_pPlayer->pev->velocity;
				// ref strncmp on player m_szTeamName (+0xe08): "Goa'uld"(7)→1, "Torri"(5)→2 @pev+0x1ac
				if( !strncmp( m_pPlayer->m_szTeamName, "Goa'uld", 7 ) )
					pLarve->pev->team = SGTC_ENTTEAM_GOAULD;
				else if( !strncmp( m_pPlayer->m_szTeamName, "Torri", 5 ) )
					pLarve->pev->team = SGTC_ENTTEAM_TORRI;
			}
#endif

			// ref: random larve1/larve2 on weapon edict, chan 2, pitch 0x69 (105)
			if( RANDOM_FLOAT( 0.0f, 1.0f ) <= 0.5f )
				EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "weapons/larve1.wav", 1.0, 0.8, 0, 105 );
			else
				EMIT_SOUND_DYN( ENT( pev ), CHAN_VOICE, "weapons/larve2.wav", 1.0, 0.8, 0, 105 );

			m_pPlayer->m_iWeaponVolume = 200;	// ref player +0xaac
			m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]--;	// ref ammo −1
			m_flSgtcLarveFireLatch = 1;					// ref this+0x1fc = 1 (@0x1030c1)

			m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.3f;	// ref +0x1d4
			m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 1.0f;	// ref +0x1dc
		}
	}

	SGTC_LARVE_DRAIN_AMMO_LATCH();	// ref tail @0x103103: +0x904→ammo, viewmodel+anim(4)
}

// ref @0x1131e2 — detonate owned larves + drain +0x904 latch; timers 0.5/1.0.
void CLarve::SecondaryAttack( void )
{
	// ref: scan larves (ref literal @0x10325f "larve"; Create() names them
	// "monster_larve" and nothing renames them); owned
	// (pev+0x198 owner == player edict) → entity+0x41c = 1, entity+0x3fc = time−1.
	CBaseEntity *pEnt = NULL;
	while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "monster_larve" ) ) != NULL )
	{
		if( FClassnameIs( pEnt->pev, "monster_larve" ) &&
			pEnt->pev->owner == m_pPlayer->edict() )
		{
			CSGTCLarve *pLarve = (CSGTCLarve *)pEnt;
			pLarve->m_bSgtcLarveDetLatch = 1;	// ref +0x41c
			pLarve->m_flDie = gpGlobals->time - 1.0f;	// ref +0x3fc / die gate
		}
	}

	SGTC_LARVE_DRAIN_AMMO_LATCH();	// ref tail: +0x904→ammo, viewmodel+anim(4)

	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.5f;	// ref +0x1d8
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 1.0f;		// ref +0x1dc
}

// ref @0x11338a — latch drain, then +0x1fc/ammo-driven idle state machine.
void CLarve::WeaponIdle( void )
{
	SGTC_LARVE_DRAIN_AMMO_LATCH();	// ref head: +0x904→ammo, viewmodel+anim(4)

	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )	// ref +0x1dc <= WTB required
		return;

	if( m_flSgtcLarveFireLatch == 0 )	// ref this+0x1fc == 0
	{
		if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] == 0 )
		{
			m_pPlayer->pev->viewmodel = 0;	// ref player pev+0xbc = NULL
		}
		else
		{
			float flRoll = UTIL_SharedRandomFloat( m_pPlayer->random_seed, 0.0f, 1.0f );
			int iAnim;
			if( flRoll <= 0.75f )
			{
				iAnim = 0;
				m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 3.75f;
			}
			else if( flRoll <= 0.875f )
			{
				iAnim = 1;
				m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 4.375f;
			}
			else
			{
				iAnim = 2;
				m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 5.0f;
			}
			SendWeaponAnim( iAnim );	// ref vcall+0x160(this, anim, 1, 0)
		}
	}
	else
	{
		m_flSgtcLarveFireLatch = 0;	// ref this+0x1fc = 0
		if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] == 0 )
		{
			m_pPlayer->pev->viewmodel = 0;	// ref player pev+0xbc = NULL
		}
		else
		{
			SendWeaponAnim( 4 );	// ref vcall+0x160(this, 4, 1, 0)
		}
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
			UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
	}
}

SGTC_NO_RELOAD( CLarve )
