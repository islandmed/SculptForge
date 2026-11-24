#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Artist pass: play the artist sessions (tests/artist/*.sfui) in the real app, judge them like a sculptor would.

Each session is a UI test script (strokes with curves, pressure and speed like a person, keys, menus). Its "print"
lines give the numbers (frame times, mesh stats); this script applies the pass bars, checks symmetry on the exported
mesh, and puts each screenshot next to the same shot from an earlier run (a previous release) with the image
difference. Not a release gate: it writes a one-page report.

  python3 tools/qa/artist_pass.py --bin build/sculptforge --out artist-out [--baseline artist-out-0.12.6]
  python3 tools/qa/artist_pass.py --bin ... --sessions blockout        # one session

Exit code: 0 all bars pass, 1 a bar failed, 2 a session failed to run.
"""
import argparse
import math
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SESSIONS = ["blockout", "detail", "paint_export"]

# Slowest single frame allowed while sculpting, by session (ms): blockout works at ~100k-300k triangles,
# detail at ~5M, paint at ~1.3M. Long operations (divide, remesh, open) get their own, looser bar.
STROKE_FRAME_MS = {"blockout": 50.0, "detail": 100.0, "paint_export": 100.0}
OPERATION_FRAME_MS = 1000.0
UNDO_FRAME_MS = 150.0


def run_session(binary, name, out):
    shots = out / name
    shots.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, SDL_VIDEO_DRIVER=os.environ.get("SDL_VIDEO_DRIVER", "offscreen"))
    script = ROOT / "tests" / "artist" / f"{name}.sfui"
    t = time.time()
    p = subprocess.run([str(binary), "--ui-test", str(script), "--ui-out", str(shots)], cwd=out, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=1800)
    (out / f"{name}.log").write_text(p.stdout)
    return p.returncode, p.stdout, time.time() - t


# What a benchmark take may run (Demo::loadTake: input, the camera, waits and the scene's set-up commands).
TAKE_RUNS = {"new_sphere", "new_plane", "new_head", "new_bench_sphere", "new_starter_mesh", "ui_look", "subdivide",
             "subdiv_divide", "subdiv_level", "dyntopo", "set_color", "set_material", "set_shading", "viewport_shading",
             "mask_clear", "mask_rect", "part_select", "part_visibility", "transform", "layer_set_weight", "undo", "redo"}
TAKE_LAG_P95_MS = 50.0
TAKE_SPIKE_MS = 250.0  # a frame this slow outside a stroke is a visible freeze


def step_seconds(words):
    """About how long a step takes on the script clock (takes advance it by the wall clock)."""
    v = words[0]
    nums = [w for w in words[1:] if re.match(r"^-?[0-9.]+$", w)]
    if v == "wait":
        return float(words[1])
    if v in ("stroke", "view_drag"):
        return (float(nums[4]) if len(nums) > 4 else (0.6 if v == "stroke" else 0.4)) + 0.1
    if v == "drag_path":
        sec = [w for w in words if w.startswith("S=")]
        return (float(sec[0][2:]) if sec else 0.6) + 0.1
    if v == "pen_stroke":
        return (float(nums[6]) if len(nums) > 6 else 0.6) + 0.1
    if v in ("hover_window", "hover_model"):
        return float(nums[2]) if len(nums) > 2 else 0.3
    return 0.02


def run_take(binary, name, out):
    """The session as a benchmark take: the same input in real time, frame times and stroke lag in a CSV."""
    src = (ROOT / "tests" / "artist" / f"{name}.sfui").read_text().splitlines()
    kept, starts, clock = [], [], 0.0
    for line in src:
        w = line.split()
        if w and w[0] == "run" and (len(w) < 2 or w[1] not in TAKE_RUNS or "path=" in line):
            kept.append("# (artist_pass: not in a take) " + line)
            continue
        kept.append(line)
        if w and not w[0].startswith("#") and w[0] not in ("print", "shot", "snapshot", "remember") \
                and not w[0].startswith("expect"):  # the take skips these
            starts.append((clock, len(kept), line.strip()))
            clock += step_seconds(w)
    take = out / f"take-{name}.sfui"
    take.write_text("\n".join(kept) + "\n")
    csv = out / f"take-{name}.csv"
    env = dict(os.environ, SDL_VIDEO_DRIVER=os.environ.get("SDL_VIDEO_DRIVER", "offscreen"))
    p = subprocess.run([str(binary), "--benchmark-script", str(take), "--benchmark-out", str(csv), "--benchmark-no-video"],
                       cwd=out, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=1800)
    (out / f"take-{name}.log").write_text(p.stdout)
    if p.returncode != 0 or not csv.exists():
        return [("real-time take ran", f"exit {p.returncode}", "exit 0", False)]
    # The take reports its script clock; stretch the estimated step starts to it (clicks and glides take time too).
    m = re.search(r"script clock ([0-9.]+) s", p.stdout)
    if m and clock > 0:
        k = float(m.group(1)) / clock
        starts = [(t * k, n, text) for t, n, text in starts]
    stroke_ms, lag, spikes = [], [], []
    for row in csv.read_text().splitlines()[1:]:
        f = row.split(",")
        t, ms, stroking = float(f[1]), float(f[2]), f[3] == "1"
        if stroking:
            stroke_ms.append(ms)
            if f[4]:
                lag.append(float(f[4]))
        elif ms > TAKE_SPIKE_MS and t > 1.5:  # the first second is start-up
            near = max((s for s in starts if s[0] <= t - ms / 1000.0 + 0.05), default=starts[0], key=lambda s: s[0])
            spikes.append((t, ms, near))

    def pct(v, q):
        v = sorted(v)
        return v[min(len(v) - 1, int(q * len(v)))] if v else 0.0

    bar = STROKE_FRAME_MS[name]
    rows = [("real time: slowest frame while stroking", f"{max(stroke_ms, default=0):.0f} ms (p95 {pct(stroke_ms, .95):.0f})",
             f"< {bar:.0f} ms", max(stroke_ms, default=0) < bar),
            ("real time: stroke trails the pointer", f"p50 {pct(lag, .5):.0f} ms, p95 {pct(lag, .95):.0f} ms",
             f"p95 < {TAKE_LAG_P95_MS:.0f} ms", pct(lag, .95) < TAKE_LAG_P95_MS)]
    for t, ms, near in spikes:
        i = starts.index(near)
        recent = " ".join(st[2] for st in starts[max(0, i - 3):i + 1])  # a long operation runs over the next waits
        op = any(k in recent for k in ("ctrl+d", "ctrl+n", "remesh", "subdiv", "new_sphere", "wait_idle"))
        rows.append((f"real time: {ms:.0f} ms frame at {t:.1f} s, near line {near[1]} `{near[2]}`", f"{ms:.0f} ms",
                     f"< {OPERATION_FRAME_MS if op else TAKE_SPIKE_MS:.0f} ms",
                     ms < (OPERATION_FRAME_MS if op else TAKE_SPIKE_MS)))
    return rows


def sections(script_path):
    """For each `print` line of the script, in order: (section title, value name). The section is the last
    '# ' comment above it (its first sentence)."""
    title, out, header = "start", [], True
    for line in Path(script_path).read_text().splitlines():
        s = line.strip()
        if header and (s.startswith("#") or not s):
            continue  # the script's own header comment
        header = False
        if s.startswith("# "):
            title = re.split(r"[:.]", s[2:], maxsplit=1)[0].strip()
        elif s.startswith("print "):
            out.append((title, s.split()[1]))
    return out


def printed(log):
    vals = []
    for line in log.splitlines():
        m = re.match(r"^([a-z_]+) = (-?[0-9.]+(?:e[-+]?[0-9]+)?|-?inf|nan)$", line.strip())
        if m:
            vals.append((m.group(1), float(m.group(2))))
    return vals


def obj_mirror_error(path):
    """Largest distance from a vertex's mirror (x -> -x) to the nearest vertex, as % of the bounds' diagonal."""
    pts = []
    with open(path) as f:
        for line in f:
            if line.startswith("v "):
                x, y, z = map(float, line.split()[1:4])
                pts.append((x, y, z))
    if not pts:
        return None
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    diag = math.dist(lo, hi)
    cell = diag / 150.0
    grid = {}
    for p in pts:
        grid.setdefault(tuple(int(math.floor(c / cell)) for c in p), []).append(p)
    worst, total = 0.0, 0.0
    step = max(1, len(pts) // 40000)  # a sample is plenty for the bar
    n = 0
    for p in pts[::step]:
        q = (-p[0], p[1], p[2])
        k = tuple(int(math.floor(c / cell)) for c in q)
        best = None
        for r in (1, 3):
            for dx in range(-r, r + 1):
                for dy in range(-r, r + 1):
                    for dz in range(-r, r + 1):
                        for o in grid.get((k[0] + dx, k[1] + dy, k[2] + dz), ()):
                            d = math.dist(q, o)
                            best = d if best is None or d < best else best
            if best is not None:
                break
        best = best if best is not None else 3 * cell
        worst, total, n = max(worst, best), total + best, n + 1
    return 100.0 * worst / diag, 100.0 * total / n / diag, len(pts)


def image_diff(a, b):
    from PIL import Image, ImageChops
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if ia.size != ib.size:
        ib = ib.resize(ia.size)
    h = ImageChops.difference(ia, ib).convert("L").histogram()
    return 100.0 * sum(h[16:]) / sum(h)


def pair_sheet(a, b, dest, label_a, label_b):
    from PIL import Image, ImageDraw
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB").resize(Image.open(a).size)
    w, h = ia.size
    s = Image.new("RGB", (w * 2, h + 28), (20, 20, 20))
    s.paste(ib, (0, 28))
    s.paste(ia, (w, 28))
    d = ImageDraw.Draw(s)
    d.text((8, 8), label_b, fill=(230, 230, 230))
    d.text((w + 8, 8), label_a, fill=(230, 230, 230))
    s.save(dest)


def judge(name, prints, vals, rc):
    """Rows: (check, value, bar, ok)."""
    rows = [("session ran to the end (all its expectations held)", f"exit {rc}", "exit 0", rc == 0)]
    by = {}
    for (sec, key), (k2, v) in zip(prints, vals):
        if key != k2:  # the log and the script disagree (a failed run stops early)
            break
        by.setdefault((sec, key), []).append(v)
    # Frame times from the UI test run count only for operations (divide, remesh, undo, redo): its strokes are
    # paced by the script, not the wall clock, so their frames come from the real-time take instead (run_take).
    for (sec, key), vs in by.items():
        if key != "longest_frame_ms":
            continue
        if sec.startswith("Undo"):
            for lab, v in list(zip(["stroke", "undo", "redo"], vs))[1:]:
                rows.append((f"{lab} of a big stroke, slowest frame", f"{v:.0f} ms", f"< {UNDO_FRAME_MS:.0f} ms",
                             v < UNDO_FRAME_MS))
        elif sec.startswith("Remesh"):
            if len(vs) > 1:
                rows.append(("remesh, slowest frame", f"{vs[1]:.0f} ms", f"< {OPERATION_FRAME_MS:.0f} ms",
                             vs[1] < OPERATION_FRAME_MS))
            for lab, v in zip(["undo", "redo"], vs[3:]):
                rows.append((f"{lab} after remesh, slowest frame", f"{v:.0f} ms", f"< {UNDO_FRAME_MS:.0f} ms",
                             v < UNDO_FRAME_MS))
        elif name == "detail" and sec == "start" and vs:
            rows.append(("divide x3 to 5.2M, slowest frame", f"{vs[0]:.0f} ms", f"< {OPERATION_FRAME_MS:.0f} ms",
                         vs[0] < OPERATION_FRAME_MS))
        elif sec.startswith("Save") and vs:
            rows.append(("reopen the project, slowest frame", f"{vs[0]:.0f} ms", f"< {OPERATION_FRAME_MS:.0f} ms",
                         vs[0] < OPERATION_FRAME_MS))

    def first(sec_prefix, key, i=0):
        for (sec, k), vs in by.items():
            if sec.startswith(sec_prefix) and k == key and len(vs) > i:
                return vs[i]
        return None

    if name == "blockout":
        v0, v1 = first("Remesh", "solid_volume", 0), first("Remesh", "solid_volume", 1)
        if v0 and v1:
            ch = 100 * abs(v1 - v0) / v0
            rows.append(("Remesh keeps the volume", f"{ch:.2f}%", "< 5%", ch < 5))
    if name == "detail":
        v0, v1 = first("Smooth", "solid_volume", 0), first("Smooth", "solid_volume", 1)
        if v0 and v1:
            ch = 100 * abs(v1 - v0) / v0
            rows.append(("Smooth keeps the volume", f"{ch:.3f}%", "< 1%", ch < 1))
        a, b = first("Pen pressure", "moved_max", 0), first("Pen pressure", "moved_max", 1)
        if a and b:
            rows.append(("Pen pressure: heavy / light stroke depth", f"x{b / a:.1f}", "> x2", b / a > 2))
        lost = first("Pen pressure", "pen_lost_samples")
        if lost is not None:
            rows.append(("Pen pressure: lost samples", f"{lost:.0f}", "0", lost == 0))
        errs = by.get(("Speed", "dab_error_px"), [])
        if errs:
            rows.append(("Fast flicks + long pull: dab furthest from the pointer", f"{max(errs):.2f} px", "< 2 px",
                         max(errs) < 2))
        dabs = by.get(("Speed", "last_stroke_dabs"), [])
        if dabs:
            rows.append(("Fast flick (0.12 s, ~3 brush widths): dabs laid", f"{dabs[0]:.0f}", ">= 4 (no gaps)",
                         dabs[0] >= 4))
        mm = first("Mask", "moved_masked")
        if mm is not None:
            rows.append(("Mask holds: fully masked vertices moved", f"{mm:.0f}", "0", mm == 0))
    if name == "paint_export":
        s0 = [first("Colour", f"size_{c}") for c in "xyz"]
        s1 = [first("Export", f"size_{c}") for c in "xyz"]
        if all(s0) and all(s1):
            err = max(abs(a - b) / a for a, b in zip(s0, s1)) * 100
            rows.append(("GLB round trip keeps the size", f"{err:.3f}%", "< 0.1%", err < 0.1))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--bin", required=True, help="the sculptforge executable")
    ap.add_argument("--out", required=True, help="folder for shots, logs and report.md")
    ap.add_argument("--baseline", help="an earlier --out folder (e.g. the previous release) to compare shots with")
    ap.add_argument("--label", default="this build")
    ap.add_argument("--baseline-label", default="baseline")
    ap.add_argument("--sessions", default=",".join(SESSIONS))
    ap.add_argument("--no-takes", action="store_true", help="skip the real-time takes (checks and shots only)")
    a = ap.parse_args()
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    base = Path(a.baseline).resolve() if a.baseline else None
    lines = [f"# Artist pass: {a.label}", ""]
    worst = 0
    for name in a.sessions.split(","):
        rc, log, secs = run_session(Path(a.bin).resolve(), name, out)
        if rc != 0 and "PASS" not in log and not printed(log):
            worst = 2
        rows = judge(name, sections(ROOT / "tests" / "artist" / f"{name}.sfui"), printed(log), rc)
        if not a.no_takes:
            rows += run_take(Path(a.bin).resolve(), name, out)
        if name == "blockout" and (out / "artist_blockout.obj").exists():
            r = obj_mirror_error(out / "artist_blockout.obj")
            if r:
                rows.append(("Symmetry after dyntopo + remesh: worst / mean mirror gap", f"{r[0]:.2f}% / {r[1]:.3f}%",
                             "worst < 0.5% of the diagonal", r[0] < 0.5))
        fails = [r for r in rows if not r[3]]
        worst = max(worst, 1 if fails else 0)
        lines += [f"## {name} ({secs:.0f} s, {'PASS' if not fails else f'{len(fails)} over the bar'})", "",
                  "| check | value | bar | |", "|---|---|---|---|"]
        lines += [f"| {c} | {v} | {b} | {'ok' if ok else 'OVER'} |" for c, v, b, ok in rows]
        fail_line = [l for l in log.splitlines() if l.startswith("FAIL")]
        if fail_line:
            lines += ["", f"Stopped at: `{fail_line[0]}`"]
        lines.append("")
        for shot in sorted((out / name).glob("*.png")):
            if shot.name.startswith("FAIL"):
                lines.append(f"![{shot.stem}]({name}/{shot.name})")
                continue
            old = base / name / shot.name if base else None
            if old and old.exists():
                d = image_diff(shot, old)
                sheet = out / name / f"vs-{shot.stem}.jpg"
                pair_sheet(shot, old, sheet, a.label, a.baseline_label)
                lines.append(f"- {shot.stem}: {d:.2f}% of pixels differ from {a.baseline_label} "
                             f"([side by side]({name}/{sheet.name}))")
            else:
                lines.append(f"- {shot.stem}: [shot]({name}/{shot.name})")
        lines.append("")
    (out / "report.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return worst


if __name__ == "__main__":
    sys.exit(main())
