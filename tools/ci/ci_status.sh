#!/usr/bin/env bash
# ci_status.sh: appends one line per finished Actions run to status.tsv on the ci-status branch (workflow ci-status.yml), so the laptop
# learns a run is done with `git fetch` (free) instead of polling the REST API (5000 calls an hour for everyone; 11-11 it ran out).
#   line: <run id> TAB <workflow> TAB <branch> TAB <sha> TAB <conclusion> TAB <finished UTC>     (newest last, 500 lines kept)
# Every field arrives as an environment variable (RUN_ID WORKFLOW BRANCH SHA CONCLUSION), never in the command line; control
# characters are dropped. The branch is rewritten as one new parentless commit each time (--force-with-lease), so it never grows.
#   CI_STATUS_REMOTE (default: this repository over https) CI_STATUS_TOKEN (the job's GITHUB_TOKEN; none for a local test remote)
#   ci_status.sh --self-test
set -euo pipefail
if [ "${1:-}" = --self-test ]; then
  t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
  git init -q --bare "$t/origin.git"
  for i in 1 2 3; do
    CI_STATUS_REMOTE=$t/origin.git RUN_ID=$i WORKFLOW="T0 x" BRANCH="t0/fix/a b" SHA=abc$i CONCLUSION=success "$0"
  done
  RUN_ID=9 WORKFLOW=CI BRANCH=main SHA=def CONCLUSION=failure CI_STATUS_REMOTE=$t/origin.git ROWS_KEEP=3 "$0"
  out=$(git -C "$t/origin.git" show ci-status:status.tsv)
  [ "$(printf '%s\n' "$out" | wc -l | tr -d " ")" -eq 3 ] || { echo "FAIL: expected 3 kept lines: $out"; exit 1; }
  printf '%s\n' "$out" | tail -1 | awk -F'\t' '$1=="9"&&$2=="CI"&&$5=="failure"&&NF==6{ok=1}END{exit !ok}' || { echo "FAIL: last line: $out"; exit 1; }
  printf '%s\n' "$out" | head -1 | awk -F'\t' '$3=="t0/fix/a_b"{ok=1}END{exit !ok}' || { echo "FAIL: branch not cleaned: $out"; exit 1; }
  [ "$(git -C "$t/origin.git" rev-list --count ci-status | tr -d " ")" -eq 1 ] || { echo "FAIL: history grew"; exit 1; }
  echo "ci_status OK"; exit 0
fi
clean() { printf '%s' "${1:-}" | tr -d '\000-\037\177' | tr ' ' '_' | cut -c1-120; }
line=$(printf '%s\t%s\t%s\t%s\t%s\t%s' "$(clean "$RUN_ID")" "$(clean "$WORKFLOW")" "$(clean "$BRANCH")" "$(clean "$SHA")" "$(clean "$CONCLUSION")" "$(date -u +%FT%TZ)")
remote=${CI_STATUS_REMOTE:-https://github.com/${GITHUB_REPOSITORY:?}}
keep=${ROWS_KEEP:-500}
d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
cd "$d"
git init -q
git remote add origin "$remote"
if [ -n "${CI_STATUS_TOKEN:-}" ]; then
  git config http.https://github.com/.extraheader "AUTHORIZATION: basic $(printf 'x-access-token:%s' "$CI_STATUS_TOKEN" | base64 | tr -d "\n")"
fi
export GIT_AUTHOR_NAME=github-actions GIT_AUTHOR_EMAIL=41898282+github-actions[bot]@users.noreply.github.com GIT_COMMITTER_NAME=github-actions GIT_COMMITTER_EMAIL=41898282+github-actions[bot]@users.noreply.github.com
for attempt in 1 2 3 4 5 6 7 8; do
  : > status.tsv; lease=""
  if git fetch -q --depth=1 origin ci-status 2>/dev/null; then
    git show FETCH_HEAD:status.tsv > status.tsv 2>/dev/null || : > status.tsv
    lease=$(git rev-parse FETCH_HEAD)
  fi
  { cat status.tsv; printf '%s\n' "$line"; } | tail -n "$keep" > status.new && mv status.new status.tsv
  git add status.tsv
  tree=$(git write-tree)
  commit=$(git commit-tree "$tree" -m "ci-status: $(clean "$RUN_ID") $(clean "$CONCLUSION")")
  if git push -q ${lease:+--force-with-lease=ci-status:$lease} origin "$commit:refs/heads/ci-status" 2>/dev/null; then exit 0; fi
  sleep $(( RANDOM % 4 + 1 ))
done
echo "ci_status: could not push after 8 tries" >&2
exit 1
