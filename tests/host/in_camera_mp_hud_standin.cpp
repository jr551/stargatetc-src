// tests/host/in_camera_mp_hud_standin.cpp - gHUD stand-in for the camera
// harness.
//
// cl_dll/in_camera.cpp references gHUD.GetSensitivity() inside CAM_Think()'s
// mouse-drag branch. Linking cl_dll/hud.cpp (the real CHud) would drag in every
// HUD widget and the VGUI stack, so the harness provides the symbol instead.
// The harness keeps cam_mousemove at 0, so the branch is never entered and the
// storage below is never read or written. The type intentionally differs from
// CHud: this translation unit must not include cl_dll/hud.h.

struct InCameraHarnessHudStandIn
{
	unsigned char storage[4096];
};

alignas( 64 ) InCameraHarnessHudStandIn gHUD;
