# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The "Creature bones" insert set (#154): original fantasy creature bones, designed here from simple forms
# (no reference meshes, scans or stock models). Each piece is a signed-distance function in metres, +Y up
# (Insert lays +Y along the surface normal: horns, claws and teeth grow along +Y), +Z forward, mirror-symmetric
# about x = 0 where the anatomy is. tools/creature-bones/build.sh meshes, quad-remeshes and packs them.
import math
from sdf import (Chain, add, clamp, ellipsoid, fbm, lerp, round_cone, sample_curve, smax, smin, smoothstep, sphere, ssub, sub,
                 vnoise)


def mx(p):  # mirror: evaluate the x >= 0 half
    return (abs(p[0]), p[1], p[2])

def scaled(p, c, s):  # p in a space stretched by 1/s about c (s < 1 makes the shape longer on that axis)
    return (c[0] + (p[0] - c[0]) * s[0], c[1] + (p[1] - c[1]) * s[1], c[2] + (p[2] - c[2]) * s[2])

def rot_z(p, c, a):
    x, y = p[0] - c[0], p[1] - c[1]
    ca, sa = math.cos(a), math.sin(a)
    return (c[0] + ca * x + sa * y, c[1] - sa * x + ca * y, p[2])

def rot_x(p, c, a):
    y, z = p[1] - c[1], p[2] - c[2]
    ca, sa = math.cos(a), math.sin(a)
    return (p[0], c[1] + ca * y + sa * z, c[2] - sa * y + ca * z)

def bone_grain(p, d, size, along=1, seed=0):
    """Bone surface: long, shallow ridges along the bone's axis plus soft erosion pits. Applied near the
    surface only; amplitudes are fractions of the piece size."""
    if abs(d) > 0.05 * size: return d
    q = list(p)
    stretch = [6.0, 6.0, 6.0]
    stretch[along] = 1.2  # grain runs along the bone
    f = 1.0 / size
    ridge = abs(vnoise(q[0] * stretch[0] * f, q[1] * stretch[1] * f, q[2] * stretch[2] * f, seed))
    pits = fbm(p, 14.0 * f, 2, seed + 7)
    return d + size * (0.0035 * (ridge - 0.5) + 0.004 * max(0.0, pits - 0.15))


def cylinder_z(p, r, half, round_r):
    """A cylinder along z, radius r, from -half to half, its rims rounded by round_r."""
    dx = math.hypot(p[0], p[1]) - r + round_r
    dz = abs(p[2]) - half + round_r
    return min(max(dx, dz), 0.0) + math.hypot(max(dx, 0.0), max(dz, 0.0)) - round_r


def curved_tube(n, fn, radius, ring=None):
    pts = sample_curve(fn, n)
    rad = []
    for i in range(n):
        t = i / (n - 1)
        r = radius(t)
        if ring: r *= 1.0 + ring(t)
        rad.append(r)
    return Chain(pts, rad)


# ---------------------------------------------------------------- skull and jaw
def rot_y(p, c, a):
    x, z = p[0] - c[0], p[2] - c[2]
    ca, sa = math.cos(a), math.sin(a)
    return (c[0] + ca * x - sa * z, p[1], c[2] + sa * x + ca * z)


def loft(q, stations, k):
    """A smooth body through cross-sections: (z, half width, y bottom, y top) stations, an ellipsoid each, blended."""
    d = 1e9
    for i, (z, w, y0, y1) in enumerate(stations):
        step = abs(stations[min(i + 1, len(stations) - 1)][0] - stations[max(i - 1, 0)][0]) / 2
        e = ellipsoid(q, (0.0, (y0 + y1) / 2, z), (w, (y1 - y0) / 2, step * 1.25))
        d = smin(d, e, k)
    return d


# z (back to snout tip), half width, bottom, top: a long wedge, widest across the cheeks
_skull_body = [(-0.27, 0.075, -0.03, 0.1), (-0.21, 0.11, -0.055, 0.125), (-0.14, 0.125, -0.07, 0.135),
               (-0.07, 0.115, -0.07, 0.13), (0.0, 0.095, -0.07, 0.115), (0.07, 0.075, -0.068, 0.085),
               (0.13, 0.064, -0.064, 0.06), (0.19, 0.055, -0.058, 0.042), (0.245, 0.045, -0.05, 0.03),
               (0.285, 0.032, -0.042, 0.018)]


def _tooth_row():
    """The upper teeth along each side of the jaw line, longest under the eye, rooted in the bone."""
    row = []
    for i in range(9):
        z = -0.1 + i * 0.044
        for (z0, w0, y0, y1), (z1, w1, y2, y3) in zip(_skull_body, _skull_body[1:]):
            if z0 <= z <= z1:
                t = (z - z0) / (z1 - z0)
                w, bottom, top = lerp(w0, w1, t), lerp(y0, y2, t), lerp(y1, y3, t)
        x = w * 0.7
        hh = (top - bottom) / 2
        y = (top + bottom) / 2 - hh * math.sqrt(1 - 0.7 ** 2)  # the body's underside at x
        size = 0.75 + 0.25 * math.sin(math.pi * (i + 1) / 10)
        row.append(((x, y + 0.01, z), (x * 0.97, y - 0.03 * size, z - 0.004), 0.0105 * size, 0.003))
    return row
_teeth = _tooth_row()


def skull(p):
    q = mx(p)
    d = loft(q, _skull_body, 0.04)
    d = smin(d, ellipsoid(q, (0.085, 0.112, -0.05), (0.048, 0.02, 0.085)), 0.05)  # the brow over each eye
    d = smin(d, round_cone(q, (0.06, 0.11, -0.2), (0.1, 0.14, -0.3), 0.04, 0.028), 0.025)  # horn roots
    d = smin(d, round_cone(q, (0.095, -0.03, -0.215), (0.105, -0.05, -0.23), 0.022, 0.018), 0.02)  # the jaw hinge
    d = smin(d, sphere(q, (0.0, 0.0, -0.285), 0.022), 0.015)  # the knob the neck turns on
    d = smin(d, ellipsoid(q, (0.0, 0.135, -0.11), (0.014, 0.022, 0.13)), 0.025)  # a low ridge down the middle
    d = ssub(d, ellipsoid(q, (0.06, 0.048, -0.03), (0.085, 0.05, 0.055)), 0.012)  # eye sockets, right through
    d = ssub(d, ellipsoid(q, (0.1, 0.0, -0.15), (0.06, 0.04, 0.055)), 0.015)  # the openings behind them
    d = ssub(d, ellipsoid(q, (0.075, 0.005, 0.1), (0.03, 0.022, 0.065)), 0.015)  # long hollows before them
    d = ssub(d, ellipsoid(q, (0.022, 0.025, 0.262), (0.014, 0.012, 0.03)), 0.008)  # nostrils
    d = ssub(d, ellipsoid(q, (0.0, -0.085, -0.04), (0.055, 0.05, 0.2)), 0.025)  # the palate, open underneath
    for a, b, ra, rb in _teeth:
        if abs(q[2] - a[2]) < 0.03: d = smin(d, round_cone(q, a, b, ra, rb), 0.006)
    return bone_grain(p, d, 0.6, along=2, seed=1)


# the lower jaw: one side's bar from the chin back to the hinge (x, z, y centre, half depth), a tall oval section
_jaw_ctrl = [(0.0, 0.272, -0.09, 0.02), (0.035, 0.235, -0.091, 0.022), (0.064, 0.14, -0.09, 0.026),
             (0.086, 0.03, -0.086, 0.034), (0.102, -0.07, -0.08, 0.042), (0.11, -0.15, -0.075, 0.036),
             (0.112, -0.205, -0.072, 0.022)]


def _catmull(ctrl, n):
    out = []
    for i in range(n):
        u = i / (n - 1) * (len(ctrl) - 1)
        k = min(int(u), len(ctrl) - 2)
        t = u - k
        p0, p1, p2, p3 = ctrl[max(k - 1, 0)], ctrl[k], ctrl[k + 1], ctrl[min(k + 2, len(ctrl) - 1)]
        out.append(tuple(0.5 * (2 * b + (c - a) * t + (2 * a - 5 * b + 4 * c - d) * t * t + (3 * b - a - 3 * c + d) * t ** 3)
                         for a, b, c, d in zip(p0, p1, p2, p3)))
    return out
_jaw_path = _catmull(_jaw_ctrl, 40)


def _jaw_bar(q, half_thick):
    best, arg = 1e9, None
    for (x0, z0, y0, h0), (x1, z1, y1, h1) in zip(_jaw_path, _jaw_path[1:]):
        dx, dz = x1 - x0, z1 - z0
        t = clamp(((q[0] - x0) * dx + (q[2] - z0) * dz) / (dx * dx + dz * dz), 0.0, 1.0)
        l = math.hypot(q[0] - (x0 + dx * t), q[2] - (z0 + dz * t))
        if l < best: best, arg = l, (lerp(y0, y1, t), lerp(h0, h1, t))
    yc, h = arg
    a, b = best / half_thick, (q[1] - yc) / h
    k0 = math.hypot(a, b)
    k1 = math.hypot(a / half_thick, b / h)
    return k0 * (k0 - 1.0) / k1 if k1 > 1e-12 else -half_thick


def _jaw_teeth():
    row = []
    for i in range(1, 9):
        x, z, y, h = _jaw_path[int(i * 3.6)]
        size = 0.7 + 0.3 * math.sin(math.pi * i / 9)
        row.append(((x, y + h * 0.5, z), (x * 0.97, y + h + 0.026 * size, z + 0.004), 0.0095 * size, 0.0028))
    return row
_lower_teeth = _jaw_teeth()


def jaw(p):
    q = mx(p)
    if q[1] > 0.0: return q[1] + 0.02  # nothing above the teeth
    d = _jaw_bar(q, 0.016)
    d = smin(d, ellipsoid(q, (0.108, -0.048, -0.13), (0.013, 0.036, 0.03)), 0.022)  # the rise for the muscle
    d = smin(d, ellipsoid(q, (0.106, -0.066, -0.205), (0.026, 0.016, 0.019)), 0.012)  # the hinge knob
    for a, b, ra, rb in _lower_teeth:
        if abs(q[2] - a[2]) < 0.03: d = smin(d, round_cone(q, a, b, ra, rb), 0.006)
    return bone_grain(p, d, 0.45, along=2, seed=2)


# ---------------------------------------------------------------- teeth, horns, claws
def _fang_curve(t): return (0.0, 0.1 * t, 0.018 * t * t)
_fang = curved_tube(24, _fang_curve, lambda t: 0.0115 * (1 - t) ** 0.8 + 0.0008)


def fang(p):
    d = _fang(scaled(p, (0, 0, 0), (1.15, 1.0, 1.0)))
    d = smin(d, ellipsoid(p, (0, 0.012, 0.0), (0.0095, 0.022, 0.011)), 0.006)  # the root's swell
    return bone_grain(p, d, 0.1, along=1, seed=3)


def _blade_curve(t): return (0.0, 0.085 * t, -0.03 * t * t)
_blade = curved_tube(24, _blade_curve, lambda t: 0.019 * (1 - t) ** 0.7 + 0.0016)


def blade_tooth(p):
    d = _blade(scaled(p, (0, 0, 0), (2.4, 1.0, 1.0)))  # thin side to side, a cutting edge front and back
    return bone_grain(p, d, 0.09, along=1, seed=4)


def _horn_curve(t):
    a = 1.5 * t
    return (0.06 * t * t, 0.32 * math.sin(a) / 1.5 * 1.1, -0.3 * (1 - math.cos(a)) / 1.5)
_horn = curved_tube(48, _horn_curve, lambda t: 0.048 * (1 - t) ** 0.9 + 0.0015,
                    ring=lambda t: 0.05 * smoothstep(0.0, 0.08, 1 - t) * math.sin(t * 2 * math.pi * 11) ** 2 * (1 - t))


def swept_horn(p):
    d = _horn(scaled(p, (0, 0, 0), (1.0, 1.0, 0.92)))
    return bone_grain(p, d, 0.45, along=1, seed=5)


def _spiral_curve(t):
    a = t * 1.35 * 2 * math.pi
    r = 0.11 * math.exp(-0.18 * a)
    return (0.16 * t, r * math.sin(a), 0.11 - r * math.cos(a))
_spiral = curved_tube(80, _spiral_curve, lambda t: 0.042 * (1 - t) ** 0.85 + 0.0015,
                      ring=lambda t: 0.06 * math.sin(t * 2 * math.pi * 16) ** 2 * (1 - t))


def spiral_horn(p):
    d = _spiral(p)
    return bone_grain(p, d, 0.35, along=0, seed=6)


def _claw_curve(t):
    a = 1.75 * t
    return (0.0, 0.075 * math.sin(a), 0.075 * (1 - math.cos(a)))
_claw = curved_tube(32, _claw_curve, lambda t: 0.022 * (1 - t) ** 0.85 + 0.0008)


def claw(p):
    d = _claw(scaled(p, (0, 0, 0), (1.7, 1.0, 1.0)))
    d = smin(d, ellipsoid(p, (0.0, 0.01, -0.012), (0.012, 0.012, 0.012)), 0.008)  # the tendon's knob
    return bone_grain(p, d, 0.12, along=1, seed=7)


# ---------------------------------------------------------------- spine, ribs, tail
def _vertebra(q, s, spine_h, transverse=True, chevron=False):
    """One vertebra of size s about the origin (spine along z), x >= 0 half."""
    r = 0.042 * s
    if abs(q[2]) < 0.045 * s: r -= 0.007 * s * (1 - (q[2] / (0.045 * s)) ** 2)  # the waist
    body = cylinder_z(q, r, 0.045 * s, 0.01 * s)
    for z in (1, -1):  # shallow cups at both ends
        body = ssub(body, ellipsoid(q, (0, 0, z * 0.05 * s), (0.03 * s, 0.03 * s, 0.012 * s)), 0.006 * s)
    yc = 0.064 * s  # the canal's centre
    ring = abs(math.hypot(q[0], q[1] - yc) - 0.036 * s) - 0.013 * s
    arch = smax(ring, abs(q[2]) - 0.032 * s, 0.008 * s)
    arch = smax(arch, -(q[1] - 0.02 * s), 0.01 * s)
    d = smin(body, arch, 0.012 * s)
    spine = round_cone(scaled(q, (0, 0, 0), (2.4, 1.0, 0.75)), (0, 0.085 * s, 0), (0, spine_h * s, -0.03 * s),
                       0.024 * s, 0.011 * s)
    d = smin(d, spine, 0.018 * s)
    for z in (1, -1):  # the joints with the next vertebrae, leaning out of the arch
        d = smin(d, ellipsoid(q, (0.03 * s, 0.096 * s, z * 0.045 * s), (0.012 * s, 0.009 * s, 0.022 * s)), 0.01 * s)
    if transverse:
        tp = round_cone(scaled(q, (0, 0, 0), (1.0, 1.0, 1.6)), (0.032 * s, 0.05 * s, 0), (0.12 * s, 0.07 * s, -0.008 * s),
                        0.017 * s, 0.009 * s)
        d = smin(d, tp, 0.02 * s)
    if chevron:
        ch = round_cone(scaled(q, (0, 0, 0), (2.0, 1.0, 1.0)), (0.01 * s, -0.035 * s, 0.01 * s), (0, -0.12 * s, -0.035 * s),
                        0.012 * s, 0.006 * s)
        d = smin(d, ch, 0.014 * s)
    return d


def vertebra(p):
    d = _vertebra(mx(p), 1.0, 0.21)
    return bone_grain(p, d, 0.25, along=1, seed=8)


_tail_n = 5
def _tail_frame(i):
    t = i / (_tail_n - 1)
    # each vertebra 8% smaller than the one before, spaced by their sizes, the tail arching up a little
    return 0.62 * 0.92 ** i, (0.0, 0.025 * math.sin(t * math.pi), -0.065 * sum(0.92 ** k for k in range(i)))


def tail(p):
    q = mx(p)
    d = 1e9
    for i in range(_tail_n):
        s, c = _tail_frame(i)
        local = sub(q, c)
        d = min(d, _vertebra(local, s, 0.16, transverse=i < 2, chevron=True))
        if i + 1 < _tail_n:  # the discs between them keep the tail one piece
            s2, c2 = _tail_frame(i + 1)
            disc = round_cone(q, add(c, (0, 0, -0.04 * s)), add(c2, (0, 0, 0.04 * s2)), 0.036 * s, 0.036 * s2)
            d = smin(d, disc, 0.012)
    return bone_grain(p, d, 0.4, along=2, seed=9)


def _rib_curve(t):
    a = 2.5 * t
    return (0.26 * math.sin(a) * (1 - 0.12 * t), 0.2 * math.cos(a) - 0.2 + 0.0, -0.05 * t * t)
_rib = curved_tube(40, _rib_curve, lambda t: 0.013 * (1.0 - 0.35 * t) + 0.004 * (1 - smoothstep(0, 0.12, t)))


def rib(p):
    d = _rib(scaled(p, (0, 0, 0), (1.0, 1.0, 0.42)))  # a flat band, wide along the body
    d = smin(d, round_cone(p, (0.0, 0.0, 0.0), (-0.03, 0.022, 0.0), 0.016, 0.013), 0.01)  # the head
    d = smin(d, sphere(p, (0.03, 0.012, 0.0), 0.017), 0.01)  # the tubercle
    return bone_grain(p, d, 0.45, along=1, seed=10)


# ---------------------------------------------------------------- limbs and wings
_shaft = Chain([(0.0, 0.07, 0.0), (0.004, 0.2, 0.006), (0.0, 0.34, 0.004), (-0.01, 0.43, 0.0)],
               [0.03, 0.024, 0.025, 0.032])


def limb_bone(p):
    d = _shaft(p)
    d = smin(d, round_cone(p, (-0.012, 0.44, 0.0), (-0.05, 0.475, 0.0), 0.026, 0.02), 0.02)  # the neck
    d = smin(d, sphere(p, (-0.058, 0.478, 0.0), 0.034), 0.012)  # the ball
    d = smin(d, sphere(p, (0.03, 0.455, 0.0), 0.026), 0.02)  # the outer knob
    for x in (-0.028, 0.028):  # the knee's two rollers
        d = smin(d, ellipsoid(p, (x, 0.04, 0.008), (0.024, 0.034, 0.036)), 0.02)
    d = ssub(d, ellipsoid(p, (0.0, 0.005, 0.012), (0.009, 0.04, 0.05)), 0.008)  # the groove between them
    return bone_grain(p, d, 0.52, along=1, seed=12)


_wing = Chain([(0.0, 0.04, 0.0), (0.008, 0.2, -0.004), (0.01, 0.4, -0.006), (0.0, 0.56, 0.0)],
              [0.02, 0.014, 0.013, 0.018])


def wing_bone(p):
    d = _wing(p)
    d = smin(d, ellipsoid(p, (0.0, 0.035, 0.0), (0.03, 0.03, 0.024)), 0.02)  # the shoulder end
    d = smin(d, sphere(p, (0.012, 0.03, 0.012), 0.016), 0.012)
    for x in (-0.012, 0.012):  # the pulley at the wrist end
        d = smin(d, ellipsoid(p, (x, 0.575, 0.0), (0.013, 0.02, 0.019)), 0.01)
    return bone_grain(p, d, 0.6, along=1, seed=13)


def _finger_curve(t): return (0.0, 0.56 * t, 0.05 * math.sin(t * math.pi) - 0.015 * t)
_finger = curved_tube(40, _finger_curve, lambda t: 0.021 * (1 - t) ** 0.7 + 0.0045)


def wing_finger(p):
    d = _finger(scaled(p, (0, 0, 0), (1.0, 1.0, 0.85)))
    d = smin(d, ellipsoid(p, (0.0, 0.008, 0.0), (0.032, 0.028, 0.029)), 0.018)  # the knuckle
    for t in (0.33, 0.64):  # two joints along it
        r = 0.021 * (1 - t) ** 0.7 + 0.0045
        d = smin(d, ellipsoid(p, _finger_curve(t), (r * 1.35, r * 1.1, r * 1.25)), 0.012)
    return bone_grain(p, d, 0.56, along=1, seed=14)


# name, sdf, bounds lo, bounds hi, cell size (m), mirror about x = 0, quad target
PIECES = [
    ("Skull", skull, (-0.2, -0.16, -0.38), (0.2, 0.22, 0.34), 0.003, True, 4000),
    ("Jaw", jaw, (-0.17, -0.15, -0.26), (0.17, 0.05, 0.34), 0.003, True, 1800),
    ("Fang", fang, (-0.02, -0.02, -0.02), (0.02, 0.11, 0.04), 0.0007, False, 500),
    ("Blade tooth", blade_tooth, (-0.02, -0.025, -0.05), (0.02, 0.1, 0.03), 0.0008, False, 500),
    ("Swept horn", swept_horn, (-0.07, -0.06, -0.4), (0.13, 0.4, 0.07), 0.003, False, 1200),
    ("Spiral horn", spiral_horn, (-0.06, -0.15, -0.07), (0.22, 0.17, 0.27), 0.0026, False, 1600),
    ("Claw", claw, (-0.03, -0.03, -0.04), (0.03, 0.11, 0.11), 0.0009, False, 600),
    ("Vertebra", vertebra, (-0.15, -0.07, -0.09), (0.15, 0.24, 0.08), 0.0022, True, 1500),
    ("Tail vertebrae", tail, (-0.1, -0.12, -0.39), (0.1, 0.15, 0.06), 0.0018, True, 2800),
    ("Rib", rib, (-0.08, -0.42, -0.2), (0.3, 0.07, 0.06), 0.0024, False, 900),
    ("Limb bone", limb_bone, (-0.1, -0.02, -0.06), (0.07, 0.53, 0.06), 0.0022, False, 1400),
    ("Wing bone", wing_bone, (-0.04, -0.01, -0.04), (0.04, 0.61, 0.04), 0.0018, False, 1000),
    ("Wing finger", wing_finger, (-0.05, -0.04, -0.06), (0.05, 0.6, 0.09), 0.0016, False, 900),
]
