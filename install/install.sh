#!/bin/sh
# Installs the latest SculptForge release for the current user (no admin rights needed).
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   curl -fsSL https://raw.githubusercontent.com/islandmed/SculptForge/main/install/install.sh | sh
#   curl -fsSL .../install.sh | sh -s -- --version v0.3.0     a specific release
#   curl -fsSL .../install.sh | sh -s -- --uninstall
#
# Linux: the AppImage, unpacked into ~/.local/share/sculptforge (no FUSE needed), with `sculptforge` and
#        `sculptforge-cli` in ~/.local/bin and a menu entry.
# macOS: SculptForge.app into /Applications (or ~/Applications), `sculptforge-cli` in ~/.local/bin.
# Every download is checked against the release's SHA256SUMS.txt.
set -eu

REPO=islandmed/SculptForge
VERSION=
UNINSTALL=0
while [ $# -gt 0 ]; do
  case $1 in
    --version) VERSION=$2; shift 2 ;;
    --uninstall) UNINSTALL=1; shift ;;
    *) echo "install.sh: unknown option $1" >&2; exit 1 ;;
  esac
done

BIN=${XDG_BIN_HOME:-$HOME/.local/bin}
DATA=${XDG_DATA_HOME:-$HOME/.local/share}
say() { printf '%s\n' "SculptForge: $*"; }
die() { printf '%s\n' "SculptForge: $*" >&2; exit 1; }

OS=$(uname -s)
case $OS in
  Linux|Darwin) ;;
  *) die "this script is for Linux and macOS; on Windows run: irm https://raw.githubusercontent.com/$REPO/main/install/install.ps1 | iex" ;;
esac

if [ $UNINSTALL = 1 ]; then
  rm -rf "$DATA/sculptforge" "$BIN/sculptforge" "$BIN/sculptforge-cli" \
    "$DATA/applications/org.sculptforge.SculptForge.desktop" \
    "$DATA/icons/hicolor/256x256/apps/org.sculptforge.SculptForge.png"
  [ $OS = Darwin ] && rm -rf /Applications/SculptForge.app "$HOME/Applications/SculptForge.app"
  say "uninstalled (your settings and brushes are kept)"
  exit 0
fi

command -v curl >/dev/null || die "needs curl"
if [ -z "$VERSION" ]; then # the tag that releases/latest redirects to
  VERSION=$(curl -fsSLI -o /dev/null -w '%{url_effective}' "https://github.com/$REPO/releases/latest")
  VERSION=${VERSION##*/}
fi
case $VERSION in v[0-9]*) ;; *) die "could not find the latest release (got '$VERSION')" ;; esac
V=${VERSION#v}
URL=https://github.com/$REPO/releases/download/$VERSION

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fetch() { # asset name -> $TMP/name, checked against SHA256SUMS.txt
  say "downloading $1"
  curl -fL --progress-bar -o "$TMP/$1" "$URL/$1" || die "download failed: $URL/$1"
  [ -f "$TMP/SHA256SUMS.txt" ] || curl -fsSL -o "$TMP/SHA256SUMS.txt" "$URL/SHA256SUMS.txt"
  want=$(grep " $1\$" "$TMP/SHA256SUMS.txt" | cut -d' ' -f1)
  [ -n "$want" ] || die "$1 is not in SHA256SUMS.txt"
  if command -v sha256sum >/dev/null; then got=$(sha256sum "$TMP/$1" | cut -d' ' -f1)
  else got=$(shasum -a 256 "$TMP/$1" | cut -d' ' -f1); fi
  [ "$got" = "$want" ] || die "$1: checksum mismatch, not installing"
}

mkdir -p "$BIN"
if [ $OS = Linux ]; then
  case $(uname -m) in
    x86_64|amd64) ARCH=x86_64 ;;
    aarch64|arm64) ARCH=aarch64 ;;
    *) die "no Linux build for $(uname -m) yet; see https://github.com/$REPO#building" ;;
  esac
  APPIMAGE=SculptForge-$V-$ARCH.AppImage # the AppImage way: x86_64 or aarch64, no "linux" (#159)
  fetch "$APPIMAGE"
  chmod +x "$TMP/$APPIMAGE"
  (cd "$TMP" && "./$APPIMAGE" --appimage-extract >/dev/null) || die "could not unpack $APPIMAGE"
  rm -rf "$DATA/sculptforge"
  mkdir -p "$DATA"
  mv "$TMP/squashfs-root" "$DATA/sculptforge"
  ln -sf "$DATA/sculptforge/AppRun" "$BIN/sculptforge"
  ln -sf "$DATA/sculptforge/usr/bin/sculptforge-cli" "$BIN/sculptforge-cli"
  mkdir -p "$DATA/applications" "$DATA/icons/hicolor/256x256/apps"
  cp "$DATA/sculptforge/usr/share/icons/hicolor/256x256/apps/org.sculptforge.SculptForge.png" \
    "$DATA/icons/hicolor/256x256/apps/"
  sed "s|^Exec=.*|Exec=$DATA/sculptforge/AppRun %f|" \
    "$DATA/sculptforge/usr/share/applications/org.sculptforge.SculptForge.desktop" \
    >"$DATA/applications/org.sculptforge.SculptForge.desktop"
  command -v update-desktop-database >/dev/null && update-desktop-database -q "$DATA/applications" 2>/dev/null || true
else
  DMG=SculptForge-$V-darwin-universal.dmg
  fetch "$DMG"
  hdiutil attach -nobrowse -quiet -mountpoint "$TMP/dmg" "$TMP/$DMG" || die "could not open $DMG"
  DEST=/Applications
  [ -w "$DEST" ] || { DEST=$HOME/Applications; mkdir -p "$DEST"; }
  rm -rf "$DEST/SculptForge.app"
  cp -R "$TMP/dmg/SculptForge.app" "$DEST/"
  hdiutil detach -quiet "$TMP/dmg" || true
  ln -sf "$DEST/SculptForge.app/Contents/MacOS/sculptforge-cli" "$BIN/sculptforge-cli"
  say "installed $DEST/SculptForge.app"
fi

say "installed $VERSION"
case :$PATH: in
  *:"$BIN":*) ;;
  *) say "add $BIN to your PATH to use sculptforge-cli from a terminal" ;;
esac
