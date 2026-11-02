#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Releases SculptForge X.Y.Z from main and brings every channel to it in lock step (docs/releasing.md).
#
#   scripts/release.sh X.Y.Z                    every step, in order
#   scripts/release.sh X.Y.Z --from site        resume at a step (after fixing what stopped it)
#   scripts/release.sh X.Y.Z --only replies     one step
#   HIGHLIGHTS=notes.txt scripts/release.sh ... the update feed's 3-5 highlight lines (step "site")
#
# Steps: check ci tag release site aur brew omarchy replies status
# Each step looks at the live state first and skips what is already done, so running it again is always safe.
# When something is wrong it stops and prints the fix; fix it, then run again with --from <that step>.
# It never posts issue replies: it writes drafts for a person to read and post.
# Needs: gh logged in as islandmed (gh auth status), git, curl, python3, sha256sum (or shasum); run it from a clean
# checkout of main. Pushes to main and the tag are made here, so only the release coordinator runs it, after
# Tony's "ship it" for this version.
set -euo pipefail

REPO=islandmed/SculptForge
AUTHOR_NAME=islandmed
AUTHOR_EMAIL=232726012+islandmed@users.noreply.github.com # never a personal address in outside repos
OMARCHY_PR=1800
STEPS=(check ci tag release site aur brew omarchy replies status)

red() { printf '\033[1;31m%s\033[0m\n' "$*" >&2; }
say() { printf '\033[1m== %s\033[0m\n' "$*"; }
ok() { printf '   ok: %s\n' "$*"; }
# die "what is wrong" "the exact fix"
die() {
    red "STOPPED at step '$CURRENT': $1"
    printf '   fix: %s\n' "$2" >&2
    printf '   then: scripts/release.sh %s --from %s\n' "$V" "$CURRENT" >&2
    exit 1
}
sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
commit() { git -c user.name="$AUTHOR_NAME" -c user.email="$AUTHOR_EMAIL" commit -q "$@"; }
gitp() { git -c credential.helper= -c credential.helper='!gh auth git-credential' "$@"; }
confirm() {
    [ -n "${YES:-}" ] && return 0
    read -r -p "$1 [y/N] " a
    [ "$a" = y ] || [ "$a" = Y ]
}

V=${1:-}
[[ "$V" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "usage: scripts/release.sh X.Y.Z [--from STEP | --only STEP]"; exit 2; }
shift
FROM=check ONLY=
while [ $# -gt 0 ]; do
    case $1 in
        --from) FROM=$2; shift 2 ;;
        --only) ONLY=$2; shift 2 ;;
        --yes) YES=1; shift ;;
        *) echo "unknown argument $1"; exit 2 ;;
    esac
done
T=v$V
CURRENT=setup
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$(git rev-parse --show-toplevel)"

# --- helpers that steps share -------------------------------------------------------------------------------------

on_main() {
    [ "$(git rev-parse --abbrev-ref HEAD)" = main ] || die "this checkout is on $(git rev-parse --abbrev-ref HEAD)" "git switch main (or run from the main worktree)"
    [ -z "$(git status --porcelain --untracked-files=no)" ] || die "uncommitted changes" "commit or stash them first (git status)"
    git fetch -q origin main --tags
    git merge -q --ff-only origin/main 2>/dev/null || die "main has diverged from origin/main" "git log --oneline origin/main...main, then rebase or reset"
}
push_main() {
    gitp push -q origin HEAD:main || die "push to main refused" "git pull --rebase origin main, check the result, run again"
}
release_ready() {
    gh release view "$T" -R "$REPO" --json assets -q '.assets[].name' > "$work/assets" 2>/dev/null ||
        die "no GitHub release $T yet" "run the earlier steps: scripts/release.sh $V --from tag"
    grep -qx SHA256SUMS.txt "$work/assets" || die "release $T has no SHA256SUMS.txt (unfinished)" "re-run the Release workflow: gh workflow run release.yml -R $REPO -f tag=$T"
}
# Downloads one release asset and checks it against SHA256SUMS.txt with a checksum computed here. Prints the sum.
asset_sha() {
    [ -f "$work/SHA256SUMS.txt" ] || gh release download "$T" -R "$REPO" -p SHA256SUMS.txt -D "$work" >/dev/null
    gh release download "$T" -R "$REPO" -p "$1" -D "$work" --clobber >/dev/null
    local mine listed
    mine=$(sha256 "$work/$1")
    listed=$(awk -v f="$1" '$2 == f || $2 == "*"f { print $1 }' "$work/SHA256SUMS.txt")
    [ "$mine" = "$listed" ] || die "$1: computed $mine, SHA256SUMS.txt says ${listed:-nothing}" "investigate before publishing anything; re-run the Release workflow if an upload was cut short"
    echo "$mine"
}
tarball_sha() {
    curl -fsSL "https://github.com/$REPO/archive/refs/tags/$T.tar.gz" -o "$work/src.tar.gz" ||
        die "can't download the $T source tarball" "check that the tag is pushed: git ls-remote origin $T"
    sha256 "$work/src.tar.gz"
}
# Successful run of a workflow on a push to main for a commit; waits for one in progress.
wait_run() {
    local wf=$1 sha=$2 id status conclusion
    id=$(gh run list -R "$REPO" -w "$wf" -c "$sha" -e push -b main -L 1 --json databaseId -q '.[0].databaseId')
    [ -n "$id" ] || return 1
    status=$(gh run view "$id" -R "$REPO" --json status -q .status)
    if [ "$status" != completed ]; then
        echo "   waiting for $wf run $id ..." >&2
        gh run watch "$id" -R "$REPO" --exit-status >/dev/null || true
    fi
    conclusion=$(gh run view "$id" -R "$REPO" --json conclusion -q .conclusion)
    [ "$conclusion" = success ] ||
        die "$wf run $id on $sha: $conclusion (https://github.com/$REPO/actions/runs/$id)" \
            "fix it on a branch, merge to main, then start again from step check (or re-run the run if it was a runner hiccup: gh run rerun $id --failed -R $REPO)"
    echo "$id"
}

# --- steps --------------------------------------------------------------------------------------------------------

step_check() {
    on_main
    gh auth status 2>&1 | grep -q "account $AUTHOR_NAME" || die "gh is not logged in as $AUTHOR_NAME" "gh auth login (or gh auth switch -u $AUTHOR_NAME)"
    grep -q "^project(SculptForge VERSION $V" CMakeLists.txt || die "CMakeLists.txt is not at $V" "set project(SculptForge VERSION $V ...) and commit"
    grep -q "^## \[$V\]" CHANGELOG.md || die "CHANGELOG.md has no '## [$V] - date' section" "rename [Unreleased] to [$V] - $(date +%F) (keep an empty [Unreleased] above it) and commit"
    grep -q "^## Version $V\$" docs/guide/whats-new.md || die "docs/guide/whats-new.md has no '## Version $V'" "add the section (Help > What's New shows it) and commit"
    grep -q "expect_text \"Version $V\"" tests/ui/whats_new.sfui || die "tests/ui/whats_new.sfui doesn't check Version $V" "update its expect_text lines to the new section and commit"
    grep -q "<release version=\"$V\"" packaging/linux/org.sculptforge.SculptForge.metainfo.xml || die "metainfo has no <release version=\"$V\">" "add <release version=\"$V\" date=\"$(date +%F)\"/> at the top of <releases> and commit"
    grep -q "^version: $V\$" CITATION.cff || die "CITATION.cff is not at $V" "set version: $V and commit"
    grep -q "^version = \"$V\"" packaging/rez/package.py || die "packaging/rez/package.py is not at $V" "set version = \"$V\" and commit"
    python3 tools/guide/build_site.py >/dev/null
    git diff --quiet -- website/guide || die "website/guide is not rebuilt from docs/guide" "git add website/guide && git commit -m 'Guide: rebuilt for $V' (build_site.py just rebuilt it)"
    if git rev-parse -q --verify "refs/tags/$T" >/dev/null && [ "$(git rev-list -n1 "$T")" != "$(git rev-parse HEAD)" ]; then
        die "$T already tags $(git rev-list -n1 "$T"), not main's $(git rev-parse --short HEAD)" "if that is the release, continue: scripts/release.sh $V --from release; if not, pick the next version"
    fi
    ok "version files, CHANGELOG, What's New, metainfo and guide say $V at $(git rev-parse --short HEAD)"
}

step_ci() {
    local sha
    sha=$(git rev-parse HEAD)
    if git rev-parse -q --verify "refs/tags/$T" >/dev/null; then ok "$T is tagged: CI was checked before tagging"; return; fi
    wait_run ci.yml "$sha" >/dev/null || die "no CI run on main for $sha" "push main first (git push origin main), or wait a minute for the run to start, then run again"
    ok "CI passed on $sha"
    if wait_run packages.yml "$sha" >/dev/null; then
        ok "the Packages run passed: the release will promote its tested packages"
    else
        echo "   note: no Packages run for $sha (only website or paperwork changed since?): the release builds its own (about 20 min)"
    fi
}

step_tag() {
    if gh api "repos/$REPO/git/ref/tags/$T" >/dev/null 2>&1; then ok "$T is already pushed"; return; fi
    local sha
    sha=$(git rev-parse HEAD)
    confirm "Tag $T at $(git log -1 --format='%h %s' "$sha") and push it? This publishes SculptForge $V." ||
        die "not confirmed" "run again and answer y (or YES=1) once Tony has said ship it for $V"
    git rev-parse -q --verify "refs/tags/$T" >/dev/null || git tag "$T" "$sha"
    gitp push -q origin "$T" || die "pushing $T failed" "git push origin $T (check gh auth status)"
    ok "pushed $T"
}

step_release() {
    if gh release view "$T" -R "$REPO" --json assets -q '.assets[].name' 2>/dev/null | grep -qx SHA256SUMS.txt; then
        ok "release $T is published"
    else
        local id=""
        for _ in $(seq 30); do
            id=$(gh run list -R "$REPO" -w release.yml -b "$T" -L 1 --json databaseId -q '.[0].databaseId')
            [ -n "$id" ] && break
            sleep 10
        done
        [ -n "$id" ] || die "no Release run for $T after 5 minutes" "gh workflow run release.yml -R $REPO -f tag=$T"
        echo "   waiting for Release run $id (https://github.com/$REPO/actions/runs/$id) ..."
        gh run watch "$id" -R "$REPO" --exit-status >/dev/null ||
            die "Release run $id failed (https://github.com/$REPO/actions/runs/$id)" \
                "read the failed job; a runner hiccup: gh run rerun $id --failed -R $REPO; a real bug: fix on main, delete the tag (git push origin :$T; git tag -d $T) and release $V again from step check"
    fi
    release_ready
    local n
    n=$(wc -l < "$work/assets")
    [ "$n" -ge 30 ] || die "release $T has only $n assets" "re-run the Release workflow: gh workflow run release.yml -R $REPO -f tag=$T"
    ok "$n assets, SHA256SUMS.txt included"
}

step_site() {
    release_ready
    on_main
    if grep -q "\"version\": \"$V\"" website/latest.json; then
        ok "website/latest.json already says $V"
    else
        [ -n "${HIGHLIGHTS:-}" ] && [ -f "$HIGHLIGHTS" ] ||
            die "the update feed needs this release's highlights" "write 3-5 one-line highlights (what users get) to a file and run: HIGHLIGHTS=that-file scripts/release.sh $V --from site"
        grep -q "new in $V" website/index.html ||
            die "website/index.html has no 'new in $V' card in What's new" "add an <article class=\"card\"> with <small>new in $V</small> at the top of the #whats-new grid and 'New in $V: ...' at the start of its lede, commit, run again"
        python3 - "$V" "$HIGHLIGHTS" <<'PY'
import json, sys
v, notes = sys.argv[1], sys.argv[2]
lines = [l.strip() for l in open(notes, encoding="utf-8") if l.strip()]
if not 1 <= len(lines) <= 6:
    sys.exit(f"{notes}: want 1-6 highlight lines, found {len(lines)}")
with open("website/latest.json", encoding="utf-8") as f:
    feed = json.load(f)
feed["version"], feed["highlights"] = v, lines
with open("website/latest.json", "w", encoding="utf-8") as f:
    json.dump(feed, f, indent=2, ensure_ascii=False)
    f.write("\n")
PY
        sed -i.bak -E "s/\"softwareVersion\": \"[0-9.]+\"/\"softwareVersion\": \"$V\"/; s/Version [0-9]+\.[0-9]+\.[0-9]+\. All builds/Version $V. All builds/" website/index.html
        rm -f website/index.html.bak
        grep -q "\"softwareVersion\": \"$V\"" website/index.html && grep -q "Version $V. All builds" website/index.html ||
            die "website/index.html's version text didn't update" "set softwareVersion and the 'Version X.Y.Z. All builds' line by hand, commit, run again"
        git add website/latest.json website/index.html
        commit -m "Website: $V is the current release (feed, download text)"
        push_main
        ok "pushed the feed and home page"
    fi
    # The Website workflow deploys main's site and the newest tag's guide; check what visitors get.
    for _ in $(seq 40); do
        if curl -fsSL "https://sculptforge.app/latest.json?$(date +%s)" | grep "\"version\": \"$V\"" >/dev/null; then
            ok "sculptforge.app/latest.json says $V"
            curl -fsSL "https://sculptforge.app/guide/whats-new.html?$(date +%s)" | grep "Version $V" >/dev/null ||
                die "the live guide's What's New doesn't show $V" "gh workflow run pages.yml -R $REPO (it takes the guide from the newest tag), wait, run again"
            ok "the live guide shows Version $V"
            return
        fi
        sleep 15
    done
    die "sculptforge.app still serves the old feed after 10 minutes" "check the Website workflow: gh run list -R $REPO -w pages.yml -L 3; re-run it if it failed"
}

step_aur() {
    release_ready
    on_main
    local sum
    sum=$(tarball_sha)
    if grep -q "^pkgver=$V\$" packaging/arch/PKGBUILD && grep -q "'$sum'" packaging/arch/PKGBUILD; then
        ok "packaging/arch/PKGBUILD already builds $V ($sum)"
        return
    fi
    python3 - "$V" "$sum" <<'PY'
import re, sys
v, s = sys.argv[1], sys.argv[2]
p = "packaging/arch/PKGBUILD"
t = open(p, encoding="utf-8").read()
t = re.sub(r"^pkgver=.*$", f"pkgver={v}", t, count=1, flags=re.M)
t = re.sub(r"^pkgrel=.*$", "pkgrel=1", t, count=1, flags=re.M)
t = re.sub(r"^sha256sums=\('[0-9a-f]{64}'", f"sha256sums=('{s}'", t, count=1, flags=re.M)
open(p, "w", encoding="utf-8").write(t)
PY
    grep -q "^pkgver=$V\$" packaging/arch/PKGBUILD && grep -q "'$sum'" packaging/arch/PKGBUILD ||
        die "couldn't update packaging/arch/PKGBUILD" "set pkgver=$V, pkgrel=1 and the first sha256sums entry to $sum by hand, commit, run again"
    git add packaging/arch/PKGBUILD
    commit -m "AUR PKGBUILD builds $V"
    push_main
    ok "PKGBUILD at $V, source checksum $sum (computed here)"
}

step_brew() {
    release_ready
    local have dmg sum
    have=$(gh api repos/islandmed/homebrew-tap/contents/Casks/sculptforge.rb -q .content | base64 -d | sed -n 's/^ *version "\(.*\)"/\1/p')
    if [ "$have" = "$V" ]; then ok "Homebrew cask already at $V"; return; fi
    dmg="SculptForge-$V-darwin-universal.dmg"
    sum=$(asset_sha "$dmg")
    YES=1 tools/publish/setup-channels.sh brew >/dev/null || die "tools/publish/setup-channels.sh brew failed" "run it by hand to see why: YES=1 tools/publish/setup-channels.sh brew"
    gh api repos/islandmed/homebrew-tap/contents/Casks/sculptforge.rb -q .content | base64 -d | grep -q "sha256 \"$sum\"" ||
        die "the cask doesn't carry the checksum computed here ($sum)" "compare Casks/sculptforge.rb in islandmed/homebrew-tap with $dmg's real checksum and fix it"
    ok "Homebrew cask at $V ($sum)"
}

step_omarchy() {
    release_ready
    local state branch owner path=pkgbuilds/sculptforge/PKGBUILD sum
    read -r state owner branch < <(gh pr view "$OMARCHY_PR" -R omacom/omarchy-pkgs --json state,headRepositoryOwner,headRefName \
        -q '.state + " " + .headRepositoryOwner.login + " " + .headRefName')
    [ "$state" = OPEN ] || die "omacom/omarchy-pkgs#$OMARCHY_PR is $state" "if it merged, open a version-bump PR for $path there and set OMARCHY_PR in this script; if it was closed, ask Tony"
    sum=$(tarball_sha)
    if gh api "repos/$owner/omarchy-pkgs/contents/$path?ref=$branch" -q .content | base64 -d | grep -q "^pkgver=$V\$"; then
        ok "Omarchy PR #$OMARCHY_PR already builds $V"
        return
    fi
    gitp clone -q --depth 1 -b "$branch" "https://github.com/$owner/omarchy-pkgs.git" "$work/omarchy"
    python3 - "$V" "$sum" "$work/omarchy/$path" <<'PY'
import re, sys
v, s, p = sys.argv[1:]
t = open(p, encoding="utf-8").read()
t = re.sub(r"^pkgver=.*$", f"pkgver={v}", t, count=1, flags=re.M)
t = re.sub(r"^pkgrel=.*$", "pkgrel=1", t, count=1, flags=re.M)
t = re.sub(r"^sha256sums=\('[0-9a-f]{64}'", f"sha256sums=('{s}'", t, count=1, flags=re.M)
open(p, "w", encoding="utf-8").write(t)
PY
    (cd "$work/omarchy" && grep -q "^pkgver=$V\$" "$path" && grep -q "'$sum'" "$path") ||
        die "couldn't update the Omarchy PKGBUILD" "edit $path on $owner/omarchy-pkgs branch $branch by hand (pkgver=$V, first sha256 $sum)"
    (cd "$work/omarchy" && git add "$path" && commit -m "sculptforge $V" && gitp push -q origin "HEAD:$branch") ||
        die "pushing to $owner/omarchy-pkgs $branch failed" "check gh auth status, then run again"
    ok "Omarchy PR #$OMARCHY_PR builds $V (author $AUTHOR_EMAIL)"
}

step_replies() {
    release_ready
    local out="release-replies-$V.md" issues
    issues=$(awk "/^## \[$V\]/{f=1;next} /^## \[/{f=0} f" CHANGELOG.md | grep -o '#[0-9]\+' | tr -d '#' | sort -un)
    {
        echo "# Draft replies for SculptForge $V (read, edit, then post by hand; nothing here was posted)"
        echo
        for n in $issues; do
            local title state lines
            read -r state title < <(gh issue view "$n" -R "$REPO" --json state,title -q '.state + " " + .title' 2>/dev/null || echo "MISSING ?")
            [ "$state" = MISSING ] && continue
            lines=$(awk "/^## \[$V\]/{f=1;next} /^## \[/{f=0} f" CHANGELOG.md | grep "#$n\b" | sed 's/^- //' | head -3)
            echo "## #$n $title ($state)"
            echo
            echo "Fixed in SculptForge $V, out now: https://github.com/$REPO/releases/tag/$T"
            echo "(Help > Check for updates in the app, or https://sculptforge.app/#download)"
            echo
            echo "$lines"
            echo
            echo "Thanks for reporting it. -- the SculptForge developer"
            echo
        done
    } > "$out"
    ok "drafts for $(echo "$issues" | wc -w) issues in $out: read them, then post with gh issue comment / close"
}

step_status() {
    local winget
    winget=$(gh pr view 464465 -R microsoft/winget-pkgs --json state -q .state 2>/dev/null || echo unknown)
    echo "   winget-pkgs#464465: $winget (update it once it merges; waits on the CLA)"
    echo "   Release:  https://github.com/$REPO/releases/tag/$T"
    echo "   Remember: post the replies in release-replies-$V.md after reading them, then update the handoff note."
}

started=
for s in "${STEPS[@]}"; do
    [ "$s" = "$FROM" ] && started=1
    if [ -n "$ONLY" ]; then [ "$s" = "$ONLY" ] || continue; else [ -n "$started" ] || continue; fi
    CURRENT=$s
    say "$s"
    "step_$s"
done
[ -n "$started$ONLY" ] || { echo "unknown step $FROM; steps: ${STEPS[*]}"; exit 2; }
say "SculptForge $V: done"
