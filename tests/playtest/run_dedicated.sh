#!/bin/bash
# run_dedicated.sh <install dir> <port> <scenario> <map> <timeout s> <logfile>
#
# Headless dedicated-server run of one scenario (tests/playtest/scenarios/<scenario>.cfg). The
# scenario drives the server through the sgtc_test_* cvars and ends with `quit`; the log keeps
# the SGTCTEST / SGTCEVENT lines that check_dedicated.py reads. Exit code line: EXIT=<n>.
D=$1; P=$2; SC=$3; MAP=$4; T=$5; LOG=$6
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$D" || exit 1
cp -n "$HERE/scenarios/$SC.cfg" "stargatetc/$SC.cfg"
rm -f stargatetc/userconfig.cfg
timeout -s KILL "$T" ./xash3d -dedicated -game stargatetc +hostport "$P" +map "$MAP" +exec "$SC.cfg" > "$LOG" 2>&1
echo "EXIT=$?" >> "$LOG"
