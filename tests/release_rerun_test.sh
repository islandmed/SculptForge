#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/release.sh infra_retry (ctest release_rerun): a CI run red in a TEST step gets one rerun of its failed jobs. A test that
# fails once and passes on the rerun is logged in flaky.md (twice in 7 days: top of the file) and the run goes green; a test that
# fails again stays red and is named; a failure in a build step is not rerun; a cancelled run is not rerun. Stand-in gh, sandbox HOME.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
mkdir -p "$S/bin" "$S/home/Work/.sf/logs"
cat > "$S/home/Work/.sf/gh-wait.sh" <<'STUB'
#!/bin/sh
[ "$1" = --lines ] && echo 0
exit 0
STUB
printf '#!/bin/sh\nexit 0\n' > "$S/bin/sleep"
cat > "$S/bin/gh" <<'STUB'
#!/bin/bash
# state: $GH_STATE/conclusion, step (the failed job: step), log (the failed log); a rerun does what $GH_RERUN says
case "$*" in
  *"run rerun"*) echo rerun >> "$GH_STATE/reruns"
     if [ "$GH_RERUN" = pass ]; then echo success > "$GH_STATE/conclusion"; : > "$GH_STATE/log"; fi ;;
  *"--log-failed"*) cat "$GH_STATE/log" ;;
  *"--json jobs"*) cat "$GH_STATE/step" ;;
  *"--json conclusion"*) cat "$GH_STATE/conclusion" ;;
esac
STUB
chmod +x "$S/bin/gh" "$S/bin/sleep" "$S/home/Work/.sf/gh-wait.sh"
export PATH=$S/bin:$PATH HOME=$S/home GH_STATE=$S/state REPO=x/y V=9.9.9
mkdir -p "$GH_STATE"
cd "$ROOT"
# shellcheck disable=SC1090
source <(sed -n '/^# server_error < log/,/^# Successful run of a workflow/p' scripts/release.sh)
setup() { # setup CONCLUSION STEP LOG RERUN
    rm -f "$GH_STATE"/*; echo "$1" > "$GH_STATE/conclusion"; echo "$2" > "$GH_STATE/step"; printf '%s\n' "$3" > "$GH_STATE/log"; export GH_RERUN=$4
}
reruns() { if [ -f "$GH_STATE/reruns" ]; then wc -l < "$GH_STATE/reruns"; else echo 0; fi; }
FLAKY=$HOME/Work/.sf/logs/flaky.md
TESTLOG=$'macOS (1/1)\tUNKNOWN STEP\t2026-11-11T18:32:58Z rerunning 1 failed test(s) once: ui_env_toggle'
TESTSTEP="macOS: Test (C++ and Python API)"

# 1. a once-failing test: rerun, green, logged
setup failure "$TESTSTEP" "$TESTLOG" pass
out=$(infra_retry 111 2>&1) || fail "a test-step failure was not rerun: $out"
[ "$(reruns)" = 1 ] || fail "expected one rerun"
grep -q "flaky: ui_env_toggle passed on rerun" <<<"$out" || fail "no flaky line: $out"
grep -q "| ui_env_toggle |.*| 9.9.9 |" "$FLAKY" || fail "flaky.md has no row: $(cat "$FLAKY")"
[ "$(cat "$GH_STATE/conclusion")" = success ] || fail "the run is not green"

# 2. the same test again within 7 days: it goes to the top of the file
setup failure "$TESTSTEP" "$TESTLOG" pass
out=$(infra_retry 112 2>&1)
grep -q "repeat: ui_env_toggle (2 in 7 days)" <<<"$out" || fail "no repeat line: $out"
[ "$(sed -n '/Fix owner needed/,/## Log/p' "$FLAKY" | grep -c 'ui_env_toggle')" = 1 ] || fail "not at the top: $(cat "$FLAKY")"

# 2b. the stage summary names the test and says REPEAT for the recurring one only
source <(sed -n '/^stage_flaky_summary()/,/^}/p' scripts/release.sh)
python3 tools/release/flaky_log.py add --log "$FLAKY" --test ui_once_only --where "PC suite" --sha abc --version 9.9.9 >/dev/null
out=$(stage_flaky_summary)
grep -q "passed on rerun in this stage: .*ui_env_toggle" <<<"$out" || fail "the summary lacks the test: $out"
grep -q "REPEAT flaky: ui_env_toggle (2 in 7 days)" <<<"$out" || fail "the summary lacks the REPEAT line: $out"
grep -q "REPEAT flaky: ui_once_only" <<<"$out" && fail "a single rerun was called a repeat"

# 3. a deliberately failing test: one rerun, still red, named, not logged as flaky
setup failure "$TESTSTEP" "$TESTLOG"$'\nmacOS (1/1)\tUNKNOWN STEP\t2026 FAILED: ui_always_red' fail
before=$(grep -c '^| 20' "$FLAKY")
out=$(infra_retry 113 2>&1) || fail "the single rerun did not happen"
[ "$(reruns)" = 1 ] || fail "more than one rerun"
grep -q "still red after the rerun, so not flaky: .*FAILED: ui_always_red" <<<"$out" || fail "red not named: $out"
[ "$(cat "$GH_STATE/conclusion")" = failure ] || fail "the run should stay red"
[ "$(grep -c '^| 20' "$FLAKY")" = "$before" ] || fail "a red test was logged as flaky"

# 4. a failing build step is no flake; a cancelled run is never rerun
setup failure "Linux: Build" "error: foo.cpp" pass
if infra_retry 114 >/dev/null 2>&1; then fail "a build failure was rerun"; fi
[ "$(reruns)" = 0 ] || fail "build rerun"
setup cancelled "$TESTSTEP" "$TESTLOG" pass
if infra_retry 115 >/dev/null 2>&1; then fail "a cancelled run was rerun"; fi
[ "$(reruns)" = 0 ] || fail "cancelled rerun"

# 5. a setup-step failure still reruns (as before)
setup failure "Linux: Install build dependencies" "Temporary failure resolving" pass
infra_retry 116 >/dev/null 2>&1 || fail "a setup failure was not rerun"
[ "$(reruns)" = 1 ] || fail "setup rerun count"
echo "release_rerun ok"
