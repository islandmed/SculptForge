#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Geometric detail of a sculpt (an OBJ, every object in it), so before/after isn't judged by eye alone.

  uv run --offline --with numpy python metric.py final.obj [more.obj ...]

Per file, on the whole surface, area-weighted, sizes relative to the bounding-box diagonal (scale-free):
  verts        vertex count
  edge         median edge length / diagonal (x1000): the finest feature the mesh can hold is about 2 edges
  hf           high-frequency energy: RMS distance (along the normal) between the surface and a Taubin-smoothed copy
               (20 passes, no shrinking), x1000. Blockout and smooth forms score near 0; pores, wrinkles, creases high.
  curv_var     variance of the signed mean curvature (cotangent Laplacian) x diagonal: crisp creases and busy surfaces
               score high, smooth ones low.
"""
import sys

import numpy as np


def load_obj(path):
    verts, faces = [], []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("v "):
                verts.append([float(x) for x in line.split()[1:4]])
            elif line.startswith("f "):
                idx = [int(t.split("/")[0]) for t in line.split()[1:]]
                idx = [i - 1 if i > 0 else len(verts) + i for i in idx]
                for k in range(1, len(idx) - 1):
                    faces.append([idx[0], idx[k], idx[k + 1]])
    return np.asarray(verts, dtype=np.float64), np.asarray(faces, dtype=np.int64)


def weld(v, f, diag):
    # Seams (UV / face-group splits) duplicate positions: weld them so the Laplacian sees one surface.
    key = np.round(v / (diag * 1e-6)).astype(np.int64)
    _, first, inverse = np.unique(key, axis=0, return_index=True, return_inverse=True)
    return v[first], inverse.reshape(-1)[f]


def vertex_normals_areas(v, f):
    fn = np.cross(v[f[:, 1]] - v[f[:, 0]], v[f[:, 2]] - v[f[:, 0]])
    area = 0.5 * np.linalg.norm(fn, axis=1)
    n = np.zeros_like(v)
    a = np.zeros(len(v))
    for k in range(3):
        np.add.at(n, f[:, k], fn)
        np.add.at(a, f[:, k], area / 3.0)
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-30)
    return n, a


def umbrella(v, f):
    e = np.concatenate([f[:, [0, 1]], f[:, [1, 2]], f[:, [2, 0]]])
    e = np.unique(np.sort(e, axis=1), axis=0)
    s = np.zeros_like(v)
    deg = np.zeros(len(v))
    np.add.at(s, e[:, 0], v[e[:, 1]])
    np.add.at(s, e[:, 1], v[e[:, 0]])
    np.add.at(deg, e[:, 0], 1)
    np.add.at(deg, e[:, 1], 1)
    return e, s, deg


def taubin(v, f, passes=20, lam=0.5, mu=-0.53):
    e, _, deg = umbrella(v, f)
    deg = np.maximum(deg, 1)[:, None]
    p = v.copy()
    for _ in range(passes):
        for w in (lam, mu):
            s = np.zeros_like(p)
            np.add.at(s, e[:, 0], p[e[:, 1]])
            np.add.at(s, e[:, 1], p[e[:, 0]])
            p = p + w * (s / deg - p)
    return p, e


def mean_curvature(v, f, n, a):
    lap = np.zeros_like(v)
    for i, j, k in ((0, 1, 2), (1, 2, 0), (2, 0, 1)):
        # The cotangent of the angle at k weights edge i-j.
        u, w = v[f[:, i]] - v[f[:, k]], v[f[:, j]] - v[f[:, k]]
        cot = np.einsum("ij,ij->i", u, w) / np.maximum(np.linalg.norm(np.cross(u, w), axis=1), 1e-30)
        cot = np.clip(cot, -50, 50)[:, None] * 0.5
        d = v[f[:, j]] - v[f[:, i]]
        np.add.at(lap, f[:, i], cot * d)
        np.add.at(lap, f[:, j], -cot * d)
    return 0.5 * np.einsum("ij,ij->i", lap, n) / np.maximum(a, 1e-30)


def measure(path):
    v, f = load_obj(path)
    diag = float(np.linalg.norm(v.max(0) - v.min(0)))
    v, f = weld(v, f, diag)
    n, a = vertex_normals_areas(v, f)
    smooth, e = taubin(v, f)
    off = np.einsum("ij,ij->i", v - smooth, n)
    w = a / a.sum()
    hf = float(np.sqrt(np.sum(w * off * off))) / diag
    h = mean_curvature(v, f, n, a) * diag
    lo, hi = np.percentile(h, [0.5, 99.5])  # (a few degenerate triangles shouldn't decide it)
    h = np.clip(h, lo, hi)
    mean = np.sum(w * h)
    curv_var = float(np.sum(w * (h - mean) ** 2))
    edge = float(np.median(np.linalg.norm(v[e[:, 0]] - v[e[:, 1]], axis=1))) / diag
    return {"verts": len(v), "edge": round(edge * 1000, 3), "hf": round(hf * 1000, 3), "curv_var": round(curv_var, 1)}


if __name__ == "__main__":
    for p in sys.argv[1:]:
        print(p, measure(p))
