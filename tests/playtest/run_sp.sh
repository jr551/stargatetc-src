#!/bin/bash
# run_sp.sh <install dir> <port> <map> <logfile>
#
# Single-player run (deathmatch 0) with the null renderer: the GUI engine, not -dedicated, which
# forces deathmatch 1 and can never reach the single-player rules. sgtc_test_enable 1 turns on the
# SGTC_SP_ENEMIES lines that check_dedicated.py sp reads. Quits itself at 28 s of game time.
D=$1; P=$2; M=$3; LOG=$4
cat > "$D/stargatetc/userconfig.cfg" <<CFG
deathmatch 0
sgtc_test_enable 1
sgtc_test_timed_script "info;players"
sgtc_test_timed_delay 8
sgtc_test_timed2_script "info"
sgtc_test_timed2_delay 20
sgtc_test_timed3_script "info;quit"
sgtc_test_timed3_delay 28
map $M
wait
wait
wait
wait
CFG
cd "$D" || exit 1
timeout -k 5 -s INT 90 ./xash3d -game stargatetc -ref null -nosound -dev 2 +hostport "$P" > "$LOG" 2>&1
echo "EXIT=$?" >> "$LOG"
rm -f "$D/stargatetc/userconfig.cfg"
