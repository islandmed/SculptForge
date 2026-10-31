#!/usr/bin/env python3
# SculptForge - package the Blender add-on
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   python3 addons/blender/build_addon.py --out DIR [--version X.Y.Z] [--check]
#
# Writes two zips of addons/blender/sculptforge_bridge:
#   sculptforge_bridge-X.Y.Z.zip         a Blender 4.2+ extension (blender_manifest.toml at the zip's root):
#                                        Edit > Preferences > Get Extensions > Install from Disk
#   sculptforge_bridge-X.Y.Z-legacy.zip  the same as a legacy add-on (a sculptforge_bridge/ folder):
#                                        Edit > Preferences > Add-ons > Install from Disk
# --version stamps the version into the manifest and bl_info (the release workflow passes the tag's).
# --check also validates the manifest the way Blender's extension validator does (the parts that can be
# checked without Blender) and that every file compiles; CMake runs it as a test (blender_addon_package).
import argparse
import os
import py_compile
import re
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "sculptforge_bridge")
LICENSE = os.path.join(HERE, "..", "..", "LICENSE")
FILES = ["__init__.py", "exchange.py", "zbrush_keys.py"]
EPOCH = (2026, 1, 1, 0, 0, 0)  # fixed timestamps: the same sources give the same zip


def read(name):
    with open(os.path.join(SOURCE, name), "r", encoding="utf-8") as f:
        return f.read()


def manifest_value(text, key):
    m = re.search(r'^%s\s*=\s*"([^"]*)"' % re.escape(key), text, re.M)
    return m.group(1) if m else None


def stamp(manifest, init, version):
    manifest = re.sub(r'^version = "[^"]*"', 'version = "%s"' % version, manifest, flags=re.M)
    nums = ", ".join(str(int(x)) for x in version.split(".")[:3])
    init = re.sub(r'"version": \([^)]*\)', '"version": (%s)' % nums, init, count=1)
    return manifest, init


def check(manifest, init):
    errors = []
    for key in ("schema_version", "id", "version", "name", "tagline", "maintainer", "type", "blender_version_min"):
        if not manifest_value(manifest, key):
            errors.append("blender_manifest.toml: missing %s" % key)
    tagline = manifest_value(manifest, "tagline") or ""
    if len(tagline) > 64 or tagline[-1:] in ".!?,;:":
        errors.append("tagline must be at most 64 characters without closing punctuation")
    if manifest_value(manifest, "id") != "sculptforge_bridge":
        errors.append("id must match the package folder (sculptforge_bridge)")
    if not re.fullmatch(r"\d+\.\d+\.\d+", manifest_value(manifest, "version") or ""):
        errors.append("version must be X.Y.Z")
    if 'license = ["SPDX:GPL-3.0-or-later"]' not in manifest:
        errors.append("license must be SPDX:GPL-3.0-or-later (like SculptForge)")
    for reason in re.findall(r'^\w+\s*=\s*"([^"]*)"', manifest.split("[permissions]", 1)[-1], re.M):
        if reason.endswith("."):
            errors.append("permission reasons must not end with a period")
    nums = re.search(r'"version": \(([^)]*)\)', init)
    if not nums or ".".join(x.strip() for x in nums.group(1).split(",")) != manifest_value(manifest, "version"):
        errors.append("bl_info version must match blender_manifest.toml")
    if re.search(r"^\s*import\s+sculptforge_bridge|^\s*from\s+sculptforge_bridge", init, re.M):
        errors.append("extensions must use relative imports")
    imported = set()
    for name in FILES:
        for rel, names in re.findall(r"^\s*from\s+\.(\w*)\s+import\s+([\w, ]+)", read(name), re.M):
            imported.update([rel] if rel else [n.strip() for n in names.split(",")])
    for mod in sorted(imported):
        if os.path.exists(os.path.join(SOURCE, mod + ".py")) and mod + ".py" not in FILES:
            errors.append("%s.py is imported but not in FILES, so the zip would lack it" % mod)
    with tempfile.TemporaryDirectory() as tmp:
        for name in FILES:
            try:
                py_compile.compile(os.path.join(SOURCE, name), cfile=os.path.join(tmp, name + "c"), doraise=True)
            except py_compile.PyCompileError as e:
                errors.append(str(e))
    return errors


def write_zip(path, entries):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for arcname, data in entries:
            info = zipfile.ZipInfo(arcname, EPOCH)
            info.external_attr = 0o644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", required=True)
    ap.add_argument("--version")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    manifest, init = read("blender_manifest.toml"), read("__init__.py")
    if args.version:
        manifest, init = stamp(manifest, init, args.version)
    version = manifest_value(manifest, "version")
    if args.check:
        errors = check(manifest, init)
        for e in errors:
            print("error:", e)
        if errors:
            return 1
    files = {name: read(name) for name in FILES}
    files["__init__.py"] = init
    with open(LICENSE, "r", encoding="utf-8") as f:
        files["LICENSE"] = f.read()
    os.makedirs(args.out, exist_ok=True)
    ext = os.path.join(args.out, "sculptforge_bridge-%s.zip" % version)
    legacy = os.path.join(args.out, "sculptforge_bridge-%s-legacy.zip" % version)
    write_zip(ext, [("blender_manifest.toml", manifest)] + sorted(files.items()))
    write_zip(legacy, [("sculptforge_bridge/" + k, v) for k, v in sorted(files.items())])
    print(ext)
    print(legacy)
    return 0


if __name__ == "__main__":
    sys.exit(main())
