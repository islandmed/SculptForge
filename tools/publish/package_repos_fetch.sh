#!/usr/bin/env bash
# Puts the apt and dnf repositories into the website (pages.yml): the latest release's signed metadata
# (sculptforge-package-repos.tar.gz, made by tools/publish/package_repos.sh in the release workflow) plus the
# release's .deb and .rpm files it names. SHA256SUMS.txt must carry the release signature (SHA256SUMS.txt.sig, Ed25519,
# the key compiled into the app: kUpdatePublicKeyHex in src/core/SelfUpdate.h, the one-click update's check), and
# every download must match it.
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
get SHA256SUMS.txt.sig "$work/SHA256SUMS.txt.sig"
# PACKAGE_REPOS_PUBKEY_HEX overrides the app's key (the test signs with a throwaway one).
pub=${PACKAGE_REPOS_PUBKEY_HEX:-$(sed -n 's/.*kUpdatePublicKeyHex = "\([0-9a-f]\{64\}\)".*/\1/p' \
    "$(dirname "$0")/../../src/core/SelfUpdate.h")}
[ ${#pub} -eq 64 ] || { echo "package_repos_fetch: no release public key (kUpdatePublicKeyHex)" >&2; exit 1; }
# Ed25519 SubjectPublicKeyInfo: the fixed 12-byte DER prefix, then the raw 32-byte key.
python3 -c 'import sys; sys.stdout.buffer.write(bytes.fromhex(sys.argv[1]))' "302a300506032b6570032100$pub" |
    openssl pkey -pubin -inform DER -out "$work/release.pub"
openssl pkeyutl -verify -pubin -inkey "$work/release.pub" -rawin -in "$work/SHA256SUMS.txt" \
    -sigfile "$work/SHA256SUMS.txt.sig" >/dev/null 2>&1 ||
    { echo "package_repos_fetch: $tag's SHA256SUMS.txt is not signed by the release key ($pub)" >&2; exit 1; }
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
# sculptforge.app/sculptforge.asc is committed (website/sculptforge.asc, made by ~/Work/.sf/repo-key.sh): the release's
# metadata must be signed by that same primary key; before it is committed, the release's copy goes there.
fprs() { gpg --batch --with-colons --show-keys "$1" 2>/dev/null | awk -F: '$1 == "pub" {p=1; next} $1 == "fpr" && p {print $10; p=0}'; }
if [ -f "$site/sculptforge.asc" ]; then
    [ -n "$(fprs "$site/sculptforge.asc")" ] && [ "$(fprs "$site/sculptforge.asc")" = "$(fprs "$work/x/apt/sculptforge.asc")" ] ||
        { echo "package_repos_fetch: $tag's repositories are signed by $(fprs "$work/x/apt/sculptforge.asc"), not website/sculptforge.asc's $(fprs "$site/sculptforge.asc")" >&2; exit 1; }
else
    cp "$work/x/apt/sculptforge.asc" "$site/sculptforge.asc"
fi
rm -rf "$site/apt" "$site/rpm"
cp -r "$work/x/apt" "$work/x/rpm" "$site/"
# The committed key (it carries the latest subkey expiry) everywhere apt and dnf fetch it.
cp "$site/sculptforge.asc" "$site/apt/sculptforge.asc"
cp "$site/sculptforge.asc" "$site/rpm/sculptforge.asc"
while read -r path asset; do
    case "$path" in apt/*|rpm/*) ;; *) echo "package_repos_fetch: unexpected path $path" >&2; exit 1 ;; esac
    case "$path$asset" in *..*) echo "package_repos_fetch: unexpected path $path" >&2; exit 1 ;; esac
    mkdir -p "$site/$(dirname "$path")"
    get "$asset" "$site/$path"
    check "$asset" "$site/$path"
done < "$work/x/FILES.txt"
echo "package_repos_fetch: $tag's apt and dnf repositories in $site/apt and $site/rpm"
