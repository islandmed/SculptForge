---
added: **`sculptforge --version`** prints the version and exits; a developer's build adds the commit it was built at ("0.13.14 dev 1a2b3c4").
changed: A developer's build shows the commit it was compiled at, not the one it was configured at, so an incremental rebuild after a commit no longer shows the previous commit in the title bar.
testers:
  - Any OS, a build from a git checkout: `sculptforge --version` prints the version; release packages print no "dev".
---
Build-only change; release packages are unchanged.
