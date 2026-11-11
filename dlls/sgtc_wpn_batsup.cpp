// sgtc_wpn_batsup.cpp — StargateTC CBatonSupplice (split from sgtc_weapons.cpp; NEW file).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
#include "sgtc_weapons.h"
#include "sgtc_util.h"
// buttons.cpp — no header decl either.
extern void DoSpark( entvars_t *pev, const Vector &location );

// ===========================================================================
// CBatonSupplice - "Baton de supplice" melee. Analogue: crowbar.cpp.
// Events: events/batsup.sc (first swing) / events/batsup2.sc (follow-up).
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_batsup, CBatonSupplice );

// Save/restore (#99). Not saved: m_trHit (TraceResult: edict pointer + per-swing scratch) and
// m_usFire/m_usFire2 (event handles, rebuilt by Precache()).
TYPEDESCRIPTION CBatonSupplice::m_SaveData[] =
{
	DEFINE_FIELD( CBatonSupplice, m_bSgtcSwingLatch, FIELD_INTEGER ),
};

IMPLEMENT_SAVERESTORE( CBatonSupplice, CBasePlayerWeapon )

void CBatonSupplice::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_batsup.mdl" );
	m_iId = SGTC_WPN_BATSUP;
	m_iClip = -1;
	m_bSgtcSwingLatch = FALSE;	// ref @0xb7339: +0x23c := 0
	FallInit();
}
void CBatonSupplice::Precache( void )
{
	PRECACHE_MODEL( "models/v_batsup.mdl" );
	PRECACHE_MODEL( "models/w_batsup.mdl" );
	PRECACHE_MODEL( "models/p_batsup.mdl" );

	PRECACHE_SOUND( "weapons/coujafcorps.wav" );
	PRECACHE_SOUND( "weapons/hitbody1.wav" );
	PRECACHE_SOUND( "weapons/hitbody2.wav" );
	PRECACHE_SOUND( "weapons/hitbody3.wav" );
	PRECACHE_SOUND( "weapons/swing1.wav" );
	PRECACHE_SOUND( "weapons/swing2.wav" );
	PRECACHE_SOUND( "weapons/swing3.wav" );

	m_usFire = PRECACHE_EVENT( 1, "events/batsup.sc" );
	m_usFire2 = PRECACHE_EVENT( 1, "events/batsup2.sc" );
}

SGTC_GETITEM( CBatonSupplice, NULL, -1, WEAPON_NOCLIP, 0, 2, SGTC_WPN_BATSUP, 0, 0 )
SGTC_ADD_TO_PLAYER( CBatonSupplice )

BOOL CBatonSupplice::Deploy( void )
{
	m_bSgtcSwingLatch = FALSE;
	return SGTC_DEPLOY( 2, "models/v_batsup.mdl", "models/p_batsup.mdl", 2, "hive" );
}

void CBatonSupplice::Holster( int skiplocal )	// ref @0xb8480
{
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;
	m_bSgtcSwingLatch = FALSE;	// ref @0xb84b0: +0x23c := 0
	CBasePlayerWeapon::Holster( skiplocal );
}

void CBatonSupplice::PrimaryAttack( void )	// ref @0xb8374
{
	if( m_pPlayer->m_flGateTravel > gpGlobals->time )
		return;
	if( m_pPlayer->IsObserver() )
		return;

	// ref: a miss returns 0 -> direct SwingAgain() + think bump + SnagInvis.
	// (No SetThink here; the ref only writes pev->nextthink, a benign no-op.)
	// ref @0xb83d1: SwingAgain only when the rechamber window is past.
	if( !Swing( 1 ) && UTIL_WeaponTimeBase() > m_flNextPrimaryAttack )
	{
		SwingAgain();
		pev->nextthink = gpGlobals->time + 0.1f;
		m_pPlayer->SnagInvis();
	}

	// ref @0xb8464: unconditional idle bump, always runs (even on the return paths above).
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.75f;
}

void CBatonSupplice::SwingAgain( void )
{
	Swing( 0 );
}

// ref @0xb7700 — crowbar-shaped but with SGTC divergences:
//  - event choice and anim 4/5 key off the persistent latch byte m_bSgtcSwingLatch
//    (ref this+0x23c; set every swing, cleared Spawn/Holster/WeaponIdle), and the
//    follow-up event is m_usFire2 (events/batsup2.sc).
//  - miss with fFirst=0 does a DoSpark at max reach then still falls into the hit
//    block below in the ref; misses return 0 so PrimaryAttack chains SwingAgain.
//  - damage is kSgtcSkillMeleeDmg (54.0f; ref gSkillData+0x128, an MP hardcode
//    not a stock field), halved on follow-up swings inside 1.0s of
//    m_flNextPrimaryAttack unless multiplayer.
//  - hitting a non-teammate player drains 30 stamina and re-sends the HUD byte.
int CBatonSupplice::Swing( int fFirst )
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

	// ref @0xb7979: first swing fires m_usFire, latched follow-ups fire m_usFire2.
	PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(),
		m_bSgtcSwingLatch ? m_usFire2 : m_usFire,
		0.0f, g_vecZero, g_vecZero, 0, 0, 0, 0, 0, 0 );

	if( tr.flFraction >= 1.0f )
	{
		// miss
		if( !fFirst )
		{
			m_bSgtcSwingLatch = TRUE;
			return fDidHit;
		}

		m_flNextPrimaryAttack = GetNextAttackDelay( 0.5 );
		m_pPlayer->SetAnimation( PLAYER_ATTACK1 );

		// ref @0xb7ae4: miss anim picks 5 once latched, 4 first time.
		SendWeaponAnim( m_bSgtcSwingLatch ? 5 : 4, 1, 0 );

		// ref @0xb7b29: DoSpark at the calculated max reach (vecSrc+fwd*32),
		// not the trace end.
		Vector vecSpark = vecSrc + gpGlobals->v_forward * 32.0f;
		DoSpark( pev, vecSpark );

		m_bSgtcSwingLatch = TRUE;

		// ref miss legs (0xb7b81/0xb7b8b) return before the hit tail:
		// a first-swing miss must not run Smack/volume/0.25/nextthink.
		return fDidHit;
	}
	else
	{
		// hit — ref @0xb7b90: spark at the PREVIOUS swing's stored endpoint.
		DoSpark( pev, m_trHit.vecEndPos );

		// ref @0xb7baf: hit anim also keys off the latch (4 then 5).
		SendWeaponAnim( m_bSgtcSwingLatch ? 5 : 4, 1, 0 );
		m_pPlayer->SetAnimation( PLAYER_ATTACK1 );

		m_bSgtcSwingLatch = TRUE;	// ref @0xb7c0b
		fDidHit = TRUE;				// ref @0xb7c12

		CBaseEntity *pEntity = CBaseEntity::Instance( tr.pHit );

		// ref @0xb7c2e: unconditional ClearMultiDamage before the hit resolves.
		ClearMultiDamage();

		// ref @0xb7c33: IsPlayer gate selects the stamina-drain victim.
		CBasePlayer *pVictim = NULL;
		if( pEntity->IsPlayer() )
			pVictim = static_cast<CBasePlayer *>( pEntity );

		// ref: full damage if the swing is more than 1.0s past the latched
		// m_flNextPrimaryAttack, or always in multiplayer.
		if( UTIL_WeaponTimeBase() >= m_flNextPrimaryAttack + 1.0f ||
			( g_pGameRules && g_pGameRules->IsMultiplayer() ) )
		{
			// ref @0xb7cc8: full damage swing.
			pEntity->TraceAttack( m_pPlayer->pev, kSgtcSkillMeleeDmg,
				gpGlobals->v_forward, &tr, DMG_CLUB );
		}
		else
		{
			// ref @0xb7db8: follow-up swing does half damage (54/2 = 27).
			pEntity->TraceAttack( m_pPlayer->pev, kSgtcSkillMeleeDmg * 0.5f,
				gpGlobals->v_forward, &tr, DMG_CLUB );
		}

		// ref @0xb7d3d: hitting a non-teammate player drains 30 stamina.
		if( pVictim && g_pGameRules &&
			g_pGameRules->PlayerRelationship( pEntity, m_pPlayer ) == GR_NOTTEAMMATE )
		{
			pVictim->m_iStamina -= 30;	// ref @0xb7d90: +0x43c
			pVictim->SGTC_UpdateStamina();
		}

		ApplyMultiDamage( m_pPlayer->pev, m_pPlayer->pev );

		if( pEntity && pEntity->Classify() != CLASS_NONE &&
			pEntity->Classify() != CLASS_MACHINE )
		{
			// ref: hitbody sounds on CHAN_ITEM, vol 1.0, pitch 98+RANDOM_LONG(0,3).
			switch( RANDOM_LONG( 1, 3 ) )
			{
			case 1:
				EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM,
					"weapons/hitbody1.wav", 1.0f, ATTN_NORM, 0,
					98 + RANDOM_LONG( 0, 3 ) );
				break;
			case 2:
				EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM,
					"weapons/hitbody2.wav", 1.0f, ATTN_NORM, 0,
					98 + RANDOM_LONG( 0, 3 ) );
				break;
			case 3:
				EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM,
					"weapons/hitbody3.wav", 1.0f, ATTN_NORM, 0,
					98 + RANDOM_LONG( 0, 3 ) );
				break;
			}

			m_pPlayer->m_iWeaponVolume = 128;	// ref @0xb809d: m_pPlayer+0xaac

			if( !pEntity->IsAlive() )
				return fDidHit;	// ref @0xb80c7: dead target early-outs before the tail

			flVol = 0.1f;
			fHitWorld = FALSE;
		}

		if( fHitWorld )
		{
			// ref @0xb80f0: texture strike + wall hit sound. Bullet type literal 12
			// (not BULLET_PLAYER_CROWBAR) per the disassembly.
			float fvolbar = TEXTURETYPE_PlaySound( &tr, vecSrc,
				vecSrc + ( vecEnd - vecSrc ) * 2.0f, 12 );

			if( g_pGameRules && g_pGameRules->IsMultiplayer() )
				fvolbar = 1.0f;

			switch( RANDOM_LONG( 0, 1 ) )
			{
			case 0:
				EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM,
					"weapons/cbar_hit1.wav", fvolbar, ATTN_NORM, 0,
					98 + RANDOM_LONG( 0, 3 ) );
				break;
			default:
				EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM,
					"weapons/cbar_hit2.wav", fvolbar, ATTN_NORM, 0,
					98 + RANDOM_LONG( 0, 3 ) );
				break;
			}
		}

		m_trHit = tr;	// ref @0xb828a: +0x200
	}

	// ref @0xb82a3 tail (hit path only — both miss legs return before this).
	m_pPlayer->m_iWeaponVolume = (int)( flVol * 512.0f );
	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.25f;
	Smack();
	pev->nextthink = UTIL_WeaponTimeBase() + 0.2f;	// ref @0xb8315

	return fDidHit;
}

void CBatonSupplice::Smack( void )	// ref @0xb76d2
{
	DecalGunshot( &m_trHit, 12 );
}

void CBatonSupplice::WeaponIdle( void )	// ref @0xb75a4
{
	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;

	int iAnim;
	if( m_bSgtcSwingLatch )
	{
		// ref @0xb75ea: consume the swing latch, push the next allowed swing out.
		m_bSgtcSwingLatch = FALSE;
		m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.5f;
	}

	if( UTIL_SharedRandomFloat( m_pPlayer->random_seed, 0.0f, 1.0f ) <= 0.75f )
	{
		iAnim = 3;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
			UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10.0f, 15.0f );
	}
	else
	{
		iAnim = 0;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.5f;
	}

	SendWeaponAnim( iAnim, 1, 0 );
}
SGTC_NO_RELOAD( CBatonSupplice )
SGTC_NO_SECONDARY( CBatonSupplice )
