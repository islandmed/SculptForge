#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks the packages a release is about to publish (release.yml, jobs verify and publish). Refuses on any mismatch.

  verify_staged.py DIST SUMS --run ID --sha SHA [--staged [--ref REF]] [--repo R] [--sums-only] [--require-signed]

1. Checksums: DIST holds exactly the files SUMS lists (SHA256SUMS.staged.txt, written by packages.yml's job sums in
   the run that built them), and each file's SHA-256, computed here, is the one listed.
2. Provenance (skipped with --sums-only): each file has a GitHub artifact attestation (actions/attest-build-provenance
   in packages.yml), checked with `gh attestation verify`: signed by this repository's packages.yml, for exactly SHA,
   in run ID; with --staged also from a push to REF (default refs/heads/staging; refs/heads/main when main's run built
   it). SHA is the commit the run built: the tagged one, or one with the same build-input key (tools/release/staged_run.py).
3. --require-signed (release.yml passes it when the repository variable SIGNING is signpath): the Windows installer,
   and the exes inside the .zip and the .mcpb, carry an Authenticode signature. packages.yml signs and verifies them
   (SignPath), but a staged run built before signing was turned on would otherwise be promoted unsigned, silently.
"""
import argparse
import hashlib
import json
import subprocess
import sys
import zipfile
from pathlib import Path


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def read_sums(text):
    out = {}
    for line in text.splitlines():
        if not line.strip():
            continue
        digest, name = line.split(None, 1)
        out[name.lstrip("*")] = digest.lower()
    return out


def check_sums(dist, sums):
    """Problems: files missing, extra or with another SHA-256 than recorded."""
    have = {p.name for p in Path(dist).iterdir() if p.is_file()}
    errs = [f"{n}: recorded but not downloaded" for n in sorted(set(sums) - have)]
    errs += [f"{n}: not in the recorded checksums" for n in sorted(have - set(sums))]
    errs += [f"{n}: SHA-256 {d} here, {sums[n]} recorded" for n in sorted(have & set(sums))
             if (d := sha256(Path(dist) / n)) != sums[n]]
    return errs


def attested(certs, run, staged):
    """True when one attestation's certificate is from run `run` (and, staged, a push)."""
    for c in certs:
        if f"/actions/runs/{run}/" not in c.get("runInvocationURI", ""):
            continue
        if staged and c.get("buildTrigger") != "push":
            continue
        return True
    return False


def check_attestation(path, repo, run, sha, staged, ref="refs/heads/staging"):
    cmd = ["gh", "attestation", "verify", str(path), "--repo", repo, "--signer-workflow", f"{repo}/.github/workflows/packages.yml",
           "--source-digest", sha, "--format", "json"]
    if staged:
        cmd += ["--source-ref", ref]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        return f"{path.name}: no valid attestation ({(p.stderr or p.stdout).strip().splitlines()[-1:] or ['gh failed']})"
    certs = [r["verificationResult"]["signature"]["certificate"] for r in json.loads(p.stdout)]
    if not attested(certs, run, staged):
        return f"{path.name}: attested, but not by run {run}{' on a push to ' + ref if staged else ''}"
    return None


def pe_signed(data):
    """True when a PE file has an Authenticode certificate table (data directory 4). Presence only: packages.yml's
    Windows job verified the signatures; this catches packages that were never signed."""
    if data[:2] != b"MZ" or len(data) < 0x40:
        return False
    pe = int.from_bytes(data[0x3C:0x40], "little")
    if data[pe:pe + 4] != b"PE\0\0":
        return False
    opt = pe + 24
    entry = opt + (112 if int.from_bytes(data[opt:opt + 2], "little") == 0x20B else 96) + 4 * 8
    return int.from_bytes(data[entry:entry + 4], "little") > 0 and int.from_bytes(data[entry + 4:entry + 8], "little") > 0


def check_signed(dist):
    """Problems: Windows executables (installer, and inside the .zip and .mcpb) without a signature."""
    errs = []
    for p in sorted(Path(dist).iterdir()):
        if "windows" not in p.name:
            continue
        if p.suffix == ".exe":
            with open(p, "rb") as f:
                if not pe_signed(f.read(4096)):
                    errs.append(f"{p.name}: not code-signed (SIGNING=signpath: rebuild the packages, release.sh --rebuild)")
        elif p.suffix in (".zip", ".mcpb"):
            with zipfile.ZipFile(p) as z:
                exes = [n for n in z.namelist() if n.lower().endswith(".exe")]
                if not exes:
                    errs.append(f"{p.name}: holds no .exe to check")
                errs += [f"{p.name}: {n} is not code-signed (SIGNING=signpath: rebuild the packages, release.sh --rebuild)"
                         for n in exes if not pe_signed(z.read(n))]
    return errs


def fake_pe(signed, pe32plus=True):
    """A PE header with just enough in it for pe_signed (self-test)."""
    b = bytearray(0x200)
    b[:2] = b"MZ"
    b[0x3C:0x40] = (0x80).to_bytes(4, "little")
    b[0x80:0x84] = b"PE\0\0"
    opt = 0x80 + 24
    b[opt:opt + 2] = (0x20B if pe32plus else 0x10B).to_bytes(2, "little")
    entry = opt + (112 if pe32plus else 96) + 32
    if signed:
        b[entry:entry + 8] = (0x1000).to_bytes(4, "little") + (0x2000).to_bytes(4, "little")
    return bytes(b)


def self_test():
    import tempfile
    bad = []
    with tempfile.TemporaryDirectory() as d:
        (Path(d) / "a.deb").write_bytes(b"deb")
        (Path(d) / "b.dmg").write_bytes(b"dmg")
        good = {"a.deb": sha256(Path(d) / "a.deb"), "b.dmg": sha256(Path(d) / "b.dmg")}
        cases = {
            "good": (good, 0),
            "changed": ({**good, "b.dmg": "0" * 64}, 1),
            "missing": ({**good, "c.exe": "0" * 64}, 1),
            "extra": ({"a.deb": good["a.deb"]}, 1),
        }
        for name, (sums, n) in cases.items():
            if bool(check_sums(d, sums)) != bool(n):
                bad.append(f"sums {name}: {check_sums(d, sums)}")
    if read_sums("ab  x\ncd *y\n") != {"x": "ab", "y": "cd"}:
        bad.append("read_sums")
    run = "https://github.com/o/r/actions/runs/42/attempts/1"
    certs = {
        "staged push": ([{"runInvocationURI": run, "buildTrigger": "push"}], True, True),
        "other run": ([{"runInvocationURI": run.replace("42", "43"), "buildTrigger": "push"}], True, False),
        "run id prefix": ([{"runInvocationURI": run.replace("42", "421"), "buildTrigger": "push"}], True, False),
        "dispatch": ([{"runInvocationURI": run, "buildTrigger": "workflow_dispatch"}], True, False),
        "rebuild dispatch": ([{"runInvocationURI": run, "buildTrigger": "workflow_dispatch"}], False, True),
        "none": ([], False, False),
    }
    for name, (c, staged, want) in certs.items():
        if attested(c, "42", staged) != want:
            bad.append(f"attested {name}")
    if not (pe_signed(fake_pe(True)) and pe_signed(fake_pe(True, False)) and not pe_signed(fake_pe(False))
            and not pe_signed(b"MZ") and not pe_signed(b"\x7fELF" + bytes(100))):
        bad.append("pe_signed")
    with tempfile.TemporaryDirectory() as d:
        signed, unsigned = fake_pe(True), fake_pe(False)
        for name, (inst, zipped, mcpb, want) in {
            "all signed": (signed, signed, signed, 0),
            "installer unsigned": (unsigned, signed, signed, 1),
            "zip exe unsigned": (signed, unsigned, signed, 1),
            "mcpb exe unsigned": (signed, signed, unsigned, 1),
            "all unsigned": (unsigned, unsigned, unsigned, 3),
        }.items():
            dd = Path(d) / name.replace(" ", "_")
            dd.mkdir()
            (dd / "SculptForge-1-windows-x86_64.exe").write_bytes(inst)
            (dd / "SculptForge-1-linux-x86_64.deb").write_bytes(b"deb")
            for ext, data in (("zip", zipped), ("mcpb", mcpb)):
                with zipfile.ZipFile(dd / f"SculptForge-1-windows-x86_64.{ext}", "w") as z:
                    z.writestr("x/bin/sculptforge.exe", data)
                    z.writestr("x/readme.txt", "text")
            if len(check_signed(dd)) != want:
                bad.append(f"check_signed {name}: {check_signed(dd)}")
        dd = Path(d) / "noexe"
        dd.mkdir()
        with zipfile.ZipFile(dd / "SculptForge-1-windows-x86_64.zip", "w") as z:
            z.writestr("readme.txt", "text")
        if not check_signed(dd):
            bad.append("check_signed: a windows zip without an exe passed")
    for b in bad:
        print("self-test FAIL", b)
    print("self-test", "failed" if bad else "ok")
    return 1 if bad else 0


def main():
    if "--self-test" in sys.argv:
        return self_test()
    ap = argparse.ArgumentParser()
    ap.add_argument("dist")
    ap.add_argument("sums")
    ap.add_argument("--run", required=True)
    ap.add_argument("--sha", required=True)
    ap.add_argument("--repo", default="islandmed/SculptForge")
    ap.add_argument("--staged", action="store_true")
    ap.add_argument("--ref", default="refs/heads/staging", choices=["refs/heads/staging", "refs/heads/main"])
    ap.add_argument("--sums-only", action="store_true")
    ap.add_argument("--require-signed", action="store_true")
    a = ap.parse_args()
    sums = read_sums(Path(a.sums).read_text(encoding="utf-8"))
    errs = ["no recorded checksums"] if not sums else check_sums(a.dist, sums)
    if not errs and not a.sums_only:
        errs = [e for p in sorted(Path(a.dist).iterdir()) if (e := check_attestation(p, a.repo, a.run, a.sha, a.staged, a.ref))]
    if not errs and a.require_signed:
        errs = check_signed(a.dist)
    for e in errs:
        print(f"::error::{e}")
    if errs:
        print(f"REFUSED: {len(errs)} problem(s); nothing is published")
        return 1
    print(f"{len(sums)} files: SHA-256 as recorded by run {a.run}" + ("" if a.sums_only else ", each attested by packages.yml for " + a.sha[:12]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
