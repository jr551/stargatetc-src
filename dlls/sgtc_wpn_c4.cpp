// sgtc_wpn_c4.cpp — weapon_c4 (SGTC link in sgtc_links.cpp) over the stock satchel.
// Throw() spends one charge, so a primary press that really throws shows up as a
// drop in ammo. That press plays the shared throw event; detonating or a no-op
// press plays nothing.
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "sgtc_weapons.h"
#include "sgtc_throwfx.h"
#include "sgtc_wpn_c4.h"

void CSgtcC4::Precache( void )
{
	CSatchel::Precache();
	SGTC_ThrowFxPrecache();
}

void CSgtcC4::PrimaryAttack( void )
{
	const int iBefore = m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
	CSatchel::PrimaryAttack();
	if( m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] < iBefore )
		SGTC_PlayThrowFx( m_pPlayer, m_pPlayer->GetGunPosition(), 0.5f, SGTC_WPN_SATCHEL );
}
