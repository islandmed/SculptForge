#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/merge-queue.sh wait_ci (ctest merge_queue_wait_ci): only a FINISHED run's conclusion is judged. A `gh run watch` that
# errors while the run is still going is retried (0.14.1: read as red, the merger landed main by hand); a run whose state can't be
# read is "unknown", never "failure". Stand-in gh and sleep, no network.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
mkdir -p "$S/bin" "$S/home" "$S/state" "$S/repo"
printf '#!/bin/sh\nexit 0\n' > "$S/bin/sleep"
# gh state: $GH_STATE/view = lines, one per `run view` call (the last repeats; ERROR = the call fails); `run watch` always errors
cat > "$S/bin/gh" <<'STUB'
#!/bin/bash
n() { local c=0; [ -f "$GH_STATE/$1.n" ] && c=$(cat "$GH_STATE/$1.n"); echo $((c + 1)) > "$GH_STATE/$1.n"; echo $((c + 1)); }
case "$*" in
  "run list"*) if [ "$(n list)" = 1 ]; then :; else echo 777; fi ;;
  "workflow run"*) ;;
  "run watch"*) echo "HTTP 502: Bad Gateway" >&2; exit 1 ;;
  "run view"*"--json status,conclusion"*)
     i=$(n view); l=$(sed -n "${i}p" "$GH_STATE/view"); [ -n "$l" ] || l=$(tail -1 "$GH_STATE/view")
     [ "$l" = ERROR ] && { echo "error connecting to api.github.com" >&2; exit 1; }
     echo "$l" ;;
esac
STUB
chmod +x "$S/bin/gh" "$S/bin/sleep"
export PATH=$S/bin:$PATH HOME=$S/home GH_STATE=$S/state REPO=x/y SF_CI_POLL_SLEEP=0 SF_CI_ERR_MAX=4
git init -q "$S/repo" && cd "$S/repo"
git -c user.name=t -c user.email=t@t commit -q --allow-empty -m x
git update-ref refs/remotes/origin/staging HEAD
# shellcheck disable=SC1090
eval "$(sed -n '/^wait_ci()/,/^run_serial()/p' "$ROOT/scripts/merge-queue.sh" | sed '$d')"
die() { echo "die: $*" >&2; exit 1; }
run() { rm -f "$GH_STATE"/*; printf '%s\n' "$@" > "$GH_STATE/view"; wait_ci staging 2>"$S/err"; }
views() { cat "$GH_STATE/view.n" 2>/dev/null || echo 0; }

# 1. the watch errors while the run is still going; it finishes green: success, not red
out=$(run "in_progress " "queued " "in_progress " "completed success")
[ "$out" = success ] || fail "a watch error while running was judged '$out': $(cat "$S/err")"
# 2. a read error between polls is retried too
out=$(run "in_progress " ERROR "completed success")
[ "$out" = success ] || fail "a view error was judged '$out'"
# 3. a run that finishes red is red; cancelled is not success either
[ "$(run "in_progress " "completed failure")" = failure ] || fail "a failed run was not reported as failure"
[ "$(run "completed cancelled")" = failure ] || fail "cancelled was not failure"
# 4. a state that can never be read is unknown (SF_CI_ERR_MAX polls), never failure
out=$(run ERROR)
[ "$out" = unknown ] || fail "an unreadable state gave '$out', not unknown"
grep -q "could not be read" "$S/err" || fail "no message for an unreadable run"
# 5. infra_retry reran the failed jobs: the finished rerun's conclusion decides
infra_retry() { printf '%s\n' "in_progress " "completed success" > "$GH_STATE/view"; rm -f "$GH_STATE/view.n"; return 0; }
[ "$(run "completed failure")" = success ] || fail "a green rerun was not success"
infra_retry() { printf '%s\n' "completed failure" > "$GH_STATE/view"; rm -f "$GH_STATE/view.n"; return 0; }
[ "$(run "completed failure")" = failure ] || fail "a red rerun was not failure"
echo "merge-queue wait_ci: ok"
