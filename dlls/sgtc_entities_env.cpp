// sgtc_entities_env.cpp — StargateTC env_* map entities: env_model, env_particule,
// env_rain, env_lensflare, env_fountain and env_fog, with their KeyValue parsers
// and wire payload writers.
//
// Split out of sgtc_rules.cpp (#94); code moved as it was. SGTC_Rules_roundf
// (used by the fountain payload) is shared and lives in sgtc_rules_msg.cpp.

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


// ===========================================================================
// env_model (CModel)
// ===========================================================================
TYPEDESCRIPTION CModel::m_SaveData[] =
{
	DEFINE_FIELD( CModel, m_iAnim, FIELD_INTEGER ),
};
IMPLEMENT_SAVERESTORE( CModel, CBaseAnimating );

void CModel::Spawn( void )
{
	PRECACHE_MODEL( (char *)STRING( pev->model ) );
	SET_MODEL( ENT( pev ), STRING( pev->model ) );
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	UTIL_SetSize( pev, Vector( -20, -20, -20 ), Vector( 20, 20, 20 ) );
	if( m_iAnim != 0 )
	{
		pev->sequence = m_iAnim;
		pev->frame = 0;
		ResetSequenceInfo();
	}
	SetThink( &CModel::Animate );
	pev->nextthink = gpGlobals->time + 0.1f;
}

void CModel::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "anim" ) )
	{
		m_iAnim = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseAnimating::KeyValue( pkvd );
}

void CModel::Animate( void )
{
	// FGD spawnflag 2 = follow target.
	if( FBitSet( pev->spawnflags, 2 ) && pev->target )
	{
		CBaseEntity *t = UTIL_FindEntityByTargetname( NULL, STRING( pev->target ) );
		if( t )
			UTIL_SetOrigin( pev, t->pev->origin );
	}
	StudioFrameAdvance();
	pev->nextthink = gpGlobals->time + 0.1f;
}
LINK_ENTITY_TO_CLASS( env_model, CModel );

// ===========================================================================
// Particle net entities. Payload layouts are BEST-EFFORT (client handlers
// drain opaquely): entindex + origin + param block, MSG_ALL on Spawn,
// MSG_ONE in SendValueTo.
// ===========================================================================
static void SGTC_ParseVector( const char *str, Vector &out )
{
	out = Vector( 0, 0, 0 );
	if( str )
		sscanf( str, "%f %f %f", &out.x, &out.y, &out.z );
}

static int SGTC_ColorByte( float f )
{
	int c = (int)f;
	if( c < 0 )
		c = 0;
	if( c > 255 )
		c = 255;
	return c;
}


static void SGTC_ParseColor4( const char *str, float out[4] )
{
	int r = 128, g = 128, b = 128, a = 128;
	if( str && sscanf( str, "%d %d %d %d", &r, &g, &b, &a ) == 4 )
	{
		out[0] = (float)r;
		out[1] = (float)g;
		out[2] = (float)b;
		out[3] = (float)a;
	}
}

static void SGTC_ParseColor( const char *str, Vector &out )
{
	float c[4];
	SGTC_ParseColor4( str, c );
	out = Vector( c[0], c[1], c[2] );
}

static void SGTC_WriteEnvParticulePayload( CEnvParticule *p )
{
	// ref CEnvParticule::SendValue @0x15ec68 — 47 bytes (19 WRITE_COORD + 7 BYTE + 1 SHORT).
	// First 6 coords: pev->origin then pev->oldorigin (entvars +0xc4..+0xd8), not absmin/max.
	WRITE_COORD( p->pev->origin.x );
	WRITE_COORD( p->pev->origin.y );
	WRITE_COORD( p->pev->origin.z );
	WRITE_COORD( p->pev->oldorigin.x );
	WRITE_COORD( p->pev->oldorigin.y );
	WRITE_COORD( p->pev->oldorigin.z );
	WRITE_COORD( p->m_vecAccel.x );
	WRITE_COORD( p->m_vecAccel.y );
	WRITE_COORD( p->m_vecAccel.z );
	WRITE_COORD( p->m_vecV0.x );
	WRITE_COORD( p->m_vecV0.y );
	WRITE_COORD( p->m_vecV0.z );
	WRITE_COORD( p->m_vecSpread.x );
	WRITE_COORD( p->m_vecSpread.y );
	WRITE_COORD( p->m_vecSpread.z );
	for( int i = 0; i < 4; i++ )
		WRITE_BYTE( SGTC_ColorByte( p->m_flColor[i] ) );
	WRITE_COORD( p->m_vecSize.x );
	WRITE_COORD( p->m_vecSize.y );
	WRITE_COORD( p->m_flLife );
	WRITE_COORD( p->m_flBounce );
	WRITE_BYTE( p->m_iSprite );
	WRITE_BYTE( p->m_iType );
	WRITE_SHORT( p->m_iCount );
	WRITE_BYTE( p->m_iId );
}

TYPEDESCRIPTION CEnvParticule::m_SaveData[] =
{
	DEFINE_FIELD( CEnvParticule, m_iCount, FIELD_INTEGER ),
	DEFINE_FIELD( CEnvParticule, m_iId, FIELD_INTEGER ),
	DEFINE_FIELD( CEnvParticule, m_vecAccel, FIELD_VECTOR ),
	DEFINE_FIELD( CEnvParticule, m_vecV0, FIELD_VECTOR ),
	DEFINE_FIELD( CEnvParticule, m_vecSpread, FIELD_VECTOR ),
	DEFINE_FIELD( CEnvParticule, m_vecSize, FIELD_VECTOR ),
	DEFINE_ARRAY( CEnvParticule, m_flColor, FIELD_FLOAT, 4 ),
	DEFINE_FIELD( CEnvParticule, m_flLife, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvParticule, m_flBounce, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvParticule, m_iSprite, FIELD_INTEGER ),
	DEFINE_FIELD( CEnvParticule, m_iType, FIELD_INTEGER ),
};
IMPLEMENT_SAVERESTORE( CEnvParticule, CPointEntity );

void CEnvParticule::Spawn( void )
{
	pev->solid = SOLID_NOT;
	pev->movetype = MOVETYPE_NONE;
	SetObjectCollisionBox();
}

void CEnvParticule::Activate( void )
{
	CPointEntity::Activate();
	SendValue();
}

void CEnvParticule::KeyValue( KeyValueData *pkvd )
{
	// Verified rodata keys (0x1d9b5f..0x1d9bbf block).
	if( FStrEq( pkvd->szKeyName, "nombre" ) )
	{
		m_iCount = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "id" ) )
	{
		m_iId = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "acceleration" ) )
	{
		SGTC_ParseVector( pkvd->szValue, m_vecAccel );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "v0" ) )
	{
		SGTC_ParseVector( pkvd->szValue, m_vecV0 );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "ecart" ) )
	{
		SGTC_ParseVector( pkvd->szValue, m_vecSpread );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "taille" ) )
	{
		int x = 2, y = 2;
		if( sscanf( pkvd->szValue, "%d %d", &x, &y ) == 2 )
			m_vecSize = Vector( (float)x, (float)y, 0 );
		else
			m_vecSize = Vector( 2, 2, 0 );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "_couleur" ) )
	{
		SGTC_ParseColor4( pkvd->szValue, m_flColor );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "vie" ) )
	{
		m_flLife = (float)atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "bounce" ) )
	{
		m_flBounce = (float)atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "sprite" ) )
	{
		m_iSprite = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "type" ) )
	{
		m_iType = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CPointEntity::KeyValue( pkvd );
}

void CEnvParticule::Reset( void )
{
	MESSAGE_BEGIN( MSG_ALL, gmsgChangeParticuleState );
		WRITE_BYTE( m_iId );
		WRITE_BYTE( FBitSet( pev->spawnflags, 1 ) ? 1 : 0 );
	MESSAGE_END();
}

void CEnvParticule::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if( !ShouldToggle( useType, FBitSet( pev->spawnflags, 1 ) ) )
		return;
	if( FBitSet( pev->spawnflags, 1 ) )
		pev->spawnflags &= ~1;
	else
		pev->spawnflags |= 1;
	MESSAGE_BEGIN( MSG_ALL, gmsgChangeParticuleState );
		WRITE_BYTE( m_iId );
		WRITE_BYTE( FBitSet( pev->spawnflags, 1 ) ? 1 : 0 );
	MESSAGE_END();
	(void)pActivator; (void)pCaller; (void)value;
}

void CEnvParticule::SendValue( void )
{
	MESSAGE_BEGIN( MSG_ALL, gmsgEnvParticule, NULL );
	SGTC_WriteEnvParticulePayload( this );
	MESSAGE_END();
}

void CEnvParticule::SendValueTo( CBasePlayer *pPlayer )
{
	MESSAGE_BEGIN( MSG_ONE, gmsgEnvParticule, NULL, pPlayer->pev );
	SGTC_WriteEnvParticulePayload( this );
	MESSAGE_END();
}
LINK_ENTITY_TO_CLASS( env_particule, CEnvParticule );

TYPEDESCRIPTION CEnvRain::m_SaveData[] =
{
	DEFINE_FIELD( CEnvRain, m_flDensity, FIELD_FLOAT ),
};
IMPLEMENT_SAVERESTORE( CEnvRain, CEnvParticule );

void CEnvRain::KeyValue( KeyValueData *pkvd )
{
	// .so CEnvRain::KeyValue handles "densite", rest falls to particule.
	if( FStrEq( pkvd->szKeyName, "densite" ) )
	{
		m_flDensity = (float)atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CEnvParticule::KeyValue( pkvd );
}

static void SGTC_RainEnsureCount( CEnvRain *pRain )
{
	if( pRain->m_iCount != 0 )
	{
		if( pRain->m_iCount < 0 )
			pRain->m_iCount = -pRain->m_iCount;
		return;
	}
	// ref CEnvRain::SendValue @0x15f84a — volume from origin vs oldorigin (+0xc4..+0xd8).
	const float vol = ( pRain->pev->origin.x - pRain->pev->oldorigin.x ) *
		( pRain->pev->origin.y - pRain->pev->oldorigin.y ) *
		( pRain->pev->origin.z - pRain->pev->oldorigin.z );
	pRain->m_iCount = (int)( pRain->m_flDensity * vol );
	if( pRain->m_iCount < 0 )
		pRain->m_iCount = -pRain->m_iCount;
}

static void SGTC_WriteEnvRainPayload( CEnvRain *pRain )
{
	// ref CEnvRain::SendValue @0x15f82a — 15 bytes (6 COORD + BYTE + SHORT).
	WRITE_COORD( pRain->pev->origin.x );
	WRITE_COORD( pRain->pev->origin.y );
	WRITE_COORD( pRain->pev->origin.z );
	WRITE_COORD( pRain->pev->oldorigin.x );
	WRITE_COORD( pRain->pev->oldorigin.y );
	WRITE_COORD( pRain->pev->oldorigin.z );
	WRITE_BYTE( pRain->m_iSprite );
	WRITE_SHORT( pRain->m_iCount );
}

void CEnvRain::SendValue( void )
{
	SGTC_RainEnsureCount( this );
	MESSAGE_BEGIN( MSG_ALL, gmsgEnvRain, NULL );
	SGTC_WriteEnvRainPayload( this );
	MESSAGE_END();
}

void CEnvRain::SendValueTo( CBasePlayer *pPlayer )
{
	SGTC_RainEnsureCount( this );
	MESSAGE_BEGIN( MSG_ONE, gmsgEnvRain, NULL, pPlayer->pev );
	SGTC_WriteEnvRainPayload( this );
	MESSAGE_END();
}
LINK_ENTITY_TO_CLASS( env_rain, CEnvRain );

TYPEDESCRIPTION CEnvLensFlare::m_SaveData[] =
{
	DEFINE_FIELD( CEnvLensFlare, m_vecColor, FIELD_VECTOR ),
	DEFINE_FIELD( CEnvLensFlare, m_bUseOrigin, FIELD_BOOLEAN ),
	DEFINE_FIELD( CEnvLensFlare, m_vecOrigin, FIELD_VECTOR ),
};
IMPLEMENT_SAVERESTORE( CEnvLensFlare, CPointEntity );

void CEnvLensFlare::KeyValue( KeyValueData *pkvd )
{
	// Verified keys: _couleur / useorigine / origine.
	if( FStrEq( pkvd->szKeyName, "_couleur" ) )
	{
		SGTC_ParseColor( pkvd->szValue, m_vecColor );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "useorigine" ) )
	{
		m_bUseOrigin = atoi( pkvd->szValue ) != 0;
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "origine" ) )
	{
		SGTC_ParseVector( pkvd->szValue, m_vecOrigin );
		pkvd->fHandled = TRUE;
	}
	else
		CPointEntity::KeyValue( pkvd );
}

static void SGTC_WriteLensFlarePayload( CEnvLensFlare *pLens )
{
	// ref CEnvLensFlare::SendValue @0x161184 — 10 bytes (3 COORD + 4 BYTE).
	Vector org = pLens->m_bUseOrigin ? pLens->pev->origin : pLens->m_vecOrigin;
	WRITE_COORD( org.x );
	WRITE_COORD( org.y );
	WRITE_COORD( org.z );
	WRITE_BYTE( (int)pLens->m_vecColor.x );
	WRITE_BYTE( (int)pLens->m_vecColor.y );
	WRITE_BYTE( (int)pLens->m_vecColor.z );
	WRITE_BYTE( 255 );
}

void CEnvLensFlare::SendValue( void )
{
	MESSAGE_BEGIN( MSG_ALL, gmsgEnvNewLens, NULL );
	SGTC_WriteLensFlarePayload( this );
	MESSAGE_END();
}

void CEnvLensFlare::SendValueTo( CBasePlayer *pPlayer )
{
	MESSAGE_BEGIN( MSG_ONE, gmsgEnvNewLens, NULL, pPlayer->pev );
	SGTC_WriteLensFlarePayload( this );
	MESSAGE_END();
}
LINK_ENTITY_TO_CLASS( env_lensflare, CEnvLensFlare );

TYPEDESCRIPTION CEnvFountain::m_SaveData[] =
{
	DEFINE_FIELD( CEnvFountain, m_iId, FIELD_INTEGER ),
	DEFINE_FIELD( CEnvFountain, m_iCount, FIELD_INTEGER ),
	DEFINE_FIELD( CEnvFountain, m_flMinAngle, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvFountain, m_flMaxAngle, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvFountain, m_flMinVel, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvFountain, m_flMaxVel, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvFountain, m_flMinRadius, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvFountain, m_flMaxRadius, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvFountain, m_vecDir, FIELD_VECTOR ),
	DEFINE_FIELD( CEnvFountain, m_vecOrigine, FIELD_VECTOR ),
	DEFINE_FIELD( CEnvFountain, m_vecSize, FIELD_VECTOR ),
	DEFINE_ARRAY( CEnvFountain, m_flColor, FIELD_FLOAT, 4 ),
	DEFINE_FIELD( CEnvFountain, m_flLife, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvFountain, m_flBounce, FIELD_FLOAT ),
	DEFINE_FIELD( CEnvFountain, m_iSprite, FIELD_INTEGER ),
	DEFINE_FIELD( CEnvFountain, m_iType, FIELD_INTEGER ),
};
IMPLEMENT_SAVERESTORE( CEnvFountain, CPointEntity );

void CEnvFountain::KeyValue( KeyValueData *pkvd )
{
	// Verified rodata keys (0x1d9be5..0x1d9c51 block).
	if( FStrEq( pkvd->szKeyName, "nombre" ) )
	{
		m_iCount = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "id" ) )
	{
		m_iId = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "directeur" ) )
	{
		SGTC_ParseVector( pkvd->szValue, m_vecDir );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "origine" ) )
	{
		SGTC_ParseVector( pkvd->szValue, m_vecOrigine );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "taille" ) )
	{
		int x = 2, y = 2;
		if( sscanf( pkvd->szValue, "%d %d", &x, &y ) == 2 )
			m_vecSize = Vector( (float)x, (float)y, 0 );
		else
			m_vecSize = Vector( 2, 2, 0 );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "_couleur" ) )
	{
		SGTC_ParseColor4( pkvd->szValue, m_flColor );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "minAngle" ) )
	{
		m_flMinAngle = (float)atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "maxAngle" ) )
	{
		m_flMaxAngle = (float)atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "minRadius" ) )
	{
		m_flMinRadius = (float)atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "maxRadius" ) )
	{
		m_flMaxRadius = (float)atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "minVelocity" ) )
	{
		m_flMinVel = (float)atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "maxVelocity" ) )
	{
		m_flMaxVel = (float)atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "vie" ) )
	{
		m_flLife = (float)atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "bounce" ) )
	{
		m_flBounce = (float)atof( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "sprite" ) )
	{
		m_iSprite = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "type" ) )
	{
		m_iType = atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CPointEntity::KeyValue( pkvd );
}

void CEnvFountain::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if( !ShouldToggle( useType, FBitSet( pev->spawnflags, 1 ) ) )
		return;
	if( FBitSet( pev->spawnflags, 1 ) )
		pev->spawnflags &= ~1;
	else
		pev->spawnflags |= 1;
	MESSAGE_BEGIN( MSG_ALL, gmsgChangeParticuleState );
		WRITE_BYTE( m_iId );
		WRITE_BYTE( FBitSet( pev->spawnflags, 1 ) ? 1 : 0 );
	MESSAGE_END();
	(void)pActivator; (void)pCaller; (void)value;
}

static void SGTC_WriteEnvFountainPayload( CEnvFountain *p )
{
	// Field order follows ref CEnvFountain::SendValue @0x1605c2. The 43-byte
	// total (15 COORD, 4+4 BYTE, sprite, type, SHORT, BYTE) is this writer's own
	// count; the ref's registered size was not re-read (older notes said 45, see
	// #123). REG_USER_MSG and cl_dll/sgtc_env.cpp must match it;
	// tests/features/test_usermsg_sizes.py checks that.
	WRITE_COORD( p->pev->origin.x );
	WRITE_COORD( p->pev->origin.y );
	WRITE_COORD( p->pev->origin.z );
	WRITE_COORD( p->m_flMinRadius );
	WRITE_COORD( p->m_flMaxRadius );
	WRITE_COORD( p->m_vecOrigine.x );
	WRITE_COORD( p->m_vecOrigine.y );
	WRITE_COORD( p->m_vecOrigine.z );
	WRITE_COORD( p->m_flMinAngle );
	WRITE_COORD( p->m_flMaxAngle );
	WRITE_COORD( p->m_flMinVel );
	WRITE_COORD( p->m_flMaxVel );
	WRITE_COORD( p->m_vecDir.x );
	WRITE_COORD( p->m_vecDir.y );
	WRITE_COORD( p->m_vecDir.z );
	for( int i = 0; i < 4; i++ )
		WRITE_BYTE( SGTC_ColorByte( p->m_flColor[i] ) );
	WRITE_BYTE( (int)SGTC_Rules_roundf( p->m_vecSize.x ) );
	WRITE_BYTE( (int)SGTC_Rules_roundf( p->m_vecSize.y ) );
	WRITE_BYTE( (int)SGTC_Rules_roundf( p->m_flLife ) );
	WRITE_BYTE( (int)SGTC_Rules_roundf( p->m_flBounce ) );
	WRITE_BYTE( p->m_iSprite );
	WRITE_BYTE( p->m_iType );
	WRITE_SHORT( p->m_iCount );
	WRITE_BYTE( p->m_iId );
}

void CEnvFountain::SendValue( void )
{
	MESSAGE_BEGIN( MSG_ALL, gmsgEnvFountain, NULL );
	SGTC_WriteEnvFountainPayload( this );
	MESSAGE_END();
}

void CEnvFountain::SendValueTo( CBasePlayer *pPlayer )
{
	MESSAGE_BEGIN( MSG_ONE, gmsgEnvFountain, NULL, pPlayer->pev );
	SGTC_WriteEnvFountainPayload( this );
	MESSAGE_END();
}
LINK_ENTITY_TO_CLASS( env_fountain, CEnvFountain );

// ===========================================================================
// env_fog — CClientFog (client-driven distance fog volume).
// Verified vs ref/stc_i386.so:
//   env_fog factory thunk @0x19d364 -> GetClassPtr<CClientFog>(...);
//   CClientFog::KeyValue  @0x19c03a: keys "startdist" (rodata 0x1ddcd0) and
//     "enddist" (0x1ddcda), both parsed with atoi() into float members
//     (this+0x19c / this+0x1a0), pkvd->fHandled = TRUE.
//   CClientFog::Spawn     @0x19c106: pev->movetype = 0 (MOVETYPE_NONE),
//     pev->solid = 0 (SOLID_NOT), pev->rendermode = 2 (kRenderTransTexture),
//     pev->renderamt = 0.0f. No Precache, no model, no think.
// The server side is a passive data entity; the client renders the fog from
// the startdist/enddist baseline fields.
// ===========================================================================
TYPEDESCRIPTION CClientFog::m_SaveData[] =
{
	DEFINE_FIELD( CClientFog, m_flStartDist, FIELD_FLOAT ),
	DEFINE_FIELD( CClientFog, m_flEndDist, FIELD_FLOAT ),
};
IMPLEMENT_SAVERESTORE( CClientFog, CBaseEntity );

void CClientFog::KeyValue( KeyValueData *pkvd )
{
	if( FStrEq( pkvd->szKeyName, "startdist" ) )
	{
		m_flStartDist = (float)atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else if( FStrEq( pkvd->szKeyName, "enddist" ) )
	{
		m_flEndDist = (float)atoi( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
		CBaseEntity::KeyValue( pkvd );
}

void CClientFog::Spawn( void )
{
	pev->movetype = MOVETYPE_NONE;
	pev->solid = SOLID_NOT;
	pev->rendermode = kRenderTransTexture;
	pev->renderamt = 0.0f;
}
LINK_ENTITY_TO_CLASS( env_fog, CClientFog );
