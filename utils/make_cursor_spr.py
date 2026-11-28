#!/usr/bin/env python3
"""Write sprites/cursor.spr, the arrow cursor the HUD loads at start-up.

The client loads "sprites/cursor.spr" (cl_dll/hud_redraw.cpp, tri.cpp). The cleanroom valve/ set
does not ship it, so every map load logged "Error: Could not load HUD sprite sprites/cursor.spr"
(a red line in the on-screen notify area now that the console font works). This writes a plain
32x32 alpha-tested arrow in the same IDSP v2 layout as the other valve/ sprites: palette index 0
black outline, 1 white fill, 255 transparent. Hot spot is the top-left corner.

Usage: make_cursor_spr.py <out.spr>      (for example valve/sprites/cursor.spr)
"""
import struct
import sys

ARROW = [
    "X",
    "XX",
    "X.X",
    "X..X",
    "X...X",
    "X....X",
    "X.....X",
    "X......X",
    "X.......X",
    "X........X",
    "X.........X",
    "X..........X",
    "X......XXXXXX",
    "X...X..X",
    "X..X X..X",
    "X.X  X..X",
    "XX    X..X",
    "X     X..X",
    "       X..X",
    "       X..X",
    "        XX",
]
W = H = 32


def build():
    pix = bytearray([255] * (W * H))
    for y, row in enumerate(ARROW):
        for x, c in enumerate(row):
            if c == "X":
                pix[y * W + x] = 0
            elif c == ".":
                pix[y * W + x] = 1
    palette = bytearray(768)
    palette[0:3] = bytes((0, 0, 0))
    palette[3:6] = bytes((255, 255, 255))
    palette[765:768] = bytes((0, 0, 255))
    radius = ((W * W + H * H) ** 0.5) / 2.0
    # IDSP header: id, version 2, type 2 (vp_parallel), texFormat 3 (alphatest), bounding radius,
    # width, height, frames, beam length, sync type. Then the palette and one frame.
    out = struct.pack("<4siiifiiifi", b"IDSP", 2, 2, 3, radius, W, H, 1, 0.0, 0)
    out += struct.pack("<h", 256) + bytes(palette)
    out += struct.pack("<iiiii", 0, 0, 0, W, H) + bytes(pix)
    return out


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    data = build()
    with open(sys.argv[1], "wb") as f:
        f.write(data)
    print("%s: %d bytes" % (sys.argv[1], len(data)))


if __name__ == "__main__":
    main()
