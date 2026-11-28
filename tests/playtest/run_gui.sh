#!/bin/bash
# run_gui.sh <install dir> <width> <height> <port> <timeout s> <logfile>      (DEV=0|2, default 2)
#
# Runs the GUI client offscreen (SDL offscreen driver, software GL), no display or sound needed.
# Put the scenario in <install>/stargatetc/userconfig.cfg first: the engine runs it at start-up
# (+exec and +map on the command line are ignored in GUI mode). The file is removed afterwards
# so a leftover one cannot drive the next client. Logs go to stdout; a killed run loses the
# buffered tail, so check engine.log in the install for the real last lines.
D=$1; W=$2; H=$3; P=$4; T=$5; LOG=$6
cd "$D" || exit 1
SDL_VIDEODRIVER=offscreen timeout -k 5 -s INT "$T" ./xash3d -game stargatetc -windowed -width "$W" -height "$H" -dev "${DEV:-2}" -nosound +hostport "$P" > "$LOG" 2>&1
echo "EXIT=$?" >> "$LOG"
rm -f "$D/stargatetc/userconfig.cfg"
