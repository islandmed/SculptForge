#!/usr/bin/env python3
"""SculptForge studio kit: your own SculptForge build, made on your machines (README.md).

    build.py check   OVERLAY                          check the overlay, nothing written
    build.py pin     --release DIR [--pin pin.json]   verify a downloaded release and pin its files
    build.py build   --overlay OVERLAY --release DIR [--pin pin.json] [--out dist] [--platform linux-x86_64 ...]
    build.py feed    --version X.Y.Z --download URL [--notes URL] [--out latest.json]
    build.py source  --overlay OVERLAY --src SOURCE_DIR [--out dist]   (build from a tag's source instead)

Nothing here reaches the network: the release files come from a folder you filled (a mirror, a USB stick), and the
only programs it runs are openssl (to check our signature) and, for `source`, cmake. The same inputs give the same
output bytes (same Python and zlib), with a CycloneDX SBOM and SHA256SUMS.txt beside them.
"""
import argparse
import base64
import gzip
import hashlib
import io
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import uuid
import zipfile

KIT = pathlib.Path(__file__).resolve().parent
KEY = KIT / "sculptforge-release.pem"  # the public half of the key that signs every release's SHA256SUMS.txt
PROFILE_KEYS = {"format", "version", "id", "name", "offline", "updates", "allow", "localNetwork", "ai", "packs",
                "brushPaths", "preferences", "branding"}
FILLED = ("packs", "preferences")  # the kit writes these from the overlay's folders
# Overlay folder -> pack item type (docs/pack-format.md) and the extensions it takes. macros/ are the studio's
# scripts: installed with the build, run by an artist from the Macros list, never on their own.
FOLDERS = {
    "brushes": ("brush", (".sfbrush",)),
    "alphas": ("alpha", (".png", ".jpg", ".jpeg")),
    "litspheres": ("litsphere", (".png", ".jpg", ".jpeg")),
    "materials": ("material", (".sfmaterial",)),
    "vdms": ("vdm", (".sfvdm",)),
    "keymaps": ("keymap", (".json",)),
    "piemenus": ("piemenu", (".json",)),
    "macros": ("macro", (".sfmacro",)),
    "plugins": ("plugin", (".sfplugin",)),
}
OTHER = {"studio.json", "LICENSE.txt", "README.md", "preferences", "branding"}
PLATFORMS = {  # release file -> where the studio folder goes inside it
    "linux-x86_64": "SculptForge-{v}-linux-x86_64.tar.gz",
    "linux-arm64": "SculptForge-{v}-linux-arm64.tar.gz",
    "linux-x86_64-vfx2026": "SculptForge-{v}-linux-x86_64-vfx2026.tar.gz",
    "linux-x86_64-vfx2027": "SculptForge-{v}-linux-x86_64-vfx2027.tar.gz",
    "windows-x86_64": "SculptForge-{v}-windows-x86_64.zip",
}
ZIP_EPOCH_MIN = 315532800  # 1980-01-01, the oldest time a zip can hold


class KitError(Exception):
    pass


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


# --- The overlay -----------------------------------------------------------------------------------------------

def name_ok(name):
    # The pack reader's rules, in short: one plain file name, no hidden or Windows-reserved names.
    return (0 < len(name.encode()) <= 120 and not name.startswith((".", " ")) and not name.endswith((".", " "))
            and not re.search(r'[\\/:<>"|?*\x00-\x1f]', name)
            and not re.fullmatch(r"(?i)(con|prn|aux|nul|com\d|lpt\d)(\..*)?", name))


def load_overlay(overlay):
    """Reads and checks OVERLAY; returns (profile dict, [(pack path, type, bytes)], files {studio path: bytes})."""
    overlay = pathlib.Path(overlay)
    errors = []
    text = (overlay / "studio.json").read_text(encoding="utf-8")

    def no_dupes(pairs):
        if len({k for k, _ in pairs}) != len(pairs):
            errors.append("studio.json: a key is given twice")
        return dict(pairs)

    profile = json.loads(text, object_pairs_hook=no_dupes)
    errors += [f'studio.json: unknown key "{k}"' for k in profile if k not in PROFILE_KEYS]
    errors += [f'studio.json: leave "{k}" out, the kit fills it in' for k in FILLED if k in profile]
    if profile.get("format") != "sculptforge-studio" or profile.get("version") != 1:
        errors.append('studio.json: format must be "sculptforge-studio", version 1')
    if not re.fullmatch(r"[a-z0-9_-]{1,56}", str(profile.get("id", ""))):
        errors.append("studio.json: id is 1-56 lower-case letters, digits, '-' and '_'")
    if not 0 < len(str(profile.get("name", ""))) <= 48:
        errors.append("studio.json: name is 1 to 48 characters")
    if profile.get("offline", True) is not True:
        errors.append("studio.json: offline stays true in a studio build")
    if "logo" in profile.get("branding", {}):
        errors.append('studio.json: leave branding.logo out, put the image at branding/logo.png')
    for u in list(profile.get("allow", [])) + [profile.get("updates", {}).get(k, "") for k in ("feed", "mirror")]:
        if u.startswith(("http://", "https://")) and not re.fullmatch(r"https?://[A-Za-z0-9.\-]+(:\d{1,5})?(/[^\s\\@]*)?", u):
            errors.append(f"studio.json: {u!r} is not a plain http(s) URL")
    for p in sorted(overlay.iterdir()):
        if p.name not in FOLDERS and p.name not in OTHER:
            errors.append(f"{p.name}: not part of the overlay layout (README.md)")

    items = []
    for folder, (kind, exts) in FOLDERS.items():
        d = overlay / folder
        if not d.is_dir():
            continue
        for f in sorted(d.iterdir(), key=lambda q: q.name):
            if f.is_symlink() or not f.is_file():
                errors.append(f"{folder}/{f.name}: plain files only (no folders or links)")
            elif not name_ok(f.name) or f.suffix.lower() not in exts:
                errors.append(f"{folder}/{f.name}: a {kind} file is a plain name ending in {' or '.join(exts)}")
            else:
                items.append((f"{folder}/{f.name}", kind, f.read_bytes()))
    names = [i[0].lower() for i in items]
    if len(names) != len(set(names)):
        errors.append("two overlay files differ only in upper/lower case")

    files = {}
    prefs = overlay / "preferences" / "ui.json"
    if prefs.is_file():
        try:
            json.loads(prefs.read_text(encoding="utf-8"))
        except ValueError as e:
            errors.append(f"preferences/ui.json: {e}")
        files["ui.json"] = prefs.read_bytes()
    logo = overlay / "branding" / "logo.png"
    if logo.is_file():
        data = logo.read_bytes()
        if not data.startswith(b"\x89PNG\r\n\x1a\n"):
            errors.append("branding/logo.png: not a PNG")
        files["logo.png"] = data
    lic = overlay / "LICENSE.txt"
    if lic.is_file():
        files["LICENSE.txt"] = lic.read_bytes()
    if errors:
        raise KitError("\n".join(errors))
    return profile, items, files


def stored_zip(entries, epoch):
    """A stored (uncompressed) zip of [(name, bytes)] in the given order: the same bytes on every machine."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_STORED) as z:
        for name, data in entries:
            info = zipfile.ZipInfo(name, date_time=zip_time(epoch))
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            z.writestr(info, data)
    return buf.getvalue()


def zip_time(epoch):
    import time
    return time.gmtime(max(epoch, ZIP_EPOCH_MIN))[:6]


def studio_folder(overlay, epoch):
    """The studio folder the app loads (core/StudioProfile.h): {name: bytes}, plus the overlay's digest."""
    profile, items, files = load_overlay(overlay)
    digest = hashlib.sha256()
    for path, _, data in items:
        digest.update(f"{path}\0{sha256(data)}\n".encode())
    for path in sorted(files):
        digest.update(f"{path}\0{sha256(files[path])}\n".encode())
    digest.update(json.dumps(profile, sort_keys=True).encode())
    tree = digest.hexdigest()
    out = {}
    if items:
        pack_id = f"{profile['id']}-studio"
        manifest = {
            "format": "sculptforge-pack", "formatVersion": 1, "id": pack_id,
            "name": f"{profile['name']} studio"[:64], "version": f"1+{tree[:12]}",  # a new version whenever it changes
            "author": profile["name"][:64], "license": "LicenseRef-Studio" if "LICENSE.txt" in files else "Proprietary",
            "description": "Brushes, keymaps, macros and plugins from your studio's own build.",
            "contents": [{"type": kind, "path": path} for path, kind, _ in items],
        }
        entries = [("pack.json", (json.dumps(manifest, indent=2) + "\n").encode())]
        if "LICENSE.txt" in files:
            entries.append(("LICENSE.txt", files["LICENSE.txt"]))
        entries += [(path, data) for path, _, data in items]
        out[f"packs/{pack_id}.sfpack"] = stored_zip(entries, epoch)
        profile["packs"] = [f"packs/{pack_id}.sfpack"]
    if "ui.json" in files:
        out["ui.json"] = files["ui.json"]
        profile["preferences"] = "ui.json"
    if "logo.png" in files:
        out["logo.png"] = files["logo.png"]
        profile.setdefault("branding", {})["logo"] = "logo.png"
    profile["offline"] = True
    text = json.dumps(profile, indent=2) + "\n"
    if len(text.encode()) > 8192:
        raise KitError("studio.json: over 8 KB once filled in")
    out["studio.json"] = text.encode()
    return out, profile, items, tree


# --- The release -----------------------------------------------------------------------------------------------

def openssl():
    exe = shutil.which("openssl")
    if not exe:
        raise KitError("openssl isn't on PATH: it checks the release signature (Git for Windows ships one)")
    return exe


def verify_release(release, key=KEY):
    """Checks DIR/SHA256SUMS.txt against its signature and returns {file name: sha256}."""
    release = pathlib.Path(release)
    sums, sig = release / "SHA256SUMS.txt", release / "SHA256SUMS.txt.sig"
    if not sums.is_file() or not sig.is_file():
        raise KitError(f"{release}: SHA256SUMS.txt and SHA256SUMS.txt.sig come with every release; copy them too")
    r = subprocess.run([openssl(), "pkeyutl", "-verify", "-pubin", "-inkey", str(key), "-rawin", "-in", str(sums),
                        "-sigfile", str(sig)], capture_output=True, text=True)
    if r.returncode != 0:
        raise KitError(f"{sums}: the signature doesn't match the SculptForge release key ({r.stdout.strip()} "
                       f"{r.stderr.strip()}). Don't use these files.")
    listed = {}
    for line in sums.read_text(encoding="utf-8").splitlines():
        m = re.fullmatch(r"([0-9a-f]{64})\s+\*?(.+)", line.strip())
        if m:
            listed[m.group(2)] = m.group(1)
    return listed


def release_version(listed):
    for name in listed:
        m = re.fullmatch(r"SculptForge-(\d+\.\d+\.\d+(?:-beta\.\d+)?)-linux-(?:x86_64|arm64)\.tar\.gz", name) or \
            re.fullmatch(r"SculptForge-(\d+\.\d+\.\d+(?:-beta\.\d+)?)-windows-x86_64\.zip", name)
        if m:
            return m.group(1)
    raise KitError("SHA256SUMS.txt names no SculptForge release archive")


def checked_file(release, name, listed, pin):
    path = pathlib.Path(release) / name
    if name not in listed:
        raise KitError(f"{name}: not in the signed SHA256SUMS.txt")
    if not path.is_file():
        raise KitError(f"{path}: missing (download it into the release folder)")
    have = sha256_file(path)
    if have != listed[name]:
        raise KitError(f"{name}: its SHA-256 is {have}, the signed list says {listed[name]}. Don't use it.")
    if pin is not None and pin.get("files", {}).get(name) != have:
        raise KitError(f"{name}: not the file pinned in pin.json (run `build.py pin` to move to it on purpose)")
    return path


def cmd_pin(a):
    listed = verify_release(a.release, a.key)
    version = release_version(listed)
    files = {}
    for plat, pattern in PLATFORMS.items():
        name = pattern.format(v=version)
        if (pathlib.Path(a.release) / name).is_file():
            checked_file(a.release, name, listed, None)
            files[name] = listed[name]
    sbom = f"SculptForge-{version}-sbom.cdx.json"
    if (pathlib.Path(a.release) / sbom).is_file():
        checked_file(a.release, sbom, listed, None)
        files[sbom] = listed[sbom]
    if not files:
        raise KitError(f"{a.release}: none of the release archives the kit builds from ({', '.join(PLATFORMS)})")
    pin = {"version": version, "files": dict(sorted(files.items()))}
    pathlib.Path(a.pin).write_text(json.dumps(pin, indent=2) + "\n", encoding="utf-8")
    print(f"pinned SculptForge {version}: {len(files)} files in {a.pin}")


def base_root(names, exe):
    """The archive's top folder, from where bin/sculptforge(.exe) is."""
    for n in names:
        parts = n.rstrip("/").split("/")
        if len(parts) >= 2 and parts[-2] == "bin" and parts[-1] == exe:
            return "/".join(parts[:-2])
    raise KitError(f"no bin/{exe} in the release archive")


def repack_tar(src, folder, epoch):
    out = io.BytesIO()
    with tarfile.open(src, "r:gz") as tin:
        members = tin.getmembers()
        root = base_root([m.name for m in members], "sculptforge")
        studio = f"{root}/share/sculptforge/studio" if root else "share/sculptforge/studio"
        if any(m.name.startswith(studio) for m in members):
            raise KitError("the release archive has a studio folder already")
        if epoch is None:
            epoch = max(int(m.mtime) for m in members)
        raw = io.BytesIO()
        with tarfile.open(fileobj=raw, mode="w", format=tarfile.GNU_FORMAT) as tout:
            for m in members:
                tout.addfile(m, tin.extractfile(m) if m.isfile() else None)
            dirs = sorted({"/".join(p.split("/")[:i]) for p in folder for i in range(1, p.count("/") + 1)})
            for d in [""] + dirs:
                t = tarfile.TarInfo(f"{studio}/{d}".rstrip("/"))
                t.type, t.mode, t.mtime = tarfile.DIRTYPE, 0o755, epoch
                tout.addfile(t)
            for path in sorted(folder):
                t = tarfile.TarInfo(f"{studio}/{path}")
                t.size, t.mode, t.mtime = len(folder[path]), 0o644, epoch
                tout.addfile(t, io.BytesIO(folder[path]))
    with gzip.GzipFile(fileobj=out, mode="wb", compresslevel=9, mtime=0, filename="") as gz:
        gz.write(raw.getvalue())
    return out.getvalue(), studio


def repack_zip(src, folder, epoch):
    out = io.BytesIO()
    with zipfile.ZipFile(src) as zin:
        infos = zin.infolist()
        root = base_root([i.filename for i in infos], "sculptforge.exe")
        studio = f"{root}/share/sculptforge/studio" if root else "share/sculptforge/studio"
        if any(i.filename.startswith(studio) for i in infos):
            raise KitError("the release archive has a studio folder already")
        if epoch is None:
            import calendar
            epoch = max(calendar.timegm(i.date_time + (0, 0, -1)) for i in infos)
        with zipfile.ZipFile(out, "w") as zout:
            for i in infos:
                zout.writestr(i, zin.read(i), compress_type=i.compress_type, compresslevel=9)
            for path in sorted(folder):
                info = zipfile.ZipInfo(f"{studio}/{path}", date_time=zip_time(epoch))
                info.create_system, info.external_attr, info.compress_type = 0, 0x20, zipfile.ZIP_DEFLATED
                zout.writestr(info, folder[path], compresslevel=9)
    return out.getvalue(), studio


def sbom(name, version, profile, output_sha, items, folder, base_name, base_sha, base_sbom, epoch, tree):
    """CycloneDX 1.5: the build, the release it came from (with its own components) and every overlay file."""
    import time
    components = [{
        "type": "application", "bom-ref": "base", "name": "SculptForge", "version": version,
        "licenses": [{"license": {"id": "GPL-3.0-or-later"}}],
        "hashes": [{"alg": "SHA-256", "content": base_sha}],
        "externalReferences": [{"type": "distribution", "url": f"https://github.com/islandmed/SculptForge/releases/tag/v{version}",
                                "comment": base_name}],
    }]
    if base_sbom:
        for c in json.loads(base_sbom).get("components", []):
            c = dict(c)
            c.pop("bom-ref", None)
            components.append(c)
    for path, kind, data in items:
        components.append({"type": "data", "name": path, "group": f"overlay/{kind}",
                           "hashes": [{"alg": "SHA-256", "content": sha256(data)}]})
    for path in sorted(folder):
        components.append({"type": "file", "name": f"studio/{path}", "group": "studio-folder",
                           "hashes": [{"alg": "SHA-256", "content": sha256(folder[path])}]})
    stamp = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(epoch))
    return json.dumps({
        "bomFormat": "CycloneDX", "specVersion": "1.5", "version": 1,
        "serialNumber": f"urn:uuid:{uuid.uuid5(uuid.NAMESPACE_URL, 'sculptforge-studio:' + output_sha)}",
        "metadata": {
            "timestamp": stamp,
            "tools": [{"vendor": "SculptForge", "name": "studio-kit"}],
            "component": {"type": "application", "name": name, "version": f"{version}+{profile['id']}",
                          "hashes": [{"alg": "SHA-256", "content": output_sha}],
                          "licenses": [{"license": {"id": "GPL-3.0-or-later"}}]},
            "properties": [{"name": "sculptforge:studio", "value": profile["id"]},
                           {"name": "sculptforge:overlay-sha256", "value": tree}],
        },
        "components": components,
    }, indent=2) + "\n"


def write_sums(out, names):
    lines = [f"{sha256_file(out / n)}  {n}\n" for n in sorted(names)]
    (out / "SHA256SUMS.txt").write_text("".join(lines), encoding="utf-8")


def epoch_from_env():
    v = os.environ.get("SOURCE_DATE_EPOCH")
    return int(v) if v else None


def cmd_build(a):
    listed = verify_release(a.release, a.key)
    pin = json.loads(pathlib.Path(a.pin).read_text(encoding="utf-8")) if a.pin else None
    version = pin["version"] if pin else release_version(listed)
    platforms = a.platform or [p for p in PLATFORMS if (pathlib.Path(a.release) / PLATFORMS[p].format(v=version)).is_file()]
    if not platforms:
        raise KitError(f"{a.release}: no release archive to build from")
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    epoch = epoch_from_env()
    folder, profile, items, tree = studio_folder(a.overlay, epoch or ZIP_EPOCH_MIN)
    sbom_name = f"SculptForge-{version}-sbom.cdx.json"
    base_sbom = None
    if sbom_name in listed and (pathlib.Path(a.release) / sbom_name).is_file():
        base_sbom = checked_file(a.release, sbom_name, listed, pin).read_text(encoding="utf-8")
    written = []
    for plat in platforms:
        base_name = PLATFORMS[plat].format(v=version)
        src = checked_file(a.release, base_name, listed, pin)
        data, studio = (repack_zip if base_name.endswith(".zip") else repack_tar)(src, folder, epoch)
        name = base_name.replace(f"SculptForge-{version}-", f"SculptForge-{version}-{profile['id']}-")
        (out / name).write_bytes(data)
        bom = name.rsplit(".tar.gz", 1)[0].rsplit(".zip", 1)[0] + ".cdx.json"
        (out / bom).write_text(sbom(name, version, profile, sha256(data), items, folder, base_name, listed[base_name],
                                    base_sbom, epoch or ZIP_EPOCH_MIN, tree), encoding="utf-8")
        written += [name, bom]
        print(f"{name}: studio folder at {studio}/")
    # The folder on its own, for macOS (beside SculptForge.app) or any install you already deploy.
    name = f"SculptForge-{version}-{profile['id']}-studio-folder.zip"
    (out / name).write_bytes(stored_zip([(f"studio/{p}", folder[p]) for p in sorted(folder)], epoch or ZIP_EPOCH_MIN))
    written.append(name)
    write_sums(out, written)
    print(f"{out}/SHA256SUMS.txt: {len(written)} files")


def cmd_check(a):
    folder, profile, items, tree = studio_folder(a.overlay, ZIP_EPOCH_MIN)
    kinds = {}
    for _, kind, _ in items:
        kinds[kind] = kinds.get(kind, 0) + 1
    print(f"{profile['name']} ({profile['id']}): " + (", ".join(f"{n} {k}" for k, n in sorted(kinds.items())) or "no pack items")
          + ("; preferences" if "ui.json" in folder else "") + ("; logo" if "logo.png" in folder else "")
          + f"; overlay {tree[:12]}")


def cmd_feed(a):
    if not a.download.startswith("https://"):
        raise KitError("--download: an https:// link (the app only follows https)")
    feed = {"version": a.version, "download": a.download}
    if a.notes:
        feed["notes"] = a.notes
    pathlib.Path(a.out).write_text(json.dumps(feed, indent=2) + "\n", encoding="utf-8")
    print(f"{a.out}: SculptForge {a.version} -> {a.download}")


def cmd_source(a):
    """A build from source with the profile compiled in (CMakeLists.txt SCULPTFORGE_STUDIO_PROFILE)."""
    epoch = epoch_from_env() or ZIP_EPOCH_MIN
    folder, profile, _, _ = studio_folder(a.overlay, epoch)
    work = pathlib.Path(tempfile.mkdtemp(prefix="sf-studio-"))
    for path, data in folder.items():
        (work / "studio" / path).parent.mkdir(parents=True, exist_ok=True)
        (work / "studio" / path).write_bytes(data)
    build, out = work / "build", pathlib.Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, SOURCE_DATE_EPOCH=str(epoch))
    gen = "ZIP" if sys.platform == "win32" else "TGZ"
    subprocess.run(["cmake", "-S", a.src, "-B", str(build), "-DCMAKE_BUILD_TYPE=Release", "-DSCULPTFORGE_BUILD_TESTS=OFF",
                    "-DSCULPTFORGE_UPDATE_CHECK=ON", f"-DSCULPTFORGE_STUDIO_PROFILE={work / 'studio' / 'studio.json'}",
                    f"-DCPACK_PACKAGE_FILE_NAME=SculptForge-{profile['id']}", f"-DCPACK_PACKAGE_DIRECTORY={out}"],
                   check=True, env=env)
    subprocess.run(["cmake", "--build", str(build), "--config", "Release", "--parallel"], check=True, env=env)
    subprocess.run(["cpack", "-G", gen, "-C", "Release", "-B", str(out)], cwd=build, check=True, env=env)
    names = [p.name for p in out.iterdir() if p.is_file() and p.name != "SHA256SUMS.txt"]
    write_sums(out, names)
    print(f"{out}: {', '.join(sorted(names))}")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--key", default=str(KEY), help="the release public key (PEM); default: the kit's own")
    sub = p.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("overlay")
    c = sub.add_parser("pin")
    c.add_argument("--release", required=True)
    c.add_argument("--pin", default="pin.json")
    c = sub.add_parser("build")
    c.add_argument("--overlay", required=True)
    c.add_argument("--release", required=True)
    c.add_argument("--pin")
    c.add_argument("--out", default="dist")
    c.add_argument("--platform", action="append", choices=sorted(PLATFORMS))
    c = sub.add_parser("feed")
    c.add_argument("--version", required=True)
    c.add_argument("--download", required=True)
    c.add_argument("--notes")
    c.add_argument("--out", default="latest.json")
    c = sub.add_parser("source")
    c.add_argument("--overlay", required=True)
    c.add_argument("--src", required=True)
    c.add_argument("--out", default="dist")
    a = p.parse_args(argv)
    try:
        {"check": cmd_check, "pin": cmd_pin, "build": cmd_build, "feed": cmd_feed, "source": cmd_source}[a.cmd](a)
    except (KitError, OSError, ValueError, tarfile.TarError, zipfile.BadZipFile) as e:
        print(f"build.py {a.cmd}: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
