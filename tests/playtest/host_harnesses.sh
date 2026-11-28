#!/bin/bash
# host_harnesses.sh [install dir]
#
# Builds and runs every host-side harness (no engine, no 32-bit toolchain) from a scratch
# directory. The install dir is only needed for the scheme font harness and the command menu
# check (default /home/deck/Projects/stargatetc-playtest/stargatetc-linux-x86).
cd "$(dirname "$0")/../.." || exit 1
R=$PWD
INSTALL=${1:-${SGTC_INSTALL:-/home/deck/Projects/stargatetc-playtest/stargatetc-linux-x86}}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
INC="-Icl_dll -Icl_dll/hl -Idlls -Icommon -Iengine -Ipublic -Igame_shared -Ipm_shared -Iutils/vgui/include -Ifreevgui -Ifreevgui/include -Iexternal"
FONTSRC="freevgui/font.cpp freevgui/fileimage.cpp freevgui/platform/common/font.cpp"
fail=0
run() {  # run <name> <expected PASS/CASE count command...>
  name=$1; shift
  if out=$("$@" 2>&1); then echo "PASS $name"; else echo "FAIL $name"; echo "$out" | tail -5; fail=1; fi
}
build() { name=$1; shift; "$@" > "$W/$name.build" 2>&1 || { echo "FAIL $name (build)"; tail -5 "$W/$name.build"; fail=1; return 1; }; }
build fire_patch c++ -std=c++11 -O1 -Idlls -o "$W/fire_patch" dlls/sgtc_firepatch.cpp tests/host/fire_patch_cap_harness.cpp && run fire_patch "$W/fire_patch"
build gate_timing c++ -std=c++11 -O1 -Idlls -Icommon -Iengine -Ipublic -o "$W/gate_timing" tests/host/gate_timing_harness.cpp && run gate_timing "$W/gate_timing"
build bot_edict c++ -std=c++11 -O1 -Idlls -o "$W/bot_edict" dlls/sgtc_bots_budget.cpp tests/host/bot_edict_budget_harness.cpp && run bot_edict "$W/bot_edict"
build director c++ -std=c++11 -O1 -Idlls -o "$W/director" tests/host/director_core_harness.cpp dlls/sgtc_director_core.cpp && run director "$W/director"
build font_fallback c++ -std=c++11 -O1 -Ifreevgui -Ifreevgui/include -Iutils/vgui/include -Iexternal -o "$W/font_fallback" tests/host/font_fallback_harness.cpp $FONTSRC && run font_fallback "$W/font_fallback"
build scheme_font c++ -std=c++11 -O1 -w -Dstricmp=strcasecmp $INC -o "$W/scheme_font" tests/host/scheme_font_fallback_harness.cpp cl_dll/vgui_SchemeManager.cpp $FONTSRC && \
  for x in 640 800 1024 1152 1280 1600; do run "scheme_font@$x" "$W/scheme_font" "$INSTALL/stargatetc" $x; done
build in_camera c++ -std=c++11 -O1 -w -Dstricmp=strcasecmp $INC -o "$W/in_camera" tests/host/in_camera_mp_harness.cpp tests/host/in_camera_mp_hud_standin.cpp cl_dll/in_camera.cpp && run in_camera "$W/in_camera"
build l10n c++ -std=c++11 -O1 -w $INC -o "$W/l10n" tests/l10n_parser_harness.cpp cl_dll/l10n.cpp && run l10n bash -c "cd $W && ./l10n"
run commandmenu python3 tests/commandmenu_regression.py "$INSTALL/stargatetc"
run qfont_wads python3 tests/qfont_wad_regression.py "$INSTALL/valve"
exit $fail
