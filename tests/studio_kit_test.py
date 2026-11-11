#!/usr/bin/env python3
"""The studio kit (packaging/studio-kit) end to end, with a throwaway signing key instead of the release key.

    studio_kit_test.py --cli sculptforge-cli [--app sculptforge --sfui tests/ui/studio/studio_kit.sfui --out DIR]

- The signature, the signed checksums and the pin are checked: a changed byte, signature or pin is refused.
- The same inputs give the same bytes (two builds in different folders, umasks and times), with an SBOM and
  SHA256SUMS.txt for every file.
- The studio folder lands where the unmodified app finds it, and the app's own checks accept its profile and pack.
- With --app: the real app, inside a release archive the kit rebuilt, starts with the overlay (brush, keymap,
  branding) from where it was unpacked, with no --studio flag (the UI test script checks it).
"""
import argparse
import hashlib
import io
import json
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
KIT = ROOT / "packaging" / "studio-kit"

VERSION = "9.9.9"
ARCH = "arm64" if platform.machine().lower() in ("aarch64", "arm64") else "x86_64"
failures = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def run_kit(*args, env=None):
    r = subprocess.run([sys.executable, "-I", str(KIT / "build.py"), *args], capture_output=True, text=True,
                       env=dict(os.environ, **(env or {})))
    return r.returncode, r.stdout + r.stderr


def make_release(release, key, app=None):
    """A release folder as GitHub has it: the archives, an SBOM and the signed SHA256SUMS.txt."""
    release.mkdir(parents=True)
    root = f"SculptForge-{VERSION}-linux-{ARCH}"
    exe = pathlib.Path(app).read_bytes() if app and sys.platform.startswith("linux") else b"#!/bin/sh\necho stub\n"
    with tarfile.open(release / f"{root}.tar.gz", "w:gz") as t:
        for name, data, mode in [(f"{root}/bin/sculptforge", exe, 0o755),
                                 (f"{root}/share/doc/sculptforge/README.md", b"readme\n", 0o644)]:
            info = tarfile.TarInfo(name)
            info.size, info.mode, info.mtime = len(data), mode, 1760000000
            t.addfile(info, io.BytesIO(data))
    win = f"SculptForge-{VERSION}-windows-x86_64"
    wexe = pathlib.Path(app).read_bytes() if app and sys.platform == "win32" else b"MZ stub"
    with zipfile.ZipFile(release / f"{win}.zip", "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(f"{win}/bin/sculptforge.exe", wexe)
        z.writestr(f"{win}/doc/README.md", b"readme\n")
    (release / f"SculptForge-{VERSION}-sbom.cdx.json").write_text(json.dumps(
        {"bomFormat": "CycloneDX", "specVersion": "1.5", "components": [{"type": "library", "name": "SDL 3"}]}))
    sums = "".join(f"{sha(p)}  {p.name}\n" for p in sorted(release.iterdir()))
    (release / "SHA256SUMS.txt").write_text(sums)
    subprocess.run(["openssl", "pkeyutl", "-sign", "-inkey", str(key), "-rawin", "-in", str(release / "SHA256SUMS.txt"),
                    "-out", str(release / "SHA256SUMS.txt.sig")], check=True)


def isolated_env(home):
    home.mkdir(parents=True, exist_ok=True)
    return dict(os.environ, XDG_DATA_HOME=str(home), XDG_CONFIG_HOME=str(home), APPDATA=str(home),
                SF_USER_DATA_DIR=str(home), HOME=str(home) if sys.platform == "darwin" else os.environ.get("HOME", ""),
                SF_KEYCHAIN="none")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--cli")
    p.add_argument("--app")
    p.add_argument("--sfui")
    p.add_argument("--out")
    a = p.parse_args()
    if not shutil.which("openssl"):
        print("SKIP: no openssl")
        return 0
    work = pathlib.Path(tempfile.mkdtemp(prefix="sf-studio-kit-test-"))
    key, pub = work / "key.pem", work / "pub.pem"
    subprocess.run(["openssl", "genpkey", "-algorithm", "ed25519", "-out", str(key)], check=True)
    subprocess.run(["openssl", "pkey", "-in", str(key), "-pubout", "-out", str(pub)], check=True)
    release = work / "release"
    make_release(release, key, a.app)
    overlay = KIT / "example"

    # The kit's own key is the release key: the throwaway signature is refused with it.
    code, out = run_kit("pin", "--release", str(release), "--pin", str(work / "x.json"))
    check(code != 0 and "signature" in out, "a release not signed with the SculptForge key is refused")
    pem = (KIT / "sculptforge-release.pem").read_text()
    der = subprocess.run(["openssl", "pkey", "-pubin", "-outform", "DER"], input=pem.encode(), capture_output=True).stdout
    header = (ROOT / "src" / "core" / "SelfUpdate.h").read_text()
    check(der[-32:].hex() in header, "the kit's key is the app's kUpdatePublicKeyHex")

    pin = work / "pin.json"
    code, out = run_kit("--key", str(pub), "pin", "--release", str(release), "--pin", str(pin))
    check(code == 0 and json.loads(pin.read_text())["version"] == VERSION, f"pin: {out.strip()}")

    builds = []
    for i, (umask, epoch) in enumerate([(0o022, None), (0o077, None)]):
        old = os.umask(umask)
        try:
            out_dir = work / f"dist{i}" / "nested"
            code, out = run_kit("--key", str(pub), "build", "--overlay", str(overlay), "--release", str(release),
                                "--pin", str(pin), "--out", str(out_dir))
        finally:
            os.umask(old)
        check(code == 0, f"build {i}: {out.strip()}")
        builds.append(out_dir)
    if failures:
        return 1
    same = (builds[0] / "SHA256SUMS.txt").read_text() == (builds[1] / "SHA256SUMS.txt").read_text()
    check(same, "two builds of the same inputs are byte-identical")
    dist = builds[0]
    listed = {line.split()[1]: line.split()[0] for line in (dist / "SHA256SUMS.txt").read_text().splitlines()}
    check(all(sha(dist / n) == h for n, h in listed.items()) and len(listed) == 5,
          f"SHA256SUMS.txt covers every output ({', '.join(sorted(listed))})")
    for bom in [n for n in listed if n.endswith(".cdx.json")]:
        b = json.loads((dist / bom).read_text())
        names = [c["name"] for c in b["components"]]
        check(b["bomFormat"] == "CycloneDX" and "SDL 3" in names and "brushes/Studio Ridge.sfbrush" in names
              and any(n.endswith(".sfpack") for n in names), f"{bom}: base components + every overlay file")

    linux = dist / f"SculptForge-{VERSION}-example-linux-{ARCH}.tar.gz"
    with tarfile.open(linux) as t:
        names = t.getnames()
        studio = f"SculptForge-{VERSION}-linux-{ARCH}/share/sculptforge/studio"
        profile = json.loads(t.extractfile(f"{studio}/studio.json").read())
        check(f"{studio}/packs/example-studio.sfpack" in names and f"{studio}/logo.png" in names
              and profile["packs"] == ["packs/example-studio.sfpack"] and profile["branding"]["logo"] == "logo.png",
              "Linux: the studio folder sits at share/sculptforge/studio with its pack and logo")
        check(t.getmember(f"SculptForge-{VERSION}-linux-{ARCH}/bin/sculptforge").mode == 0o755,
              "Linux: the release's own files keep their modes")
    with zipfile.ZipFile(dist / f"SculptForge-{VERSION}-example-windows-x86_64.zip") as z:
        check(f"SculptForge-{VERSION}-windows-x86_64/share/sculptforge/studio/studio.json" in z.namelist(),
              "Windows: the studio folder sits at share/sculptforge/studio")

    # Tampering: a changed archive byte, a changed pin and a broken signature are each refused.
    bad = work / "bad"
    shutil.copytree(release, bad)
    tgz = bad / f"SculptForge-{VERSION}-linux-{ARCH}.tar.gz"
    tgz.write_bytes(tgz.read_bytes() + b"\0")
    code, out = run_kit("--key", str(pub), "build", "--overlay", str(overlay), "--release", str(bad), "--out", str(work / "o"))
    check(code != 0 and "SHA-256" in out, "an archive that isn't the signed one is refused")
    shutil.copy(release / tgz.name, tgz)
    other = json.loads(pin.read_text())
    other["files"][tgz.name] = "0" * 64
    (work / "pin2.json").write_text(json.dumps(other))
    code, out = run_kit("--key", str(pub), "build", "--overlay", str(overlay), "--release", str(bad),
                        "--pin", str(work / "pin2.json"), "--out", str(work / "o"))
    check(code != 0 and "pinned" in out, "an archive that isn't the pinned one is refused")
    sig = bad / "SHA256SUMS.txt.sig"
    sig.write_bytes(bytes([sig.read_bytes()[0] ^ 1]) + sig.read_bytes()[1:])
    code, out = run_kit("--key", str(pub), "build", "--overlay", str(overlay), "--release", str(bad), "--out", str(work / "o"))
    check(code != 0 and "signature" in out, "a broken signature is refused")

    # Overlay mistakes are caught before anything is written.
    for name, change in [("unknown key", lambda o: (o / "studio.json").write_text('{"format": "sculptforge-studio", "version": 1, "id": "x", "name": "X", "typo": 1}')),
                         ("filled key", lambda o: (o / "studio.json").write_text('{"format": "sculptforge-studio", "version": 1, "id": "x", "name": "X", "packs": []}')),
                         ("online", lambda o: (o / "studio.json").write_text('{"format": "sculptforge-studio", "version": 1, "id": "x", "name": "X", "offline": false}')),
                         ("stray folder", lambda o: (o / "scripts").mkdir()),
                         ("wrong extension", lambda o: (o / "brushes" / "x.txt").write_text("{}"))]:
        o = work / ("overlay-" + name.replace(" ", "-"))
        shutil.copytree(overlay, o)
        change(o)
        code, out = run_kit("check", str(o))
        check(code != 0, f"overlay with a {name} is refused ({out.strip().splitlines()[-1] if out.strip() else ''})")

    code, out = run_kit("feed", "--version", VERSION, "--download", "http://x/", "--out", str(work / "l.json"))
    check(code != 0, "feed: a plain-http download link is refused")
    code, out = run_kit("feed", "--version", VERSION, "--download", "https://sw.example.internal/sf/", "--out", str(work / "l.json"))
    check(code == 0 and json.loads((work / "l.json").read_text())["version"] == VERSION, "feed: latest.json for the studio's channel")

    # The app's own checks: the profile loads and its pack installs (sculptforge-cli --studio).
    folder = work / "folder"
    with zipfile.ZipFile(dist / f"SculptForge-{VERSION}-example-studio-folder.zip") as z:
        z.extractall(folder)
    if a.cli:
        r = subprocess.run([a.cli, "--studio", str(folder / "studio" / "studio.json"), "-c", "pack_list"],
                           capture_output=True, text=True, env=isolated_env(work / "cli-home"))
        check(r.returncode == 0 and "Studio pack" not in r.stderr and "example-studio" in r.stdout,
              f"sculptforge-cli accepts the studio profile and installs its pack: {(r.stdout + r.stderr).strip()[:300]}")

    # The real app from the rebuilt release archive, with no --studio flag: it finds the folder itself.
    if a.app and a.sfui and (sys.platform.startswith("linux") or sys.platform == "win32") \
            and (sys.platform.startswith("linux") or platform.machine().lower() in ("x86_64", "amd64")):
        unpacked = work / "unpacked"
        if sys.platform == "win32":
            with zipfile.ZipFile(dist / f"SculptForge-{VERSION}-example-windows-x86_64.zip") as z:
                z.extractall(unpacked)
            exe = unpacked / f"SculptForge-{VERSION}-windows-x86_64" / "bin" / "sculptforge.exe"
            for dll in pathlib.Path(a.app).parent.glob("*.dll"):  # (a build tree's runtime libraries, if any)
                shutil.copy(dll, exe.parent)
        else:
            with tarfile.open(linux) as t:
                t.extractall(unpacked, filter="tar") if hasattr(tarfile, "data_filter") else t.extractall(unpacked)
            exe = unpacked / f"SculptForge-{VERSION}-linux-{ARCH}" / "bin" / "sculptforge"
        shots = pathlib.Path(a.out or work / "shots")
        r = subprocess.run([str(exe), "--ui-test", a.sfui, "--ui-out", str(shots)], capture_output=True, text=True)
        tail = (r.stdout + r.stderr).strip().splitlines()[-15:]
        check(r.returncode == 0, "the rebuilt release starts with the overlay (brush, keymap, branding):\n  " + "\n  ".join(tail))

    if not failures:
        shutil.rmtree(work, ignore_errors=True)
    print(f"{len(failures)} failed" if failures else "all passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
