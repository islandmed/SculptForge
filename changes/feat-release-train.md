---
changed: Faster CI for branches and pull requests: the VFX Reference Platform and Blender add-on checks run on main,
  in the merge queue and on request (label `full-ci`, or a dispatch with full=true); Windows tests run on 4 runners;
  CMake's configure checks are cached; old compiler caches are pruned so main's stay warm (prune-caches.yml); docs-only
  changes skip the build; every test job lists its slowest tests in the job summary.
changed: Release notes come from one note fragment per branch, `changes/<branch>.md` (changes/README.md);
  `scripts/release.sh` step bump assembles them into CHANGELOG.md and What's New, and the update feed's highlights
  default to the version's highlight lines.
---
