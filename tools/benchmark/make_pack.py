#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the benchmark scene pack from the repository (bench/pack/pack.json + the starter sculpts it names).

    make_pack.py build [--out DIR] [--zip FILE]   write the pack folder and its zip (default website/benchmarks/pack/) and
                                                  record the hash in bench/pack/hashes.json (a changed pack needs a new version)
    make_pack.py hash DIR                         print the pack's SHA-256 (pack.json, then each scene's file in order)
    make_pack.py verify DIR_OR_ZIP                check a pack against the repository's (exit 1 when it differs)
    make_pack.py check                            build the pack in a temporary folder and compare its hash with hashes.json
                                                  (the test: a changed starter mesh or manifest must come with a new version)

The hash is the one the app puts in a result (settings.pack.sha256, src/core/BenchPack.cpp); docs/benchmark.md.
"""
import argparse
import hashlib
import json
import pathlib
import sys
import tempfile
import zipfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "bench" / "pack"
STARTERS = ROOT / "assets" / "starter"
DEFAULT_DIR = ROOT / "website" / "benchmarks" / "pack"
HASHES = SRC / "hashes.json"  # {"<version>": "<sha256>"}: the published packs; submit.py accepts results of these only
FIXED_TIME = (2026, 11, 17, 0, 0, 0)  # the same bytes on every build


def scene_files(manifest):
    seen = []
    for s in manifest["scenes"]:
        if s["file"] not in seen:
            seen.append(s["file"])
    return seen


def pack_hash(folder):
    folder = pathlib.Path(folder)
    raw = (folder / "pack.json").read_bytes()
    h = hashlib.sha256(raw)
    for s in json.loads(raw)["scenes"]:  # per scene, as the app does: a file two scenes use is hashed twice
        h.update((folder / s["file"]).read_bytes())
    return h.hexdigest()


def starter_project(name):
    """creature-head.sfproj: the starter mesh's zlib-compressed project, unpacked."""
    stem = name[: -len(".sfproj")]
    return zlib.decompress((STARTERS / f"{stem}.sfproj.z").read_bytes())


def build(out_dir, zip_path):
    manifest = json.loads((SRC / "pack.json").read_text())
    out_dir = pathlib.Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    files = {"pack.json": (SRC / "pack.json").read_bytes(), "README.md": (SRC / "README.md").read_bytes()}
    for f in scene_files(manifest):
        files[f] = starter_project(f)
    for name, data in files.items():
        (out_dir / name).write_bytes(data)
    zp = pathlib.Path(zip_path)
    zp.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name in sorted(files):
            info = zipfile.ZipInfo(f"sculptforge-bench-pack-{manifest['version']}/{name}", FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, files[name])
    print(f"pack {out_dir} ({len(files)} files), zip {zp} ({zp.stat().st_size} bytes)")
    digest = pack_hash(out_dir)
    print(f"sha256 {digest}")
    known = json.loads(HASHES.read_text()) if HASHES.exists() else {}
    version = str(manifest["version"])
    if known.get(version, digest) != digest:
        print(f"version {version} was published with another hash ({known[version]}): bump \"version\" in bench/pack/pack.json")
        return 1
    if known.get(version) != digest:
        known[version] = digest
        HASHES.write_text(json.dumps(known, indent=2, sort_keys=True) + "\n")
    return 0


def check():
    manifest = json.loads((SRC / "pack.json").read_text())
    with tempfile.TemporaryDirectory() as tmp:
        folder = pathlib.Path(tmp) / "pack"
        folder.mkdir()
        (folder / "pack.json").write_bytes((SRC / "pack.json").read_bytes())
        for f in scene_files(manifest):
            (folder / f).write_bytes(starter_project(f))
        digest = pack_hash(folder)
    known = json.loads(HASHES.read_text()) if HASHES.exists() else {}
    want = known.get(str(manifest["version"]))
    if want != digest:
        print(f"pack version {manifest['version']} hashes to {digest}, bench/pack/hashes.json has {want}: a changed pack needs a new "
              f"version in pack.json, then `python3 tools/benchmark/make_pack.py build`")
        return 1
    print(f"pack version {manifest['version']} ok ({digest})")
    return 0


def verify(target):
    target = pathlib.Path(target)
    with tempfile.TemporaryDirectory() as tmp:
        folder = target
        if target.is_file():
            with zipfile.ZipFile(target) as z:
                z.extractall(tmp)
            subs = [p for p in pathlib.Path(tmp).iterdir() if p.is_dir()]
            folder = subs[0] if len(subs) == 1 else pathlib.Path(tmp)
        manifest = json.loads((SRC / "pack.json").read_text())
        mine = {"pack.json": (SRC / "pack.json").read_bytes()}
        for f in scene_files(manifest):
            mine[f] = starter_project(f)
        bad = [n for n, data in mine.items() if not (folder / n).is_file() or (folder / n).read_bytes() != data]
        if bad:
            print("differs from the repository's pack: " + ", ".join(bad))
            return 1
        print(f"matches the repository's pack, sha256 {pack_hash(folder)}")
        return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--out", default=str(DEFAULT_DIR / "bench-pack"))
    b.add_argument("--zip", default=None)
    h = sub.add_parser("hash")
    h.add_argument("dir")
    v = sub.add_parser("verify")
    v.add_argument("target")
    sub.add_parser("check")
    a = ap.parse_args()
    if a.cmd == "build":
        version = json.loads((SRC / "pack.json").read_text())["version"]
        return build(a.out, a.zip or DEFAULT_DIR / f"sculptforge-bench-pack-{version}.zip")
    if a.cmd == "hash":
        print(pack_hash(a.dir))
        return 0
    if a.cmd == "check":
        return check()
    return verify(a.target)


if __name__ == "__main__":
    sys.exit(main())
