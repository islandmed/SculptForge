#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Puts SculptForge where workplace networks can reach it (they often block domains under ~30 days old):
#   mirror   https://islandmed.github.io/ - a copy of sculptforge.app, kept in sync by the Website workflow
#   brew     brew install --cask islandmed/tap/sculptforge   (the islandmed/homebrew-tap repo)
#   winget   winget install SculptForge.SculptForge          (a pull request to microsoft/winget-pkgs)
# Run with no arguments for all three, or name the steps: tools/publish/setup-channels.sh brew winget
# Version and checksums come from the latest GitHub release, so `brew` and `winget` also update for a new release.
# YES=1 answers yes to every question (e.g. when run without a terminal). Needs gh logged in as islandmed (gh auth status), git and ssh-keygen.
set -euo pipefail

REPO=islandmed/SculptForge
OWNER=islandmed
steps=("$@")
[ ${#steps[@]} -eq 0 ] && steps=(mirror brew winget)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
gitp() { git -c credential.helper= -c credential.helper='!gh auth git-credential' "$@"; }

tag=$(gh release view -R "$REPO" --json tagName -q .tagName)
version=${tag#v}
gh release download "$tag" -R "$REPO" -p SHA256SUMS.txt -D "$work"
sum() { awk -v f="$1" '$2 == f || $2 == "*"f { print $1 }' "$work/SHA256SUMS.txt"; }
dmg="SculptForge-$version-darwin-universal.dmg"
exe="SculptForge-$version-windows-x86_64.exe"
dmg_sha=$(sum "$dmg")
exe_sha=$(sum "$exe")
[ -n "$dmg_sha" ] && [ -n "$exe_sha" ] || { echo "No checksums for $dmg / $exe in $tag's SHA256SUMS.txt" >&2; exit 1; }
echo "Release $tag: $dmg $dmg_sha, $exe $exe_sha"

confirm() { [ -n "${YES:-}" ] && return 0; read -r -p "$1 [y/N] " a; [ "$a" = y ] || [ "$a" = Y ]; }  # YES=1: no questions

mirror() {
    local site=$OWNER/$OWNER.github.io
    if ! gh repo view "$site" >/dev/null 2>&1; then
        confirm "Create the public repo $site (the website copy)?" || return 0
        gh repo create "$site" --public --homepage https://sculptforge.app/ \
            --description "Copy of sculptforge.app for networks that block new domains. Synced from $REPO/website."
    fi
    # A deploy key that may push to the copy only, for the Website workflow (secret MIRROR_DEPLOY_KEY).
    ssh-keygen -q -t ed25519 -N "" -C "sculptforge-website-mirror" -f "$work/mirror"
    gh repo deploy-key add "$work/mirror.pub" -R "$site" --allow-write --title "SculptForge Website workflow"
    gh secret set MIRROR_DEPLOY_KEY -R "$REPO" < "$work/mirror"
    rm -f "$work/mirror" "$work/mirror.pub"
    # First commit, so the workflow has a branch to push to, then GitHub Pages from main.
    if [ -z "$(gh api "repos/$site/commits?per_page=1" -q '.[0].sha' 2>/dev/null)" ]; then
        mkdir "$work/site" && cd "$work/site"
        git init -q -b main
        printf '# SculptForge website copy\n\nA copy of [sculptforge.app](https://sculptforge.app/), synced from [%s](https://github.com/%s/tree/main/website).\n' "$REPO" "$REPO" > README.md
        touch .nojekyll
        git add -A && git -c user.name="$OWNER" -c user.email="$OWNER@users.noreply.github.com" commit -qm "Start the copy"
        gitp push -q "https://github.com/$site.git" HEAD:main
        cd - >/dev/null
    fi
    gh api -X POST "repos/$site/pages" -f 'source[branch]=main' -f 'source[path]=/' >/dev/null 2>&1 || true
    gh workflow run pages.yml -R "$REPO"
    echo "Copy: https://$OWNER.github.io/ (live a few minutes after the Website workflow finishes)"
}

brew() {
    local tap=$OWNER/homebrew-tap
    if ! gh repo view "$tap" >/dev/null 2>&1; then
        confirm "Create the public repo $tap (the Homebrew tap)?" || return 0
        gh repo create "$tap" --public --homepage https://sculptforge.app/ --description "Homebrew tap for SculptForge"
    fi
    mkdir "$work/tap" && cd "$work/tap"
    if gh api "repos/$tap/commits?per_page=1" >/dev/null 2>&1 && [ -n "$(gh api "repos/$tap/commits?per_page=1" -q '.[0].sha')" ]; then
        gitp clone -q "https://github.com/$tap.git" .
    else
        git init -q -b main
        printf '# Homebrew tap for SculptForge\n\n```\nbrew install --cask %s/tap/sculptforge\n```\n\nFree, open-source digital sculpting: https://sculptforge.app/\n' "$OWNER" > README.md
    fi
    mkdir -p Casks
    cat > Casks/sculptforge.rb <<CASK
cask "sculptforge" do
  version "$version"
  sha256 "$dmg_sha"

  url "https://github.com/$REPO/releases/download/v#{version}/SculptForge-#{version}-darwin-universal.dmg",
      verified: "github.com/$REPO/"
  name "SculptForge"
  desc "Free, open-source digital sculpting, an alternative to ZBrush"
  homepage "https://sculptforge.app/"

  livecheck do
    url :url
    strategy :github_latest
  end

  depends_on macos: ">= :big_sur"

  app "SculptForge.app"
  binary "#{appdir}/SculptForge.app/Contents/MacOS/sculptforge-cli"

  # The app is signed ad hoc, not notarized: without this macOS refuses to open it.
  postflight do
    system_command "/usr/bin/xattr", args: ["-dr", "com.apple.quarantine", "#{appdir}/SculptForge.app"]
  end

  zap trash: "~/Library/Application Support/SculptForge"
end
CASK
    git add -A
    git -c user.name="$OWNER" -c user.email="$OWNER@users.noreply.github.com" commit -qm "sculptforge $version" || true
    gitp push -q "https://github.com/$tap.git" HEAD:main
    cd - >/dev/null
    echo "Homebrew: brew install --cask $OWNER/tap/sculptforge"
}

winget() {
    local id=SculptForge.SculptForge dir="manifests/s/SculptForge/SculptForge/$version" branch="sculptforge-$version"
    local date
    date=$(gh release view "$tag" -R "$REPO" --json publishedAt -q '.publishedAt[:10]')
    mkdir -p "$work/winget/$dir"
    cat > "$work/winget/$dir/$id.yaml" <<YAML
# yaml-language-server: \$schema=https://aka.ms/winget-manifest.version.1.6.0.schema.json
PackageIdentifier: $id
PackageVersion: $version
DefaultLocale: en-US
ManifestType: version
ManifestVersion: 1.6.0
YAML
    cat > "$work/winget/$dir/$id.installer.yaml" <<YAML
# yaml-language-server: \$schema=https://aka.ms/winget-manifest.installer.1.6.0.schema.json
PackageIdentifier: $id
PackageVersion: $version
InstallerType: nullsoft
Scope: machine
ReleaseDate: $date
Installers:
- Architecture: x64
  InstallerUrl: https://github.com/$REPO/releases/download/$tag/$exe
  InstallerSha256: $(echo "$exe_sha" | tr a-f A-F)
ManifestType: installer
ManifestVersion: 1.6.0
YAML
    cat > "$work/winget/$dir/$id.locale.en-US.yaml" <<YAML
# yaml-language-server: \$schema=https://aka.ms/winget-manifest.defaultLocale.1.6.0.schema.json
PackageIdentifier: $id
PackageVersion: $version
PackageLocale: en-US
Publisher: The SculptForge contributors
PublisherUrl: https://github.com/$REPO
PublisherSupportUrl: https://github.com/$REPO/issues
PackageName: SculptForge
PackageUrl: https://sculptforge.app/
License: GPL-3.0-or-later
LicenseUrl: https://github.com/$REPO/blob/main/LICENSE
Copyright: Copyright (C) 2026 The SculptForge contributors
ShortDescription: Free, open-source digital sculpting, an alternative to ZBrush and Mudbox.
Description: SculptForge is a free, open-source digital sculpting app for artists and studios, with SubTools, layers, dynamic topology, remeshing, polypaint, baking and AI-assisted image-to-3D.
Moniker: sculptforge
Tags:
- 3d
- sculpting
- modeling
- zbrush-alternative
- open-source
ReleaseNotesUrl: https://github.com/$REPO/releases/tag/$tag
ManifestType: defaultLocale
ManifestVersion: 1.6.0
YAML
    echo "winget manifests:"; ls "$work/winget/$dir"
    [ -n "${SHOW:-}" ] && cat "$work/winget/$dir"/*.yaml
    confirm "Open a pull request to microsoft/winget-pkgs adding $id $version?" || return 0
    gh repo fork microsoft/winget-pkgs --clone=false >/dev/null 2>&1 || true
    local fork=$OWNER/winget-pkgs base
    base=$(gh api repos/microsoft/winget-pkgs/git/ref/heads/master -q .object.sha)
    gh api -X POST "repos/$fork/git/refs" -f "ref=refs/heads/$branch" -f "sha=$base" >/dev/null
    for f in "$work/winget/$dir"/*.yaml; do
        gh api -X PUT "repos/$fork/contents/$dir/$(basename "$f")" -f "message=New version: $id version $version" \
            -f "branch=$branch" -f "content=$(base64 -w0 "$f")" >/dev/null
    done
    gh pr create -R microsoft/winget-pkgs --head "$OWNER:$branch" --title "New package: $id version $version" \
        --body "Adds SculptForge $version (free, open-source digital sculpting, https://sculptforge.app/). Installer: the NSIS setup from the GitHub release $tag."
}

for s in "${steps[@]}"; do "$s"; done
