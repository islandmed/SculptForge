#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""The winget manifests of a release (scripts/release.sh step winget).

  tools/release/winget_manifest.py render X.Y.Z --sha SHA256 --date YYYY-MM-DD [--src packaging/winget] --out DIR
  tools/release/winget_manifest.py --self-test

packaging/winget/ holds the three manifests (installer, locale en-US, version) that microsoft/winget-pkgs takes under
manifests/s/SculptForge/SculptForge/X.Y.Z/. `render` copies them to DIR with what changes per release: PackageVersion,
the installer's URL (the release's NSIS setup .exe), its checksum (upper-case, from the release's SHA256SUMS.txt), the
ReleaseDate (the release's publishedAt date, UTC) and the ReleaseNotesUrl (the tag). Every line it changes must be there
exactly once, so a template that drifted from the shape stops the step instead of shipping a wrong manifest.
"""
import os
import re
import sys
import tempfile

REPO_URL = "https://github.com/islandmed/SculptForge"
IDENTIFIER = "SculptForge.SculptForge"
FILES = {  # file name -> {key: value builder}
    f"{IDENTIFIER}.installer.yaml": ("InstallerUrl", "InstallerSha256", "ReleaseDate", "PackageVersion"),
    f"{IDENTIFIER}.locale.en-US.yaml": ("ReleaseNotesUrl", "PackageVersion"),
    f"{IDENTIFIER}.yaml": ("PackageVersion",),
}


def values(version, sha, date):
    return {
        "PackageVersion": version,
        "InstallerUrl": f"{REPO_URL}/releases/download/v{version}/SculptForge-{version}-windows-x86_64.exe",
        "InstallerSha256": sha.upper(),
        "ReleaseDate": date,
        "ReleaseNotesUrl": f"{REPO_URL}/releases/tag/v{version}",
    }


def render_text(text, keys, vals, name):
    for key in keys:
        # a list item's first key is "- Architecture: x64": the keys we set are indented or at the start of a line
        pat = re.compile(rf"^(\s*(?:- )?{key}: ).*$", re.M)
        if len(pat.findall(text)) != 1:
            raise ValueError(f"{name}: expected exactly one {key} line")
        text = pat.sub(lambda m: m.group(1) + vals[key], text)
    return text


def render(version, sha, date, src, out):
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError(f"not a stable version: {version}")
    if not re.fullmatch(r"[0-9a-fA-F]{64}", sha):
        raise ValueError(f"not a SHA-256: {sha}")
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", date):
        raise ValueError(f"not a date YYYY-MM-DD: {date}")
    vals = values(version, sha, date)
    os.makedirs(out, exist_ok=True)
    for name, keys in FILES.items():
        with open(os.path.join(src, name), encoding="utf-8", newline="") as f:
            text = f.read()
        with open(os.path.join(out, name), "w", encoding="utf-8", newline="") as f:
            f.write(render_text(text, keys, vals, name))


def self_test():
    here = os.path.dirname(os.path.abspath(__file__))
    src = os.path.join(here, "..", "..", "packaging", "winget")
    sha = "ab" * 32
    with tempfile.TemporaryDirectory() as tmp:
        render("9.8.7", sha, "2031-02-03", src, tmp)
        got = {n: open(os.path.join(tmp, n), encoding="utf-8").read() for n in FILES}
        inst = got[f"{IDENTIFIER}.installer.yaml"]
        assert "InstallerUrl: " + REPO_URL + "/releases/download/v9.8.7/SculptForge-9.8.7-windows-x86_64.exe\n" in inst, inst
        assert "InstallerSha256: " + sha.upper() + "\n" in inst, inst
        assert "ReleaseDate: 2031-02-03\n" in inst, inst
        assert "ReleaseNotesUrl: " + REPO_URL + "/releases/tag/v9.8.7\n" in got[f"{IDENTIFIER}.locale.en-US.yaml"]
        for n, t in got.items():
            assert re.findall(r"^PackageVersion: (.*)$", t, re.M) == ["9.8.7"], n
            assert "ManifestVersion: 1.6.0" in t, n
        # only the per-release lines differ from the template; the rest (the wording check_marks.py scans) is untouched
        for n in FILES:
            old = open(os.path.join(src, n), encoding="utf-8").read().splitlines()
            new = got[n].splitlines()
            assert len(old) == len(new), n
            changed = [a.split(":")[0].strip("- ") for a, b in zip(old, new) if a != b]
            assert set(changed) <= {"PackageVersion", "InstallerUrl", "InstallerSha256", "ReleaseDate", "ReleaseNotesUrl"}, changed
        # rendering the result again with the same inputs is a no-op (the step skips a manifest already current)
        again = os.path.join(tmp, "again")
        render("9.8.7", sha, "2031-02-03", tmp, again)
        for n in FILES:
            assert open(os.path.join(again, n), encoding="utf-8").read() == got[n], n
        # a bad input or a template that lost a line stops
        for bad in (("9.8", sha, "2031-02-03"), ("9.8.7", "xyz", "2031-02-03"), ("9.8.7", sha, "yesterday"), ("9.8.7-beta.1", sha, "2031-02-03")):
            try:
                render(*bad, src, os.path.join(tmp, "bad"))
            except ValueError:
                continue
            raise AssertionError(f"accepted {bad}")
        os.makedirs(os.path.join(tmp, "broken"))
        for n in FILES:
            t = open(os.path.join(src, n), encoding="utf-8").read()
            if n.endswith("installer.yaml"):
                t = t.replace("InstallerSha256", "Sha")
            open(os.path.join(tmp, "broken", n), "w", encoding="utf-8").write(t)
        try:
            render("9.8.7", sha, "2031-02-03", os.path.join(tmp, "broken"), os.path.join(tmp, "bad2"))
        except ValueError as e:
            assert "InstallerSha256" in str(e), e
        else:
            raise AssertionError("a template without InstallerSha256 rendered")
    print("winget_manifest self-test: ok")


def main(argv):
    if argv[1:] == ["--self-test"]:
        self_test()
        return 0
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("render")
    r.add_argument("version")
    r.add_argument("--sha", required=True)
    r.add_argument("--date", required=True)
    r.add_argument("--src", default="packaging/winget")
    r.add_argument("--out", required=True)
    a = ap.parse_args(argv[1:])
    try:
        render(a.version, a.sha, a.date, a.src, a.out)
    except (ValueError, OSError) as e:
        print(f"winget_manifest: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
