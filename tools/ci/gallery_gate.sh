#!/usr/bin/env bash
# T0's gallery check (tools/ci/t0.sh): a branch whose diff touches tools/brush-gallery/repo/ must match the public gallery
# repository (the app's default community gallery) before it merges: release.sh's preflight stops the stage at bump when the
# two differ (0.14.0, 11-24: a docs/readmes change reached main first and the stage stopped an hour in).
#   tools/ci/gallery_gate.sh BASE TIP     exit 0: not touched, matches, or the public tree can't be read (warns: release.sh checks again)
#                                         exit 1: differs (the message is on stdout)
base=${1:?base} tip=${2:?tip}
git diff --name-only "$base" "$tip" -- tools/brush-gallery/repo | grep -q . || exit 0
rc=0
out=$(python3 -I tools/brush-gallery/check_published.py 2>&1) || rc=$?
if [ $rc -eq 0 ]; then echo "gallery: tools/brush-gallery/repo changed and matches the public gallery"; exit 0; fi
if [ $rc -eq 2 ]; then echo "WARNING: gallery: could not compare with the public gallery ($(head -1 <<<"$out")); release.sh checks again"; exit 0; fi
echo "the public gallery repo needs the same change first: this branch changes tools/brush-gallery/repo, which differs from github.com/islandmed/sculptforge-brushes"
echo "copy tools/brush-gallery/repo into a clone of it, commit, push its main, then run T0 again (the owner pushes it if you have no access)"
echo
head -10 <<<"$out"
exit 1
