---
changed: Release packages are built once per set of build inputs: a commit whose packaged files are byte for byte an
  earlier green build's (a website or release-paperwork change) reuses that build's packages, verified and attested as
  before, instead of building them again (docs/releasing.md, "Build once, promote").
---
For reviewers (ci/build-once):
- tools/release/build_key.py + build_inputs.txt: the build-input key (git ls-tree -r over the inputs, SHA-256).
- tools/release/staged_run.py: a run for the exact SHA or the same key, main or staging pushes, every old check kept;
  --before-run (no mutual waits), --full (id, built SHA, branch). Self-test: key match/mismatch, fork, dispatch by
  another actor, expired artifacts, reuse runs skipped for their builder, the 0.11.14 case.
- packages.yml: no `paths:` (check_workflows.py forbids it), job reuse keyed, main no longer cancels a build in progress
  (a website push would have); `ccache -z` per build, VFX arm64 no longer
  restores CI's x86_64 cache. ci.yml: Blender job `ccache -z` (its 29-43% "hits" were lifetime counters).
- release.yml: locate passes the built SHA/branch on, re-checks both keys from git; verify attests against them.
