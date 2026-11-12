---
changed: Release tooling: a staging CI run that fails in a test step gets one rerun of its failed jobs, and a test that passes on
  the rerun is logged as flaky instead of failing the release. A test that fails twice stays red.
---
Tooling only; nothing users see.
