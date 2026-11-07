#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Releases SculptForge X.Y.Z from main and brings every channel to it in lock step (docs/releasing.md).
# Build once, promote: the packages are built and tested once, on `staging`, carrying X.Y.Z; the release publishes
# exactly those files.
#
#   scripts/release.sh X.Y.Z --stage                     1. in the merger's batch checkout: the bump commit on top of the
#                     batch, then scripts/merge-queue.sh staging (STAGE_WITH, default '~/Work/.sf/t1.sh .', runs here
#                     beside CI); main fast-forwards to it and packages.yml builds X.Y.Z on the push to staging
#   HIGHLIGHTS=notes.txt scripts/release.sh X.Y.Z --yes   2. on main: promotes. Checks main is the staged commit, waits for
#                     and checks staging's Packages run (tools/release/staged_run.py), tags that exact commit; release.yml
#                     verifies checksums and attestations, smoke-tests, signs and publishes; then site, feed, packages
#   scripts/release.sh X.Y.Z --rebuild --yes             the old way (fallback): bump pushed to main, the release builds
#                     its packages from the tag (about 40 minutes more)
#   scripts/release.sh X.Y.Z --from site                  resume at a step (after fixing what stopped it)
#   scripts/release.sh X.Y.Z --only replies              one step
#   scripts/release.sh X.Y.Z --only bump --staged        the bump commit only, not pushed (what --stage does first;
#                     RELEASE=X.Y.Z ~/Work/.sf/merge-batch.sh uses it before its own staging run)
#   scripts/release.sh X.Y.Z-beta.N --yes  a beta (also -rc.N): a members-only pre-release in the private repository
#                     islandmed/sculptforge-insiders, announced in #members (see "Betas" below and docs/releasing.md)
#   HIGHLIGHTS=file   the update feed's 3-5 highlight lines, one per line (default: the version's highlight: lines in
#                     What's New and the note fragments, tools/release/assemble_notes.py X.Y.Z --highlights)
#   (the home page's What's New area is written by tools/guide/build_site.py from docs/guide/whats-new.md: the
#   version's ```features block, its tagline and pictures; CARD is no longer used)
#
# Steps: bump check ci tag release site aur brew omarchy replies status (--stage: bump stage)
# bump sets every version file and rebuilds the guide (--stage: committed in the batch; --rebuild: pushed to main;
# promoting: only checks main already has it); check then verifies everything the later steps need, so a release
# either stops in seconds before the tag or runs through to the end.
# Each step looks at the live state first and skips what is already done, so running it again is always safe.
# When something is wrong it stops and prints the fix; fix it, then run again with --from <that step>.
# It never posts issue replies: it writes drafts (release-replies-X.Y.Z.md) that sfmonitor reads and posts.
# Betas (a member perk): steps bump check ci tag release status. bump sets CMakeLists.txt to X.Y.Z with
# SCULPTFORGE_PRERELEASE "beta.N" (the app says "X.Y.Z Beta N"; CHANGELOG, CITATION, metainfo and the home page stay
# at the stable release). release.yml publishes the beta in the private repository $INSIDERS_REPO (members are
# invited by hand) and posts it in #members; nothing public: no GitHub pre-release here, no update feed entry, no
# site, AUR, Homebrew, Omarchy, winget or issue replies. The stable release later is plain scripts/release.sh X.Y.Z,
# which clears the prerelease.
# Needs: gh logged in as islandmed (gh auth status), git, curl, python3, sha256sum (or shasum); run it from a clean
# checkout of main. Pushes to main and the tag are made here, so only the release coordinator runs it, after
# the owner's "ship it" for this version.
set -euo pipefail

REPO=islandmed/SculptForge
INSIDERS_REPO=islandmed/sculptforge-insiders # members' betas (release.yml publishes there with INSIDERS_REPO_TOKEN)
MEMBER_ENV=member-release
AUTHOR_NAME=islandmed
AUTHOR_EMAIL=232726012+islandmed@users.noreply.github.com # never a personal address in outside repos
# The AUR push (step aur) is on once this file exists on the releasing machine (packaging/arch/SUBMIT.md makes it).
AUR_FLAG=${AUR_FLAG:-$HOME/Work/.sf/aur-publish}
AUR_REPO=ssh://aur@aur.archlinux.org/sculptforge.git
OMARCHY_PR=1800
STEPS=(bump check ci tag release site aur brew omarchy replies status)

red() { printf '\033[1;31m%s\033[0m\n' "$*" >&2; }
say() { printf '\033[1m== %s\033[0m\n' "$*"; }
ok() { printf '   ok: %s\n' "$*"; }
# resume STEP: the exact command that continues at STEP, with this run's HIGHLIGHTS, BUILD_DIR and --yes (a stopped
# release is one the owner already said ship it for; without --yes, step tag would stop again at "not confirmed").
resume() {
    printf '%s%sscripts/release.sh %s%s --from %s --yes' "${HIGHLIGHTS_GIVEN:+HIGHLIGHTS=$HIGHLIGHTS_GIVEN }" \
        "${BUILD_DIR:+BUILD_DIR=$BUILD_DIR }" "$V" "${MODE_FLAG:-}" "$1"
}
# die "what is wrong" "the exact fix"
die() {
    red "STOPPED at step '$CURRENT': $1"
    printf '   fix: %s\n' "$2" >&2
    printf '   then: %s\n' "$(resume "$CURRENT")" >&2
    exit 1
}
sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
# lockretry CMD...: CMD again once, 3 s later, when it failed only on a git lock another worktree held (0.11.9: a batch
# merge's MERGE_RR.lock stopped the site step). Its output goes to stderr.
lockretry() {
    local out rc
    out=$("$@" 2>&1) && rc=0 || rc=$?
    if [ $rc -ne 0 ] && grep -Eq '\.lock.*: File exists|Unable to create .*\.lock' <<<"$out"; then
        echo "   a git lock was held (another worktree?): trying again in 3 s" >&2
        sleep 3
        out=$("$@" 2>&1) && rc=0 || rc=$?
    fi
    [ -z "$out" ] || printf '%s\n' "$out" >&2
    return $rc
}
commit() { lockretry git -c user.name="$AUTHOR_NAME" -c user.email="$AUTHOR_EMAIL" commit -q "$@"; }
gitp() { lockretry git -c credential.helper= -c credential.helper='!gh auth git-credential' "$@"; }
confirm() {
    [ -n "${YES:-}" ] && return 0
    read -r -p "$1 [y/N] " a
    [ "$a" = y ] || [ "$a" = Y ]
}

V=${1:-}
[[ "$V" =~ ^([0-9]+\.[0-9]+\.[0-9]+)(-((beta|rc)\.[0-9]+))?$ ]] ||
    { echo "usage: scripts/release.sh X.Y.Z[-beta.N|-rc.N] [--stage | --rebuild] [--from STEP | --only STEP] [--yes] [--staged]"; exit 2; }
BASE=${BASH_REMATCH[1]} PRE=${BASH_REMATCH[3]} # 0.11.0-beta.1: BASE 0.11.0, PRE beta.1; a stable release: PRE ""
[ -z "$PRE" ] || STEPS=(bump check ci tag release status)
RELEASE_REPO=$REPO; [ -z "$PRE" ] || RELEASE_REPO=$INSIDERS_REPO # where release.yml publishes this version
shift
FROM=bump ONLY= STAGED= MODE=promote MODE_FLAG=
while [ $# -gt 0 ]; do
    case $1 in
        --from) FROM=$2; shift 2 ;;
        --only) ONLY=$2; shift 2 ;;
        --yes) YES=1; shift ;;
        --staged) STAGED=1; shift ;;
        --stage) [ $MODE = promote ] || { echo "--stage and --rebuild exclude each other"; exit 2; }; MODE=stage MODE_FLAG=" --stage" STAGED=1 STEPS=(bump stage); shift ;;
        --rebuild) [ $MODE = promote ] || { echo "--stage and --rebuild exclude each other"; exit 2; }; MODE=rebuild MODE_FLAG=" --rebuild"; shift ;;
        *) echo "unknown argument $1"; exit 2 ;;
    esac
done
[[ " ${STEPS[*]} " == *" ${ONLY:-$FROM} "* ]] || { echo "unknown step ${ONLY:-$FROM}; steps: ${STEPS[*]}"; exit 2; }
[ -z "$STAGED" ] || [ "$ONLY" = bump ] || [ $MODE = stage ] || { echo "--staged goes with --only bump (the later steps run on main after it lands)"; exit 2; }
T=v$V
TODAY=$(date +%F)
CURRENT=setup
# HIGHLIGHTS may be relative to where the script was started; the script works from the top of the checkout.
case ${HIGHLIGHTS:-} in ''|/*) ;; *) HIGHLIGHTS=$PWD/$HIGHLIGHTS ;; esac
HIGHLIGHTS_GIVEN=${HIGHLIGHTS:-} # preflight may fill HIGHLIGHTS with a file of its own; resume names only this one
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
    gh release view "$T" -R "$RELEASE_REPO" --json assets -q '.assets[].name' > "$work/assets" 2>/dev/null ||
        die "no GitHub release $T yet" "run the earlier steps: $(resume tag)"
    grep -qx SHA256SUMS.txt "$work/assets" || die "release $T has no SHA256SUMS.txt (unfinished)" "re-run the Release workflow: gh workflow run release.yml -R $REPO --ref $T -f tag=$T"
    # One-click update (src/core/SelfUpdate.h): once the app has a key, every release must carry the signature.
    if grep -q 'kUpdatePublicKeyHex = "[0-9a-f]\{64\}"' src/core/SelfUpdate.h 2>/dev/null; then
        grep -qx SHA256SUMS.txt.sig "$work/assets" || die "release $T has no SHA256SUMS.txt.sig: apps can't update to it in one click" "check the UPDATE_SIGNING_KEY secret, then re-run the Release workflow: gh workflow run release.yml -R $REPO --ref $T -f tag=$T"
    fi
    # install/install.sh and install.ps1 (served from raw main) fetch from releases/latest: every name must be here (#159).
    local out
    out=$(python3 tools/release/check_install_assets.py --version "$V" --assets "$work/assets" 2>&1) ||
        die "$out" "fix the asset names in install/ (site lane, merges at once) or re-run the Release workflow if an upload is missing"
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
# server_error < log: the log shows an HTTP 5xx answer (curl, git, uv, flatpak-builder, FetchContent phrasings).
server_error() {
    grep -Eiq '(HTTP/[0-9.]+|HTTP status code|status code:?|error:|returned error:|response code) ?5[0-9]{2}\b|\b50[234] (Gateway Time-?out|Bad Gateway|Service Unavailable)\b'
}
# Successful run of a workflow on a push to main for a commit; waits for one in progress. The third argument is how
# many 10-second tries a just-pushed commit's run gets to appear (default 1).
wait_run() {
    local wf=$1 sha=$2 tries=${3:-1} id="" status conclusion rerun_done=""
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
    # A job that died on a GitHub/CDN 5xx while downloading (0.11.1: seven 504s in Packages) is rerun once, then judged.
    if [ "$conclusion" = failure ] && [ -z "$rerun_done" ] && gh run view "$id" -R "$REPO" --log-failed 2>/dev/null | server_error; then
        echo "   $wf run $id failed on an HTTP 5xx download: rerunning its failed jobs once" >&2
        rerun_done=1
        gh run rerun "$id" --failed -R "$REPO" >/dev/null 2>&1 || true
        sleep 10
        gh run watch "$id" -R "$REPO" --exit-status >/dev/null 2>&1 || true
        conclusion=$(gh run view "$id" -R "$REPO" --json conclusion -q .conclusion)
    fi
    [ "$conclusion" = success ] ||
        die "$wf run $id on $sha: $conclusion (https://github.com/$REPO/actions/runs/$id)" \
            "fix it on a branch, merge to main, then start again from step check (or re-run the run if it was a runner hiccup: gh run rerun $id --failed -R $REPO)"
    echo "$id"
}
# ci_reused SHA: prints "<ancestor> <run id>" when SHA differs from a main commit with a successful CI push run only by
# release paperwork (the bump commit: tools/release/paperwork_only.py checks the exact files and lines), so its CI
# stands for SHA and the release doesn't wait 20 minutes for the same answer. Fails safe: anything else, keep waiting.
# The same rule is in release.yml (job locate), which then promotes SHA's own Packages run.
ci_reused() {
    local sha=$1 c id
    for c in $(git rev-list --first-parent -n 8 "$sha~1" 2>/dev/null); do
        python3 tools/release/paperwork_only.py "$c" "$sha" >/dev/null 2>&1 || return 1
        id=$(gh run list -R "$REPO" -w ci.yml -c "$c" -e push -b main -L 1 --json databaseId -q '.[0].databaseId' 2>/dev/null)
        [ -n "$id" ] || continue
        if [ "$(gh run view "$id" -R "$REPO" --json status -q .status)" != completed ]; then
            echo "   waiting for ci.yml run $id on ${c:0:8} (the commit before the bump) ..." >&2
            gh run watch "$id" -R "$REPO" --exit-status >/dev/null 2>&1 || true
        fi
        [ "$(gh run view "$id" -R "$REPO" --json conclusion -q .conclusion)" = success ] && { echo "${c:0:8} $id"; return 0; }
    done
    return 1
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

# bumped: CMakeLists.txt (and for a stable release CHANGELOG) already say this version: step bump has been made.
bumped() {
    grep -q "^project(SculptForge VERSION ${BASE//./\\.}[ )]" CMakeLists.txt && prerelease_is "$PRE" &&
        { [ -n "$PRE" ] || grep -q "^## \[${V//./\\.}\]" CHANGELOG.md; }
}
# staged_run SHA [--wait]: the id of staging's Packages run that this release may promote (tools/release/staged_run.py:
# a push to staging by islandmed for exactly SHA, successful, at most SF_STAGED_MAX_DAYS (7) days old, artifacts
# present). Its exit status: 0 found, 3 there is no staging run for SHA, 1 none may be promoted (why in $work/staged.err).
staged_run() {
    python3 tools/release/staged_run.py --repo "$REPO" --sha "$1" --actors "$AUTHOR_NAME" --max-days "${SF_STAGED_MAX_DAYS:-7}" \
        ${2:+"$2"} 2>"$work/staged.err"
}
stage_fix() { echo "stage $V: in the merger's batch checkout, scripts/release.sh $V --stage (bump, staging, main), then $(resume ci); or build from the tag instead: ${1:-$(MODE_FLAG=" --rebuild" resume bump)}"; }

# --- preconditions: what the steps after the tag need, checked before anything is tagged --------------------------

# The home page's "new in X.Y.Z" card and "New in X.Y.Z:" lede in #whats-new, written by build_site.py (step bump) from
# the version's ```features block in whats-new.md, which needs a date and a tagline.
card_ok() { grep -qF "<small>new in $V</small>" website/index.html && grep -qF "class=\"section-lede\">New in $V:" website/index.html; }
features_ok() { awk -v v="## Version $BASE" '$0==v{f=1;next} f&&/^## /{exit} f' docs/guide/whats-new.md | grep '^tagline: .' >/dev/null; }
card_fix="give '## Version $V' in docs/guide/whats-new.md a \`\`\`features block (date: unreleased, tagline: ..., highlight: ..., cards), commit, push main, run again"
feed_done() { grep -qF "\"version\": \"$V\"" website/latest.json; }
prerelease_is() { grep -qx "set(SCULPTFORGE_PRERELEASE \"$1\")" CMakeLists.txt; }
# HIGHLIGHTS names a file of 3-5 non-empty lines.
highlights_ok() {
    local n
    [ -n "${HIGHLIGHTS:-}" ] && [ -f "$HIGHLIGHTS" ] || return 1
    n=$(grep -c '[^[:space:]]' "$HIGHLIGHTS" || true)
    [ "$n" -ge 3 ] && [ "$n" -le 5 ]
}
highlights_fix="write 3-5 one-line highlights (what users get) to a file and run again with HIGHLIGHTS=that-file"
# The words people write for What's New: the guide section and (after step bump wrote it) the UI test's pin to it.
words_ok() {
    python3 tools/check_marks_media.py >/dev/null 2>&1 || die "an image or video shows a mark: $(python3 tools/check_marks_media.py 2>/dev/null | grep -v '^check_marks_media:' | head -3)" "swap in a clean capture from current main (python3 tools/check_marks_media.py lists them), commit, push main"
    python3 tools/release/assemble_notes.py --check >/dev/null || die "a note fragment in changes/ doesn't parse: $(python3 tools/release/assemble_notes.py --check 2>&1)" "fix it (changes/README.md has the format), commit, push main"
    grep -q "^## Version ${BASE//./\\.}\$" docs/guide/whats-new.md || grep -qs '^whats_new:' changes/*.md ||
        die "docs/guide/whats-new.md has no '## Version $BASE'" "write that section at the top (Help > What's New shows it), commit, push main"
    # Numbers still waiting for a measurement are written {{NAME}}; none may ship.
    local ph
    ph=$(grep -Hn '{{[A-Z0-9_]*}}' docs/guide/whats-new.md CHANGELOG.md changes/*.md ${HIGHLIGHTS:+"$HIGHLIGHTS"} 2>/dev/null | head -5 || true)
    [ -z "$ph" ] || die "unfilled placeholders: $ph" "put the measured numbers in (or drop those lines), commit, push main"
    [ "${1:-}" = bump ] || python3 tools/release/pin_whats_new_test.py "$BASE" --check >/dev/null ||
        die "tests/ui/whats_new.sfui isn't pinned to $BASE's What's New" "run step bump (it pins it): $(resume bump)"
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
    [ -f "$b/CTestTestfile.cmake" ] || die "no build tree at $b for the DPI UI tests" "cmake -S . -B $b -DCMAKE_BUILD_TYPE=Release, or run again with BUILD_DIR=<your build>"
    "${heavy[@]}" cmake --build "$b" --target sculptforge -j3 >/dev/null || die "the app doesn't build in $b" "cmake --build $b --target sculptforge and fix the error"
    SDL_APP_ID=sculptforge-agent "${heavy[@]}" ctest --test-dir "$b" -L dpi --output-on-failure >"$b/release-dpi.log" 2>&1 ||
        die "the UI scale / DPI tests fail (ctest -L dpi; log: $b/release-dpi.log)" "fix them on a branch, merge to main, run again from step check"
    # What the bump changed (What's New, guide, notes) is tested here, on the bumped tree: step ci may reuse the CI
    # result of the commit before the bump (ci_reused), which never saw these files.
    local re green
    if green=$(git rev-list --first-parent -n 1 --grep '^Release \|^Beta ' --invert-grep HEAD 2>/dev/null) && [ -n "$green" ]; then
        re=$(python3 tools/test/impacted.py --build "$b" --base "$green" 2>/dev/null || true)
        if [ -n "$re" ] && [ "$re" != . ]; then
            "${heavy[@]}" cmake --build "$b" -j3 >/dev/null || die "the build in $b fails" "cmake --build $b and fix the error"
            SDL_APP_ID=sculptforge-agent "${heavy[@]}" ctest --test-dir "$b" -R "$re" --output-on-failure >"$b/release-bump.log" 2>&1 ||
                die "the tests of what the bump changed fail (ctest -R '$re'; log: $b/release-bump.log)" "fix the notes or the guide, commit, run again from step check"
            ok "the bump's own tests pass on the bumped tree ($re)"
        fi
    fi
    ok "UI scale tests (100-200 %, display-scale event) pass; Windows manifest declares PerMonitorV2"
}
# Everything the steps after the tag need that doesn't depend on the release existing. The home page card is
# checked through the features block until bump writes it.
preflight() {
    local mode=${1:-} t auth pr state owner branch pk out
    for t in git gh curl python3 base64 awk; do command -v "$t" >/dev/null || die "$t is not installed" "install $t"; done
    command -v sha256sum >/dev/null || command -v shasum >/dev/null || die "no sha256sum or shasum" "install coreutils"
    auth=$(gh auth status 2>&1 || true)
    grep -q "account $AUTHOR_NAME" <<<"$auth" || die "gh is not logged in as $AUTHOR_NAME" "gh auth login (or gh auth switch -u $AUTHOR_NAME)"
    words_ok "$mode"
    local gallery rc=0
    gallery=$(python3 tools/brush-gallery/check_published.py 2>/dev/null) || rc=$?
    [ $rc -ne 2 ] || die "$(head -1 <<<"$gallery")" "check the network and gh auth status, then run again"
    [ $rc -eq 0 ] || die "the public gallery (islandmed/sculptforge-brushes) differs from tools/brush-gallery/repo: $(head -3 <<<"$gallery")" "copy tools/brush-gallery/repo into a clone of it, commit, push its main"
    # Credits: every @handle in the notes is a GitHub account; a Discord-only requester is "name on Discord", no @.
    local credits; rc=0
    credits=$(python3 tools/release/check_credits.py 2>&1) || rc=$?
    [ $rc -ne 2 ] || die "$credits" "check the network and gh auth status, then run again"
    [ $rc -eq 0 ] || die "a credited @handle isn't a GitHub account: $credits" "fix the spelling, or for a Discord-only requester write \"thanks name on Discord\" (no @) in that changes/ file and the highlights, commit, run again"
    grep -q '^set(SCULPTFORGE_PRERELEASE "[a-z0-9.]*")$' CMakeLists.txt ||
        die "CMakeLists.txt lost its set(SCULPTFORGE_PRERELEASE \"\") line that step bump sets" "restore it under project() (git log -p CMakeLists.txt), commit"
    python3 -c 'import json, sys; json.load(open(sys.argv[1]))' website/latest.json 2>/dev/null || die "website/latest.json is not valid JSON" "fix it, commit"
    # The members' publishing path: release.yml's actions pinned to commit SHAs, INSIDERS_REPO_TOKEN only in the
    # member-release environment (docs/releasing.md, "Secrets").
    out=$(python3 tools/release/check_workflows.py 2>&1) || die "the release workflows aren't locked down: $out" "fix .github/workflows (docs/releasing.md, \"Secrets\"), commit, push main"
    # The update signing key: only in the protected environments (deployments from tags v*), never at repository
    # level, where every workflow on every branch (staging's packages included) could read it.
    if [ "$mode" != bump ]; then
        local env=release
        [ -z "$PRE" ] || env=$MEMBER_ENV
        gh secret list -R "$REPO" >"$work/secrets" 2>/dev/null || die "can't list the repository's secrets" "gh auth status (the owner's account)"
        ! grep -q '^UPDATE_SIGNING_KEY\b' "$work/secrets" ||
            die "UPDATE_SIGNING_KEY is a repository secret: any workflow on any branch could read it" \
                "the owner moves it (the PEM from Drive \"SculptForge keys\"): gh secret set UPDATE_SIGNING_KEY --env release -R $REPO < key.pem (member-release has it already), then gh secret delete UPDATE_SIGNING_KEY -R $REPO"
        gh secret list --env "$env" -R "$REPO" 2>/dev/null | grep '^UPDATE_SIGNING_KEY\b' >/dev/null ||
            die "the $env environment has no UPDATE_SIGNING_KEY: release.yml couldn't sign SHA256SUMS.txt" \
                "the owner sets it (the PEM from Drive \"SculptForge keys\"): gh secret set UPDATE_SIGNING_KEY --env $env -R $REPO < key.pem"
    fi
    if [ -n "$PRE" ]; then # a beta: members only, through the private repository; nothing of the site, no packages
        gh secret list --env member-release -R "$REPO" 2>/dev/null | grep '^INSIDERS_REPO_TOKEN\b' >/dev/null ||
            die "the member-release environment has no INSIDERS_REPO_TOKEN, so release.yml can't publish the beta to $INSIDERS_REPO" \
                "the owner pastes it (fine-grained token, only $INSIDERS_REPO, Contents: read and write): wl-paste -n | gh secret set INSIDERS_REPO_TOKEN --env member-release -R $REPO"
        gh repo view "$INSIDERS_REPO" >/dev/null 2>&1 || die "can't see $INSIDERS_REPO" "gh auth status: log in as the owner of $INSIDERS_REPO"
        ok "What's New $BASE is ready and $INSIDERS_REPO takes the beta (a beta has no site, package or reply steps)"
        return
    fi
    # site (bump writes the home page's card from the features block)
    card_ok || features_ok || die "docs/guide/whats-new.md: Version $V has no \`\`\`features block with a tagline" "$card_fix"
    if ! feed_done; then
        if [ -z "${HIGHLIGHTS:-}" ]; then # the version's highlight lines (What's New features block + note fragments)
            HIGHLIGHTS=$(mktemp)
            python3 tools/release/assemble_notes.py "$BASE" --highlights >"$HIGHLIGHTS"
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
    # .SRCINFO names the same version, release and source checksum (the full comparison is CI's, arch.yml)
    for f in pkgver pkgrel; do
        grep -qx "$(printf '\t%s = %s' "$f" "$(sed -n "s/^$f=//p" "$pk")")" packaging/arch/.SRCINFO ||
            die "packaging/arch/.SRCINFO's $f differs from the PKGBUILD's" "(cd packaging/arch && makepkg --printsrcinfo > .SRCINFO), commit"
    done
    grep -qF "$(printf '\tsha256sums = %s' "$(sed -n "s/^sha256sums=('\([0-9a-f]*\)'.*/\1/p" "$pk")")" packaging/arch/.SRCINFO ||
        die "packaging/arch/.SRCINFO's source checksum differs from the PKGBUILD's" "(cd packaging/arch && makepkg --printsrcinfo > .SRCINFO), commit"
    if [ -f "$AUR_FLAG" ]; then
        GIT_SSH_COMMAND="ssh -o BatchMode=yes -o ConnectTimeout=15" git ls-remote "$AUR_REPO" >/dev/null 2>&1 ||
            die "the AUR doesn't accept this machine's SSH key ($AUR_REPO), and $AUR_FLAG turns the AUR push on" "ssh -T aur@aur.archlinux.org to see why (key in My Account on aur.archlinux.org), or remove $AUR_FLAG to skip the AUR"
    fi
    # brew
    [ -x tools/publish/setup-channels.sh ] || die "tools/publish/setup-channels.sh is missing or not executable" "git checkout origin/main -- tools/publish/setup-channels.sh"
    gh_file islandmed/homebrew-tap Casks/sculptforge.rb | grep '^ *version "' >/dev/null ||
        die "can't read Casks/sculptforge.rb in islandmed/homebrew-tap" "check gh auth status and that the tap exists (YES=1 tools/publish/setup-channels.sh brew sets it up)"
    # omarchy
    pr=$(gh pr view "$OMARCHY_PR" -R omacom/omarchy-pkgs --json state,headRepositoryOwner,headRefName \
        -q '.state + " " + .headRepositoryOwner.login + " " + .headRefName' 2>/dev/null || true)
    read -r state owner branch <<<"$pr" || true
    [ "$state" = OPEN ] || die "omacom/omarchy-pkgs#$OMARCHY_PR is ${state:-unreadable}" "if it merged, open a version-bump PR for pkgbuilds/sculptforge/PKGBUILD there and set OMARCHY_PR in this script; if it was closed, ask the owner"
    gh_file "$owner/omarchy-pkgs" pkgbuilds/sculptforge/PKGBUILD "$branch" | grep -E "^sha256sums=\('[0-9a-f]{64}'" >/dev/null ||
        die "the Omarchy PKGBUILD on $owner/omarchy-pkgs $branch has no sha256sums=('<64 hex>' line" "fix it on that branch"
    ok "What's New, home page card, highlights, PKGBUILD, Homebrew tap and Omarchy PR #$OMARCHY_PR are ready"
}

# --- steps --------------------------------------------------------------------------------------------------------

# Sets every version file to X.Y.Z, rebuilds the guide and the home page's What's New area, pushes main (--staged:
# commits on top of the checked-out batch and pushes nothing; scripts/merge-queue.sh staging lands it).
step_bump() {
    if [ $MODE = promote ] && [ -z "$STAGED" ]; then # the bump was staged with the packages: main must already have it
        on_main
        bumped || die "main is not at $V yet: the bump goes through staging with the packages it names" "$(stage_fix)"
        ok "main $(git rev-parse --short HEAD) already carries $V (staged)"
        return
    fi
    if [ -n "$STAGED" ]; then
        [ -z "$(git status --porcelain --untracked-files=no)" ] || die "uncommitted changes" "commit them first (git status)"
        git fetch -q origin main --tags
        git merge-base --is-ancestor origin/main HEAD || die "HEAD does not contain origin/main" "merge origin/main into the batch first"
    else
        on_main
    fi
    preflight bump
    local out
    # A beta's #testers checklist comes from the fragments, so it is written before they go (step status names it).
    if [ -n "$PRE" ] && ls changes/*.md 2>/dev/null | grep -v '/README.md$' >/dev/null; then
        python3 tools/release/assemble_notes.py "$BASE" --testers >"release-testers-$V.md"
    fi
    # Note fragments (changes/<branch>.md) into CHANGELOG [Unreleased] and What's New X.Y.Z; the fragments go.
    out=$(python3 tools/release/assemble_notes.py "$BASE" --apply 2>&1) || die "$out" "fix that fragment in changes/, commit, push main"
    grep -q 'written' <<<"$out" && echo "$out" | sed 's/^/   notes: /'
    # The What's New UI test checks the guide's newest three sections: pinned here to this version's.
    out=$(python3 tools/release/pin_whats_new_test.py "$BASE" 2>&1) || die "$out" "fix docs/guide/whats-new.md (its newest section must be '## Version $BASE'), commit, push main"
    [ -n "$out" ] && echo "   pinned: $out"
    out=$(python3 - "$BASE" "$TODAY" "$PRE" <<'PY'
import re, sys
v, today, pre = sys.argv[1:]
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
    t = new[p] = sub1(p, t, r"^(project\(SculptForge VERSION )[0-9]+(?:\.[0-9]+)*", rf"\g<1>{v}")
if f'set(SCULPTFORGE_PRERELEASE "{pre}")' not in t:  # a beta sets it, the stable release clears it
    new[p] = sub1(p, t, r'^set\(SCULPTFORGE_PRERELEASE "[^"]*"\)$', f'set(SCULPTFORGE_PRERELEASE "{pre}")')
if pre:  # a beta: CITATION, metainfo, CHANGELOG, What's New's date and the home page wait for the stable release
    for p, t in new.items():
        open(p, "w", encoding="utf-8").write(t)
        print(p)
    sys.exit(0)

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
    python3 tools/guide/build_site.py >/dev/null # the guide, and the home page's What's New area for $V (not a beta's)
    [ -n "$PRE" ] || card_ok || die "website/index.html has no 'new in $V' card after build_site.py" "$card_fix"
    git add CMakeLists.txt CITATION.cff packaging/rez/package.py packaging/linux/org.sculptforge.SculptForge.metainfo.xml CHANGELOG.md website/index.html docs/guide/whats-new.md
    git add -A changes tests/ui/whats_new.sfui
    git add -A website/guide
    if git diff --cached --quiet; then
        ok "every version file already says $V and the guide is built"
        return
    fi
    if [ -n "$PRE" ]; then
        commit -m "Beta $V: CMake version $BASE $PRE, notes, guide"
    else
        commit -m "Release $V: version files, CHANGELOG section, guide"
    fi
    if [ -n "$STAGED" ]; then
        ok "version files at $V, guide rebuilt, committed $(git rev-parse --short HEAD) (not pushed: scripts/merge-queue.sh staging HEAD lands it)"
        return
    fi
    push_main
    ok "version files at $V, guide rebuilt, pushed main $(git rev-parse --short HEAD)"
}

# --stage: the batch with the bump on top goes through staging (CI there, STAGE_WITH here); main fast-forwards to it
# and packages.yml builds the packages of X.Y.Z on the push to staging, which the release then promotes.
step_stage() {
    local sha with=${STAGE_WITH-}
    sha=$(git rev-parse HEAD)
    bumped || die "HEAD ${sha:0:8} is not at $V" "run the bump first: $(resume bump)"
    [ -n "${STAGE_WITH+x}" ] || { [ -x "$HOME/Work/.sf/t1.sh" ] && with="$HOME/Work/.sf/t1.sh ."; }
    "${MERGE_QUEUE:-scripts/merge-queue.sh}" staging ${with:+--with "$with"} "$sha" ||
        die "staging ${sha:0:8} is red, or main moved (above)" "fix the batch (main moved: merge origin/main), then $(resume bump)"
    ok "main is the staged ${sha:0:8} ($V); packages.yml is building its packages on the staging push"
    # The staging push's Packages run builds what --yes will promote, the macOS .dmg included (deployment target 11:
    # 0.11.10's std::to_chars(float) passed CI and failed only there, noticed by hand). Wait for it here.
    local id="" tries=30 failed
    while [ -z "$id" ] && [ "$tries" -gt 0 ]; do
        id=$(gh run list -R "$REPO" -w packages.yml -c "$sha" -b staging -e push -L 1 --json databaseId -q '.[0].databaseId')
        tries=$((tries - 1))
        [ -n "$id" ] || sleep 10
    done
    [ -n "$id" ] || die "no Packages run on the staging push of ${sha:0:8} after 5 minutes" "gh run list -R $REPO -w packages.yml -L 3, then $(resume stage)"
    echo "   waiting for Packages run $id on staging (every platform's package) ..." >&2
    gh run watch "$id" -R "$REPO" --interval 60 >/dev/null 2>&1 || true
    if [ "$(gh run view "$id" -R "$REPO" --json conclusion -q .conclusion)" != success ]; then
        failed=$(gh run view "$id" -R "$REPO" --json jobs -q '[.jobs[] | select(.conclusion == "failure") | .name] | join(", ")')
        die "Packages on staging failed (${failed:-see the run}): https://github.com/$REPO/actions/runs/$id" \
            "fix it on top of ${sha:0:8} (main is already there), then $(resume stage)"
    fi
    ok "Packages run $id built every package for ${sha:0:8}"
    echo "   next, on main: ${HIGHLIGHTS_GIVEN:+HIGHLIGHTS=$HIGHLIGHTS_GIVEN }scripts/release.sh $V --yes (promotes that run's packages)"
}

step_check() {
    on_main
    preflight
    local bump="run step bump: scripts/release.sh $V --only bump (it sets every version file)"
    [ $MODE = rebuild ] || bump=$(stage_fix "scripts/release.sh $V --rebuild --only bump, then $(MODE_FLAG=" --rebuild" resume check)")
    grep -q "^project(SculptForge VERSION ${BASE//./\\.}[ )]" CMakeLists.txt || die "CMakeLists.txt is not at $BASE" "$bump"
    prerelease_is "$PRE" || die "CMakeLists.txt's SCULPTFORGE_PRERELEASE is not \"$PRE\"" "$bump"
    if [ -n "$PRE" ]; then
        python3 tools/guide/build_site.py >/dev/null
        [ -z "$(git status --porcelain -- website/guide website/index.html)" ] ||
            die "website/guide is not rebuilt from docs/guide, or the home page changed (a beta leaves it alone)" "$bump"
        if git rev-parse -q --verify "refs/tags/$T" >/dev/null && [ "$(git rev-list -n1 "$T")" != "$(git rev-parse HEAD)" ]; then
            die "$T already tags $(git rev-list -n1 "$T"), not main's $(git rev-parse --short HEAD)" "if that is the beta, continue: $(resume release); if not, use the next number ($BASE-${PRE%.*}.$((${PRE##*.} + 1)))"
        fi
        ok "CMakeLists.txt says $BASE $PRE and the guide is built at $(git rev-parse --short HEAD)"
        dpi_ok
        return
    fi
    grep -q "^project(SculptForge VERSION ${V//./\\.}[ )]" CMakeLists.txt || die "CMakeLists.txt is not at $V" "$bump"
    grep -q "^## \[${V//./\\.}\]" CHANGELOG.md || die "CHANGELOG.md has no '## [$V] - date' section" "$bump"
    grep -qF "<release version=\"$V\"" packaging/linux/org.sculptforge.SculptForge.metainfo.xml || die "metainfo has no <release version=\"$V\">" "$bump"
    grep -qx "version: $V" CITATION.cff || die "CITATION.cff is not at $V" "$bump"
    grep -qx "version = \"$V\"" packaging/rez/package.py || die "packaging/rez/package.py is not at $V" "$bump"
    python3 tools/guide/build_site.py >/dev/null
    [ -z "$(git status --porcelain -- website/guide)" ] || die "website/guide is not rebuilt from docs/guide" "$bump"
    if git rev-parse -q --verify "refs/tags/$T" >/dev/null && [ "$(git rev-list -n1 "$T")" != "$(git rev-parse HEAD)" ]; then
        die "$T already tags $(git rev-list -n1 "$T"), not main's $(git rev-parse --short HEAD)" "if that is the release, continue: $(resume release); if not, pick the next version"
    fi
    ok "version files, CHANGELOG, What's New, metainfo and guide say $V at $(git rev-parse --short HEAD)"
    dpi_ok
}

step_ci() {
    local sha
    sha=$(git rev-parse HEAD)
    if git rev-parse -q --verify "refs/tags/$T" >/dev/null; then ok "$T is tagged: CI was checked before tagging"; return; fi
    local green
    if green=$(ci_reused "$sha"); then
        ok "CI: ${sha:0:8} only adds release paperwork to ${green%% *}, whose CI run ${green##* } passed (tools/release/paperwork_only.py): reused"
    else
        # bump may have pushed a moment ago: give the run 3 minutes to appear
        wait_run ci.yml "$sha" 18 >/dev/null || die "no CI run on main for $sha after 3 minutes" "push main first (git push origin main), or check Actions, then run again"
        ok "CI passed on $sha"
    fi
    if [ $MODE = rebuild ]; then
        echo "   note: --rebuild: the Release workflow builds the packages from the tag (about 40 min)"
        return
    fi
    local run rc=0
    echo "   staging's Packages run for ${sha:0:8} (waits while it runs) ..."
    run=$(staged_run "$sha" --wait) || rc=$?
    case $rc in
        0) ok "staging's Packages run $run built and tested $V for ${sha:0:8}: the release promotes exactly its packages" ;;
        3) die "no Packages run on staging for ${sha:0:8}" "$(stage_fix)" ;;
        *) die "staging's Packages runs for ${sha:0:8} can't be promoted: $(tail -1 "$work/staged.err")" \
               "a failed run that was a runner hiccup: gh run rerun <id> --failed -R $REPO, then $(resume ci); otherwise $(stage_fix)" ;;
    esac
}

step_tag() {
    if gh api "repos/$REPO/git/ref/tags/$T" >/dev/null 2>&1; then ok "$T is already pushed"; return; fi
    local sha
    sha=$(git rev-parse HEAD)
    if [ $MODE = promote ]; then # the tag goes on exactly the staged commit, which main is
        git fetch -q origin main
        [ "$sha" = "$(git rev-parse origin/main)" ] || die "HEAD ${sha:0:8} is not origin/main $(git rev-parse --short origin/main)" "git switch main && git merge --ff-only origin/main; if main moved past the staged commit, stage again: $(stage_fix)"
        staged_run "$sha" >/dev/null || die "${sha:0:8} has no promotable staging Packages run: $(tail -1 "$work/staged.err")" "$(stage_fix)"
    fi
    confirm "Tag $T at $(git log -1 --format='%h %s' "$sha") and push it? This publishes SculptForge $V${PRE:+ for members, in $INSIDERS_REPO}." ||
        die "not confirmed" "run again and answer y (or YES=1) once the owner has said ship it for $V"
    git rev-parse -q --verify "refs/tags/$T" >/dev/null || git tag "$T" "$sha"
    gitp push -q origin "$T" || die "pushing $T failed" "git push origin $T (check gh auth status)"
    ok "pushed $T"
}

step_release() {
    local assets
    assets=$(gh release view "$T" -R "$RELEASE_REPO" --json assets -q '.assets[].name' 2>/dev/null || true)
    if grep -qx SHA256SUMS.txt <<<"$assets"; then
        ok "release $T is published"
    else
        local id=""
        for _ in $(seq 30); do
            id=$(gh run list -R "$REPO" -w release.yml -b "$T" -L 1 --json databaseId -q '.[0].databaseId')
            [ -n "$id" ] && break
            sleep 10
        done
        [ -n "$id" ] || die "no Release run for $T after 5 minutes" "gh workflow run release.yml -R $REPO --ref $T -f tag=$T"
        echo "   waiting for Release run $id (https://github.com/$REPO/actions/runs/$id) ..."
        gh run watch "$id" -R "$REPO" --exit-status >/dev/null ||
            die "Release run $id failed (https://github.com/$REPO/actions/runs/$id)" \
                "read the failed job; a runner hiccup: gh run rerun $id --failed -R $REPO; a real bug: fix on main, delete the tag (git push origin :$T; git tag -d $T) and release $V again from step check"
    fi
    release_ready
    local n
    n=$(wc -l < "$work/assets")
    [ "$n" -ge 30 ] || die "release $T has only $n assets" "re-run the Release workflow: gh workflow run release.yml -R $REPO --ref $T -f tag=$T"
    ok "$n assets, SHA256SUMS.txt included"
}

# website_run SHA: waits for the Website run of SHA's push. When its deploy job fails, a fresh run of the workflow
# on main (a rerun of the failed job can't deploy: the first attempt's github-pages artifact is still there,
# "Multiple artifacts named github-pages"). GitHub's Pages service can answer "try again later" for a while with
# its status page green (0.11.8: three runs in 15 minutes, the fourth 10 minutes later worked): then it waits
# 10 minutes before each fresh run, up to 3 of them. Any other deploy failure gets one fresh run.
website_run() {
    local id="" tries=18 c last attempt
    while [ -z "$id" ] && [ "$tries" -gt 0 ]; do
        id=$(gh run list -R "$REPO" -w pages.yml -c "$1" -e push -b main -L 1 --json databaseId -q '.[0].databaseId')
        tries=$((tries - 1))
        [ -n "$id" ] || sleep 10
    done
    [ -n "$id" ] || { echo "   no Website run found for ${1:0:8}: waiting on the live feed alone" >&2; return 0; }
    for attempt in 1 2 3 4; do
        if [ "$(gh run view "$id" -R "$REPO" --json status -q .status)" != completed ]; then
            echo "   waiting for the Website run $id ..." >&2
            gh run watch "$id" -R "$REPO" --exit-status >/dev/null 2>&1 || true
        fi
        c=$(gh run view "$id" -R "$REPO" --json conclusion -q .conclusion)
        [ "$c" = success ] && return 0
        gh run view "$id" -R "$REPO" --json jobs -q '.jobs[] | select(.conclusion == "failure") | .name' 2>/dev/null |
            grep -x deploy >/dev/null ||
            die "the Website run $id: $c (https://github.com/$REPO/actions/runs/$id)" "fix it on main or start it again: gh workflow run pages.yml -R $REPO --ref main; then run again with --from site"
        [ "$attempt" -lt 4 ] ||
            die "GitHub Pages still fails to deploy after 3 fresh runs (last: https://github.com/$REPO/actions/runs/$id)" "wait, then: gh workflow run pages.yml -R $REPO --ref main; then run again with --from site"
        if gh run view "$id" -R "$REPO" --log-failed 2>/dev/null | grep -i "try again later" >/dev/null; then
            echo "   GitHub Pages said 'try again later' (run $id): waiting 10 minutes, then a fresh run ($attempt of 3)" >&2
            sleep 600
        elif [ "$attempt" -gt 1 ]; then
            die "the Website's deploy failed again (https://github.com/$REPO/actions/runs/$id)" "check the run's deploy log, fix it on main, then run again with --from site"
        else
            echo "   the Website's deploy failed (run $id): starting a fresh run on main" >&2
        fi
        last=$(gh run list -R "$REPO" -w pages.yml -e workflow_dispatch -b main -L 1 --json databaseId -q '.[0].databaseId')
        gh workflow run pages.yml -R "$REPO" --ref main >/dev/null ||
            die "can't start the Website workflow" "gh workflow run pages.yml -R $REPO --ref main; then run again with --from site"
        id="" tries=18
        while [ -z "$id" ] && [ "$tries" -gt 0 ]; do
            sleep 10
            id=$(gh run list -R "$REPO" -w pages.yml -e workflow_dispatch -b main -L 1 --json databaseId -q '.[0].databaseId')
            [ "$id" != "$last" ] || id=""
            tries=$((tries - 1))
        done
        [ -n "$id" ] || die "the fresh Website run didn't start within 3 minutes" "gh run list -R $REPO -w pages.yml -L 3; then run again with --from site"
    done
}

step_site() {
    release_ready
    on_main
    # A run stopped after committing the feed but before pushing it (0.11.9: a .git lock) leaves main ahead of
    # origin/main: push that first. And judge the feed by origin/main, what the site deploys, not the local file.
    local ahead
    ahead=$(git rev-list --count origin/main..HEAD)
    if [ "$ahead" -gt 0 ]; then
        push_main
        ok "pushed the $ahead commit(s) on main a stopped run had made"
        git fetch -q origin main
    fi
    if git show origin/main:website/latest.json 2>/dev/null | grep -F "\"version\": \"$V\"" >/dev/null; then
        ok "origin/main's website/latest.json already says $V"
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
def key(s):  # 0.11.0-beta.1 < 0.11.0, as src/core/UpdateCheck.cpp orders them
    n, _, pre = s.partition("-")
    return [int(x) for x in n.split(".")], (0, pre) if pre else (1, "")
pv = feed.get("channels", {}).get("preview", {}).get("version", "")
if pv and key(pv) <= key(v):  # stable passed the beta: Preview is offered stable (the app checks this too)
    del feed["channels"]["preview"]
    if not feed["channels"]:
        del feed["channels"]
with open("website/latest.json", "w", encoding="utf-8") as f:
    json.dump(feed, f, indent=2, ensure_ascii=False)
    f.write("\n")
PY
        sed -i.bak -E "s/\"softwareVersion\": \"[0-9.]+\"/\"softwareVersion\": \"$V\"/; s/Version [0-9]+\.[0-9]+\.[0-9]+\. All builds/Version $V. All builds/" website/index.html
        rm -f website/index.html.bak
        grep -qF "\"softwareVersion\": \"$V\"" website/index.html && grep -qF "Version $V. All builds" website/index.html ||
            die "website/index.html's version text didn't update" "set softwareVersion and the 'Version X.Y.Z. All builds' line by hand, commit, run again"
        lockretry git add website/latest.json website/index.html
        commit -m "Website: $V is the current release (feed, download text)"
        push_main
        ok "pushed the feed and home page"
    fi
    # The Website workflow deploys main's site and the newest tag's guide. Its run for the feed commit can sit queued
    # longer than any fixed wait (0.11.7: past 10 minutes), so wait for that run, then check what visitors get.
    website_run "$(git log -1 --format=%H -- website/latest.json)"
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
        aur_push
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
    (cd packaging/arch && makepkg --printsrcinfo > .SRCINFO) || die "makepkg --printsrcinfo failed" "run it in packaging/arch by hand, commit, run again"
    git add packaging/arch/PKGBUILD packaging/arch/.SRCINFO
    commit -m "AUR PKGBUILD builds $V"
    push_main
    ok "PKGBUILD at $V, source checksum $sum (computed here)"
    aur_push
}

# aur_push: the PKGBUILD and .SRCINFO from main to the AUR, when $AUR_FLAG exists (off until the package is submitted).
aur_push() {
    if [ ! -f "$AUR_FLAG" ]; then
        echo "   AUR push off ($AUR_FLAG absent; packaging/arch/SUBMIT.md publishes the package first)"
        return
    fi
    rm -rf "$work/aur"
    GIT_SSH_COMMAND="ssh -o BatchMode=yes" git clone -q "$AUR_REPO" "$work/aur" 2>/dev/null ||
        die "can't clone $AUR_REPO" "ssh -T aur@aur.archlinux.org to check the key, run again"
    if [ -f "$work/aur/.SRCINFO" ] && grep -qx "$(printf '\tpkgver = %s' "$V")" "$work/aur/.SRCINFO"; then
        ok "AUR already at $V"; return
    fi
    cp packaging/arch/PKGBUILD packaging/arch/.SRCINFO "$work/aur/"
    (cd "$work/aur" && git add PKGBUILD .SRCINFO && commit -m "sculptforge $V" &&
        GIT_SSH_COMMAND="ssh -o BatchMode=yes" git push -q origin HEAD:master) ||
        die "pushing sculptforge $V to the AUR failed" "ssh -T aur@aur.archlinux.org to check the key, run again"
    ok "AUR at $V"
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
    [ "$state" = OPEN ] || die "omacom/omarchy-pkgs#$OMARCHY_PR is ${state:-unreadable}" "if it merged, open a version-bump PR for $path there and set OMARCHY_PR in this script; if it was closed, ask the owner"
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
    local out="release-replies-$V.md"
    awk -v h="## [$V]" 'index($0, h) == 1 {f = 1; next} /^## \[/ {f = 0} f' CHANGELOG.md > "$work/section"
    # Whole entries, the reporter's @handle, a REVIEW line where the entry may not cover the issue; issues that
    # already have our reply for $V are left out (tools/release/reply_drafts.py).
    python3 tools/release/reply_drafts.py "$V" "$work/section" "$REPO" "$T" > "$out" 2> "$work/drafts" ||
        die "couldn't write the reply drafts" "python3 tools/release/reply_drafts.py --self-test"
    ok "$(cat "$work/drafts") in $out: sfmonitor posts these (it reads them first)"
}

step_status() {
    if [ -n "$PRE" ]; then
        echo "   Beta:     https://github.com/$INSIDERS_REPO/releases/tag/$T (members only; nothing public)"
        echo "   Discord:  release.yml posted it in #members (DISCORD_MEMBERS_WEBHOOK)"
        echo "   Members:  invite new members' GitHub usernames (from the Stripe checkout) by hand:"
        echo "             gh api -X PUT repos/$INSIDERS_REPO/collaborators/USERNAME -f permission=pull"
        if [ -f "release-testers-$V.md" ]; then
            echo "   Testers:  $PWD/release-testers-$V.md (read it, then post it in #members)"
        else
            echo "   Testers:  no checklist (the note fragments had no testers: lines)"
        fi
        echo "   Stable:   when testers are happy: HIGHLIGHTS=f scripts/release.sh $BASE --yes (clears the prerelease)"
        return
    fi
    local winget
    winget=$(gh pr view 464465 -R microsoft/winget-pkgs --json state -q .state 2>/dev/null || echo unknown)
    echo "   winget-pkgs#464465: $winget (update it once it merges; waits on the CLA)"
    echo "   Release:  https://github.com/$REPO/releases/tag/$T"
    echo "   Replies:  sfmonitor posts release-replies-$V.md (after reading it). Then update the handoff note."
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
