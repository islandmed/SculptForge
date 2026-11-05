#!/usr/bin/env python3
"""Checks a studio profile before it is baked into a build (docs/releasing.md "Studio builds").

    tools/studio/check_profile.py path/to/studio.json

The app checks the same rules again at start-up (src/core/StudioProfile.cpp); this one fails the build early and also
checks that every file the profile names exists and that it fits in the binary (8 KB).
"""
import json
import pathlib
import re
import sys

KEYS = {"format", "version", "id", "name", "offline", "updates", "allow", "localNetwork", "ai", "packs",
        "brushPaths", "preferences", "branding"}


def url_ok(u):
    return re.fullmatch(r"https?://[A-Za-z0-9.\-]+(:[0-9]{1,5})?(/[^\s\\@]*)?", u) is not None


def inside(p):
    q = pathlib.PurePosixPath(p)
    return p and not q.is_absolute() and ".." not in q.parts and ":" not in p and "\\" not in p


def check(path):
    path = pathlib.Path(path)
    text = path.read_text(encoding="utf-8")
    errors = []
    if len(text.encode()) > 8192:
        errors.append("the profile is over 8 KB (it is compiled into the binary)")
    if "${" in text or ")sfstudio" in text:
        errors.append("the profile may not contain '${' or ')sfstudio'")

    def no_dupes(pairs):
        keys = [k for k, _ in pairs]
        if len(keys) != len(set(keys)):
            errors.append("a key is given twice")
        return dict(pairs)

    p = json.loads(text, object_pairs_hook=no_dupes)
    errors += [f'unknown key "{k}"' for k in p if k not in KEYS]
    if p.get("format") != "sculptforge-studio" or p.get("version") != 1:
        errors.append('format must be "sculptforge-studio", version 1')
    if not re.fullmatch(r"[a-z0-9_-]{1,64}", str(p.get("id", ""))):
        errors.append("id: lower-case letters, digits, '-' and '_'")
    if not 0 < len(str(p.get("name", ""))) <= 80:
        errors.append("name: 1 to 80 characters")
    if p.get("offline", True) is not True:
        errors.append("offline must stay true in a studio build")
    for k in ("feed", "mirror"):
        v = p.get("updates", {}).get(k, "")
        if v.startswith(("http://", "https://")) and not url_ok(v):
            errors.append(f"updates.{k}: not a plain http(s) URL")
    errors += [f"allow: {u!r} is not a plain http(s) URL" for u in p.get("allow", []) if not url_ok(u)]
    files = list(p.get("packs", [])) + [p[k] for k in ("preferences",) if k in p]
    if "logo" in p.get("branding", {}):
        files.append(p["branding"]["logo"])
    for f in files:
        if not inside(f):
            errors.append(f"{f}: paths stay inside the profile's folder")
        elif not (path.parent / f).is_file():
            errors.append(f"{f}: missing")
    for e in errors:
        print(f"{path}: {e}", file=sys.stderr)
    return not errors


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(0 if check(sys.argv[1]) else 1)
