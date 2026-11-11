#
# Copyright (c) 2015 Pavlo Lavrenenko
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

cmake_minimum_required(VERSION 3.9)
project (SVDLL)

set (SVDLL_LIBRARY server)

option( SGTC_EXPORT_ALL_SYMBOLS "Export class methods for tests/stub_engine (logic parity harness)" OFF )

if(NOT MSVC)
	if(NOT WIN32)
		add_compile_options(-fno-exceptions) # GCC/Clang flag; Windows builds keep exceptions like MSVC
	endif()
	add_compile_options(-fno-rtti) # GCC/Clang flag
	add_compile_options(-Wno-invalid-offsetof) # GCC/Clang flag
	if( SGTC_EXPORT_ALL_SYMBOLS )
		add_compile_options(-fvisibility=default) # must win over any CMAKE_CXX_FLAGS hidden
		# Keep engine-wrapper test hooks out of the shipping DLL.
		add_definitions(-DSGTC_EXPORT_ALL_SYMBOLS)
	else()
		add_compile_options(-fvisibility=hidden) # GCC/Clang flag
	endif()
	add_definitions(-D_LINUX) # It seems enough for all non-Win32 systems
	add_definitions(-Dstricmp=strcasecmp -Dstrnicmp=strncasecmp)
	if(NOT MINGW)
		# MinGW already declares snprintf/vsnprintf; renaming them clashes.
		add_definitions(-D_snprintf=snprintf -D_vsnprintf=vsnprintf)
	endif()
	if(CMAKE_BUILD_TYPE MATCHES "Release"
		OR (CMAKE_BUILD_TYPE MATCHES "RelWithDebInfo"
		AND CMAKE_SYSTEM_NAME STREQUAL "Android"))
		add_compile_options(-fno-unwind-tables -fno-asynchronous-unwind-tables) # GCC/Clang flag
		add_compile_options(-fomit-frame-pointer) # GCC/Clang flag
		add_compile_options(-ftree-vectorize) # GCC/Clang flag
		add_compile_options(-funsafe-math-optimizations) # GCC/Clang flag
		# pm_shared is compiled into both the client and the server, and the client predicts
		# movement with it, so both must give the same results. Keep it out of the
		# reassociating -funsafe-math-optimizations (the flag comes last on the command line).
		set_source_files_properties(
			../pm_shared/pm_debug.c
			../pm_shared/pm_math.c
			../pm_shared/pm_shared.c
			PROPERTIES COMPILE_FLAGS -fno-unsafe-math-optimizations)
	endif()
else()
	add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE)
endif()

set (SVDLL_SOURCES
	airtank.cpp
	animating.cpp
	animation.cpp
	barney.cpp
	bmodels.cpp
	buttons.cpp
	cbase.cpp
	client.cpp
	combat.cpp
	defaultai.cpp
	doors.cpp
	effects.cpp
	explode.cpp
	func_break.cpp
	func_tank.cpp
	game.cpp
	gamerules.cpp
	gargantua.cpp
	genericmonster.cpp
	ggrenade.cpp
	globals.cpp
	glock.cpp
	h_ai.cpp
	h_battery.cpp
	h_cycler.cpp
	h_export.cpp
	handgrenade.cpp
	healthkit.cpp
	islave.cpp
	items.cpp
	lights.cpp
	maprules.cpp
	monstermaker.cpp
	monsters.cpp
	monsterstate.cpp
	mp5.cpp
	multiplay_gamerules.cpp
	nodes.cpp
	observer.cpp
	pathcorner.cpp
	plane.cpp
	plats.cpp
	player.cpp
	rat.cpp
	satchel.cpp
	schedule.cpp
	scientist.cpp
	scripted.cpp
	shotgun.cpp
	singleplay_gamerules.cpp
	skill.cpp
	sound.cpp
	soundent.cpp
	spectator.cpp
	squadmonster.cpp
	subs.cpp
	talkmonster.cpp
	teamplay_gamerules.cpp
	triggers.cpp
	tripmine.cpp
	turret.cpp
	util.cpp
	vehicle.cpp
	weapons.cpp
	world.cpp
	zombie.cpp
	../game_shared/voice_gamemgr.cpp
	../pm_shared/pm_debug.c
	../pm_shared/pm_math.c
	../pm_shared/pm_shared.c
	../public/safe_snprintf.c
	sgtc_gas_grenade.cpp
	sgtc_gate.cpp
	sgtc_gate_timing.cpp
	sgtc_gate_dlight.cpp
	sgtc_lanceproj.cpp
	sgtc_links.cpp
	sp_shims.cpp
	sgtc_sp_enemies.cpp
	sgtc_player.cpp
	sgtc_rea_debug.cpp
	sgtc_cvar.cpp
	sgtc_itemguard.cpp
	sgtc_diag.cpp
	sgtc_diagfx.cpp
	sgtc_skill_stubs.cpp
	sgtc_pack_auto.cpp
	sgtc_shield.cpp
	sgtc_monsters.cpp
	sgtc_rules.cpp
	sgtc_rules_alt.cpp
	sgtc_rules_class.cpp
	sgtc_rules_cmd.cpp
	sgtc_rules_cp.cpp
	sgtc_rules_msg.cpp
	sgtc_rules_spawn.cpp
	sgtc_entities.cpp
	sgtc_entities_env.cpp
	sgtc_round.cpp
	sgtc_token.cpp
	sgtc_zones.cpp
	sgtc_misc.cpp
	sgtc_structs.cpp
	sgtc_testcmd.cpp
	sgtc_wpn_ammo_psg1.cpp
	sgtc_wpn_batsup.cpp
	sgtc_wpn_coujaf.cpp
	sgtc_wpn_couteau.cpp
	sgtc_wpn_flashbang.cpp
	sgtc_wpn_gacgoa.cpp
	sgtc_wpn_glstaff.cpp
	sgtc_wpn_goaregen.cpp
	sgtc_wpn_invis.cpp
	sgtc_wpn_lance.cpp
	sgtc_wpn_larve.cpp
	sgtc_wpn_m16.cpp
	sgtc_wpn_m92s.cpp
	sgtc_wpn_main.cpp
	sgtc_wpn_medic.cpp
	sgtc_gas_grenade.cpp
	sgtc_wpn_mustardgrenade.cpp
	sgtc_wpn_narcogrenade.cpp
	sgtc_wpn_nervegrenade.cpp
	sgtc_wpn_p90.cpp
	sgtc_wpn_psg1.cpp
	sgtc_wpn_reetou.cpp
	sgtc_wpn_shield.cpp
	sgtc_wpn_smokegrenade.cpp
	sgtc_wpn_tacgun.cpp
	sgtc_wpn_usas.cpp
	sgtc_wpn_zat.cpp
	sgtc_wpn_zatarc.cpp
	sgtc_sniper.cpp
)

if(BUILD_JK_BOTTI)
	# sgtc_bots_radio.cpp: human `radio` -> addressed MSG_ONE + SGTC_Bots_RadioHear;
	# bot talk -> SGTC_Bots_RadioSend -> per-teammate MSG_ONE + bots hear.
	list(APPEND SVDLL_SOURCES
		sgtc_bots.cpp
	sgtc_bots_rules.cpp
	sgtc_mech_state.cpp
    sgtc_muzzlefx.cpp
    sgtc_c4fire.cpp
		sgtc_bots_budget.cpp
		sgtc_bots_classes.cpp
		sgtc_bots_classplay.cpp
		sgtc_bots_events.cpp
		sgtc_bots_loadout.cpp
		sgtc_bots_tac.cpp
		sgtc_director.cpp
		sgtc_director_core.cpp
		sgtc_director_http.cpp
		sgtc_bots_radio.cpp
		sgtc_bots_rules_ai.cpp)
endif()

if (HAVE_STRLCPY)
	add_definitions(-DHAVE_STRLCPY)
else()
	list(APPEND SVDLL_SOURCES
		../external/openbsd/strlcpy.c)
endif()

if (HAVE_STRLCAT)
	add_definitions(-DHAVE_STRLCAT)
else()
	list(APPEND SVDLL_SOURCES
		../external/openbsd/strlcat.c)
endif()

include_directories (. wpn_shared ../common ../engine ../pm_shared ../game_shared ../public)

if(MSVC)
	set(SVDLL_SOURCES
		${SVDLL_SOURCES}
		hl.def)
endif()

add_library (${SVDLL_LIBRARY} SHARED ${SVDLL_SOURCES})
if(BUILD_JK_BOTTI)
	target_compile_definitions(${SVDLL_LIBRARY} PRIVATE BUILD_JK_BOTTI)
	target_link_libraries(${SVDLL_LIBRARY} vcs_info jk_botti_native)
	# Bot director: the HTTP worker uses pthread on POSIX and WinHTTP on Windows.
	# libcurl itself is dlopen'd at runtime, so there is no curl link dependency.
	if(WIN32)
		target_link_libraries(${SVDLL_LIBRARY} winhttp)
	else()
		find_package(Threads REQUIRED)
		target_link_libraries(${SVDLL_LIBRARY} Threads::Threads ${CMAKE_DL_LIBS})
	endif()
else()
	target_link_libraries(${SVDLL_LIBRARY} vcs_info)
endif()
sgtc_configure_library(${SVDLL_LIBRARY}) # C++ standard and --no-undefined, see the top-level CMakeLists.txt

set_target_properties (${SVDLL_LIBRARY} PROPERTIES POSITION_INDEPENDENT_CODE 1)

if(APPLE AND NOT "${SERVER_LIBRARY_NAME_OSX}" STREQUAL "")
	# on Apple platforms we always use gamedll_osx liblist/gameinfo key, if it's set
	set(SVDLL_NAME "${SERVER_LIBRARY_NAME_OSX}")
elseif(NOT WIN32 AND NOT "${SERVER_LIBRARY_NAME_LINUX}" STREQUAL "")
	# on non-Windows platforms we use gamedll_linux liblist/gameinfo key, if it's set
	set(SVDLL_NAME "${SERVER_LIBRARY_NAME_LINUX}")
else()
	# on Windows or if no gamedll_osx or gamedll_linux is set, use default gamedll key
	set(SVDLL_NAME "${SERVER_LIBRARY_NAME}")
endif()

set_target_postfix_with_name(${SVDLL_LIBRARY} ${SVDLL_NAME})

if(HAVE_LTO)
	set_property(TARGET ${SVDLL_LIBRARY} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

install(TARGETS ${SVDLL_LIBRARY}
	RUNTIME DESTINATION "${GAMEDIR}/${SERVER_INSTALL_DIR}/"
	LIBRARY DESTINATION "${GAMEDIR}/${SERVER_INSTALL_DIR}/"
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
		GROUP_READ GROUP_EXECUTE
		WORLD_READ WORLD_EXECUTE)

if(CMAKE_BUILD_TYPE MATCHES "Release" AND NOT WIN32)
	if(APPLE)
		# Apple strip treats -s as a symbol list file, -x keeps exported symbols
		set(STRIP_FLAGS -x)
	else()
		set(STRIP_FLAGS -s)
	endif()
	add_custom_command(TARGET ${SVDLL_LIBRARY}
			POST_BUILD DEPENDS ${SVDLL_LIBRARY}
			COMMAND ${CMAKE_STRIP} ${STRIP_FLAGS} $<TARGET_FILE:${SVDLL_LIBRARY}>)
endif()
