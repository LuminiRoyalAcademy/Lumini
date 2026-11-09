"""Scalloped rotary-encoder knob (design F) + a D-hole fit-test plate.

Run:  python make_scalloped_knob.py
Writes scalloped_knob_hole5_print.stl (top face on the bed, hole facing up) and fit_test_rings.stl.
All sizes are millimetres. Change the numbers below and run it again.
"""
import math, numpy as np
from shapely.geometry import Point, Polygon
from manifold3d import Manifold, CrossSection
import trimesh

# ---------------- knob size ----------------
OUTER_R      = 11.0     # lobe radius            (22 mm across)
HEIGHT       = 10.0     # total height
SCOOPS       = 6        # number of finger scoops
SCOOP_R      = 4.5      # radius of each scoop circle
SCOOP_DEPTH  = 2.3      # how far each scoop cuts into the rim
LOBE_ROUND   = 0.8      # rounding of the lobe corners
TOP_CHAMFER  = 1.0      # 45 degree chamfer round the top edge
BOT_CHAMFER  = 0.5      # small chamfer round the bottom edge

# ---------------- shaft hole (from encoder_hold.dxf: 6.0 round, flat 1.5 from centre) ----------------
SHAFT_R      = 3.0      # nominal round radius (6 mm shaft)
FLAT_DIST    = 1.5      # nominal flat distance from the centre (4.5 mm across the flat)
CLEARANCE    = 0.15     # extra on the hole DIAMETER for a press fit on a 3D printer (0.10 to 0.20)
HOLE_DEPTH   = 5.0      # blind hole depth. 5.0 = the thickness of gear_encoder.stl. Set this to the real shaft length once measured.
LEAD_IN      = 0.6      # funnel at the hole entrance, makes it easy to push on
FLAT_FACES   = 90.0     # direction the flat faces, degrees (only matters for where the flat sits)

def d_profile(clear, scale=1.0, n=96):
    r = (SHAFT_R + clear / 2) * scale
    d = (FLAT_DIST + clear / 2) * scale
    half = math.acos(d / r)
    mid = math.radians(FLAT_FACES)
    a = np.linspace(mid + half, mid + 2 * math.pi - half, n)
    return np.c_[r * np.cos(a), r * np.sin(a)]

def scalloped_outline():
    body = Point(0, 0).buffer(OUTER_R, 128)
    centre_dist = OUTER_R - SCOOP_DEPTH + SCOOP_R
    for i in range(SCOOPS):
        a = 2 * math.pi * i / SCOOPS
        body = body.difference(Point(centre_dist * math.cos(a), centre_dist * math.sin(a)).buffer(SCOOP_R, 96))
    body = body.buffer(-LOBE_ROUND, 32).buffer(LOBE_ROUND, 32)      # round the sharp lobe corners
    body = body.simplify(0.004)                                     # fewer points, still smooth to 0.004 mm
    pts = np.array(body.exterior.coords)[:-1]
    area = 0.5 * np.sum(pts[:, 0] * np.roll(pts[:, 1], -1) - np.roll(pts[:, 0], -1) * pts[:, 1])
    return pts if area > 0 else pts[::-1]                          # the 3D library wants counter-clockwise

def to_mesh(m):
    mg = m.to_mesh()
    mesh = trimesh.Trimesh(np.round(np.array(mg.vert_properties)[:, :3], 4), np.array(mg.tri_verts), process=False)
    mesh.merge_vertices()                                           # STL stores float32, so snap near-equal corners
    mesh.update_faces(mesh.nondegenerate_faces())
    mesh.remove_unreferenced_vertices()
    return mesh

def build_knob():
    outline = scalloped_outline()
    prism = Manifold.extrude(CrossSection([outline]), HEIGHT)
    R = OUTER_R + 0.5
    env = CrossSection([np.array([[0, 0], [OUTER_R - BOT_CHAMFER, 0], [OUTER_R, BOT_CHAMFER],
                                  [OUTER_R, HEIGHT - TOP_CHAMFER], [OUTER_R - TOP_CHAMFER, HEIGHT],
                                  [0, HEIGHT]])])
    envelope = Manifold.revolve(env, 128)
    body = prism ^ envelope                                         # chamfer the top and bottom edges
    hole = Manifold.extrude(CrossSection([d_profile(CLEARANCE)]), HOLE_DEPTH + 0.5).translate([0, 0, -0.5])
    # The funnel is a little taller than LEAD_IN and ends slightly SMALLER than the hole, so its
    # surface crosses the hole wall instead of sitting exactly on it (that keeps the mesh clean).
    top_scale = 0.95 / 1.25
    funnel = Manifold.extrude(CrossSection([d_profile(CLEARANCE, 1.25)]), LEAD_IN * 1.17, scale_top=(top_scale, top_scale))
    return body - hole - funnel

def build_fit_rings():
    parts = []
    for i, c in enumerate([0.0, 0.1, 0.2, 0.3]):
        ring = Manifold.cylinder(3.0, 6.5, 6.5, 96)
        hole = Manifold.extrude(CrossSection([d_profile(c)]), 4).translate([0, 0, -0.5])
        ring = ring - hole
        for k in range(i + 1):                                      # i+1 notches tell the rings apart
            ang = math.radians(-60 + k * 30)
            notch = Manifold.cube([1.4, 2.0, 4.0], True).translate([6.5, 0, 1.5]).rotate([0, 0, math.degrees(ang)])
            ring = ring - notch
        parts.append(ring.translate([i * 16.0, 0, 0]))
    out = parts[0]
    for p in parts[1:]:
        out = out + p
    return out

if __name__ == '__main__':
    knob = to_mesh(build_knob())
    print('knob   watertight=%s  volume=%.1f mm3  size=%s' % (knob.is_watertight, knob.volume, np.round(knob.extents, 2)))
    # flip so the TOP face lies on the print bed and the hole opens upwards (no bridging over the hole)
    flipped = knob.copy()
    flipped.apply_transform(trimesh.transformations.rotation_matrix(math.pi, [1, 0, 0]))
    flipped.apply_translation([0, 0, -flipped.bounds[0][2]])
    flipped.export('scalloped_knob_hole5_print.stl')
    knob.export('scalloped_knob_hole5_as_designed.stl')
    rings = to_mesh(build_fit_rings())
    print('rings  watertight=%s  size=%s' % (rings.is_watertight, np.round(rings.extents, 2)))
    rings.export('fit_test_rings.stl')
