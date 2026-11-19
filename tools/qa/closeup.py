#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Close-up sheet for a brush branch: play a close-up session (tests/artist/closeup_*.sfui) and judge each step.

One session per brush family (planar = cut-only, clay/build, crease, ...). The session saves the mesh before and after each step (cf_NN.obj, same vertex order). For each step this measures,
on the vertices the step moved: how far any vertex ROSE along its pre-step normal (a planar brush must never raise),
and edge SPIKES (a vertex standing out of its ring: umbrella length / mean edge > SPIKE_K and > 3x its ring's median),
compared with the same count before the step. Shots are put next to a baseline run's shots (e.g. the release).

  python3 tools/qa/closeup.py --bin build/sculptforge --out cf-branch [--baseline cf-0.12.7] [--session crisp_flatten]

Exit 0: every step cut only and added no spikes; 1: a bar failed; 2: the session failed to run.
"""
import argparse
import math
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from artist_pass import image_diff, pair_sheet  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
RISE_TOL = 1e-5   # of the bounds' diagonal: what counts as "rose" (float noise is ~1e-7)
SPIKE_K = 0.6     # umbrella length over mean edge length that reads as a spike (a clean crease is ~0.1-0.3)
# Each session declares its steps: "#step PRE POST MODE label" (PRE/POST: the cf_NN.obj it saved; MODE: cut = nothing may
# rise, build = nothing may sink, any = no direction bar, remesh = new vertices within 0.5% of the bounds of the old
# surface; "rose" then counts the far ones and "moved" is the new vertex count). Spikes are checked on every step.


def steps(script):
    out = []
    for line in script.read_text().splitlines():
        if line.startswith("#step "):
            w = line.split(None, 4)
            out.append((w[1], w[2], w[3], w[4] if len(w) > 4 else ""))
    return out


def read_obj(path):
    pts, faces = [], []
    with open(path) as f:
        for line in f:
            if line.startswith("v "):
                pts.append(tuple(map(float, line.split()[1:4])))
            elif line.startswith("f "):
                ids = [int(t.split("/")[0]) - 1 for t in line.split()[1:]]
                for i in range(1, len(ids) - 1):
                    faces.append((ids[0], ids[i], ids[i + 1]))
    return pts, faces


def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def norm(a): return math.sqrt(dot(a, a))


def spikes_all(p, faces):
    """Spike count over a whole mesh (no before: the absolute rule)."""
    ring = {}
    for a, b, c in faces:
        for v, o1, o2 in ((a, b, c), (b, c, a), (c, a, b)):
            r = ring.setdefault(v, set())
            r.add(o1)
            r.add(o2)
    k = {}
    for v, r in ring.items():
        c = [sum(p[o][i] for o in r) / len(r) for i in range(3)]
        e = sum(norm(sub(p[o], p[v])) for o in r) / len(r)
        k[v] = norm(sub(p[v], c)) / e if e > 0 else 0.0
    n = 0
    for v, kv in k.items():
        if kv > SPIKE_K:
            nb = sorted(k[o] for o in ring[v])
            n += kv > 3.0 * nb[len(nb) // 2]
    return n


def judge_remesh(pre, pre_faces, post, post_faces, diag):
    """A remesh: how far the new vertices sit from the old ones (nearest, % of diag) and spikes before -> after."""
    cell = diag / 200.0
    grid = {}
    for q in pre:
        grid.setdefault((int(q[0] // cell), int(q[1] // cell), int(q[2] // cell)), []).append(q)
    far, worst = 0, 0.0
    for q in post[::max(1, len(post) // 50000)]:
        k = (int(q[0] // cell), int(q[1] // cell), int(q[2] // cell))
        best = 3 * cell
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for o in grid.get((k[0] + dx, k[1] + dy, k[2] + dz), ()):
                        best = min(best, math.dist(q, o))
        worst = max(worst, best / diag)
        far += best / diag > 0.005
    return {"moved": len(post), "rose": far, "rise_max": worst, "sank": 0, "sink_max": 0.0,
            "spikes_pre": spikes_all(pre, pre_faces), "spikes_post": spikes_all(post, post_faces), "spike_at": None}


def judge_step(pre, post, faces, diag):
    moved = [v for v in range(len(pre)) if pre[v] != post[v]]
    if not moved:
        return {"moved": 0, "rose": 0, "rise_max": 0.0, "sank": 0, "sink_max": 0.0, "spikes_pre": 0, "spikes_post": 0,
                "spike_at": None}
    ms = set(moved)
    ring, nrm = {}, {}
    for a, b, c in faces:
        if a in ms or b in ms or c in ms:
            n = cross(sub(pre[b], pre[a]), sub(pre[c], pre[a]))
            for v, o1, o2 in ((a, b, c), (b, c, a), (c, a, b)):
                r = ring.setdefault(v, set())
                r.add(o1)
                r.add(o2)
                if v in ms:
                    t = nrm.get(v, (0.0, 0.0, 0.0))
                    nrm[v] = (t[0] + n[0], t[1] + n[1], t[2] + n[2])
    rose, rise_max, sank, sink_max = 0, 0.0, 0, 0.0
    for v in moved:
        n = nrm.get(v)
        if not n or norm(n) == 0.0:
            continue
        h = dot(sub(post[v], pre[v]), n) / norm(n) / diag
        rise_max = max(rise_max, h)
        rose += h > RISE_TOL
        sink_max = max(sink_max, -h)
        sank += -h > RISE_TOL

    def spikes(p):
        k = {}
        for v in ms:
            r = ring.get(v)
            if not r:
                continue
            c = [sum(p[o][i] for o in r) / len(r) for i in range(3)]
            e = sum(norm(sub(p[o], p[v])) for o in r) / len(r)
            k[v] = norm(sub(p[v], c)) / e if e > 0 else 0.0
        out = []
        for v, kv in k.items():
            if kv <= SPIKE_K:
                continue
            nb = sorted(k.get(o, 0.0) for o in ring[v])
            if kv > 3.0 * nb[len(nb) // 2]:
                out.append((kv, v))
        return out

    def folds(p):  # triangles folded over a neighbour (normals across a shared edge more than 120 degrees apart)
        tri = [t for t in faces if t[0] in ms or t[1] in ms or t[2] in ms]
        nrm_t, edge = [], {}
        for i, (a, b, c) in enumerate(tri):
            n = cross(sub(p[b], p[a]), sub(p[c], p[a]))
            ln = norm(n)
            nrm_t.append((n[0] / ln, n[1] / ln, n[2] / ln) if ln > 0 else None)
            for e in ((a, b), (b, c), (c, a)):
                edge.setdefault((min(e), max(e)), []).append(i)
        bad = set()
        for f in edge.values():
            if len(f) == 2 and nrm_t[f[0]] and nrm_t[f[1]] and dot(nrm_t[f[0]], nrm_t[f[1]]) < -0.5:
                bad.update(f)
        return len(bad)

    flips = folds(post) - folds(pre)
    sp_pre, sp_post = spikes(pre), spikes(post)
    worst = max(sp_post)[1] if sp_post else None
    return {"moved": len(moved), "flips": flips, "rose": rose, "rise_max": rise_max, "sank": sank, "sink_max": sink_max, "spikes_pre": len(sp_pre),
            "spikes_post": len(sp_post), "spike_at": post[worst] if worst is not None else None}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--bin", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--baseline", help="an earlier --out folder to put the shots next to")
    ap.add_argument("--label", default="branch")
    ap.add_argument("--baseline-label", default="baseline")
    ap.add_argument("--session", default="crisp_flatten")
    ap.add_argument("--keep-obj", action="store_true")
    a = ap.parse_args()
    out = Path(a.out).resolve()
    shots = out / "shots"
    shots.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, SDL_VIDEO_DRIVER=os.environ.get("SDL_VIDEO_DRIVER", "offscreen"))
    script = ROOT / "tests" / "artist" / f"closeup_{a.session}.sfui"
    p = subprocess.run([str(Path(a.bin).resolve()), "--ui-test", str(script), "--ui-out", str(shots)], cwd=out,
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=1800)
    (out / "session.log").write_text(p.stdout)
    if p.returncode != 0:
        print(f"session failed ({p.returncode}): see {out / 'session.log'}")
        return 2
    rows, ok, cache = [], True, {}

    def load(tag):
        if tag not in cache:
            cache[tag] = read_obj(out / f"cf_{tag}.obj")
        return cache[tag]

    for pre_t, post_t, mode, what in steps(script):
        pre, faces = load(pre_t)
        post, post_faces = load(post_t)
        cache.pop(pre_t, None)
        lo = [min(q[i] for q in pre) for i in range(3)]
        hi = [max(q[i] for q in pre) for i in range(3)]
        if mode == "remesh":  # new vertices: distance off the old surface (bar 0.5% of diag) and spikes
            r = judge_remesh(pre, faces, post, post_faces, math.dist(lo, hi))
            bad = r["rose"] > 0 or r["spikes_post"] > r["spikes_pre"]
        else:
            r = judge_step(pre, post, faces, math.dist(lo, hi))
            bad = ((mode == "cut" and r["rose"] > 0) or (mode == "build" and r["sank"] > 0) or r["spikes_post"] > r["spikes_pre"]
               or r.get("flips", 0) > 0)
        ok &= not bad
        rows.append(f"| {what} | {mode} | {r['moved']} | {r['rose']} / {100 * r['rise_max']:.4f}% | "
                    f"{r['sank']} / {100 * r['sink_max']:.4f}% | {r['spikes_pre']} -> {r['spikes_post']} | {r.get('flips', '-')} | "
                    f"{'FAIL' if bad else 'ok'} |")
        print(rows[-1], flush=True)
    if not a.keep_obj:
        for f in out.glob("cf_*.obj"):
            f.unlink()
    sheet = []
    if a.baseline:
        for s in sorted(shots.glob("*.png")):
            b = Path(a.baseline) / "shots" / s.name
            if b.exists():
                pair_sheet(s, b, out / f"pair-{s.name}", a.label, a.baseline_label)
                sheet.append(f"| {s.stem} | {image_diff(s, b):.1f}% | pair-{s.name} |")
    rep = [f"# Close-up: {a.session} ({a.label})", "",
           f"Rose / sank = vertices that went up / down along their normal by > {RISE_TOL:g} of the bounds, and the most (bar 0 rose",
           f"for cut steps, 0 sank for build steps). Spikes = edge",
           f"vertices standing out of their ring (umbrella/edge > {SPIKE_K}, > 3x ring median), before -> after (bar: no new). Folded = new triangles folded over a neighbour (> 120 deg across an edge; bar 0).", "",
           "| step | bar | moved | rose / most | sank / most | spikes | folded | |", "|---|---|---|---|---|---|---|---|", *rows, "",
           f"**{'PASS' if ok else 'FAIL'}**"]
    if sheet:
        rep += ["", f"Shots vs {a.baseline_label} (pixels apart):", "", "| shot | diff | pair |", "|---|---|---|", *sheet]
    (out / "report.md").write_text("\n".join(rep) + "\n")
    print(f"{'PASS' if ok else 'FAIL'}: {out / 'report.md'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
