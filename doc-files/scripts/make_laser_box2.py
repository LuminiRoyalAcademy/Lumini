"""Finger-jointed laser box that keeps the ORIGINAL chamfered outline (8 wall panels) + the original lid, with the lid's frames as layers.
Usage: python make_laser_box2.py <lid.stl> <outdir>   (sizes from telegraph_shellreal.stl / telegraph_shell_lid.stl)"""
import sys, os, math, trimesh, numpy as np
from shapely.geometry import box as B, Polygon, Point
from shapely.ops import unary_union
from shapely import affinity
from manifold3d import Manifold, CrossSection, FillRule

T   = 6.0                    # sheet thickness: 6 mm acrylic (CONFIRMED by user)
W, D, H = 124.14, 79.14, 49.6
CH  = 10.5                   # corner chamfer leg. Original is 7.07, but with 6 mm sheet the 10 mm corner panel would be too narrow for fingers
N   = 9                      # finger segments up each edge (odd)
E   = round(T*0.7071 + 0.15, 2)   # recess of the short finger at the 45-degree corners (no collisions below ~0.71*T)
FIT = 0.0                    # extra slot clearance (add ~0.1 if tight)
USB = dict(w=20.0, h=10.0, z0=13.6, dy=-0.23)
LOC_BORDER, LOC_FIT = 3.5, 0.4
seg = H / N

# octagon, outer vertices CCW starting bottom-left of the front edge
V = [(-W/2+CH, -D/2), (W/2-CH, -D/2), (W/2, -D/2+CH), (W/2, D/2-CH), (W/2-CH, D/2), (-W/2+CH, D/2), (-W/2, D/2-CH), (-W/2, -D/2+CH)]
NAMES = ['front', 'chamfer_FR', 'right', 'chamfer_BR', 'back', 'chamfer_BL', 'left', 'chamfer_FL']
OUT = Polygon(V)

def panel2d(i):
    a, b = np.array(V[i]), np.array(V[(i+1) % 8]); L = float(np.linalg.norm(b - a)); rects = []
    for k in range(N):
        ext = (k % 2 == 0) == (i % 2 == 0)
        rects.append(B(0 if ext else E, k*seg, L if ext else L - E, (k+1)*seg))
    p = unary_union(rects)
    # floor slots (z = T..2T). Straight panels: 3 or 2 slots of 20; chamfer panels: one 4 mm slot
    if i % 2 == 0:
        ucs, sw = ([L/2 - 35, L/2, L/2 + 35], 20.0) if L > 90 else ([L/2 - 12.5, L/2 + 12.5], 18.0)
    else: ucs, sw = [L/2], 4.0
    for uc in ucs: p = p.difference(B(uc - sw/2 - FIT/2, T - FIT/2, uc + sw/2 + FIT/2, 2*T + FIT/2))
    if NAMES[i] == 'right':
        uc = L/2 + USB['dy']; p = p.difference(B(uc - USB['w']/2, USB['z0'], uc + USB['w']/2, USB['z0'] + USB['h']))
    return p, L, ucs, sw

def floor2d():
    f = OUT.buffer(-T, join_style=2)
    for i in range(8):
        a, b = np.array(V[i]), np.array(V[(i+1) % 8]); L = np.linalg.norm(b - a); d = (b - a) / L; n = np.array([-d[1], d[0]])
        _, _, ucs, sw = panel2d(i)
        for uc in ucs:
            p0 = a + d*(uc - sw/2); p1 = a + d*(uc + sw/2)
            f = f.union(Polygon([p0, p1, p1 + n*(T + 0.02), p0 + n*(T + 0.02)]))   # tab: spans the wall thickness
    return f

def lid_pieces(lidstl):
    m = trimesh.load(lidstl); z0 = m.bounds[0][2]
    def region(z):
        s = m.section(plane_origin=[0, 0, z], plane_normal=[0, 0, 1]); g = Polygon()
        if s is None: return g
        for loop in s.discrete: g = g.symmetric_difference(Polygon(loop[:, :2]).buffer(0))
        return g
    out = []
    PLATE = 3.0                                   # plate thickness in the STL (features hang below it, from z=3 to 11)
    for k in range(4):
        g = region(z0 + 1.5) if k == 0 else region(z0 + PLATE + (k - 0.5)*T)
        if not g.is_empty and g.area > 1: out.append(g)
    cx = (out[0].bounds[0] + out[0].bounds[2]) / 2; cy = (out[0].bounds[1] + out[0].bounds[3]) / 2
    out = [affinity.translate(g, -cx, -cy) for g in out]       # centred, y-down = top view
    inner = OUT.buffer(-(T + 0.3), join_style=2)             # nothing hanging under the lid may touch the walls
    for idx in range(1, len(out)):
        parts = []
        for p in ([out[idx]] if out[idx].geom_type == 'Polygon' else list(out[idx].geoms)):
            pocket = Polygon(p.interiors[0]) if p.interiors else p
            sx = sy = 0.0
            for _ in range(300):                              # move the whole frame towards the centre until its POCKET fits inside the walls
                if inner.contains(affinity.translate(pocket, sx, sy)): break
                c = pocket.centroid; ox = affinity.translate(pocket, sx, sy).difference(inner)
                if ox.is_empty: break
                b = ox.bounds
                if (b[2] - b[0]) >= (b[3] - b[1]): sy -= 0.1 * np.sign(c.y)       # sticking out in y -> move in y
                else: sx -= 0.1 * np.sign(c.x)
            q = affinity.translate(p, sx, sy).intersection(inner)                  # the wall itself closes the pocket; trim the rim at the wall
            if abs(sx) + abs(sy) > 0: print('  lid-layer part moved by (%.1f, %.1f) mm to clear the 6 mm walls' % (sx, sy))
            parts.append(q)
        out[idx] = unary_union(parts)
    out[0] = out[0].intersection(OUT)                          # re-cut the lid outline to the (slightly larger) chamfer
    return out

def locator():
    o = OUT.buffer(-(T + LOC_FIT/2), join_style=2)
    return o.difference(o.buffer(-LOC_BORDER, join_style=2))

# ---------- 3D (collision check + preview) ----------
def ext3(poly, t):
    polys = [poly] if poly.geom_type == 'Polygon' else list(poly.geoms); rings = []
    for p in polys:
        rings.append(np.array(p.exterior.coords)[:-1])
        for r in p.interiors: rings.append(np.array(r.coords)[:-1])
    return Manifold.extrude(CrossSection(rings, FillRule.EvenOdd), t)

def panel3d(i, p2):
    a, b = np.array(V[i]), np.array(V[(i+1) % 8]); d = (b - a) / np.linalg.norm(b - a); n = np.array([-d[1], d[0]])
    org = a + T*n                                   # x->d (u), y->z (v), z->-n (thickness runs inward from the outer face)
    M = [[d[0], 0, -n[0], org[0]], [d[1], 0, -n[1], org[1]], [0, 1, 0, 0]]
    return ext3(p2, T).transform(M)

def build():
    P = {n: panel2d(i)[0] for i, n in enumerate(NAMES)}
    P['floor'] = floor2d(); lp = lid_pieces(sys.argv[1])
    P['lid'] = lp[0]
    for k, g in enumerate(lp[1:], 2): P['lid_layer%d' % k] = g
    loc = locator()
    for g in lp[1:]: loc = loc.difference(g.buffer(0.3))     # keep the locator ring clear of the lid's frames
    loc = unary_union([q for q in ([loc] if loc.geom_type == 'Polygon' else loc.geoms) if q.area > 150])   # drop loose slivers
    P['locator'] = loc
    return P

def assemble(P):
    A = {}
    for i, n in enumerate(NAMES): A[n] = panel3d(i, P[n])
    A['floor'] = ext3(P['floor'], T).translate([0, 0, T])
    flip = lambda g: affinity.scale(g, yfact=-1, origin=(0, 0))
    A['lid'] = ext3(flip(P['lid']), T).translate([0, 0, H])
    A['locator'] = ext3(flip(P['locator']), T).translate([0, 0, H - T])
    for k in (2, 3, 4):
        if 'lid_layer%d' % k in P: A['lid_layer%d' % k] = ext3(flip(P['lid_layer%d' % k]), T).translate([0, 0, H - (k-1)*T])
    return A

def check(A):
    names = list(A); bad = 0
    for i in range(len(names)):
        for j in range(i+1, len(names)):
            v = (A[names[i]] ^ A[names[j]]).volume()
            if v > 0.5: print('OVERLAP %s / %s %.1f mm3' % (names[i], names[j], v)); bad += 1
    print('overlap check:', 'OK, no parts collide' if not bad else '%d problems' % bad)

def write_svg(items, path, cols=330, labels=False):
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
    P = build(); A = assemble(P); check(A)
    # flat parts for cutting: panels are drawn with the outside face up, v pointing up -> flip for SVG (y down)
    items = []
    for n, g in P.items():
        if n in NAMES: g = affinity.scale(g, yfact=-1, origin=(0, H/2))
        items.append((n, g))
    print('sheet %.0f x %.0f mm' % write_svg(items, os.path.join(out, 'telegraph_box_chamfer_laser_T%g.svg' % T)))
    write_svg(items, os.path.join(out, 'telegraph_box_chamfer_labelled.svg'), labels=True)
    for k, m in A.items():
        mg = m.to_mesh(); trimesh.Trimesh(np.round(np.array(mg.vert_properties)[:, :3], 4), np.array(mg.tri_verts), process=False).export(os.path.join(out, 'part_%s.stl' % k))
    for n in NAMES[:2]: print(n, 'length %.2f' % panel2d(NAMES.index(n))[1])
