// sgtc_wpn_couteau.cpp — StargateTC CCouteau (split from sgtc_weapons.cpp; NEW file).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "gamerules.h"
#include "soundent.h"
#include "sgtc_weapons.h"
#include <cstddef>

// ref rodata @0x1ccd20 — Deploy/Holster seed copied to +0x208 then +0x204
static const float kCouteauPadDeploySeed = 40.06666946411133f;
// ref rodata @0x1cd9d8 — inactive / cleared sentinel (flds compare in PA/SA)
static const float kCouteauPadIdle = 0.0f;
// ref PA epilogue rodata (@0xd7f73 / @0xd7f8f / @0xd809e / @0xd80bf / @0xd8124)
static const float kCouteauPaPrepWpnDelay = 0.5f;		// float @0x1cd9dc
static const float kCouteauPaPrepPlayerDelay = 0.4f;	// double @0x1cd9e0
static const float kCouteauPaSwingPlayerDelay = 0.3f;	// double @0x1cd9e8
static const float kCouteauPaSwingWpnDelay = 0.4f;	// double @0x1cd9e0 (post-swing)
static const float kCouteauPaClass4Delay = 0.15f;	// double @0x1cd9f0 — portrait class 4, team 2
// ref SA rodata (@0xd6c16 / @0xd6c32 / @0xd6d39 / @0xd6e78) — same 0.5/0.4 prep as PA; swing gate 0.7
static const float kCouteauSaSwingGateDelay = 0.7f;	// double @0x1cd968
static const float kCouteauIdlePlayerBump = 0.1f;	// double @0x1cd988 — WeaponIdle tail

#if defined( __GNUC__ ) && defined( __i386__ ) && !defined( CLIENT_DLL )
SGTC_STATIC_ASSERT( offsetof( CCouteau, m_iSgtcMeleeLatch ) == 0x200,
	"CCouteau m_iSgtcMeleeLatch must sit at ref +0x200" );
SGTC_STATIC_ASSERT( offsetof( CCouteau, m_flSgtcThrowGate204 ) == 0x204,
	"CCouteau m_flSgtcThrowGate204 must sit at ref +0x204" );
SGTC_STATIC_ASSERT( offsetof( CCouteau, m_flSgtcThrowPad208 ) == 0x208,
	"CCouteau m_flSgtcThrowPad208 must sit at ref +0x208" );
#endif

// ===========================================================================
// CCouteau - "Couteau" throwing knife. Analogue: crowbar.cpp + thrown item.
// Event: events/couteau.sc. Secondary throws the knife (ThrowIt).
// ===========================================================================

LINK_ENTITY_TO_CLASS( weapon_couteau, CCouteau );

// Save/restore (#99). Not saved: m_trHit (TraceResult), m_usFire (event handle), m_iSwing (unused
// here) and the layout padding.
TYPEDESCRIPTION CCouteau::m_SaveData[] =
{
	DEFINE_FIELD( CCouteau, m_iSgtcMeleeLatch, FIELD_INTEGER ),
	DEFINE_FIELD( CCouteau, m_flSgtcThrowGate204, FIELD_TIME ),
	DEFINE_FIELD( CCouteau, m_flSgtcThrowPad208, FIELD_TIME ),
};

IMPLEMENT_SAVERESTORE( CCouteau, CBasePlayerWeapon )

void CCouteau::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_couteau.mdl" );
	m_iId = SGTC_WPN_COUTEAU;
	m_iClip = -1;
	FallInit();
}

void CCouteau::Precache( void )
{
	PRECACHE_MODEL( "models/v_couteau.mdl" );
	PRECACHE_MODEL( "models/w_couteau.mdl" );
	PRECACHE_MODEL( "models/p_couteau.mdl" );

	PRECACHE_SOUND( "weapons/cbar_hit1.wav" );
	PRECACHE_SOUND( "weapons/cbar_hit2.wav" );
	PRECACHE_SOUND( "weapons/hitbody1.wav" );
	PRECACHE_SOUND( "weapons/hitbody2.wav" );
	PRECACHE_SOUND( "weapons/hitbody3.wav" );
	PRECACHE_SOUND( "weapons/swing1.wav" );
	PRECACHE_SOUND( "weapons/swing2.wav" );
	PRECACHE_SOUND( "weapons/swing3.wav" );

	m_usFire = PRECACHE_EVENT( 1, "events/couteau.sc" );
}

SGTC_GETITEM( CCouteau, NULL, 3, 0, 0, 1, SGTC_WPN_COUTEAU, 0, 0 )
SGTC_ADD_TO_PLAYER( CCouteau )

BOOL CCouteau::Deploy( void )
{
	m_flSgtcThrowPad208 = kCouteauPadDeploySeed;
	m_flSgtcThrowGate204 = m_flSgtcThrowPad208;	// ref @0xd62c3 copies +0x208 → +0x204
	if( m_pPlayer->m_iSgtcCouteauThrowLatch == 2 )	// ref player+0x4f8
		m_pPlayer->m_iSgtcCouteauThrowLatch = 0;
	m_iSgtcMeleeLatch = 0;	// ref Deploy @0xd6305: movl $0, this+0x200
	return SGTC_DEPLOY( 2, "models/v_couteau.mdl", "models/p_couteau.mdl", SGTC_DRAW, "crowbar" );
}

void CCouteau::Holster( int skiplocal )
{
	m_flSgtcThrowPad208 = kCouteauPadDeploySeed;
	m_flSgtcThrowGate204 = m_flSgtcThrowPad208;	// ref Holster @0xd6360..0xd6372
	CBasePlayerWeapon::Holster( skiplocal );
}

void CCouteau::PrimaryAttack( void )
{
	if( gpGlobals->time < m_pPlayer->m_flGateTravel )
		return;
	if( m_pPlayer->IsObserver() )
		return;
	if( m_pPlayer->m_bShieldLarge )
		return;
	if( m_pPlayer->m_bHurtInvertFx )
		return;

	// ref @0xd7e48: +0x208 at idle (0.0) may clear player+0x4f8 when it holds 2.
	if( m_flSgtcThrowPad208 == kCouteauPadIdle && m_pPlayer->m_iSgtcCouteauThrowLatch == 2 )
		m_pPlayer->m_iSgtcCouteauThrowLatch = 0;

	// ref @0xd7e8d: latch≠0 → optional Throwit (@0xd7eed); no coujaf-style latch clear here.
	if( m_iSgtcMeleeLatch && m_flSgtcThrowGate204 != kCouteauPadIdle
		&& m_flSgtcThrowGate204 > gpGlobals->time )
	{
		if( m_pPlayer->m_iSgtcCouteauThrowLatch == 0 )	// ref player+0x4f8 == 0
			ThrowIt();
	}

	BOOL bPrepSwing = FALSE;	// ref local byte @-0x9(%ebp)

	// ref @0xd7efc..0xd7fd2: +0x208 past due → anim(2) + timer/latch seed (throw recovery / deploy pad)
	if( m_flSgtcThrowPad208 != kCouteauPadIdle )
	{
		if( m_flSgtcThrowPad208 <= gpGlobals->time
			&& m_pPlayer->m_iSgtcCouteauThrowLatch == 0 )
		{
			SendWeaponAnim( 2, 1, 0 );	// ref vcall+0x160 @0xd7f66
			m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + kCouteauPaPrepWpnDelay;
			m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + kCouteauPaPrepPlayerDelay;
			m_flSgtcThrowPad208 = kCouteauPadIdle;
			m_pPlayer->m_iSgtcCouteauThrowLatch = 1;
			m_iSgtcMeleeLatch = 0;
			bPrepSwing = TRUE;
		}
	}

	// ref @0xd7fd6: +0x208 must be idle before swing path
	if( m_flSgtcThrowPad208 != kCouteauPadIdle )
		return;

	if( bPrepSwing )
		m_iSgtcMeleeLatch = 0;	// ref @0xd8000 when prep flag set

	BOOL bDoSwing = FALSE;
	if( m_flSgtcThrowGate204 == kCouteauPadIdle
		&& m_flNextPrimaryAttack <= UTIL_WeaponTimeBase() )
	{
		bDoSwing = TRUE;	// ref @0xd8030..0xd8068 (+0x204==0, weapon timer ready)
	}

	if( !bDoSwing )
		return;

#ifndef CLIENT_DLL
	m_pPlayer->SnagInvis();	// ref @0xd807b before Swing(1)
#endif

	if( !Swing( 1 ) )
	{
#ifndef CLIENT_DLL
		SetThink( &CCouteau::SwingAgain );
		pev->nextthink = gpGlobals->time + 0.1;
#endif
	}

	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + kCouteauPaSwingPlayerDelay;
	m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + kCouteauPaSwingWpnDelay;

	// ref @0xd80d7..0xd8154: Torri class 4 (+0xa40==4) with talent gate on +0x494
	if( m_pPlayer->m_flTalentCooldown <= gpGlobals->time
		&& m_pPlayer->pev->team == SGTC_ENTTEAM_TORRI
		&& m_pPlayer->m_iSgtcPortraitShown == SGTC_CLASS_TEALC )
	{
		m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + kCouteauPaClass4Delay;
		m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + kCouteauPaClass4Delay;
	}
}

void CCouteau::SwingAgain( void )
{
	Swing( 0 );
}

int CCouteau::Swing( int fFirst )
{
	int fDidHit = FALSE;
	TraceResult tr;

	UTIL_MakeVectors( m_pPlayer->pev->v_angle );
	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecEnd = vecSrc + gpGlobals->v_forward * 32;

	UTIL_TraceLine( vecSrc, vecEnd, dont_ignore_monsters, ENT( m_pPlayer->pev ), &tr );

	if( fFirst )
	{
		// build-21: always play the swing + set the cooldown, even when the
		// trace hits nothing ("stab air"); damage is still applied only in the
		// hit branch below.
		PLAYBACK_EVENT_FULL( SGTC_EVENT_FLAGS, m_pPlayer->edict(), m_usFire,
			0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0, 0, 0, 0, 0, 0 );
		m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
		m_flNextPrimaryAttack = GetNextAttackDelay( 0.4 );
	}

	if( tr.flFraction >= 1.0 )
	{
	}
	else
	{
		m_pPlayer->SetAnimation( PLAYER_ATTACK1 );

#ifndef CLIENT_DLL
		fDidHit = TRUE;
		CBaseEntity *pEntity = CBaseEntity::Instance( tr.pHit );

		if( pEntity )
		{
			ClearMultiDamage();

			// ref @0xd76de: talent addend (Torri class 4 (tealc), cooldown live).
			int iTalentAdd = 0;
			if( gpGlobals->time < m_pPlayer->m_flTalentCooldown &&
				m_pPlayer->pev->team == SGTC_ENTTEAM_TORRI &&
				m_pPlayer->m_iSgtcPortraitShown == SGTC_CLASS_TEALC )
				iTalentAdd = 0x46;

			// ref @0xd77dd: full swing = 54+talent; @0xd7841: SP follow-up =
			// (54+talent)/2 via the same +1.0s/IsMultiplayer gate as batsup.
			const float flGlobal = kSgtcSkillMeleeDmg + (float)iTalentAdd;
			const float flDamage = ( ( m_flNextPrimaryAttack + 1.0f < UTIL_WeaponTimeBase() ) ||
				( g_pGameRules && g_pGameRules->IsMultiplayer() ) )
				? flGlobal : flGlobal * 0.5f;

			pEntity->TraceAttack( m_pPlayer->pev, flDamage,
				gpGlobals->v_forward, &tr, DMG_CLUB );
			ApplyMultiDamage( m_pPlayer->pev, m_pPlayer->pev );
			EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_ITEM, "weapons/hitbody3.wav", 1, ATTN_NORM );
		}
#endif

		if( fFirst )
			m_flNextPrimaryAttack = GetNextAttackDelay( 0.4 );
	}

	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
		UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );

	return fDidHit;
}

void CCouteau::SecondaryAttack( void )
{
	if( gpGlobals->time < m_pPlayer->m_flGateTravel )
		return;
	if( m_pPlayer->IsObserver() )
		return;

	if( m_flSgtcThrowPad208 == kCouteauPadIdle && m_pPlayer->m_iSgtcCouteauThrowLatch == 2 )
		m_pPlayer->m_iSgtcCouteauThrowLatch = 0;	// ref SA @0xd6b0a

	BOOL bThrowGateFired = FALSE;	// ref jmp @0xd6b9b skips idle anim when set

	// ref @0xd6b31..0xd6b9b: armed +0x204 past due → Throwit
	if( m_flSgtcThrowGate204 != kCouteauPadIdle )
	{
		if( m_flSgtcThrowGate204 <= gpGlobals->time
			&& m_pPlayer->m_iSgtcCouteauThrowLatch == 0 )
		{
			ThrowIt();
			bThrowGateFired = TRUE;
		}
	}

	if( !bThrowGateFired )
	{
		// ref @0xd6ba0..0xd6c72: +0x208 epilogue (matches PA)
		if( m_flSgtcThrowPad208 != kCouteauPadIdle )
		{
			if( m_flSgtcThrowPad208 <= gpGlobals->time
				&& m_pPlayer->m_iSgtcCouteauThrowLatch == 0 )
			{
				SendWeaponAnim( 2, 1, 0 );
				m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + kCouteauPaPrepWpnDelay;
				m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + kCouteauPaPrepPlayerDelay;
				m_flSgtcThrowPad208 = kCouteauPadIdle;
				m_pPlayer->m_iSgtcCouteauThrowLatch = 1;
				m_iSgtcMeleeLatch = 0;
			}
		}

		// ref @0xd6c75..0xd6cb1: weapon timer not ready → anim(0)
		if( m_flNextPrimaryAttack > UTIL_WeaponTimeBase() )
			SendWeaponAnim( 0, 1, 0 );
	}

	// ref @0xd6cb4: still armed +0x208 or player latch → done
	if( m_flSgtcThrowPad208 != kCouteauPadIdle )
		return;
	if( m_pPlayer->m_iSgtcCouteauThrowLatch != 0 )
		return;

	// ref @0xd6cec..0xd6eae: melee when +0x204 idle
	if( m_flSgtcThrowGate204 == kCouteauPadIdle )
	{
		m_flSgtcThrowGate204 = gpGlobals->time + kCouteauSaSwingGateDelay;
		m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
		SendWeaponAnim( 4, 1, 0 );

#ifndef CLIENT_DLL
		(void)Swing( 1 );
		EMIT_SOUND( ENT( m_pPlayer->pev ), CHAN_WEAPON, "weapons/swing1.wav", 1, ATTN_NORM );
#endif

		m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + kCouteauSaSwingGateDelay;
		m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + kCouteauSaSwingGateDelay;
	}
}

void CCouteau::ThrowIt( void )	// ref @0xd65b2
{
#ifndef CLIENT_DLL
	// ref @0xd65cd: latch already armed → return; else movl $1, this+0x200 + SnagInvis.
	if( m_iSgtcMeleeLatch )
		return;
	m_iSgtcMeleeLatch = 1;
	m_pPlayer->SnagInvis();

	UTIL_MakeVectors( m_pPlayer->pev->v_angle );	// ref ThrowIt @0xd6610: pev+0x74 v_angle, not angles

	Vector vecSrc = m_pPlayer->GetGunPosition();

	// ref: instant trace GetGunPosition → +v_forward*1100, then drops a
	// weapon_couteau pickup at the hit point (NO flying bolt entity).
	TraceResult tr;
	UTIL_TraceLine( vecSrc, vecSrc + gpGlobals->v_forward * 1100.0f,
		ignore_monsters, ENT( m_pPlayer->pev ), &tr );
	CBaseEntity *pHit = CBaseEntity::Instance( tr.pHit );

	CBaseEntity *pKnife = CBaseEntity::Create( "weapon_couteau", tr.vecEndPos,
		vecSrc, m_pPlayer->edict() );	// ref: origin=endPos, angles arg=vecSrc, owner=player
	if( pKnife )
	{
		pKnife->pev->punchangle = m_pPlayer->pev->punchangle;	// ref copies pev+0x50..+0x58
		// build-11: the ref literal folds in the item's previous (physics) velocity,
		// which can leave a dropped knife over the SV_Phys velocity warning limit.
		// A thrown knife only needs the 40 u/s toss; ignore inherited velocity and
		// cap it defensively.
		Vector vecKnifeVel = gpGlobals->v_forward * 40.0f;
		if( vecKnifeVel.Length() > 300.0f )
			vecKnifeVel = vecKnifeVel.Normalize() * 300.0f;
		pKnife->pev->velocity = vecKnifeVel;
	}

	const BOOL bHitPlayer = ( pHit && pHit->IsPlayer() && pHit->IsAlive() );
	if( bHitPlayer )
	{
		RadiusDamage( tr.vecEndPos, m_pPlayer->pev, pHit->pev, 30.0f, 30.0f,
			CLASS_NONE, DMG_CLUB );	// ref RadiusDamage(30,30,0,0x80)
		switch( RANDOM_LONG( 0, 2 ) )
		{
		case 0: EMIT_SOUND( ENT( pHit->pev ), CHAN_ITEM, "weapons/hitbody1.wav", 1.0f, 0.8f ); break; // ref Throwit chan 3
		case 1: EMIT_SOUND( ENT( pHit->pev ), CHAN_ITEM, "weapons/hitbody2.wav", 1.0f, 0.8f ); break;
		default: EMIT_SOUND( ENT( pHit->pev ), CHAN_ITEM, "weapons/hitbody3.wav", 1.0f, 0.8f ); break;
		}
	}
	else if( pKnife )
	{
		// ref miss path: wall ping on the dropped knife, pitch 98-101.
		if( RANDOM_LONG( 0, 1 ) )
			EMIT_SOUND_DYN( ENT( pKnife->pev ), CHAN_ITEM, "weapons/cbar_hit1.wav", 1.0f, 0.8f, 0, RANDOM_LONG( 0, 3 ) + 98 );
		else
			EMIT_SOUND_DYN( ENT( pKnife->pev ), CHAN_ITEM, "weapons/cbar_hit2.wav", 1.0f, 0.8f, 0, RANDOM_LONG( 0, 3 ) + 98 );
	}

	m_flSgtcThrowGate204 = 0.0f;			// ref mov $0 → +0x204 @0xd6a7f
	m_flSgtcThrowPad208 = gpGlobals->time + 0.5f;	// ref fstp +0x208 (time+0.5)
#endif
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
}

SGTC_NO_RELOAD( CCouteau )

void CCouteau::WeaponIdle( void )	// ref @0xd6eea
{
	if( m_flSgtcThrowPad208 == kCouteauPadIdle && m_pPlayer->m_iSgtcCouteauThrowLatch == 2 )
		m_pPlayer->m_iSgtcCouteauThrowLatch = 0;	// ref @0xd6f1c

	BOOL bSkipStockIdle = FALSE;

	if( m_flSgtcThrowGate204 != kCouteauPadIdle )
	{
		if( m_flSgtcThrowGate204 <= gpGlobals->time
			&& m_pPlayer->m_iSgtcCouteauThrowLatch == 0 )
		{
			ThrowIt();	// ref @0xd6fa5
			bSkipStockIdle = TRUE;
		}
	}

	if( !bSkipStockIdle )
	{
		if( m_flSgtcThrowPad208 != kCouteauPadIdle )
		{
			if( m_flSgtcThrowPad208 <= gpGlobals->time
				&& m_pPlayer->m_iSgtcCouteauThrowLatch == 0 )
			{
				SendWeaponAnim( 2, 1, 0 );	// ref @0xd7017
				m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + kCouteauPaPrepWpnDelay;
				m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + kCouteauPaPrepPlayerDelay;
				m_flSgtcThrowPad208 = kCouteauPadIdle;
				m_pPlayer->m_iSgtcCouteauThrowLatch = 1;
				m_iSgtcMeleeLatch = 0;
			}
		}

		if( m_flTimeWeaponIdle <= UTIL_WeaponTimeBase() )
			SendWeaponAnim( 0, 1, 0 );	// ref @0xd70ba
	}

	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + kCouteauIdlePlayerBump;
}

