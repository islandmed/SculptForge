#!/usr/bin/env bash
# Update resolver for SculptForge.
#
# Prints the latest GitHub release and its two Linux tarballs (x86_64, arm64) as JSON on stdout; logs go to
# stderr. The tarballs are named SculptForge-<version>-linux-<arch>.tar.gz; the -vfx2026/-vfx2027 variants beside
# them are other builds and are not matched.
set -euo pipefail

repo="islandmed/SculptForge"

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing command: $1" >&2; exit 1; }; }
need curl; need jq

rel="$(curl -fsSL ${GITHUB_TOKEN:+-H "Authorization: Bearer $GITHUB_TOKEN"} \
        "https://api.github.com/repos/$repo/releases/latest")"

version="$(jq -r '.tag_name | ltrimstr("v")' <<<"$rel")"
date="$(jq -r '.published_at' <<<"$rel" | cut -c1-10)"
asset() { jq -r --arg re "$1" '.assets[] | select(.name | test($re)) | .browser_download_url' <<<"$rel" | head -n1; }
x64="$(asset '^SculptForge-[0-9][0-9.]*-linux-x86_64\.tar\.gz$')"
arm="$(asset '^SculptForge-[0-9][0-9.]*-linux-arm64\.tar\.gz$')"

[ -n "$version" ] && [ -n "$x64" ] && [ -n "$arm" ] || { echo "failed to resolve the SculptForge release" >&2; exit 1; }
echo "resolved sculptforge $version ($date): $x64 $arm" >&2

jq -n --arg v "$version" --arg d "$date" --arg x "$x64" --arg a "$arm" --arg r "https://github.com/$repo/releases/tag/v$version" \
  '{version:$v, releaseDate:$d, releaseUrl:$r, sources:[{filename:"sculptforge.tar.gz", arch:"x86_64", url:$x},
                                                         {filename:"sculptforge.tar.gz", arch:"aarch64", url:$a}]}'
