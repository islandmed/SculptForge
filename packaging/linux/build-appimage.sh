#!/bin/sh
# Builds the AppImage from an installed AppDir (DESTDIR=$PWD/AppDir cmake --install build, prefix /usr).
#
#   packaging/linux/build-appimage.sh APPDIR VERSION ARCH OUTDIR
#
# ARCH is x86_64 or aarch64. Writes OUTDIR/SculptForge-VERSION-ARCH.AppImage and its .zsync (update information
# for AppImageUpdate and similar tools: the newest GitHub release). LINUXDEPLOY and APPIMAGETOOL may name tools
# already downloaded. Otherwise they are fetched with retries into SF_APPIMAGE_TOOLS (CI caches that folder; default
# a temporary one): appimagetool and the AppImage runtime it embeds at tagged releases, pinned by SHA-256 (given with
# --runtime-file, so appimagetool downloads nothing itself: 0.11.1's arm64 AppImage died on a GitHub 504 there),
# linuxdeploy its continuous build.
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
if [ -n "${SF_APPIMAGE_TOOLS:-}" ]; then
  mkdir -p "$SF_APPIMAGE_TOOLS"
  TOOLS=$(cd "$SF_APPIMAGE_TOOLS" && pwd)
else
  TOOLS=$(mktemp -d)
  trap 'rm -rf "$TOOLS"' EXIT
fi

sha256() { sha256sum "$1" | cut -d' ' -f1; }
fetch() { # file name, URL [, SHA-256]: a kept copy when it is there (and matches), else downloaded with retries
  f=$TOOLS/$1
  if [ ! -s "$f" ] || { [ -n "${3:-}" ] && [ "$(sha256 "$f")" != "$3" ]; }; then
    curl -fsSL --retry 6 --retry-delay 15 --retry-all-errors --connect-timeout 30 -o "$f.part" "$2" || { echo "can't download $2" >&2; exit 1; }
    if [ -n "${3:-}" ] && [ "$(sha256 "$f.part")" != "$3" ]; then
      echo "$2: SHA-256 $(sha256 "$f.part"), expected $3" >&2
      exit 1
    fi
    mv "$f.part" "$f"
  fi
  chmod +x "$f"
  echo "$f"
}
case $ARCH in
  x86_64) tool_sum=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
          runtime_sum=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d ;;
  aarch64) tool_sum=f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158
           runtime_sum=00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444 ;;
  *) echo "unknown ARCH $ARCH (x86_64 or aarch64)" >&2; exit 2 ;;
esac
LINUXDEPLOY=${LINUXDEPLOY:-$(fetch linuxdeploy-$ARCH "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage")}
APPIMAGETOOL=${APPIMAGETOOL:-$(fetch appimagetool-1.9.1-$ARCH "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-$ARCH.AppImage" $tool_sum)}
RUNTIME=$(fetch runtime-20251108-$ARCH "https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-$ARCH" $runtime_sum)

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
ARCH=$ARCH VERSION=$VERSION "$APPIMAGETOOL" --appimage-extract-and-run --runtime-file "$RUNTIME" \
  -u "gh-releases-zsync|islandmed|SculptForge|latest|SculptForge-*-$ARCH.AppImage.zsync" "$APPDIR" "$OUT"
[ -f "$OUT" ] && [ -f "$OUT.zsync" ] || { echo "appimagetool made no $OUT (and .zsync)" >&2; exit 1; }
echo "$OUTDIR/$OUT"
