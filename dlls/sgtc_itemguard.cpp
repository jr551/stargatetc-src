// sgtc_itemguard.cpp — build-11: keep dropped/pickup weapon entities out of the
// engine's "Got a velocity too high" warning class.
//
// SV_Phys warns when an entity's total speed exceeds sv_maxvelocity(2000) *
// sqrt(1.73) (~2630) — a diagonal item that picked up excessive velocity from a
// throw/bounce/explosion.  Items never need more than walking speed; this
// low-frequency (0.5 s) server pass clamps any weapon_* entity to 1500 u/s and
// clamps the knife-throw source in sgtc_wpn_couteau.cpp.  It only touches item
// entities, never players or projectiles.

#include "extdll.h"
#include "util.h"
#include "cbase.h"

#define SGTC_ITEM_SPEED_CAP 1500.0f

void SGTC_ItemGuard_Tick( void )
{
	static double s_flNext = 0.0;
	if( gpGlobals->time < s_flNext )
		return;
	s_flNext = gpGlobals->time + 0.5;

	for( int i = 1; i < gpGlobals->maxEntities; ++i )
	{
		edict_t *pEdict = INDEXENT( i );
		if( !pEdict || pEdict->free )
			continue;
		if( !pEdict->v.classname )
			continue;
		const char *pszClass = STRING( pEdict->v.classname );
		if( !pszClass || strncmp( pszClass, "weapon_", 7 ) != 0 )
			continue;

		const float flSpeed = pEdict->v.velocity.Length();
		if( flSpeed > SGTC_ITEM_SPEED_CAP )
			pEdict->v.velocity = pEdict->v.velocity * ( SGTC_ITEM_SPEED_CAP / flSpeed );
	}
}
