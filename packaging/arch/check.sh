#!/usr/bin/env bash
# The AUR package, checked as the AUR expects: .SRCINFO current, namcap clean, built in a clean chroot of this
# machine's architecture (devtools). CI does the same on x86_64 in .github/workflows/arch.yml.
#   packaging/arch/check.sh        (needs: sudo pacman -S --needed devtools namcap; the chroot asks for sudo)
set -euo pipefail
cd "$(dirname "$0")"
makepkg --printsrcinfo | diff -u .SRCINFO - || { echo "check: .SRCINFO is stale: makepkg --printsrcinfo > .SRCINFO"; exit 1; }
namcap -i PKGBUILD
chroot=${SF_ARCH_CHROOT:-$HOME/.cache/sculptforge-archroot}
[ -d "$chroot/root" ] || { mkdir -p "$chroot"; mkarchroot "$chroot/root" base-devel; }
makechrootpkg -c -r "$chroot"
namcap -i ./sculptforge-*.pkg.tar.*
echo "check: PKGBUILD builds in a clean chroot; namcap output above (E: lines are errors)"
