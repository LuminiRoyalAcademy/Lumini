"""Rounded, stacked-layer acrylic case for the Lumini telegraph (no finger joints, no sharp vertical corners).
6 mm acrylic, outer size 124.14 x 79.14 mm (same as the shell), rounded corners R, lid holes copied from telegraph_shell_lid.stl.
Usage: python make_laser_box3.py <lid.stl> <outdir>"""
import sys, os, trimesh, numpy as np
from shapely.geometry import box as B, Polygon, Point
from shapely.ops import unary_union
from shapely import affinity
from manifold3d import Manifold, CrossSection, FillRule

T = 6.0                       # acrylic thickness (CONFIRMED)
W, D = 124.14, 79.14          # outer size (from the shell STL)
R = 14.0                      # outer corner radius (round, no sharp corners)
WALL = 4.0                    # width of the wall rings
RINGS = 7                     # wall layers on top of the floor: 7 x 6 = 42 mm inside depth (original shell 43 mm)
USB = dict(w=20.0, dy=-0.23, layers=(1, 2))   # right wall opening, 20 mm wide, in ring layers 2 and 3 (= 12..24 mm; original opening is 14..24 mm)
BOSS = [(sx*52.8, sy*31.2) for sx in (-1, 1) for sy in (-1, 1)]   # corner bosses sit under the lid's four screw holes
BOSS_R, PIN = 5.5, 2.6        # boss radius, hole for a M3 self-tapping screw or a 2.5 mm alignment pin
LOC_BORDER, LOC_GAP = 3.5, 0.25

def rrect(w, d, r): return B(-w/2, -d/2, w/2, d/2).buffer(-r, quad_segs=24).buffer(r, quad_segs=24)
OUT = rrect(W, D, R)
IN = rrect(W - 2*WALL, D - 2*WALL, R - WALL)

def pins(): return unary_union([Point(x, y).buffer(PIN/2, quad_segs=12) for x, y in BOSS])
def bosses(): return unary_union([Point(x, y).buffer(BOSS_R, quad_segs=16) for x, y in BOSS])

def ring(notch=False):
    g = OUT.difference(IN).union(bosses().intersection(IN.buffer(0.01))) if False else OUT.difference(IN.difference(bosses()))
    g = g.difference(pins())
    if notch: g = g.difference(B(W/2 - WALL - 1, USB['dy'] - USB['w']/2, W/2 + 1, USB['dy'] + USB['w']/2))
    return g

def lid_parts(lidstl):
    m = trimesh.load(lidstl); z0 = m.bounds[0][2]
    def region(z):
        s = m.section(plane_origin=[0, 0, z], plane_normal=[0, 0, 1]); g = Polygon()
        for loop in s.discrete: g = g.symmetric_difference(Polygon(loop[:, :2]).buffer(0))
        return g
    plate, feat = region(z0 + 1.5), region(z0 + 3 + T/2)    # plate; frames/rings that hang under it (3 mm plate in the STL, features 3..11 mm)
    cx, cy = (plate.bounds[0] + plate.bounds[2]) / 2, (plate.bounds[1] + plate.bounds[3]) / 2
    plate, feat = affinity.translate(plate, -cx, -cy), affinity.translate(feat, -cx, -cy)    # centred; y-down = top view
    lid = plate.union(plate.envelope.difference(plate).intersection(plate.envelope) if False else Polygon())   # keep holes
    # the original lid has chamfered corners; make a rounded lid with the same holes: take the holes out of the plate and re-cut
    holes = [Polygon(r) for p in ([plate] if plate.geom_type == 'Polygon' else plate.geoms) for r in p.interiors]
    lid = OUT
    for h in holes:
        c = h.centroid
        if any(abs(c.x - bx) < 1.5 and abs(abs(c.y) - abs(by)) < 1.5 for bx, by in BOSS):     # the four screw holes: M3 clearance (3.2 mm)
            h = Point(c.x, c.y).buffer(1.6, quad_segs=12)
        lid = lid.difference(h)
    # pockets of frames must sit inside the walls: nudge towards the centre, then let the wall close the back of the pocket
    inner = IN.buffer(-0.3); parts = []
    for p in ([feat] if feat.geom_type == 'Polygon' else list(feat.geoms)):
        pocket = Polygon(p.interiors[0]) if p.interiors else p; sx = sy = 0.0
        for _ in range(300):
            q = affinity.translate(pocket, sx, sy); ox = q.difference(inner)
            if ox.is_empty: break
            b = ox.bounds; c = pocket.centroid
            if (b[2]-b[0]) >= (b[3]-b[1]): sy -= 0.1*np.sign(c.y)
            else: sx -= 0.1*np.sign(c.x)
        if abs(sx) + abs(sy): print('  frame part moved by (%.1f, %.1f) mm to clear the wall' % (sx, sy))
        parts.append(affinity.translate(p, sx, sy).intersection(inner))
    feat = unary_union(parts).difference(bosses().buffer(0.3))
    return lid, feat

def locator():
    o = rrect(W - 2*(WALL + LOC_GAP), D - 2*(WALL + LOC_GAP), R - WALL - LOC_GAP)
    return o.difference(o.buffer(-LOC_BORDER)).difference(bosses().buffer(0.3))

def ext3(poly, t):
    polys = [poly] if poly.geom_type == 'Polygon' else list(poly.geoms); rings = []
    for p in polys:
        rings.append(np.array(p.exterior.coords)[:-1])
        for r in p.interiors: rings.append(np.array(r.coords)[:-1])
    return Manifold.extrude(CrossSection(rings, FillRule.EvenOdd), t)

def build(lidstl):
    P = {}; P['floor'] = OUT.difference(pins())
    for k in range(RINGS): P['wall_%d' % (k+1)] = ring(notch=(k in USB['layers']))
    lid, feat = lid_parts(lidstl)
    P['lid'] = lid; P['lid_frames'] = feat      # the lid is located and held by four M3 screws into the corner bosses
    return P

def assemble(P):
    A = {}; flip = lambda g: affinity.scale(g, yfact=-1, origin=(0, 0))
    A['floor'] = ext3(P['floor'], T)
    for k in range(RINGS): A['wall_%d' % (k+1)] = ext3(P['wall_%d' % (k+1)], T).translate([0, 0, T*(k+1)])
    top = T*(RINGS+1)
    A['lid'] = ext3(flip(P['lid']), T).translate([0, 0, top])
    A['lid_frames'] = ext3(flip(P['lid_frames']), T).translate([0, 0, top - T])
    return A

def check(A):
    names = list(A); bad = 0
    for i in range(len(names)):
        for j in range(i+1, len(names)):
            v = (A[names[i]] ^ A[names[j]]).volume()
            if v > 0.5: print('OVERLAP %s / %s %.1f mm3' % (names[i], names[j], v)); bad += 1
    print('overlap check:', 'OK, no parts collide' if not bad else '%d problems' % bad)

def write_svg(items, path, cols=520, labels=False):
    G = 5.0; x = y = G; rowh = 0; placed = []
    for name, g in items:
        b = g.bounds; w, h = b[2]-b[0], b[3]-b[1]
        if x + w + G > cols: x = G; y += rowh + G; rowh = 0
        placed.append((name, affinity.translate(g, x - b[0], y - b[1]))); x += w + G; rowh = max(rowh, h)
    sw = max(p.bounds[2] for _, p in placed) + G; sh = max(p.bounds[3] for _, p in placed) + G
    s = ['<?xml version="1.0" encoding="UTF-8"?>', '<svg xmlns="http://www.w3.org/2000/svg" width="%.2fmm" height="%.2fmm" viewBox="0 0 %.2f %.2f">' % (sw, sh, sw, sh), '<g fill="none" stroke="#ff0000" stroke-width="0.1">']
    for _, p in placed:
        for q in ([p] if p.geom_type == 'Polygon' else p.geoms):
            for r in [q.exterior] + list(q.interiors): s.append('<path d="M%s Z"/>' % ' L'.join('%.3f,%.3f' % c for c in list(r.coords)[:-1]))
    s.append('</g>')
    if labels:
        s.append('<g font-family="sans-serif" font-size="4.5" fill="#2563eb">')
        for n, p in placed: bb = p.bounds; s.append('<text x="%.1f" y="%.1f" text-anchor="middle">%s</text>' % ((bb[0]+bb[2])/2, bb[1]-0.8, n))
        s.append('</g>')
    s.append('</svg>'); open(path, 'w').write('\n'.join(s)); return sw, sh

if __name__ == '__main__':
    out = sys.argv[2]; os.makedirs(out, exist_ok=True)
    P = build(sys.argv[1]); A = assemble(P); check(A)
    items = list(P.items())
    print('sheet %.0f x %.0f mm' % write_svg(items, os.path.join(out, 'telegraph_case_rounded_T6.svg')))
    write_svg(items, os.path.join(out, 'telegraph_case_rounded_labelled.svg'), labels=True)
    for k, m in A.items():
        mg = m.to_mesh(); trimesh.Trimesh(np.round(np.array(mg.vert_properties)[:, :3], 4), np.array(mg.tri_verts), process=False).export(os.path.join(out, 'part_%s.stl' % k))
    print('total height %.0f mm (floor + %d rings + lid)' % (T*(RINGS+2), RINGS))
