"""Layered laser-cut version of the ORIGINAL shell + lid: the STL is sliced into sheets of thickness T, so chamfered corners,
corner posts with screw holes, floor, USB opening and the lid's frames all come straight from your STL files.
Usage: python make_layers.py <shell.stl> <lid.stl> <outdir>"""
import sys, os, math, hashlib, trimesh, numpy as np
from shapely.geometry import Polygon, Point
from shapely.ops import unary_union
from shapely import affinity

T = 6.0                 # sheet thickness: 6 mm acrylic (CONFIRMED by user)
WALL_LAYERS = 8         # 8 x 6 = 48 mm; the original shell wall is 49.6 mm tall (9 layers would give 54 mm)

def region(mesh, z):
    s = mesh.section(plane_origin=[0, 0, z], plane_normal=[0, 0, 1])
    if s is None: return Polygon()
    g = Polygon()
    for loop in s.discrete:
        p = Polygon(loop[:, :2]).buffer(0)
        g = g.symmetric_difference(p)          # nested loops become holes (even-odd)
    return g

def circles(g):
    """Replace polygonised round holes by true circles where possible (smaller SVG, cleaner cuts)."""
    return g

def ring_paths(g):
    out = []
    for p in ([g] if g.geom_type == 'Polygon' else g.geoms):
        if p.is_empty: continue
        for r in [p.exterior] + list(p.interiors):
            c = list(r.coords)[:-1]
            out.append(c)
    return out

if __name__ == '__main__':
    shell = trimesh.load(sys.argv[1]); lid = trimesh.load(sys.argv[2]); out = sys.argv[3]; os.makedirs(out, exist_ok=True)
    pieces = []     # (name, polygon)
    z0 = shell.bounds[0][2]
    for k in range(WALL_LAYERS):
        a, b = z0 + k*T + 0.1, z0 + (k+1)*T - 0.1
        ga, gb = region(shell, a), region(shell, min(b, shell.bounds[1][2] - 0.1))
        g = ga.intersection(gb)                                    # solid only if solid through the whole layer (the last layer may stick out above the shell)
        pieces.append(('wall layer %02d' % (k+1), g))
    lz = lid.bounds[0][2]
    for k in range(3):                                             # plate, then the layer(s) of frames/rings that hang under it
        g = region(lid, lz + 1.5) if k == 0 else region(lid, lz + 3 + (k - 0.5)*T)   # the STL plate is 3 mm; its frames run from 3 to 11 mm
        if not g.is_empty and g.area > 1: pieces.append(('lid layer %d%s' % (k+1, ' (plate)' if k == 0 else ''), g))
    # top view: flip y for the shell (real orientation has y up); the lid STL is already y-down
    fixed = []
    for n, g in pieces:
        if n.startswith('wall'): g = affinity.scale(g, yfact=-1, origin=(0, 0))
        fixed.append((n, g))
    pieces = fixed
    # lid reference: all lid layers in their TRUE relative positions (use it to place the frame layers under the plate)
    cols = ['#9ca3af', '#2563eb', '#16a34a', '#f59e0b']
    ref = [(n, g) for n, g in pieces if n.startswith('lid')]
    allb = unary_union([g for _, g in ref]).bounds; M = 4
    sv = ['<svg xmlns="http://www.w3.org/2000/svg" width="%.0fmm" height="%.0fmm" viewBox="%.1f %.1f %.1f %.1f">' % (allb[2]-allb[0]+2*M, allb[3]-allb[1]+2*M, allb[0]-M, allb[1]-M, allb[2]-allb[0]+2*M, allb[3]-allb[1]+2*M), '<rect x="-1000" y="-1000" width="3000" height="3000" fill="#fff"/>']
    for (n, g), c in zip(ref, cols):
        for path in ring_paths(g): sv.append('<path d="M%s Z" fill="%s" fill-opacity="0.25" stroke="%s" stroke-width="0.4" fill-rule="evenodd"/>' % (' L'.join('%.2f,%.2f' % q for q in path), c, c))
    sv.append('</svg>'); open(os.path.join(out, 'lid_layer_positions_reference.svg'), 'w').write('\n'.join(sv))
    if '--noposts' in sys.argv:       # leave out the 8.8 mm corner posts (loose discs that only matter if you screw the lid down)
        pieces = [(n, (unary_union([p for p in ([g] if g.geom_type == 'Polygon' else g.geoms) if p.area > 200]) if n.startswith('wall layer') and int(n.split()[2]) >= 3 else g)) for n, g in pieces]
    # 3D preview of the stack (centred): wall layers, then lid plate on top with its frame layer hanging underneath
    try:
        from manifold3d import Manifold, CrossSection, FillRule
        def ext3(poly, t):
            rings = []
            for p in ([poly] if poly.geom_type == 'Polygon' else list(poly.geoms)):
                rings.append(np.array(p.exterior.coords)[:-1]); rings += [np.array(r.coords)[:-1] for r in p.interiors]
            return Manifold.extrude(CrossSection(rings, FillRule.EvenOdd), t)
        wl = [(n, g) for n, g in pieces if n.startswith('wall')]; ll = [(n, g) for n, g in pieces if n.startswith('lid')]
        c = wl[0][1].centroid; lc = ll[0][1].envelope.centroid
        os.makedirs(os.path.join(out, 'parts'), exist_ok=True)
        def save(name, poly, z, cen):
            if poly.is_empty: return
            m = ext3(affinity.scale(affinity.translate(poly, -cen.x, -cen.y), yfact=-1, origin=(0, 0)), T).translate([0, 0, z]).to_mesh()
            trimesh.Trimesh(np.round(np.array(m.vert_properties)[:, :3], 4), np.array(m.tri_verts), process=False).export(os.path.join(out, 'parts', name + '.stl'))
        for i, (n, g) in enumerate(wl): save('wall_%02d' % (i+1), g, i*T, c)
        top = len(wl)*T
        save('lid_plate', ll[0][1], top, lc)
        for j, (n, g) in enumerate(ll[1:]): save('lid_frames_%d' % (j+1), g, top - (j+1)*T, lc)
    except Exception as e: print('3D preview skipped:', e)
    # group identical pieces
    groups = {}
    for n, g in pieces:
        b = g.bounds; key = (round(g.area), round(b[2]-b[0], 1), round(b[3]-b[1], 1), len(ring_paths(g)))
        groups.setdefault(key, []).append((n, g))
    uniq = [(v[0][1], [n for n, _ in v]) for v in groups.values()]
    print('%d pieces total, %d different shapes' % (len(pieces), len(uniq)))
    for g, names in uniq:
        b = g.bounds; print('  x%-2d %-5.0f x %-5.0f area %6.0f  %s' % (len(names), b[2]-b[0], b[3]-b[1], g.area, ', '.join(names)))

    def write(path, items, cols, labels):
        G = 5.0; x = y = G; rowh = 0; placed = []
        for g, label in items:
            b = g.bounds; w, h = b[2]-b[0], b[3]-b[1]
            if x + w + G > cols: x = G; y += rowh + G; rowh = 0
            placed.append((affinity.translate(g, x - b[0], y - b[1]), label)); x += w + G; rowh = max(rowh, h)
        sw = max(p.bounds[2] for p, _ in placed) + G; sh = max(p.bounds[3] for p, _ in placed) + G
        s = ['<?xml version="1.0" encoding="UTF-8"?>', '<svg xmlns="http://www.w3.org/2000/svg" width="%.2fmm" height="%.2fmm" viewBox="0 0 %.2f %.2f">' % (sw, sh, sw, sh), '<g fill="none" stroke="#ff0000" stroke-width="0.1">']
        for p, _ in placed:
            for c in ring_paths(p): s.append('<path d="M%s Z"/>' % ' L'.join('%.3f,%.3f' % q for q in c))
        s.append('</g>')
        if labels:
            s.append('<g font-family="sans-serif" font-size="4.5" fill="#2563eb">')
            for p, label in placed:
                bb = p.bounds; s.append('<text x="%.1f" y="%.1f" text-anchor="middle">%s</text>' % ((bb[0]+bb[2])/2, bb[1] - 0.8, label))
            s.append('</g>')
        s.append('</svg>'); open(path, 'w').write('\n'.join(s)); return sw, sh
    # 1) one of each shape (+ labelled copy showing how many to cut)
    items = [(g, 'x%d: %s' % (len(n), n[0] if len(n) == 1 else n[0] + ' ... ' + n[-1])) for g, n in uniq]
    print('unique sheet %.0f x %.0f' % write(os.path.join(out, 'layers_unique_cut_one_of_each.svg'), items, 420, False))
    write(os.path.join(out, 'layers_unique_labelled.svg'), items, 420, True)
    # 2) the full job: every layer, repeated as often as needed
    full = [(g, n) for n, g in pieces]
    print('full job %.0f x %.0f' % write(os.path.join(out, 'layers_full_job.svg'), full, 520, False))
    write(os.path.join(out, 'layers_full_job_labelled.svg'), full, 520, True)
