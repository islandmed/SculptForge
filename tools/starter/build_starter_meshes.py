#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the starter meshes (Library > Tool > Starter meshes, File > New from starter mesh) as OBJ base cages.
# Everything is either made here from scratch (the eye, the stylised and creature heads' extra Parts, the
# bevelled primitives) or cut from the MakeHuman hm08 base mesh, which MakeHuman released under CC0 1.0
# (docs/starter-meshes.md has the provenance). Deterministic: the same input gives the same bytes.
#
# Usage: build_starter_meshes.py <makehuman base.obj> <out dir>
# tools/starter/build.sh runs this, then sculptforge-cli (OBJ -> .sfproj) and compresses the projects.
#
# Units are centimetres, Y up, looking down +Z, the character's left is +X; every mesh is centred on X and Z
# and stands on the ground (lowest point at Y = 0). All faces are quads.

import math
import os
import sys
from collections import Counter, defaultdict

# ---------------------------------------------------------------------------
# Small vector helpers
# ---------------------------------------------------------------------------


def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def length(a): return math.sqrt(dot(a, a))
def norm(a):
    l = length(a)
    return mul(a, 1.0 / l) if l > 1e-12 else (0.0, 0.0, 0.0)
def lerp(a, b, t): return a + (b - a) * t
def smoothstep(e0, e1, x):
    t = min(1.0, max(0.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


class Mesh:
    """Quads only: verts [(x,y,z)], faces [(a,b,c,d)], CCW seen from outside."""

    def __init__(self, verts=None, faces=None):
        self.verts = list(verts or [])
        self.faces = list(faces or [])

    def transformed(self, fn):
        return Mesh([fn(v) for v in self.verts], self.faces)


def bounds(meshes):
    lo = [1e30] * 3
    hi = [-1e30] * 3
    for m in meshes:
        for v in m.verts:
            for k in range(3):
                lo[k] = min(lo[k], v[k])
                hi[k] = max(hi[k], v[k])
    return lo, hi


def place_on_ground(parts):
    """Centres the Parts together on X and Z and puts the lowest point at Y = 0."""
    lo, hi = bounds(parts)
    off = (-(lo[0] + hi[0]) / 2, -lo[1], -(lo[2] + hi[2]) / 2)
    return [m.transformed(lambda v: add(v, off)) for m in parts]


# ---------------------------------------------------------------------------
# Cube topology: six quad grids sharing their edges (no poles, every vertex valence 4 except the 8 corners)
# ---------------------------------------------------------------------------


def cube_grid(gx, gy, gz):
    """gx, gy, gz: sorted coordinates along each axis (first = -extent, last = +extent).
    Returns the surface of the box as a quad mesh whose vertices are (x, y, z) taken from the grids, plus each
    vertex's index triple (to tell faces / edges apart)."""
    grids = (gx, gy, gz)
    index = {}
    verts = []
    keys = []

    def vid(ijk):
        if ijk not in index:
            index[ijk] = len(verts)
            verts.append(tuple(grids[k][ijk[k]] for k in range(3)))
            keys.append(ijk)
        return index[ijk]

    faces = []
    for axis in range(3):
        b, c = (axis + 1) % 3, (axis + 2) % 3
        for side in (0, len(grids[axis]) - 1):
            sign = 1.0 if side else -1.0
            for i in range(len(grids[b]) - 1):
                for j in range(len(grids[c]) - 1):
                    def at(di, dj):
                        ijk = [0, 0, 0]
                        ijk[axis] = side
                        ijk[b] = i + di
                        ijk[c] = j + dj
                        return vid(tuple(ijk))
                    q = [at(0, 0), at(1, 0), at(1, 1), at(0, 1)]
                    # b x c points along +axis: CCW from outside on the + side, reversed on the - side.
                    if sign < 0:
                        q.reverse()
                    faces.append(tuple(q))
    return verts, faces, keys


def uniform(n, extent=1.0):
    return [-extent + 2.0 * extent * i / n for i in range(n + 1)]


def rounded_box(hx, hy, hz, r, cell):
    """Box with half extents (hx, hy, hz) and every edge rounded with radius r; quads about `cell` across."""
    def axis_grid(h):
        flat = 2 * (h - r)
        m = max(1, round(flat / cell))
        bev = max(2, round((math.pi / 4) * r / cell))  # each face covers half of the quarter-round
        g = []
        for k in range(bev, 0, -1):
            g.append(-(h - r) - r * math.tan(k / bev * math.pi / 4))
        for k in range(m + 1):
            g.append(-(h - r) + flat * k / m)
        for k in range(1, bev + 1):
            g.append((h - r) + r * math.tan(k / bev * math.pi / 4))
        return g

    gx, gy, gz = axis_grid(hx), axis_grid(hy), axis_grid(hz)
    verts, faces, _ = cube_grid(gx, gy, gz)
    inner = (hx - r, hy - r, hz - r)
    h = (hx, hy, hz)
    out = []
    for q in verts:
        # q is on the box surface; snap it onto the rounded box (flat faces stay flat).
        qq = tuple(max(-h[k], min(h[k], q[k])) for k in range(3))
        c = tuple(max(-inner[k], min(inner[k], qq[k])) for k in range(3))
        out.append(add(c, mul(norm(sub(qq, c)), r)))
    return Mesh(out, faces)


def profile_sampler(points):
    """points: a polyline (radius, height) from the bottom pole (radius 0) to the top pole (radius 0).
    Returns f(t) -> (radius, height), t in [0, 1] by arc length."""
    cum = [0.0]
    for a, b in zip(points, points[1:]):
        cum.append(cum[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    total = cum[-1]

    def f(t):
        s = t * total
        lo, hi = 0, len(cum) - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if cum[mid] <= s:
                lo = mid
            else:
                hi = mid
        seg = cum[hi] - cum[lo]
        u = 0.0 if seg <= 0 else (s - cum[lo]) / seg
        a, b = points[lo], points[hi]
        return (lerp(a[0], b[0], u), lerp(a[1], b[1], u))

    return f, total


def arc(cx, cy, r, a0, a1, n):
    return [(cx + r * math.cos(lerp(a0, a1, k / n)), cy + r * math.sin(lerp(a0, a1, k / n))) for k in range(n + 1)]


def revolve(points, n):
    """Surface of revolution about +Y on cube topology: the cube's top and bottom faces become the caps (rings
    of quads round the poles, no triangle fans), its sides the band. n = quads along a cube edge (even);
    4n quads go round, 2n from pole to pole."""
    f, _ = profile_sampler(points)
    g = uniform(n)
    verts, faces, keys = cube_grid(g, g, g)
    out = []
    for (ix, iy, iz) in keys:
        ux, uy, uz = g[ix], g[iy], g[iz]
        if iy == 0 or iy == n:
            ring = max(abs(ux), abs(uz))  # 0 at the pole, 1 at the cube edge
            m = ring * (n / 2) if iy == 0 else 2 * n - ring * (n / 2)
        else:
            m = n / 2 + (uy + 1) / 2 * n
        r, h = f(m / (2 * n))
        d = norm((math.tan(ux * math.pi / 4), 0.0, math.tan(uz * math.pi / 4)))
        out.append((r * d[0], h, r * d[2]))
    return Mesh(out, faces)


def torus(points, n_major, major):
    """Ring about +Y: the closed profile `points` (radius offset, height), CCW, swept round a circle of radius
    `major`, sampled by arc length with as many quads as keep them about square."""
    closed = points + [points[0]]
    f, total = profile_sampler(closed)
    n_minor = max(8, round(total / (2 * math.pi * major / n_major)))
    n_minor += n_minor % 2
    verts = []
    for i in range(n_major):
        a = 2 * math.pi * i / n_major
        for j in range(n_minor):
            dr, h = f(j / n_minor)
            verts.append(((major + dr) * math.cos(a), h, (major + dr) * math.sin(a)))
    faces = []
    for i in range(n_major):
        for j in range(n_minor):
            a = i * n_minor + j
            b = ((i + 1) % n_major) * n_minor + j
            c = ((i + 1) % n_major) * n_minor + (j + 1) % n_minor
            d = i * n_minor + (j + 1) % n_minor
            faces.append((a, d, c, b))
    return orient_outward(Mesh(verts, faces))


def ellipsoid_box(hx, hy, hz, cell, n=2.0):
    """An ellipsoid with semi-axes (hx, hy, hz) made by projecting a box grid (cells about `cell` across)
    radially onto it: quads stay close to even even when the axes differ a lot. n > 2 makes it boxier
    (a superellipsoid, |x|^n + |y|^n + |z|^n = 1)."""
    gx = uniform(max(2, 2 * round(hx / cell)), 1.0)
    gy = uniform(max(2, 2 * round(hy / cell)), 1.0)
    gz = uniform(max(2, 2 * round(hz / cell)), 1.0)
    verts, faces, _ = cube_grid(gx, gy, gz)
    out = []
    for (x, y, z) in verts:
        # Equal-angle warp per axis keeps the projected quads square-ish.
        p = (math.tan(x * math.pi / 4), math.tan(y * math.pi / 4), math.tan(z * math.pi / 4))
        d = mul(p, 1.0 / (sum(abs(c) ** n for c in p) ** (1.0 / n)))
        out.append((d[0] * hx, d[1] * hy, d[2] * hz))
    return Mesh(out, faces)


def orient_outward(m):
    """Flips every face if the mesh's signed volume is negative (closed meshes)."""
    vol = 0.0
    for f in m.faces:
        a = m.verts[f[0]]
        for k in range(1, len(f) - 1):
            vol += dot(a, cross(m.verts[f[k]], m.verts[f[k + 1]]))
    if vol < 0:
        m.faces = [tuple(reversed(f)) for f in m.faces]
    return m


# ---------------------------------------------------------------------------
# MakeHuman hm08 (CC0): the "body" group, in decimetres -> centimetres
# ---------------------------------------------------------------------------


def load_makehuman(path):
    verts, groups, g = [], {}, None
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("v "):
                verts.append(tuple(float(t) * 10.0 for t in line.split()[1:4]))
            elif line.startswith("g "):
                g = line.split()[1]
                groups.setdefault(g, [])
            elif line.startswith("f "):
                groups[g].append(tuple(int(t.split("/")[0]) - 1 for t in line.split()[1:]))
    body = groups["body"]
    if any(len(f) != 4 for f in body):
        sys.exit("expected an all-quad body group")
    joints = {}
    for name, fs in groups.items():
        if name.startswith("joint-"):
            ids = {i for f in fs for i in f}
            joints[name[6:]] = mul(
                tuple(sum(verts[i][k] for i in ids) for k in range(3)), 1.0 / len(ids))
    ground = joints["ground"][1]
    verts = [(v[0], v[1] - ground, v[2]) for v in verts]
    joints = {k: (v[0], v[1] - ground, v[2]) for k, v in joints.items()}
    used = sorted({i for f in body for i in f})
    remap = {old: new for new, old in enumerate(used)}
    return Mesh([verts[i] for i in used], [tuple(remap[i] for i in f) for f in body]), joints


def centroid(m, f):
    return mul(tuple(sum(m.verts[i][k] for i in f) for k in range(3)), 1.0 / len(f))


def keep_faces(m, pred):
    """The faces whose centroid passes `pred`, then the largest connected piece, then faces hanging on by
    fewer than two edges peeled off (the cut stays a clean edge loop). Reindexed."""
    faces = [f for f in m.faces if pred(centroid(m, f))]
    while True:
        edges = Counter()
        for f in faces:
            for a, b in zip(f, f[1:] + f[:1]):
                edges[(min(a, b), max(a, b))] += 1
        keep = []
        for f in faces:
            shared = sum(1 for a, b in zip(f, f[1:] + f[:1]) if edges[(min(a, b), max(a, b))] == 2)
            if shared >= 2:
                keep.append(f)
        if len(keep) == len(faces):
            break
        faces = keep
    # Largest piece by shared edges.
    by_edge = defaultdict(list)
    for fi, f in enumerate(faces):
        for a, b in zip(f, f[1:] + f[:1]):
            by_edge[(min(a, b), max(a, b))].append(fi)
    seen = [False] * len(faces)
    best = []
    for s in range(len(faces)):
        if seen[s]:
            continue
        stack, piece = [s], []
        seen[s] = True
        while stack:
            fi = stack.pop()
            piece.append(fi)
            f = faces[fi]
            for a, b in zip(f, f[1:] + f[:1]):
                for o in by_edge[(min(a, b), max(a, b))]:
                    if not seen[o]:
                        seen[o] = True
                        stack.append(o)
        if len(piece) > len(best):
            best = piece
    faces = [faces[i] for i in sorted(best)]
    used = sorted({i for f in faces for i in f})
    remap = {old: new for new, old in enumerate(used)}
    return Mesh([m.verts[i] for i in used], [tuple(remap[i] for i in f) for f in faces])


def plane_side(point, normal):
    n = norm(normal)
    return lambda p: dot(sub(p, point), n) > 0


def mirror_x(p): return (-p[0], p[1], p[2])


# ---------------------------------------------------------------------------
# The set
# ---------------------------------------------------------------------------


def seg_dist(p, a, b):
    ab = sub(b, a)
    t = max(0.0, min(1.0, dot(sub(p, a), ab) / dot(ab, ab)))
    return length(sub(p, add(a, mul(ab, t))))


def human_parts(body, j):
    out = {}
    neck_y = j["neck"][1] + 2.0
    out["human-head"] = [keep_faces(body, lambda p: p[1] > neck_y)]

    # The left arm (mirrored with |x| for the right): past a plane across the upper arm and near the bones.
    sh, el, wr = j["l-shoulder"], j["l-elbow"], j["l-hand"]
    tip = j["l-finger-3-4"]
    arm_dir = sub(el, sh)

    def arm(p, along, radius):
        q = (abs(p[0]), p[1], p[2])
        if dot(sub(q, add(sh, mul(arm_dir, along))), norm(arm_dir)) <= 0:
            return False
        return min(seg_dist(q, sh, el), seg_dist(q, el, wr), seg_dist(q, wr, tip)) < radius

    # Bust: head, neck, shoulders and upper chest; the arms cut a third of the way down the upper arm.
    chest_y = j["spine-1"][1] - 4.0
    out["bust"] = [keep_faces(body, lambda p: p[1] > chest_y and not arm(p, 0.33, 9.0))]

    # Torso: neck to upper thighs, arms off at the shoulder.
    top_y = j["neck"][1] - 1.0
    hip_y = j["l-upper-leg"][1] - 9.0
    out["torso"] = [keep_faces(body, lambda p: hip_y < p[1] < top_y and not arm(p, 0.1, 8.0))]

    out["full-body"] = [body]

    # Hand (left): past the wrist, then stood up: wrist down, fingers up, palm to the front.
    wrist, fore = j["l-hand"], sub(j["l-hand"], j["l-elbow"])
    cut = add(wrist, mul(norm(fore), -1.0))
    hand = keep_faces(body, plane_side(cut, fore))
    up = norm(sub(j["l-finger-3-1"], wrist))
    thumb = sub(j["l-finger-1-3"], wrist)
    side = norm(sub(thumb, mul(up, dot(thumb, up))))  # thumb direction, across the hand
    front = cross(side, up)
    # Rows of the rotation: thumb -> -X (a raised left hand, palm forward), fingers -> +Y.
    xa, ya, za = mul(side, -1.0), up, mul(front, -1.0)
    if dot(cross(xa, ya), za) < 0:
        za = mul(za, -1.0)
    out["hand"] = [hand.transformed(lambda v: (dot(sub(v, wrist), xa), dot(sub(v, wrist), ya), dot(sub(v, wrist), za)))]

    ankle = j["l-ankle"]
    out["foot"] = [keep_faces(body, lambda p: p[0] > 2.0 and p[1] < ankle[1] + 9.0)]

    # Ear (left): centred on the vertices that stand out furthest from the side of the head.
    side = [v for v in body.verts if abs(v[1] - (j["l-eye"][1] - 2.0)) < 6.0 and v[2] < j["l-eye"][2] - 4.0
            and v[0] > 0.0]
    xmax = max(v[0] for v in side)
    tips = [v for v in side if v[0] > xmax - 1.2]
    ear_c = mul(tuple(sum(v[k] for v in tips) for k in range(3)), 1.0 / len(tips))
    ear_c = (ear_c[0] - 1.0, ear_c[1], ear_c[2])

    def ear_pred(p):
        d = sub(p, ear_c)
        return p[0] > ear_c[0] - 1.2 and (d[1] / 4.0) ** 2 + (d[2] / 2.9) ** 2 < 1.0

    ear = keep_faces(body, ear_pred)
    # Ear lying open to the viewer: rotate the outward (+X) side to face +Z.
    out["ear"] = [ear.transformed(lambda v: (-(v[2] - ear_c[2]), v[1], v[0] - ear_c[0]))]
    return out


def stylised_head(head, j):
    """The human head pushed toward a cartoon: bigger cranium and eyes, smaller nose, mouth and jaw."""
    eyes = [j["l-eye"], mirror_x(j["l-eye"])]
    brow_y = j["l-eye"][1] + 1.0
    jaw_y = j["jaw"][1]
    nose = (0.0, j["l-eye"][1] - 3.0, j["l-eye"][2] + 2.0)
    centre = (0.0, j["head"][1] + 2.0, j["head"][2])

    def warp(v):
        p = v
        # Cranium: everything above the brows grows by up to 22 % about the head's centre.
        s = 1.0 + 0.22 * smoothstep(brow_y - 3.0, brow_y + 6.0, p[1])
        p = add(centre, mul(sub(p, centre), s))
        # Eyes: a magnifying warp round each eye.
        for e in eyes:
            d = sub(v, e)
            r = length(d)
            w = 1.0 - smoothstep(0.0, 4.2, r)
            p = add(p, mul(d, 0.55 * w))
        # Nose: shrink toward the face.
        d = sub(v, nose)
        w = 1.0 - smoothstep(0.0, 3.5, length(d))
        p = add(p, mul((d[0], d[1], d[2] - 1.0), -0.35 * w))
        # Jaw and chin: narrower and shorter.
        t = 1.0 - smoothstep(jaw_y - 6.0, jaw_y + 3.0, v[1])
        p = (p[0] * (1.0 - 0.22 * t), p[1] + t * 0.18 * (jaw_y + 3.0 - v[1]), p[2] - 0.12 * t * (p[2] - centre[2]))
        return p

    return head.transformed(warp)


def eye():
    """Eyeball, 24 mm across, with the cornea bulging 2.6 mm at the front; rings of quads round the pupil."""
    R = 1.2
    pts = []
    n = 96
    for k in range(n + 1):
        a = -math.pi / 2 + math.pi * k / n  # back pole -> front pole, as (radius, height along the view axis)
        r, h = R * math.cos(a), R * math.sin(a)
        ang = math.pi / 2 - a  # angle from the front pole
        if ang < 0.62:
            h += 0.26 * (1.0 - (ang / 0.62) ** 2) ** 2
        pts.append((max(0.0, r), h))
    m = revolve(pts, 16)
    # The revolve axis (Y) becomes the view axis (+Z).
    return m.transformed(lambda v: (v[0], -v[2], v[1]))


def creature_head():
    """Dragon / creature head blockout: a skull with a long, blunt snout, brow ridges over the eye sockets, cheek
    bones and nostrils, a lower jaw tucked under it and two horns sweeping back (four Parts). About 37 cm long."""
    skull = ellipsoid_box(1.0, 0.85, 2.0, 0.17, 2.6)

    def bump(v, c, rad, amt):
        out = (0.0, 0.0, 0.0)
        for sx in ((1, -1) if c[0] else (1,)):
            w = 1.0 - smoothstep(0.0, rad, length(sub(v, (c[0] * sx, c[1], c[2]))))
            out = add(out, mul(norm(v), amt * w * w * (3 - 2 * w)))
        return out

    def shape(v):
        x, y, z = v
        t = smoothstep(-0.3, 2.0, z)  # 0 at the back of the skull, 1 at the snout tip
        p = (x * lerp(1.0, 0.55, t), y * lerp(1.0, 0.5, t) - 0.25 * t, z)
        for (c, rad, amt) in (((0.5, 0.55, 0.55), 0.5, 0.30),     # brow ridges
                              ((0.62, 0.22, 0.8), 0.32, -0.14),   # eye sockets under them
                              ((0.85, -0.25, 0.1), 0.5, 0.12),    # cheek bones
                              ((0.25, 0.3, 1.85), 0.25, 0.08),    # nostrils
                              ((0.0, 0.8, -0.4), 0.6, 0.10)):     # crest over the cranium
            p = add(p, bump(v, c, rad, amt))
        return mul(p, 9.0)

    skull = skull.transformed(shape)
    jaw = ellipsoid_box(0.7, 0.22, 1.7, 0.14, 2.4)

    def jaw_shape(v):
        x, y, z = v
        t = smoothstep(-0.7, 1.7, z)
        return mul((x * lerp(1.0, 0.5, t), y * lerp(1.0, 0.8, t) + lerp(-0.72, -0.62, t), z + 0.15), 9.0)

    jaw = jaw.transformed(jaw_shape)
    # Horn: a tapering tube along +Y, bent back as it grows, then pitched back, turned out and set on the skull.
    L, R = 13.0, 1.5
    pts = [(0.0, 0.0), (R, 0.0)] + [(R * (1.0 - (k / 40) ** 1.3) + 0.03, L * k / 40) for k in range(1, 41)] + \
        [(0.0, L + 0.05)]
    horn0 = revolve(pts, 8)
    bend_angle, pitch, roll = 1.0, 0.55, 0.35

    def place(v, sx):
        x, y, z = v
        a = bend_angle * y / L
        rr = L / bend_angle
        nrm = (0.0, -math.sin(a), -math.cos(a))  # across the bent axis, toward the back
        p = add((x, rr * math.sin(a), -(rr - rr * math.cos(a))), mul(nrm, -z))
        p = (p[0], p[1] * math.cos(pitch) + p[2] * math.sin(pitch), -p[1] * math.sin(pitch) + p[2] * math.cos(pitch))
        p = (p[0] * math.cos(roll) + p[1] * math.sin(roll), -p[0] * math.sin(roll) + p[1] * math.cos(roll), p[2])
        return (sx * (p[0] + 4.3), p[1] + 4.6, p[2] - 5.5)

    horns = []
    for sx in (1, -1):
        h = horn0.transformed(lambda v, sx=sx: place(v, sx))
        if sx < 0:
            h.faces = [tuple(reversed(f)) for f in h.faces]
        horns.append(h)
    return [skull, jaw] + horns


def primitives():
    out = {}
    out["rounded-box"] = [rounded_box(10.0, 10.0, 10.0, 1.5, 1.0)]
    R, H, b = 10.0, 20.0, 1.5
    pts = [(0.0, 0.0), (R - b, 0.0)] + arc(R - b, b, b, -math.pi / 2, 0.0, 12) + [(R, H - b)] + \
        arc(R - b, H - b, b, 0.0, math.pi / 2, 12) + [(0.0, H)]
    out["cylinder"] = [revolve(pts, 16)]
    R, L = 7.5, 15.0
    pts = arc(0.0, R, R, -math.pi / 2, 0.0, 24) + arc(0.0, R + L, R, 0.0, math.pi / 2, 24)
    out["capsule"] = [revolve(pts, 16)]
    w, h, b = 3.0, 2.4, 0.6  # cross-section: 3 cm wide, 2.4 cm tall, 6 mm bevels
    sec = []
    for (cx, cy, a0) in ((w / 2 - b, -h / 2 + b, -math.pi / 2), (w / 2 - b, h / 2 - b, 0.0),
                         (-w / 2 + b, h / 2 - b, math.pi / 2), (-w / 2 + b, -h / 2 + b, math.pi)):
        sec += arc(cx, cy, b, a0, a0 + math.pi / 2, 8)
    out["ring"] = [torus(sec, 64, 9.0)]
    return out


# (id, name, Part names, symmetric, closed, Divide levels at load)
SET = [
    ("human-head", "Human Head", ["Head"], True, False, 2),
    ("bust", "Bust", ["Bust"], True, False, 2),
    ("full-body", "Full Body (A-pose)", ["Body"], True, True, 1),
    ("hand", "Hand", ["Hand"], False, False, 2),
    ("foot", "Foot", ["Foot"], False, False, 2),
    ("ear", "Ear", ["Ear"], False, False, 2),
    ("eye", "Eye", ["Eye"], True, True, 2),
    ("stylised-head", "Stylised Head", ["Head"], True, False, 2),
    ("creature-head", "Creature Head", ["Skull", "Jaw", "Horn L", "Horn R"], True, True, 2),
    ("torso", "Torso", ["Torso"], True, False, 2),
    ("rounded-box", "Rounded Box", ["Rounded Box"], True, True, 2),
    ("cylinder", "Cylinder", ["Cylinder"], True, True, 2),
    ("capsule", "Capsule", ["Capsule"], True, True, 2),
    ("ring", "Ring", ["Ring"], True, True, 2),
]


def check(mid, parts, closed):
    nv = sum(len(m.verts) for m in parts)
    nf = sum(len(m.faces) for m in parts)
    lens = []
    boundary = 0
    for m in parts:
        e = Counter()
        for f in m.faces:
            for a, b in zip(f, f[1:] + f[:1]):
                e[(min(a, b), max(a, b))] += 1
                lens.append(length(sub(m.verts[a], m.verts[b])))
        if any(c > 2 for c in e.values()):
            sys.exit(f"{mid}: non-manifold edge")
        boundary += sum(1 for c in e.values() if c == 1)
    if closed and boundary:
        sys.exit(f"{mid}: expected closed, {boundary} boundary edges")
    mean = sum(lens) / len(lens)
    cv = math.sqrt(sum((l - mean) ** 2 for l in lens) / len(lens)) / mean
    lo, hi = bounds(parts)
    print(f"{mid:15s} {nv:6d} verts {nf:6d} quads  edge cv {cv:.2f}  boundary {boundary:4d}  "
          f"size {hi[0]-lo[0]:.1f} x {hi[1]-lo[1]:.1f} x {hi[2]-lo[2]:.1f} cm")


def write_obj(path, names, parts):
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("# SculptForge starter mesh (tools/starter/build_starter_meshes.py); docs/starter-meshes.md\n")
        base = 1
        for name, m in zip(names, parts):
            fh.write(f"o {name}\n")
            for v in m.verts:
                fh.write("v %.4f %.4f %.4f\n" % tuple(0.0 if abs(c) < 5e-5 else c for c in v))
            for f in m.faces:
                fh.write("f " + " ".join(str(i + base) for i in f) + "\n")
            base += len(m.verts)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__ if __doc__ else "usage: build_starter_meshes.py <makehuman base.obj> <out dir>")
    body, joints = load_makehuman(sys.argv[1])
    out_dir = sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    meshes = human_parts(body, joints)
    meshes["stylised-head"] = [stylised_head(meshes["human-head"][0], joints)]
    meshes["eye"] = [eye()]
    meshes["creature-head"] = creature_head()
    meshes.update(primitives())
    for (mid, name, part_names, symmetric, closed, levels) in SET:
        parts = place_on_ground(meshes[mid])
        check(mid, parts, closed)
        write_obj(os.path.join(out_dir, mid + ".obj"), part_names, parts)


if __name__ == "__main__":
    main()
