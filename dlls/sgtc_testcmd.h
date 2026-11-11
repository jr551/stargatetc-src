// sgtc_testcmd.h - in-game test/debug console commands for automated feature tests.
//
// The command surface is deliberately tiny, machine-readable and gated:
//   * SGTC_TestCommand(line) runs one command and prints "SGTCTEST {...}" lines.
//   * SGTC_TestInit() registers the test cvars (called from GameDLLInit).
//   * SGTC_TestActivate() runs $sgtc_test_script once per map when $sgtc_test_enable=1.
//   * SGTC_TestClientCommand() is reachable from a client console as "sgtc_test ...".
//
// Nothing here runs unless a test explicitly calls it or a test server sets
// sgtc_test_enable 1, so shipping the DLL costs nothing in a normal game.
#ifndef SGTC_TESTCMD_H
#define SGTC_TESTCMD_H

#ifdef __cplusplus
extern "C" {
#endif

// Run one command line ("info", "entities 50", "players", ...). Safe to call anytime.
void SGTC_TestCommand(const char *line);

// Register test cvars; call once from GameDLLInit.
void SGTC_TestInit(void);

// Execute $sgtc_test_script (once per map) when $sgtc_test_enable != 0.
void SGTC_TestActivate(void);

// Handle a client console command "sgtc_test <cmd>"; prints back to the caller.
void SGTC_TestClientCommand(void *pEntity);

// Per-frame movement sampler, enabled by $sgtc_test_watch (dedicated/headless runs).
void SGTC_TestFrame(void);

#ifdef __cplusplus
}
#endif

#endif // SGTC_TESTCMD_H

