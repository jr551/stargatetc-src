// sgtc_weapons_client.cpp — StargateTC client weapon prediction table (NEW).
//
// See sgtc_weapons_client.h for truth sources. Ammo registry names marked
// TBD need the server peer (EventualPlanarian sgtc_weapons.*) to confirm the
// AddAmmoNameToAmmoRegistry strings; slots/IDs/models/events are exact.

#include "hud.h"
#include "cl_util.h"
#include "ammohistory.h"
#include "sgtc_weapons_client.h"

// Every field below is REA-verified against GetItemInfo in ref/stc_i386.so
// (2.0 server). pszAmmo1/pszAmmo2 are the literal .rodata strings; -1 in
// maxClip/iMaxAmmo means the ref's 0xffffffff (WEAPON_NOCLIP / unlimited).
// slot=iSlot(+0), pos=iPosition(+4), id=iId(+0x20); iFlags(+0x24)/iWeight
// (+0x28) are not modeled here.
const SGTCClientWeaponInfo g_SGTCClientWeapons[] =
{
	// link, id, slot, pos, ammo1, maxAmmo1, ammo2, maxAmmo2, maxClip, v_model, p_model, event
	{ "weapon_batsup", SGTC_WPN_BATSUP, 0, 2, NULL, -1, NULL, -1, -1, "models/v_batsup.mdl", "models/p_batsup.mdl", "events/batsup.sc" },
	{ "weapon_coujaf", SGTC_WPN_COUJAF, 0, 0, NULL, -1, NULL, -1, -1, "models/v_coujaf.mdl", "models/p_coujaf.mdl", "events/coujaf.sc" },
	{ "weapon_couteau", SGTC_WPN_COUTEAU, 0, 1, NULL, 3, NULL, -1, 0, "models/v_couteau.mdl", "models/p_couteau.mdl", "events/couteau.sc" },
	{ "weapon_m92s", SGTC_WPN_M92S, 1, 3, "9mmC", 60, NULL, -1, 12, "models/v_m92s.mdl", "models/p_m92s.mdl", "events/eagle.sc" },
	{ "weapon_flashbang", SGTC_WPN_FLASHBANG, 5, 5, "Flash Grenade", 10, NULL, -1, -1, "models/v_grenade.mdl", "models/p_grenade.mdl", NULL },
	{ "weapon_gacgoa", SGTC_WPN_GACGOA, 5, 4, "GrenadeChoc", 4, NULL, -1, -1, "models/v_gacgoa.mdl", "models/p_gacgoa.mdl", NULL },
	{ "weapon_glstaff", SGTC_WPN_BIGLANCE, 3, 5, "EnergieGL", 22, NULL, -1, -1, "models/v_deathglider.mdl", "models/p_deathglider.mdl", "events/biglance.sc" },
	{ "weapon_goaregen", SGTC_WPN_GOAREGEN, 3, 4, "goaregen", 150, NULL, -1, -1, "models/v_goaregen.mdl", "models/p_goaregen.mdl", NULL },
	{ "weapon_handgrenade", SGTC_WPN_HANDGRENADE, 6, 1, "Hand Grenade", 3, NULL, -1, -1, "models/v_grenade.mdl", "models/p_grenade.mdl", NULL },
	{ "weapon_invis", SGTC_WPN_INVIS, 4, 3, "Invis", 10, "Invis", 10, -1, "models/v_invis.mdl", "models/p_invis.mdl", "events/invis.sc" },
	{ "weapon_lance", SGTC_WPN_LANCE, 3, 2, "Energie3", 22, NULL, -1, -1, "models/v_lancegoa.mdl", "models/p_lancegoa.mdl", "events/lance.sc" },
	{ "weapon_larve", SGTC_WPN_LARVE, 5, 1, "Larve", 2, NULL, -1, -1, "models/v_larve.mdl", "models/p_larve.mdl", "events/larve.sc" },
	{ "weapon_m16", SGTC_WPN_M16, 2, 1, "m16 ammo", 210, NULL, -1, 30, "models/v_m16.mdl", "models/p_m16.mdl", "events/m16.sc" },
	{ "weapon_main", SGTC_WPN_MAIN, 3, 1, "Energie", 100, NULL, -1, -1, "models/v_main.mdl", "models/p_main.mdl", "events/main.sc" },
	{ "weapon_medic", SGTC_WPN_MEDIC, 6, 0, "medgoo", 200, NULL, -1, -1, "models/v_medic.mdl", "models/p_medic.mdl", NULL },
	{ "weapon_mine", SGTC_WPN_TRIPMINE, 5, 3, "Trip Mine", 2, NULL, -1, -1, "models/v_mine.mdl", "models/p_mine.mdl", NULL },
	{ "weapon_mp5", SGTC_WPN_MP5, 2, 2, "Mp5", 200, NULL, -1, 30, "models/v_mp5.mdl", "models/p_mp5.mdl", "events/mp5.sc" },
	{ "weapon_mustardgrenade", SGTC_WPN_MUSTARDGRENADE, 6, 5, "Mustard Grenade", 1, NULL, -1, -1, "models/v_mustardgrenade.mdl", "models/p_mustardgrenade.mdl", NULL },
	{ "weapon_narcogrenade", SGTC_WPN_NARCOGRENADE, 6, 4, "Narco Grenade", 1, NULL, -1, -1, "models/v_narcogrenade.mdl", "models/p_narcogrenade.mdl", NULL },
	{ "weapon_nervegrenade", SGTC_WPN_NERVEGRENADE, 6, 3, "Nerve Grenade", 1, NULL, -1, -1, "models/v_nervegrenade.mdl", "models/p_nervegrenade.mdl", NULL },
	{ "weapon_p90", SGTC_WPN_P90, 2, 0, "p90 ammo", 250, NULL, -1, 50, "models/v_p90.mdl", "models/p_p90.mdl", "events/p90.sc" },
	{ "weapon_psg1", SGTC_WPN_PSG1, 2, 4, "psg1 clip", 50, NULL, -1, 10, "models/v_psg1.mdl", "models/p_psg1.mdl", "events/psg1.sc" },
	{ "weapon_reetou", SGTC_WPN_REETOU, 4, 4, "TER", 10, NULL, -1, -1, "models/v_reetou.mdl", "models/p_reetou.mdl", "events/reetou.sc" },
	// #218: weapon_shield (id 25) was the only linked server weapon with no
	// client row. The HUD fallback in CHudAmmo::MsgFunc_CurWeapon and the
	// viewmodel fallback in HUD_WeaponsPostThink are both table-driven, so the
	// shield had no HUD record and no viewmodel mapping. Fields mirror
	// CShieldGun::GetItemInfo (slot 4, pos 0, no ammo, no clip) and Deploy()
	// (models/v_shield.mdl + models/p_shield.mdl).
	{ "weapon_shield", SGTC_WPN_SHIELD, 4, 0, NULL, -1, NULL, -1, -1, "models/v_shield.mdl", "models/p_shield.mdl", "events/shield.sc" },
	{ "weapon_smokegrenade", SGTC_WPN_SMOKEGRENADE, 6, 2, "Smoke Grenade", 1, NULL, -1, -1, "models/v_smokegrenade.mdl", "models/p_smokegrenade.mdl", NULL },
	{ "weapon_spas", SGTC_WPN_SHOTGUN_STOCK, 2, 3, "buckshot", 125, NULL, -1, 8, "models/v_spas.mdl", "models/p_spas.mdl", "events/spas.sc" },
	{ "weapon_tacgun", SGTC_WPN_TAC, 4, 2, "Tacgun", 3, NULL, -1, -1, "models/v_tac.mdl", "models/p_tac.mdl", "events/tac.sc" },
	{ "weapon_usas", SGTC_WPN_USAS, 2, 5, "buckshot", 125, NULL, -1, 10, "models/v_usas.mdl", "models/p_usas.mdl", "events/usas.sc" },
	{ "weapon_zat", SGTC_WPN_ZAT, 1, 2, "Energie2", 100, NULL, -1, -1, "models/v_zat.mdl", "models/p_zat.mdl", "events/zata.sc" },
	{ "weapon_zatarc", SGTC_WPN_ZATARC, 3, 0, "Energie5", 40, NULL, -1, -1, "models/v_zar.mdl", "models/p_zar.mdl", "events/zatarc.sc" },
	{ "weapon_c4", SGTC_WPN_SATCHEL, 5, 2, "Satchel Charge", 2, NULL, -1, -1, "models/v_c4.mdl", "models/p_c4.mdl", NULL },
	// weapon_9mmAR binds to the same CMP5 class as weapon_mp5 (both link thunks
	// call GetClassPtr<CMP5>), so its effective ItemInfo row is identical:
	// iId=4 ("Mp5", 200, clip 30). There is no separate 9mmAR id (31 is
	// WEAPON_SUIT and must never be a weapon id).
	{ "weapon_9mmAR", SGTC_WPN_MP5, 2, 2, "Mp5", 200, NULL, -1, 30, "models/v_mp5.mdl", "models/p_mp5.mdl", "events/mp5.sc" },
};

const int g_SGTCClientWeaponCount =
	sizeof( g_SGTCClientWeapons ) / sizeof( g_SGTCClientWeapons[0] );

void SGTC_ClientWeaponsInit( void )
{
	// MAX_WEAPONS is 32 (cdll_dll.h, shared game+client). SGTC uses IDs
	// 0-30; the table has one row per linked server weapon (32, with
	// weapon_9mmAR aliasing weapon_mp5's id, so 31 distinct ids).
	//
	// #218: seed the HUD weapon records from this table instead of waiting
	// for a WeaponList message. The server sends WeaponList for every
	// ItemInfo row, but a late join or a missed message used to leave an
	// SGTC weapon with no HUD record at all. These rows are advisory: a
	// later WeaponList overwrites them with the server's exact ammo types.
	int iRegistered = 0;
	bool bSeenId[MAX_WEAPONS] = { false };

	for( int i = 0; i < g_SGTCClientWeaponCount; i++ )
	{
		const SGTCClientWeaponInfo *pInfo = &g_SGTCClientWeapons[i];

		// The slot grid is rgSlots[MAX_WEAPON_SLOTS + 1][MAX_WEAPON_POSITIONS + 1];
		// an out-of-range row would corrupt the HUD, so reject it here.
		if( pInfo->iId < 0 || pInfo->iId >= MAX_WEAPONS ||
			pInfo->iSlot < 0 || pInfo->iSlot > MAX_WEAPON_SLOTS ||
			pInfo->iPos < 0 || pInfo->iPos > MAX_WEAPON_POSITIONS )
		{
			gEngfuncs.Con_Printf( "SGTC: bad client weapon row %s (id %d slot %d pos %d)\n",
				pInfo->linkName, pInfo->iId, pInfo->iSlot, pInfo->iPos );
			continue;
		}

		// weapon_9mmAR aliases weapon_mp5's CMP5 class and id; seed the first
		// link name only (it is the one the server reports in WeaponList).
		if( bSeenId[pInfo->iId] )
			continue;
		bSeenId[pInfo->iId] = true;

		// A WeaponList record from the server is authoritative (exact ammo
		// registry indices); never overwrite it on a later VidInit.
		if( WeaponsResource::IsRegistered( gWR.GetWeapon( pInfo->iId )))
			continue;

		WEAPON wp;
		memset( &wp, 0, sizeof( wp ));
		strlcpy( wp.szName, pInfo->linkName, sizeof( wp.szName ));
		wp.iId = pInfo->iId;
		wp.iSlot = pInfo->iSlot;
		wp.iSlotPos = pInfo->iPos;
		wp.iAmmoType = -1;
		wp.iAmmo2Type = -1;
		wp.iMax1 = pInfo->iMaxAmmo1;
		wp.iMax2 = pInfo->iMaxAmmo2;
		wp.iClip = 0;
		gWR.AddWeapon( &wp );
		iRegistered++;
	}

	if( iRegistered )
		gEngfuncs.Con_Printf( "SGTC: %d client weapons registered\n", iRegistered );
}

const char *SGTC_ClientViewModelForId( int iId )
{
	const SGTCClientWeaponInfo *pInfo = SGTC_ClientWeaponById( iId );
	return pInfo ? pInfo->pszViewModel : NULL;
}

const SGTCClientWeaponInfo *SGTC_ClientWeaponById( int iId )
{
	for( int i = 0; i < g_SGTCClientWeaponCount; i++ )
		if( g_SGTCClientWeapons[i].iId == iId )
			return &g_SGTCClientWeapons[i];
	return NULL;
}
