#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""The content lane's file (docs/design/release-lanes.md §1): website/content/v1.json from the same sources the app
compiles in (docs/supporters.txt, website/founding.json, website/testimonials.json). pages.yml runs it, then signs the
exact bytes with the content key (v1.json.sig). The serial is the newest commit time of those sources, so it only
rises on main. The app re-checks every field (src/app/CommunityContent.h); this keeps the file small and valid.

  python3 tools/content/build_content.py [--out website/content/v1.json] [--serial N]
"""
import argparse, json, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SOURCES = ["docs/supporters.txt", "website/founding.json", "website/testimonials.json"]
MAX_BYTES = 64 * 1024  # kContentMaxBytes


def serial():
    out = subprocess.run(["git", "-C", ROOT, "log", "-1", "--format=%ct", "--"] + SOURCES,
                         capture_output=True, text=True).stdout.strip()
    return int(out or 0)


def build(n):
    with open(os.path.join(ROOT, SOURCES[0]), encoding="utf-8") as f:
        supporters = [l.strip() for l in f if l.strip() and not l.lstrip().startswith("#")]
    with open(os.path.join(ROOT, SOURCES[1]), encoding="utf-8") as f:
        founding = json.load(f)
    with open(os.path.join(ROOT, SOURCES[2]), encoding="utf-8") as f:
        testimonials = [{k: t[k] for k in ("quote", "name", "role") if isinstance(t.get(k), str)}
                        for t in json.load(f) if isinstance(t, dict)]
    return {"schema": 1, "serial": n, "supporters": supporters,
            "founding": {k: founding[k] for k in ("limit", "taken", "names") if k in founding},
            "testimonials": testimonials}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "website/content/v1.json"))
    ap.add_argument("--serial", type=int)
    a = ap.parse_args()
    data = json.dumps(build(a.serial if a.serial is not None else serial()), ensure_ascii=False,
                      separators=(",", ":")).encode("utf-8")
    if len(data) > MAX_BYTES:
        sys.exit(f"build_content: {len(data)} bytes, over the app's {MAX_BYTES}")
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "wb") as f:
        f.write(data)
    print(f"build_content: {a.out} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
