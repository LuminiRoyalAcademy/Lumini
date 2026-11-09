"""Layout C: compact two-thumb handheld case (base + top plate). Millimetres.
Run:  python make_case_C.py     -> case_C_base.stl, case_C_top_plate_print.stl
CONFIRMED: breadboard width and depth (95 x 65). Everything marked ASSUMED is a default to check against the real parts."""
import math, numpy as np, trimesh
from shapely.geometry import box as sbox
from manifold3d import Manifold, CrossSection

# ---- outside shape ----
OUT_W, OUT_D, CORNER_R = 132.0, 80.0, 10.0
WALL, FLOOR, PLATE_T   = 2.4, 2.5, 3.0
BODY_H   = 34.0                      # total height of base + plate (without knobs)
EDGE     = 0.0                       # small chamfer on the bottom edge and the top edge
# ---- breadboard ----
BB_W, BB_D = 95.0, 65.0              # CONFIRMED
BB_CLEAR, BB_LIP, BB_LIP_W = 0.8, 1.5, 1.6   # pocket is 0.8 mm bigger than the board; 1.5 mm high holding lip
# ---- screws holding the plate on (ASSUMED M3 self-tapping) ----
POST_XY  = [(-55, -27), (55, -27), (-55, 27), (55, 27)]
POST_R, PILOT_R, PILOT_DEPTH = 3.6, 1.35, 12.0
CLEAR_R, HEAD_R, HEAD_DEPTH   = 1.7, 3.1, 1.8
# ---- USB opening (ASSUMED: ESP32 USB faces the LEFT end, centred) ----
USB_W, USB_H, USB_Z0, USB_Y = 14.0, 9.0, 11.5, 0.0
# ---- top plate features ----
OLED_X, OLED_Y           = 0.0, 16.0
BB_THICK = 10.0                            # CONFIRMED by user (1 cm)
DISPLAYS = {                               # board W x D, window W x D
    '13':  dict(pcb=(35.0, 32.0), win=(30.4, 15.7)),   # pcb: user measured 32 x 35 (35 wide assumed); window: ASSUMED typical 1.3in SH1106 active area
    '096': dict(pcb=(27.0, 27.0), win=(23.2, 12.4)),   # pcb: seller data 27 x 27 (varies 26-30); window: 21.74 x 11.2 active + margin
}
OLED_FRAME_H, OLED_FRAME_WALL, OLED_CLEAR = 6.0, 1.5, 0.25
ENC_X, ENC_Y, ENC_HOLE_R = 44.0, 0.0, 3.6  # ASSUMED M7 bushing (typical for HW-040, not confirmed); hole 7.2 mm
ENC_BOSS_R, ENC_BOSS_H   = 7.5, 1.5
NAV_X, NAV_Y, NAV_HOLE_R = 0.0, -22.0, 6.8 # CONFIRMED approx: 12 mm switch, 13 mm cap -> 13.6 mm hole
NAV_SW, NAV_FRAME_H      = 12.6, 7.0
BUZ_X, BUZ_Y, BUZ_R      = 36.0, 26.0, 6.2 # CONFIRMED approx: 12 mm dia (+0.4 clearance), 8 mm thick
BUZ_RING_H               = 8.0

def ccw(p):
    p = np.array(p); a = 0.5*np.sum(p[:,0]*np.roll(p[:,1],-1)-np.roll(p[:,0],-1)*p[:,1]); return p if a > 0 else p[::-1]
def rrect(w, d, r, n=1): return ccw(np.array(sbox(-w/2,-d/2,w/2,d/2).buffer(-r,n).buffer(r,n).exterior.coords)[:-1])
def prism(p, z0, z1, scale_top=(1,1)): return Manifold.extrude(CrossSection([ccw(p)]), z1-z0, scale_top=scale_top).translate([0,0,z0])
def cyl(r, z0, z1, x=0, y=0, n=12):
    r = r/math.cos(math.pi/12)   # 12-sided; flats sit at the nominal radius so holes are not undersized
    return Manifold.cylinder(z1-z0, r, r, 12).translate([x, y, z0])
def box(w, d, z0, z1, x=0, y=0): return Manifold.cube([w, d, z1-z0], True).translate([x, y, (z0+z1)/2])
def frame(w, d, wall, z0, z1, x, y): return box(w+2*wall, d+2*wall, z0, z1, x, y) - box(w, d, z0-1, z1+1, x, y)
def mesh(m):
    mg = m.to_mesh(); t = trimesh.Trimesh(np.round(np.array(mg.vert_properties)[:,:3],4), np.array(mg.tri_verts), process=False)
    t.merge_vertices(); t.update_faces(t.nondegenerate_faces()); t.remove_unreferenced_vertices(); return t

Z_PLATE0 = BODY_H - PLATE_T
outer = rrect(OUT_W, OUT_D, CORNER_R); inner = rrect(OUT_W-2*WALL, OUT_D-2*WALL, CORNER_R-WALL)

def build_base():
    # bottom edge chamfer: start slightly smaller and grow to full size over EDGE mm
    walls = prism(outer, 0, Z_PLATE0)
    base = walls - prism(inner, FLOOR, Z_PLATE0 + 1)
    # breadboard pocket lip
    pocket = rrect(BB_W+2*BB_CLEAR, BB_D+2*BB_CLEAR, 1.5); lip_out = rrect(BB_W+2*BB_CLEAR+2*BB_LIP_W, BB_D+2*BB_CLEAR+2*BB_LIP_W, 3.0)
    base = base + (prism(lip_out, FLOOR-0.1, FLOOR+BB_LIP) - prism(pocket, FLOOR-1, FLOOR+BB_LIP+1))
    # screw posts
    for (x, y) in POST_XY:
        base = base + cyl(POST_R, FLOOR-0.1, Z_PLATE0, x, y, 40)
        base = base - cyl(PILOT_R, Z_PLATE0-PILOT_DEPTH, Z_PLATE0+1, x, y, 24)
    # USB opening through the left end wall
    base = base - box(WALL+6, USB_W, USB_Z0, USB_Z0+USB_H, -OUT_W/2+WALL/2, USB_Y)
    return base

def build_plate(disp):
    OLED_PCB, OLED_WINDOW = DISPLAYS[disp]['pcb'], DISPLAYS[disp]['win']
    plate = prism(outer, Z_PLATE0, BODY_H)
    # screen window + frame that holds the module
    plate = plate - box(*OLED_WINDOW, Z_PLATE0-1, BODY_H+1, OLED_X, OLED_Y)
    plate = plate + frame(OLED_PCB[0]+OLED_CLEAR, OLED_PCB[1]+OLED_CLEAR, OLED_FRAME_WALL, Z_PLATE0-OLED_FRAME_H, Z_PLATE0+0.1, OLED_X, OLED_Y)
    # encoder holes with a boss ring underneath
    for sgn in (-1, 1):
        x = sgn*ENC_X
        plate = plate + (cyl(ENC_BOSS_R, Z_PLATE0-ENC_BOSS_H, Z_PLATE0+0.1, x, ENC_Y) - cyl(ENC_HOLE_R, Z_PLATE0-ENC_BOSS_H-1, Z_PLATE0+1, x, ENC_Y))
        plate = plate - cyl(ENC_HOLE_R, Z_PLATE0-1, BODY_H+1, x, ENC_Y)
    # NAV button hole + switch holder
    plate = plate + frame(NAV_SW, NAV_SW, 1.5, Z_PLATE0-NAV_FRAME_H, Z_PLATE0+0.1, NAV_X, NAV_Y)
    plate = plate - cyl(NAV_HOLE_R, Z_PLATE0-1, BODY_H+1, NAV_X, NAV_Y)
    # buzzer holder + sound holes
    plate = plate + (cyl(BUZ_R+1.4, Z_PLATE0-BUZ_RING_H, Z_PLATE0+0.1, BUZ_X, BUZ_Y) - cyl(BUZ_R, Z_PLATE0-BUZ_RING_H-1, Z_PLATE0+1, BUZ_X, BUZ_Y))
    for dx, dy in [(0,0)] + [(3.2*math.cos(a*math.pi/3), 3.2*math.sin(a*math.pi/3)) for a in range(6)]:
        plate = plate - cyl(0.85, Z_PLATE0-1, BODY_H+1, BUZ_X+dx, BUZ_Y+dy, 16)
    # screws: clearance hole + counterbore for the head
    for (x, y) in POST_XY:
        plate = plate - cyl(CLEAR_R, Z_PLATE0-1, BODY_H+1, x, y, 24) - cyl(HEAD_R, BODY_H-HEAD_DEPTH, BODY_H+1, x, y, 24)
    return plate

if __name__ == '__main__':
    print('vertical budget: breadboard top at %.1f mm above floor outside, plate underside at %.1f -> %.1f mm free for ESP32/wires' % (FLOOR+BB_THICK, Z_PLATE0, Z_PLATE0-FLOOR-BB_THICK))
    base = mesh(build_base()); base.export('case_C_base.stl'); print('base', base.is_watertight, np.round(base.extents,1))
    for d in DISPLAYS:
        plate = mesh(build_plate(d)); print('plate', d, plate.is_watertight, np.round(plate.extents,1))
        plate.export('case_C_top_plate_oled%s_as_designed.stl' % d)
        f = plate.copy(); f.apply_transform(trimesh.transformations.rotation_matrix(math.pi, [1,0,0])); f.apply_translation([0,0,-f.bounds[0][2]])
        f.export('case_C_top_plate_oled%s_print.stl' % d)
