#!/usr/bin/env python3
"""Waypoint table for the single-player enemy route (dlls/sgtc_sp_nav_<map>.h).

Usage:
    make_nav.py <map.bsp> <map name> <out.h> [--step 96] [--clear 16] [--seed x:y:z]

Reads the BSP directly (GoldSrc v30). Floors are upward-facing faces (normal z > 0.45) of the
world and of every brush model, so doors, lifts and breakables count as floor. Walls are the
vertical faces of the world model only; a wall shorter than STEP_UP above the floor can be
stepped over, so stair risers do not cut the graph. Nodes sit on cell centres, one per floor
polygon under the centre; links join 8-neighbour cells when the straight line stays on floor.
The output keeps the connected component that contains the seed point and nothing else.
Generated data: rerun this tool instead of editing the header by hand.
"""
import argparse, math, re, struct
from collections import deque

FLOOR_NZ = 0.45
STEP_UP = 18.0
Z_MIN, Z_MAX = 20.0, 420.0


def read_bsp(path):
    data = open(path, 'rb').read()
    if struct.unpack_from('<i', data, 0)[0] != 30:
        raise SystemExit('not a GoldSrc v30 BSP: ' + path)

    def lump(i):
        return struct.unpack_from('<ii', data, 4 + i * 8)
    po, pl = lump(1); vo, vl = lump(3); eo, el = lump(12); so, sl = lump(13); fo, _ = lump(7); mo, ml = lump(14)
    planes = [struct.unpack_from('<4f', data, po + i * 20) for i in range(pl // 20)]
    verts = [struct.unpack_from('<3f', data, vo + i * 12) for i in range(vl // 12)]
    edges = [struct.unpack_from('<2H', data, eo + i * 4) for i in range(el // 4)]
    surfedges = [struct.unpack_from('<i', data, so + i * 4)[0] for i in range(sl // 4)]
    floors, walls = [], []
    for m in range(ml // 64):
        firstface, nfaces = struct.unpack_from('<ii', data, mo + m * 64 + 56)
        for fi in range(firstface, firstface + nfaces):
            planenum, side, firstedge, numedges, _tex = struct.unpack_from('<HhihH', data, fo + fi * 20)
            nx, ny, nz, _d = planes[planenum]
            if side:
                nx, ny, nz = -nx, -ny, -nz
            pts = []
            for k in range(numedges):
                se = surfedges[firstedge + k]
                pts.append(verts[edges[se][0] if se >= 0 else edges[-se][1]])
            if nz > FLOOR_NZ:
                floors.append({'n': (nx, ny, nz), 'pts': pts})
            elif m == 0 and abs(nz) < 0.3:
                walls.append({'pts': pts, 'ztop': max(p[2] for p in pts)})
    return floors, walls


def plane_z(f, x, y):
    nx, ny, nz = f['n']; p0 = f['pts'][0]
    return (nx * p0[0] + ny * p0[1] + nz * p0[2] - nx * x - ny * y) / nz


def pip(x, y, pts):
    inside = False; j = len(pts) - 1
    for i in range(len(pts)):
        xi, yi = pts[i][0], pts[i][1]; xj, yj = pts[j][0], pts[j][1]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / ((yj - yi) or 1e-9) + xi:
            inside = not inside
        j = i
    return inside


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0.0 if L == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


class Map:
    def __init__(self, floors, walls, clear):
        self.clear = clear
        self.floors = [f for f in floors if Z_MIN - 30 <= max(p[2] for p in f['pts']) and min(p[2] for p in f['pts']) <= Z_MAX + 30]
        for f in self.floors:
            f['bb'] = (min(p[0] for p in f['pts']), min(p[1] for p in f['pts']), max(p[0] for p in f['pts']), max(p[1] for p in f['pts']))
        self.B = 256
        self.wb = {}
        for w in walls:
            p = w['pts']
            for i in range(len(p)):
                a, b = p[i], p[(i + 1) % len(p)]
                s = (a[0], a[1], b[0], b[1], w['ztop'])
                x0, x1 = sorted((s[0], s[2])); y0, y1 = sorted((s[1], s[3]))
                for bx in range(int(math.floor((x0 - 64) / self.B)), int(math.floor((x1 + 64) / self.B)) + 1):
                    for by in range(int(math.floor((y0 - 64) / self.B)), int(math.floor((y1 + 64) / self.B)) + 1):
                        self.wb.setdefault((bx, by), []).append(s)

    def walls_near(self, x, y, reach):
        for s in self.wb.get((int(math.floor(x / self.B)), int(math.floor(y / self.B))), []):
            if seg_dist(x, y, s[0], s[1], s[2], s[3]) < reach:
                yield s

    def blocked(self, x, y, z, reach):
        return any(s[4] > z + STEP_UP for s in self.walls_near(x, y, reach))

    def floors_at(self, x, y):
        px, py = x + 0.31, y + 0.47
        out = []
        for f in self.floors:
            bx0, by0, bx1, by1 = f['bb']
            if px < bx0 or px > bx1 or py < by0 or py > by1:
                continue
            if pip(px, py, f['pts']):
                z = plane_z(f, x, y)
                if Z_MIN <= z <= Z_MAX:
                    out.append(z)
        return out


def build(mp, step):
    xs = [p[0] for f in mp.floors for p in f['pts']]
    ys = [p[1] for f in mp.floors for p in f['pts']]
    nodes, cells = [], {}
    half = step // 2
    for gx in range(int(min(xs)) // step * step, int(max(xs)) + step, step):
        for gy in range(int(min(ys)) // step * step, int(max(ys)) + step, step):
            cx, cy = gx + half, gy + half
            for z in mp.floors_at(cx, cy):
                if mp.blocked(cx, cy, z, mp.clear):
                    continue
                cells.setdefault((gx, gy), []).append(len(nodes))
                nodes.append((cx, cy, round(z, 1)))

    def sample_ok(a, b):
        last = a[2]
        for k in range(1, 9):
            t = k / 9.0
            x = a[0] + (b[0] - a[0]) * t; y = a[1] + (b[1] - a[1]) * t
            cands = mp.floors_at(x, y)
            if not cands:
                return False
            z = min(cands, key=lambda c: abs(c - last))
            if mp.blocked(x, y, z, mp.clear * 0.5) or abs(z - last) > 24:
                return False
            last = z
        return abs(last - b[2]) <= 24
    edges = []
    for (gx, gy), ids in cells.items():
        for dx, dy in ((step, 0), (0, step), (step, step), (step, -step)):
            for j in cells.get((gx + dx, gy + dy), []):
                for i in ids:
                    a, b = nodes[i], nodes[j]
                    if abs(a[2] - b[2]) <= 40 and sample_ok(a, b):
                        edges.append((i, j))
    return nodes, edges


def component(nodes, edges, seed):
    adj = [[] for _ in nodes]
    for a, b in edges:
        adj[a].append(b); adj[b].append(a)
    start = min(range(len(nodes)), key=lambda i: (nodes[i][0] - seed[0]) ** 2 + (nodes[i][1] - seed[1]) ** 2 + 4 * (nodes[i][2] - seed[2]) ** 2)
    seen = {start}; q = deque([start])
    while q:
        u = q.popleft()
        for v in adj[u]:
            if v not in seen:
                seen.add(v); q.append(v)
    return sorted(seen)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bsp'); ap.add_argument('mapname'); ap.add_argument('out')
    ap.add_argument('--step', type=int, default=96); ap.add_argument('--clear', type=float, default=16.0)
    ap.add_argument('--seed', required=True, help='x:y:z inside the wanted component')
    args = ap.parse_args()
    seed = tuple(float(v) for v in args.seed.split(':'))
    floors, walls = read_bsp(args.bsp)
    mp = Map(floors, walls, args.clear)
    nodes, edges = build(mp, args.step)
    keep = component(nodes, edges, seed)
    remap = {old: new for new, old in enumerate(keep)}
    kept_edges = [(remap[a], remap[b]) for a, b in edges if a in remap and b in remap]
    ident = re.sub(r'[^A-Za-z0-9]', '_', args.mapname).upper()
    camel = ''.join(p.capitalize() for p in re.split(r'[^A-Za-z0-9]+', args.mapname) if p)
    with open(args.out, 'w') as h:
        h.write('// Generated by tools/sgtc_nav/make_nav.py from %s. Do not edit by hand.\n' % args.mapname)
        h.write('// Waypoints: the connected floor area around seed %s (step %d, clearance %g).\n' % (args.seed, args.step, args.clear))
        h.write('#ifndef SGTC_SP_NAV_%s_H\n#define SGTC_SP_NAV_%s_H\n\n' % (ident, ident))
        h.write('#define SGTC_NAV_%s_NODES %d\n#define SGTC_NAV_%s_EDGES %d\n\n' % (ident, len(keep), ident, len(kept_edges)))
        h.write('static const short kSgtcNav%sNodes[SGTC_NAV_%s_NODES][3] =\n{\n' % (camel, ident))
        for i in keep:
            h.write('\t{ %d, %d, %d },\n' % (round(nodes[i][0]), round(nodes[i][1]), round(nodes[i][2])))
        h.write('};\n\n')
        h.write('static const short kSgtcNav%sEdges[SGTC_NAV_%s_EDGES][2] =\n{\n' % (camel, ident))
        for a, b in kept_edges:
            h.write('\t{ %d, %d },\n' % (a, b))
        h.write('};\n\n#endif\n')
    print('nodes %d (of %d), edges %d (of %d)' % (len(keep), len(nodes), len(kept_edges), len(edges)))


if __name__ == '__main__':
    main()
