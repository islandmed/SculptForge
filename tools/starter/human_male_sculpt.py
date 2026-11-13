#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The Human (male, A-pose)'s definition pass: sculptforge-cli strokes (our own brushes, deterministic) that carve
# the muscle forms a lean athletic figure shows, placed from the figure's joints (human-male.landmarks.json, written
# by build_starter_meshes.py) and a few points read off its mesh. tools/starter/build.sh runs it: the strokes go
# into a sculpt layer called "Definition" on subdivision level 1 (Add Level), so the artist can turn them down
# (Layers) or step down to the cage; the project is saved stepped down to the cage, and opening the starter mesh
# adds a level on top. Only the left side is stroked; every stroke mirrors in X.
#
# Usage: human_male_sculpt.py <human-male.obj> <human-male.landmarks.json> <out commands.txt>

import json
import math
import sys


def load_obj_verts(path):
    out = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("v "):
                out.append(tuple(float(t) for t in line.split()[1:4]))
    return out


def pt(x, y, z=40.0):
    return f"{x:.1f},{y:.1f},{z:.1f}"


def path(points, z=40.0):
    return " ".join(pt(x, y, z) for x, y in points)


def main():
    obj, landmarks, out = sys.argv[1:4]
    V = load_obj_verts(obj)
    J = {k: tuple(v) for k, v in json.load(open(landmarks, encoding="utf-8")).items()}
    H = max(v[1] for v in V)
    # Points read off the mesh: the nipple (the most forward point of the chest, left side), the navel (the midline's
    # deepest point in the belly band), the sternal notch (the midline's deepest point under the neck).
    chest = [v for v in V if 5.0 < v[0] < 14.0 and 0.68 * H < v[1] < 0.80 * H]
    nipple = max(chest, key=lambda v: v[2])
    belly = sorted((v for v in V if abs(v[0]) < 0.6 and 0.50 * H < v[1] < 0.70 * H), key=lambda v: v[1])
    navel = min(belly, key=lambda v: v[2] - sum(w[2] for w in belly if abs(w[1] - v[1]) < 6.0) / max(1, sum(1 for w in belly if abs(w[1] - v[1]) < 6.0)))
    notch = min((v for v in V if abs(v[0]) < 0.8 and 0.80 * H < v[1] < 0.86 * H), key=lambda v: v[2])
    nx, ny = nipple[0], nipple[1]
    vy = navel[1]
    sh, el, wr = J["l-shoulder"], J["l-elbow"], J["l-hand"]
    hip, knee, ankle = J["l-upper-leg"], J["l-knee"], J["l-ankle"]

    def along(a, b, t, side=0.0):
        """A point t of the way from joint a to joint b, pushed `side` cm perpendicular (in the XY plane, outward)."""
        dx, dy = b[0] - a[0], b[1] - a[1]
        L = math.hypot(dx, dy)
        nx_, ny_ = -dy / L, dx / L  # perpendicular; for an arm going out-and-down this points out-and-up
        return (a[0] + dx * t + nx_ * side, a[1] + dy * t + ny_ * side)

    cmds = []

    def stroke(points, brush, radius, strength, view="0,0,-1", invert=False, curve=True, spacing=None):
        # A stroke down the midline is its own mirror: no symmetry, or it would land twice. A mirrored Crease that
        # reaches the midline from both sides pinches the midline vertices sideways twice (not exactly cancelling),
        # so a symmetric Crease keeps its points at least a radius and a half from the midline.
        xs = [abs(float(q.split(",")[0])) for q in points.split()]
        midline = all(x == 0.0 for x in xs)
        if brush == "Crease" and not midline:
            assert min(xs) >= radius + 0.5, (points, radius)
        c = (f'stroke points="{points}" brush={brush} radius={radius} strength={strength} view={view} '
             + ("" if midline else "symmetry=x ") + f'curve={"true" if curve else "false"}' + (" invert=true" if invert else "")
             + (f" spacing={spacing}" if spacing is not None else ""))
        cmds.append(c)

    FRONT, BACK, LEFT = "0,0,-1", "0,0,1", "-1,0,0"
    # Torso ---------------------------------------------------------------------------------------------------------
    # Pectoral shelf: a crease along the lower border of the pec, sternum to armpit, and the mass above it.
    stroke(path([(3.0, ny - 7.8), (6.0, ny - 8.0), (9.0, ny - 7.5), (13.0, ny - 6.0), (16.5, ny - 3.0), (18.5, ny + 1.5)]),
           "Crease", 2.4, 0.8)
    stroke(path([(4.0, ny - 2.0), (9.0, ny - 1.0), (14.0, ny + 1.0)]), "Inflate", 5.5, 0.35)
    # Sternum groove and linea alba, the sternal notch to the navel (lighter below the chest).
    stroke(path([(0.0, notch[1] - 3.0), (0.0, ny + 2.0), (0.0, ny - 8.0)]), "Crease", 1.6, 0.6)
    stroke(path([(0.0, ny - 9.0), (0.0, (ny - 9.0 + vy) / 2), (0.0, vy + 1.5)]), "Crease", 1.3, 0.5)
    # Abs: three rows above the navel line, each a mound with a tendinous crease between rows.
    for row, yo in enumerate((14.0, 7.5, 1.5)):
        stroke(pt(4.3, vy + yo), "Inflate", 4.3, 0.7)
        stroke(pt(4.3, vy + yo), "Inflate", 3.0, 0.5)
    for yo in (11.0, 4.5, -2.5):
        stroke(path([(2.0, vy + yo), (4.8, vy + yo - 0.3), (7.8, vy + yo + 0.3)]), "Crease", 1.4, 0.6)
    # Below the navel: the lower abs' outer edge (the linea semilunaris) and the inguinal line to the pubis.
    stroke(path([(8.0, vy + 15.0), (8.6, vy + 6.0), (8.0, vy - 3.0), (6.0, vy - 10.0)]), "Crease", 1.5, 0.45)
    stroke(path([(13.0, hip[1] + 4.0), (9.0, hip[1] - 2.0), (4.5, hip[1] - 8.0), (2.2, hip[1] - 11.5)]), "Crease", 1.6, 0.5)
    # Obliques: the mass over the hip and the serratus digitations under the pec's outer edge.
    stroke(path([(12.5, vy + 8.0), (14.0, vy + 2.0), (14.0, vy - 4.0)]), "Inflate", 4.5, 0.3)
    for i, yo in enumerate((-9.0, -12.5, -16.0)):
        stroke(path([(13.5 - i * 0.8, ny + yo), (16.5 - i * 0.5, ny + yo + 1.5)]), "Inflate", 1.8, 0.35)
    # Clavicle: the ridge and the hollow under it.
    stroke(path([(1.5, notch[1] + 0.5), (7.0, notch[1] + 1.5), (13.0, notch[1] + 2.5), (18.0, notch[1] + 3.5)]), "Inflate", 1.6, 0.35)
    stroke(path([(3.0, notch[1] - 2.0), (8.0, notch[1] - 1.5), (13.0, notch[1] - 0.5)]), "Crease", 1.6, 0.4)
    # Arms ----------------------------------------------------------------------------------------------------------
    # Deltoid cap (from the side) and its lower border; biceps and the brachioradialis of the forearm (from the front).
    stroke(pt(sh[0] + 30.0, sh[1] - 1.0, sh[2]), "Inflate", 7.0, 0.4, view=LEFT)
    stroke(path([along(sh, el, 0.40, 3.5), along(sh, el, 0.45, 0.0), along(sh, el, 0.40, -3.5)]), "Crease", 1.5, 0.35)
    stroke(path([along(sh, el, 0.42, -1.0), along(sh, el, 0.60, -1.5), along(sh, el, 0.78, -1.0)]), "Inflate", 4.2, 0.4)
    stroke(path([along(el, wr, 0.10, 1.5), along(el, wr, 0.30, 2.0), along(el, wr, 0.55, 1.5)]), "Inflate", 3.6, 0.35)
    stroke(path([along(el, wr, 0.15, -1.0), along(el, wr, 0.45, -1.5)]), "Inflate", 3.0, 0.25)
    # Legs ----------------------------------------------------------------------------------------------------------
    # Quads: rectus femoris down the front, vastus medialis above the inner knee, vastus lateralis outside; the
    # sartorius furrow from the hip bone to the inner knee; the knee cap.
    stroke(path([along(hip, knee, 0.25, 0.0), along(hip, knee, 0.50, 0.0), along(hip, knee, 0.72, 0.0)]), "Inflate", 5.5, 0.45)
    stroke(pt(knee[0] - 4.5, knee[1] + 8.0), "Inflate", 4.6, 0.6)
    stroke(pt(knee[0] - 4.0, knee[1] + 8.0), "Inflate", 3.0, 0.45)
    stroke(path([along(hip, knee, 0.30, -5.5), along(hip, knee, 0.55, -5.0)]), "Inflate", 4.5, 0.4)
    stroke(path([(hip[0] + 3.0, hip[1] + 2.0), along(hip, knee, 0.35, 1.5), along(hip, knee, 0.70, -3.5), (knee[0] - 5.0, knee[1] + 3.0)]),
           "Crease", 1.5, 0.4)
    stroke(pt(knee[0], knee[1] + 1.0), "Inflate", 3.2, 0.3)
    stroke(path([(knee[0] - 3.5, knee[1] - 2.5), (knee[0] + 3.5, knee[1] - 2.5)]), "Crease", 1.2, 0.3)
    # Calves (from the back, the two heads) and the shin's tibialis ridge (front).
    stroke(path([(knee[0] - 2.5, knee[1] - 8.0), (knee[0] - 3.0, knee[1] - 16.0), (knee[0] - 2.0, knee[1] - 24.0)]), "Inflate", 4.2, 0.4, view=BACK)
    stroke(path([(knee[0] + 2.5, knee[1] - 9.0), (knee[0] + 3.0, knee[1] - 17.0), (knee[0] + 2.0, knee[1] - 23.0)]), "Inflate", 3.6, 0.3, view=BACK)
    stroke(path([(knee[0] + 1.5, knee[1] - 8.0), (knee[0] + 2.0, knee[1] - 20.0), (knee[0] + 1.0, knee[1] - 32.0)]), "Inflate", 2.4, 0.3)

    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(cmds) + "\n")
    print(f"{len(cmds)} strokes -> {out}")


if __name__ == "__main__":
    main()
