---
changed: **Release tooling**: a release's staging holds `main` until its promote has finished, so site pushes and the merge queue cannot move it between the two; the merge queue judges a CI run only once it has finished, and retries a watch that errors mid-run instead of reading it as red.
---
Reviewers: scripts/release.sh (hold-main), scripts/merge-queue.sh (wait_ci), tests/merge_queue_wait_ci_test.sh, tests/release_beta_test.sh.
