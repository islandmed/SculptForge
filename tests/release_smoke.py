#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Release smoke test for a packaged sculptforge-cli (.github/workflows/release.yml runs it on every installed or
# unpacked package before anything is published; ctest runs it on the build tree): open a model, sculpt one stroke,
# save a project, open that project again and check that it holds the sculpted mesh.
#
#   python3 tests/release_smoke.py --cli path/to/sculptforge-cli [--out DIR]
import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL = os.path.join(HERE, "data", "marble-bust.glb")  # 17456 triangles, about 0.27 x 0.52 x 0.30


def run(cli, script):
    p = subprocess.run([cli, "-c", script], capture_output=True, text=True, timeout=300)
    sys.stdout.write(p.stdout)
    sys.stderr.write(p.stderr)
    if p.returncode != 0:
        sys.exit(f"sculptforge-cli failed ({p.returncode}): {script}")
    return p.stdout


def vertices(path):
    with open(path, encoding="utf-8") as f:
        return [line for line in f if line.startswith("v ")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", required=True)
    ap.add_argument("--out")
    a = ap.parse_args()
    out = a.out or tempfile.mkdtemp(prefix="sf-release-smoke-")
    os.makedirs(out, exist_ok=True)
    before, after, again = (os.path.join(out, n) for n in ("before.obj", "after.obj", "reopened.obj"))
    project = os.path.join(out, "smoke.sfproj")

    # A Clay stroke across the face, drawn in front of it and projected onto the surface (front view).
    run(a.cli, f'open path="{MODEL}"; save_obj path="{before}"; '
               f'stroke brush=Clay points="-0.06,0.30,0.3 0.0,0.33,0.3 0.06,0.30,0.3" strength=1; '
               f'save_project path="{project}"; save_obj path="{after}"')
    info = run(a.cli, f'open path="{project}"; mesh_info; save_obj path="{again}"')

    failures = []
    if not os.path.getsize(project) > 0:
        failures.append("the saved project is empty")
    m = re.search(r"(\d+) triangles", info)
    if not m or int(m.group(1)) != 17456:
        failures.append(f"the reopened project should have 17456 triangles: {info.strip()}")
    vb, va, vr = vertices(before), vertices(after), vertices(again)
    if len(vb) != len(va) or not vb:
        failures.append(f"vertex count changed by a sculpt stroke: {len(vb)} -> {len(va)}")
    moved = sum(1 for x, y in zip(vb, va) if x != y)
    if moved == 0:
        failures.append("the stroke moved no vertex")
    if va != vr:
        failures.append("the reopened project's mesh differs from the one saved")
    for f in failures:
        print("FAILED:", f)
    if failures:
        return 1
    print(f"release smoke: OK ({moved} vertices moved by the stroke, project {os.path.getsize(project)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
