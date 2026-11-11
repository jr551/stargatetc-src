/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
#include "extdll.h"
#include "sgtc_testcmd.h"
#include "eiface.h"
#include "util.h"
#include "game.h"
#include "vcs_info.h"
#include "sgtc_gate_dlight.h"
#include "sgtc_sniper.h"	// scope sway cvars / SgtcHit usermsg
#ifdef BUILD_JK_BOTTI
#include "sgtc_bots.h"
#endif

static cvar_t build_commit = { "sv_game_build_commit", g_VCSInfo_Commit };
static cvar_t build_branch = { "sv_game_build_branch", g_VCSInfo_Branch };

cvar_t displaysoundlist = {"displaysoundlist","0"};

// multiplayer server rules
cvar_t fragsleft	= { "mp_fragsleft","0", FCVAR_SERVER | FCVAR_UNLOGGED };	  // Don't spam console/log files/users with this changing
cvar_t timeleft		= { "mp_timeleft","0" , FCVAR_SERVER | FCVAR_UNLOGGED };	  // "      "

// multiplayer server rules
cvar_t teamplay		= { "mp_teamplay","0", FCVAR_SERVER };
cvar_t fraglimit	= { "mp_fraglimit","0", FCVAR_SERVER };
cvar_t timelimit	= { "mp_timelimit","0", FCVAR_SERVER };
cvar_t friendlyfire	= { "mp_friendlyfire","0", FCVAR_SERVER };
cvar_t falldamage	= { "mp_falldamage","0", FCVAR_SERVER };
cvar_t weaponstay	= { "mp_weaponstay","0", FCVAR_SERVER };
cvar_t selfgauss	= { "selfgauss", "1", FCVAR_SERVER };
cvar_t chargerfix	= { "chargerfix", "0", FCVAR_SERVER };
cvar_t satchelfix	= { "satchelfix", "1", FCVAR_SERVER };
cvar_t explosionfix	= { "explosionfix", "0", FCVAR_SERVER };
cvar_t monsteryawspeedfix	= { "monsteryawspeedfix", "1", FCVAR_SERVER };
cvar_t corpsephysics = { "corpsephysics", "0", FCVAR_SERVER };
extern cvar_t sgtc_maxcorpse;	// world.cpp — ref maxcorpse "mp_maxcorpse"
cvar_t resetflagtime = { "resetflagtime", "15", FCVAR_SERVER };
cvar_t pushablemode = { "pushablemode", "0", FCVAR_SERVER };
cvar_t forcerespawn	= { "mp_forcerespawn","1", FCVAR_SERVER };
cvar_t flashlight	= { "mp_flashlight","1", FCVAR_SERVER };	// ref cvar_t @0x213308 — default '1'
cvar_t aimcrosshair	= { "mp_autocrosshair","1", FCVAR_SERVER };
cvar_t decalfrequency	= { "decalfrequency","30", FCVAR_SERVER };
cvar_t teamlist		= { "mp_teamlist","hgrunt;scientist", FCVAR_SERVER };
cvar_t teamoverride	= { "mp_teamoverride","1" };
cvar_t defaultteam	= { "mp_defaultteam","0" };
cvar_t allowmonsters	= { "mp_allowmonsters","1", FCVAR_SERVER };	// ref cvar_t @0x213290 — default '1'
cvar_t bhopcap		= { "mp_bhopcap", "1", FCVAR_SERVER };

cvar_t allow_spectators = { "allow_spectators", "1.0", FCVAR_SERVER };	// ref cvar_t @0x213934 — default '1.0' (0 prevents players from being spectators)
cvar_t multibyte_only = { "mp_multibyte_only", "0", FCVAR_SERVER };

cvar_t mp_chattime	= { "mp_chattime","10", FCVAR_SERVER };

// Engine Cvars
cvar_t *g_psv_gravity;
cvar_t *g_psv_aim;
cvar_t *g_psv_allow_autoaim;
cvar_t *g_footsteps;
cvar_t *g_enable_cheats;

cvar_t *g_psv_developer;

//CVARS FOR SKILL LEVEL SETTINGS

// Barney
cvar_t	sk_barney_health1 = {"sk_barney_health1","0"};
cvar_t	sk_barney_health2 = {"sk_barney_health2","0"};
cvar_t	sk_barney_health3 = {"sk_barney_health3","0"};

// Gargantua
cvar_t	sk_gargantua_health1 = {"sk_gargantua_health1","0"};
cvar_t	sk_gargantua_health2 = {"sk_gargantua_health2","0"};
cvar_t	sk_gargantua_health3 = {"sk_gargantua_health3","0"};

cvar_t	sk_gargantua_dmg_slash1	= {"sk_gargantua_dmg_slash1","0"};
cvar_t	sk_gargantua_dmg_slash2	= {"sk_gargantua_dmg_slash2","0"};
cvar_t	sk_gargantua_dmg_slash3	= {"sk_gargantua_dmg_slash3","0"};

cvar_t	sk_gargantua_dmg_fire1 = {"sk_gargantua_dmg_fire1","0"};
cvar_t	sk_gargantua_dmg_fire2 = {"sk_gargantua_dmg_fire2","0"};
cvar_t	sk_gargantua_dmg_fire3 = {"sk_gargantua_dmg_fire3","0"};

cvar_t	sk_gargantua_dmg_stomp1	= {"sk_gargantua_dmg_stomp1","0"};
cvar_t	sk_gargantua_dmg_stomp2	= {"sk_gargantua_dmg_stomp2","0"};
cvar_t	sk_gargantua_dmg_stomp3	= {"sk_gargantua_dmg_stomp3","0"};


// Headcrab
cvar_t	sk_headcrab_health1 = {"sk_headcrab_health1","0"};
cvar_t	sk_headcrab_health2 = {"sk_headcrab_health2","0"};
cvar_t	sk_headcrab_health3 = {"sk_headcrab_health3","0"};

cvar_t	sk_headcrab_dmg_bite1 = {"sk_headcrab_dmg_bite1","0"};
cvar_t	sk_headcrab_dmg_bite2 = {"sk_headcrab_dmg_bite2","0"};
cvar_t	sk_headcrab_dmg_bite3 = {"sk_headcrab_dmg_bite3","0"};


// ISlave
cvar_t	sk_islave_health1 = {"sk_islave_health1","0"};
cvar_t	sk_islave_health2 = {"sk_islave_health2","0"};
cvar_t	sk_islave_health3 = {"sk_islave_health3","0"};

cvar_t	sk_islave_dmg_claw1 = {"sk_islave_dmg_claw1","0"};
cvar_t	sk_islave_dmg_claw2 = {"sk_islave_dmg_claw2","0"};
cvar_t	sk_islave_dmg_claw3 = {"sk_islave_dmg_claw3","0"};

cvar_t	sk_islave_dmg_clawrake1	= {"sk_islave_dmg_clawrake1","0"};
cvar_t	sk_islave_dmg_clawrake2	= {"sk_islave_dmg_clawrake2","0"};
cvar_t	sk_islave_dmg_clawrake3	= {"sk_islave_dmg_clawrake3","0"};
	
cvar_t	sk_islave_dmg_zap1 = {"sk_islave_dmg_zap1","0"};
cvar_t	sk_islave_dmg_zap2 = {"sk_islave_dmg_zap2","0"};
cvar_t	sk_islave_dmg_zap3 = {"sk_islave_dmg_zap3","0"};


// Scientist
cvar_t	sk_scientist_health1 = {"sk_scientist_health1","0"};
cvar_t	sk_scientist_health2 = {"sk_scientist_health2","0"};
cvar_t	sk_scientist_health3 = {"sk_scientist_health3","0"};

// Snark
cvar_t	sk_snark_health1 = {"sk_snark_health1","0"};
cvar_t	sk_snark_health2 = {"sk_snark_health2","0"};
cvar_t	sk_snark_health3 = {"sk_snark_health3","0"};

cvar_t	sk_snark_dmg_pop1 = {"sk_snark_dmg_pop1","0"};
cvar_t	sk_snark_dmg_pop2 = {"sk_snark_dmg_pop2","0"};
cvar_t	sk_snark_dmg_pop3 = {"sk_snark_dmg_pop3","0"};

// Zombie
cvar_t	sk_zombie_health1 = {"sk_zombie_health1","0"};
cvar_t	sk_zombie_health2 = {"sk_zombie_health2","0"};
cvar_t	sk_zombie_health3 = {"sk_zombie_health3","0"};

cvar_t	sk_zombie_dmg_one_slash1 = {"sk_zombie_dmg_one_slash1","0"};
cvar_t	sk_zombie_dmg_one_slash2 = {"sk_zombie_dmg_one_slash2","0"};
cvar_t	sk_zombie_dmg_one_slash3 = {"sk_zombie_dmg_one_slash3","0"};

cvar_t	sk_zombie_dmg_both_slash1 = {"sk_zombie_dmg_both_slash1","0"};
cvar_t	sk_zombie_dmg_both_slash2 = {"sk_zombie_dmg_both_slash2","0"};
cvar_t	sk_zombie_dmg_both_slash3 = {"sk_zombie_dmg_both_slash3","0"};

//Turret
cvar_t	sk_turret_health1 = {"sk_turret_health1","0"};
cvar_t	sk_turret_health2 = {"sk_turret_health2","0"};
cvar_t	sk_turret_health3 = {"sk_turret_health3","0"};

// MiniTurret
cvar_t	sk_miniturret_health1 = {"sk_miniturret_health1","0"};
cvar_t	sk_miniturret_health2 = {"sk_miniturret_health2","0"};
cvar_t	sk_miniturret_health3 = {"sk_miniturret_health3","0"};

// Sentry Turret
cvar_t	sk_sentry_health1 = {"sk_sentry_health1","0"};
cvar_t	sk_sentry_health2 = {"sk_sentry_health2","0"};
cvar_t	sk_sentry_health3 = {"sk_sentry_health3","0"};

// PLAYER WEAPONS

// Crowbar whack
cvar_t	sk_plr_crowbar1 = {"sk_plr_crowbar1","0"};
cvar_t	sk_plr_crowbar2 = {"sk_plr_crowbar2","0"};
cvar_t	sk_plr_crowbar3 = {"sk_plr_crowbar3","0"};

// Glock Round
cvar_t	sk_plr_9mm_bullet1 = {"sk_plr_9mm_bullet1","0"};
cvar_t	sk_plr_9mm_bullet2 = {"sk_plr_9mm_bullet2","0"};
cvar_t	sk_plr_9mm_bullet3 = {"sk_plr_9mm_bullet3","0"};

// 357 Round
cvar_t	sk_plr_357_bullet1 = {"sk_plr_357_bullet1","0"};
cvar_t	sk_plr_357_bullet2 = {"sk_plr_357_bullet2","0"};
cvar_t	sk_plr_357_bullet3 = {"sk_plr_357_bullet3","0"};

// MP5 Round
cvar_t	sk_plr_9mmAR_bullet1 = {"sk_plr_9mmAR_bullet1","0"};
cvar_t	sk_plr_9mmAR_bullet2 = {"sk_plr_9mmAR_bullet2","0"};
cvar_t	sk_plr_9mmAR_bullet3 = {"sk_plr_9mmAR_bullet3","0"};


// M203 grenade
cvar_t	sk_plr_9mmAR_grenade1 = {"sk_plr_9mmAR_grenade1","0"};
cvar_t	sk_plr_9mmAR_grenade2 = {"sk_plr_9mmAR_grenade2","0"};
cvar_t	sk_plr_9mmAR_grenade3 = {"sk_plr_9mmAR_grenade3","0"};


// Shotgun buckshot
cvar_t	sk_plr_buckshot1 = {"sk_plr_buckshot1","0"};
cvar_t	sk_plr_buckshot2 = {"sk_plr_buckshot2","0"};
cvar_t	sk_plr_buckshot3 = {"sk_plr_buckshot3","0"};


// Hand Grendade
cvar_t	sk_plr_hand_grenade1 = {"sk_plr_hand_grenade1","0"};
cvar_t	sk_plr_hand_grenade2 = {"sk_plr_hand_grenade2","0"};
cvar_t	sk_plr_hand_grenade3 = {"sk_plr_hand_grenade3","0"};


// Satchel Charge
cvar_t	sk_plr_satchel1	= {"sk_plr_satchel1","0"};
cvar_t	sk_plr_satchel2	= {"sk_plr_satchel2","0"};
cvar_t	sk_plr_satchel3	= {"sk_plr_satchel3","0"};


// Tripmine
cvar_t	sk_plr_tripmine1 = {"sk_plr_tripmine1","0"};
cvar_t	sk_plr_tripmine2 = {"sk_plr_tripmine2","0"};
cvar_t	sk_plr_tripmine3 = {"sk_plr_tripmine3","0"};


// WORLD WEAPONS
cvar_t	sk_12mm_bullet1 = {"sk_12mm_bullet1","0"};
cvar_t	sk_12mm_bullet2 = {"sk_12mm_bullet2","0"};
cvar_t	sk_12mm_bullet3 = {"sk_12mm_bullet3","0"};

cvar_t	sk_9mmAR_bullet1 = {"sk_9mmAR_bullet1","0"};
cvar_t	sk_9mmAR_bullet2 = {"sk_9mmAR_bullet2","0"};
cvar_t	sk_9mmAR_bullet3 = {"sk_9mmAR_bullet3","0"};

cvar_t	sk_9mm_bullet1 = {"sk_9mm_bullet1","0"};
cvar_t	sk_9mm_bullet2 = {"sk_9mm_bullet2","0"};
cvar_t	sk_9mm_bullet3 = {"sk_9mm_bullet3","0"};


// HEALTH/CHARGE
cvar_t	sk_suitcharger1	= { "sk_suitcharger1","0" };
cvar_t	sk_suitcharger2	= { "sk_suitcharger2","0" };		
cvar_t	sk_suitcharger3	= { "sk_suitcharger3","0" };		

cvar_t	sk_battery1	= { "sk_battery1","0" };			
cvar_t	sk_battery2	= { "sk_battery2","0" };			
cvar_t	sk_battery3	= { "sk_battery3","0" };			

cvar_t	sk_healthcharger1	= { "sk_healthcharger1","0" };		
cvar_t	sk_healthcharger2	= { "sk_healthcharger2","0" };		
cvar_t	sk_healthcharger3	= { "sk_healthcharger3","0" };		

cvar_t	sk_healthkit1	= { "sk_healthkit1","0" };		
cvar_t	sk_healthkit2	= { "sk_healthkit2","0" };		
cvar_t	sk_healthkit3	= { "sk_healthkit3","0" };		

cvar_t	sk_scientist_heal1	= { "sk_scientist_heal1","0" };	
cvar_t	sk_scientist_heal2	= { "sk_scientist_heal2","0" };	
cvar_t	sk_scientist_heal3	= { "sk_scientist_heal3","0" };	


// monster damage adjusters
cvar_t	sk_monster_head1	= { "sk_monster_head1","2" };
cvar_t	sk_monster_head2	= { "sk_monster_head2","2" };
cvar_t	sk_monster_head3	= { "sk_monster_head3","2" };

cvar_t	sk_monster_chest1	= { "sk_monster_chest1","1" };
cvar_t	sk_monster_chest2	= { "sk_monster_chest2","1" };
cvar_t	sk_monster_chest3	= { "sk_monster_chest3","1" };

cvar_t	sk_monster_stomach1	= { "sk_monster_stomach1","1" };
cvar_t	sk_monster_stomach2	= { "sk_monster_stomach2","1" };
cvar_t	sk_monster_stomach3	= { "sk_monster_stomach3","1" };

cvar_t	sk_monster_arm1	= { "sk_monster_arm1","1" };
cvar_t	sk_monster_arm2	= { "sk_monster_arm2","1" };
cvar_t	sk_monster_arm3	= { "sk_monster_arm3","1" };

cvar_t	sk_monster_leg1	= { "sk_monster_leg1","1" };
cvar_t	sk_monster_leg2	= { "sk_monster_leg2","1" };
cvar_t	sk_monster_leg3	= { "sk_monster_leg3","1" };

// player damage adjusters
cvar_t	sk_player_head1	= { "sk_player_head1","2" };
cvar_t	sk_player_head2	= { "sk_player_head2","2" };
cvar_t	sk_player_head3	= { "sk_player_head3","2" };

cvar_t	sk_player_chest1 = { "sk_player_chest1","1" };
cvar_t	sk_player_chest2 = { "sk_player_chest2","1" };
cvar_t	sk_player_chest3 = { "sk_player_chest3","1" };

cvar_t	sk_player_stomach1 = { "sk_player_stomach1","1" };
cvar_t	sk_player_stomach2 = { "sk_player_stomach2","1" };
cvar_t	sk_player_stomach3 = { "sk_player_stomach3","1" };

cvar_t	sk_player_arm1	= { "sk_player_arm1","1" };
cvar_t	sk_player_arm2	= { "sk_player_arm2","1" };
cvar_t	sk_player_arm3	= { "sk_player_arm3","1" };

cvar_t	sk_player_leg1	= { "sk_player_leg1","1" };
cvar_t	sk_player_leg2	= { "sk_player_leg2","1" };
cvar_t	sk_player_leg3	= { "sk_player_leg3","1" };

// END Cvars for Skill Level settings

cvar_t sv_pushable_fixed_tick_fudge = { "sv_pushable_fixed_tick_fudge", "15" };
cvar_t sv_busters = { "sv_busters", "0" };
cvar_t sv_armor_metalfx = { "sv_armor_metalfx", "1" };

// Register your console variables here
// This gets called one time when the game is initialied
void GameDLLInit( void )
{
	g_psv_gravity = CVAR_GET_POINTER( "sv_gravity" );
	g_psv_aim = CVAR_GET_POINTER( "sv_aim" );
	g_psv_allow_autoaim = CVAR_GET_POINTER( "sv_allow_autoaim" );
	g_footsteps = CVAR_GET_POINTER( "mp_footsteps" );

	g_psv_developer = CVAR_GET_POINTER( "developer" );

	g_enable_cheats = CVAR_GET_POINTER( "sv_cheats" );

	SGTC_TestInit();
	SGTC_Sniper_Init();
	extern void SGTC_SP_EnemiesInit( void );
	SGTC_SP_EnemiesInit();
	extern void SGTC_PackAutoInit( void );
	SGTC_PackAutoInit();
	extern void SGTC_DiagInit( void );
	SGTC_DiagInit();
	extern void SGTC_ReaDebugInit( void );
	SGTC_ReaDebugInit();

	extern cvar_t sgtc_cvar_team_nocollide;
	SGTC_RegisterOrAdoptCvar( &sgtc_cvar_team_nocollide );
	extern void SGTC_MuzzleFxInit( void );
	SGTC_MuzzleFxInit();

	SGTC_RegisterOrAdoptCvar( &build_commit );
	SGTC_RegisterOrAdoptCvar( &build_branch );

	SGTC_RegisterOrAdoptCvar( &displaysoundlist );
	SGTC_RegisterOrAdoptCvar( &allow_spectators );

	SGTC_RegisterOrAdoptCvar( &teamplay );
	SGTC_RegisterOrAdoptCvar( &fraglimit );
	SGTC_RegisterOrAdoptCvar( &timelimit );

	SGTC_RegisterOrAdoptCvar( &fragsleft );
	SGTC_RegisterOrAdoptCvar( &timeleft );

	SGTC_RegisterOrAdoptCvar( &friendlyfire );
	SGTC_RegisterOrAdoptCvar( &falldamage );
	SGTC_RegisterOrAdoptCvar( &weaponstay );
	SGTC_RegisterOrAdoptCvar( &selfgauss );
	SGTC_RegisterOrAdoptCvar( &chargerfix );
	SGTC_RegisterOrAdoptCvar( &satchelfix );
	SGTC_RegisterOrAdoptCvar( &explosionfix );
	SGTC_RegisterOrAdoptCvar( &monsteryawspeedfix );
	SGTC_RegisterOrAdoptCvar( &corpsephysics );
	SGTC_RegisterOrAdoptCvar( &sgtc_maxcorpse );	// ref GameDLLInit registers maxcorpse "mp_maxcorpse"
	SGTC_RegisterOrAdoptCvar( &resetflagtime );
	SGTC_RegisterOrAdoptCvar( &pushablemode );
	SGTC_RegisterOrAdoptCvar( &forcerespawn );
	SGTC_RegisterOrAdoptCvar( &flashlight );
	SGTC_RegisterOrAdoptCvar( &aimcrosshair );
	SGTC_RegisterOrAdoptCvar( &decalfrequency );
	SGTC_RegisterOrAdoptCvar( &teamlist );
	SGTC_RegisterOrAdoptCvar( &teamoverride );
	SGTC_RegisterOrAdoptCvar( &defaultteam );
	SGTC_RegisterOrAdoptCvar( &allowmonsters );
	SGTC_RegisterOrAdoptCvar( &bhopcap );
	SGTC_RegisterOrAdoptCvar( &multibyte_only );

	SGTC_RegisterOrAdoptCvar( &mp_chattime );
	SGTC_RegisterOrAdoptCvar( &sv_busters );
	SGTC_RegisterOrAdoptCvar( &sv_armor_metalfx );


// REGISTER CVARS FOR SKILL LEVEL STUFF

	// Barney
	SGTC_RegisterOrAdoptCvar( &sk_barney_health1 );// {"sk_barney_health1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_barney_health2 );// {"sk_barney_health2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_barney_health3 );// {"sk_barney_health3","0"};

	// Gargantua
	SGTC_RegisterOrAdoptCvar( &sk_gargantua_health1 );// {"sk_gargantua_health1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_gargantua_health2 );// {"sk_gargantua_health2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_gargantua_health3 );// {"sk_gargantua_health3","0"};

	SGTC_RegisterOrAdoptCvar( &sk_gargantua_dmg_slash1 );// {"sk_gargantua_dmg_slash1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_gargantua_dmg_slash2 );// {"sk_gargantua_dmg_slash2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_gargantua_dmg_slash3 );// {"sk_gargantua_dmg_slash3","0"};

	SGTC_RegisterOrAdoptCvar( &sk_gargantua_dmg_fire1 );// {"sk_gargantua_dmg_fire1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_gargantua_dmg_fire2 );// {"sk_gargantua_dmg_fire2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_gargantua_dmg_fire3 );// {"sk_gargantua_dmg_fire3","0"};

	SGTC_RegisterOrAdoptCvar( &sk_gargantua_dmg_stomp1 );// {"sk_gargantua_dmg_stomp1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_gargantua_dmg_stomp2 );// {"sk_gargantua_dmg_stomp2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_gargantua_dmg_stomp3	);// {"sk_gargantua_dmg_stomp3","0"};

	// Headcrab
	SGTC_RegisterOrAdoptCvar( &sk_headcrab_health1 );// {"sk_headcrab_health1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_headcrab_health2 );// {"sk_headcrab_health2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_headcrab_health3 );// {"sk_headcrab_health3","0"};

	SGTC_RegisterOrAdoptCvar( &sk_headcrab_dmg_bite1 );// {"sk_headcrab_dmg_bite1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_headcrab_dmg_bite2 );// {"sk_headcrab_dmg_bite2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_headcrab_dmg_bite3 );// {"sk_headcrab_dmg_bite3","0"};

	// ISlave
	SGTC_RegisterOrAdoptCvar( &sk_islave_health1 );// {"sk_islave_health1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_islave_health2 );// {"sk_islave_health2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_islave_health3 );// {"sk_islave_health3","0"};

	SGTC_RegisterOrAdoptCvar( &sk_islave_dmg_claw1 );// {"sk_islave_dmg_claw1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_islave_dmg_claw2 );// {"sk_islave_dmg_claw2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_islave_dmg_claw3 );// {"sk_islave_dmg_claw3","0"};

	SGTC_RegisterOrAdoptCvar( &sk_islave_dmg_clawrake1 );// {"sk_islave_dmg_clawrake1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_islave_dmg_clawrake2 );// {"sk_islave_dmg_clawrake2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_islave_dmg_clawrake3 );// {"sk_islave_dmg_clawrake3","0"};

	SGTC_RegisterOrAdoptCvar( &sk_islave_dmg_zap1 );// {"sk_islave_dmg_zap1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_islave_dmg_zap2 );// {"sk_islave_dmg_zap2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_islave_dmg_zap3 );// {"sk_islave_dmg_zap3","0"};

	// Scientist
	SGTC_RegisterOrAdoptCvar( &sk_scientist_health1 );// {"sk_scientist_health1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_scientist_health2 );// {"sk_scientist_health2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_scientist_health3 );// {"sk_scientist_health3","0"};

	// Snark
	SGTC_RegisterOrAdoptCvar( &sk_snark_health1 );// {"sk_snark_health1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_snark_health2 );// {"sk_snark_health2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_snark_health3 );// {"sk_snark_health3","0"};

	SGTC_RegisterOrAdoptCvar( &sk_snark_dmg_pop1 );// {"sk_snark_dmg_pop1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_snark_dmg_pop2 );// {"sk_snark_dmg_pop2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_snark_dmg_pop3 );// {"sk_snark_dmg_pop3","0"};

	// Zombie
	SGTC_RegisterOrAdoptCvar( &sk_zombie_health1 );// {"sk_zombie_health1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_zombie_health2 );// {"sk_zombie_health3","0"};
	SGTC_RegisterOrAdoptCvar( &sk_zombie_health3 );// {"sk_zombie_health3","0"};

	SGTC_RegisterOrAdoptCvar( &sk_zombie_dmg_one_slash1 );// {"sk_zombie_dmg_one_slash1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_zombie_dmg_one_slash2 );// {"sk_zombie_dmg_one_slash2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_zombie_dmg_one_slash3 );// {"sk_zombie_dmg_one_slash3","0"};

	SGTC_RegisterOrAdoptCvar( &sk_zombie_dmg_both_slash1 );// {"sk_zombie_dmg_both_slash1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_zombie_dmg_both_slash2 );// {"sk_zombie_dmg_both_slash2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_zombie_dmg_both_slash3 );// {"sk_zombie_dmg_both_slash3","0"};

	//Turret
	SGTC_RegisterOrAdoptCvar( &sk_turret_health1 );// {"sk_turret_health1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_turret_health2 );// {"sk_turret_health2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_turret_health3 );// {"sk_turret_health3","0"};

	// MiniTurret
	SGTC_RegisterOrAdoptCvar( &sk_miniturret_health1 );// {"sk_miniturret_health1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_miniturret_health2 );// {"sk_miniturret_health2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_miniturret_health3 );// {"sk_miniturret_health3","0"};

	// Sentry Turret
	SGTC_RegisterOrAdoptCvar( &sk_sentry_health1 );// {"sk_sentry_health1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_sentry_health2 );// {"sk_sentry_health2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_sentry_health3 );// {"sk_sentry_health3","0"};


	// PLAYER WEAPONS

	// Crowbar whack
	SGTC_RegisterOrAdoptCvar( &sk_plr_crowbar1 );// {"sk_plr_crowbar1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_crowbar2 );// {"sk_plr_crowbar2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_crowbar3 );// {"sk_plr_crowbar3","0"};

	// Glock Round
	SGTC_RegisterOrAdoptCvar( &sk_plr_9mm_bullet1 );// {"sk_plr_9mm_bullet1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_9mm_bullet2 );// {"sk_plr_9mm_bullet2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_9mm_bullet3 );// {"sk_plr_9mm_bullet3","0"};

	// 357 Round
	SGTC_RegisterOrAdoptCvar( &sk_plr_357_bullet1 );// {"sk_plr_357_bullet1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_357_bullet2 );// {"sk_plr_357_bullet2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_357_bullet3 );// {"sk_plr_357_bullet3","0"};

	// MP5 Round
	SGTC_RegisterOrAdoptCvar( &sk_plr_9mmAR_bullet1 );// {"sk_plr_9mmAR_bullet1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_9mmAR_bullet2 );// {"sk_plr_9mmAR_bullet2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_9mmAR_bullet3 );// {"sk_plr_9mmAR_bullet3","0"};

	// M203 grenade
	SGTC_RegisterOrAdoptCvar( &sk_plr_9mmAR_grenade1 );// {"sk_plr_9mmAR_grenade1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_9mmAR_grenade2 );// {"sk_plr_9mmAR_grenade2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_9mmAR_grenade3 );// {"sk_plr_9mmAR_grenade3","0"};

	// Shotgun buckshot
	SGTC_RegisterOrAdoptCvar( &sk_plr_buckshot1 );// {"sk_plr_buckshot1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_buckshot2 );// {"sk_plr_buckshot2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_buckshot3 );// {"sk_plr_buckshot3","0"};

	// Hand Grendade
	SGTC_RegisterOrAdoptCvar( &sk_plr_hand_grenade1 );// {"sk_plr_hand_grenade1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_hand_grenade2 );// {"sk_plr_hand_grenade2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_hand_grenade3 );// {"sk_plr_hand_grenade3","0"};

	// Satchel Charge
	SGTC_RegisterOrAdoptCvar( &sk_plr_satchel1 );// {"sk_plr_satchel1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_satchel2 );// {"sk_plr_satchel2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_satchel3 );// {"sk_plr_satchel3","0"};

	// Tripmine
	SGTC_RegisterOrAdoptCvar( &sk_plr_tripmine1 );// {"sk_plr_tripmine1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_tripmine2 );// {"sk_plr_tripmine2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_plr_tripmine3 );// {"sk_plr_tripmine3","0"};

	// WORLD WEAPONS
	SGTC_RegisterOrAdoptCvar( &sk_12mm_bullet1 );// {"sk_12mm_bullet1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_12mm_bullet2 );// {"sk_12mm_bullet2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_12mm_bullet3 );// {"sk_12mm_bullet3","0"};

	SGTC_RegisterOrAdoptCvar( &sk_9mmAR_bullet1 );// {"sk_9mm_bullet1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_9mmAR_bullet2 );// {"sk_9mm_bullet1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_9mmAR_bullet3 );// {"sk_9mm_bullet1","0"};

	SGTC_RegisterOrAdoptCvar( &sk_9mm_bullet1 );// {"sk_9mm_bullet1","0"};
	SGTC_RegisterOrAdoptCvar( &sk_9mm_bullet2 );// {"sk_9mm_bullet2","0"};
	SGTC_RegisterOrAdoptCvar( &sk_9mm_bullet3 );// {"sk_9mm_bullet3","0"};

	// HEALTH/SUIT CHARGE DISTRIBUTION
	SGTC_RegisterOrAdoptCvar( &sk_suitcharger1 );
	SGTC_RegisterOrAdoptCvar( &sk_suitcharger2 );
	SGTC_RegisterOrAdoptCvar( &sk_suitcharger3 );

	SGTC_RegisterOrAdoptCvar( &sk_battery1 );
	SGTC_RegisterOrAdoptCvar( &sk_battery2 );
	SGTC_RegisterOrAdoptCvar( &sk_battery3 );

	SGTC_RegisterOrAdoptCvar( &sk_healthcharger1 );
	SGTC_RegisterOrAdoptCvar( &sk_healthcharger2 );
	SGTC_RegisterOrAdoptCvar( &sk_healthcharger3 );

	SGTC_RegisterOrAdoptCvar( &sk_healthkit1 );
	SGTC_RegisterOrAdoptCvar( &sk_healthkit2 );
	SGTC_RegisterOrAdoptCvar( &sk_healthkit3 );

	SGTC_RegisterOrAdoptCvar( &sk_scientist_heal1 );
	SGTC_RegisterOrAdoptCvar( &sk_scientist_heal2 );
	SGTC_RegisterOrAdoptCvar( &sk_scientist_heal3 );

	// monster damage adjusters
	SGTC_RegisterOrAdoptCvar( &sk_monster_head1 );
	SGTC_RegisterOrAdoptCvar( &sk_monster_head2 );
	SGTC_RegisterOrAdoptCvar( &sk_monster_head3 );

	SGTC_RegisterOrAdoptCvar( &sk_monster_chest1 );
	SGTC_RegisterOrAdoptCvar( &sk_monster_chest2 );
	SGTC_RegisterOrAdoptCvar( &sk_monster_chest3 );

	SGTC_RegisterOrAdoptCvar( &sk_monster_stomach1 );
	SGTC_RegisterOrAdoptCvar( &sk_monster_stomach2 );
	SGTC_RegisterOrAdoptCvar( &sk_monster_stomach3 );

	SGTC_RegisterOrAdoptCvar( &sk_monster_arm1 );
	SGTC_RegisterOrAdoptCvar( &sk_monster_arm2 );
	SGTC_RegisterOrAdoptCvar( &sk_monster_arm3 );

	SGTC_RegisterOrAdoptCvar( &sk_monster_leg1 );
	SGTC_RegisterOrAdoptCvar( &sk_monster_leg2 );
	SGTC_RegisterOrAdoptCvar( &sk_monster_leg3 );

	// player damage adjusters
	SGTC_RegisterOrAdoptCvar( &sk_player_head1 );
	SGTC_RegisterOrAdoptCvar( &sk_player_head2 );
	SGTC_RegisterOrAdoptCvar( &sk_player_head3 );

	SGTC_RegisterOrAdoptCvar( &sk_player_chest1 );
	SGTC_RegisterOrAdoptCvar( &sk_player_chest2 );
	SGTC_RegisterOrAdoptCvar( &sk_player_chest3 );

	SGTC_RegisterOrAdoptCvar( &sk_player_stomach1 );
	SGTC_RegisterOrAdoptCvar( &sk_player_stomach2 );
	SGTC_RegisterOrAdoptCvar( &sk_player_stomach3 );

	SGTC_RegisterOrAdoptCvar( &sk_player_arm1 );
	SGTC_RegisterOrAdoptCvar( &sk_player_arm2 );
	SGTC_RegisterOrAdoptCvar( &sk_player_arm3 );

	SGTC_RegisterOrAdoptCvar( &sk_player_leg1 );
	SGTC_RegisterOrAdoptCvar( &sk_player_leg2 );
	SGTC_RegisterOrAdoptCvar( &sk_player_leg3 );
// END REGISTER CVARS FOR SKILL LEVEL STUFF

	SGTC_RegisterOrAdoptCvar( &sv_pushable_fixed_tick_fudge );

	extern void SGTC_RegisterRulesCvars( void );	// sgtc_round.cpp (REA: ref GameDLLInit @0xff68c registers tourny etc.)
	SGTC_RegisterRulesCvars();

	SGTC_GateDlightRegisterCvars();	// sgtc_gate_dlight.cpp — "sgtc_gate_dlight"

	extern void SGTC_RegisterSkillStubs( void );
	SGTC_RegisterSkillStubs();	// placeholder sk_* table: valve/skill.cfg assigns them all
	SERVER_COMMAND( "exec skill.cfg\n" );

#ifdef BUILD_JK_BOTTI
	extern DLL_FUNCTIONS *SGTC_GetGameDllFunctions( void );
	SGTC_Bots_Init( SGTC_GetGameDllFunctions(), &g_engfuncs );
#endif
}

