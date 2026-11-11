// tests/host/in_camera_mp_harness.cpp - third-person camera, multiplayer.
//
// Links the real cl_dll/in_camera.cpp against a stub engine and drives the
// camera the way the engine does (CAM_Think() every frame, cam_command {1,2}).
// Plain int main(), no test framework: one line per case
//   CASE <name> PASS
//   CASE <name> FAIL <detail>
// and a nonzero exit when any case fails. The HUD object lives in
// in_camera_mp_hud_standin.cpp; tests/test_camera_thirdperson_mp.py builds and
// runs both with the tree under test.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hud.h"
#include "cl_util.h"
#include "cl_entity.h"
#include "kbutton.h"
#include "camera.h"

// in_camera.cpp: C entry points the engine calls, plus the C++ toggles that
// the client registers as the "thirdperson"/"firstperson" commands.
extern "C"
{
	void CAM_Think( void );
	int CL_IsThirdPerson( void );
	void CL_CameraOffset( float *ofs );
}
void CAM_ToThirdPerson( void );
void CAM_ToFirstPerson( void );

// in_camera.cpp globals (only camera.h exports cam_ofs/cam_thirdperson/CAM_Init).
extern cvar_t *cam_command;
extern cvar_t *cam_idealpitch;
extern cvar_t *cam_idealyaw;
extern cvar_t *cam_idealdist;
extern int cam_mousemove;

cl_enginefunc_t gEngfuncs;
int g_iUser1;
int g_iUser2;

#define STUB_CVARS 32

static cvar_t s_cvars[STUB_CVARS];
static int s_cvar_count;

static int s_maxclients = 1;
static float s_viewangles[3] = { 0.0f, 90.0f, 0.0f };
static cl_entity_t s_localplayer;

static int s_cases;
static int s_failures;

static void check( const char *name, int ok, const char *detail )
{
	s_cases++;
	if( ok )
	{
		printf( "CASE %s PASS\n", name );
	}
	else
	{
		s_failures++;
		printf( "CASE %s FAIL %s\n", name, detail ? detail : "(no detail)" );
	}
}

// Report the two values the engine reads for third person, so a failure says
// exactly what the camera did instead of only that it was wrong.
static void check_camera( const char *name, int ok, const char *detail )
{
	char buffer[160];

	if( ok )
	{
		check( name, 1, NULL );
		return;
	}
	snprintf( buffer, sizeof( buffer ), "%s: cam_thirdperson=%d CL_IsThirdPerson=%d",
		detail, cam_thirdperson, CL_IsThirdPerson() );
	check( name, 0, buffer );
}

static void checkf( const char *name, int ok, const char *fmt, float a, float b )
{
	char detail[128];

	if( ok )
	{
		check( name, 1, NULL );
		return;
	}
	snprintf( detail, sizeof( detail ), fmt, a, b );
	check( name, 0, detail );
}

//---------------------------------------------------------------------------
// Engine stubs. Only what in_camera.cpp/this harness call.

static cvar_t *stub_register_variable( const char *name, const char *value, int flags )
{
	cvar_t *cv;

	if( s_cvar_count >= STUB_CVARS )
	{
		printf( "CASE stub-cvar-capacity FAIL registered more than %d cvars\n", STUB_CVARS );
		exit( 2 );
	}
	cv = &s_cvars[s_cvar_count++];
	memset( cv, 0, sizeof( *cv ) );
	cv->name = name;
	cv->string = value;
	cv->value = (float)atof( value );
	cv->flags = flags;
	return cv;
}

static cvar_t *stub_find_cvar( const char *name )
{
	int i;

	for( i = 0; i < s_cvar_count; i++ )
	{
		if( strcmp( s_cvars[i].name, name ) == 0 )
			return &s_cvars[i];
	}
	return NULL;
}

static int stub_add_command( const char *name, void (*fn)( void ) )
{
	(void)name;
	(void)fn;
	return 0;
}

static int stub_get_window_center( void )
{
	return 0;
}

static int stub_get_maxclients( void )
{
	return s_maxclients;
}

static void stub_get_viewangles( float *angles )
{
	memcpy( angles, s_viewangles, sizeof( s_viewangles ) );
}

static void stub_cvar_setvalue( const char *name, float value )
{
	cvar_t *cv = stub_find_cvar( name );

	if( cv )
		cv->value = value;
}

static cl_entity_t *stub_get_localplayer( void )
{
	return &s_localplayer;
}

//---------------------------------------------------------------------------
// HUD / key state stubs. CAM_Think reaches the HUD only through
// GetSensitivity() in its mouse-drag branch, which needs cam_mousemove == 1;
// the harness never sets that.

float CHud::GetSensitivity( void )
{
	return 1.0f;
}

float CL_KeyState( kbutton_t *key )
{
	(void)key;
	return 0.0f;
}

void KeyDown( kbutton_t *b )
{
	(void)b;
}

void KeyUp( kbutton_t *b )
{
	(void)b;
}

//---------------------------------------------------------------------------

static void init_engine( void )
{
	memset( &gEngfuncs, 0, sizeof( gEngfuncs ) );
	gEngfuncs.pfnRegisterVariable = stub_register_variable;
	gEngfuncs.pfnAddCommand = stub_add_command;
	gEngfuncs.GetMaxClients = stub_get_maxclients;
	gEngfuncs.GetViewAngles = stub_get_viewangles;
	gEngfuncs.Cvar_SetValue = stub_cvar_setvalue;
	gEngfuncs.GetLocalPlayer = stub_get_localplayer;
	gEngfuncs.GetWindowCenterX = stub_get_window_center; // never used (no mouse movement)

	s_localplayer = cl_entity_t();
	s_localplayer.index = 1;

	CAM_Init();
}

// Put the camera back to the state a fresh level starts in, without leaving the
// third-person toggles: CAM_ClearStates() reads engine view angles and cvars,
// which is more stub surface than the test needs.
static void reset_camera( int maxclients )
{
	s_maxclients = maxclients;
	cam_thirdperson = 0;
	cam_mousemove = 0;
	cam_command->value = 0;
	cam_idealpitch->value = 0.0f;
	cam_idealyaw->value = 0.0f;
	cam_idealdist->value = 128.0f;
	cam_ofs[0] = cam_ofs[1] = cam_ofs[2] = 0.0f;
	g_iUser1 = 0;
	g_iUser2 = 0;
}

static void run_think_frames( int frames )
{
	int i;

	for( i = 0; i < frames; i++ )
		CAM_Think();
}

int main( void )
{
	float ofs[3];

	init_engine();

	// -- single player: untouched behaviour -------------------------------
	reset_camera( 1 );
	cam_command->value = 1;
	CAM_Think();
	check_camera( "sp-cam-command-1-engages", cam_thirdperson == 1 && CL_IsThirdPerson() == 1,
		"cam_command 1 did not engage third person" );
	check( "sp-cam-command-resets", cam_command->value == 0.0f, "cam_command not reset to 0" );
	checkf( "sp-cam-ofs-updated", cam_ofs[2] == 128.0f, "cam_ofs[2] %f != %f", cam_ofs[2], 128.0f );
	checkf( "sp-cam-ofs-yaw-from-view", cam_ofs[1] == 90.0f, "cam_ofs[1] %f != %f", cam_ofs[1], 90.0f );

	reset_camera( 1 );
	cam_command->value = 1;
	CAM_Think();
	cam_command->value = 2;
	CAM_Think();
	check_camera( "sp-cam-command-2-first-person", cam_thirdperson == 0 && CL_IsThirdPerson() == 0,
		"cam_command 2 did not restore first person" );

	// -- multiplayer: cam_command 1 must reach the stock camera path ------
	reset_camera( 2 );
	cam_command->value = 1;
	CAM_Think();
	check_camera( "mp-cam-command-1-engages", cam_thirdperson == 1 && CL_IsThirdPerson() == 1,
		"cam_command 1 did not engage third person" );
	check( "mp-cam-command-resets", cam_command->value == 0.0f, "cam_command not reset to 0" );
	checkf( "mp-cam-ofs-updated", cam_ofs[2] == 128.0f, "cam_ofs[2] %f != %f", cam_ofs[2], 128.0f );
	checkf( "mp-cam-ofs-yaw-from-view", cam_ofs[1] == 90.0f, "cam_ofs[1] %f != %f", cam_ofs[1], 90.0f );

	// view.cpp offsets the view by CL_CameraOffset() whenever CL_IsThirdPerson().
	ofs[0] = ofs[1] = ofs[2] = 0.0f;
	CL_CameraOffset( ofs );
	check( "mp-camera-offset-published",
		ofs[0] == cam_ofs[0] && ofs[1] == cam_ofs[1] && ofs[2] == 128.0f,
		"offset != cam_ofs" );

	// The camera must survive the engine's per-frame CAM_Think() call: the old
	// CAM_Think() forced first person every frame in multiplayer.
	run_think_frames( 10 );
	check_camera( "mp-third-person-persists", cam_thirdperson == 1 && CL_IsThirdPerson() == 1,
		"camera fell back to first person during CAM_Think()" );
	checkf( "mp-camera-distance-persists", cam_ofs[2] == 128.0f, "cam_ofs[2] %f != %f", cam_ofs[2], 128.0f );

	// Direct toggles, as the "firstperson"/"thirdperson" console commands use.
	CAM_ToFirstPerson();
	check_camera( "mp-cam-to-first-person", cam_thirdperson == 0 && CL_IsThirdPerson() == 0,
		"CAM_ToFirstPerson() left the camera in third person" );
	CAM_ToThirdPerson();
	check_camera( "mp-cam-to-third-person", cam_thirdperson == 1 && CL_IsThirdPerson() == 1,
		"CAM_ToThirdPerson() refused in multiplayer" );
	checkf( "mp-cam-to-third-person-seeds-ofs", cam_ofs[2] == 30.0f, "cam_ofs[2] %f != %f", cam_ofs[2], 30.0f );
	check( "mp-cam-to-third-person-resets-command", cam_command->value == 0.0f, "cam_command not reset to 0" );

	// cam_command 2 still restores first person in multiplayer.
	reset_camera( 2 );
	cam_command->value = 1;
	CAM_Think();
	cam_command->value = 2;
	CAM_Think();
	check_camera( "mp-cam-command-2-first-person", cam_thirdperson == 0 && CL_IsThirdPerson() == 0,
		"cam_command 2 did not restore first person" );

	// -- spectator branch of CL_IsThirdPerson: unchanged -------------------
	reset_camera( 2 );
	g_iUser1 = 1;
	g_iUser2 = s_localplayer.index;
	check( "spectator-third-person", CL_IsThirdPerson() == 1, "spectating own view entity not third person" );
	g_iUser2 = s_localplayer.index + 1;
	check( "spectator-other-target", CL_IsThirdPerson() == 0, "other spectator target reported third person" );
	g_iUser1 = 0;
	check( "not-spectating", CL_IsThirdPerson() == 0, "non-spectator reported third person" );

	printf( "SUMMARY cases=%d failures=%d\n", s_cases, s_failures );
	return s_failures ? 1 : 0;
}
