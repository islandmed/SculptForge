#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Insider daily drops: make the day's candidates, gate them, pack the best one, post it.

    drops.py daily  --date 2026-11-02 --out OUT --history H.json      # OUT/<date>/: .sfpack, preview.png, drop.json
    drops.py daily  ... --hold                                          # held: not posted or bundled until approved
    drops.py approve OUT/2026-11-02                                     # the coordinator's OK for a held drop
    drops.py post   OUT/2026-11-02 --webhook-env DISCORD_INSIDERS_WEBHOOK
    drops.py monthly --month 2026-11 --drops DROPS_ROOT --out OUT       # "Insider Pack 2026-11" for Supporters
    drops.py schedule --listing L.json --pack OUT/insider-pack-2026-11.sfpack --month 2026-11
    drops.py due --listing L.json --today 2027-03-01                      # ids whose public date has come
    drops.py dryrun --start 2026-11-02 --days 14 --out ~/Work/look-mockups/drops
    drops.py selftest --cli build/sculptforge-cli                        # ctest: determinism, gate, pack validity

Tiers: Insiders get the daily drop, Supporters the month's bundle at month end, everyone the bundle free three
months after the month ends. Candidates come only from tools/drops/generate.py (our own procedural code).
The renders need the `sculptforge` app (--app or $SCULPTFORGE; under xvfb-run on a headless box); without it the
gate still runs every check that needs no render.
"""

import argparse
import atexit
import calendar
import datetime
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate  # noqa: E402
import sfimage  # noqa: E402

AUTHOR = "The SculptForge developer"
LICENSE_ID = "CC-BY-4.0"
LICENSE_TEXT = """These brushes, alphas, VDMs and presets were made by the SculptForge developer with SculptForge's own
procedural tools. They are licensed under the Creative Commons Attribution 4.0 International License
(CC BY 4.0): https://creativecommons.org/licenses/by/4.0/

You may use them in any work, commercial or not, and share or adapt them, with credit to SculptForge.
"""
FOLDER = {"alpha": ("alphas", ".png"), "brush": ("brushes", ".sfbrush"), "vdm": ("vdms", ".sfvdm"),
          "macro": ("macros", ".sfmacro")}
DUPLICATE_BITS = 10       # perceptual hashes this close (of 64 bits) count as the same drop
MIN_RANGE, MIN_STD = 0.15, 0.04
MAX_SEAM = 1.5            # tileable: the wrap-around jump against the largest jump inside
MAX_BORDER = 0.06         # stamps must fade out at their edges
MAX_CLIPPED = 0.30
MIN_RENDER_CHANGE = 0.004  # mean luminance change on the test sculpt
ROOT = Path(__file__).resolve().parents[2]
PUBLIC_DELAY_MONTHS = 3


# --- Tools ---

def find_exe(explicit, env, names):
    cands = [explicit, os.environ.get(env)]
    for b in ("build", "build-release"):
        cands += [ROOT / b / n for n in names]
    cands += [shutil.which(n) for n in names]
    for c in cands:
        if c and Path(c).is_file():
            return str(c)
    return None


def quote(text):
    return '"' + str(text).replace("\\", "\\\\").replace('"', '\\"') + '"'


_SANDBOX = None


def sandbox():
    """A throwaway home for the app's settings, data and log, so renders never touch the user's own (their
    sculptforge.log rotates on every start: crash evidence would be lost). Returns (env, log dir)."""
    global _SANDBOX
    if _SANDBOX is None:
        root = Path(tempfile.mkdtemp(prefix="sf-drops-"))
        atexit.register(shutil.rmtree, root, True)
        env = dict(os.environ, XDG_DATA_HOME=str(root / "data"), XDG_CONFIG_HOME=str(root / "config"),
                   XDG_CACHE_HOME=str(root / "cache"), APPDATA=str(root / "data"), LOCALAPPDATA=str(root / "data"),
                   SDL_APP_ID=os.environ.get("SDL_APP_ID", "sculptforge-agent"))
        for d in ("data", "config", "cache", "log"):
            (root / d).mkdir()
        _SANDBOX = (env, str(root / "log"))
    return _SANDBOX


def run_cli(cli, command):
    r = subprocess.run([cli, "-c", command], capture_output=True, text=True, timeout=300, env=sandbox()[0])
    return r.returncode == 0, (r.stdout + r.stderr).strip()


# --- Candidates on disk ---

def item_name(c):
    folder, ext = FOLDER[c["type"]]
    return f"{folder}/{c['name']}{ext}"


def rename(c, name):
    """Gives the candidate a new name everywhere it is stored (the brush JSON, the macro header, the VDM header)."""
    old = c["name"]
    c["name"] = name
    if c["type"] == "brush":
        j = json.loads(c["data"])
        j["name"] = name
        c["data"] = (json.dumps(j, indent=2) + "\n").encode()
    elif c["type"] == "macro":
        c["data"] = c["data"].decode().replace(f"# name: {old}\n", f"# name: {name}\n", 1).encode()
    elif c["type"] == "vdm":
        import struct
        w, h, n = struct.unpack("<III", c["data"][8:20])
        c["data"] = generate.vdm_bytes(name, w, h, []) + c["data"][20 + n:]


def write_item(c, folder):
    """Writes the candidate's file under FOLDER in the pack layout; returns its path."""
    path = Path(folder) / item_name(c)
    path.parent.mkdir(parents=True, exist_ok=True)
    if c["type"] == "alpha":
        sfimage.write_gray(path, c["grid"])
    else:
        path.write_bytes(c["data"])
    return path


# --- Rendering on the standard test sculpt ---

CURVES = [[(-0.75, 0.42), (-0.25, 0.58), (0.25, 0.30), (0.75, 0.45)],
          [(-0.90, 0.00), (-0.30, 0.15), (0.30, -0.15), (0.90, 0.00)],
          [(-0.75, -0.45), (-0.25, -0.30), (0.25, -0.58), (0.75, -0.42)]]


def render_setup(c, item, check=False):
    """(script, view, zoom): the candidate on the standard test sculpt; item None = the bare sculpt (baseline).
    check: the fold test's strokes (a Kelvinlet twist curls the surface over on purpose; the test grabs it instead)."""
    if c["type"] == "alpha":
        lines = ["new_plane resolution=1024"]
        if item:
            carve = "true" if c.get("kind") in ("pores", "wrinkles") else "false"
            lines += [f"brush_set_alpha path={quote(item)}",
                      f"stroke brush=Standard points=\"0,0,0\" view=0,-1,0 radius=0.85 strength=0.8 invert={carve}"]
        return "; ".join(lines + ["set_material name=Clay"]), "0,-60", "2.2"
    if c["type"] == "vdm":
        lines = ["new_sphere subdivisions=8"]
        if item:
            lines += [f"vdm_set path={quote(item)}", "vdm_stamp point=0,0,0 view=0,0,-1 radius=0.7 strength=0.6"]
        return "; ".join(lines + ["set_material name=Clay"]), "30,-25", "1.6"
    lines = ["new_sphere subdivisions=8"]
    pts = [" ".join(f"{x},{y},0" for x, y in cv) for cv in CURVES]
    if item and c["type"] == "brush":
        lines.append(f"brush_load path={quote(item)}")
        lines += [f"stroke points={quote(p)} view=0,0,-1 radius=0.12 seed={i + 1}" for i, p in enumerate(pts)]
    elif item:  # a Kelvinlet preset: its settings, then the Kelvinlet brush along the same curves
        lines += [ln for ln in Path(item).read_text().splitlines() if ln and not ln.startswith("#")]
        if check:
            lines.append("stroke brush=KelvinGrab points=\"-0.15,0.1,0 0.2,0.25,0\" view=0,0,-1 radius=0.4 strength=0.8 "
                         "curve=false")
        else:
            lines += [f"stroke brush=KelvinTwist points={quote(pts[0])} view=0,0,-1 radius=0.3 strength=0.7",
                      f"stroke brush=KelvinGrab points={quote(pts[2])} view=0,0,-1 radius=0.35 strength=0.5"]
    return "; ".join(lines + ["set_material name=Clay"]), "0,0", "1.75"


MAX_FOLDED = 0.0001  # a torn surface folds over: at most 1 in 10,000 triangles may turn their face around


def _face_normals(obj):
    v, normals = [], []
    with open(obj) as f:
        for line in f:
            if line.startswith("v "):
                v.append(tuple(map(float, line.split()[1:4])))
            elif line.startswith("f "):
                i = [int(t.split("/")[0]) - 1 for t in line.split()[1:4]]
                a, b, c = v[i[0]], v[i[1]], v[i[2]]
                e1 = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
                e2 = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
                normals.append((e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]))
    return normals


def folded_fraction(cli, c, item):
    """Runs the candidate on the test sphere in sculptforge-cli; the share of triangles whose normal turned around."""
    script = render_setup(c, item, check=True)[0].rsplit("; set_material", 1)[0]
    first, rest = script.split("; ", 1)
    with tempfile.TemporaryDirectory() as t:
        before, after = Path(t) / "before.obj", Path(t) / "after.obj"
        ok, log = run_cli(cli, f"{first}; save_obj path={quote(before)}; {rest}; save_obj path={quote(after)}")
        if not ok or not after.is_file():
            return None, log[-300:]
        n0, n1 = _face_normals(before), _face_normals(after)
        if len(n0) != len(n1):
            return None, "the stroke changed the topology"
        bad = sum(1 for a, b in zip(n0, n1) if a[0] * b[0] + a[1] * b[1] + a[2] * b[2] < 0)
        return bad / max(len(n0), 1), None


def render(app, c, item, out, size="512x384"):
    script, view, zoom = render_setup(c, item)
    out = Path(out)
    out.unlink(missing_ok=True)
    env, log_dir = sandbox()
    cmd = [app, "--render", str(out), "--size", size, "--view", view, "--zoom", zoom, "--log-dir", log_dir,
           "--run", script]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=300, env=env)
    if r.returncode != 0 or not out.is_file():
        return (r.stdout + r.stderr).strip()[-400:] or f"exit code {r.returncode}"
    return None


# --- The gate ---

def check_data(c):
    """Checks that need no render. Returns (reasons to reject, measurements)."""
    reasons, m = [], {}
    if c["type"] in ("alpha", "vdm"):
        s = sfimage.stats(c["grid"])
        m.update({k: round(v, 4) for k, v in s.items()})
        if s["bad"]:
            reasons.append("broken: non-finite values")
        elif s["range"] < MIN_RANGE or s["std"] < MIN_STD:
            reasons.append(f"too faint (range {s['range']:.3f}, spread {s['std']:.3f})")
        elif s["high"] > MAX_CLIPPED or (c["tileable"] and s["low"] > MAX_CLIPPED + 0.2):
            reasons.append(f"broken: clipped (white {s['high']:.0%}, black {s['low']:.0%})")
        if not reasons and c["tileable"]:
            m["seam"] = round(sfimage.seam_ratio(c["grid"]), 3)
            if m["seam"] > MAX_SEAM:
                reasons.append(f"tiling seam ({m['seam']:.2f} x the largest inner step)")
        if not reasons and not c["tileable"]:
            m["border"] = round(sfimage.border_mean(c["grid"]), 4)
            if m["border"] > MAX_BORDER:
                reasons.append(f"hard edge: the stamp doesn't fade out (border {m['border']:.3f})")
    if c["type"] == "vdm" and not reasons:
        import struct
        w, h, n = struct.unpack("<III", c["data"][8:20])
        if len(c["data"]) != 20 + n + w * h * 12:
            reasons.append("broken: VDM size doesn't match its header")
    return reasons, m


def gate(c, history, app=None, work=None, baselines=None, cli=None):
    """Rejects or scores one candidate. history: earlier drops' {"phash": int, ...}. Fills c["gate"]."""
    reasons, m = check_data(c)
    signature = c["grid"]
    if not reasons and cli and work and c["type"] in ("brush", "macro"):
        item = write_item(c, Path(work) / "items" / c["kind"])
        frac, err = folded_fraction(cli, c, str(item))
        if frac is None:
            reasons.append(f"broken: the test stroke failed ({err})")
        else:
            m["folded"] = round(frac, 6)
            if frac > MAX_FOLDED:
                reasons.append(f"broken: tears the surface ({frac:.2%} of triangles fold over)")
    if not reasons and app:
        item = write_item(c, Path(work) / "items" / c["kind"])
        shot = Path(work) / f"{c['kind']}.png"
        err = render(app, c, str(item), shot)
        if err:
            reasons.append(f"broken: render failed ({err.splitlines()[-1] if err else ''})")
        else:
            c["preview"] = str(shot)
            lum = sfimage.luminance(shot)
            base = baselines.get(render_setup(c, None))
            m["render_change"] = round(sfimage.mean_abs_diff(lum, base), 4) if base else 0.0
            if base and m["render_change"] < MIN_RENDER_CHANGE:
                reasons.append(f"too faint on the test sculpt (change {m['render_change']:.4f})")
            if signature is None:
                signature = lum
    if not reasons and signature is not None and c["type"] in ("alpha", "vdm"):  # renders of brushes look alike
        h = sfimage.phash(signature)
        m["phash"] = f"{h:016x}"
        for d in history:
            if d.get("kind") == c["kind"] and d.get("phash") and \
                    sfimage.hamming(h, int(d["phash"], 16)) <= DUPLICATE_BITS:
                reasons.append(f"near-duplicate of {d.get('date')} {d.get('name')}")
                break
    if not reasons:  # the same recipe again (a weave with the same threads ...): compare the settings
        for d in history:
            if d.get("kind") == c["kind"] and d.get("params") and _param_distance(d["params"], c["params"]) < 0.05:
                reasons.append(f"near-duplicate settings of {d.get('date')} {d.get('name')}")
                break
    score = 0.0
    if not reasons:
        detail = m.get("detail", 0.02)
        score = (min(m.get("std", 0.2), 0.3) / 0.3 + min(detail, 0.06) / 0.06 +
                 min(m.get("render_change", 0.02), 0.05) / 0.05)
        recent = [d.get("kind") for d in history[-7:]]  # variety: a kind not seen this week wins over a nicer repeat
        score += 2.0 if c["kind"] not in recent else -1.5 * recent.count(c["kind"])
        if recent and recent[-1] == c["kind"]:  # never the same kind two days running while another one passes
            score -= 10.0
    c["gate"] = {"ok": not reasons, "reasons": reasons, "score": round(score, 3), "metrics": m}
    return c["gate"]


def _param_distance(a, b):
    keys = [k for k in a if isinstance(a.get(k), (int, float)) and isinstance(b.get(k), (int, float))
            and not isinstance(a.get(k), bool)]
    if not keys or any(a.get(k) != b.get(k) for k in a if isinstance(a.get(k), (str, bool))):
        return 1.0
    return sum(abs(a[k] - b[k]) for k in keys) / max(1, len(keys))


# --- Packs ---

def make_pack(cli, folder, out, pack_id, name, description):
    ok, log = run_cli(cli, f"pack_create folder={quote(folder)} path={quote(out)} id={pack_id} name={quote(name)} "
                           f"version=1.0 author={quote(AUTHOR)} license={LICENSE_ID} "
                           f"description={quote(description)} channel=insider")
    if not ok:
        raise RuntimeError(f"pack_create failed: {log}")
    return log


def unique_name(c, history):
    used = {d.get("name") for d in history}
    if c["name"] in used:
        n = 2
        while f"{c['name']} {n}" in used:
            n += 1
        rename(c, f"{c['name']} {n}")


def daily(date, out, history, app, cli, keep_rejects=None, hold=False):
    """Builds the drop for DATE in OUT/<date>/ and appends it to `history`. Returns the drop or None."""
    work = Path(tempfile.mkdtemp(prefix=f"drop-{date}-"))
    try:
        baselines = {}
        if app:
            for kind_type in ("alpha", "vdm", "brush"):
                setup = render_setup({"type": kind_type}, None)
                shot = work / f"baseline-{kind_type}.png"
                if render(app, {"type": kind_type}, None, shot) is None:
                    baselines[setup] = sfimage.luminance(shot)
        cands = generate.candidates(date)
        for c in cands:
            gate(c, history, app, work, baselines, cli)
        log = [{"kind": c["kind"], "name": c["name"], **c["gate"]} for c in cands]
        good = sorted((c for c in cands if c["gate"]["ok"]), key=lambda c: -c["gate"]["score"])
        day = Path(out) / date.isoformat()
        day.mkdir(parents=True, exist_ok=True)
        (day / "gate.json").write_text(json.dumps(log, indent=2) + "\n")
        if keep_rejects:
            for c in cands:
                if not c["gate"]["ok"] and c.get("preview"):
                    shutil.copy(c["preview"], Path(keep_rejects) / f"{date}-rejected-{c['kind']}.png")
        if not good:
            return None
        best = good[0]
        unique_name(best, history)
        stage = work / "stage"
        write_item(best, stage)
        write_item(best, day)  # the item on its own, for the month's bundle
        (stage / "LICENSE.txt").write_text(LICENSE_TEXT)
        preview = day / "preview.png"
        if best.get("preview"):
            shutil.copy(best["preview"], preview)
            shutil.copy(preview, stage / "preview.png")
        elif best["grid"] is not None:
            sfimage.write_gray(preview, sfimage.resample(best["grid"], 256))
            shutil.copy(preview, stage / "preview.png")
        pack_id = f"insider-drop-{date.isoformat()}"
        pack = day / f"{pack_id}.sfpack"
        if cli:
            make_pack(cli, stage, pack, pack_id, f"Insider Drop {date.isoformat()}", best["description"])
        drop = {"date": date.isoformat(), "kind": best["kind"], "type": best["type"], "name": best["name"],
                "description": best["description"], "item": item_name(best), "pack": pack.name if cli else None,
                "preview": preview.name if preview.is_file() else None, "phash": best["gate"]["metrics"].get("phash"),
                "params": best["params"], "score": best["gate"]["score"], "held": hold}
        (day / "drop.json").write_text(json.dumps(drop, indent=2) + "\n")
        history.append(drop)
        return drop
    finally:
        shutil.rmtree(work, ignore_errors=True)


# --- Delivery ---

def post_discord(webhook, content, files, username="SculptForge Insiders", image=None):
    """A message with attachments (multipart/form-data). files: paths."""
    embed = None
    if image:
        embed = {"image": {"url": f"attachment://{Path(image).name}"}}
    payload = {"content": content, "username": username, "allowed_mentions": {"parse": []},
               "attachments": [{"id": i, "filename": Path(f).name} for i, f in enumerate(files)]}
    if embed:
        payload["embeds"] = [embed]
    boundary = uuid.uuid4().hex
    parts = [f"--{boundary}\r\nContent-Disposition: form-data; name=\"payload_json\"\r\n"
             f"Content-Type: application/json\r\n\r\n{json.dumps(payload)}\r\n".encode()]
    for i, f in enumerate(files):
        kind = "image/png" if str(f).endswith(".png") else "application/zip"
        parts.append(f"--{boundary}\r\nContent-Disposition: form-data; name=\"files[{i}]\"; "
                     f"filename=\"{Path(f).name}\"\r\nContent-Type: {kind}\r\n\r\n".encode() +
                     Path(f).read_bytes() + b"\r\n")
    parts.append(f"--{boundary}--\r\n".encode())
    req = urllib.request.Request(webhook, data=b"".join(parts), method="POST",
                                 headers={"Content-Type": f"multipart/form-data; boundary={boundary}",
                                          "User-Agent": "SculptForge-drops"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.status


def cmd_post(args):
    hook = os.environ.get(args.webhook_env, "")
    if not hook:
        print(f"{args.webhook_env} is not set; nothing posted.")
        return 0
    day = Path(args.drop)
    meta = json.loads((day / ("bundle.json" if (day / "bundle.json").is_file() else "drop.json")).read_text())
    if meta.get("held"):
        print(f"{meta['name']} is held for approval; nothing posted.")
        return 0
    files = [day / meta["preview"]] if meta.get("preview") else []
    files.append(day / meta["pack"])
    if meta.get("date"):
        kind = {"alpha": "alpha", "brush": "brush", "vdm": "VDM", "macro": "Kelvinlet preset"}[meta["type"]]
        text = (f"**Today's drop: {meta['name']}** (new {kind})\n{meta['description']}\n"
                "Install: File > Install Pack... and pick the attached .sfpack.")
    else:
        text = (f"**{meta['name']}** is here: this month's {meta['count']} Insider drops in one pack.\n"
                f"{meta['description']}\nInstall: File > Install Pack... and pick the attached .sfpack. "
                f"It goes free for everyone on {meta['publicDate']}.")
    status = post_discord(hook, text, files, image=files[0] if meta.get("preview") else None)
    print(f"posted {meta['name']}: {status}")
    return 0


def month_drops(root, month):
    y, m = month.split("-")
    found = [(json.loads(p.read_text()), p.parent) for p in Path(root, y, m).glob("*/drop.json")]
    found = [d for d in found if not d[0].get("held")]  # held and never approved: not part of the bundle
    return sorted(found, key=lambda d: d[0]["date"])


def public_date(month):
    y, m = map(int, month.split("-"))
    m += 1 + PUBLIC_DELAY_MONTHS  # the first day after the month, plus three months
    y, m = y + (m - 1) // 12, (m - 1) % 12 + 1
    return datetime.date(y, m, 1).isoformat()


def monthly(month, root, out, cli):
    drops = month_drops(root, month)
    if not drops:
        raise RuntimeError(f"no drops for {month} under {root}")
    stage = Path(tempfile.mkdtemp(prefix=f"bundle-{month}-"))
    try:
        seen, previews = set(), []
        for meta, folder in drops:
            src = folder / meta["item"]
            folder_name, ext = meta["item"].split("/")[0], Path(meta["item"]).suffix
            name = meta["item"]
            if name.lower() in seen:  # two days with the same item name: the later one gets its date
                name = f"{folder_name}/{meta['name']} {meta['date'][5:]}{ext}"
            seen.add(name.lower())
            (stage / folder_name).mkdir(parents=True, exist_ok=True)
            shutil.copy(src, stage / name)
            if meta.get("preview"):
                previews.append(folder / meta["preview"])
        (stage / "LICENSE.txt").write_text(LICENSE_TEXT)
        if previews:
            sfimage.contact_sheet(stage / "preview.png", previews[:20], cols=5, cell=(192, 144))
        label = datetime.date(*map(int, month.split("-")), 1).strftime("%B %Y")
        counts = {}
        for meta, _ in drops:
            counts[meta["type"]] = counts.get(meta["type"], 0) + 1
        words = {"alpha": "alphas", "brush": "brushes", "vdm": "VDMs", "macro": "Kelvinlet presets"}
        desc = f"Every Insider drop from {label}: " + ", ".join(f"{n} {words[t] if n > 1 else words[t][:-1]}"
                                                                 for t, n in sorted(counts.items())) + "."
        out = Path(out)
        out.mkdir(parents=True, exist_ok=True)
        pack_id = f"insider-pack-{month}"
        pack = out / f"{pack_id}.sfpack"
        make_pack(cli, stage, pack, pack_id, f"Insider Pack {month}", desc)
        if (stage / "preview.png").is_file():
            shutil.copy(stage / "preview.png", out / f"{pack_id}.png")
        bundle = {"id": pack_id, "name": f"Insider Pack {month}", "month": month, "count": len(drops),
                  "description": desc, "pack": pack.name, "preview": f"{pack_id}.png" if previews else None,
                  "publicDate": public_date(month), "items": [m["name"] for m, _ in drops]}
        (out / "bundle.json").write_text(json.dumps(bundle, indent=2) + "\n")
        return bundle
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def schedule(listing, bundle):
    """Adds (or updates) the bundle in the public pack listing; `url` stays empty until its public date."""
    path = Path(listing)
    data = json.loads(path.read_text()) if path.is_file() else {"format": "sculptforge-pack-listing", "version": 1,
                                                                 "packs": []}
    entry = {k: bundle[k] for k in ("id", "name", "description", "publicDate")}
    entry["file"] = f"insider/{bundle['pack']}"
    entry["preview"] = f"insider/{bundle['preview']}" if bundle.get("preview") else None
    entry["released"] = False
    data["packs"] = [p for p in data["packs"] if p["id"] != entry["id"]] + [entry]
    data["packs"].sort(key=lambda p: p["publicDate"])
    path.write_text(json.dumps(data, indent=2) + "\n")
    return entry


def due(listing, today):
    path = Path(listing)
    if not path.is_file():
        return []
    return [p for p in json.loads(path.read_text())["packs"] if not p["released"] and p["publicDate"] <= today]


# --- Commands ---

def load_history(path):
    p = Path(path) if path else None
    return json.loads(p.read_text()) if p and p.is_file() else []


def cmd_daily(args):
    app = None if args.no_render else find_exe(args.app, "SCULPTFORGE", ["sculptforge", "sculptforge.exe"])
    cli = find_exe(args.cli, "SCULPTFORGE_CLI", ["sculptforge-cli", "sculptforge-cli.exe"])
    if not cli:
        sys.exit("error: cannot find sculptforge-cli (--cli)")
    if not app and not args.no_render:
        sys.exit("error: cannot find the sculptforge app for the renders (--app, or --no-render)")
    history = load_history(args.history)
    drop = daily(generate.parse_date(args.date), args.out, history, app, cli, hold=args.hold)
    if args.history:
        Path(args.history).write_text(json.dumps(history, indent=2) + "\n")
    if not drop:
        print(f"{args.date}: every candidate was rejected; no drop today (see gate.json)")
        return 0
    print(f"{args.date}: {drop['name']} ({drop['kind']}): {drop['description']}" + (" [held for approval]" if drop["held"] else ""))
    return 0


def cmd_approve(args):
    """Releases a held drop (the coordinator's approval): it may then be posted and goes into the month's bundle."""
    path = Path(args.drop) / "drop.json"
    if not path.is_file():
        sys.exit(f"error: no drop at {path}")
    meta = json.loads(path.read_text())
    meta["held"] = False
    path.write_text(json.dumps(meta, indent=2) + "\n")
    print(f"approved {meta['date']}: {meta['name']}")
    return 0


def cmd_monthly(args):
    cli = find_exe(args.cli, "SCULPTFORGE_CLI", ["sculptforge-cli", "sculptforge-cli.exe"])
    b = monthly(args.month, args.drops, args.out, cli)
    print(f"{b['name']}: {b['count']} drops, public on {b['publicDate']}")
    return 0


def cmd_schedule(args):
    bundle = json.loads((Path(args.bundle) / "bundle.json").read_text())
    e = schedule(args.listing, bundle)
    print(f"scheduled {e['id']} for {e['publicDate']}")
    return 0


def cmd_due(args):
    for p in due(args.listing, args.today or datetime.date.today().isoformat()):
        print(p["id"])
    return 0


def cmd_release(args):
    """Marks a pack released in the listing (the workflow copies its files next to the listing first)."""
    path = Path(args.listing)
    data = json.loads(path.read_text())
    for p in data["packs"]:
        if p["id"] == args.id:
            p["released"] = True
    path.write_text(json.dumps(data, indent=2) + "\n")
    return 0


def cmd_dryrun(args):
    app = find_exe(args.app, "SCULPTFORGE", ["sculptforge", "sculptforge.exe"])
    cli = find_exe(args.cli, "SCULPTFORGE_CLI", ["sculptforge-cli", "sculptforge-cli.exe"])
    out = Path(args.out).expanduser()
    out.mkdir(parents=True, exist_ok=True)
    rejects = out / "rejected"
    rejects.mkdir(exist_ok=True)
    work = out / "build"
    history, start, rows = [], generate.parse_date(args.start), []
    for i in range(args.days):
        date = start + datetime.timedelta(days=i)
        drop = daily(date, work, history, app, cli, keep_rejects=rejects)
        if drop:
            shutil.copy(work / drop["date"] / "preview.png", out / f"{drop['date']}-{drop['kind']}.png")
            rows.append(f"{drop['date']}  {drop['kind']:<9} {drop['name']}: {drop['description']}")
        else:
            rows.append(f"{date}  (no drop: every candidate rejected)")
        print(rows[-1], flush=True)
    previews = sorted(out.glob("20*.png"))
    if previews:
        sfimage.contact_sheet(out / "overview.png", previews, cols=7, cell=(256, 192))
    (out / "README.txt").write_text("Insider daily drops, dry run (nothing posted).\n\n" + "\n".join(rows) + "\n")
    return 0


def cmd_selftest(args):
    """Determinism, the gate rejecting a blank alpha, and pack validity (through sculptforge-cli's own checks)."""
    failures = []
    d = datetime.date(2026, 11, 2)
    a, b = generate.candidates(d), generate.candidates(d)
    for x, y in zip(a, b):
        bx = x["data"] if x["grid"] is None else repr(x["grid"]).encode()
        by = y["data"] if y["grid"] is None else repr(y["grid"]).encode()
        if bx != by or x["name"] != y["name"]:
            failures.append(f"not deterministic: {x['kind']}")
    if [c["name"] for c in generate.candidates(d + datetime.timedelta(days=1))] == [c["name"] for c in a]:
        failures.append("the next day gave the same candidates")
    for c in a:  # the written files are byte-identical too
        with tempfile.TemporaryDirectory() as t1, tempfile.TemporaryDirectory() as t2:
            p1, p2 = write_item(c, t1), write_item(dict(c), t2)
            if p1.read_bytes() != p2.read_bytes():
                failures.append(f"file bytes differ: {c['kind']}")
    blank = {"kind": "noise", "type": "alpha", "tileable": True, "name": "Blank", "params": {},
             "grid": [[0.0] * 64 for _ in range(64)]}
    g = gate(blank, [])
    if g["ok"] or not any("faint" in r for r in g["reasons"]):
        failures.append(f"blank alpha not rejected as faint: {g}")
    flat = dict(blank, grid=[[0.5] * 64 for _ in range(64)])
    if gate(flat, [])["ok"]:
        failures.append("flat grey alpha passed")
    seam = dict(blank, grid=[[x / 63 for x in range(64)] for _ in range(64)])
    if not any("seam" in r for r in gate(seam, [])["reasons"]):
        failures.append(f"a ramp (seam at the wrap) passed: {gate(seam, [])}")
    hard = dict(blank, tileable=False, grid=[[1.0 if (x // 8 + y // 8) % 2 else 0.2 for x in range(64)] for y in range(64)])
    if gate(hard, [])["ok"]:
        failures.append("a stamp with hard edges passed")
    if args.cli:
        harsh = {"kind": "trims", "type": "brush", "tileable": False, "grid": None, "name": "Harsh", "params": {},
                 "data": generate.brush_json("Harsh", "x", depth=-0.75, pinch=0.85, hardness=0.95, spacing=0.04)}
        with tempfile.TemporaryDirectory() as t:
            g = gate(harsh, [], work=t, cli=args.cli)
        if g["ok"] or not any("tears" in r for r in g["reasons"]):
            failures.append(f"a brush that tears the surface passed: {g}")
    good = generate.make("pores", d)
    if not gate(good, [])["ok"]:
        failures.append(f"a real pore alpha was rejected: {good['gate']}")
    dup = generate.make("pores", d)
    if gate(dup, [{"kind": "pores", "phash": good["gate"]["metrics"]["phash"], "date": "x", "name": "y"}])["ok"]:
        failures.append("a near-duplicate passed")
    for k in generate.KINDS:  # every kind passes the data checks on some day of a week
        if not any(check_data(generate.make(k, d + datetime.timedelta(days=i)))[0] == [] for i in range(7)):
            failures.append(f"{k}: rejected every day of a week")
    if args.cli:
        with tempfile.TemporaryDirectory() as t:
            for c in a + [generate.make(k, d) for k in ("kelvinlet", "trims")]:
                stage = Path(t) / c["kind"]
                write_item(c, stage)
                (stage / "LICENSE.txt").write_text(LICENSE_TEXT)
                try:
                    make_pack(args.cli, stage, Path(t) / f"{c['kind']}.sfpack", f"insider-drop-test-{c['kind']}",
                              "Insider Drop test", c["description"])
                except RuntimeError as e:
                    failures.append(f"{c['kind']}: {e}")
    if any(k in generate.DISABLED for i in range(60) for k in generate.kinds_for(d + datetime.timedelta(days=i))):
        failures.append("a disabled kind is in the rotation")
    with tempfile.TemporaryDirectory() as t:
        for day, held in (("02", False), ("03", True)):
            f = Path(t, "2026", "11", f"2026-11-{day}")
            f.mkdir(parents=True)
            (f / "drop.json").write_text(json.dumps({"date": f"2026-11-{day}", "held": held}))
        if [m["date"] for m, _ in month_drops(t, "2026-11")] != ["2026-11-02"]:
            failures.append("a held drop went into the month's bundle")
    if (public_date("2026-11"), public_date("2026-12")) != ("2027-03-01", "2027-04-01"):
        failures.append(f"public dates: {public_date('2026-11')}, {public_date('2026-12')}")
    for f in failures:
        print("FAIL", f)
    print("drops selftest:", "FAILED" if failures else "ok")
    return 1 if failures else 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("daily")
    s.add_argument("--date", default=datetime.date.today().isoformat())
    s.add_argument("--out", required=True)
    s.add_argument("--history", help="history.json of earlier drops (read and updated)")
    s.add_argument("--app")
    s.add_argument("--cli")
    s.add_argument("--no-render", action="store_true")
    s.add_argument("--hold", action="store_true", help="mark the drop held: not posted or bundled until `approve`")
    s.set_defaults(fn=cmd_daily)
    s = sub.add_parser("approve")
    s.add_argument("drop", help="a day's folder (drop.json)")
    s.set_defaults(fn=cmd_approve)
    s = sub.add_parser("post")
    s.add_argument("drop", help="a day's folder (drop.json) or a bundle folder (bundle.json)")
    s.add_argument("--webhook-env", default="DISCORD_INSIDERS_WEBHOOK")
    s.set_defaults(fn=cmd_post)
    s = sub.add_parser("monthly")
    s.add_argument("--month", required=True)
    s.add_argument("--drops", required=True, help="the drops/ folder (drops/YYYY/MM/<date>/drop.json)")
    s.add_argument("--out", required=True)
    s.add_argument("--cli")
    s.set_defaults(fn=cmd_monthly)
    s = sub.add_parser("schedule")
    s.add_argument("--listing", required=True)
    s.add_argument("--bundle", required=True, help="the monthly output folder (bundle.json)")
    s.set_defaults(fn=cmd_schedule)
    s = sub.add_parser("due")
    s.add_argument("--listing", required=True)
    s.add_argument("--today")
    s.set_defaults(fn=cmd_due)
    s = sub.add_parser("release")
    s.add_argument("--listing", required=True)
    s.add_argument("--id", required=True)
    s.set_defaults(fn=cmd_release)
    s = sub.add_parser("dryrun")
    s.add_argument("--start", required=True)
    s.add_argument("--days", type=int, default=14)
    s.add_argument("--out", required=True)
    s.add_argument("--app")
    s.add_argument("--cli")
    s.set_defaults(fn=cmd_dryrun)
    s = sub.add_parser("selftest")
    s.add_argument("--cli")
    s.set_defaults(fn=cmd_selftest)
    args = p.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
