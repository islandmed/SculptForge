---
changed: Release tooling: the ci-status and winget self-tests no longer flake when a pipe closes early.
---
Reviewers: tools/ci/ci_status.sh line 20 (head -1 -> awk NR==1), tests/release_winget_test.sh (head -1 -> sed -n 1p).
