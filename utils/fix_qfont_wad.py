#!/usr/bin/env python3
"""Repair QFONT (type 70) lumps in a WAD3 so the Xash3D engine can load them.

The engine (FWGS 0.21+, engine/common/imagelib/img_wad.c, Image_LoadFNT) reads
a 16-bit colour count and a 768-byte RGB palette straight after the pixel data
of a Half-Life font lump, and rejects the lump unless the count is 256 or 768.
It also tells the Half-Life 1.1 layout apart by an exact size check:

    expected = sizeof(qfont_t) - 4 + height * width * 16 + 2 + 768 + 64

Fonts that are missing the palette trailer never load ("FS_LoadImage: couldn't
load ..."), so the console and the HUD font draw nothing.

With the masked-palette convention the engine uses for fonts, palette index 255
is fully transparent and every other index is opaque. This tool therefore also
moves the background pixel value (0 in the generated sets) to 255, leaves the ink
indices alone, and writes a palette whose opaque entries are white, so the engine
tints the glyphs with the colour the console or HUD asks for.

Usage: fix_qfont_wad.py <in.wad> <out.wad>
"""
import struct
import sys

QFONT_TYPE = 70
QCHAR_WIDTH = 16
HEADER = 16 + 256 * 4          # width, height, rowcount, rowheight, fontinfo[256]
TRAILER = 2 + 768 + 64         # colour count, palette, "last sixty four bytes"


def read_wad(path):
    d = open(path, "rb").read()
    magic, n, off = struct.unpack("<4sii", d[:12])
    if magic != b"WAD3":
        raise SystemExit("%s: not a WAD3 file" % path)
    lumps = []
    for i in range(n):
        o = off + i * 32
        filepos, disksize, size, typ, comp, _pad, name = struct.unpack("<iiibbh16s", d[o:o + 32])
        lumps.append([name, typ, comp, d[filepos:filepos + size]])
    return lumps


def fix_qfont(data):
    """Return the repaired lump data, or the input when it is already complete."""
    width, height, _rowcount, _rowheight = struct.unpack("<iiii", data[:16])
    pixels = width * QCHAR_WIDTH * height
    full = HEADER + pixels + TRAILER
    if len(data) == full:
        return data                         # already carries the trailer
    if len(data) != HEADER + pixels:
        raise SystemExit("unexpected QFONT lump size %d (header+pixels = %d)"
                         % (len(data), HEADER + pixels))
    body = bytearray(data[HEADER:])
    # Background 0 -> 255 (transparent under the masked palette). A glyph that
    # already uses 255 for ink would clash, so refuse rather than guess.
    if 255 in body:
        raise SystemExit("lump already uses palette index 255; not converting")
    for i, v in enumerate(body):
        if v == 0:
            body[i] = 255
    palette = b"".join(b"\xff\xff\xff" for _ in range(255)) + b"\x00\x00\x00"
    return data[:HEADER] + bytes(body) + struct.pack("<h", 256) + palette + b"\x00" * 64


def write_wad(path, lumps):
    out = bytearray(b"WAD3" + struct.pack("<ii", len(lumps), 0))
    entries = []
    for name, typ, comp, data in lumps:
        while len(out) % 4:
            out.append(0)
        entries.append((len(out), len(data), typ, comp, name))
        out += data
    while len(out) % 4:
        out.append(0)
    dir_off = len(out)
    for pos, size, typ, comp, name in entries:
        out += struct.pack("<iiibbh16s", pos, size, size, typ, comp, 0, name)
    out[8:12] = struct.pack("<i", dir_off)
    open(path, "wb").write(out)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    lumps = read_wad(sys.argv[1])
    fixed = 0
    for lump in lumps:
        if lump[1] == QFONT_TYPE:
            new = fix_qfont(lump[3])
            if new is not lump[3]:
                fixed += 1
            lump[3] = new
    write_wad(sys.argv[2], lumps)
    print("%s: %d lumps, %d QFONT lumps repaired -> %s" % (sys.argv[1], len(lumps), fixed, sys.argv[2]))


if __name__ == "__main__":
    main()
