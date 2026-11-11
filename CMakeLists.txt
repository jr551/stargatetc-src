#
# Copyright (c) 2016 Alibek Omarov
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

# 3.9 added captures in if(MATCHES)
cmake_minimum_required(VERSION 3.9)

if(NOT ${CMAKE_VERSION} VERSION_LESS "3.15.0")
	cmake_policy(SET CMP0091 NEW)
	# use the static CRT for all targets
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Install custom module path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")

include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckCSourceCompiles)
include(CheckCXXSourceCompiles)
include(VSForceXPToolchain) # Force XP toolchain for Visual Studio
include(CheckIPOSupported)

project (HLSDK-PORTABLE)

#--------------
# USER DEFINES \
################\
option(BUILD_CLIENT "Build client dll" ON)
option(BUILD_SERVER "Build server dll" ON)
option(LTO "Enable interprocedural optimization" OFF)
option(POLLY "Enable pollyhedral optimization" OFF)
option(ANDROID_APK "Enable APK styled deploy" OFF)
option(BUILD_JK_BOTTI "Compile jk_botti bots into the server library" ON)
option(SGTC_NO_UNDEFINED "Fail the Linux link of the server and client libraries on unresolved symbols" ON)

# C++ standard for the server and client libraries (see sgtc_configure_library below).
# The code is old-style C++ that also uses the C++11 "override" keyword (dlls/sgtc_rules.h and
# others), so 11 is the lowest level at which it is clean: with GCC 14 on Linux, gnu++11 compiles
# the server and the client without language warnings, while gnu++98 compiles but adds ~700
# default-on -Wc++11-extensions warnings for "override". Pin it on Linux instead of leaving it to
# whatever the compiler defaults to (gnu++98 on the Steam Runtime scout GCC 4.8, gnu++17 on
# current GCC). Other platforms keep the compiler default: MSVC is never touched, and the MinGW,
# macOS and Android builds have not been checked at gnu++11. Override with
# -DSGTC_CXX_STANDARD=<98|11|14|17>, or pass an empty value to go back to the compiler default.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(SGTC_CXX_STANDARD_DEFAULT "11")
else()
	set(SGTC_CXX_STANDARD_DEFAULT "")
endif()
set(SGTC_CXX_STANDARD "${SGTC_CXX_STANDARD_DEFAULT}" CACHE STRING "C++ standard for the server and client libraries (empty: compiler default)")

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4 OR
	((WIN32 OR ${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	AND (CMAKE_SYSTEM_PROCESSOR STREQUAL "x64"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "X64"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "X86_64"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "amd64"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "EM64T")))
	option(64BIT "Disable auto -m32 appending to compiler flags" OFF)
else()
	option(64BIT "Disable auto -m32 appending to compiler flags" ON)
endif()

# It seems CMAKE_SYSTEM_PROCESSOR parameter completely useless for APPLE platform,
# so may need to set options here manually.
if((WIN32 OR ${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	AND (((CMAKE_SYSTEM_PROCESSOR STREQUAL "x64"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "X64"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "X86_64"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "amd64"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "EM64T") AND NOT 64BIT)
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "x86"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "X86"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "i386"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "i486"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "i586"
	OR CMAKE_SYSTEM_PROCESSOR STREQUAL "i686"))
	option(GOLDSOURCE_SUPPORT "Build goldsource compatible client library" ON)
	# Win32 GoldSrc ships vgui2.dll; Linux Xash3D FWGS needs InitVGUISupportAPI inside client.so.
	if(WIN32)
		set(USE_STATIC_FREEVGUI OFF)
	else()
		set(USE_STATIC_FREEVGUI ON)
	endif()
else()
	option(GOLDSOURCE_SUPPORT "Build goldsource compatible client library" OFF)
	# no prebuilt vgui shared library exists here by default, so link FreeVGUI into the client
	set(USE_STATIC_FREEVGUI ON)
endif()

#-----------------
# MAIN BUILD CODE \
###################\

CHECK_FUNCTION_EXISTS(strlcpy HAVE_STRLCPY)
CHECK_FUNCTION_EXISTS(strlcat HAVE_STRLCAT)

file(STRINGS "mod_options.txt" MOD_OPTIONS_STRINGS REGEX "^([A-Za-z0-9_-]+)=([A-Za-z0-9_-]+)\ \#\ (.*)$")
foreach(LINE IN LISTS MOD_OPTIONS_STRINGS)
	# file() itself doesn't populate CMAKE_MATCH_<n>, so
	# reparse the string

	if(${LINE} MATCHES "^([A-Za-z0-9_-]+)=([A-Za-z0-9_-]+)\ \#\ (.*)$")
		# detect boolean options
		if(${CMAKE_MATCH_2} STREQUAL "ON" OR ${CMAKE_MATCH_2} STREQUAL "OFF")
			option(${CMAKE_MATCH_1} "${CMAKE_MATCH_3}" ${CMAKE_MATCH_2})

			# let's check it here as well
			if(${CMAKE_MATCH_1})
				message(STATUS ${CMAKE_MATCH_3} " is enabled")
				add_definitions(-D${CMAKE_MATCH_1})
			else()
				message(STATUS ${CMAKE_MATCH_3} " is disabled")
			endif()
		else()
			message(STATUS ${CMAKE_MATCH_3} " set to " ${CMAKE_MATCH_2})
			set(${CMAKE_MATCH_1} "${CMAKE_MATCH_2}" CACHE STRING "${CMAKE_MATCH_3}")
		endif()
	endif()
endforeach()

if(ANDROID_APK)
	set(GAMEDIR ".")
	set(SERVER_INSTALL_DIR ".")
	set(CLIENT_INSTALL_DIR ".")
endif()

if(HLDEMO_BUILD AND OEM_BUILD)
	message(FATAL_ERROR "Don't mix Demo and OEM builds!")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT 64BIT)
	if(MSVC)
		message(FATAL_ERROR "Add \"-D64BIT=ON\" to build 64-bit version of SDK or add \"-A Win32\" if you want to build a 32-bit version")
	else()
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -m32")
	endif()
	set(CMAKE_SIZEOF_VOID_P 4)
endif()

if(64BIT AND CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR "You enabled XASH_64BIT, but compiler can't create 64 bit code!")
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Android")
	find_program(TERMUX_INFO termux-info)
	if(NOT TERMUX_INFO)
		# no-op
	else()
		# set through flags because otherwise macro won't be seen in tests
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D__TERMUX__=1")
		set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -D__TERMUX__=1")
	endif()
endif()

# Xash3D FWGS Library Naming Scheme compliance
# see documentation: https://github.com/FWGS/xash3d-fwgs/blob/master/Documentation/extensions/library-naming.md
include(LibraryNaming)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(STATUS "Building for 64 Bit")
else()
	message(STATUS "Building for 32 Bit")
endif()

if (MINGW)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc")
	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--add-stdcall-alias")
endif()

if(NOT MSVC)
	#add_compile_options(-Wempty-body) # GCC/Clang flag
	add_compile_options(-Wreturn-type) # GCC/Clang flag
else()
	add_definitions(-D_CRT_SILENCE_NONCONFORMING_TGMATH_H)
endif()

if(VITA)
	add_compile_options(-fno-use-cxa-atexit)
endif()

if(LTO)
	check_ipo_supported(RESULT HAVE_LTO OUTPUT LTO_ERROR)
	if(HAVE_LTO)
		message(STATUS "IPO / LTO enabled")
	else()
		message(STATUS "IPO / LTO not supported: ${LTO_ERROR}")
	endif()
endif()

check_include_file("tgmath.h" HAVE_TGMATH_H)
if(HAVE_TGMATH_H)
	if(NOT MSVC)
		set(CMAKE_REQUIRED_LIBRARIES "m")
	endif()
	check_c_source_compiles("#include <tgmath.h>
	const float val = 2, val2 = 3;
	int main(){ return (int)(-asin(val) + cos(val2)); }" HAVE_VALID_TGMATH_H )
	if(${HAVE_VALID_TGMATH_H})
		add_definitions(-DHAVE_TGMATH_H)
	endif()
endif()

# Polly (clang) / Graphite (GCC) loop optimizations. This has to stay above the add_subdirectory()
# calls: add_compile_options() only reaches targets that are created after it. Only enable the
# flags if the compiler accepts them, e.g. clang without Polly fails on "-mllvm -polly".
if(POLLY)
	if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
		set(SGTC_POLLY_FLAGS -mllvm -polly)
	elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		set(SGTC_POLLY_FLAGS -fgraphite-identity -floop-interchange -floop-block)
	endif()

	if(SGTC_POLLY_FLAGS)
		string(REPLACE ";" " " SGTC_POLLY_FLAGS_STRING "${SGTC_POLLY_FLAGS}")
		set(CMAKE_REQUIRED_FLAGS "${SGTC_POLLY_FLAGS_STRING}")
		check_cxx_source_compiles("int main() { return 0; }" HAVE_POLLY_FLAGS)
		unset(CMAKE_REQUIRED_FLAGS)
		if(HAVE_POLLY_FLAGS)
			add_compile_options(${SGTC_POLLY_FLAGS})
			message(STATUS "POLLY enabled: ${SGTC_POLLY_FLAGS_STRING}")
		else()
			message(WARNING "POLLY requested, but the compiler does not accept: ${SGTC_POLLY_FLAGS_STRING}")
		endif()
	else()
		message(WARNING "POLLY requested, but there are no loop optimization flags for ${CMAKE_CXX_COMPILER_ID}")
	endif()
endif()

# Settings shared by the server and client libraries; called from dlls/ and cl_dll/ right after
# their add_library(). Both are SHARED libraries, so the executable-only CMAKE_EXE_LINKER_FLAGS
# (which CI used to pass for --no-undefined) never reached them.
function(sgtc_configure_library target)
	if(NOT MSVC)
		if(SGTC_CXX_STANDARD)
			set_target_properties(${target} PROPERTIES
				CXX_STANDARD ${SGTC_CXX_STANDARD}
				CXX_STANDARD_REQUIRED ON
				CXX_EXTENSIONS ON)
		endif()

		# GNU/lld style linkers only. Skipped when sanitizers are enabled: clang does not link
		# the sanitizer runtime into shared libraries, so the link would fail on its symbols.
		if(SGTC_NO_UNDEFINED AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
			AND NOT CMAKE_CXX_FLAGS MATCHES "-fsanitize")
			set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--no-undefined")
		endif()
	endif()
endfunction()

add_subdirectory(game_shared)

if(BUILD_CLIENT)
	message(STATUS "Building client enabled")

	add_subdirectory(freevgui)
	add_subdirectory(cl_dll)
endif()

if(BUILD_SERVER)
	message(STATUS "Building server enabled")
	if(BUILD_JK_BOTTI)
		add_subdirectory(bots)
	endif()
	add_subdirectory(dlls)
endif()

if(NOT BUILD_SERVER AND NOT BUILD_CLIENT)
	message(FATAL_ERROR "Nothing to build")
endif()
