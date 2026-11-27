# scripts

Scripts for the people who build and release SculptForge. You do not need any of them to build, test or contribute
(for that, see [docs/building.md](../docs/building.md) and [CONTRIBUTING.md](../CONTRIBUTING.md)); read them to see
how a release or a landing works.

| Script | What it does |
|---|---|
| `release.sh` | Releases a version from `main` and brings every channel to it in lock step: version files, changelog, the tag, the GitHub release, the website and update feed, and the AUR, Homebrew, Scoop and winget packages. **Build once, promote:** `--stage` builds and tests the packages for `X.Y.Z` on `staging`, and `--yes` publishes exactly those files. The header lists every step and option; [docs/releasing.md](../docs/releasing.md) explains the process |
| `merge-queue.sh` | The merge queue: pull requests are labelled `queue`, tested on top of `main` and landed one by one, and nobody pushes `main` by hand ([docs/merge-queue.md](../docs/merge-queue.md)). It also runs `tools/ci/no_revert.py`, which refuses a merge that quietly undoes what landed on the other side |
| `android-build.sh`, `android-sign.sh`, `android-verify.sh` | Build the Android APK (NDK plus Gradle); `android-verify.sh` checks an APK against the release key pin (`android-cert.sha256`); `android-sign.sh` is the old by-hand signing; the CI tester build uses a throwaway debug key, a `v*` tag build is signed with the release key ([android](../android/README.md)) |
| `check_else_if_chains.py` | Fails on an `if / else if` chain of 100 or more links under `src/`: MSVC stops at about 128 nested blocks, GCC and Clang do not, so it would break only on Windows. Use a table or a switch |
| `ai3d-health.py`, `hf-space-health.sh` | Probe the free Hugging Face Spaces behind Generate 3D and write the optional `ai3d` block of `website/latest.json`, so a dead Space is greyed out with a reason instead of hanging an artist. They read public status only |

All of them are POSIX shell or Python 3 with no packages; read a script's header comment before you run it.
Releases and landings need push rights and secrets that contributors do not have: send a pull request instead.
