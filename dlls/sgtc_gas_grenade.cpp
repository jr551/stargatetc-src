// sgtc_gas_grenade.cpp — gas grenade projectiles + shared weapon cook/throw (ref P106).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "sgtc_gas_grenade.h"
#include "sgtc_weapons.h"
#include "soundent.h"

short g_sModelIndexSgtcMustard = 0;
short g_sModelIndexSgtcNarco = 0;
short g_sModelIndexSgtcNerve = 0;
short g_sModelIndexSgtcGasSmoke = 0;

void SGTC_PrecacheGasSprites( void )
{
	// Re-precache every map: model indices are per-level; stale globals break gas clouds (#133).
	g_sModelIndexSgtcMustard = PRECACHE_MODEL( "sprites/gasmustard.spr" );
	g_sModelIndexSgtcNarco = PRECACHE_MODEL( "sprites/nargas.spr" );
	g_sModelIndexSgtcNerve = PRECACHE_MODEL( "sprites/ngas.spr" );
	g_sModelIndexSgtcGasSmoke = PRECACHE_MODEL( "sprites/steam1.spr" );
}

static short SGTC_GasSpriteIndex( int gasType )
{
	switch( gasType )
	{
	case SGTC_GAS_MUSTARD: return g_sModelIndexSgtcMustard;
	case SGTC_GAS_NARCO: return g_sModelIndexSgtcNarco;
	case SGTC_GAS_NERVE: return g_sModelIndexSgtcNerve;
	case SGTC_GAS_SMOKE: return g_sModelIndexSgtcGasSmoke;
	default: return 0;
	}
}

// ref MustardThink @0x11bbba: width = (int)((pev->dmg - 50) * scale); rodata 50 @0x1d4cf4,
// scale 0.7 @0x1d4cf8 / 0.3 @0x1d4cd8; TE life 12 @0x11bc16; think +0.1 @0x1d4d00.
static int SGTC_GasBeamWidth( float flDmg, float flScale )
{
	int w = (int)(( flDmg - 50.0f ) * flScale);
	if( w < 1 )
		w = 1;
	return w;
}

static void SGTC_GasBeamPoints( const Vector &start, const Vector &end, short spriteIndex, int width )
{
	if( !spriteIndex || width <= 0 )
		return;
	MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, start );
		WRITE_BYTE( TE_BEAMPOINTS );
		WRITE_COORD( start.x );
		WRITE_COORD( start.y );
		WRITE_COORD( start.z );
		WRITE_COORD( end.x );
		WRITE_COORD( end.y );
		WRITE_COORD( end.z );
		WRITE_SHORT( spriteIndex );
		WRITE_BYTE( 0 );   // startframe
		WRITE_BYTE( 0 );   // framerate
		WRITE_BYTE( 12 );  // life (ref MustardThink @0x11bc16)
		WRITE_BYTE( width );
		WRITE_BYTE( 0 );   // noise
		WRITE_BYTE( 255 ); // r
		WRITE_BYTE( 255 ); // g
		WRITE_BYTE( 255 ); // b
		WRITE_BYTE( 128 ); // brightness
		WRITE_BYTE( 0 );   // speed
	MESSAGE_END();
}

void CSGTCGasCloud::Spawn( void )
{
	CGrenade::Spawn();
	pev->classname = MAKE_STRING( "sgtc_gascloud" );
	m_iGasType = SGTC_GAS_MUSTARD;
	m_flCloudRadius = 0;
	m_flExpireTime = 0;
	m_iThrowerTeam = 0;
}

LINK_ENTITY_TO_CLASS( sgtc_gascloud, CSGTCGasCloud );

// Save/restore (#99). Spawn sets classname sgtc_gascloud after CGrenade::Spawn (#113).
TYPEDESCRIPTION CSGTCGasCloud::m_SaveData[] =
{
	DEFINE_FIELD( CSGTCGasCloud, m_iGasType, FIELD_INTEGER ),
	DEFINE_FIELD( CSGTCGasCloud, m_flCloudRadius, FIELD_FLOAT ),
	DEFINE_FIELD( CSGTCGasCloud, m_flExpireTime, FIELD_TIME ),
	DEFINE_FIELD( CSGTCGasCloud, m_iThrowerTeam, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE( CSGTCGasCloud, CGrenade )

void CSGTCGasCloud::Detonate( void )
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	pev->velocity = g_vecZero;
	pev->effects |= EF_NODRAW;
	m_flCloudRadius = 24.0f;
	m_flExpireTime = gpGlobals->time + 30.0f;
	if( pev->owner )
	{
		CBaseEntity *pOwner = CBaseEntity::Instance( pev->owner );
		if( pOwner )
			m_iThrowerTeam = (int)pOwner->pev->team;
	}
	SetThink( &CSGTCGasCloud::CloudThink );
	pev->nextthink = gpGlobals->time + 0.1f;
	// ref CMustardGrenade::MustardThink @0x11b3dc: chan 4 (BODY).
	EMIT_SOUND( ENT( pev ), CHAN_BODY, "weapons/gasflow.wav", 1.0f, ATTN_NORM );
}

void CSGTCGasCloud::TumbleThink( void )
{
	if( !IsInWorld() )
	{
		UTIL_Remove( this );
		return;
	}

	StudioFrameAdvance();
	pev->nextthink = gpGlobals->time + 0.1f;

	if( pev->dmgtime - 1 < gpGlobals->time )
	{
		CSoundEnt::InsertSound( bits_SOUND_DANGER, pev->origin + pev->velocity * ( pev->dmgtime - gpGlobals->time ), 400, 0.1 );
	}

	if( pev->dmgtime <= gpGlobals->time )
	{
		SetThink( &CSGTCGasCloud::Detonate );
		pev->nextthink = gpGlobals->time;
		return;
	}
	if( pev->waterlevel != 0 )
	{
		pev->velocity = pev->velocity * 0.5f;
		pev->framerate = 0.2f;
	}
}

void CSGTCGasCloud::CloudThink( void )
{
	if( gpGlobals->time >= m_flExpireTime )
	{
		UTIL_Remove( this );
		return;
	}

	if( m_flCloudRadius < 150.0f )
		m_flCloudRadius += 4.0f;

	const Vector origin = pev->origin;
	const short iSpr = SGTC_GasSpriteIndex( m_iGasType );
	const int iWMain = SGTC_GasBeamWidth( pev->dmg, 0.7f );
	SGTC_GasBeamPoints( origin + Vector( 0, 0, 8 ), origin - Vector( 0, 0, 8 ), iSpr, iWMain );
	// ref @0x11bca6: second burst when ((pev->effects >> 4) & 1) == 0 (EF_NODRAW set).
	if( (((int)pev->effects >> 4) & 1) == 0 )
	{
		const int iWAlt = SGTC_GasBeamWidth( pev->dmg, 0.3f );
		SGTC_GasBeamPoints( origin, origin, iSpr, iWAlt );
	}
	// ref MustardThink @0x11b73b: RadiusDamage( pev->origin+8, pev,
	//   owner, dmg=5.0, radius=2.0, iClassIgnore=8, DMG_GENERIC ) each think.
	if( m_iGasType == SGTC_GAS_MUSTARD )
		::RadiusDamage( origin + Vector( 0, 0, 8 ), pev, VARS( pev->owner ), 5.0f, 2.0f, 8, DMG_GENERIC );

	CBaseEntity *pEnt = NULL;
	while( ( pEnt = UTIL_FindEntityInSphere( pEnt, origin, m_flCloudRadius ) ) != NULL )
	{
		if( !pEnt->IsPlayer() || !pEnt->IsAlive() )
			continue;
		CBasePlayer *pl = (CBasePlayer *)pEnt;
		if( (int)pl->pev->team == m_iThrowerTeam && m_iThrowerTeam != 0 )
			continue;

		switch( m_iGasType )
		{
		case SGTC_GAS_MUSTARD:
			// ref @0x11b7b8-0x11b8a3: unconditional per-player-per-think
			// cough pick: RANDOM_LONG(1,3) -> player/cough{1,2,3}.wav,
			// EMIT_SOUND CHAN_VOICE(2), vol 1.0, attn 0.8 (@0x11b82e vol rodata
			// @B-0x2fd2c=1.0, attn @B-0x2fd30=0.8). No RL(0,4) gate, no bustflesh.
			{
				const char *pszCough = "player/cough1.wav";
				switch( RANDOM_LONG( 1, 3 ) )
				{
				case 2: pszCough = "player/cough2.wav"; break;
				case 3: pszCough = "player/cough3.wav"; break;
				default: break;
				}
				EMIT_SOUND( ENT( pl->pev ), CHAN_VOICE, pszCough, 1.0f, 0.8f );
			}
			break;
		case SGTC_GAS_NARCO:
			pl->m_flSgtcLatch3fc = gpGlobals->time + 15.0f;
			break;
		case SGTC_GAS_NERVE:
			pl->TakeDamage( pev, VARS( pev->owner ), 1.0f, DMG_PARALYZE );	// ref @0x11e618: push $0x8000
			// ref @0x11e4a0-0x11e574: unconditional cough pick RANDOM_LONG(1,3)
			// -> player/cough{1,2,3}.wav, vol 1.0 attn 0.8 (@0x11e4f0 rodata
			// @B-0x2fa14=1.0, @B-0x2fa18=0.8). No RL(0,3) gate, no bustflesh.
			{
				const char *pszCough = "player/cough1.wav";
				switch( RANDOM_LONG( 1, 3 ) )
				{
				case 2: pszCough = "player/cough2.wav"; break;
				case 3: pszCough = "player/cough3.wav"; break;
				default: break;
				}
				EMIT_SOUND( ENT( pl->pev ), CHAN_VOICE, pszCough, 1.0f, 0.8f );
			}
			break;
		case SGTC_GAS_SMOKE:
			break;
		}
	}

	pev->nextthink = gpGlobals->time + 0.1f;
}

CSGTCGasCloud *SGTC_ShootGasGrenade( entvars_t *pevOwner, Vector vecStart,
	Vector vecVelocity, float time, const char *pszWorldModel, SGTC_GasType gasType,
	float flDmg )
{
	CSGTCGasCloud *pGrenade = GetClassPtr( (CSGTCGasCloud *)NULL );
	pGrenade->Spawn();
	UTIL_SetOrigin( pGrenade->pev, vecStart );
	pGrenade->pev->velocity = vecVelocity;
	pGrenade->pev->angles = UTIL_VecToAngles( vecVelocity );
	pGrenade->pev->owner = ENT( pevOwner );
	pGrenade->m_iGasType = gasType;
	pGrenade->pev->dmg = flDmg;
	if( gasType == SGTC_GAS_SMOKE )
		pGrenade->pev->health = 10.0f;	// ref CSmokeGrenade::WeaponIdle @0x174ca8

	pGrenade->SetTouch( &CGrenade::BounceTouch );
	pGrenade->pev->dmgtime = gpGlobals->time + time;
	pGrenade->SetThink( &CSGTCGasCloud::TumbleThink );
	pGrenade->pev->nextthink = gpGlobals->time + 0.1f;
	if( time < 0.1f )
	{
		pGrenade->pev->nextthink = gpGlobals->time;
		pGrenade->pev->velocity = g_vecZero;
	}

	SET_MODEL( ENT( pGrenade->pev ), pszWorldModel );
	pGrenade->pev->gravity = 0.5f;
	pGrenade->pev->friction = 0.8f;
	return pGrenade;
}

// --- weapon shared -----------------------------------------------------------

// Save/restore (#99), shared by CSmokeGrenade/CNerveGrenade/CNarcoGrenade/CMustardGrenade.
// m_flGasStartThrow (cook start) and m_flGasDeployIdle are gpGlobals->time stamps, so they
// are re-based by FIELD_TIME. m_flGasReleaseThrow is a 0 / stamp / -1 flag that is only ever
// compared against 0, so it is saved as a plain float to keep the -1 sentinel intact.
TYPEDESCRIPTION CSGTCGasGrenadeWeapon::m_SaveData[] =
{
	DEFINE_FIELD( CSGTCGasGrenadeWeapon, m_flGasStartThrow, FIELD_TIME ),
	DEFINE_FIELD( CSGTCGasGrenadeWeapon, m_flGasReleaseThrow, FIELD_FLOAT ),
	DEFINE_FIELD( CSGTCGasGrenadeWeapon, m_flGasDeployIdle, FIELD_TIME ),
};

IMPLEMENT_SAVERESTORE( CSGTCGasGrenadeWeapon, CBasePlayerWeapon )

BOOL CSGTCGasGrenadeWeapon::SGTC_GasCanHolster( void )
{
	return m_flGasStartThrow == 0.0f;
}

void CSGTCGasGrenadeWeapon::SGTC_GasHolster( int skiplocal, int iWeaponId )
{
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;

	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] )
		SendWeaponAnim( SGTC_HOLSTER );
	else
	{
		m_pPlayer->pev->weapons &= ~( 1 << iWeaponId );
		if( g_pGameRules )
			g_pGameRules->PlayerGotWeapon( m_pPlayer, this );
		DestroyItem();
	}

	if( m_flGasStartThrow )
	{
		m_flGasStartThrow = 0.0f;
		m_flGasReleaseThrow = 0.0f;
	}

	EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "common/null.wav", 1.0f, 0.8f );
}

BOOL CSGTCGasGrenadeWeapon::SGTC_GasDeploy( int disp, const char *pszView, const char *pszPlayer )
{
	m_flGasStartThrow = 0.0f;
	m_flGasReleaseThrow = 0.0f;
	m_flGasDeployIdle = gpGlobals->time + 0.6f;
	return SGTC_DEPLOY( disp, pszView, pszPlayer, SGTC_DRAW, "crowbar" );
}

void CSGTCGasGrenadeWeapon::SGTC_GasPinPull( void )
{
	if( m_flGasStartThrow != 0.0f )
		return;
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 )
		return;

	m_flGasStartThrow = gpGlobals->time;
	m_flGasReleaseThrow = 0.0f;
	m_pPlayer->SnagInvis();
	SendWeaponAnim( SGTC_FIRE1 );	// ref pin-pull anim 2
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.5f;
}

void CSGTCGasGrenadeWeapon::SGTC_GasWeaponIdle( int iWeaponId, SGTC_GasType gasType,
	const char *pszWorldModel, float flDmg )
{
	if( m_flGasDeployIdle > gpGlobals->time )
		return;

	if( m_flGasReleaseThrow == 0.0f && m_flGasStartThrow != 0.0f )
		m_flGasReleaseThrow = gpGlobals->time;

	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;

	if( m_flGasStartThrow != 0.0f )
	{
		Vector angThrow = m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle;
		if( angThrow.x < 0.0f )
			angThrow.x = -10.0f + angThrow.x * ( ( 90.0f - 10.0f ) / 90.0f );
		else
			angThrow.x = -10.0f + angThrow.x * ( ( 90.0f + 10.0f ) / 90.0f );

		float flVel = ( 90.0f - angThrow.x ) * 8.0f;
		if( flVel > 1000.0f )
			flVel = 1000.0f;

		UTIL_MakeVectors( angThrow );
		Vector vecSrc = m_pPlayer->pev->origin + m_pPlayer->pev->view_ofs + gpGlobals->v_forward * 16.0f;
		Vector vecThrow = gpGlobals->v_forward * flVel + m_pPlayer->pev->velocity;

		float time = m_flGasStartThrow - gpGlobals->time + 3.0f;
		if( time < 0.0f )
			time = 0.0f;

#ifndef CLIENT_DLL
		CSGTCGasCloud *pNade = SGTC_ShootGasGrenade( m_pPlayer->pev, vecSrc, vecThrow,
			time, pszWorldModel, gasType, flDmg );
		if( pNade )
			pNade->m_iThrowerTeam = (int)m_pPlayer->pev->team;
#endif

		SendWeaponAnim( SGTC_LAUNCH );
		m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
		m_flGasStartThrow = 0.0f;
		m_flNextPrimaryAttack = GetNextAttackDelay( 0.5f );
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.5f;
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]--;

		if( !m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] )
			m_flTimeWeaponIdle = m_flNextSecondaryAttack = m_flNextPrimaryAttack =
				GetNextAttackDelay( 0.5f );
		return;
	}

	if( m_flGasReleaseThrow > 0.0f )
	{
		m_flGasStartThrow = 0.0f;
		if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] )
		{
			SendWeaponAnim( SGTC_DRAW );
			m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
				UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
		}
		else
			RetireWeapon();
		m_flGasReleaseThrow = -1.0f;
		return;
	}

	ResetEmptySound();
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] )
	{
		SendWeaponAnim( SGTC_IDLE );
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
			UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
	}
}
