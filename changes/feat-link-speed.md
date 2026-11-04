---
changed: **Faster builds from source**: on Linux the build links with mold when it is installed and precompiles the
  standard library and glm headers; app code the tests also run is a static library both link, so it compiles once.
  Packagers: `-DSCULPTFORGE_MOLD=OFF` keeps the system linker (the release packages do), `-DSCULPTFORGE_PCH=OFF`
  turns the precompiled headers off. See docs/build-speed.md.
---
Edit-build loop numbers (before/after, PC and laptop) are in docs/build-speed.md and REPORT.md.
