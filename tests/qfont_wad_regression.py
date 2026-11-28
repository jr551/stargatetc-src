#!/usr/bin/env python3
"""Check that the shipped Half-Life font lumps are ones the Xash3D engine can load.

Usage:
    python3 tests/qfont_wad_regression.py /path/to/valve [/path/to/other/dir ...]

The console font (gfx.wad CONCHARS, fonts.wad FONT0-2) and the HUD font (gfx.wad
CREDITSFONT) are QFONT lumps (WAD type 70). The engine's Image_LoadFNT
(engine/common/imagelib/img_wad.c) reads a 16-bit colour count and a 768-byte
palette straight after the pixel data and refuses the lump unless the count is
256 or 768. It also recognises the Half-Life 1.1 layout by an exact size:

    expected = sizeof(qfont_t) - 4 + height * width * 16 + 2 + 768 + 64

A lump without that trailer fails to load ("FS_LoadImage: couldn't load
gfx/conchars"), and then the console, chat text and the HUD font draw nothing.
Palette index 255 is the transparent colour of the masked palette the engine
applies, so a font whose background is not 255 draws a solid box behind every
glyph.

utils/fix_qfont_wad.py repairs lumps that fail this check.
"""
import os
import struct
import sys

QFONT_TYPE = 70
QCHAR_WIDTH = 16
HEADER = 16 + 256 * 4
TRAILER = 2 + 768 + 64
WADS = ("gfx.wad", "fonts.wad")
EXPECTED = {"gfx.wad": ("CONCHARS", "CREDITSFONT"), "fonts.wad": ("FONT0", "FONT1", "FONT2")}


def lumps(path):
    d = open(path, "rb").read()
    magic, n, off = struct.unpack("<4sii", d[:12])
    if magic != b"WAD3":
        raise ValueError("not a WAD3 file")
    for i in range(n):
        o = off + i * 32
        filepos, _disk, size, typ, _comp, _pad, name = struct.unpack("<iiibbh16s", d[o:o + 32])
        yield name.split(b"\0")[0].decode("latin1"), typ, d[filepos:filepos + size]


def check_lump(name, data):
    problems = []
    if len(data) < HEADER:
        return ["lump shorter than the qfont header"]
    width, height, _rows, _rowheight = struct.unpack("<iiii", data[:16])
    pixels = width * QCHAR_WIDTH * height
    want = HEADER + pixels + TRAILER
    if len(data) != want:
        problems.append("size %d, the engine expects %d (missing palette trailer?)" % (len(data), want))
        return problems
    off = HEADER + pixels
    colors = struct.unpack("<h", data[off:off + 2])[0]
    if colors not in (256, 768):
        problems.append("palette colour count %d, the engine needs 256 or 768" % colors)
    body = data[HEADER:HEADER + pixels]
    if 255 not in body:
        problems.append("no pixel uses index 255, so the glyph background is not transparent")
    if not any(b != 255 for b in body):
        problems.append("every pixel is the transparent index, so the font is blank")
    return problems


def check_dir(valve):
    failures = 0
    for wad in WADS:
        path = os.path.join(valve, wad)
        if not os.path.isfile(path):
            print("FAIL %s: missing" % path)
            failures += 1
            continue
        try:
            found = {name.upper(): data for name, typ, data in lumps(path) if typ == QFONT_TYPE}
        except ValueError as e:
            print("FAIL %s: %s" % (path, e))
            failures += 1
            continue
        for name in EXPECTED[wad]:
            if name not in found:
                print("FAIL %s: no QFONT lump %s" % (path, name))
                failures += 1
                continue
            problems = check_lump(name, found[name])
            if problems:
                failures += 1
                print("FAIL %s %s: %s" % (path, name, "; ".join(problems)))
            else:
                print("ok   %s %s" % (path, name))
    return failures


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    failures = sum(check_dir(d) for d in sys.argv[1:])
    if failures:
        print("%d font lump problem(s)" % failures)
        return 1
    print("PASS: console and HUD font lumps load in the engine")
    return 0


if __name__ == "__main__":
    sys.exit(main())
