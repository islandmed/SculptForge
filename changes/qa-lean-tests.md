---
changed: **Tests** (contributors): a CMakeLists.txt edit that only adds new source files or a guarded `add_test` runs
  those files' tests in T0, not the whole suite; the mode matrix runs as four shards side by side; `stale_tree` runs on
  every change; a per-push tier (`SF_T0_TIER=push`: direct picks + start, sculpt, save and open) runs in about half the
  test time and is never the READY verdict; known flakes go in `tests/quarantine.txt` with an owner and a date, after which they block again.
---
