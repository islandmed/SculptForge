# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Candidate generators for the Insider daily drops.

Everything here is made from scratch by our own procedural code: tileable noise, scales, fabric weaves and skin
pores, wrinkle and trim stamps, hard-surface trim brushes, Kelvinlet clay presets and vector displacement maps
captured from procedural shapes. No third-party images or other apps' output go in.

`candidates(date)` is seeded by the date alone, so the same date always gives byte-identical files.
"""

import datetime
import hashlib
import json
import math
import random
import struct

import sfimage

ALPHA_SIZE = 512
VDM_SIZE = 192
KINDS = ["noise", "scales", "weave", "pores", "wrinkles", "trims", "kelvinlet"]  # in the daily rotation
# Kept out of the rotation until they make useful, recognisable shapes (coordinator review 11-01: the VDMs read as a
# blob or a horn on a sphere; wanted: a horn set, ear, nostril, scale plate, rivet ...). make("vdm", ...) still works.
DISABLED = ["vdm"]
PER_DAY = 6  # candidates per day; the gate keeps one


def seed_for(date, salt=""):
    return int.from_bytes(hashlib.sha256(f"sculptforge-drop:{date.isoformat()}:{salt}".encode()).digest()[:8], "big")


# --- Tileable building blocks (period divides the image, so every map wraps) ---

def _smooth(t):
    return t * t * t * (t * (t * 6 - 15) + 10)


def value_noise(rng, size, period):
    lat = [[rng.random() for _ in range(period)] for _ in range(period)]
    idx, frac = [], []
    for x in range(size):
        p = x * period / size
        i = int(p)
        idx.append((i % period, (i + 1) % period))
        frac.append(_smooth(p - i))
    out = []
    for y in range(size):
        (j0, j1), fy = idx[y], frac[y]
        r0, r1 = lat[j0], lat[j1]
        row = []
        for x in range(size):
            (i0, i1), fx = idx[x], frac[x]
            a = r0[i0] + (r0[i1] - r0[i0]) * fx
            b = r1[i0] + (r1[i1] - r1[i0]) * fx
            row.append(a + (b - a) * fy)
        out.append(row)
    return out


def fbm(rng, size, period, octaves, gain=0.5, mode="plain"):
    total = [[0.0] * size for _ in range(size)]
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        n = value_noise(rng, size, period << o)
        for y in range(size):
            t, s = total[y], n[y]
            for x in range(size):
                v = s[x]
                if mode == "ridged":
                    v = 1.0 - abs(2.0 * v - 1.0)
                    v *= v
                elif mode == "billow":
                    v = abs(2.0 * v - 1.0)
                t[x] += v * amp
        norm += amp
        amp *= gain
    return [[v / norm for v in row] for row in total]


def worley(rng, size, cells, jitter=0.9):
    """F1 and F2 distances (in cell units) to jittered points on a cells x cells wrapping grid."""
    pts = [[(0.5 + (rng.random() - 0.5) * jitter, 0.5 + (rng.random() - 0.5) * jitter) for _ in range(cells)]
           for _ in range(cells)]
    f1 = [[0.0] * size for _ in range(size)]
    f2 = [[0.0] * size for _ in range(size)]
    ids = [[0] * size for _ in range(size)]
    s = cells / size
    for y in range(size):
        py = (y + 0.5) * s
        cy = int(py)
        for x in range(size):
            px = (x + 0.5) * s
            cx = int(px)
            d1 = d2 = 9.0
            best = 0
            for oy in (-1, 0, 1):
                gy = cy + oy
                row = pts[gy % cells]
                for ox in (-1, 0, 1):
                    gx = cx + ox
                    qx, qy = row[gx % cells]
                    dx, dy = gx + qx - px, gy + qy - py
                    d = dx * dx + dy * dy
                    if d < d1:
                        d2, d1, best = d1, d, (gy % cells) * cells + gx % cells
                    elif d < d2:
                        d2 = d
            f1[y][x], f2[y][x], ids[y][x] = math.sqrt(d1), math.sqrt(d2), best
    return f1, f2, ids


def normalize(grid, lo_pct=0.01, hi_pct=0.99):
    vals = sorted(v for row in grid for v in row)
    lo, hi = vals[int(lo_pct * (len(vals) - 1))], vals[int(hi_pct * (len(vals) - 1))]
    k = 1.0 / max(hi - lo, 1e-6)
    return [[min(1.0, max(0.0, (v - lo) * k)) for v in row] for row in grid]


def window(size, inner=0.55):
    """Radial falloff: 1 inside `inner`, smooth to 0 at the square's inscribed circle (a stamp fades out)."""
    out = []
    for y in range(size):
        v = (y + 0.5) / size * 2 - 1
        row = []
        for x in range(size):
            u = (x + 0.5) / size * 2 - 1
            r = math.sqrt(u * u + v * v)
            t = min(1.0, max(0.0, (r - inner) / (0.98 - inner)))
            row.append(1.0 - t * t * (3 - 2 * t))
        out.append(row)
    return out


def mul(a, b):
    return [[x * y for x, y in zip(ra, rb)] for ra, rb in zip(a, b)]


# --- Names and words ---

def pick(rng, items):
    return items[rng.randrange(len(items))]


# --- Kinds ---

def make_noise(rng, size):
    variant = pick(rng, ["cloud", "ridged", "billow", "warped", "grain"])
    period = pick(rng, [2, 4, 4, 8])
    octaves = rng.randint(4, 6)
    if variant == "cloud":
        g = fbm(rng, size, period, octaves, 0.55)
        name, use = pick(rng, ["Soft Cloud", "Rolling Mist", "Clay Cloud"]), "breaking up large, smooth forms"
    elif variant == "ridged":
        g = fbm(rng, size, period, octaves, 0.5, "ridged")
        name, use = pick(rng, ["Ridged Stone", "Vein Rock", "Ridge Field"]), "rock veins and eroded ridges"
    elif variant == "billow":
        g = fbm(rng, size, period, octaves, 0.5, "billow")
        name, use = pick(rng, ["Billow Lumps", "Pitted Crust", "Lumpy Plaster"]), "pitted, lumpy plaster and crusts"
    elif variant == "warped":
        base = fbm(rng, size, period, octaves, 0.5)
        warp = fbm(rng, size, period, 3, 0.5)
        amt = rng.uniform(0.04, 0.12) * size
        g = [[base[int(y + (warp[y][x] - 0.5) * amt) % size][int(x + (warp[(y + size // 3) % size][x] - 0.5) * amt) % size]
              for x in range(size)] for y in range(size)]
        name, use = pick(rng, ["Warped Flow", "Swirl Grain", "Molten Drift"]), "flowing, molten or wind-worn surfaces"
    else:
        g = fbm(rng, size, period * 4, 3, 0.7)
        name, use = pick(rng, ["Fine Grain", "Sand Grain", "Stipple Grain"]), "fine grain on stone, sand or cast metal"
    return {"type": "alpha", "tileable": True, "grid": normalize(g), "name": f"{name} Noise",
            "description": f"Tileable noise alpha for {use}; use with Drag Stamp or Spray.", "params": {"variant": variant}}


def make_scales(rng, size):
    variant = pick(rng, ["dome", "plate", "shingle"])
    cells = pick(rng, [6, 8, 10, 12, 16])
    if variant == "shingle":
        rows = cells
        g = []
        for y in range(size):
            fy = (y + 0.5) / size * rows
            j = int(fy)
            row = []
            for x in range(size):
                best = 0.0
                for dj in (0, 1):  # a scale from this row or the one overlapping from below
                    jj = j + dj
                    off = 0.5 * (jj % 2)
                    fx = (x + 0.5) / size * cells + off
                    cx = math.floor(fx) + 0.5
                    dx, dy = (fx - cx) * 2, (fy - jj) * 2 + 0.2
                    r = math.sqrt(dx * dx + dy * dy)
                    if r < 1.15:
                        best = max(best, (1 - r / 1.15) ** 0.6 * (0.55 + 0.45 * (fy - j + dj * 0)))
                row.append(best)
            g.append(row)
        name, use = pick(rng, ["Fish Scales", "Overlap Scales", "Shingle Scales"]), "fish, snake bellies and armour"
    else:
        f1, f2, _ = worley(rng, size, cells, 0.8)
        if variant == "dome":
            g = [[max(0.0, 1 - a * 1.4) ** 0.7 for a in row] for row in f1]
            name, use = pick(rng, ["Round Scales", "Pebble Scales", "Bead Scales"]), "lizard skin and beaded hides"
        else:
            g = [[min(1.0, (b - a) * 3.0) ** 0.5 for a, b in zip(r1, r2)] for r1, r2 in zip(f1, f2)]
            name, use = pick(rng, ["Plate Scales", "Croc Plates", "Tile Hide"]), "crocodile plates and armoured hide"
    detail = fbm(rng, size, 8, 2)
    g = [[v * (0.9 + 0.1 * d) for v, d in zip(r, dr)] for r, dr in zip(g, detail)]
    return {"type": "alpha", "tileable": True, "grid": normalize(g), "name": name,
            "description": f"Tileable scale alpha for {use}; {cells} scales across.", "params": {"variant": variant, "cells": cells}}


WEAVES = {  # over/under matrices (1 = warp over weft), one repeat
    "Plain": [[1, 0], [0, 1]],
    "Twill": [[1, 1, 0, 0], [0, 1, 1, 0], [0, 0, 1, 1], [1, 0, 0, 1]],
    "Basket": [[1, 1, 0, 0], [1, 1, 0, 0], [0, 0, 1, 1], [0, 0, 1, 1]],
    "Satin": [[1, 0, 0, 0, 0], [0, 0, 1, 0, 0], [0, 0, 0, 0, 1], [0, 1, 0, 0, 0], [0, 0, 0, 1, 0]],
    "Herringbone": [[1, 1, 0, 0, 0, 0, 1, 1], [0, 1, 1, 0, 0, 1, 1, 0], [0, 0, 1, 1, 1, 1, 0, 0], [1, 0, 0, 1, 1, 0, 0, 1]],
}


def make_weave(rng, size):
    kind = pick(rng, list(WEAVES))
    pat = WEAVES[kind]
    ph, pw = len(pat), len(pat[0])
    reps = pick(rng, [2, 3, 4, 6, 8])
    tx, ty = pw * reps, ph * reps * (pw // ph if pw > ph else 1)
    gap = rng.uniform(0.08, 0.2)
    fibre = fbm(rng, size, 16, 2)
    g = []
    for y in range(size):
        fy = (y + 0.5) / size * ty
        j = int(fy)
        ly = fy - j
        row = []
        for x in range(size):
            fx = (x + 0.5) / size * tx
            i = int(fx)
            lx = fx - i
            over = pat[j % ph][i % pw]
            across, along = (lx, ly) if over else (ly, lx)  # the top thread's profile and length
            if across < gap / 2 or across > 1 - gap / 2:
                h = 0.15
            else:
                a = (across - gap / 2) / (1 - gap)
                h = 0.35 + 0.65 * math.sin(math.pi * a) ** 0.6 * (0.75 + 0.25 * math.sin(math.pi * along))
            row.append(h * (0.92 + 0.08 * fibre[y][x]))
        g.append(row)
    return {"type": "alpha", "tileable": True, "grid": normalize(g), "name": f"{kind} Weave {tx}",
            "description": f"Tileable {kind.lower()} weave alpha for cloth, straps and sacking; {tx} threads across.",
            "params": {"weave": kind, "threads": tx}}


def make_pores(rng, size):
    cells = pick(rng, [24, 32, 40, 48])
    f1, _, ids = worley(rng, size, cells, 1.0)
    rad = rng.uniform(0.18, 0.3)
    skin = fbm(rng, size, 8, 4, 0.55)
    creases = rng.random() < 0.5
    g = []
    for y in range(size):
        row = []
        for x in range(size):
            d = f1[y][x]
            r = rad * (0.6 + 0.8 * ((ids[y][x] * 2654435761 % 1000) / 1000.0))
            pore = max(0.0, 1 - d / r) ** 1.5
            v = 0.25 * skin[y][x] + 0.75 * pore
            if creases:
                v += 0.15 * max(0.0, 1 - abs(skin[y][x] - 0.5) * 30)
            row.append(v)
        g.append(row)
    which = pick(rng, ["Fine", "Cheek", "Nose", "Back"]) if cells >= 32 else pick(rng, ["Coarse", "Orange Peel"])
    extra = " and fine creases" if creases else ""
    return {"type": "alpha", "tileable": True, "grid": normalize(g), "name": f"{which} Pores",
            "description": f"Tileable skin pore alpha{extra}; carve with low strength on faces and hands.",
            "params": {"cells": cells, "creases": creases}}


def make_wrinkles(rng, size):
    variant = pick(rng, ["forehead", "crow", "knuckle"])
    lines = rng.randint(3, 7)
    width = rng.uniform(0.012, 0.03)
    g = [[0.0] * size for _ in range(size)]
    if variant == "crow":
        spread = rng.uniform(0.9, 1.6)
        base = [(-spread / 2 + spread * (k + rng.uniform(-0.25, 0.25)) / max(1, lines - 1), rng.uniform(0.5, 0.95),
                 rng.uniform(0.02, 0.08), rng.uniform(3, 7)) for k in range(lines)]
        for y in range(size):
            v = (y + 0.5) / size * 2 - 1
            for x in range(size):
                u = (x + 0.5) / size * 2 - 1 + 0.9  # fan out from the left edge
                r = math.hypot(u, v)
                th = math.atan2(v, u)
                best = 0.0
                for th0, length, wob, fr in base:
                    if r > length * 1.8:
                        continue
                    d = abs(th - th0 - wob * math.sin(fr * r)) * r
                    w = width * (1.2 - r / (length * 1.8))
                    if w > 0:
                        best = max(best, math.exp(-(d / w) ** 2) * min(1.0, r * 4))
                g[y][x] = best
        name, use = "Crow's Feet Wrinkles", "eye corners and fanned creases"
    else:
        rows = [(-0.6 + 1.2 * (k + 0.5) / lines + rng.uniform(-0.05, 0.05), rng.uniform(0.02, 0.07),
                 rng.uniform(2, 5), rng.uniform(0, 6.28), rng.uniform(0.5, 1.0)) for k in range(lines)]
        if variant == "knuckle":
            rows = [(y0, a * 2, f, p, s) for y0, a, f, p, s in rows]
        for y in range(size):
            v = (y + 0.5) / size * 2 - 1
            for x in range(size):
                u = (x + 0.5) / size * 2 - 1
                best = 0.0
                for y0, amp, fr, ph, strength in rows:
                    c = y0 + amp * math.sin(fr * u + ph) * (1 + (0.5 * abs(u) if variant == "knuckle" else 0))
                    slope = amp * fr * math.cos(fr * u + ph)
                    d = abs(v - c) / math.sqrt(1 + slope * slope)
                    taper = max(0.0, 1 - (u / 0.9) ** 2)
                    best = max(best, strength * taper * math.exp(-(d / width) ** 2))
                g[y][x] = best
        name = "Forehead Wrinkles" if variant == "forehead" else "Knuckle Folds"
        use = "foreheads and long skin creases" if variant == "forehead" else "knuckles, elbows and bunched skin"
    g = mul(g, window(size, 0.5))
    return {"type": "alpha", "tileable": False, "grid": normalize(g, 0.0, 0.999), "name": f"{name} {lines}",
            "description": f"Wrinkle stamp alpha with {lines} creases for {use}; carve with Drag Stamp or a short stroke.",
            "params": {"variant": variant, "lines": lines}}


def brush_json(name, description, **k):
    r = {"name": name, "description": description, "direction": "normal", "depth": 0.5, "plane": "none",
         "planeAmount": 0.5, "pinch": 0.0, "drag": 0.0, "smooth": 0.0, "hardness": 0.0, "tipSharpness": 0.0,
         "pattern": "none", "patternScale": 0.3, "patternAmount": 0.0, "spacing": 0.15, "jitter": 0.0,
         "pressureCurve": 1.0, "frontFacesOnly": False, "cavityMask": 0.0}
    r.update({key: round(v, 3) if isinstance(v, float) else v for key, v in k.items()})
    r.update({"author": "The SculptForge developer", "format": "sfbrush", "version": 1})
    return (json.dumps(r, indent=2) + "\n").encode()


def make_trims(rng, size):
    if rng.random() < 0.55:  # a hard-surface trim brush
        variant = pick(rng, ["panel", "seam", "bevel", "groove", "weld"])
        u = rng.uniform
        if variant == "panel":
            name, desc = "Panel Cut", "Cuts a crisp, narrow panel line into hard surfaces."
            p = dict(depth=-u(0.25, 0.45), pinch=u(0.3, 0.55), hardness=u(0.6, 0.8), spacing=u(0.05, 0.08))
        elif variant == "seam":
            name, desc = "Raised Seam", "Builds a raised, flat-topped seam along panel edges."
            p = dict(depth=u(0.25, 0.45), plane="flatten", planeAmount=u(0.6, 0.9), pinch=u(0.15, 0.4),
                     hardness=u(0.6, 0.8), spacing=u(0.05, 0.1))
        elif variant == "bevel":
            name, desc = "Bevel Trim", "Flattens a hard edge into a narrow bevel; work along the edge."
            p = dict(depth=u(0.1, 0.3), plane="flatten", planeAmount=u(0.8, 1.0), hardness=u(0.85, 1.0),
                     frontFacesOnly=True, spacing=u(0.06, 0.12))
        elif variant == "groove":
            name, desc = "Scraped Groove", "Scrapes a flat-bottomed groove for trims and inset strips."
            p = dict(depth=-u(0.25, 0.45), plane="scrape", planeAmount=u(0.6, 0.9), hardness=u(0.6, 0.85),
                     spacing=u(0.05, 0.1))
        else:
            name, desc = "Weld Bead", "Lays a rippled weld bead along joins."
            p = dict(depth=u(0.4, 0.7), pinch=u(0.1, 0.3), pattern="ridges", patternScale=u(0.2, 0.4),
                     patternAmount=u(0.4, 0.7), hardness=u(0.5, 0.7), spacing=u(0.08, 0.15))
        name = f"{name} {rng.randint(2, 9)}"
        return {"type": "brush", "tileable": False, "grid": None, "name": name, "description": desc,
                "data": brush_json(name, desc, **p), "params": {"variant": variant, **{k: v for k, v in p.items()}}}
    variant = pick(rng, ["rivets", "vents", "bolts"])
    g = [[0.0] * size for _ in range(size)]
    if variant == "rivets":
        n = rng.randint(4, 9)
        rr = rng.uniform(0.04, 0.07)
        rows = pick(rng, [1, 2])
        centers = [(-0.75 + 1.5 * i / (n - 1), (r - (rows - 1) / 2) * 0.3) for i in range(n) for r in range(rows)]
        name, use = f"Rivet Strip {n * rows}", "plating seams and armour edges"
    elif variant == "vents":
        n = rng.randint(3, 7)
        centers, rr = None, 0
        name, use = f"Vent Slots {n}", "intakes, grilles and machine housings"
    else:
        n = pick(rng, [4, 6])
        rr = rng.uniform(0.07, 0.1)
        centers = [(0.6 * math.cos(2 * math.pi * k / n), 0.6 * math.sin(2 * math.pi * k / n)) for k in range(n)]
        name, use = f"Hex Bolt Ring {n}", "flanges, hatches and wheel hubs"
    for y in range(size):
        v = (y + 0.5) / size * 2 - 1
        for x in range(size):
            u = (x + 0.5) / size * 2 - 1
            if variant == "vents":
                pitch = 1.4 / n
                k = round((v + 0.7 - pitch / 2) / pitch)
                cy = -0.7 + pitch / 2 + k * pitch
                if 0 <= k < n and abs(u) < 0.65:
                    d = abs(v - cy) / (pitch * 0.3)
                    end = max(0.0, (abs(u) - 0.55) / 0.1)
                    g[y][x] = max(0.0, 1 - max(d, end) ** 4) if max(d, end) < 1 else 0.0
                continue
            best = 0.0
            for cx, cy in centers:
                dx, dy = u - cx, v - cy
                if variant == "bolts":  # a hexagon: the largest of three projections
                    d = max(abs(dx), abs(dx * 0.5 + dy * 0.866), abs(dx * 0.5 - dy * 0.866)) / rr
                    best = max(best, 1.0 if d < 0.7 else max(0.0, (1 - d) / 0.3) if d < 1 else 0.0)
                else:
                    d = math.hypot(dx, dy) / rr
                    if d < 1:
                        best = max(best, math.sqrt(1 - d * d))
            g[y][x] = best
    g = mul(g, window(size, 0.8))
    return {"type": "alpha", "tileable": False, "grid": g, "name": name,
            "description": f"Hard-surface trim alpha for {use}; stamp it with Drag Stamp.", "params": {"variant": variant}}


def make_kelvinlet(rng, size):
    feel = pick(rng, [("Soft Clay", 0.2, 0.45, 0.0, 0.15), ("Firm Clay", 0.55, 0.75, 0.1, 0.3),
                      ("Wet Clay", 0.85, 1.0, 0.0, 0.1), ("Rubber Clay", 0.0, 0.2, 0.2, 0.45),
                      ("Stiff Wax", 0.6, 0.85, 0.5, 0.8)])
    label, s0, s1, h0, h1 = feel
    squash, sharp = round(rng.uniform(s0, s1), 2), round(rng.uniform(h0, h1), 2)
    spacing = round(rng.uniform(0.6, 1.4), 2)
    bulge = "bulges like real clay" if squash > 0.6 else "stays springy" if squash < 0.3 else "holds its volume"
    name = f"Kelvinlet {label}"
    desc = f"Kelvinlet brush preset: Squash {round(squash * 100)}, Sharpness {round(sharp * 100)}; the surface {bulge}."
    text = (f"# name: {name}\n# description: {desc}\n"
            f"kelvinlet_settings squash={squash:g} sharpness={sharp:g} clone_spacing={spacing:g}\n")
    return {"type": "macro", "tileable": False, "grid": None, "name": name, "description": desc,
            "data": text.encode(), "params": {"squash": squash, "sharpness": sharp, "clone_spacing": spacing}}


def vdm_bytes(name, w, h, vecs):
    nb = name.encode()
    head = b"SFVDM1\n\0" + struct.pack("<III", w, h, len(nb)) + nb
    return head + struct.pack(f"<{len(vecs)}f", *vecs)


def make_vdm(rng, size):
    """A procedural shape over the flat square; each texel's displacement (in brush radii) is captured as a VDM."""
    variant = pick(rng, ["claw", "twist", "lip", "knot", "sucker"])
    height = rng.uniform(0.5, 1.1)
    bend = rng.uniform(0.2, 0.6) * pick(rng, [-1, 1])
    twist = rng.uniform(1.5, 4.0)
    turns = rng.randint(3, 6)
    vecs, sig = [], []
    for y in range(size):
        v = 1 - (y + 0.5) / size * 2  # row 0 at the top (+v)
        srow = []
        for x in range(size):
            u = (x + 0.5) / size * 2 - 1
            r = math.hypot(u, v)
            t = max(0.0, 1 - r / 0.9)
            fade = t * t * (3 - 2 * t)
            dx = dy = dz = 0.0
            if variant == "claw":  # a horn that hooks over: the tip moves sideways as it rises
                z = height * fade ** 1.6
                dz, dy = z, bend * (z / height) ** 2 * height
                dx, dy = -u * 0.3 * fade, dy - v * 0.3 * fade
            elif variant == "twist":  # a spike twisted around its axis
                z = height * fade ** 2
                a = twist * fade
                ca, sa = math.cos(a) - 1, math.sin(a)
                dx, dy, dz = (u * ca - v * sa) * (1 - fade), (u * sa + v * ca) * (1 - fade), z
            elif variant == "lip":  # a ridge whose top curls over (an overhanging lip)
                band = max(0.0, 1 - abs(v) / 0.35) * max(0.0, 1 - (u / 0.9) ** 4)
                b = band * band * (3 - 2 * band)
                dz = height * 0.7 * b
                dy = bend * b ** 2
            elif variant == "knot":  # a lumpy knot: a dome with radial lobes
                lobes = 1 + 0.35 * math.cos(turns * math.atan2(v, u))
                z = height * 0.6 * fade ** 1.2 * lobes
                dz = z
                dx, dy = u * 0.15 * fade, v * 0.15 * fade
            else:  # a sucker: a raised ring with a cupped centre
                ring = math.exp(-((r - 0.45) / 0.15) ** 2)
                cup = -0.35 * math.exp(-(r / 0.25) ** 2)
                dz = height * 0.5 * (ring + cup) * (1 if r < 0.9 else 0)
                dx, dy = -u * 0.1 * ring, -v * 0.1 * ring
            vecs += [dx, dy, dz]
            srow.append(math.sqrt(dx * dx + dy * dy + dz * dz))
        sig.append(srow)
    m = max(max(r) for r in sig) or 1.0
    names = {"claw": "Hooked Claw", "twist": "Twisted Spike", "lip": "Curled Lip", "knot": "Burl Knot",
             "sucker": "Sucker Cup"}
    uses = {"claw": "claws, thorns and hooked horns", "twist": "twisted horns and spiral spines",
            "lip": "overhanging brows, ledges and rolled rims", "knot": "tree burls and lumpy growths",
            "sucker": "tentacle suckers and pocked organic forms"}
    name = f"{names[variant]} {rng.randint(2, 9)}"
    return {"type": "vdm", "tileable": False, "grid": [[s / m for s in row] for row in sig], "name": name,
            "description": f"Vector displacement stamp for {uses[variant]}; drag it out on the surface.",
            "data": vdm_bytes(name, size, size, vecs),
            "params": {"variant": variant, "height": round(height, 3), "bend": round(bend, 3)}}


MAKERS = {"noise": make_noise, "scales": make_scales, "weave": make_weave, "pores": make_pores,
          "wrinkles": make_wrinkles, "trims": make_trims, "kelvinlet": make_kelvinlet, "vdm": make_vdm}


def kinds_for(date):
    """The day's candidate kinds: a shuffled rotation, so every kind comes up about equally often."""
    rng = random.Random(seed_for(date, "kinds"))
    kinds = KINDS[:]
    rng.shuffle(kinds)
    return kinds[:PER_DAY]


def make(kind, date, index=0):
    """One candidate: deterministic for (kind, date, index)."""
    rng = random.Random(seed_for(date, f"{kind}:{index}"))
    size = VDM_SIZE if kind == "vdm" else ALPHA_SIZE
    c = MAKERS[kind](rng, size)
    c["kind"] = kind
    c["date"] = date.isoformat()
    return c


def candidates(date):
    return [make(k, date) for k in kinds_for(date)]


def parse_date(text):
    return datetime.date.fromisoformat(text)
