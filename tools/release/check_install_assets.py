#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Every asset install/install.sh and install/install.ps1 download exists in the release (#159: install.sh asked for
SculptForge-V-linux-arm64.AppImage while releases ship SculptForge-V-aarch64.AppImage). The scripts are served from
raw main, so a wrong name breaks every curl | sh install at once.

  check_install_assets.py                           ctest (release_install_assets): each name is one release.yml
                                                    copies into the release (its version-less copies step)
  check_install_assets.py --version V --assets F    scripts/release.sh: each name is in F (gh release view's asset list)
  check_install_assets.py --self-test
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SUMS = "SHA256SUMS.txt"


def install_names(sh: str, ps1: str) -> set[str]:
    """The asset names both scripts fetch, with {V} for the version."""
    names = {SUMS}
    arches = re.findall(r"\)\s*ARCH=([\w.-]+)\s*;;", sh)
    for t in re.findall(r"SculptForge-\$V-[\w$.-]+", sh):
        t = t.replace("$V", "{V}")
        names |= {t.replace("$ARCH", a) for a in arches} if "$ARCH" in t else {t}
    names |= {t.replace("$v", "{V}") for t in re.findall(r'Get-Asset "(SculptForge-\$v-[\w.-]+)"', ps1)}
    if any("$" in n for n in names):
        raise SystemExit(f"check_install_assets: unexpanded variable in {sorted(n for n in names if '$' in n)}")
    return names


def release_names(yml: str) -> set[str]:
    """The versioned names release.yml copies from (cp fails the release when one is missing), with {V}."""
    return {t.replace("${V}", "{V}").replace("$V", "{V}") for t in re.findall(r'cp "([^"]*\$\{?V\}?[^"]*)"', yml)} | {SUMS}


def missing(wanted: set[str], have: set[str], version: str = "{V}") -> list[str]:
    return sorted(n.replace("{V}", version) for n in wanted if n.replace("{V}", version) not in have)


def self_test() -> None:
    sh = 'case x in\n  x86_64) ARCH=x86_64 ;;\n  arm64) ARCH=aarch64 ;;\nesac\nA=SculptForge-$V-$ARCH.AppImage\nD=SculptForge-$V-darwin-universal.dmg\n'
    ps1 = 'Get-Asset "SculptForge-$v-windows-x86_64.zip"\nMove-Item "SculptForge-$v-windows-x86_64" d\n'
    names = install_names(sh, ps1)
    assert names == {SUMS, "SculptForge-{V}-x86_64.AppImage", "SculptForge-{V}-aarch64.AppImage",
                     "SculptForge-{V}-darwin-universal.dmg", "SculptForge-{V}-windows-x86_64.zip"}, names
    have = {"SHA256SUMS.txt", "SculptForge-1.2.3-x86_64.AppImage", "SculptForge-1.2.3-aarch64.AppImage",
            "SculptForge-1.2.3-darwin-universal.dmg", "SculptForge-1.2.3-windows-x86_64.zip"}
    assert missing(names, have, "1.2.3") == []
    old = install_names(sh.replace("ARCH=aarch64", "ARCH=arm64").replace("-$ARCH.", "-linux-$ARCH."), ps1)
    assert missing(old, have, "1.2.3") == ["SculptForge-1.2.3-linux-arm64.AppImage",
                                          "SculptForge-1.2.3-linux-x86_64.AppImage"], missing(old, have, "1.2.3")
    yml = 'cp "SculptForge-$V-x86_64.AppImage" a\ncp "sculptforge_${V}_amd64.deb" b\n'
    assert release_names(yml) == {SUMS, "SculptForge-{V}-x86_64.AppImage", "sculptforge_{V}_amd64.deb"}
    print("check_install_assets self-test: ok")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", help="release version without the v, e.g. 1.2.3")
    ap.add_argument("--assets", type=Path, help="file with one release asset name per line")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        self_test()
        return 0
    wanted = install_names((ROOT / "install/install.sh").read_text(), (ROOT / "install/install.ps1").read_text())
    if a.assets:
        if not a.version:
            ap.error("--assets needs --version")
        gone = missing(wanted, set(a.assets.read_text().split()), a.version)
        where = f"release {a.version}"
    else:
        gone = missing(wanted, release_names((ROOT / ".github/workflows/release.yml").read_text()))
        where = "release.yml's packages"
    if gone:
        print(f"install/install.sh or install.ps1 downloads assets not in {where}: {', '.join(gone)}", file=sys.stderr)
        return 1
    print(f"install scripts: {len(wanted)} asset names, all in {where}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
