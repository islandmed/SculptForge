#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Sums up mcp-detail benchmark runs: a contact sheet per group (front, three-quarter, close-up of each run), the
detail metric (metric.py) of each final mesh, and what each agent did (tool calls, the triangles it detailed at).

  uv run --offline --with numpy --with pillow python report.py RUNS_DIR OUT_DIR before-* after-*

Writes OUT_DIR/<group>.jpg and OUT_DIR/results.md (the run folders' transcripts go to OUT_DIR/runs/<run>.jsonl with
the home folder taken out).
"""
import collections
import glob
import json
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import metric  # noqa: E402

runs_dir, out = sys.argv[1], sys.argv[2]
groups = sys.argv[3:]
os.makedirs(os.path.join(out, "runs"), exist_ok=True)
home = os.path.expanduser("~")
W = 420


def sheet(runs, path):
    rows = []
    for r in runs:
        ims = [Image.open(os.path.join(r, "final-%s.png" % v)).convert("RGB") for v in ("front", "three_quarter", "closeup")]
        ims = [i.resize((W, int(i.height * W / i.width))) for i in ims]
        h = max(i.height for i in ims)
        row = Image.new("RGB", (W * 3, h + 22), (20, 20, 22))
        for k, i in enumerate(ims):
            row.paste(i, (k * W, 22))
        ImageDraw.Draw(row).text((6, 4), os.path.basename(r), fill=(230, 230, 230))
        rows.append(row)
    img = Image.new("RGB", (W * 3, sum(r.height for r in rows)))
    y = 0
    for r in rows:
        img.paste(r, (0, y))
        y += r.height
    img.save(path, quality=82)


def calls(r):
    agent, tools = 0, collections.Counter()
    for line in open(os.path.join(r, "calls.jsonl"), encoding="utf-8"):
        j = json.loads(line)
        if j["by"] in ("agent", "capped"):
            agent += 1
            tools[j["tool"]] += 1
    return agent, tools


lines = ["| run | tool calls | detail calls | verts | hf (x1000) | curv_var | top tools |", "|---|---|---|---|---|---|---|"]
for g in groups:
    runs = sorted(d for d in glob.glob(os.path.join(runs_dir, g)) if os.path.exists(os.path.join(d, "final.obj")))
    if not runs:
        continue
    sheet(runs, os.path.join(out, g.rstrip("-*").rstrip("*") + ".jpg"))
    for r in runs:
        n, tools = calls(r)
        m = metric.measure(os.path.join(r, "final.obj"))
        detail = tools["detail_pass"] + tools["view_closeup"]
        top = ", ".join("%s %d" % kv for kv in tools.most_common(5))
        lines.append("| %s | %d | %d | %d | %.3f | %.0f | %s |" % (os.path.basename(r), n, detail, m["verts"], m["hf"], m["curv_var"], top))
        with open(os.path.join(r, "calls.jsonl"), encoding="utf-8") as f, \
                open(os.path.join(out, "runs", os.path.basename(r) + ".jsonl"), "w", encoding="utf-8") as o:
            o.write(f.read().replace(home, "~"))
with open(os.path.join(out, "results.md"), "w") as f:
    f.write("\n".join(lines) + "\n")
print("\n".join(lines))
