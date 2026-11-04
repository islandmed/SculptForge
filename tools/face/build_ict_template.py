#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds assets/face/ict-arkit.sfface, the template head SculptForge's Generate Facial Shapes fits to a sculpt.

Source: ICT-FaceKit (USC Institute for Creative Technologies, MIT licence), the "ICT Face Model Light":
https://github.com/ICT-VGL/ICT-FaceKit (FaceXModel/*.obj). Download that folder (about 150 MB of OBJs), then

    python3 tools/face/build_ict_template.py <ICT-FaceKit>/FaceXModel assets/face/ict-arkit.sfface

What is kept: the neutral mesh's skin (face, head and neck), mouth socket and eye sockets (vertices 0-14061), and
for each of the 52 Apple ARKit blend shapes the offsets of those vertices, sparse and quantised to 16 bits. ICT's
names are ARKit's with _L / _R for Left / Right (the character's own left, +X); its separate browInnerUp_L/_R and
cheekPuff_L/_R are summed into ARKit's single browInnerUp and cheekPuff, cheekRaiser_L/_R and PupilDilate_L/_R are
not ARKit shapes and are left out, and ICT has no tongueOut: the file's tongueOut opens the jaw a little (the
tongue itself is a Part's job, see core/Face.cpp). The teeth aren't kept, only how the lower teeth move in each
shape (a rigid transform fitted to ICT's lower teeth), which drives teeth Parts. Units: centimetres, Y up, the
face looking down +Z.

File layout (little endian):
    char[8]  "SFFACE01"
    u32      vertex count V, triangle count T, shape count S
    u32[4]   region ends: face, skin (face + head and neck), mouth socket, eye sockets (= V)
    f32[3V]  neutral positions
    u16[3T]  triangles (ICT's quads split along their first diagonal)
    f32[3]   lower teeth centroid, f32 upper teeth centroid y
    S times: u8 name length, name (ASCII), f32 quantisation step, f32[4] lower jaw rotation (x, y, z, w),
             f32[3] lower jaw translation (x' = R x + t), u32 count N, u16[N] vertex indices, i16[3N] offsets / step
"""
import math
import os
import struct
import sys

ARKIT = sorted([
    "browDownLeft", "browDownRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight", "cheekPuff",
    "cheekSquintLeft", "cheekSquintRight", "eyeBlinkLeft", "eyeBlinkRight", "eyeLookDownLeft", "eyeLookDownRight",
    "eyeLookInLeft", "eyeLookInRight", "eyeLookOutLeft", "eyeLookOutRight", "eyeLookUpLeft", "eyeLookUpRight",
    "eyeSquintLeft", "eyeSquintRight", "eyeWideLeft", "eyeWideRight", "jawForward", "jawLeft", "jawOpen",
    "jawRight", "mouthClose", "mouthDimpleLeft", "mouthDimpleRight", "mouthFrownLeft", "mouthFrownRight",
    "mouthFunnel", "mouthLeft", "mouthLowerDownLeft", "mouthLowerDownRight", "mouthPressLeft", "mouthPressRight",
    "mouthPucker", "mouthRight", "mouthRollLower", "mouthRollUpper", "mouthShrugLower", "mouthShrugUpper",
    "mouthSmileLeft", "mouthSmileRight", "mouthStretchLeft", "mouthStretchRight", "mouthUpperUpLeft",
    "mouthUpperUpRight", "noseSneerLeft", "noseSneerRight", "tongueOut"])
assert len(ARKIT) == 52

FACE_END, SKIN_END, MOUTH_END, EYES_END = 9409, 11248, 13294, 14062
UPPER_TEETH = range(17039, 19247)
LOWER_TEETH = range(19247, 21451)
THRESHOLD = 0.002  # cm: offsets below 0.02 mm are dropped


def load_obj(path, faces=False):
    verts, polys = [], []
    with open(path) as f:
        for line in f:
            if line.startswith("v "):
                a = line.split()
                verts.append((float(a[1]), float(a[2]), float(a[3])))
            elif faces and line.startswith("f "):
                polys.append([int(c.split("/")[0]) - 1 for c in line.split()[1:]])
    return verts, polys


def ict_sources(name):
    """The ICT files whose offsets make up ARKit shape `name`, and a factor for each."""
    if name == "browInnerUp":
        return [("browInnerUp_L", 1.0), ("browInnerUp_R", 1.0)]
    if name == "cheekPuff":
        return [("cheekPuff_L", 1.0), ("cheekPuff_R", 1.0)]
    if name == "tongueOut":
        return [("jawOpen", 0.25)]
    for side, suffix in (("Left", "_L"), ("Right", "_R")):
        if name.endswith(side) and name not in ("jawLeft", "jawRight", "mouthLeft", "mouthRight"):
            return [(name[: -len(side)] + suffix, 1.0)]
    return [(name, 1.0)]


def mat_to_quat(m):
    tr = m[0][0] + m[1][1] + m[2][2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        return ((m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s)
    if m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        return (0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s)
    if m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        return ((m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s)
    s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
    return ((m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s)


def quat_to_mat(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def rigid_fit(src, dst):
    """Rotation (quaternion) and translation taking points src to dst in the least-squares sense: Horn's method
    (the largest eigenvector of the 4x4 matrix N, found by power iteration on N + cI)."""
    n = len(src)
    cs = [sum(p[k] for p in src) / n for k in range(3)]
    cd = [sum(p[k] for p in dst) / n for k in range(3)]
    S = [[0.0] * 3 for _ in range(3)]
    for p, q in zip(src, dst):
        a = [p[k] - cs[k] for k in range(3)]
        b = [q[k] - cd[k] for k in range(3)]
        for i in range(3):
            for j in range(3):
                S[i][j] += a[i] * b[j]
    Sxx, Sxy, Sxz = S[0]
    Syx, Syy, Syz = S[1]
    Szx, Szy, Szz = S[2]
    N = [[Sxx + Syy + Szz, Syz - Szy, Szx - Sxz, Sxy - Syx],
         [Syz - Szy, Sxx - Syy - Szz, Sxy + Syx, Szx + Sxz],
         [Szx - Sxz, Sxy + Syx, -Sxx + Syy - Szz, Syz + Szy],
         [Sxy - Syx, Szx + Sxz, Syz + Szy, -Sxx - Syy + Szz]]
    c = sum(abs(N[i][j]) for i in range(4) for j in range(4)) + 1e-9
    v = [1.0, 0.0, 0.0, 0.0]
    for _ in range(2000):
        w = [sum((N[i][j] + (c if i == j else 0.0)) * v[j] for j in range(4)) for i in range(4)]
        norm = math.sqrt(sum(x * x for x in w))
        w = [x / norm for x in w]
        if sum(abs(a - b) for a, b in zip(w, v)) < 1e-14:
            v = w
            break
        v = w
    q = (v[1], v[2], v[3], v[0])  # (x, y, z, w)
    if q[3] < 0:
        q = tuple(-x for x in q)
    R = quat_to_mat(q)
    t = [cd[i] - sum(R[i][j] * cs[j] for j in range(3)) for i in range(3)]
    return q, t


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    src, out = sys.argv[1], sys.argv[2]
    neutral, polys = load_obj(os.path.join(src, "generic_neutral_mesh.obj"), faces=True)
    V = EYES_END
    tris = []
    for p in polys:
        if max(p) >= V:
            continue
        for k in range(1, len(p) - 1):
            tris.append((p[0], p[k], p[k + 1]))
    lower = [neutral[i] for i in LOWER_TEETH]
    lower_centroid = [sum(p[k] for p in lower) / len(lower) for k in range(3)]
    upper_y = sum(neutral[i][1] for i in UPPER_TEETH) / len(UPPER_TEETH)

    cache = {}

    def expression(name):
        if name not in cache:
            cache[name] = load_obj(os.path.join(src, name + ".obj"))[0]
        return cache[name]

    data = bytearray(b"SFFACE01")
    data += struct.pack("<3I", V, len(tris), len(ARKIT))
    data += struct.pack("<4I", FACE_END, SKIN_END, MOUTH_END, EYES_END)
    for p in neutral[:V]:
        data += struct.pack("<3f", *p)
    for t in tris:
        data += struct.pack("<3H", *t)
    data += struct.pack("<4f", *lower_centroid, upper_y)
    total = 0
    for name in ARKIT:
        offsets = [[0.0, 0.0, 0.0] for _ in range(V)]
        moved = [list(p) for p in lower]
        for file, factor in ict_sources(name):
            e = expression(file)
            for i in range(V):
                for k in range(3):
                    offsets[i][k] += factor * (e[i][k] - neutral[i][k])
            for j, i in enumerate(LOWER_TEETH):
                for k in range(3):
                    moved[j][k] += factor * (e[i][k] - neutral[i][k])
        idx = [i for i in range(V) if math.sqrt(sum(x * x for x in offsets[i])) > THRESHOLD]
        peak = max((abs(x) for i in idx for x in offsets[i]), default=0.0)
        step = peak / 32767.0 if peak > 0 else 1.0
        q, t = rigid_fit(lower, moved)
        R = quat_to_mat(q)
        reach = max(math.dist([sum(R[i][j] * p[j] for j in range(3)) + t[i] for i in range(3)], p) for p in lower)
        if reach < 0.01:  # under 0.1 mm (ICT's blinks nudge a few teeth vertices): the jaw doesn't move
            q, t = (0.0, 0.0, 0.0, 1.0), [0.0, 0.0, 0.0]
        data += struct.pack("<B", len(name)) + name.encode("ascii")
        data += struct.pack("<f4f3fI", step, *q, *t, len(idx))
        data += struct.pack("<%dH" % len(idx), *idx)
        for i in idx:
            data += struct.pack("<3h", *(max(-32767, min(32767, round(x / step))) for x in offsets[i]))
        total += len(idx)
        print("%-20s %6d vertices, peak %.3f cm, jaw %s" % (name, len(idx), peak,
                                                             "moves" if q[3] != 1.0 or any(t) else "-"))
    with open(out, "wb") as f:
        f.write(data)
    print("%s: %d bytes, %d vertices, %d triangles, %d offsets" % (out, len(data), V, len(tris), total))


if __name__ == "__main__":
    main()
