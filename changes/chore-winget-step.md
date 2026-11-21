---
changed: Release tooling: every release now updates the winget package (installer link, checksum, release date) for the
  Windows Package Manager; the manifests live in `packaging/winget/`.
---
Reviewers: scripts/release.sh step winget; test: ctest -R release_winget.
