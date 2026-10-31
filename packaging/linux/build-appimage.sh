#!/bin/sh
# Builds the AppImage from an installed AppDir (DESTDIR=$PWD/AppDir cmake --install build, prefix /usr).
#
#   packaging/linux/build-appimage.sh APPDIR VERSION ARCH OUTDIR
#
# ARCH is x86_64 or aarch64. Writes OUTDIR/SculptForge-VERSION-ARCH.AppImage and its .zsync (update information
# for AppImageUpdate and similar tools: the newest GitHub release). LINUXDEPLOY and APPIMAGETOOL may name tools
# already downloaded; otherwise the continuous builds are fetched.
#
# linuxdeploy bundles the libraries. The AppDir's root then gets real PNG icons, not links to the SVG that
# linuxdeploy picks: AppImage managers (AppManager, Gear Lever) and thumbnailers read the root icon and .DirIcon
# and cannot use an SVG or a link into usr/, and appimagetool refuses an SVG .DirIcon (#17).
set -eu
[ $# -eq 4 ] || { echo "usage: $0 APPDIR VERSION ARCH OUTDIR" >&2; exit 2; }
APPDIR=$(cd "$1" && pwd)
VERSION=$2
ARCH=$3
mkdir -p "$4"
OUTDIR=$(cd "$4" && pwd)
SRC=$(cd "$(dirname "$0")/../.." && pwd)
ID=org.sculptforge.SculptForge
TOOLS=$(mktemp -d)
trap 'rm -rf "$TOOLS"' EXIT

fetch() { # tool name, URL
  curl -fsSLo "$TOOLS/$1" "$2"
  chmod +x "$TOOLS/$1"
  echo "$TOOLS/$1"
}
LINUXDEPLOY=${LINUXDEPLOY:-$(fetch linuxdeploy "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage")}
APPIMAGETOOL=${APPIMAGETOOL:-$(fetch appimagetool "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage")}

# 1. Libraries, AppRun, the root .desktop file (no AppImage yet).
"$LINUXDEPLOY" --appimage-extract-and-run --appdir "$APPDIR" \
  --desktop-file "$APPDIR/usr/share/applications/$ID.desktop" \
  --icon-file "$SRC/assets/branding/sculptforge-512.png" --icon-filename "$ID"

# 2. The root icon and .DirIcon: real 512 px PNG files.
rm -f "$APPDIR/$ID.svg" "$APPDIR/$ID.png" "$APPDIR/.DirIcon"
cp "$SRC/assets/branding/sculptforge-512.png" "$APPDIR/$ID.png"
cp "$SRC/assets/branding/sculptforge-512.png" "$APPDIR/.DirIcon"
for f in "$APPDIR/$ID.png" "$APPDIR/.DirIcon"; do
  if [ -L "$f" ] || ! file -b "$f" | grep -q '^PNG image data, 512 x 512'; then
    echo "$f is not a 512 px PNG file" >&2
    exit 1
  fi
done

# 3. The AppImage. appimagetool checks the AppDir first (.DirIcon, the .desktop file) and stops on a problem.
cd "$OUTDIR"
OUT="SculptForge-$VERSION-$ARCH.AppImage"
ARCH=$ARCH VERSION=$VERSION "$APPIMAGETOOL" --appimage-extract-and-run \
  -u "gh-releases-zsync|islandmed|SculptForge|latest|SculptForge-*-$ARCH.AppImage.zsync" "$APPDIR" "$OUT"
[ -f "$OUT" ] && [ -f "$OUT.zsync" ] || { echo "appimagetool made no $OUT (and .zsync)" >&2; exit 1; }
echo "$OUTDIR/$OUT"
