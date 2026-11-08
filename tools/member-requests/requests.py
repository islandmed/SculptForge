#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Member Requests: each month, members ask for one item in the members-only #member-requests forum on Discord; we
make the ones we can, the requester approves a preview, and at the end of the month they go out to members together
as one pack ("Member Requests YYYY-MM"), public for everyone a month later. .github/workflows/member-requests.yml
runs this at month end.

The month's items live in the private repository islandmed/sculptforge-insiders, one folder each:

    requests/2026-11/bark-ridges/request.json   {"name": "Bark Ridges", "type": "alpha", "file": "bark-ridges.png",
                                                 "requested_by": "someone", "approved": true}
    requests/2026-11/bark-ridges/bark-ridges.png
    requests/2026-11/bark-ridges/preview.png    (optional)

type: brush (.sfbrush), alpha (.png/.jpg), litsphere (.png/.jpg), material (.sfmaterial), vdm (.sfvdm) or macro
(.sfmacro). Only items marked "approved" (the requester saw the preview) go out. Every item carries "Requested by
@name" in its description (brushes, materials and macros in the file; every item in the pack's description).
Names and descriptions must pass tools/check_marks.py (no other apps' names): a request named after another app's
brush, alpha or material is refused here too, not only by the forum rules.

    requests.py bundle   --month 2026-11 --root insiders/requests --out OUT [--cli sculptforge-cli]
    requests.py post     OUT --webhook-env DISCORD_MEMBERS_WEBHOOK
    requests.py schedule --listing L.json --bundle OUT     # in the public pack listing, released a month later
    requests.py due      --listing L.json [--today 2027-01-01]
    requests.py release  --listing L.json --id member-requests-2026-11
    requests.py list     --month 2026-11 --root insiders/requests [--ballot]   # the month in order; --ballot: the vote's
    requests.py selftest [--cli sculptforge-cli]           # ctest member_requests

A request whose member was refunded carries "member": false (written by the onboarding script, with "priority":
"last"): it leaves the members' pack and the vote and sorts last, its folder and approval kept, never deleted.
"""

import argparse
import datetime
import json
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "drops"))
sys.path.insert(0, str(HERE.parent))
import drops  # noqa: E402  (make_pack, post_discord, find_exe, the license text)
import check_marks  # noqa: E402

TYPES = {"brush": ("brushes", (".sfbrush",)), "alpha": ("alphas", (".png", ".jpg", ".jpeg")),
         "litsphere": ("litspheres", (".png", ".jpg", ".jpeg")), "material": ("materials", (".sfmaterial",)),
         "vdm": ("vdms", (".sfvdm",)), "macro": ("macros", (".sfmacro",))}
WORDS = {"brush": "brush", "alpha": "alpha", "litsphere": "Lit Sphere", "material": "material", "vdm": "VDM",
         "macro": "macro"}
PUBLIC_DELAY_MONTHS = 1  # members first; everyone a month after the month ends
DESCRIPTION_MAX = 1000   # pack.json's limit (docs/pack-format.md)
NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 ._'()-]{0,47}$")
HANDLE_RE = re.compile(r"^[A-Za-z0-9_.-]{1,32}$")


def public_date(month):
    y, m = map(int, month.split("-"))
    m += 1 + PUBLIC_DELAY_MONTHS  # the first day after the month, plus one month
    y, m = y + (m - 1) // 12, (m - 1) % 12 + 1
    return datetime.date(y, m, 1).isoformat()


def credit(handle):
    return f"Requested by @{handle}"


def demoted(r):
    """The requester was refunded: no member priority (the onboarding script sets member=false)."""
    return r.get("member") is False


def requests_in_order(root, month):
    """Every request.json of the month (approved or not): member requests by name, then the demoted ones by name."""
    out = []
    for meta_path in Path(root, month).glob("*/request.json"):
        try:
            r = json.loads(meta_path.read_text(encoding="utf-8"))
        except ValueError:
            continue
        if isinstance(r, dict):
            out.append((r, meta_path.parent))
    return sorted(out, key=lambda x: (demoted(x[0]), str(x[0].get("name", "")).lower(), x[1].name))


def load_items(root, month, marks=None):
    """The month's items: (request, folder) for each approved one, by name. Raises on a broken request."""
    marks = marks or check_marks.load_marks()
    items, seen = [], set()
    for meta_path in sorted(Path(root, month).glob("*/request.json")):
        folder = meta_path.parent
        r = json.loads(meta_path.read_text(encoding="utf-8"))
        where = f"{month}/{folder.name}"
        if not r.get("approved"):
            print(f"{where}: not approved yet (the requester sees the preview first): skipped")
            continue
        if demoted(r):
            print(f"{where}: not a member request any more (member=false): left out of the members' pack, kept")
            continue
        name, kind, handle = str(r.get("name", "")), r.get("type"), str(r.get("requested_by", "")).lstrip("@")
        if not NAME_RE.match(name):
            raise ValueError(f"{where}: name {name!r}: letters, digits, spaces and ._'()- only, 48 at most")
        if kind not in TYPES:
            raise ValueError(f"{where}: type {kind!r}: one of {', '.join(TYPES)}")
        if not HANDLE_RE.match(handle):
            raise ValueError(f"{where}: requested_by {handle!r}: a Discord username")
        f = folder / str(r.get("file", ""))
        if f.parent != folder or not f.is_file() or f.suffix.lower() not in TYPES[kind][1]:
            raise ValueError(f"{where}: file {r.get('file')!r}: a {'/'.join(TYPES[kind][1])} file in this folder")
        for text in (name, str(r.get("description", ""))):
            if marks.search(text):
                raise ValueError(f"{where}: {text!r} names another app (tools/check_marks.py): rename it")
        if name.lower() in seen:
            raise ValueError(f"{where}: a second item called {name!r} this month")
        seen.add(name.lower())
        items.append((dict(r, name=name, requested_by=handle), folder))
    return items


def with_credit(data, kind, handle, description):
    """The item's file with "Requested by @handle" in its own description (brush and material recipes are JSON
    objects with a description; macros have `# description:` header lines). Other kinds are returned unchanged."""
    line = credit(handle)
    text = f"{description} {line}".strip() if description else line
    if kind in ("brush", "material"):
        obj = json.loads(data.decode("utf-8"))
        if not isinstance(obj, dict):
            raise ValueError("not a JSON object")
        old = str(obj.get("description", "")).strip()
        obj["description"] = text if not old else (old if line in old else f"{old} {line}")
        return (json.dumps(obj, indent=2) + "\n").encode()
    if kind == "macro":
        lines = data.decode("utf-8").splitlines()
        for i, l in enumerate(lines):
            if l.lower().startswith("# description:"):
                if line not in l:
                    lines[i] = f"{l.rstrip()} {line}"
                return ("\n".join(lines) + "\n").encode()
        return ("\n".join([f"# description: {text}"] + lines) + "\n").encode()
    return data


def pack_description(month, items):
    label = datetime.date(*map(int, month.split("-")), 1).strftime("%B %Y")
    head = f"Member Requests, {label}: "
    parts = [f"{r['name']} ({WORDS[r['type']]}, {credit(r['requested_by']).lower()})" for r, _ in items]
    text = head + "; ".join(parts) + "."
    while len(text.encode()) > DESCRIPTION_MAX and parts:  # (a long month: the rest are named in each item)
        parts.pop()
        text = head + "; ".join(parts) + f"; and {len(items) - len(parts)} more."
    return text


def bundle(month, root, out, cli):
    if not re.match(r"^20\d\d-[01]\d$", month):
        raise ValueError(f"bad month {month!r}")
    items = load_items(root, month)
    if not items:
        print(f"{month}: no approved requests: nothing to publish")
        return None
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=f"requests-{month}-"))
    try:
        previews = []
        for r, folder in items:
            sub, _ = TYPES[r["type"]]
            src = folder / r["file"]
            (stage / sub).mkdir(exist_ok=True)
            dst = stage / sub / f"{r['name']}{src.suffix.lower()}"
            dst.write_bytes(with_credit(src.read_bytes(), r["type"], r["requested_by"], str(r.get("description", ""))))
            if (folder / "preview.png").is_file():
                previews.append(folder / "preview.png")
        (stage / "LICENSE.txt").write_text(drops.LICENSE_TEXT)
        pack_id = f"member-requests-{month}"
        desc = pack_description(month, items)
        pack = out / f"{pack_id}.sfpack"
        if cli:
            drops.make_pack(cli, stage, pack, pack_id, f"Member Requests {month}", desc)
        preview = None
        if previews:
            preview = f"{pack_id}.png"
            drops.sfimage.contact_sheet(out / preview, previews[:20], cols=5, cell=(192, 144))
        meta = {"id": pack_id, "name": f"Member Requests {month}", "month": month, "count": len(items),
                "description": desc, "pack": pack.name, "preview": preview, "publicDate": public_date(month),
                "items": [{"name": r["name"], "type": r["type"], "requestedBy": r["requested_by"]} for r, _ in items]}
        (out / "bundle.json").write_text(json.dumps(meta, indent=2) + "\n")
        return meta
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def post(folder, webhook_env):
    hook = os.environ.get(webhook_env, "")
    if not hook:
        print(f"{webhook_env} is not set; nothing posted.")
        return 0
    folder = Path(folder)
    meta = json.loads((folder / "bundle.json").read_text())
    who = ", ".join(sorted({"@" + i["requestedBy"] for i in meta["items"]}))
    text = (f"**{meta['name']}** is here: {meta['count']} member request{'s' if meta['count'] != 1 else ''}, made for "
            f"{who}. Thank you!\nInstall: File > Install Pack... and pick the attached .sfpack. It goes free for "
            f"everyone on {meta['publicDate']}. Next month's requests are open in #member-requests.")
    files = ([folder / meta["preview"]] if meta.get("preview") else []) + [folder / meta["pack"]]
    status = drops.post_discord(hook, text, files, username="SculptForge Members",
                                image=files[0] if meta.get("preview") else None)
    print(f"posted {meta['name']}: {status}")
    return 0


def schedule(listing, meta):
    """Adds (or updates) the month's pack in the public listing; `released` stays false until its public date."""
    path = Path(listing)
    data = json.loads(path.read_text()) if path.is_file() else {"format": "sculptforge-pack-listing", "version": 1,
                                                                 "packs": []}
    entry = {k: meta[k] for k in ("id", "name", "description", "publicDate")}
    entry["file"] = f"requests/{meta['pack']}"
    entry["preview"] = f"requests/{meta['preview']}" if meta.get("preview") else None
    entry["released"] = False
    data["packs"] = [p for p in data["packs"] if p["id"] != entry["id"]] + [entry]
    data["packs"].sort(key=lambda p: p["publicDate"])
    path.write_text(json.dumps(data, indent=2) + "\n")
    return entry


def release(listing, pack_id):
    path = Path(listing)
    data = json.loads(path.read_text())
    for p in data["packs"]:
        if p["id"] == pack_id:
            p["released"] = True
    path.write_text(json.dumps(data, indent=2) + "\n")


def selftest(cli):
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    check((public_date("2026-11"), public_date("2026-12")) == ("2027-01-01", "2027-02-01"), "public dates")
    with tempfile.TemporaryDirectory() as t:
        root = Path(t, "requests")

        def item(slug, **r):
            f = root / "2026-11" / slug
            f.mkdir(parents=True)
            (f / r["file"]).write_bytes(r.pop("data", b"x"))
            (f / "request.json").write_text(json.dumps(r))

        brush = drops.generate.brush_json("Bark Ridges", "Deep bark grooves.", depth=0.4)
        item("bark", name="Bark Ridges", type="brush", file="bark.sfbrush", requested_by="@ada", approved=True, data=brush)
        item("pending", name="Later", type="alpha", file="later.png", requested_by="bob", approved=False)
        item("macro", name="Clean Up", type="macro", file="clean.sfmacro", requested_by="cy", approved=True,
             data=b"# name: Clean Up\nmask_clear\n")
        items = load_items(root, "2026-11")
        check([r["name"] for r, _ in items] == ["Bark Ridges", "Clean Up"], f"approved items only: {items}")
        check(items[0][0]["requested_by"] == "ada", "the @ is dropped from the handle")
        b = json.loads(with_credit(brush, "brush", "ada", ""))
        check(b["description"] == "Deep bark grooves. Requested by @ada", f"brush credit: {b['description']}")
        check(json.loads(with_credit(with_credit(brush, "brush", "ada", ""), "brush", "ada", ""))["description"]
              .count("Requested by") == 1, "credit added once")
        m = with_credit(b"# name: Clean Up\nsmooth\n", "macro", "cy", "").decode()
        check(m.startswith("# description: Requested by @cy\n"), f"macro credit: {m!r}")
        d = pack_description("2026-11", items)
        check("Bark Ridges (brush, requested by @ada)" in d and "November 2026" in d, f"pack description: {d}")
        item("refunded", name="Refunded Ask", type="alpha", file="r.png", requested_by="dee", approved=True,
             member=False, priority="last")
        item("aardvark", name="Aardvark", type="alpha", file="a.png", requested_by="eve", approved=False)
        check([r["name"] for r, _ in load_items(root, "2026-11")] == ["Bark Ridges", "Clean Up"],
              "a member=false request leaves the pack")
        order = [r["name"] for r, _ in requests_in_order(root, "2026-11")]
        check(order == ["Aardvark", "Bark Ridges", "Clean Up", "Later", "Refunded Ask"], f"demoted sorts last: {order}")
        check(json.loads((root / "2026-11/refunded/request.json").read_text())["approved"] is True, "status kept")
        many = [({"name": f"Item {i:02d}", "type": "alpha", "requested_by": "someone_long_name"}, None) for i in range(60)]
        check(len(pack_description("2026-11", many).encode()) <= DESCRIPTION_MAX, "a long month fits pack.json")
        # Refused: another app's name (the forum rule, checked again here), a bad name, a file outside the folder.
        fake_marks = re.compile("othermark", re.IGNORECASE)  # (stands in for docs/legal/trademarks.md's list)
        for slug, r in (("mark", dict(name="Ok", description="like OtherMark's", type="alpha", file="m.png")),
                        ("badname", dict(name="../x", type="alpha", file="b.png")),
                        ("escape", dict(name="Esc", type="alpha", file="../bark/bark.sfbrush"))):
            bad = Path(t, "bad-" + slug)
            f = bad / "2026-11" / slug
            f.mkdir(parents=True)
            if not r["file"].startswith(".."):
                (f / r["file"]).write_bytes(b"x")
            (f / "request.json").write_text(json.dumps(dict(r, requested_by="x", approved=True)))
            try:
                load_items(bad, "2026-11", fake_marks)
                failures.append(f"{slug}: not refused")
            except ValueError:
                pass
        listing = Path(t, "listing.json")
        meta = {"id": "member-requests-2026-11", "name": "Member Requests 2026-11", "description": d,
                "publicDate": public_date("2026-11"), "pack": "member-requests-2026-11.sfpack", "preview": None}
        schedule(listing, meta)
        schedule(listing, meta)
        check([p["id"] for p in drops.due(listing, "2026-12-31")] == [], "not public before its date")
        check([p["id"] for p in drops.due(listing, "2027-01-01")] == ["member-requests-2026-11"], "public on its date")
        release(listing, "member-requests-2026-11")
        check(drops.due(listing, "2027-06-01") == [], "released once")
        check(len(json.loads(listing.read_text())["packs"]) == 1, "scheduled once")
        if cli:  # the real pack, through sculptforge-cli's own checks
            out = Path(t, "out")
            try:
                meta = bundle("2026-11", root, out, cli)
                check(meta and (out / meta["pack"]).is_file() and meta["count"] == 2, f"bundle: {meta}")
            except RuntimeError as e:
                failures.append(f"pack: {e}")
    for f in failures:
        print("FAIL", f)
    print("member requests selftest:", "FAILED" if failures else "ok")
    return 1 if failures else 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("bundle")
    s.add_argument("--month", required=True)
    s.add_argument("--root", required=True, help="the private repository's requests/ folder")
    s.add_argument("--out", required=True)
    s.add_argument("--cli")
    s = sub.add_parser("post")
    s.add_argument("folder")
    s.add_argument("--webhook-env", default="DISCORD_MEMBERS_WEBHOOK")
    s = sub.add_parser("schedule")
    s.add_argument("--listing", required=True)
    s.add_argument("--bundle", required=True)
    s = sub.add_parser("due")
    s.add_argument("--listing", required=True)
    s.add_argument("--today")
    s = sub.add_parser("release")
    s.add_argument("--listing", required=True)
    s.add_argument("--id", required=True)
    s = sub.add_parser("list")
    s.add_argument("--month", required=True)
    s.add_argument("--root", required=True, help="the private repository's requests/ folder")
    s.add_argument("--ballot", action="store_true", help="only what members vote on (no member=false requests)")
    s = sub.add_parser("selftest")
    s.add_argument("--cli")
    a = p.parse_args()
    if a.cmd == "bundle":
        cli = drops.find_exe(a.cli, "SCULPTFORGE_CLI", ["sculptforge-cli", "sculptforge-cli.exe"])
        if not cli:
            sys.exit("bundle needs sculptforge-cli (--cli or $SCULPTFORGE_CLI)")
        meta = bundle(a.month, a.root, a.out, cli)
        if meta:
            print(f"{meta['name']}: {meta['count']} items, public on {meta['publicDate']}")
        return 0
    if a.cmd == "post":
        return post(a.folder, a.webhook_env)
    if a.cmd == "schedule":
        e = schedule(a.listing, json.loads((Path(a.bundle) / "bundle.json").read_text()))
        print(f"scheduled {e['id']} for {e['publicDate']}")
        return 0
    if a.cmd == "due":
        for p_ in drops.due(a.listing, a.today or datetime.date.today().isoformat()):
            print(p_["id"])
        return 0
    if a.cmd == "list":
        for r, folder in requests_in_order(a.root, a.month):
            if a.ballot and demoted(r):
                continue
            state = "approved" if r.get("approved") else "waiting for the requester's OK"
            print(f"{r.get('name', folder.name)}\t{state}" + ("\tnot a member request (member=false)" if demoted(r) else ""))
        return 0
    if a.cmd == "release":
        release(a.listing, a.id)
        return 0
    return selftest(a.cli)


if __name__ == "__main__":
    sys.exit(main())
