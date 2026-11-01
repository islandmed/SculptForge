# cifast report: fast CI and a fast, gated release

Branch `feat/cifast` (pushed at d3782bb, rebased on origin/main b78969e: 0.10.4 in progress, not shipped). It changes
workflows, the test runner, test registration and smoke tests, plus `scripts/release.sh` and docs. **No app code.**

## Result

| | before | now |
|---|---|---|
| **CI wall clock** (warm) | 15-21 min | **6.0 min** (run 41336529771, all 6 required checks green) |
| **Release** (tag → published) | 20.9 min (v0.10.3, run 41331627109) | **about 1.5 min** when it promotes: locate + 5 smoke tests ran in 55 s (dry run 41338068684), plus publish (about 30 s) |
| Release that has to build (no Packages run for the commit) | 20.9 min | 16.6 min (dry run 41336601308; Flatpak x86_64 15.5 min is the long pole) |
| Packaged files tested before publishing | none | Windows installer + .zip, .dmg, .deb + AppImage (x86_64, arm64), .rpm |

The cold CI case (no cache, or after a change to a header everything includes): about 12-14 min (run 41335208645).
Today's old workflow took 15-21 min for the same.

## What changed, by request

### 1. ccache + parallel ctest + sharding, now in releases too

- **Where the build lives:** the release build matrix moved into `packages.yml`. That workflow runs on every push to
  main that changes the app, and release.yml can also call it.
- **Compiler cache:** every compiling job uses ccache. Linux uses Ubuntu's package; macOS, Windows and the Rocky
  containers download the ccache 4.14.2 release binary, checked against SHA-256.
  - Windows keeps the Visual Studio generator the release uses: ccache is put in as `cl.exe`, and MSBuild compiles
    one file per core.
  - Depend mode is on (`-MD` / `/showIncludes`), so a cache miss doesn't run the preprocessor twice.
- **Who can save the cache:** pull requests only read it. Pushes to main and maintainers' own dispatches save it.
  **Tags never save** (`SAVE_CACHE` is false for any `refs/tags/` run). A release that builds restores main's cache;
  GitHub lets a tag read only its own and the default branch's caches.
- **Parallel tests:** `sculptforge_tests --shard K/N` is registered as 8 ctest entries, each with its own temp folder.
  ctest runs at `-j 4`. Timed UI measurements and the scripts that use the clipboard run alone (`RUN_SERIAL`). The
  clipboard rule is detected from each .sfui, after an X11 BadWindow crash seen in CI.
- **Why cache restores into release builds are safe:** the cache can only be written by trusted runs (main pushes),
  ccache keys on the compiler binary, every flag and every include, and a hit is the object that compiler would
  produce. Release packages are still built from the tag's own source.

### 2. Runner variables

Every build and test job's `runs-on` reads a repository variable, falling back to the standard runner.

| variable | default |
|---|---|
| `SF_RUNNER_LINUX` | `ubuntu-24.04` |
| `SF_RUNNER_LINUX_ARM` | `ubuntu-24.04-arm` |
| `SF_RUNNER_LINUX_OLD` | `ubuntu-22.04` (.deb and AppImage, for the older glibc) |
| `SF_RUNNER_LINUX_OLD_ARM` | `ubuntu-22.04-arm` |
| `SF_RUNNER_MAC` | `macos-15` |
| `SF_RUNNER_WIN` | `windows-2022` |

- **Jobs pinned to GitHub's own runners:** CI `changes`, Release `locate`, the required-check result jobs, and
  `publish`.
- **Tokens:** `release.yml` is now `contents: read` at the top. **Only `publish` gets `contents: write`.** Before
  this, every release build job held a write token. So a third-party runner would only ever see a read-only token,
  and no build or test job has any secret.
- docs/releasing.md has the table.

### 3. Smoke tests of the packaged files before Publish

- **What runs:** `release.yml` job `smoke`, matrix of 5, each on a fresh runner:
  - Windows: NSIS installer run silently, and the .zip unpacked;
  - macOS: .dmg mounted, the app copied out, `codesign --verify`;
  - Linux x86_64 and arm64: `apt install ./*.deb`, and the AppImage extracted;
  - .rpm: `dnf install` in rockylinux:9.
- **Each install or unpack runs two checks:**
  - the GUI (`tests/ui/release_smoke.sfui`: drop the bust model, Clay stroke, can_undo / dirty);
  - `tests/release_smoke.py` on that package's `sculptforge-cli`: open, Clay stroke, save_project, reopen, then check
    the triangle count, that vertices moved, and that the reopened mesh is identical.
- **Verified:** every check ran and passed in the dry runs, macOS GUI included (Apple's software renderer). The
  Flatpak is still installed and UI-tested inside its own build job.
- **Gate:** publish needs smoke to succeed. Both test files also run under ctest (`release_smoke`,
  `ui_release_smoke`), so CI keeps them working.

### 4. scripts/release.sh and docs/releasing.md

- **Steps:** check → ci → tag → release → site → aur → brew → omarchy → replies → status. Run them all, or use
  `--from STEP` / `--only STEP`.
- **Idempotent:** each step checks live state and skips what's done (e.g. AUR already at that pkgver and checksum).
  On a failure it prints `STOPPED`, the exact `fix:`, and the resume command.
- **Checksums are computed here:**
  - AUR and Omarchy: the tag tarball is downloaded and hashed;
  - brew: the .dmg is downloaded, hashed and compared with SHA256SUMS before the tap update.
- **Writing it never does:** the feed highlights come from a file you hand it (`HIGHLIGHTS=`). For the "new in X.Y.Z"
  card it checks and stops with the fix. Issue replies are **drafts only** (`release-replies-X.Y.Z.md`); it never
  posts.
- **Outside commits:** made as islandmed noreply.
- **Tested** against the real repo, read-only, in a throwaway clone:
  - `check` passes at the v0.10.3 tag, and stops with the right fix when CMakeLists isn't bumped or a tag is
    elsewhere;
  - `release` verified v0.10.3's 35 assets;
  - `replies` drafted 7 replies.
- **Not run yet:** tag, site, aur, brew and omarchy. They push or publish, so they first run for real on the next
  release.

### Addendum items

- **(a) More free runners in parallel:** the CI tests are split with `ctest -I K,,N`: Windows ×3, Linux x86_64 ×2,
  ARM64 ×2. The shards together run every test once (checked: 3 × 29 = 87). One shard saves the cache.
  - **Required checks:** `Linux x86_64`, `Windows`, … are now tiny result jobs, so branch protection needs no change.
  - **Load:** CI is 13 jobs and Packages 11, both on a main push. That is at the free plan's 20 concurrent jobs, so
    a few may queue for a minute.
- **(b) Build once, promote:**
  - **Locate:** `locate` checks that the tag's CMakeLists version equals the tag. It then looks for a successful CI
    run and a successful Packages run for exactly that commit: event push, branch main, this repository, the right
    workflow file.
  - **Promote:** if both are there, it downloads that Packages run's artifacts, smoke-tests them and publishes. If
    not, it builds via packages.yml.
  - **Signing:** there is none today; macOS is ad-hoc signed inside CPack at build time, which survives promotion.
    Future SignPath / notarization would sign the promoted files between smoke and publish.
  - **Reproducibility:** the promoted packages are byte-for-byte the tested ones, which is stronger than
    rebuilding.
  - **Naming:** file names use the CMakeLists version, and locate fails a mismatched tag early (it used to fail late,
    at publish).
- **(c) Path filters:**
  - **CI:** a `changes` job reads the compare API. Pushes and PRs touching only `website/` (not `website/link/`,
    which is built into the app), pages/download-stats workflows, `packaging/arch/`, `tools/publish/`, the release
    script or this doc skip the build and test jobs. Their required checks pass as skipped.
  - **Tested** on real history: 5868d6f..27848d1 skips, the 0.10.3 range builds.
  - **Packages** uses the same list as a workflow path filter.
  - **The Flatpak PR check** (flatpak.yml) stays path-filtered. On main, Packages builds both Flatpaks every push,
    so its nightly run was removed.
- **(d) Blacksmith** and **(e) self-hosted:** see the runner section below.

## Paid runners (nothing signed up for or installed)

Facts were checked by a research agent on 2026-11-01; sources are in its notes. Prices are USD per minute.

- **Today's setup costs $0.** Standard runners are free and unlimited for this public repo: 20 concurrent jobs,
  5 of them macOS. They have 4 vCPU for Linux and Windows, and 3-core M1 for macOS.
- **Every paid option requires an organization.**
  - GitHub larger runners: Team or Enterprise Cloud only ($4 / user / month for Team), billed per minute even on
    public repos. **A personal account like islandmed can't use them.**
  - Blacksmith: "limited to GitHub organizations and not available for personal repositories."
  - WarpBuild: describes only organization setup (personal accounts unverified).
  - So any paid runner means **transferring the repo to an organization**. GitHub redirects the URLs, but releases,
    the update feed, the brew tap, AUR sources and the Pages mirror would all need checking. That decision is
    Tony's.
- **Prices for runners bigger than the free ones** (only bigger ones are faster than free; 2-vCPU paid runners are
  slower):

| runner | GitHub larger | Blacksmith | WarpBuild |
|---|---|---|---|
| Linux x64 8 vCPU | $0.022 | $0.016* | $0.016 |
| Linux arm64 8 vCPU | $0.014 | $0.010* | $0.012 |
| Windows 8 vCPU | $0.042 | $0.032* (beta) | $0.032 |
| macOS | M2 Pro XL $0.102 | M4 6 vCPU $0.08 | M4 Pro $0.08 |

  \* scaled from Blacksmith's 2-vCPU prices; its page shows no per-size prices.
- **Free tiers:**
  - Blacksmith: 3,000 x64 2-vCPU minutes / month per org. A Windows minute counts as 2, a macOS minute as 20, so it
    covers about 150 macOS minutes. Its OSS program is by application.
  - WarpBuild: a one-time $10 credit.
- **Usage now** (from the last 8 days): about 440 CI runs and 300 main pushes a month (each push now also runs
  Packages). Warm job-minutes per run:
  - CI: Linux x64 17, arm64 7, Windows 14, macOS 3.5;
  - Packages: Linux x64 33 (Flatpak 15.5 of it), arm64 26, Windows 7.4, macOS 6.3.
- **Assumption for paid costs:** 8-vCPU runners take about 0.7× the job time (compile scales, llvmpipe UI tests
  only partly).

| scenario | per release commit (CI + Packages) | per month |
|---|---|---|
| Today, standard runners | $0, 6 min CI, 1.5 min release | $0 |
| Blacksmith 8 vCPU everywhere (+ M4 mac) | about $1.80 | about $650 (+ org) |
| GitHub larger 8-core everywhere | about $2.40 | about $860 + $4/user Team |
| Only Windows on Blacksmith 8 vCPU | about $0.48 | about $185 |

**What paying would buy now:** CI 6 → about 4 min; the release is already about 1.5 min when it promotes. The
remaining wait was Packages on main (Flatpak x86_64, 15.5 min), which a tag waits for before it can promote. Tony
approved the free fix and it is **done**: `packaging/flatpak/build.sh` takes `SF_FLATPAK_CCACHE=1` (flatpak-builder
`--ccache`, the SDK's own ccache), and packages.yml restores and saves `build-flatpak/state/ccache` (never from a
tag). Cold run 41339709154: both Flatpaks green (x86_64 12m14s), 216 MB caches saved. **The warm speed-up is not
measured yet** (it was time-boxed). Expected 4-5 min; the first main pushes after merge will show it.

**Decision (Tony, via the coordinator):** no paid runners. On a personal account every paid option needs an org transfer, and free runners are enough.

- **Blacksmith security (d):**
  - jobs run in Firecracker microVMs, one per job, destroyed afterwards; SOC 2 Type 2;
  - its GitHub App asks for write access to Contents, Workflows, Actions, Pull requests and Checks, which is broad.
  - Our build and test jobs carry only a read-only GITHUB_TOKEN and no secrets, and publish stays on GitHub. That is
    acceptable for build and test jobs only.
  - Its Windows runners are beta and give no edge over our free 3-way split. Its macOS M4 ($0.08/min) is the only
    clear speed-up, but macOS isn't our long pole.
- **Self-hosted on Tony's machine (e):**
  - **Cost:** free, and no platform fee for public repos (GitHub postponed the self-hosted fee).
  - **The risk:** GitHub says self-hosted runners "should almost never be used for public repositories". Any fork PR
    that gets approved, or slips past approval, runs its code on that machine.
  - **If ever used:**
    - only for jobs that can't come from a PR (push to main and tags);
    - an ephemeral, just-in-time runner inside a throwaway container or VM with no access to his home directory,
      keys or LAN;
    - fork PR approval set to "all external contributors";
    - a personal account has no runner groups, so only workflow-level `if:` guards keep PR jobs off it. One mistake
      in a workflow exposes the machine.
  - **Not recommended**, and not set up.

## Tests and verification

- **New tests:**
  - `tests/test_harness.cpp`: every test lands in exactly one shard;
  - `tests/release_smoke.py`, registered as ctest `release_smoke`;
  - `tests/ui/release_smoke.sfui`.
- **Local, rebased tree (aarch64 Release):** `sculptforge_tests` 663 tests, 0 failures; `ctest -j 4` 101/101 green,
  including every UI test.
- **`ui_idle_redraw` locally:** it fails some runs on this live desktop (25 frames in 4 s when the session is busy;
  its comment says pointer activity draws frames). It is unchanged, and it passes in CI every time.
- **CI and release runs:**
  - CI 41336529771: green, 6.0 min.
  - Release dry run 41336601308: builds, 5 smoke tests and publish skipped, all as designed.
  - Promote dry run 41338068684: 55 s.
  - On the final rebased head: CI 41338301432 green (15.4 min, mostly cold because main's 0.10.4 merge touched most
    files: the "header change" case), and release dry run 41338303633 green (builds 16.1 min, all 5 smoke tests,
    publish skipped).
- **Also found:** dry-running the old v0.10.3 tag through the new pipeline failed `ui_stroke_path`. Old tags lack the
  `RUN_SERIAL` markers, so the timed test ran under parallel load. Tags made after this merge carry them.
  Rebuilding a pre-merge tag with `gh workflow run release.yml -f tag=…` can hit that. Deliberately not "fixed"
  with retries.

## For the coordinator / Tony

1. **Merge** feat/cifast **right after v0.10.4 is tagged**, so 0.10.5 is the first release on the new pipeline. Then run `gh workflow run Flatpak` once; packages.yml and flatpak.yml only dispatch after
   they exist on main.
2. **The first main pushes after merge** run Packages cold (about 15-20 min, off the CI path). The release after that
   promotes.
3. Check that the second Packages run on main shows the warm Flatpak (about 4-5 min expected). If ccache in the SDK
   misbehaves, unset `SF_FLATPAK_CCACHE` in packages.yml (one line) to go back to a plain build.
4. **Optional:** change the required checks to the single set of result jobs. They already work as they are.
5. **Payment:** only if Tony wants it, and only after moving the repo to an organization; numbers above.
