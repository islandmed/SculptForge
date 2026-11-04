# Merge queue

Branches land on main through pull requests and `scripts/merge-queue.sh`; nobody pushes main by hand. (GitHub's own
merge queue exists only for organization-owned repositories; this one belongs to a user, so the script does the
same job.)

## For a branch (any agent)

1. Branch from `origin/main`, one note fragment `changes/<branch>.md` instead of CHANGELOG / What's New edits
   ([changes/README.md](../changes/README.md)).
2. Full ctest green locally (`~/Work/.sf/heavy.sh`), push the branch, `gh pr create --fill`.
3. The PR's CI runs the fast set (Linux x86_64 and ARM64, macOS, Windows over 4 runners; about 10 minutes). Add the
   label `full-ci` for the VFX Reference Platform and Blender add-on jobs too (build or packaging changes).
4. `scripts/merge-queue.sh add <pr>`, write REPORT.md, tell the coordinator.

## For the coordinator

`scripts/merge-queue.sh run` in the background (one at a time). It merges main plus every queued PR whose own checks
are green into a branch `queue/batch-<time>`, runs the full CI there (everything, as on main), and when that is
green moves main to exactly that commit; the PRs show as merged. A PR that conflicts or is red is dropped with a
comment and the `queue-failed` label; its author fixes the branch and queues it again. A red batch falls back to
`--serial` (update each PR from main, wait for its checks, merge), which isolates the PR that broke it.
`scripts/merge-queue.sh list` shows the queue.

Cut, don't wait: what is green at cut time lands; the rest rides the next batch.

## Timing checks on CI

Speed checks in tests use `timeLimit(ms)` (tests/TestHarness.h): the given limit on a developer machine, four times it
on CI runners (shared, loaded VMs), and the strict limit again with `SF_BENCH_LIMITS=1`, as benchmarks are judged.
