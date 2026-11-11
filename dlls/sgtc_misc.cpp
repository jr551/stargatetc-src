// sgtc_misc.cpp — remaining SGTC point entities (ref item_NVG @0xfcb84,
// special_teleport_in @0x195e1e / CSpecialTeleport @0x1955a6, zat_proj @0x1b6cae).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "satchel.h"
#include "gamerules.h"
#include "items.h"
#include "sgtc_rules.h"
#include "sgtc_c4fire.h"
#include "sgtc_weapons.h"
#include "sgtc_misc.h"
#include "sgtc_lanceproj.h"
#include "sgtc_diag.h"
#include "sgtc_shield.h"
#include "effects.h"
#include "sgtc_util.h"

extern int gmsgFlashlight;

static void SGTC_ZatImpactDLight( const Vector &origin )
{
	MESSAGE_BEGIN( MSG_PAS, SVC_TEMPENTITY, origin );
	WRITE_BYTE( 27 );	// ref TE 0x1b
	WRITE_COORD( origin.x );
	WRITE_COORD( origin.y );
	WRITE_COORD( origin.z );
	WRITE_BYTE( 10 );
	WRITE_BYTE( 0x19 );
	WRITE_BYTE( 10 );
	WRITE_BYTE( 255 );
	WRITE_BYTE( 12 );
	WRITE_BYTE( 0 );
	MESSAGE_END();
}

static void SGTC_ZatImpactFx( const Vector &origin )
{
	CSprite *pSpr = CSprite::SpriteCreate( "sprites/zatimpact.spr", origin, TRUE );
	if( pSpr )
	{
		pSpr->AnimateAndDie( 15.0f );
		pSpr->SetTransparency( kRenderGlow, 25, 10, 255, 255, 14 );	// ref mode 3 @zatproj.c
		// ref CZatProj zatimpact emits ×3: CHAN_BODY(4), pitch RANDOM_LONG(-5,5)+100.
		EMIT_SOUND_DYN( ENT( pSpr->pev ), CHAN_BODY, "weapons/zatimpact.wav",
			0.7f, 0.8f, 0, RANDOM_LONG( -5, 5 ) + 100 );
	}
	SGTC_ZatImpactDLight( origin );
	// build-21: small fire burning at the hit point for ~3.5 s (zatarc request).
	SGTC_FirePatch_Spawn( origin, 3.5f, 7.0f );
}

// ref CZatProj::ProjTouch @0x1c4e2a — oldzat beam follow-up (TE 0, 4x).
static void SGTC_ZatProjTouchBeams( CZatProj *pProj )
{
	for( int i = 1; i < 5; i++ )
	{
		MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
		WRITE_BYTE( 0 );
		WRITE_COORD( pProj->pev->origin.x );
		WRITE_COORD( pProj->pev->origin.y );
		WRITE_COORD( pProj->pev->origin.z );
		WRITE_COORD( pProj->m_vecBeamEnd.x );
		WRITE_COORD( pProj->m_vecBeamEnd.y );
		WRITE_COORD( pProj->m_vecBeamEnd.z );
		WRITE_SHORT( pProj->m_iZatWaveSprite );
		WRITE_BYTE( 10 );
		WRITE_BYTE( 10 );
		WRITE_BYTE( 1 );
		WRITE_BYTE( 50 );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 25 );
		WRITE_BYTE( 10 );
		WRITE_BYTE( 255 );
		WRITE_BYTE( 255 );
		WRITE_BYTE( 0 );
		MESSAGE_END();
	}
}

// ---------------------------------------------------------------------------
// item_NVG — ref CItemNVG::MyTouch @0xfda98 sets player+0xe1c; we use iuser4.
// ---------------------------------------------------------------------------
class CItemNVG : public CItem
{
public:
	void Spawn( void );
	void Precache( void );
	BOOL MyTouch( CBasePlayer *pPlayer );
};

void CItemNVG::Spawn( void )
{
	Precache();
	SET_MODEL( ENT( pev ), "models/w_9mmclip.mdl" );
	CItem::Spawn();
}

void CItemNVG::Precache( void )
{
	PRECACHE_MODEL( "models/w_9mmclip.mdl" );
	PRECACHE_SOUND( "items/gunpickup2.wav" );
}

BOOL CItemNVG::MyTouch( CBasePlayer *pPlayer )
{
	if( pPlayer->m_iHasNVG )	// ref +0xe1c
		return FALSE;
	pPlayer->m_iHasNVG = 1;
	if( gmsgFlashlight )
	{
		MESSAGE_BEGIN( MSG_ONE, gmsgFlashlight, NULL, pPlayer->edict() );
			WRITE_BYTE( 1 );
			WRITE_COORD( 1.0f );
		MESSAGE_END();
	}
	EMIT_SOUND( ENT( pPlayer->pev ), CHAN_ITEM, "items/gunpickup2.wav", 1.0f, ATTN_NORM );
	return TRUE;
}

LINK_ENTITY_TO_CLASS( item_NVG, CItemNVG );

// ---------------------------------------------------------------------------
// special_teleport_in — ref CSpecialTeleport (Spawn @0x1955a6, TeleportTouch).
// ---------------------------------------------------------------------------
class CSpecialTeleport : public CBaseToggle
{
public:
	void Spawn( void );
	void KeyValue( KeyValueData *pkvd );
	void EXPORT TeleportTouch( CBaseEntity *pOther );
	void EXPORT TeleportThink( void );
	void EXPORT SpecialUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	void Reset( void );

	char m_bActive;			// ref +0x240
	string_t m_iszSourceClass;	// ref +0x244 — cached pev->classname of dest
	int m_iReverse;			// ref +0x248 — FGD "reverse"
	int m_iTeleportPlayers;	// ref +0x24c — FGD "players"
	EHANDLE m_hMarker;		// ref +0x250 — info marker from message
	EHANDLE m_hDest;		// ref +0x254 — teleport destination
};

void CSpecialTeleport::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "reverse" ) )
	{
		m_iReverse = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "players" ) )
	{
		m_iTeleportPlayers = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseToggle::KeyValue( pkvd );
}

void CSpecialTeleport::Spawn( void )
{
	m_hMarker = NULL;
	m_hDest = NULL;
	m_iszSourceClass = iStringNull;
	m_iReverse = 0;
	m_iTeleportPlayers = 0;
	m_bActive = 1;

	SGTC_InitTrigger( this );
	SetTouch( &CSpecialTeleport::TeleportTouch );
	SetThink( &CSpecialTeleport::TeleportThink );
	SetUse( &CSpecialTeleport::SpecialUse );
	pev->nextthink = 0;
	pev->takedamage = DAMAGE_YES;
	pev->health = 100.0f;	// ref CSpecialTeleport::Spawn @0x195647 rodata 100

	TeleportThink();
	pev->nextthink = gpGlobals->time + 1.5f;	// ref fldl @0x195671

	if( pev->spawnflags & 2 )	// FGD spawnflag 2 "Start On" inverted
		m_bActive = 0;
}

static CBaseEntity *SGTC_FindTargetnameChain( const char *pszName )
{
	CBaseEntity *pLast = NULL;
	for( CBaseEntity *pEnt = NULL; ( pEnt = UTIL_FindEntityByTargetname( pEnt, pszName ) ) != NULL; )
		pLast = pEnt;
	return pLast;
}

// ref Endo_InverseVelocity @0x1aba14 / Endo_InverseAngle @0x1aba98 (mode 2 = xy negate).
static void SGTC_Endo_InverseVelocity( Vector &out, const Vector &in, int iMode )
{
	const float flNegTwo = -2.0f;	// rodata mul @ ebx-0x25c58
	out.x = in.x + in.x * flNegTwo;
	out.y = in.y + in.y * flNegTwo;
	if( iMode == 1 )
		out.z = in.z + in.z * flNegTwo;
	else
		out.z = in.z;
}

static void SGTC_Endo_InverseAngle( Vector &out, const Vector &in, int iMode )
{
	out = in;
	if( iMode == 0 )
	{
		out.x += 180.0f;
		if( out.x >= 180.0f )
			out.x -= 360.0f;
	}
	else if( iMode == 1 )
	{
		out.y += 180.0f;
		if( out.y >= 180.0f )
			out.y -= 360.0f;
	}
	else if( iMode == 2 )
	{
		out.z += 180.0f;
		if( out.z >= 180.0f )
			out.z -= 360.0f;
	}
}

// Slot 13 is "temp_eject" (CEject below, #120). Nothing in the port creates one
// from the firing path, but the slot keeps its place so the slots after it keep
// their numbers, and a map-placed one still teleports.
static const char *const s_szTeleportClasses[] =
{
	"player", "grenade", "monster_larve", "lance_proj", "reetou_proj",
	"monster_satchel", "func_pushable", "func_train", "monster_tac",
	"monster_mine", "func_breakable", "game_nuke", "big_lance_proj",
	"temp_eject", "zat_proj", "monster_zatboom",
};

// The table is indexed by SgtcTeleportClass (game_shared/sgtc_enums.h).
SGTC_STATIC_ASSERT( sizeof( s_szTeleportClasses ) / sizeof( s_szTeleportClasses[0] ) == SGTC_TELE_NUM_CLASSES,
	"s_szTeleportClasses must have one name per SgtcTeleportClass" );

static void SGTC_TeleportPostTouch( CSpecialTeleport *pGate, CBaseEntity *pEnt, int iClass )
{
	switch( iClass )
	{
	case SGTC_TELE_PLAYER:	// ref clears +0x3e0 (m_bShieldWall)
		if( pEnt->IsPlayer() )
			((CBasePlayer *)pEnt)->m_bShieldWall = FALSE;
		break;
	case SGTC_TELE_LANCE_PROJ:
		if( pGate->m_iReverse )
		{
			CLanceProj *pProj = (CLanceProj *)pEnt;
			SGTC_Endo_InverseVelocity( pProj->m_vecSavedVel, pProj->m_vecSavedVel, 2 );
		}
		break;
	case SGTC_TELE_GRENADE:
		// The ref inverts a saved-velocity Vector at entity +0x418 here when
		// reversing. That offset is past the end of a CGrenade in this tree (a
		// heap overwrite), and a plain "grenade" has no saved velocity: only
		// the lance bolts (CSGTCProjBase::m_vecSavedVel) do, and nothing reads
		// one off a grenade. The velocity itself is already reversed by
		// TeleportTouch, so there is nothing left to do.
		break;
	case SGTC_TELE_SATCHEL:
		((CSatchelCharge *)pEnt)->Unstick();
		break;
	case SGTC_TELE_BIG_LANCE_PROJ:	// ref: inverse saved vel @ +0x408 of the big layout
		if( pGate->m_iReverse )
		{
			CBigLanceProj *pProj = (CBigLanceProj *)pEnt;
			SGTC_Endo_InverseVelocity( pProj->m_vecSavedVel, pProj->m_vecSavedVel, 2 );
		}
		break;
	case SGTC_TELE_ZAT_PROJ:	// arm +0xfc
		((CZatProj *)pEnt)->m_iTouchPass = 1;
		break;
	default:
		break;
	}
}

void CSpecialTeleport::TeleportTouch( CBaseEntity *pOther )
{
	if( m_bActive || !pOther )	// ref @0x194d25: active when +0x240 == 0
		return;

	if( !m_hMarker )
	{
		const char *pszMarker = STRING( pev->message );
		if( pszMarker && pszMarker[0] )
			m_hMarker = SGTC_FindTargetnameChain( pszMarker );
	}
	if( !m_hDest )
	{
		const char *pszDest = STRING( pev->target );
		if( pszDest && pszDest[0] )
		{
			m_hDest = SGTC_FindTargetnameChain( pszDest );
			if( m_hDest )
				m_iszSourceClass = m_hDest->pev->classname;
		}
	}
	else if( m_iszSourceClass != m_hDest->pev->classname )
	{
		m_hDest = NULL;
		const char *pszDest = STRING( pev->target );
		if( pszDest && pszDest[0] )
		{
			m_hDest = SGTC_FindTargetnameChain( pszDest );
			if( m_hDest )
				m_iszSourceClass = m_hDest->pev->classname;
		}
	}
	if( !m_hMarker || !m_hDest )
		return;

	int iStart = ( m_iTeleportPlayers > 0 ) ? SGTC_TELE_PLAYER : SGTC_TELE_PLAYER + 1;

	for( int iClass = iStart; iClass < SGTC_TELE_NUM_CLASSES; iClass++ )
	{
		CBaseEntity *pFound = NULL;
		while( ( pFound = UTIL_FindEntityByClassname( pFound, s_szTeleportClasses[iClass] ) ) != NULL )
		{
			if( pFound != pOther )
				continue;

			Vector delta = pOther->pev->origin - m_hMarker->pev->origin;
			Vector dest = m_hDest->pev->origin + delta;
			UTIL_SetOrigin( pOther->pev, dest );

			if( m_iReverse )
			{
				Vector ang;
				SGTC_Endo_InverseAngle( ang, pOther->pev->v_angle, 0 );
				pOther->pev->v_angle = ang;
				SGTC_Endo_InverseAngle( ang, pOther->pev->angles, 0 );
				pOther->pev->angles = ang;
			}

			if( iClass == SGTC_TELE_PUSHABLE )	// func_pushable — scale velocity * 5
				pOther->pev->velocity = pOther->pev->velocity * 5.0f;

			if( m_iReverse && iClass != SGTC_TELE_TEMP_EJECT )
			{
				Vector vel;
				SGTC_Endo_InverseVelocity( vel, pOther->pev->velocity, 2 );
				pOther->pev->velocity = vel;
			}

			SGTC_TeleportPostTouch( this, pOther, iClass );
			return;
		}
	}
}

void CSpecialTeleport::TeleportThink( void )
{
	pev->nextthink = gpGlobals->time + 0.1f;	// ref @0x194cd2
}

void CSpecialTeleport::SpecialUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// ref @0x195ee6 — toggle m_bActive by USE_TYPE (no SUB_UseTargets).
	if( useType == USE_TOGGLE )
		m_bActive = !m_bActive;
	else if( useType == USE_OFF )
		m_bActive = 0;
	else if( useType == USE_ON )
		m_bActive = 1;
}

void CSpecialTeleport::Reset( void )
{
	// ref @0x195f2c — invert spawnflag 2 "Start On" into m_bActive.
	m_bActive = ( pev->spawnflags & 2 ) ? 0 : 1;
}

LINK_ENTITY_TO_CLASS( special_teleport_in, CSpecialTeleport );

// ---------------------------------------------------------------------------
// zat_proj — ref CZatProj::Spawn @0x1c454c, Think @0x1c4764, Touch @0x1c4e2a.
// ---------------------------------------------------------------------------

// Port safeguard, not a ref value (#123): longest a bolt may live without
// impacting anything (lance bolts use the same 5 s). The ref Think tail reads
// this+0x128, which may be the suppress timer (m_flSgtcSuppressTime, stamped by
// the shield emitter) rather than a lifetime; unverified without the ref binary.
#define SGTC_ZATPROJ_LIFETIME 5.0f

// ref hl.so: Think @0x180120 / ProjTouch @0x18080b arm pev+0x1e4, which is
// entvars_t::dmgtime in the 32-bit layout (named here instead of a raw byte
// offset, which lands on a different field in a 64-bit build).
static inline float &SGTC_ZatProjScanGate( entvars_t *pev )
{
	return pev->dmgtime;
}

void CZatProj::Precache( void )
{
	PRECACHE_MODEL( "models/w_projreetou.mdl" );
	m_iZatWaveSprite = PRECACHE_MODEL( "sprites/zatwave.spr" );	// ref +0x3ec @0x1c57ac
	m_iGammaWaveSprite = PRECACHE_MODEL( "sprites/gammawave.spr" );	// ref +0x408
	PRECACHE_MODEL( "sprites/zatimpact.spr" );
	PRECACHE_SOUND( "weapons/zatimpact.wav" );
}

void CZatProj::Spawn( void )
{
	Precache();
	pev->movetype = MOVETYPE_BOUNCE;	// ref pev+0x108 = 10
	pev->solid = SOLID_BBOX;			// ref pev+0x10c = 2
	SET_MODEL( ENT( pev ), "models/w_projreetou.mdl" );
	UTIL_SetSize( pev, Vector( 0, 0, 0 ), Vector( 0, 0, 0 ) );
	UTIL_SetOrigin( pev, pev->origin );
	pev->classname = MAKE_STRING( "zat_proj" );
	pev->friction = 0.01f;	// ref 0x3c23d70a
	pev->dmg = 0;			// ref pev+0x1e0 @0x1c454c
	// build-7: w_projreetou.mdl has one sequence; 4 is out of range (warns).
	pev->sequence = 0;		// ref pev+0x148
	pev->gaitsequence = 0;	// ref pev+0x14c
	SetTouch( &CZatProj::ProjTouch );
	SetThink( &CZatProj::Think );
	m_flLifeEnd = gpGlobals->time + SGTC_ZATPROJ_LIFETIME;
	UTIL_MakeVectors( pev->angles );
	Vector vel = gpGlobals->v_forward.Normalize() * 2000.0f;	// ref rodata = 2000 @0x1b46cd
	pev->velocity = vel;
	SGTC_DiagSpawn( "zat_proj", pev->owner ? CBaseEntity::Instance( pev->owner ) : NULL,
		pev->origin, pev->scale, pev->body, pev->skin, pev->sequence );
	pev->nextthink = gpGlobals->time + 0.01f;	// ref rodata ≈ 0.01 @0x1b46f7
}

CZatProj *CZatProj::CreateZatProj( const Vector &vecOrigin, const Vector &vecAngles,
	CBaseEntity *pOwner, CZat *pWeapon )
{
	CZatProj *pProj = GetClassPtr( (CZatProj *)NULL );
	pProj->m_pZatOwner = NULL;
	pProj->m_flLifeEnd = 0.0f;
	pProj->m_bAllyShield = 0;
	pProj->m_bBeamMode = 0;
	pProj->m_iTouchPass = 0;
	pProj->m_flTouchPassTime = 0.0f;
	pProj->m_vecBeamEnd = vecOrigin;	// ref +0x410
	UTIL_SetOrigin( pProj->pev, vecOrigin );
	Vector vecDir = vecAngles;
	if( vecDir.Length() > 0.0f )
		vecDir = vecDir.Normalize();
	pProj->pev->angles = vecAngles;	// ref pev+0x50: raw aim delta, not UTIL_VecToAngles @0x1c5fac
	if( pOwner )
	{
		pProj->pev->owner = pOwner->edict();
		if( pOwner->IsAlive() )
			pProj->m_pZatOwner = pOwner;	// ref +0x3f8
		if( gpGlobals->time < pOwner->m_flSgtcSuppressTime )	// ref owner+0x128
			pProj->m_bAllyShield = 1;	// ref +0xd5
	}
	pProj->Spawn();
	(void)pWeapon;
	return pProj;
}

void CZatProj::Think( void )
{
	// ProjTouch arms the scan deadline (dmgtime) on impact, but it only removes
	// the bolt when it is touched again, which never happens to a motionless
	// projectile. Expire it here instead of scanning every 10 ms forever.
	if( SGTC_ZatProjScanGate( pev ) > 0.0f && SGTC_ZatProjScanGate( pev ) < gpGlobals->time )
	{
		UTIL_Remove( this );
		return;
	}

	// ref Think @0x1c4764: outer gate pev+0x1e4 (scan deadline) or velocity.
	if( SGTC_ZatProjScanGate( pev ) > 0.0f || pev->velocity.Length() > 0.0f )
	{
		UTIL_MakeVectors( pev->v_angle );	// ref pev+0x74 @0x1c4764
		Vector vecSavedOrigin = pev->origin;
		TraceResult tr;
		// ref: (byte)this+0x40c * 8000 + 0x50
		const float flTrace = m_bBeamMode ? 8080.0f : 80.0f;	// ref 0x40c*8000+0x50
		Vector vecEnd = pev->origin + gpGlobals->v_forward * flTrace;
		UTIL_TraceLine( pev->origin, vecEnd, dont_ignore_monsters, ENT( pev ), &tr );
		pev->origin = tr.vecEndPos;

		CBaseEntity *pScan = NULL;
		while( ( pScan = UTIL_FindEntityInSphere( pScan, vecSavedOrigin, 250.0f ) ) != NULL )
		{
			if( !FClassnameIs( pScan->pev, "monster_shieldemit" ) )
				continue;

			BOOL bBlocked = FALSE;
			if( pScan->pev->origin.z < vecSavedOrigin.z )
			{
				if( vecSavedOrigin.z - pScan->pev->origin.z > 200.0f )
					bBlocked = TRUE;
			}
			else if( pScan->pev->origin.z - vecSavedOrigin.z > 100.0f )
				bBlocked = TRUE;

			if( !bBlocked && !m_bAllyShield )
			{
				pev->origin = vecSavedOrigin;	// ref: restore local_5c, not trace end
				SGTC_ZatImpactFx( pev->origin );
				SGTC_ShieldEmitArmImpactFlash( pScan );	// ref shieldemit +0x104
				UTIL_Remove( this );
				return;
			}
		}
	}

	pev->nextthink = gpGlobals->time + 0.01f;	// ref rodata @B-0x24934 @0x1b4be0

	// ref @0x1c4764 tail gates on this+0x128. The port uses it as a life cap
	// (SGTC_ZATPROJ_LIFETIME) so a bolt that never impacts cannot live
	// forever; whether the ref field is the suppress timer instead is open (#123).
	if( gpGlobals->time >= m_flLifeEnd )
	{
		if( !m_bAllyShield )
			SGTC_ZatImpactFx( pev->origin );
		UTIL_Remove( this );
	}
}

void CZatProj::ProjTouch( CBaseEntity *pOther )
{
	if( !pOther || FClassnameIs( pOther->pev, "zat_proj" ) )
		return;
	if( m_pZatOwner == pOther )
		return;

	// ref @0x1c4e2a — pass-through bounce off non-blocking touches
	BOOL bPassthrough = FALSE;
	if( FBitSet( pOther->pev->flags, FL_MONSTER ) || pOther->IsAlive() ||
		( m_iTouchPass != 1 && m_flTouchPassTime <= gpGlobals->time ) )
		bPassthrough = FALSE;
	else
		bPassthrough = TRUE;

	if( bPassthrough )
	{
		m_iTouchPass = 0;
		m_flTouchPassTime = gpGlobals->time;
	}
	else if( pev->velocity.Length() > 0.0f )
	{
		pev->velocity = g_vecZero;

		// ref: IsAlive (+0xa8) && vfunc +0x94 before CBasePlayer::Zat
		if( pOther->IsAlive() && pOther->IsPlayer() )
		{
			entvars_t *pAtt = m_pZatOwner ? m_pZatOwner->pev : pev;
			((CBasePlayer *)pOther)->Zat( pAtt );
		}

		int iOwnerTeam = m_pZatOwner ? m_pZatOwner->pev->team : 0;
		CBaseEntity *pEnt = NULL;

		while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "monster_tac" ) ) != NULL )
		{
			if( SGTC_Distance3D( pev->origin, pEnt->pev->origin ) < 25 &&
				pEnt->pev->team != iOwnerTeam )
				UTIL_Remove( pEnt );
		}

		pEnt = NULL;
		while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "monster_mine" ) ) != NULL )
		{
			if( SGTC_Distance3D( pev->origin, pEnt->pev->origin ) < 25 &&
				pEnt->pev->team != iOwnerTeam )
				UTIL_Remove( pEnt );
		}

		pEnt = NULL;
		while( ( pEnt = UTIL_FindEntityByClassname( pEnt, "monster_satchel" ) ) != NULL )
		{
			if( SGTC_Distance3D( pev->origin, pEnt->pev->origin ) < 25 &&
				pEnt->pev->team != iOwnerTeam &&
				pEnt->pev->takedamage != DAMAGE_NO )	// ref pev+0x16c @0x1c4e2a
				UTIL_Remove( pEnt );
		}

		SGTC_ZatImpactFx( pev->origin );
		if( m_bBeamMode )
			SGTC_ZatProjTouchBeams( this );
		SGTC_ZatProjScanGate( pev ) = gpGlobals->time + 0.5f;	// ref pev+0x1e4
	}

	if( SGTC_ZatProjScanGate( pev ) > 0.0f && SGTC_ZatProjScanGate( pev ) < gpGlobals->time )
		UTIL_Remove( this );
}

LINK_ENTITY_TO_CLASS( zat_proj, CZatProj );

// Save/restore (#99). m_pZatOwner is the firing player (saved like
// CBasePlayerItem::m_pPlayer). Not saved: m_iZatWaveSprite / m_iGammaWaveSprite
// (precache handles, rebuilt by Precache() after restore).
TYPEDESCRIPTION CZatProj::m_SaveData[] =
{
	DEFINE_FIELD( CZatProj, m_pZatOwner, FIELD_CLASSPTR ),
	DEFINE_FIELD( CZatProj, m_flLifeEnd, FIELD_TIME ),
	DEFINE_FIELD( CZatProj, m_bAllyShield, FIELD_CHARACTER ),
	DEFINE_FIELD( CZatProj, m_bBeamMode, FIELD_CHARACTER ),
	DEFINE_FIELD( CZatProj, m_iTouchPass, FIELD_INTEGER ),
	DEFINE_FIELD( CZatProj, m_flTouchPassTime, FIELD_TIME ),
	DEFINE_FIELD( CZatProj, m_vecBeamEnd, FIELD_POSITION_VECTOR ),
};

IMPLEMENT_SAVERESTORE( CZatProj, CBaseEntity )

// ---------------------------------------------------------------------------
// entity_spawn_round — ref CEntSpawn (Spawn @0x195bea, Event @0x195ba6).
// Round-scoped spawn marker: CStargateTeamDM::NewRound() (ref @0x184f42) walks
// every "entity_spawn_round" entity and calls Event() on it, which re-creates
// the classname stored in the marker's "message" key at the marker's own
// origin/angles. Spawn() caches STRING(pev->message) once so Event() can fire
// repeatedly across rounds.
// ---------------------------------------------------------------------------
void CEntSpawn::Spawn( void )
{
	// ref @0x195bea — member +0x19c = gpGlobals->pStringBase + pev->message
	// (STRING(pev->message) pre-resolved), and pev+0x104 nextthink =
	// gpGlobals->time + 0.1. No networkable state, no model.
	m_pSpawnClass = STRING( pev->message );
	pev->nextthink = gpGlobals->time + 0.1f;
}

void CEntSpawn::Event( void )
{
	// ref @0x195ba6 — CBaseEntity::Create( cached +0x19c, pev->origin (+0x8),
	// pev->angles (+0x50), NULL ). The marker itself stays in the world.
	CBaseEntity::Create( m_pSpawnClass, pev->origin, pev->angles, NULL );
}

LINK_ENTITY_TO_CLASS( entity_spawn_round, CEntSpawn );

// ---------------------------------------------------------------------------
// temp_eject (#120) — ref CEject (Spawn @0x195c9e, Think @0x195c48, factory
// @0x195ebe). In the ref it is an invisible, short-lived carrier created and
// armed by the firing path. The port never reproduced that creation site, and
// #98 dropped the class, which also dropped the temp_eject export: a map entity
// or "give temp_eject" then failed to spawn. This keeps the export at parity
// with the ref as an inert, invisible entity that removes itself. Unlike the ref
// Think (which only removes an armed eject, and nothing arms one here), it does
// not wait for an arm flag, so a stray instance cannot linger.
// ---------------------------------------------------------------------------
class CEject : public CBaseEntity
{
public:
	void Spawn( void );
	void EXPORT EjectThink( void );
	virtual int ObjectCaps( void ) { return CBaseEntity::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }
};

void CEject::Spawn( void )
{
	// ref @0x195c9e, minus SET_MODEL( "sprites/zatimpact.spr" ): the entity is
	// EF_NODRAW, so it needs no model (or a precache for one).
	pev->movetype = MOVETYPE_BOUNCEMISSILE;	// ref pev+0x108 = 10
	pev->solid = SOLID_NOT;			// ref SOLID_BBOX with a zero-size box; nothing touches it here
	UTIL_SetSize( pev, g_vecZero, g_vecZero );
	pev->effects |= EF_NODRAW;		// ref pev+0x118 |= 0x80
	pev->angles = g_vecZero;
	pev->rendermode = kRenderTransAdd;	// ref pev+0x148 = 5
	pev->renderamt = 0.0f;
	UTIL_SetOrigin( pev, pev->origin );
	pev->classname = MAKE_STRING( "temp_eject" );
	SetThink( &CEject::EjectThink );
	pev->nextthink = gpGlobals->time + 0.5f;	// ref pev+0x104 = time + 0.5
}

void CEject::EjectThink( void )
{
	UTIL_Remove( this );
}

LINK_ENTITY_TO_CLASS( temp_eject, CEject );

// ---------------------------------------------------------------------------
// effect_bigshield — ref CBigShield (Spawn @0x162672, Think @0x162548,
// Classify @0x16253e, factory @0x162836). Team-sized forcefield wall: own
// model, its own renderamt regeneration loop driven by pev->health
// (100 → renderamt 130, 80 → renderamt 225 + crash sound + reset to 100), and
// a 0.25s stepping gate. CStargateTeamDM::NewRound() removes every
// "effect_bigshield" entity each round (ref NewRound @0x184f42).
// ---------------------------------------------------------------------------
class CBigShield : public CBaseEntity
{
public:
	void Spawn( void );
	void EXPORT Think( void );
	int Classify( void ) { return CLASS_NONE; }	// ref @0x16253e returns 0

	float m_flExpire;		// ref +0x19c — lifetime gate (read only outside this class)
	float m_flNextArmorStep;	// ref +0x1a0 — 0.25s gate for the renderamt stepping
};

void CBigShield::Spawn( void )
{
	// ref @0x162672, in write order:
	SET_MODEL( ENT( pev ), "models/bigshield.mdl" );
	// build-11: bigshield.mdl is a placeholder copy of shield.mdl, so the
	// forcefield wall rendered as a random floating shield in MP maps.  Keep
	// the entity/Think behaviour but hide the placeholder mesh until a real
	// forcefield asset ships (the wall is gameplay-only anyway).
	pev->effects |= EF_NODRAW;
	UTIL_SetSize( pev, Vector( 0, 0, 0 ), Vector( 0, 0, 0 ) );
	pev->solid = SOLID_NOT;			// ref pev+0x10c = 0
	pev->movetype = MOVETYPE_NONE;		// ref pev+0x108 = 0
	pev->health = 100.0f;			// ref pev+0x160 = 100.0
	pev->view_ofs = g_vecZero;		// ref pev+0x174 = (0,0,0)
	pev->renderamt = 130.0f;		// ref pev+0x14c = 130.0
	pev->rendermode = kRenderTransAdd;	// ref pev+0x148 = 5
	m_flExpire = gpGlobals->time + 1.0f;	// ref +0x19c, first store
	SetThink( &CBigShield::Think );		// ref this+0x134/0x138 = Think (vslot +0xc4)
	m_flExpire = gpGlobals->time + 1.0f;	// ref +0x19c, second store (as in ref)
	Think();				// ref: immediate virtual call via vslot +0xc4
}

void CBigShield::Think( void )
{
	// ref @0x162548 — renderamt stepping is gated on +0x1a0 (which Spawn never
	// initialises; the first pass that wins the gate arms it for 0.25s).
	if( m_flNextArmorStep < gpGlobals->time )
	{
		if( pev->health == 100.0f )		// ref pev+0x160 == 100.0
			pev->renderamt = 130.0f;	// ref pev+0x14c

		if( pev->health == 80.0f )		// ref pev+0x160 == 80.0 (damaged wall)
		{
			pev->renderamt = 225.0f;	// ref pev+0x14c
			pev->health = 100.0f;		// ref pev+0x160
			EMIT_SOUND( ENT( pev ), CHAN_VOICE, "forcefield/forcefield_crash.wav", 1.0f, ATTN_NORM );
		}
		m_flNextArmorStep = gpGlobals->time + 0.25f;
	}
	pev->nextthink = gpGlobals->time + 0.2f;	// ref pev+0x104
}

LINK_ENTITY_TO_CLASS( effect_bigshield, CBigShield );
