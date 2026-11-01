#!/bin/sh
# Builds the SculptForge Flatpak from this checkout and writes a single-file bundle,
# SculptForge-<version>-<arch>.flatpak, to the given folder (default: the checkout).
#
#   packaging/flatpak/build.sh [out-dir]
#
# Needs flatpak and flatpak-builder. The freedesktop runtime and SDK come from Flathub (only the runtime: the app
# itself is not published there). Work files go to build-flatpak/ (the manifest leaves it out of the sources).
# The release workflow runs this on x86_64 and aarch64; docs/guide/installing.md has the install side.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=$(mkdir -p "${1:-$root}" && cd "${1:-$root}" && pwd)
id=org.sculptforge.SculptForge
flathub=https://dl.flathub.org/repo/flathub.flatpakrepo
version=$(sed -n 's/^project(SculptForge VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
arch=$(flatpak --default-arch)
work=$root/build-flatpak

flatpak --user remote-add --if-not-exists flathub "$flathub"
# --disable-rofiles-fuse: CI runners and containers often have no FUSE.
# SF_FLATPAK_CCACHE=1: compile through the SDK's ccache, kept in $work/state/ccache (CI restores and saves that folder).
ccache=
[ "${SF_FLATPAK_CCACHE:-}" = 1 ] && ccache=--ccache
flatpak-builder --user --install-deps-from=flathub --force-clean --disable-rofiles-fuse --default-branch=stable $ccache \
  --state-dir="$work/state" --repo="$work/repo" "$work/app" "$here/$id.yml"
bundle="$out/SculptForge-$version-$arch.flatpak"
flatpak build-bundle --runtime-repo="$flathub" "$work/repo" "$bundle" "$id" stable
echo "$bundle"
