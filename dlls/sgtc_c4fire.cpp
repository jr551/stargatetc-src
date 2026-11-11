// sgtc_c4fire.cpp — build-21: 5 s fire patch after a C4/satchel detonation.
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "weapons.h"
#include "sgtc_c4fire.h"

class CC4Fire : public CBaseEntity
{
public:
	void Spawn( void );
	void Think( void );
	float m_flEnd;
	float m_flScale;
};

LINK_ENTITY_TO_CLASS( sgtc_c4_fire, CC4Fire )

void CC4Fire::Spawn( void )
{
	pev->movetype = MOVETYPE_NONE;
	pev->solid = SOLID_NOT;
	pev->gravity = 0;
	pev->effects |= EF_NODRAW;		// temp entities are the visible part
	if( m_flScale <= 0.0f ) m_flScale = 12.0f;
	m_flEnd = gpGlobals->time + 5.0f;
	SetThink( &CC4Fire::Think );
	pev->nextthink = gpGlobals->time + 0.1f;
}

void CC4Fire::Think( void )
{
	if( gpGlobals->time >= m_flEnd )
	{
		UTIL_Remove( this );
		return;
	}
	// flame sprite
	MESSAGE_BEGIN( MSG_PAS, SVC_TEMPENTITY, pev->origin );
		WRITE_BYTE( TE_SPRITE );
		WRITE_COORD( pev->origin.x + RANDOM_FLOAT( -6, 6 ) );
		WRITE_COORD( pev->origin.y + RANDOM_FLOAT( -6, 6 ) );
		WRITE_COORD( pev->origin.z + RANDOM_FLOAT( 0, 10 ) );
		WRITE_SHORT( g_sModelIndexFireball );
		WRITE_BYTE( (int)RANDOM_LONG( (int)( m_flScale * 0.6f ), (int)m_flScale ) );
		WRITE_BYTE( 200 );
	MESSAGE_END();
	// flickering orange light
	MESSAGE_BEGIN( MSG_PAS, SVC_TEMPENTITY, pev->origin );
		WRITE_BYTE( TE_DLIGHT );
		WRITE_COORD( pev->origin.x );
		WRITE_COORD( pev->origin.y );
		WRITE_COORD( pev->origin.z + 8.0f );
		WRITE_BYTE( (int)RANDOM_LONG( 100, 140 ) );
		WRITE_BYTE( 255 );
		WRITE_BYTE( (int)RANDOM_LONG( 90, 150 ) );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 1 );
		WRITE_BYTE( 300 );
	MESSAGE_END();
	if( g_sModelIndexSmoke > 0 )
	{
		MESSAGE_BEGIN( MSG_PAS, SVC_TEMPENTITY, pev->origin );
			WRITE_BYTE( TE_SMOKE );
			WRITE_SHORT( g_sModelIndexSmoke );
			WRITE_COORD( pev->origin.x + RANDOM_FLOAT( -8, 8 ) );
			WRITE_COORD( pev->origin.y + RANDOM_FLOAT( -8, 8 ) );
			WRITE_COORD( pev->origin.z + RANDOM_FLOAT( 4, 16 ) );
			WRITE_BYTE( (int)RANDOM_LONG( 3, 6 ) );
			WRITE_BYTE( (int)RANDOM_LONG( 20, 40 ) );
		MESSAGE_END();
	}
	pev->nextthink = gpGlobals->time + 0.15f;
}

void SGTC_FirePatch_Spawn( const Vector &vecOrigin, float flLife, float flScale )
{
	CC4Fire *p = (CC4Fire *)CBaseEntity::Create( "sgtc_c4_fire", vecOrigin, g_vecZero, NULL );
	if( !p )
		return;
	p->m_flEnd = gpGlobals->time + ( flLife > 0.0f ? flLife : 5.0f );
	p->m_flScale = flScale > 0.0f ? flScale : 12.0f;
	UTIL_LogPrintf( "SGTCEVENT {\"event\":\"fire_patch\",\"origin\":[%.1f,%.1f,%.1f],\"life\":%.1f}\n",
		vecOrigin.x, vecOrigin.y, vecOrigin.z, p->m_flEnd - gpGlobals->time );
}

void SGTC_C4Fire_Spawn( const Vector &vecOrigin )
{
	SGTC_FirePatch_Spawn( vecOrigin, 5.0f, 12.0f );
}
