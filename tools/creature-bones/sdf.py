# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Small signed-distance toolkit for tools/creature-bones (pure Python, no dependencies): primitives, smooth
# blends, value noise, and a narrow-band naive surface-nets mesher that writes an all-quad, watertight OBJ.
import math

# ---------------------------------------------------------------- vectors
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def length(a): return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
def lerp(a, b, t): return a + (b - a) * t
def clamp(x, lo, hi): return lo if x < lo else hi if x > hi else x
def smoothstep(e0, e1, x):
    t = clamp((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


# ---------------------------------------------------------------- blends
def smin(a, b, k):
    if k <= 0: return min(a, b)
    h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return lerp(b, a, h) - k * h * (1 - h)

def smax(a, b, k): return -smin(-a, -b, k)
def ssub(a, b, k): return smax(a, -b, k)  # a minus b


# ---------------------------------------------------------------- primitives (exact or close bounds)
def sphere(p, c, r): return length(sub(p, c)) - r

def ellipsoid(p, c, r):
    q = sub(p, c)
    k0 = length((q[0] / r[0], q[1] / r[1], q[2] / r[2]))
    k1 = length((q[0] / (r[0] * r[0]), q[1] / (r[1] * r[1]), q[2] / (r[2] * r[2])))
    return k0 * (k0 - 1.0) / k1 if k1 > 1e-12 else -min(r)

def round_cone(p, a, b, ra, rb):
    """A capsule from a (radius ra) to b (radius rb), tangent-continuous (iq's round cone)."""
    ba = sub(b, a)
    l2 = dot(ba, ba)
    rr = ra - rb
    a2 = l2 - rr * rr
    il2 = 1.0 / l2
    pa = sub(p, a)
    y = dot(pa, ba)
    z = y - l2
    xv = sub(mul(pa, l2), mul(ba, y))
    x2 = dot(xv, xv)
    y2 = y * y * l2
    z2 = z * z * l2
    k = math.copysign(1.0, rr) * rr * rr * x2
    if math.copysign(1.0, z) * a2 * z2 > k: return math.sqrt(x2 + z2) * il2 - rb
    if math.copysign(1.0, y) * a2 * y2 < k: return math.sqrt(x2 + y2) * il2 - ra
    return (math.sqrt(x2 * a2 * il2) + y * rr) * il2 - ra

def round_box(p, c, half, r):
    q = sub(p, c)
    dx, dy, dz = abs(q[0]) - half[0] + r, abs(q[1]) - half[1] + r, abs(q[2]) - half[2] + r
    out = length((max(dx, 0), max(dy, 0), max(dz, 0)))
    return out + min(max(dx, dy, dz), 0.0) - r

def torus_xz(p, c, R, r):
    """A torus around the Y axis through c."""
    q = sub(p, c)
    return length((math.hypot(q[0], q[2]) - R, q[1], 0.0)) - r


class Chain:
    """A swept tube: round cones between consecutive (point, radius) samples, with a bounding box per segment
    so far segments are skipped cheaply."""
    def __init__(self, pts, radii):
        self.segs = []
        for i in range(len(pts) - 1):
            a, b, ra, rb = pts[i], pts[i + 1], radii[i], radii[i + 1]
            if length(sub(b, a)) < 1e-9: continue
            m = max(ra, rb)
            lo = tuple(min(a[k], b[k]) - m for k in range(3))
            hi = tuple(max(a[k], b[k]) + m for k in range(3))
            self.segs.append((a, b, ra, rb, lo, hi))

    def __call__(self, p):
        best = 1e9
        for a, b, ra, rb, lo, hi in self.segs:
            # distance to the segment's box: a lower bound of its distance
            dx = max(lo[0] - p[0], 0.0, p[0] - hi[0])
            dy = max(lo[1] - p[1], 0.0, p[1] - hi[1])
            dz = max(lo[2] - p[2], 0.0, p[2] - hi[2])
            if dx * dx + dy * dy + dz * dz >= best * best and best > 0: continue
            d = round_cone(p, a, b, ra, rb)
            if d < best: best = d
        return best


def sample_curve(fn, n):
    return [fn(i / (n - 1)) for i in range(n)]


# ---------------------------------------------------------------- noise
def _hash(ix, iy, iz, seed):
    h = (ix * 374761393 + iy * 668265263 + iz * 2147483647 + seed * 144269504) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFF) / 32767.5 - 1.0

def vnoise(x, y, z, seed=0):
    ix, iy, iz = math.floor(x), math.floor(y), math.floor(z)
    fx, fy, fz = x - ix, y - iy, z - iz
    ux, uy, uz = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy), fz * fz * (3 - 2 * fz)
    def h(a, b, c): return _hash(ix + a, iy + b, iz + c, seed)
    x00 = lerp(h(0, 0, 0), h(1, 0, 0), ux); x10 = lerp(h(0, 1, 0), h(1, 1, 0), ux)
    x01 = lerp(h(0, 0, 1), h(1, 0, 1), ux); x11 = lerp(h(0, 1, 1), h(1, 1, 1), ux)
    return lerp(lerp(x00, x10, uy), lerp(x01, x11, uy), uz)

def fbm(p, freq, octaves=3, seed=0):
    s, amp, tot = 0.0, 1.0, 0.0
    for o in range(octaves):
        s += amp * vnoise(p[0] * freq, p[1] * freq, p[2] * freq, seed + o)
        tot += amp
        amp *= 0.5
        freq *= 2.03
    return s / tot


# ---------------------------------------------------------------- mesher
def surface_nets(f, lo, hi, cell, path, name="piece", mirror_x=False):
    """Meshes {f < 0} inside the box lo..hi with cubic cells of size `cell` (naive surface nets: one vertex per
    sign-changing cell, one quad per sign-changing edge), narrow band: a coarse pass finds the blocks near the
    surface. With mirror_x the field is evaluated for x <= 0 only and mirrored (an exactly symmetric mesh)."""
    n = [max(2, int(math.ceil((hi[k] - lo[k]) / cell))) for k in range(3)]
    if mirror_x:  # the grid symmetric about x = 0, x = 0 on a grid plane
        half = int(math.ceil(max(-lo[0], hi[0]) / cell))
        n[0] = 2 * half
        lo = (-half * cell, lo[1], lo[2])
    B = 4  # block size in cells
    nb = [(n[k] + B - 1) // B for k in range(3)]
    vals = {}

    def val(i, j, k):
        key = (i, j, k)
        v = vals.get(key)
        if v is None:
            x = lo[0] + i * cell
            if mirror_x and x > 0: x = -x
            v = f((x, lo[1] + j * cell, lo[2] + k * cell))
            if v == 0.0: v = 1e-9
            vals[key] = v
        return v

    margin = 1.6 * B * cell * math.sqrt(3) / 2 * 1.6
    active = []
    for bi in range(nb[0]):
        for bj in range(nb[1]):
            for bk in range(nb[2]):
                c = (lo[0] + (bi + 0.5) * B * cell, lo[1] + (bj + 0.5) * B * cell, lo[2] + (bk + 0.5) * B * cell)
                x = c[0]
                if mirror_x and x > 0: x = -x
                if abs(f((x, c[1], c[2]))) < margin: active.append((bi, bj, bk))
    # cells to examine: every cell of an active block
    verts, index = [], {}
    def cell_vertex(i, j, k):
        key = (i, j, k)
        if key in index: return index[key]
        corners = [val(i + a, j + b, k + c) for c in (0, 1) for b in (0, 1) for a in (0, 1)]
        inside = [v < 0 for v in corners]
        if all(inside) or not any(inside):
            index[key] = -1
            return -1
        sx = sy = sz = 0.0
        cnt = 0
        for (u, w) in ((0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)):
            if inside[u] != inside[w]:
                t = corners[u] / (corners[u] - corners[w])
                au, bu, cu = u & 1, (u >> 1) & 1, (u >> 2) & 1
                aw, bw, cw = w & 1, (w >> 1) & 1, (w >> 2) & 1
                sx += lerp(au, aw, t); sy += lerp(bu, bw, t); sz += lerp(cu, cw, t)
                cnt += 1
        pos = (lo[0] + (i + sx / cnt) * cell, lo[1] + (j + sy / cnt) * cell, lo[2] + (k + sz / cnt) * cell)
        index[key] = len(verts)
        verts.append(pos)
        return index[key]

    quads, seen = [], set()
    for bi, bj, bk in active:
        for i in range(bi * B, min((bi + 1) * B, n[0])):
            for j in range(bj * B, min((bj + 1) * B, n[1])):
                for k in range(bk * B, min((bk + 1) * B, n[2])):
                    v0 = val(i, j, k)
                    # the three edges leaving grid point (i,j,k) in +x, +y, +z
                    for axis in range(3):
                        e = (i + (axis == 0), j + (axis == 1), k + (axis == 2))
                        if e[0] > n[0] or e[1] > n[1] or e[2] > n[2]: continue
                        v1 = val(*e)
                        if (v0 < 0) == (v1 < 0): continue
                        if (i, j, k, axis) in seen: continue
                        seen.add((i, j, k, axis))
                        # the four cells around this edge
                        if axis == 0: cs = [(i, j - 1, k - 1), (i, j, k - 1), (i, j, k), (i, j - 1, k)]
                        elif axis == 1: cs = [(i - 1, j, k - 1), (i - 1, j, k), (i, j, k), (i, j, k - 1)]
                        else: cs = [(i - 1, j - 1, k), (i, j - 1, k), (i, j, k), (i - 1, j, k)]
                        if any(c[0] < 0 or c[1] < 0 or c[2] < 0 or c[0] >= n[0] or c[1] >= n[1] or c[2] >= n[2] for c in cs):
                            continue
                        ids = [cell_vertex(*c) for c in cs]
                        if min(ids) < 0: continue
                        if v0 > 0: ids.reverse()  # faces point from inside to outside
                        quads.append(ids)
    # the narrow band may miss a few edges at block borders: they are inside active blocks of a neighbour, so
    # the surface is closed as long as the margin is generous; report open edges so a build can fail loudly
    edges = {}
    for q in quads:
        for a in range(4):
            e = (q[a], q[(a + 1) % 4])
            edges[e] = edges.get(e, 0) + 1
    open_edges = sum(1 for (a, b) in edges if (b, a) not in edges)
    with open(path, "w") as out:
        out.write(f"o {name}\n")
        for v in verts: out.write(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}\n")
        for q in quads: out.write("f " + " ".join(str(i + 1) for i in q) + "\n")
    return len(verts), len(quads), open_edges
