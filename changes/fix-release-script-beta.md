---
changed: Release tooling: the beta-path self-test runs on machines with a machine-wide push guard.
---
Reviewers: tests/release_beta_test.sh links the real guard script into the sandbox HOME when one exists (CI: unchanged), so the guard still runs on the sandbox pushes.
