#!/bin/bash
# gui_map_sweep.sh <install dir> <port> <outdir> <map> [<map> ...]
#
# GUI client on each map (1024x768 offscreen): briefing shot, autopick, in-game shot. Writes
# <outdir>/<map>.log, <outdir>/<map>_<n>.png and a line per map to <outdir>/progress.txt. A map
# passes when its log ends with EXIT=0 and has no crash markers (check_dedicated.py clean).
D=$1; P=$2; OUT=$3; shift 3
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
for M in "$@"; do
  rm -f "$D"/stargatetc/scrshots/*
  python3 "$HERE/gui_scenarios.py" map "$D/stargatetc/userconfig.cfg" "$M"
  DEV=0 "$HERE/run_gui.sh" "$D" 1024 768 "$P" 90 "$OUT/$M.log"
  i=0
  for f in $(ls "$D"/stargatetc/scrshots/*.png 2>/dev/null | sort); do cp "$f" "$OUT/${M}_$i.png"; i=$((i+1)); done
  echo "$M shots=$i" >> "$OUT/progress.txt"
done
echo WORKER_DONE >> "$OUT/progress.txt"
