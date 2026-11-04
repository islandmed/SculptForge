#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks proof mode results (sculptforge --benchmark) and adds them to the website's list.

    submit.py check result.json [...]          validate and print a summary
    submit.py add result.json --name NAME [--machine TEXT] [--video URL] [--results website/benchmarks/results.json]
    submit.py selftest                          check the validator against a made-up result

The format is described in docs/benchmark.md (schema "sculptforge-benchmark/1").
"""
import argparse
import json
import pathlib
import sys

SCHEMA = "sculptforge-benchmark/1"
ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_RESULTS = ROOT / "website" / "benchmarks" / "results.json"
PCT = ("p50", "p95", "p99", "max", "mean")


def _num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool) and v == v and v >= 0


def validate(doc):
    """Returns a list of problems (empty: valid)."""
    errs = []

    def need(obj, key, kind, where):
        if not isinstance(obj, dict) or key not in obj:
            errs.append(f"{where}: missing '{key}'")
            return None
        v = obj[key]
        ok = {"str": isinstance(v, str), "num": _num(v), "obj": isinstance(v, dict), "list": isinstance(v, list),
              "bool": isinstance(v, bool)}[kind]
        if not ok:
            errs.append(f"{where}.{key}: expected {kind}, got {type(v).__name__}")
            return None
        return v

    def pct(obj, key, where):
        p = need(obj, key, "obj", where)
        if p is None:
            return
        for k in PCT:
            if not _num(p.get(k)):
                errs.append(f"{where}.{key}.{k}: expected a number >= 0")
        if all(_num(p.get(k)) for k in PCT) and not (p["p50"] <= p["p95"] <= p["p99"] <= p["max"]):
            errs.append(f"{where}.{key}: percentiles out of order")

    if not isinstance(doc, dict):
        return ["the file is not a JSON object"]
    if doc.get("schema") != SCHEMA:
        errs.append(f"schema: expected '{SCHEMA}', got {doc.get('schema')!r}")
    app = need(doc, "sculptforge", "obj", "root")
    if app:
        need(app, "version", "str", "sculptforge")
        need(app, "commit", "str", "sculptforge")
    need(doc, "date", "str", "root")
    hw = need(doc, "hardware", "obj", "root")
    if hw:
        for k in ("cpu", "os", "gpu"):
            need(hw, k, "str", "hardware")
        need(hw, "logical_cores", "num", "hardware")
        need(hw, "ram_gb", "num", "hardware")
    st = need(doc, "settings", "obj", "root")
    if st:
        need(st, "vsync", "bool", "settings")
        need(st, "pointer_hz", "num", "settings")
    runs = need(doc, "runs", "list", "root") or []
    if not runs:
        errs.append("runs: empty")
    for k in doc.get("skipped") or []:  # a scene left out for lack of free memory: not a complete result
        errs.append(f"skipped: {k.get('message', k.get('label', '?'))}")
    for i, run in enumerate(runs):
        w = f"runs[{i}]"
        need(run, "label", "str", w)
        need(run, "triangles", "num", w)
        need(run, "vertices", "num", w)
        phases = need(run, "phases", "list", w) or []
        if not phases:
            errs.append(f"{w}.phases: empty")
        for j, ph in enumerate(phases):
            pw = f"{w}.phases[{j}]"
            need(ph, "name", "str", pw)
            need(ph, "seconds", "num", pw)
            need(ph, "frames", "num", pw)
            need(ph, "fps", "num", pw)
            pct(ph, "frame_ms", pw)
            if isinstance(ph, dict) and "worst_stall_ms" in ph and not _num(ph["worst_stall_ms"]):
                errs.append(f"{pw}.worst_stall_ms: expected a number >= 0")
            if isinstance(ph, dict) and "brush_radius_px" in ph and ph.get("brush") != "Move":
                pct(ph, "lag_ms", pw)
                pct(ph, "lag_px", pw)
    return errs


def summary(doc):
    hw = doc.get("hardware", {})
    lines = [f"SculptForge {doc['sculptforge']['version']} ({doc['sculptforge']['commit']}), {doc['date']}",
             f"{hw.get('cpu')} | {hw.get('gpu')} | {hw.get('ram_gb')} GB | {hw.get('os')}"
             f" | vsync {'on' if doc['settings']['vsync'] else 'off'}"]
    for run in doc["runs"]:
        for ph in run["phases"]:
            f = ph["frame_ms"]
            s = f"  {run['label']:>6} {ph['name']:<20} {ph['fps']:6.1f} fps  frame p50 {f['p50']:5.1f}  p95 {f['p95']:5.1f}  p99 {f['p99']:5.1f} ms"
            if "worst_stall_ms" in ph:
                s += f"  stall {ph['worst_stall_ms']:.0f} ms"
            if "lag_ms" in ph:
                s += f"  trails {ph['lag_ms']['p50']:.0f} ms / {ph['lag_px']['p50']:.0f} px (p95 {ph['lag_ms']['p95']:.0f} ms)"
            lines.append(s)
    return "\n".join(lines)


def compact(doc, name, video, machine=None):
    """The entry the website lists: everything but the long settings text."""
    settings = doc["settings"]
    entry = {
        "name": name,
        "date": doc["date"],
        "sculptforge": doc["sculptforge"],
        "hardware": {k: doc["hardware"][k] for k in ("cpu", "gpu", "ram_gb", "logical_cores", "os", "framebuffer")
                     if k in doc["hardware"]},
        "vsync": settings["vsync"],
        "runs": [],
    }
    if machine:
        entry["machine"] = machine
    for k in ("scene", "stroke_set"):
        if k in settings:
            entry[k] = settings[k]
    if video:
        entry["video"] = video
    for run in doc["runs"]:
        r = {"label": run["label"], "triangles": run["triangles"], "phases": []}
        for k in ("peak_ram_mb", "gpu_buffers_mb", "vram_used_mb", "worst_stall_ms"):
            if k in run:
                r[k] = run[k]
        for ph in run["phases"]:
            p = {"name": ph["name"], "fps": ph["fps"], "frame_ms": ph["frame_ms"]}
            for k in ("brush", "worst_stall_ms"):
                if k in ph:
                    p[k] = ph[k]
            if "brush_radius_px" in ph:
                p["brush_radius_px"] = ph["brush_radius_px"]
            if "lag_ms" in ph:
                p.update(lag_ms=ph["lag_ms"], lag_px=ph["lag_px"])
            r["phases"].append(p)
        entry["runs"].append(r)
    return entry


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def cmd_check(args):
    bad = 0
    for p in args.files:
        try:
            doc = load(p)
        except (OSError, ValueError) as e:
            print(f"{p}: {e}")
            bad += 1
            continue
        errs = validate(doc)
        if errs:
            bad += 1
            print(f"{p}: {len(errs)} problem(s)")
            for e in errs:
                print("  " + e)
        else:
            print(f"{p}: valid\n{summary(doc)}")
    return 1 if bad else 0


def cmd_add(args):
    doc = load(args.file)
    errs = validate(doc)
    if errs:
        print("Not added:\n  " + "\n  ".join(errs))
        return 1
    results_path = pathlib.Path(args.results)
    data = load(results_path) if results_path.exists() else {"schema": "sculptforge-benchmark-list/1", "results": []}
    entry = compact(doc, args.name, args.video, args.machine)
    key = (entry["name"], entry["date"])
    if any((e.get("name"), e.get("date")) == key for e in data["results"]):
        print("Already listed")
        return 0
    data["results"].append(entry)
    results_path.parent.mkdir(parents=True, exist_ok=True)
    results_path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print(f"Added to {results_path} ({len(data['results'])} results)")
    return 0


def cmd_selftest(_args):
    pct = {"p50": 10, "p95": 20, "p99": 30, "max": 40, "mean": 12}
    good = {"schema": SCHEMA, "sculptforge": {"version": "0.9.7", "commit": "abc1234"}, "date": "2026-11-01T00:00:00Z",
            "hardware": {"cpu": "x", "os": "y", "gpu": "z", "logical_cores": 8, "ram_gb": 16},
            "settings": {"vsync": False, "pointer_hz": 240},
            "runs": [{"label": "5M", "triangles": 5e6, "vertices": 2.5e6,
                      "phases": [{"name": "stroke", "brush_radius_px": 60, "seconds": 6, "frames": 300, "fps": 50,
                                  "frame_ms": pct, "lag_ms": pct, "lag_px": pct}]}]}
    assert validate(good) == [], validate(good)
    bad = json.loads(json.dumps(good))
    bad["runs"][0]["phases"][0]["lag_ms"]["p95"] = 99  # above p99
    del bad["hardware"]["gpu"]
    assert len(validate(bad)) == 2, validate(bad)
    assert compact(good, "me", None)["runs"][0]["phases"][0]["lag_px"] == pct
    std = json.loads(json.dumps(good))
    std["settings"].update(scene="standard-15m", stroke_set="standard/1")
    std["runs"][0].update(label="15M", peak_ram_mb=4000, gpu_buffers_mb=900, worst_stall_ms=45.5)
    std["runs"][0]["phases"][0].update(name="clay small", brush="Clay", worst_stall_ms=45.5)
    assert validate(std) == [], validate(std)
    e = compact(std, "me", None, "a laptop")
    assert e["scene"] == "standard-15m" and e["machine"] == "a laptop" and e["runs"][0]["worst_stall_ms"] == 45.5
    assert e["runs"][0]["phases"][0]["brush"] == "Clay"
    print("selftest ok")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("files", nargs="+")
    a = sub.add_parser("add")
    a.add_argument("file")
    a.add_argument("--name", required=True)
    a.add_argument("--video", default=None)
    a.add_argument("--machine", default=None, help="free text: the computer (model, cooling, power mode...)")
    a.add_argument("--results", default=str(DEFAULT_RESULTS))
    sub.add_parser("selftest")
    args = ap.parse_args()
    return {"check": cmd_check, "add": cmd_add, "selftest": cmd_selftest}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
