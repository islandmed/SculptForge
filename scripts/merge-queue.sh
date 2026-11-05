#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The merge queue (docs/merge-queue.md): agents open pull requests and add them to the queue; the release
# coordinator runs the queue, which lands them on main tested on top of main. Nobody pushes main by hand.
# (GitHub's own merge queue needs an organization-owned repository; this one belongs to a user.)
#
#   scripts/merge-queue.sh add <pr>...      queue pull requests (label "queue"); any session may do this
#   scripts/merge-queue.sh list             the queue, oldest first, with each PR's check state
#   scripts/merge-queue.sh run [--serial]   land the queue (the coordinator, in the background)
#   scripts/merge-queue.sh staging [--with CMD] [REF]   the merger's batch (T2, docs/testing-flow.md): REF (default HEAD,
#                                           main merged into it) goes to the branch `staging`, the full CI runs there
#                                           while CMD runs here (e.g. '~/Work/.sf/pc.sh .', T1), and when both pass main
#                                           fast-forwards to exactly that commit. CI's run on main then reuses the
#                                           staging result (ci.yml, job Changes) instead of building it all again.
#                                           The release packages (packages.yml) are built on staging too, beside CI;
#                                           main's Packages run reuses them, and a release batch that carries the
#                                           bump (release.sh --staged) gets the packages it will publish.
#
# run, batch mode (the default): main plus every queued PR merged in queue order on a branch queue/batch-<time>
# (a PR that conflicts, or whose own checks are red, is dropped with a comment and the "queue-failed" label), the
# full CI on that branch (VFX Reference Platform and Blender add-on included), and when it is green main moves to it
# (a fast-forward, so the tested tree is exactly what lands; GitHub marks the PRs merged). If main moved meanwhile
# the batch is rebuilt. A red batch falls back to --serial, which finds the PR that broke it.
# run --serial: one PR at a time: bring it up to date with main (gh pr update-branch), wait for its checks, merge.
set -euo pipefail

REPO=islandmed/SculptForge
LABEL=queue
FAILED=queue-failed
cd "$(git rev-parse --show-toplevel)"

say() { printf '\033[1m== %s\033[0m\n' "$*"; }
die() { printf '\033[1;31m%s\033[0m\n' "$*" >&2; exit 1; }

queued() { # PR numbers with the queue label, oldest first
    gh pr list -R "$REPO" --label "$LABEL" --state open --json number,createdAt -q 'sort_by(.createdAt) | .[].number'
}

ensure_labels() {
    gh label create "$LABEL" -R "$REPO" --color 0e8a16 --description "Merge queue: scripts/merge-queue.sh lands it" 2>/dev/null || true
    gh label create "$FAILED" -R "$REPO" --color d93f0b --description "Merge queue dropped it: see the comment" 2>/dev/null || true
}

drop() { # drop <pr> <why>
    gh pr edit "$1" -R "$REPO" --remove-label "$LABEL" --add-label "$FAILED" >/dev/null
    gh pr comment "$1" -R "$REPO" --body "Merge queue: dropped. $2 Fix it on the branch, then \`scripts/merge-queue.sh add $1\`." >/dev/null
    echo "   dropped #$1: $2"
}

checks_green() { # the PR's own required checks passed (skipped counts as passed)
    local bad
    bad=$(gh pr checks "$1" -R "$REPO" --required --json state -q '[.[] | select(.state != "SUCCESS" and .state != "SKIPPED")] | length' 2>/dev/null || echo x)
    [ "$bad" = 0 ]
}

wait_ci() { # wait_ci <branch>: dispatches the full CI on the branch, waits, returns its conclusion
    local branch=$1 sha id=""
    sha=$(git rev-parse "origin/$branch")
    gh workflow run ci.yml -R "$REPO" --ref "$branch" -f full=true >/dev/null
    for _ in $(seq 60); do
        id=$(gh run list -R "$REPO" --workflow ci.yml --branch "$branch" --event workflow_dispatch --json databaseId,headSha \
            -q "[.[] | select(.headSha == \"$sha\")][0].databaseId")
        [ -n "$id" ] && break
        sleep 5
    done
    [ -n "$id" ] || die "no CI run started for $branch"
    echo "   CI: https://github.com/$REPO/actions/runs/$id" >&2
    gh run watch "$id" -R "$REPO" --exit-status --interval 30 >/dev/null 2>&1 && echo success || echo failure
}

run_serial() {
    local pr
    for pr in $(queued); do
        say "#$pr"
        if ! gh pr update-branch "$pr" -R "$REPO" >/dev/null 2>&1; then
            drop "$pr" "It conflicts with main."
            continue
        fi
        sleep 10 # the update starts a new check suite
        if ! gh pr checks "$pr" -R "$REPO" --required --watch --fail-fast --interval 30 >/dev/null 2>&1 || ! checks_green "$pr"; then
            drop "$pr" "Its checks failed on top of main."
            continue
        fi
        gh pr merge "$pr" -R "$REPO" --merge --delete-branch >/dev/null
        echo "   merged #$pr"
    done
}

run_batch() {
    local prs=() merged=() pr head base branch
    mapfile -t prs < <(queued)
    [ ${#prs[@]} -gt 0 ] || { echo "queue is empty"; return 0; }
    git fetch -q origin main
    base=$(git rev-parse origin/main)
    branch="queue/batch-$(date +%Y%m%d-%H%M%S)"
    say "batch $branch on main ${base:0:7}: ${prs[*]}"
    local wt
    wt=$(mktemp -d)
    git worktree add -q --detach "$wt" "$base"
    for pr in "${prs[@]}"; do
        if ! checks_green "$pr"; then
            drop "$pr" "Its own checks are not green."
            continue
        fi
        head=$(gh pr view "$pr" -R "$REPO" --json headRefName -q .headRefName)
        git fetch -q origin "$head"
        if git -C "$wt" merge -q --no-ff --no-edit -m "Merge $head (#$pr)" FETCH_HEAD >/dev/null 2>&1; then
            merged+=("$pr")
        else
            git -C "$wt" merge --abort
            drop "$pr" "It conflicts with main or with a PR ahead of it in the queue."
        fi
    done
    if [ ${#merged[@]} -eq 0 ]; then
        git worktree remove --force "$wt"
        return 0
    fi
    git -C "$wt" push -q origin "HEAD:refs/heads/$branch"
    git worktree remove --force "$wt"
    if [ "$(wait_ci "$branch")" != success ]; then
        git push -q origin --delete "$branch"
        say "batch red: landing one at a time to find the PR that broke it"
        run_serial
        return
    fi
    git fetch -q origin main
    if [ "$(git rev-parse origin/main)" != "$base" ]; then
        git push -q origin --delete "$branch"
        say "main moved while the batch ran: again on the new main"
        run_batch
        return
    fi
    git push -q origin "origin/$branch:refs/heads/main" # a fast-forward: exactly the tree CI passed
    git push -q origin --delete "$branch"
    for pr in "${merged[@]}"; do
        gh pr edit "$pr" -R "$REPO" --remove-label "$LABEL" >/dev/null 2>&1 || true
        echo "   landed #$pr"
    done
}

land_staging() { # land_staging [--with CMD] [REF]
    local with="" ref sha pid="" ci t0 local_rc=0
    if [ "${1:-}" = --with ]; then with=$2; shift 2; fi
    ref=${1:-HEAD}
    sha=$(git rev-parse "$ref^{commit}") || die "no commit $ref"
    git fetch -q origin main
    git merge-base --is-ancestor origin/main "$sha" ||
        die "$ref does not contain origin/main: merge origin/main into it first (main must fast-forward to the tested commit)"
    [ "$sha" != "$(git rev-parse origin/main)" ] || { echo "main is already at ${sha:0:8}"; return 0; }
    t0=$(date +%s)
    say "staging ${sha:0:8} ($(git log -1 --format=%s "$sha" | cut -c1-60)) on main $(git rev-parse --short origin/main)"
    git push -q --force origin "$sha:refs/heads/staging"
    git fetch -q origin staging
    # Not waited for: a red Packages run doesn't hold main (main's push then builds its own, as before staging).
    gh workflow run packages.yml -R "$REPO" --ref staging >/dev/null && echo "   Packages dispatched on staging" ||
        echo "   note: couldn't dispatch Packages on staging: main's push builds them"
    if [ -n "$with" ]; then
        bash -c "$with" > "/tmp/merge-queue-with-${sha:0:8}.log" 2>&1 &
        pid=$!
        echo "   alongside: $with (log /tmp/merge-queue-with-${sha:0:8}.log)"
    fi
    ci=$(wait_ci staging)
    if [ -n "$pid" ]; then wait "$pid" || local_rc=$?; tail -5 "/tmp/merge-queue-with-${sha:0:8}.log" | sed 's/^/   /'; fi
    echo "   CI on staging: $ci; local: $([ $local_rc = 0 ] && echo ok || echo "failed ($local_rc)"); $(( $(date +%s) - t0 )) s"
    [ "$ci" = success ] && [ $local_rc = 0 ] || die "staging is red: main stays at $(git rev-parse --short origin/main); fix the batch and run again"
    git fetch -q origin main
    git merge-base --is-ancestor origin/main "$sha" ||
        die "main moved while staging ran ($(git rev-parse --short origin/main)): merge it in and run again"
    git push -q origin "$sha:refs/heads/main" # a fast-forward: exactly the tree CI passed
    say "main is now ${sha:0:8}"
}

case ${1:-} in
staging)
    shift
    land_staging "$@"
    ;;
add)
    shift
    [ $# -gt 0 ] || die "usage: scripts/merge-queue.sh add <pr>..."
    ensure_labels
    for pr in "$@"; do
        gh pr edit "$pr" -R "$REPO" --add-label "$LABEL" --remove-label "$FAILED" >/dev/null 2>&1 ||
            gh pr edit "$pr" -R "$REPO" --add-label "$LABEL" >/dev/null
        echo "queued #$pr"
    done
    ;;
list)
    gh pr list -R "$REPO" --label "$LABEL" --state open --json number,title,headRefName,createdAt,statusCheckRollup \
        -q 'sort_by(.createdAt) | .[] | "#\(.number)  \(.headRefName)  \([.statusCheckRollup[]? | .conclusion // .status] | unique | join(","))  \(.title)"'
    ;;
run)
    ensure_labels
    if [ "${2:-}" = --serial ]; then run_serial; else run_batch; fi
    ;;
*)
    sed -n '5,23p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
    ;;
esac
