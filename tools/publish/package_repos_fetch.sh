#!/usr/bin/env bash
# Puts the apt and dnf repositories into the website (pages.yml): the latest release's signed metadata
# (sculptforge-package-repos.tar.gz, made by tools/publish/package_repos.sh in the release workflow) plus the
# release's .deb and .rpm files it names. Every download must match the release's SHA256SUMS.txt.
#   tools/publish/package_repos_fetch.sh TAG WEBSITE_DIR [REPO]
# A release without the metadata (before the signing key exists) leaves the site as it is: exit 0, a note.
set -euo pipefail
tag=$1 site=$2 repo=${3:-islandmed/SculptForge}
base="https://github.com/$repo/releases/download/$tag"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# PACKAGE_REPOS_LOCAL=DIR takes the assets from DIR instead of the release (the test, .github/workflows/repos.yml).
get() {
    if [ -n "${PACKAGE_REPOS_LOCAL:-}" ]; then cp "$PACKAGE_REPOS_LOCAL/$1" "$2"
    else curl -fsSL --retry 4 --retry-all-errors -o "$2" "$base/$1"; fi
}

get SHA256SUMS.txt "$work/SHA256SUMS.txt"
check() { # check <asset name> <file>
    local want
    want=$(awk -v n="$1" '$2 == n || $2 == "*" n {print $1}' "$work/SHA256SUMS.txt")
    [ -n "$want" ] || { echo "package_repos_fetch: $1 is not in $tag's SHA256SUMS.txt" >&2; exit 1; }
    [ "$(sha256sum "$2" | cut -d' ' -f1)" = "$want" ] || { echo "package_repos_fetch: $1 doesn't match SHA256SUMS.txt" >&2; exit 1; }
}
if ! grep -q ' \*\?sculptforge-package-repos.tar.gz$' "$work/SHA256SUMS.txt"; then
    echo "package_repos_fetch: $tag has no package repositories (no REPO_SIGNING_KEY when it was released): skipped"
    exit 0
fi
get sculptforge-package-repos.tar.gz "$work/repos.tar.gz"
check sculptforge-package-repos.tar.gz "$work/repos.tar.gz"
mkdir "$work/x"
tar -C "$work/x" -xzf "$work/repos.tar.gz" --no-same-owner
rm -rf "$site/apt" "$site/rpm"
cp -r "$work/x/apt" "$work/x/rpm" "$site/"
while read -r path asset; do
    case "$path" in apt/*|rpm/*) ;; *) echo "package_repos_fetch: unexpected path $path" >&2; exit 1 ;; esac
    case "$path$asset" in *..*) echo "package_repos_fetch: unexpected path $path" >&2; exit 1 ;; esac
    mkdir -p "$site/$(dirname "$path")"
    get "$asset" "$site/$path"
    check "$asset" "$site/$path"
done < "$work/x/FILES.txt"
echo "package_repos_fetch: $tag's apt and dnf repositories in $site/apt and $site/rpm"
