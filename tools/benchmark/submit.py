#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks proof mode results (sculptforge --benchmark) and adds them to the website's list.

    submit.py check result.json [...]          validate and print a summary
    submit.py add result.json --name NAME [--machine TEXT] [--video URL] [--results website/benchmarks/results.json]
    submit.py issue ISSUE_BODY.md|-             a "Benchmark pack result" issue (the form's text, `gh issue view N --json body -q .body`):
                                                check it, print what a moderator looks at, add it (--dry-run: only print)
    submit.py selftest                          check the validator against a made-up result

The format is described in docs/benchmark.md (schema "sculptforge-benchmark/1"; a scene pack run adds settings.pack and
per-run tiers).
"""
import argparse
import json
import pathlib
import re
import sys

SCHEMA = "sculptforge-benchmark/1"
ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_RESULTS = ROOT / "website" / "benchmarks" / "results.json"
PCT = ("p50", "p95", "p99", "max", "mean")
PACK_HASHES = ROOT / "bench" / "pack" / "hashes.json"  # the published scene packs: {"<version>": "<sha256>"}
TIERS = ("floor", "standard", "high")
HW_TIERS = ("Floor", "Standard", "High", "Extreme")


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
    pack = st.get("pack") if isinstance(st, dict) else None
    for k in doc.get("skipped") or []:  # a scene left out for lack of free memory: not a complete result
        # (a scene pack leaves out the scenes the computer has no memory for: that is a result. One that wouldn't build isn't.)
        if pack is not None and isinstance(k, dict) and _num(k.get("need_mb")) and k["need_mb"] > 0:
            continue
        errs.append(f"skipped: {k.get('message', k.get('label', '?'))}")
    if pack is not None:
        errs.extend(_validate_pack(doc, pack, runs))
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


def known_pack_hashes():
    try:
        return json.loads(PACK_HASHES.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def _validate_pack(doc, pack, runs):
    """The extra rules of a scene pack result (sculptforge --benchmark-pack)."""
    errs = []
    if not isinstance(pack, dict) or not all(isinstance(pack.get(k), t) for k, t in (("name", str), ("version", int), ("sha256", str))):
        return ["settings.pack: needs name, version and sha256"]
    want = known_pack_hashes().get(str(pack["version"]))
    if want is None:
        errs.append(f"settings.pack: no published pack has version {pack['version']}")
    elif pack["sha256"] != want:
        errs.append("settings.pack.sha256: not the published pack (an altered pack's numbers can't be compared)")
    hw = doc.get("hardware") or {}
    for k in ("gpu_name", "driver"):
        if not isinstance(hw.get(k), str) or not hw[k]:
            errs.append(f"hardware.{k}: missing (an older build? use the latest release)")
    if hw.get("hw_tier") not in HW_TIERS:
        errs.append(f"hardware.hw_tier: expected one of {', '.join(HW_TIERS)}")
    for i, run in enumerate(runs):
        if run.get("tier") not in TIERS:
            errs.append(f"runs[{i}].tier: expected one of {', '.join(TIERS)}")
        if not isinstance(run.get("worst_stall_ms"), (int, float)):
            errs.append(f"runs[{i}].worst_stall_ms: missing")
    return errs


def plausibility(doc):
    """Things a moderator should look at (not errors): numbers a real run doesn't produce."""
    notes = []
    for run in doc.get("runs", []):
        for ph in run.get("phases", []):
            f, w = ph.get("frame_ms", {}), f"{run.get('label')} / {ph.get('name')}"
            if _num(f.get("p50")) and f["p50"] < 0.2:
                notes.append(f"{w}: frame p50 {f['p50']} ms (faster than a display loop can run)")
            if _num(ph.get("fps")) and _num(f.get("mean")) and f["mean"] > 0 and abs(1000.0 / f["mean"] - ph["fps"]) > 0.1 * ph["fps"] + 1:
                notes.append(f"{w}: fps {ph['fps']} does not match the mean frame time {f['mean']} ms")
            if _num(ph.get("seconds")) and _num(ph.get("frames")) and ph["seconds"] > 0 and abs(ph["frames"] / ph["seconds"] - ph.get("fps", 0)) > 1:
                notes.append(f"{w}: fps does not match frames / seconds")
    hw = doc.get("hardware", {})
    if isinstance(hw.get("ram_gb"), (int, float)) and hw["ram_gb"] > 4096:
        notes.append(f"RAM {hw['ram_gb']} GB")
    if "hw_simulate" in doc.get("settings", {}):
        notes.append(f"simulated hardware tier ({doc['settings']['hw_simulate']}): list it as such, not as the computer's own")
    return notes


def clean_text(value, what, limit=80):
    """What a submitter typed (name, machine): one line, short, no links or addresses. Returns (text, problem or None)."""
    v = " ".join(str(value or "").split())
    if len(v) > limit:
        return v[:limit], f"{what} is longer than {limit} characters"
    if re.search(r"https?://|www\.|@|<|>|\[|\]\(", v):
        return "", f"{what} has a link, an address or markup in it"
    return v, None


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
        "hardware": {k: doc["hardware"][k] for k in ("cpu", "gpu", "gpu_name", "driver", "hw_tier", "ram_gb", "logical_cores", "os",
                                                      "framebuffer") if k in doc["hardware"]},
        "vsync": settings["vsync"],
        "runs": [],
    }
    if machine:
        entry["machine"] = machine
    for k in ("scene", "stroke_set", "hw_simulate"):
        if k in settings:
            entry[k] = settings[k]
    if "pack" in settings:  # a scene pack run: which pack (its hash was checked against the published one)
        entry["pack"] = {"name": settings["pack"]["name"], "version": settings["pack"]["version"]}
    if video:
        entry["video"] = video
    for run in doc["runs"]:
        r = {"label": run["label"], "triangles": run["triangles"], "phases": []}
        if "tier" in run:
            r["tier"] = run["tier"]
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


def add_entry(doc, name, video, machine, results, extra=None):
    """Appends the compact entry to the results list. Returns (exit code, message)."""
    results_path = pathlib.Path(results)
    data = load(results_path) if results_path.exists() else {"schema": "sculptforge-benchmark-list/1", "results": []}
    entry = compact(doc, name, video, machine)
    entry.update(extra or {})
    key = (entry["name"], entry["date"])
    if any((e.get("name"), e.get("date")) == key for e in data["results"]):
        return 0, "Already listed"
    data["results"].append(entry)
    results_path.parent.mkdir(parents=True, exist_ok=True)
    results_path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return 0, f"Added to {results_path} ({len(data['results'])} results)"


def cmd_add(args):
    doc = load(args.file)
    errs = validate(doc)
    if errs:
        print("Not added:\n  " + "\n  ".join(errs))
        return 1
    code, msg = add_entry(doc, args.name, args.video, args.machine, args.results)
    print(msg)
    return code


def form_sections(body):
    """The sections of a GitHub issue form's body: '### Label' lines, each followed by its value."""
    out, label, lines = {}, None, []
    for line in body.replace("\r\n", "\n").split("\n"):
        m = re.match(r"^###\s+(.*?)\s*$", line)
        if m:
            if label is not None:
                out[label] = "\n".join(lines).strip()
            label, lines = m.group(1), []
        else:
            lines.append(line)
    if label is not None:
        out[label] = "\n".join(lines).strip()
    return out


def json_of(text):
    """The JSON in a form's textarea: inside a ```json fence (render: json), or bare."""
    m = re.search(r"```(?:json)?\s*\n(.*?)\n```", text, re.S)
    return json.loads(m.group(1) if m else text)


def cmd_issue(args):
    body = sys.stdin.read() if args.body == "-" else pathlib.Path(args.body).read_text(encoding="utf-8")
    sec = form_sections(body)
    problems = []
    try:
        doc = json_of(sec.get("Result JSON", ""))
    except ValueError as e:
        print(f"Result JSON: not valid JSON ({e})")
        return 1
    problems += validate(doc)
    name, bad = clean_text(sec.get("Name to show") if sec.get("Name to show", "_No response_") != "_No response_" else "", "name", 40)
    if bad:
        problems.append(bad)
    machine, bad = clean_text(sec.get("Machine") if sec.get("Machine") != "_No response_" else "", "machine")
    if bad:
        problems.append(bad)
    if "- [x] I'm happy for this result" not in sec.get("Listing", "").replace("[X]", "[x]"):
        problems.append("the listing box is not ticked")
    if problems:
        print("Not added:\n  " + "\n  ".join(problems))
        return 1
    print(summary(doc))
    notes = plausibility(doc)
    print("\nFor the moderator:" + ("" if notes else " nothing unusual"))
    for n in notes:
        print("  - " + n)
    print(f"  name shown: {name or 'anonymous'}; machine: {machine or '(none)'}")
    if args.dry_run:
        return 0
    code, msg = add_entry(doc, name or "anonymous", None, machine or None, args.results,
                          {"source": "community", **({"issue": args.issue} if args.issue else {})})
    print(msg)
    return code


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
    # A scene pack result: the published hash, a tier per run, the split hardware fields; a scene left out for memory is fine,
    # one that wouldn't build is not.
    pk = json.loads(json.dumps(std))
    pk["hardware"].update(gpu_name="GPU", driver="OpenGL 4.6 Mesa 26.1", hw_tier="Floor")
    pk["settings"].update(scene="pack-1", stroke_set="pack/1", pack={"name": "SculptForge bench pack", "version": 1,
                                                                    "sha256": known_pack_hashes()["1"]})
    pk["runs"][0].update(label="creature-floor", tier="floor", worst_stall_ms=30)
    pk["runs"][0]["phases"][0]["frame_ms"] = dict(pct, mean=20)  # (50 fps: what the frames and seconds say)
    assert validate(pk) == [], validate(pk)
    assert compact(pk, "me", None)["pack"] == {"name": "SculptForge bench pack", "version": 1}
    assert compact(pk, "me", None)["runs"][0]["tier"] == "floor" and compact(pk, "me", None)["hardware"]["hw_tier"] == "Floor"
    bad = json.loads(json.dumps(pk))
    bad["settings"]["pack"]["sha256"] = "0" * 64
    del bad["hardware"]["driver"]
    del bad["runs"][0]["tier"]
    assert len(validate(bad)) == 3, validate(bad)
    memory = json.loads(json.dumps(pk))
    memory["skipped"] = [{"label": "creature-high", "need_mb": 14000, "available_mb": 8000, "message": "needs 14 GB"}]
    assert validate(memory) == [], validate(memory)
    memory["skipped"] = [{"label": "creature-high", "need_mb": 0, "available_mb": 0, "message": "creature-head.sfproj is damaged"}]
    assert len(validate(memory)) == 1
    plain = json.loads(json.dumps(std))
    plain["skipped"] = [{"label": "33M", "need_mb": 14000, "message": "memory"}]
    assert len(validate(plain)) == 1  # (outside a pack, a skipped scene still isn't a complete result)
    assert clean_text("  Ana's   laptop ", "name")[1] is None and clean_text("see https://x.example", "name")[1]
    assert clean_text("me@example.com", "name")[1] and clean_text("x" * 90, "machine")[1]
    assert plausibility(pk) == [] and plausibility(dict(pk, settings=dict(pk["settings"], hw_simulate="floor")))
    # The issue form: sections by '### Label', the JSON in a fence; the entry lands in the list with the submitter's words.
    import io, tempfile, contextlib
    body = ("### Name to show\n\nAna\n\n### Machine\n\nFramework 16, plugged in\n\n### Result JSON\n\n```json\n" + json.dumps(pk) +
            "\n```\n\n### Listing\n\n- [x] The run was on a quiet machine\n- [x] I'm happy for this result to be listed publicly on sculptforge.app/benchmarks\n")
    assert json_of(form_sections(body)["Result JSON"])["settings"]["scene"] == "pack-1"
    with tempfile.TemporaryDirectory() as tmp:
        bodyfile, results = pathlib.Path(tmp) / "issue.md", pathlib.Path(tmp) / "results.json"
        bodyfile.write_text(body, encoding="utf-8")
        ns = argparse.Namespace(body=str(bodyfile), issue=77, dry_run=False, results=str(results))
        with contextlib.redirect_stdout(io.StringIO()):
            assert cmd_issue(ns) == 0
            entry = json.loads(results.read_text())["results"][0]
            assert entry["name"] == "Ana" and entry["machine"] == "Framework 16, plugged in" and entry["issue"] == 77
            assert entry["source"] == "community" and entry["scene"] == "pack-1"
            assert cmd_issue(ns) == 0 and len(json.loads(results.read_text())["results"]) == 1  # (the same run twice: once)
            bodyfile.write_text(body.replace("Ana", "Ana http://spam.example"), encoding="utf-8")
            assert cmd_issue(ns) == 1  # a link in the name: refused
            bodyfile.write_text(body.replace("- [x] I'm happy", "- [ ] I'm happy"), encoding="utf-8")
            assert cmd_issue(ns) == 1  # not agreed to
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
    i = sub.add_parser("issue")
    i.add_argument("body", help="the issue's text (a file, or - for stdin)")
    i.add_argument("--issue", type=int, default=None, help="the issue's number, kept in the entry")
    i.add_argument("--dry-run", action="store_true")
    i.add_argument("--results", default=str(DEFAULT_RESULTS))
    sub.add_parser("selftest")
    args = ap.parse_args()
    return {"check": cmd_check, "add": cmd_add, "issue": cmd_issue, "selftest": cmd_selftest}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
