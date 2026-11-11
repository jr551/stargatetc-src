// sgtc_enums.h — team, class and loadout-slot numbering shared by the SGTC
// server (dlls/) and client (cl_dll/). Audit issue #95.
//
// Plain header: no engine, util or player includes, so both DLLs can take it.
// Every value below is on the wire (usermessages, client commands) or in the
// original client, so the NUMBERS MUST NOT CHANGE. Only the names are new.
#pragma once
#ifndef SGTC_ENUMS_H
#define SGTC_ENUMS_H

// ---------------------------------------------------------------------------
// Teams. The mod uses TWO team numberings and they are opposite of each other.
// Both are fixed by the original binary and both are read by the client, so
// neither can be renumbered on its own. A value of one numbering must never be
// stored in or compared with a field of the other; cross between them only with
// SGTC_TeamToEntTeam() / SGTC_EntTeamToTeam() below.
//
// SgtcTeam ("pick" numbering) — 1 = Torri (Tau'ri), 2 = Goa'uld.
//   Server: CBasePlayer::m_iSgtcTeam / m_iSgtcActiveTeam / m_iSgtcPrevTeam,
//           the team argument of the "classe" and "jointeam" client commands,
//           and GetTeamIndex( name ) + 1 (the team list is "Torri;Goa'uld").
//   Wire:   ScoreInfo team slot, the TeamNames name order (slot = index + 1),
//           the "classe <class> <team>" command the client menu sends.
//   Unas mode ("Unas;Marine" team list) reuses the same two slots.
//
// SgtcEntTeam (entity numbering) — 1 = Goa'uld, 2 = Torri.
//   Server: pev->team (set from the team NAME in PlayerSpawn and copied onto
//           projectiles, monsters and deployables), the per-team
//           SetWeaponAllow tables, UpdateClassCount, Goal().
//   Wire:   AddPortrait team field, ScoreInfo trailing short, SendAboutMe.
// ---------------------------------------------------------------------------
enum SgtcTeam
{
	SGTC_TEAM_NONE   = 0,
	SGTC_TEAM_TORRI  = 1,
	SGTC_TEAM_GOAULD = 2,
	SGTC_TEAM_UNAS   = 1,	// Unas mode: 1 = Unas
	SGTC_TEAM_MARINE = 2	// Unas mode: 2 = Marine
};

enum SgtcEntTeam
{
	SGTC_ENTTEAM_NONE   = 0,
	SGTC_ENTTEAM_GOAULD = 1,
	SGTC_ENTTEAM_TORRI  = 2
};

// The only place the two team numberings are converted. Anything that is not
// one of the two real teams maps to "none" (0) in both directions.
static inline int SGTC_TeamToEntTeam( int iTeam )
{
	if( iTeam == SGTC_TEAM_TORRI )
		return SGTC_ENTTEAM_TORRI;
	if( iTeam == SGTC_TEAM_GOAULD )
		return SGTC_ENTTEAM_GOAULD;
	return SGTC_ENTTEAM_NONE;
}

static inline int SGTC_EntTeamToTeam( int iEntTeam )
{
	if( iEntTeam == SGTC_ENTTEAM_TORRI )
		return SGTC_TEAM_TORRI;
	if( iEntTeam == SGTC_ENTTEAM_GOAULD )
		return SGTC_TEAM_GOAULD;
	return SGTC_TEAM_NONE;
}

// ---------------------------------------------------------------------------
// Classes. A class is a slot 1..5 inside a team: the number alone means
// nothing until it is paired with a team. CBasePlayer::m_iSgtcPortraitShown
// holds the class in play, m_iSgtcPortraitSel the pending pick (0 = none).
// Same value on the wire: the "classe <class> <team>" command, the ScoreInfo
// class slot, the AddPortrait class field.
//   Goa'uld: 1 systemlord, 2 anubis (counted as "prime"), 3 jaffa,
//            4 serpent (snakegoauld), 5 horus
//   Torri:   1 oneill, 2 carter, 3 jackson, 4 tealc, 5 commander
// ---------------------------------------------------------------------------
enum SgtcClass
{
	SGTC_CLASS_NONE      = 0,
	SGTC_CLASS_FIRST     = 1,
	// Goa'uld (SgtcTeam SGTC_TEAM_GOAULD / SgtcEntTeam SGTC_ENTTEAM_GOAULD)
	SGTC_CLASS_SYSLORD   = 1,
	SGTC_CLASS_ANUBIS    = 2,
	SGTC_CLASS_JAFFA     = 3,
	SGTC_CLASS_SERPENT   = 4,
	SGTC_CLASS_HORUS     = 5,
	// Torri (SgtcTeam SGTC_TEAM_TORRI / SgtcEntTeam SGTC_ENTTEAM_TORRI)
	SGTC_CLASS_ONEILL    = 1,
	SGTC_CLASS_CARTER    = 2,
	SGTC_CLASS_JACKSON   = 3,
	SGTC_CLASS_TEALC     = 4,
	SGTC_CLASS_COMMANDER = 5,
	SGTC_CLASS_LAST      = 5,
	SGTC_NUM_CLASSES     = 5,	// classes per team
	// Not a pick: the ScoreInfo class slot sent while m_bSnagInvisLatch is set
	// (Unas / invisible player).
	SGTC_CLASS_UNAS      = 6
};

// ---------------------------------------------------------------------------
// Loadout weapon slots. The buy menu ("gond4h9wp" command, TeamNames cost tail,
// Progress state bytes, CBasePlayer::m_iProgressTeam / m_iSgtcGondArgs, the
// wpnC0..wpnC27 cost cvars) addresses weapons by a slot index 0..27. This is
// NOT the HUD bucket (iSlot/iPosition in ItemInfo).
// ---------------------------------------------------------------------------
enum SgtcWeaponSlot
{
	SGTC_WSLOT_GLSTAFF       = 0,
	SGTC_WSLOT_USAS          = 1,
	SGTC_WSLOT_BATSUP        = 2,
	SGTC_WSLOT_M92S          = 3,
	SGTC_WSLOT_MP5           = 4,
	SGTC_WSLOT_PSG1          = 5,
	SGTC_WSLOT_P90           = 6,
	SGTC_WSLOT_SPAS          = 7,
	SGTC_WSLOT_M16           = 8,
	SGTC_WSLOT_MAIN          = 9,
	SGTC_WSLOT_ZAT           = 10,
	SGTC_WSLOT_LANCE         = 11,
	SGTC_WSLOT_REETOU        = 12,
	SGTC_WSLOT_ZATARC        = 13,
	SGTC_WSLOT_TACGUN        = 14,
	SGTC_WSLOT_HANDGRENADE   = 15,
	SGTC_WSLOT_MINE          = 16,
	SGTC_WSLOT_INVIS         = 17,
	SGTC_WSLOT_SHIELD        = 18,
	SGTC_WSLOT_GACGOA        = 19,
	SGTC_WSLOT_C4            = 20,
	SGTC_WSLOT_LARVE         = 21,
	SGTC_WSLOT_MEDIC         = 22,
	SGTC_WSLOT_SMOKEGRENADE  = 23,
	SGTC_WSLOT_NERVEGRENADE  = 24,
	SGTC_WSLOT_NARCOGRENADE  = 25,
	SGTC_WSLOT_MUSTARDGRENADE = 26,
	SGTC_WSLOT_GOAREGEN      = 27,
	SGTC_NUM_WEAPON_SLOTS    = 28,

	// Buy-menu groups. Slots 0..13 are the "primary" group (at most
	// SGTC_WSLOT_MAX_PRIMARY picks); c4 and the four gas grenades are the
	// "special" group (at most SGTC_WSLOT_MAX_SPECIAL picks).
	SGTC_WSLOT_NUM_PRIMARY   = 14,
	SGTC_WSLOT_MAX_PRIMARY   = 3,
	SGTC_WSLOT_MAX_SPECIAL   = 2
};

static inline int SGTC_WeaponSlotIsPrimary( int iSlot )
{
	return iSlot >= 0 && iSlot < SGTC_WSLOT_NUM_PRIMARY;
}

static inline int SGTC_WeaponSlotIsSpecial( int iSlot )
{
	return iSlot == SGTC_WSLOT_C4 ||
		( iSlot >= SGTC_WSLOT_SMOKEGRENADE && iSlot <= SGTC_WSLOT_MUSTARDGRENADE );
}

// ---------------------------------------------------------------------------
// special_teleport_in target classes (server only; audit issue #48). The gate
// looks the touching entity up by classname in this order, so the index IS the
// ref's class number and the table in sgtc_misc.cpp (s_szTeleportClasses) is
// indexed by it. Slot 13 keeps its place with no entity behind it (#98).
// ---------------------------------------------------------------------------
enum SgtcTeleportClass
{
	SGTC_TELE_PLAYER         = 0,	// "player"
	SGTC_TELE_GRENADE        = 1,	// "grenade"
	SGTC_TELE_LARVE          = 2,	// "monster_larve"
	SGTC_TELE_LANCE_PROJ     = 3,	// "lance_proj"
	SGTC_TELE_REETOU_PROJ    = 4,	// "reetou_proj"
	SGTC_TELE_SATCHEL        = 5,	// "monster_satchel"
	SGTC_TELE_PUSHABLE       = 6,	// "func_pushable"
	SGTC_TELE_TRAIN          = 7,	// "func_train"
	SGTC_TELE_TAC            = 8,	// "monster_tac"
	SGTC_TELE_MINE           = 9,	// "monster_mine"
	SGTC_TELE_BREAKABLE      = 10,	// "func_breakable"
	SGTC_TELE_NUKE           = 11,	// "game_nuke"
	SGTC_TELE_BIG_LANCE_PROJ = 12,	// "big_lance_proj"
	SGTC_TELE_TEMP_EJECT     = 13,	// "temp_eject" (no entity behind it)
	SGTC_TELE_ZAT_PROJ       = 14,	// "zat_proj"
	SGTC_TELE_ZATBOOM        = 15,	// "monster_zatboom"
	SGTC_TELE_NUM_CLASSES    = 16
};

// ---------------------------------------------------------------------------
// Named literals (#118). Values are the ones the original binary uses.
// ---------------------------------------------------------------------------

// Round length, in seconds. mp_roundtime is clamped to [MIN, MAX]. The original
// also uses MAX (9:59) as the "ticket mode, never expires" round length: modes
// 1, 2 and 4 and CP types 3 and 4 re-arm the round end to now + MAX, and the
// client hides the timer in ticket mode. The two uses share one value in the
// original binary, so they share one constant here; keep them equal.
#define SGTC_ROUNDTIME_MIN_SEC		30
#define SGTC_ROUNDTIME_MAX_SEC		599
#define SGTC_ROUNDTIME_TICKET_SEC	SGTC_ROUNDTIME_MAX_SEC

// Off-map park point used on all three axes (corpses at NewRound, the "no key
// spawn" item_objective position, the bad-guy trap). CBaseEntity::IsInWorld
// treats any component >= 4096 as outside the world, so this must stay >= 4096.
#define SGTC_OFFMAP_COORD		4096.0f

// Client: an observer origin with x and y above this is treated as parked at
// SGTC_OFFMAP_COORD (margin below 4096 for float/interp drift).
#define SGTC_OFFMAP_CLIENT_THRESHOLD	3500.0f

// Radio client command ("radio <msg>"); the client sends it from the radio menu.
#define SGTC_CMD_RADIO			"radio"

#endif // SGTC_ENUMS_H
