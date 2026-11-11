cmake_minimum_required(VERSION 3.9)

set(JK_BOTTI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/jk_botti)

set(JK_BOTTI_ZLIB_SRC
	${JK_BOTTI_ROOT}/zlib/adler32.c
	${JK_BOTTI_ROOT}/zlib/compress.c
	${JK_BOTTI_ROOT}/zlib/crc32.c
	${JK_BOTTI_ROOT}/zlib/deflate.c
	${JK_BOTTI_ROOT}/zlib/inffast.c
	${JK_BOTTI_ROOT}/zlib/inflate.c
	${JK_BOTTI_ROOT}/zlib/inftrees.c
	${JK_BOTTI_ROOT}/zlib/trees.c
	${JK_BOTTI_ROOT}/zlib/uncompr.c
	${JK_BOTTI_ROOT}/zlib/zutil.c
	${JK_BOTTI_ROOT}/zlib/gzlib.c
	${JK_BOTTI_ROOT}/zlib/gzread.c
	${JK_BOTTI_ROOT}/zlib/gzwrite.c
	${JK_BOTTI_ROOT}/zlib/gzclose.c
)

set(JK_BOTTI_CPP
	${JK_BOTTI_ROOT}/bot.cpp
	${JK_BOTTI_ROOT}/bot_chat.cpp
	${JK_BOTTI_ROOT}/bot_client.cpp
	${JK_BOTTI_ROOT}/bot_combat.cpp
	${JK_BOTTI_ROOT}/bot_trace.cpp
	${JK_BOTTI_ROOT}/bot_config_init.cpp
	${JK_BOTTI_ROOT}/bot_models.cpp
	${JK_BOTTI_ROOT}/bot_navigate.cpp
	${JK_BOTTI_ROOT}/bot_query_hook.cpp
	${JK_BOTTI_ROOT}/bot_radio.cpp
	${JK_BOTTI_ROOT}/bot_skill.cpp
	${JK_BOTTI_ROOT}/bot_sound.cpp
	${JK_BOTTI_ROOT}/bot_weapons.cpp
	${JK_BOTTI_ROOT}/commands.cpp
	${JK_BOTTI_ROOT}/dll.cpp
	${JK_BOTTI_ROOT}/engine.cpp
	${JK_BOTTI_ROOT}/random_num.cpp
	${JK_BOTTI_ROOT}/util.cpp
	${JK_BOTTI_ROOT}/safe_snprintf.cpp
	${JK_BOTTI_ROOT}/waypoint.cpp
	${JK_BOTTI_ROOT}/native/meta_native.cpp
	${JK_BOTTI_ROOT}/native/engine_hooks.cpp
)

if(WIN32)
	list(APPEND JK_BOTTI_CPP ${JK_BOTTI_ROOT}/bot_query_hook_win32.cpp)
elseif(APPLE)
	# Patsy/DSH port: linux hook uses x86 asm + GNU-specific libc; no-op on macOS.
	list(APPEND JK_BOTTI_CPP ${JK_BOTTI_ROOT}/bot_query_hook_osx.cpp)
else()
	list(APPEND JK_BOTTI_CPP ${JK_BOTTI_ROOT}/bot_query_hook_linux.cpp)
endif()

add_library(jk_botti_native STATIC ${JK_BOTTI_CPP} ${JK_BOTTI_ZLIB_SRC})
target_compile_definitions(jk_botti_native PRIVATE JK_BOTTI_NATIVE Z_PREFIX NO_snprintf)
target_include_directories(jk_botti_native PRIVATE
	${JK_BOTTI_ROOT}/metamod
	${JK_BOTTI_ROOT}/common
	${JK_BOTTI_ROOT}/dlls
	${JK_BOTTI_ROOT}/engine
	${JK_BOTTI_ROOT}/pm_shared
	${JK_BOTTI_ROOT}/zlib
	${JK_BOTTI_ROOT}
	${CMAKE_SOURCE_DIR}/public
	${CMAKE_SOURCE_DIR}/game_shared
)
if(NOT MSVC)
	target_compile_options(jk_botti_native PRIVATE -Wno-write-strings -Wno-invalid-offsetof)
endif()
if(APPLE)
	# Patsy/DSH port: the vendored bot sources use Windows/glibc spellings that
	# macOS libc only provides as strcasecmp/strncasecmp.
	target_compile_definitions(jk_botti_native PRIVATE stricmp=strcasecmp strnicmp=strncasecmp)
endif()
set_target_properties(jk_botti_native PROPERTIES POSITION_INDEPENDENT_CODE ON)
sgtc_configure_library(jk_botti_native)
if(WIN32)
	target_link_libraries(jk_botti_native PUBLIC ws2_32)
endif()
