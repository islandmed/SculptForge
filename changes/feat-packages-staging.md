---
changed: **Release packages on staging**: the merge batch builds the release packages on the `staging` branch beside
  CI, and main reuses them. A release batch carries the version bump, so the release promotes packages that were
  already built and tested for the exact tagged commit: tag to published takes about 5 minutes instead of about 25.
---
Contributor-facing only; nothing in the app changes.
