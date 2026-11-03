#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Releases SculptForge X.Y.Z from main and brings every channel to it in lock step (docs/releasing.md).
#
#   HIGHLIGHTS=notes.txt scripts/release.sh X.Y.Z --yes   every step, in order (the normal way: one command)
#   scripts/release.sh X.Y.Z --from site                  resume at a step (after fixing what stopped it)
#   scripts/release.sh X.Y.Z --only replies              one step
#   HIGHLIGHTS=file   the update feed's 3-5 highlight lines, one per line (default: the version's highlight: lines in
#                     What's New and the note fragments, tools/release/assemble_notes.py X.Y.Z --highlights)
#   (the home page's What's New area is written by tools/guide/build_site.py from docs/guide/whats-new.md: the
#   version's ```features block, its tagline and pictures; CARD is no longer used)
#
# Steps: bump check ci tag release site aur brew omarchy replies status
# bump sets every version file, rebuilds the guide and pushes main; check then verifies everything the later steps
# need, so a release either stops in seconds before the tag or runs through to the end.
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
STEPS=(bump check ci tag release site aur brew omarchy replies status)

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
FROM=bump ONLY=
while [ $# -gt 0 ]; do
    case $1 in
        --from) FROM=$2; shift 2 ;;
        --only) ONLY=$2; shift 2 ;;
        --yes) YES=1; shift ;;
        *) echo "unknown argument $1"; exit 2 ;;
    esac
done
T=v$V
TODAY=$(date +%F)
CURRENT=setup
# HIGHLIGHTS may be relative to where the script was started; the script works from the top of the checkout.
case ${HIGHLIGHTS:-} in ''|/*) ;; *) HIGHLIGHTS=$PWD/$HIGHLIGHTS ;; esac
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
# Successful run of a workflow on a push to main for a commit; waits for one in progress. The third argument is how
# many 10-second tries a just-pushed commit's run gets to appear (default 1).
wait_run() {
    local wf=$1 sha=$2 tries=${3:-1} id="" status conclusion
    while :; do
        id=$(gh run list -R "$REPO" -w "$wf" -c "$sha" -e push -b main -L 1 --json databaseId -q '.[0].databaseId')
        tries=$((tries - 1))
        if [ -n "$id" ] || [ "$tries" -le 0 ]; then break; fi
        sleep 10
    done
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
# wait_live URL TEXT SECONDS: true once the live page contains TEXT. Pages deploys lag, so it polls every 15 s.
# grep reads the whole page: grep -q would close the pipe early and curl would fail under pipefail.
wait_live() {
    local end=$((SECONDS + $3))
    while :; do
        curl -fsSL "$1?$(date +%s)" 2>/dev/null | grep -F -- "$2" >/dev/null && return 0
        [ "$SECONDS" -lt "$end" ] || return 1
        sleep 15
    done
}
# gh_file OWNER/REPO PATH [REF]: a file's contents from GitHub ("" when it can't be read).
gh_file() { gh api "repos/$1/contents/$2${3:+?ref=$3}" -q .content 2>/dev/null | base64 -d 2>/dev/null || true; }

# --- preconditions: what the steps after the tag need, checked before anything is tagged --------------------------

# The home page's "new in X.Y.Z" card and "New in X.Y.Z:" lede in #whats-new, written by build_site.py (step bump) from
# the version's ```features block in whats-new.md, which needs a date and a tagline.
card_ok() { grep -qF "<small>new in $V</small>" website/index.html && grep -qF "class=\"section-lede\">New in $V:" website/index.html; }
features_ok() { awk -v v="## Version $V" '$0==v{f=1;next} f&&/^## /{exit} f' docs/guide/whats-new.md | grep -q '^tagline: .'; }
card_fix="give '## Version $V' in docs/guide/whats-new.md a \`\`\`features block (date: unreleased, tagline: ..., highlight: ..., cards), commit, push main, run again"
feed_done() { grep -qF "\"version\": \"$V\"" website/latest.json; }
# HIGHLIGHTS names a file of 3-5 non-empty lines.
highlights_ok() {
    local n
    [ -n "${HIGHLIGHTS:-}" ] && [ -f "$HIGHLIGHTS" ] || return 1
    n=$(grep -c '[^[:space:]]' "$HIGHLIGHTS" || true)
    [ "$n" -ge 3 ] && [ "$n" -le 5 ]
}
highlights_fix="write 3-5 one-line highlights (what users get) to a file and run again with HIGHLIGHTS=that-file"
# The words people write for What's New: the guide section and the UI test that expects it.
words_ok() {
    python3 tools/release/assemble_notes.py --check >/dev/null || die "a note fragment in changes/ doesn't parse: $(python3 tools/release/assemble_notes.py --check 2>&1)" "fix it (changes/README.md has the format), commit, push main"
    grep -q "^## Version ${V//./\\.}\$" docs/guide/whats-new.md || grep -qs '^whats_new:' changes/*.md ||
        die "docs/guide/whats-new.md has no '## Version $V'" "write that section at the top (Help > What's New shows it), commit, push main"
    grep -qF "expect_text \"Version $V\"" tests/ui/whats_new.sfui ||
        die "tests/ui/whats_new.sfui doesn't check Version $V" "update its first expect_text lines to the new section (the version and one line from it), commit, push main"
}
# UI scale / DPI (#87): the Windows manifest still asks for per-monitor DPI v2, and the "dpi" UI tests (100-200 %, a
# display-scale event) pass on main's build. BUILD_DIR (default build) is the coordinator's tree, already built for
# the full ctest; only the app is rebuilt here.
dpi_ok() {
    local b=${BUILD_DIR:-build} heavy=(nice -n 19 ionice -c3)
    [ -x "$HOME/Work/.sf/heavy.sh" ] && heavy=("$HOME/Work/.sf/heavy.sh") # one heavy job machine-wide
    grep -q '<dpiAwareness[^>]*>PerMonitorV2' packaging/windows/sculptforge.manifest 2>/dev/null &&
        grep -q 'packaging/windows/sculptforge.manifest' CMakeLists.txt ||
        die "the Windows build no longer declares per-monitor DPI v2" "restore packaging/windows/sculptforge.manifest and its target_sources line in CMakeLists.txt (git log -p -- packaging/windows)"
    [ -f "$b/CTestTestfile.cmake" ] || die "no build tree at $b for the DPI UI tests" "cmake -S . -B $b -DCMAKE_BUILD_TYPE=Release, or BUILD_DIR=<your build> scripts/release.sh $V --from check"
    "${heavy[@]}" cmake --build "$b" --target sculptforge -j3 >/dev/null || die "the app doesn't build in $b" "cmake --build $b --target sculptforge and fix the error"
    SDL_APP_ID=sculptforge-agent "${heavy[@]}" ctest --test-dir "$b" -L dpi --output-on-failure >"$b/release-dpi.log" 2>&1 ||
        die "the UI scale / DPI tests fail (ctest -L dpi; log: $b/release-dpi.log)" "fix them on a branch, merge to main, run again from step check"
    ok "UI scale tests (100-200 %, display-scale event) pass; Windows manifest declares PerMonitorV2"
}
# Everything the steps after the tag need that doesn't depend on the release existing. The home page card is
# checked through the features block until bump writes it.
preflight() {
    local mode=${1:-} t auth pr state owner branch pk
    for t in git gh curl python3 base64 awk; do command -v "$t" >/dev/null || die "$t is not installed" "install $t"; done
    command -v sha256sum >/dev/null || command -v shasum >/dev/null || die "no sha256sum or shasum" "install coreutils"
    auth=$(gh auth status 2>&1 || true)
    grep -q "account $AUTHOR_NAME" <<<"$auth" || die "gh is not logged in as $AUTHOR_NAME" "gh auth login (or gh auth switch -u $AUTHOR_NAME)"
    words_ok
    # site (bump writes the home page's card from the features block)
    card_ok || features_ok || die "docs/guide/whats-new.md: Version $V has no \`\`\`features block with a tagline" "$card_fix"
    if ! feed_done; then
        if [ -z "${HIGHLIGHTS:-}" ]; then # the version's highlight lines (What's New features block + note fragments)
            HIGHLIGHTS=$(mktemp)
            python3 tools/release/assemble_notes.py "$V" --highlights >"$HIGHLIGHTS"
            highlights_ok || HIGHLIGHTS=
        fi
        highlights_ok || die "HIGHLIGHTS must name a file with 3-5 highlight lines for the update feed (now: '${HIGHLIGHTS:-unset}')" "$highlights_fix"
    fi
    python3 -c 'import json, sys; json.load(open(sys.argv[1]))' website/latest.json 2>/dev/null || die "website/latest.json is not valid JSON" "fix it, commit"
    grep -q '"softwareVersion": "[0-9.]*"' website/index.html && grep -Eq 'Version [0-9]+\.[0-9]+\.[0-9]+\. All builds' website/index.html ||
        die "website/index.html lost the softwareVersion or 'Version X.Y.Z. All builds' text that step site updates" "restore both (git log -p website/index.html), commit"
    # aur
    pk=packaging/arch/PKGBUILD
    grep -q '^pkgver=' "$pk" && grep -q '^pkgrel=' "$pk" && grep -Eq "^sha256sums=\('[0-9a-f]{64}'" "$pk" ||
        die "$pk lacks the pkgver=, pkgrel= or sha256sums=('<64 hex>' line that step aur rewrites" "restore them, commit"
    # brew
    [ -x tools/publish/setup-channels.sh ] || die "tools/publish/setup-channels.sh is missing or not executable" "git checkout origin/main -- tools/publish/setup-channels.sh"
    gh_file islandmed/homebrew-tap Casks/sculptforge.rb | grep '^ *version "' >/dev/null ||
        die "can't read Casks/sculptforge.rb in islandmed/homebrew-tap" "check gh auth status and that the tap exists (YES=1 tools/publish/setup-channels.sh brew sets it up)"
    # omarchy
    pr=$(gh pr view "$OMARCHY_PR" -R omacom/omarchy-pkgs --json state,headRepositoryOwner,headRefName \
        -q '.state + " " + .headRepositoryOwner.login + " " + .headRefName' 2>/dev/null || true)
    read -r state owner branch <<<"$pr" || true
    [ "$state" = OPEN ] || die "omacom/omarchy-pkgs#$OMARCHY_PR is ${state:-unreadable}" "if it merged, open a version-bump PR for pkgbuilds/sculptforge/PKGBUILD there and set OMARCHY_PR in this script; if it was closed, ask Tony"
    gh_file "$owner/omarchy-pkgs" pkgbuilds/sculptforge/PKGBUILD "$branch" | grep -E "^sha256sums=\('[0-9a-f]{64}'" >/dev/null ||
        die "the Omarchy PKGBUILD on $owner/omarchy-pkgs $branch has no sha256sums=('<64 hex>' line" "fix it on that branch"
    ok "What's New, home page card, highlights, PKGBUILD, Homebrew tap and Omarchy PR #$OMARCHY_PR are ready"
}

# --- steps --------------------------------------------------------------------------------------------------------

# Sets every version file to X.Y.Z, rebuilds the guide and the home page's What's New area, pushes main.
step_bump() {
    on_main
    preflight bump
    local out
    # Note fragments (changes/<branch>.md) into CHANGELOG [Unreleased] and What's New X.Y.Z; the fragments go.
    out=$(python3 tools/release/assemble_notes.py "$V" --apply 2>&1) || die "$out" "fix that fragment in changes/, commit, push main"
    grep -q 'written' <<<"$out" && echo "$out" | sed 's/^/   notes: /'
    out=$(python3 - "$V" "$TODAY" <<'PY'
import re, sys
v, today = sys.argv[1:]
ev = re.escape(v)
new = {}

def load(p):
    return new[p] if p in new else open(p, encoding="utf-8").read()

def fail(msg):
    print(msg)
    sys.exit(1)

def sub1(p, t, pat, rep):
    t2, n = re.subn(pat, rep, t, count=1, flags=re.M)
    if not n:
        fail(f"{p}: no line matching {pat}")
    return t2

p = "CMakeLists.txt"; t = load(p)
if not re.search(rf"^project\(SculptForge VERSION {ev}\b(?!\.)", t, re.M):
    new[p] = sub1(p, t, r"^(project\(SculptForge VERSION )[0-9]+(?:\.[0-9]+)*", rf"\g<1>{v}")

p = "CITATION.cff"; t = load(p)
if not re.search(rf"^version: {ev}$", t, re.M):
    t = sub1(p, t, r"^version: .*$", f"version: {v}")
    new[p] = sub1(p, t, r"^date-released: .*$", f"date-released: {today}")

p = "packaging/rez/package.py"; t = load(p)
if not re.search(rf'^version = "{ev}"', t, re.M):
    new[p] = sub1(p, t, r'^version = ".*"', f'version = "{v}"')

p = "packaging/linux/org.sculptforge.SculptForge.metainfo.xml"; t = load(p)
if f'<release version="{v}"' not in t:
    new[p] = sub1(p, t, r"^([ \t]*)<releases>[ \t]*\n", rf'\g<0>\g<1>  <release version="{v}" date="{today}"/>\n')

# What's New: the version's ```features block gets its release date (it says "date: unreleased" until then).
p = "docs/guide/whats-new.md"; t = load(p)
m = re.search(rf"^## Version {ev}\n\n```features\ndate: unreleased$", t, re.M)
if m:
    new[p] = t[:m.start()] + m.group(0).replace("date: unreleased", f"date: {today}") + t[m.end():]

p = "CHANGELOG.md"; t = load(p)
if not re.search(rf"^## \[{ev}\]", t, re.M):
    m = re.search(r"^## \[Unreleased\][^\n]*\n", t, re.M)
    if not m:
        fail(f"{p} has no '## [Unreleased]' section to release")
    nxt = re.search(r"^## \[", t[m.end():], re.M)
    body = t[m.end():m.end() + nxt.start()] if nxt else t[m.end():]
    if not body.strip():
        fail(f"{p}: [Unreleased] is empty, so there is nothing to release as {v}")
    new[p] = t[:m.start()] + f"## [Unreleased]\n\n## [{v}] - {today}\n" + t[m.end():]

p = "website/index.html"; t = load(p)  # "What's new in X.Y" and the hero badge
mm = ".".join(v.split(".")[:2])
t = re.sub(r"(<h2>What's new in )[0-9.]+(</h2>)", rf"\g<1>{mm}\g<2>", t, count=1)
t = re.sub(r'(class="new-badge"[^>]*><span>New in )[0-9.]+(</span>)', rf"\g<1>{mm}\g<2>", t, count=1)
new[p] = t

for p, t in new.items():  # written only after every check passed, so a failure leaves the tree clean
    open(p, "w", encoding="utf-8").write(t)
    print(p)
PY
) || die "$out" "fix that file by hand (CHANGELOG entries and What's New are written by people), commit, push main"
    [ -n "$out" ] && echo "$out" | sed 's/^/   set: /'
    python3 tools/guide/build_site.py >/dev/null # the guide, and the home page's What's New area for $V
    card_ok || die "website/index.html has no 'new in $V' card after build_site.py" "$card_fix"
    git add CMakeLists.txt CITATION.cff packaging/rez/package.py packaging/linux/org.sculptforge.SculptForge.metainfo.xml CHANGELOG.md website/index.html docs/guide/whats-new.md
    git add -A changes
    git add -A website/guide
    if git diff --cached --quiet; then
        ok "every version file already says $V and the guide is built"
        return
    fi
    commit -m "Release $V: version files, CHANGELOG section, guide"
    push_main
    ok "version files at $V, guide rebuilt, pushed main $(git rev-parse --short HEAD)"
}

step_check() {
    on_main
    preflight
    local bump="run step bump: scripts/release.sh $V --only bump (it sets every version file)"
    grep -q "^project(SculptForge VERSION ${V//./\\.}[ )]" CMakeLists.txt || die "CMakeLists.txt is not at $V" "$bump"
    grep -q "^## \[${V//./\\.}\]" CHANGELOG.md || die "CHANGELOG.md has no '## [$V] - date' section" "$bump"
    grep -qF "<release version=\"$V\"" packaging/linux/org.sculptforge.SculptForge.metainfo.xml || die "metainfo has no <release version=\"$V\">" "$bump"
    grep -qx "version: $V" CITATION.cff || die "CITATION.cff is not at $V" "$bump"
    grep -qx "version = \"$V\"" packaging/rez/package.py || die "packaging/rez/package.py is not at $V" "$bump"
    python3 tools/guide/build_site.py >/dev/null
    [ -z "$(git status --porcelain -- website/guide)" ] || die "website/guide is not rebuilt from docs/guide" "$bump"
    if git rev-parse -q --verify "refs/tags/$T" >/dev/null && [ "$(git rev-list -n1 "$T")" != "$(git rev-parse HEAD)" ]; then
        die "$T already tags $(git rev-list -n1 "$T"), not main's $(git rev-parse --short HEAD)" "if that is the release, continue: scripts/release.sh $V --from release; if not, pick the next version"
    fi
    ok "version files, CHANGELOG, What's New, metainfo and guide say $V at $(git rev-parse --short HEAD)"
    dpi_ok
}

step_ci() {
    local sha
    sha=$(git rev-parse HEAD)
    if git rev-parse -q --verify "refs/tags/$T" >/dev/null; then ok "$T is tagged: CI was checked before tagging"; return; fi
    # bump may have pushed a moment ago: give the run 3 minutes to appear
    wait_run ci.yml "$sha" 18 >/dev/null || die "no CI run on main for $sha after 3 minutes" "push main first (git push origin main), or check Actions, then run again"
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
    local assets
    assets=$(gh release view "$T" -R "$REPO" --json assets -q '.assets[].name' 2>/dev/null || true)
    if grep -qx SHA256SUMS.txt <<<"$assets"; then
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
    if feed_done; then
        ok "website/latest.json already says $V"
    else
        highlights_ok || die "the update feed needs this release's highlights" "$highlights_fix --from site"
        card_ok || die "website/index.html has no 'new in $V' card and lede in What's new" "$card_fix"
        python3 - "$V" "$HIGHLIGHTS" <<'PY'
import json, sys
v, notes = sys.argv[1], sys.argv[2]
lines = [l.strip() for l in open(notes, encoding="utf-8") if l.strip()]
if not 3 <= len(lines) <= 5:
    sys.exit(f"{notes}: want 3-5 highlight lines, found {len(lines)}")
with open("website/latest.json", encoding="utf-8") as f:
    feed = json.load(f)
feed["version"], feed["highlights"] = v, lines
with open("website/latest.json", "w", encoding="utf-8") as f:
    json.dump(feed, f, indent=2, ensure_ascii=False)
    f.write("\n")
PY
        sed -i.bak -E "s/\"softwareVersion\": \"[0-9.]+\"/\"softwareVersion\": \"$V\"/; s/Version [0-9]+\.[0-9]+\.[0-9]+\. All builds/Version $V. All builds/" website/index.html
        rm -f website/index.html.bak
        grep -qF "\"softwareVersion\": \"$V\"" website/index.html && grep -qF "Version $V. All builds" website/index.html ||
            die "website/index.html's version text didn't update" "set softwareVersion and the 'Version X.Y.Z. All builds' line by hand, commit, run again"
        git add website/latest.json website/index.html
        commit -m "Website: $V is the current release (feed, download text)"
        push_main
        ok "pushed the feed and home page"
    fi
    # The Website workflow deploys main's site and the newest tag's guide; check what visitors get.
    wait_live https://sculptforge.app/latest.json "\"version\": \"$V\"" 600 ||
        die "sculptforge.app still serves the old feed after 10 minutes" "check the Website workflow: gh run list -R $REPO -w pages.yml -L 3; re-run it if it failed"
    ok "sculptforge.app/latest.json says $V"
    wait_live https://sculptforge.app/guide/whats-new.html "Version $V" 300 ||
        die "the live guide's What's New still doesn't show $V after 5 minutes" "gh workflow run pages.yml -R $REPO (it takes the guide from the newest tag), wait, run again"
    ok "the live guide shows Version $V"
}

step_aur() {
    release_ready
    on_main
    local sum
    sum=$(tarball_sha)
    if grep -q "^pkgver=$V\$" packaging/arch/PKGBUILD && grep -qF "'$sum'" packaging/arch/PKGBUILD; then
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
    grep -q "^pkgver=$V\$" packaging/arch/PKGBUILD && grep -qF "'$sum'" packaging/arch/PKGBUILD ||
        die "couldn't update packaging/arch/PKGBUILD" "set pkgver=$V, pkgrel=1 and the first sha256sums entry to $sum by hand, commit, run again"
    git add packaging/arch/PKGBUILD
    commit -m "AUR PKGBUILD builds $V"
    push_main
    ok "PKGBUILD at $V, source checksum $sum (computed here)"
}

step_brew() {
    release_ready
    local have dmg sum
    have=$(gh_file islandmed/homebrew-tap Casks/sculptforge.rb | sed -n 's/^ *version "\(.*\)"/\1/p')
    if [ "$have" = "$V" ]; then ok "Homebrew cask already at $V"; return; fi
    dmg="SculptForge-$V-darwin-universal.dmg"
    sum=$(asset_sha "$dmg")
    YES=1 tools/publish/setup-channels.sh brew >/dev/null || die "tools/publish/setup-channels.sh brew failed" "run it by hand to see why: YES=1 tools/publish/setup-channels.sh brew"
    gh_file islandmed/homebrew-tap Casks/sculptforge.rb | grep -F "sha256 \"$sum\"" >/dev/null ||
        die "the cask doesn't carry the checksum computed here ($sum)" "compare Casks/sculptforge.rb in islandmed/homebrew-tap with $dmg's real checksum and fix it"
    ok "Homebrew cask at $V ($sum)"
}

step_omarchy() {
    release_ready
    local state branch owner path=pkgbuilds/sculptforge/PKGBUILD sum pr
    pr=$(gh pr view "$OMARCHY_PR" -R omacom/omarchy-pkgs --json state,headRepositoryOwner,headRefName \
        -q '.state + " " + .headRepositoryOwner.login + " " + .headRefName' 2>/dev/null || true)
    read -r state owner branch <<<"$pr" || true
    [ "$state" = OPEN ] || die "omacom/omarchy-pkgs#$OMARCHY_PR is ${state:-unreadable}" "if it merged, open a version-bump PR for $path there and set OMARCHY_PR in this script; if it was closed, ask Tony"
    sum=$(tarball_sha)
    if gh_file "$owner/omarchy-pkgs" "$path" "$branch" | grep -x "pkgver=$V" >/dev/null; then
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
    (cd "$work/omarchy" && grep -q "^pkgver=$V\$" "$path" && grep -qF "'$sum'" "$path") ||
        die "couldn't update the Omarchy PKGBUILD" "edit $path on $owner/omarchy-pkgs branch $branch by hand (pkgver=$V, first sha256 $sum)"
    (cd "$work/omarchy" && git add "$path" && commit -m "sculptforge $V" && gitp push -q origin "HEAD:$branch") ||
        die "pushing to $owner/omarchy-pkgs $branch failed" "check gh auth status, then run again"
    ok "Omarchy PR #$OMARCHY_PR builds $V (author $AUTHOR_EMAIL)"
}

step_replies() {
    release_ready
    local out="release-replies-$V.md" issues
    awk -v h="## [$V]" 'index($0, h) == 1 {f = 1; next} /^## \[/ {f = 0} f' CHANGELOG.md > "$work/section"
    issues=$(grep -o '#[0-9]\+' "$work/section" | tr -d '#' | sort -un || true)  # no issues named is fine
    {
        echo "# Draft replies for SculptForge $V (read, edit, then post by hand; nothing here was posted)"
        echo
        for n in $issues; do
            local title state lines
            read -r state title < <(gh issue view "$n" -R "$REPO" --json state,title -q '.state + " " + .title' 2>/dev/null || echo "MISSING ?")
            [ "$state" = MISSING ] && continue
            lines=$(grep -m3 "#$n\b" "$work/section" | sed 's/^- //' || true)
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
