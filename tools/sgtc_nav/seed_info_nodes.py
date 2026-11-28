#!/usr/bin/env python3
# seed_info_nodes.py <in.bsp> <out.bsp> [--clear 32] [--step 104]
#
# Writes a copy of a GoldSrc BSP (v30) with one info_node entity per floor sample, appended to
# the entity lump. The stock node builder (dlls/nodes.cpp, CTestHull) builds the .nod graph from
# these seeds the first time the map loads. Build the graph in a private copy, then ship the
# map and maps/graphs/<map>.nod together; the .nod must be newer than the .bsp.
#
# Floor samples come from make_nav.py (same directory). Keep the count at or below 1024
# (MAX_NODES in dlls/nodes.cpp). MAX_NODE_INITIAL_LINKS must be 512 for dense seed sets.
import argparse, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_nav


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bsp_in')
    ap.add_argument('bsp_out')
    ap.add_argument('--clear', type=float, default=32.0)
    ap.add_argument('--step', type=int, default=104)
    args = ap.parse_args()

    floors, walls = make_nav.read_bsp(args.bsp_in)
    mp = make_nav.Map(floors, walls, args.clear)
    nodes, _ = make_nav.build(mp, args.step)
    if len(nodes) > 1024:
        sys.exit('%d seeds is over MAX_NODES (1024); raise --step or --clear' % len(nodes))

    data = bytearray(open(args.bsp_in, 'rb').read())
    off, ln = struct.unpack_from('<ii', data, 4)
    ent = bytes(data[off:off + ln]).rstrip(b'\x00')
    existing = ent.count(b'"info_node"')
    if existing:
        sys.exit('%s already has %d info_node entities; seed the original map, not a seeded one' % (args.bsp_in, existing))
    blocks = ''.join('{\n"classname" "info_node"\n"origin" "%d %d %d"\n}\n' % (round(x), round(y), round(z))
                     for x, y, z in nodes)
    new_ent = ent + blocks.encode('latin-1') + b'\x00'
    out = bytearray(bytes(data) + new_ent)
    struct.pack_into('<ii', out, 4, len(data), len(new_ent))
    with open(args.bsp_out, 'wb') as f:
        f.write(bytes(out))
    print('seeds %d (clearance %g, step %d) -> %s' % (len(nodes), args.clear, args.step, args.bsp_out))


if __name__ == '__main__':
    main()
