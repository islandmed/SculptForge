---
changed: Release tooling: a missing standard `#include` that only the Ubuntu 22.04 packages build catches (they have no precompiled headers) now fails the branch test run and CI first.
---
Reviewers: tools/ci/iwyu_lite.py (new), tools/ci/t0.sh (iwyu stage), .github/workflows/t0.yml (g++-11), ci.yml oldest-linux (full sweep).
