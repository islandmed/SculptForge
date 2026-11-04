#!/usr/bin/env bash
# Lists what still carries an old version once a release is live, so the coordinator can remove it:
#   - every GitHub release (page + assets) older than KEEP (tags and git history stay),
#   - every website page, download link or feed entry that still names a version older than KEEP.
# Dry run by default: prints what it would do. --apply deletes the old GitHub releases (not their tags);
# website hits are only listed, fix them in the repo and let pages.yml publish.
#   tools/release/scrub_old_releases.sh 0.10.11            # list
#   tools/release/scrub_old_releases.sh 0.10.11 --apply    # delete old release pages and assets
set -euo pipefail
KEEP=${1:?usage: scrub_old_releases.sh <version to keep, e.g. 0.10.11> [--apply]}
APPLY=${2:-}
REPO=${REPO:-islandmed/SculptForge}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)

older() { # older A B: is version A older than B (sort -V; pre-releases like 0.11.0-beta.1 count as older than 0.11.0)
  [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -1)" = "$1" ]
}

command -v gh >/dev/null || { echo "gh is needed (GitHub CLI)" >&2; exit 1; }
gh release view "v$KEEP" -R "$REPO" >/dev/null 2>&1 || { echo "v$KEEP isn't released yet: nothing removed before it is live" >&2; exit 1; }

echo "== GitHub releases older than v$KEEP ($REPO)"
old=()
while IFS=$'\t' read -r tag; do
  v=${tag#v}
  older "$v" "$KEEP" && old+=("$tag")
done < <(gh release list -R "$REPO" --limit 1000 --json tagName --jq '.[].tagName')
for tag in "${old[@]}"; do
  assets=$(gh release view "$tag" -R "$REPO" --json assets --jq '.assets | length')
  echo "  $tag ($assets assets)"
done
echo "  ${#old[@]} release(s)"

echo "== Website pages, downloads and feeds that name an older version"
hits=0
while IFS= read -r line; do
  file=${line%%:*}; rest=${line#*:}
  for v in $(grep -oE '\b[0-9]+\.[0-9]+\.[0-9]+(-[a-z]+\.[0-9]+)?\b' <<<"$rest" | sort -u); do
    if older "$v" "$KEEP"; then echo "  $file: $v"; hits=$((hits + 1)); fi
  done
done < <(cd "$ROOT" && git grep -n -I -E '\b0\.[0-9]+\.[0-9]+' -- website ':!website/guide/whats-new.html' \
           ':!website/guide/search-index.js' ':!website/guide/images' ':!website/benchmarks' | \
         grep -E 'releases/(download|tag)/v|"version"|softwareVersion|Version [0-9]|New in [0-9]|<enclosure|sparkle:version' || true)
echo "  $hits hit(s) (What's New history and benchmark records keep their versions on purpose)"

if [ "$APPLY" = "--apply" ]; then
  for tag in "${old[@]}"; do
    echo "deleting release $tag (the tag stays)"
    gh release delete "$tag" -R "$REPO" --yes
  done
else
  echo "Dry run. Add --apply to delete the ${#old[@]} old release page(s) and their assets; tags and history stay."
fi
