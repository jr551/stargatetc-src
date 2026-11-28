#!/bin/bash
# private_copy.sh <dest dir> [build dir]
#
# Makes a private playtest install: a hard-linked copy of the shared install (cheap, and the
# assets stay shared), with real copies of every file the engine rewrites, empty logs, scrshots
# and save dirs, and the game libraries from the build under test. Several sessions can then run
# engines side by side without touching each other's config.cfg or screenshots.
#
#   SGTC_INSTALL  source install (default /home/deck/Projects/stargatetc-playtest/stargatetc-linux-x86)
#   build dir     a 32-bit build tree holding dlls/hl.so and cl_dll/client.so (default ./build)
set -e
SRC=${SGTC_INSTALL:-/home/deck/Projects/stargatetc-playtest/stargatetc-linux-x86}
DST=${1:?usage: private_copy.sh <dest dir> [build dir]}
BUILD=${2:-build}
[ -d "$DST" ] || cp -al "$SRC" "$DST"
for f in "$DST"/stargatetc/*.cfg "$DST"/stargatetc/*.txt "$DST"/stargatetc/*.scr "$DST"/stargatetc/*.bak \
         "$DST"/stargatetc/*.lst "$DST"/*.cfg "$DST"/*.log "$DST"/*.txt; do
  [ -f "$f" ] || continue
  cp --remove-destination "$(readlink -f "$f")" "$f.tmp.$$" && mv -f "$f.tmp.$$" "$f"
done
for d in logs scrshots save; do rm -rf "$DST/stargatetc/$d"; mkdir -p "$DST/stargatetc/$d"; done
for pair in "cl_dll/client.so:cl_dlls/client.so" "dlls/hl.so:dlls/hl.so"; do
  rm -f "$DST/stargatetc/${pair##*:}"
  cp "$BUILD/${pair%%:*}" "$DST/stargatetc/${pair##*:}"
done
rm -f "$DST/stargatetc/userconfig.cfg"
cp "$(dirname "$0")"/scenarios/*.cfg "$DST/stargatetc/"
echo "private copy ready: $DST"
