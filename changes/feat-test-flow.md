---
changed: **Testing flow** (docs/testing-flow.md): each change is tested once per tier instead of four times. T0 is
  `tools/test/impacted.py`, the tests a change can break, from the build graph. T1 is the whole suite once per merge
  batch on a GPU machine, in parallel with T2, CI on the `staging` branch; main fast-forwards only when both pass, and
  CI on main reuses that run. T3 is a nightly label and workflow for benchmarks and heavy meshes. `tools/test/run.py`
  wraps ctest with a content-hash test cache, one rerun for flaky tests (logged in tests/flaky.md), and a 20-second
  budget per test. A release bump that only adds paperwork reuses CI instead of waiting for it again.
---
Contributor-facing only; nothing in the app changes. UI test names now follow the trademark rules (ui_classic_keys,
ui_forge_spheres, ui_library, ui_parts, ui_parts_files, ui_custom_lit_spheres).
