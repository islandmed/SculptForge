#!/usr/bin/env bash
# Signed apt and dnf repository metadata for one release's .deb and .rpm files (design: docs/design/package-repos.md).
#   tools/publish/package_repos.sh DIST OUT.tar.gz
# DIST holds the release's versioned sculptforge_<v>_<arch>.deb and sculptforge-<v>-<rel>.<arch>.rpm. Writes
# OUT.tar.gz with the metadata only (no packages):
#   apt/dists/stable/{InRelease,Release,Release.gpg,main/binary-{amd64,arm64}/Packages{,.gz}}, apt/sculptforge.asc
#   rpm/repodata/* (repomd.xml signed: repomd.xml.asc), rpm/sculptforge.repo, rpm/sculptforge.asc
#   FILES.txt: "<path on the site> <release asset name>" for every package the metadata names
# website/apt and website/rpm are put together from it by tools/publish/package_repos_fetch.sh (pages.yml), which
# downloads those assets. The signature covers each package's SHA-256, so apt and dnf refuse any other bytes.
# Signing key: REPO_SIGNING_KEY (armored OpenPGP private key) and REPO_SIGNING_PASSPHRASE in the environment, as
# the release workflow's `release` environment provides them. Needs apt-ftparchive (apt-utils), createrepo_c, gpg.
set -euo pipefail
dist=$(cd "$1" && pwd)
out=$(realpath -m "$2")
[ -n "${REPO_SIGNING_KEY:-}" ] || { echo "package_repos: REPO_SIGNING_KEY is empty" >&2; exit 2; }
for t in apt-ftparchive createrepo_c gpg; do command -v "$t" >/dev/null || { echo "package_repos: $t missing" >&2; exit 2; }; done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export GNUPGHOME="$work/gnupg"
mkdir -m 700 "$GNUPGHOME"
printf '%s\n' "$REPO_SIGNING_KEY" | gpg --batch --quiet --import
fpr=$(gpg --batch --with-colons --list-secret-keys | awk -F: '$1 == "fpr" {print $10; exit}')
[ -n "$fpr" ] || { echo "package_repos: REPO_SIGNING_KEY holds no secret key" >&2; exit 2; }
# The passphrase from a 0600 file in the 0700 work folder, never on a command line (the runner's process list).
(umask 077; printf '%s' "${REPO_SIGNING_PASSPHRASE:-}" > "$work/passphrase")
sign() { gpg --batch --yes --quiet --pinentry-mode loopback --passphrase-file "$work/passphrase" \
    --digest-algo SHA512 --local-user "$fpr" "$@"; }

shopt -s nullglob
debs=("$dist"/sculptforge_[0-9]*_*.deb)
rpms=("$dist"/sculptforge-[0-9]*.rpm)
[ ${#debs[@]} -gt 0 ] && [ ${#rpms[@]} -gt 0 ] || { echo "package_repos: no versioned .deb or .rpm in $dist" >&2; exit 2; }
root="$work/out"
mkdir -p "$root/apt" "$root/rpm"
: > "$root/FILES.txt"

# apt: one suite (stable), one component (main), the packages in pool/.
pool=pool/main/s/sculptforge
mkdir -p "$root/apt/$pool"
for f in "${debs[@]}"; do
    ln -s "$f" "$root/apt/$pool/$(basename "$f")"
    echo "apt/$pool/$(basename "$f") $(basename "$f")" >> "$root/FILES.txt"
done
archs=()
for a in amd64 arm64; do
    compgen -G "$root/apt/$pool/*_$a.deb" >/dev/null || continue
    archs+=("$a")
    d="$root/apt/dists/stable/main/binary-$a"
    mkdir -p "$d"
    (cd "$root/apt" && apt-ftparchive --arch "$a" packages "$pool") > "$d/Packages"
    gzip -9kn "$d/Packages"
done
(cd "$root/apt/dists/stable" && apt-ftparchive \
    -o APT::FTPArchive::Release::Origin=SculptForge -o APT::FTPArchive::Release::Label=SculptForge \
    -o APT::FTPArchive::Release::Suite=stable -o APT::FTPArchive::Release::Codename=stable \
    -o APT::FTPArchive::Release::Components=main -o "APT::FTPArchive::Release::Architectures=${archs[*]}" \
    -o APT::FTPArchive::Release::Description="SculptForge releases" release . > "$work/Release")
mv "$work/Release" "$root/apt/dists/stable/Release"
sign --clearsign -o "$root/apt/dists/stable/InRelease" "$root/apt/dists/stable/Release"
sign --armor --detach-sign -o "$root/apt/dists/stable/Release.gpg" "$root/apt/dists/stable/Release"
rm -r "$root/apt/pool" # the site fetches them (FILES.txt)

# rpm: one repository for both architectures (dnf picks its own), the packages in packages/.
mkdir -p "$root/rpm/packages"
for f in "${rpms[@]}"; do
    cp "$f" "$root/rpm/packages/" # createrepo_c reads headers; copies are dropped below
    echo "rpm/packages/$(basename "$f") $(basename "$f")" >> "$root/FILES.txt"
done
createrepo_c --quiet --no-database --simple-md-filenames "$root/rpm" >/dev/null
sign --armor --detach-sign -o "$root/rpm/repodata/repomd.xml.asc" "$root/rpm/repodata/repomd.xml"
rm -r "$root/rpm/packages"
cat > "$root/rpm/sculptforge.repo" <<'EOF'
[sculptforge]
name=SculptForge
baseurl=https://sculptforge.app/rpm
enabled=1
# The repository metadata is signed and lists every package's SHA-256, so dnf checks each download against the
# signed checksum. The .rpm files themselves are the release's exact bytes, unsigned (docs/design/package-repos.md).
repo_gpgcheck=1
gpgcheck=0
gpgkey=https://sculptforge.app/rpm/sculptforge.asc
EOF

gpg --batch --armor --export "$fpr" > "$root/apt/sculptforge.asc"
cp "$root/apt/sculptforge.asc" "$root/rpm/sculptforge.asc"
tar -C "$root" -czf "$out" --owner=0 --group=0 --sort=name FILES.txt apt rpm
echo "package_repos: $(basename "$out"): apt (${archs[*]}), rpm ($(ls "$dist"/sculptforge-[0-9]*.rpm | sed 's/.*\.\([^.]*\)\.rpm/\1/' | tr '\n' ' ')), key $fpr"
