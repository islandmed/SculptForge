# Releasing SculptForge

One page for whoever coordinates a release. `scripts/release.sh X.Y.Z` does the work; this explains the gates, what
each step does, and how to recover.

## Gates (in this order; a release never skips one)

1. **Merged tree, tested locally**: on main with the branches for this release merged, `ctest --test-dir build -j3`
   all green.
2. **Version and words**:
   - the version is set in CMakeLists.txt, CITATION.cff, packaging/rez/package.py and the metainfo `<release>`;
   - CHANGELOG has a `## [X.Y.Z] - date` section;
   - docs/guide/whats-new.md has `## Version X.Y.Z`, and tests/ui/whats_new.sfui checks it;
   - website/guide is rebuilt (`python3 tools/guide/build_site.py`).

   `release.sh` step `check` verifies all of this.
3. **CI and Packages green on main** for exactly that commit:
   - **CI** (ci.yml) runs every test on Linux x86_64 and ARM64, macOS, Windows, the VFX platforms and Blender.
   - **Packages** (packages.yml) builds every release package the way it ships, with its tests.
4. **Packaged-artifact smoke tests** (release.yml `smoke`): every package is installed or unpacked on a fresh runner.
   The app opens a model and sculpts (tests/ui/release_smoke.sfui), and `sculptforge-cli` opens, sculpts, saves and
   reopens (tests/release_smoke.py). This covers the Windows installer and .zip, the .dmg, the .deb and AppImage
   (x86_64 and arm64) and the .rpm. Publishing waits for every one.
5. **Lockstep**: the site, update feed, live guide, AUR, Homebrew and Omarchy all say X.Y.Z, and issue replies are
   drafted. Read the drafts, then post them.

## Build once, promote

A push to main runs CI and Packages. When the tag points at a commit for which both passed on main, the Release
workflow takes **that run's packages unchanged** (it checks the commit, the workflow file and the event). It
smoke-tests them, then publishes. Nothing is compiled again, so a release takes a few minutes. Otherwise (for
example, the tag is on a commit Packages skipped), Release builds the packages itself from the tag.

Two rules make this safe:
- **Compiler caches:** release builds restore main's caches, and nothing run from a tag or a pull request ever saves
  one.
- **Tokens:** only the `publish` job holds a token that can write.

The packages are not signed yet. When signing comes (SignPath for Windows, Apple notarization), it belongs in a step
between smoke and publish that signs the promoted files, so promotion stays valid.

## The script

```sh
scripts/release.sh 0.10.4                        # all steps
HIGHLIGHTS=highlights.txt scripts/release.sh 0.10.4 --from site
scripts/release.sh 0.10.4 --only replies
```

| step | does | skips when |
|---|---|---|
| check | gate 2, on a clean, up-to-date main, with gh logged in as islandmed | never (read-only) |
| ci | waits for CI and Packages on main's commit; stops if either failed | the tag exists |
| tag | asks, then tags main and pushes the tag (this publishes) | the tag is on GitHub |
| release | waits for the Release run, then checks the assets and SHA256SUMS.txt | the release has SHA256SUMS.txt |
| site | update feed (version + your highlights), home page version text, pushes; then checks the live site and guide | latest.json says X.Y.Z |
| aur | packaging/arch/PKGBUILD: pkgver and the source checksum, computed by downloading the tag's tarball | already at X.Y.Z with that checksum |
| brew | Homebrew cask through tools/publish/setup-channels.sh, after checking the .dmg's checksum here | the cask is at X.Y.Z |
| omarchy | bumps the PKGBUILD on our branch of omacom/omarchy-pkgs#1800 (author islandmed noreply) | the PR is at X.Y.Z |
| replies | writes `release-replies-X.Y.Z.md`: a draft per issue named in the CHANGELOG section; posts nothing | never |
| status | winget PR state, links, reminders | never |

The "new in X.Y.Z" card and lede on the home page, and the 3-5 feed highlights, are writing. The script checks
they exist and stops with the fix if they don't; it never writes them.

## When a step fails

The script stops with a line starting `fix:` and the command to resume (`--from <step>`). Fix that one thing, then
resume. Every step checks what is already done first, so re-running is always safe. Fix problems as they come;
don't pile them up for later.

| failure | do |
|---|---|
| check: a version file or section missing | add it, commit, push main, resume from check |
| ci: a CI or Packages job failed | runner hiccup (network, a timeout): `gh run rerun <id> --failed`. Real failure: fix on a branch, merge, start from check |
| release: a smoke test failed | the package is broken: fix on main, delete the tag (`git push origin :vX.Y.Z; git tag -d vX.Y.Z`), release again |
| release: publish failed after smoke passed | `gh workflow run release.yml -f tag=vX.Y.Z` (it promotes the same packages) |
| site: live site still old | check the Website workflow (`gh run list -w pages.yml`) and re-run it |
| omarchy: the PR merged or closed | open a bump PR there and update `OMARCHY_PR` in the script |

**Trying the pipeline without releasing:**

```sh
gh workflow run release.yml -f tag=vX.Y.Z -f dry_run=true
```

Add `-f promote_run=<Packages run id>` to smoke-test the packages of any Packages run, e.g. one dispatched from a
branch. A dry run never publishes.

## Runners

The build and test jobs run on GitHub's standard runners: free and unlimited for this public repository, up to
20 jobs at once (5 macOS). To move them to other runners (bigger GitHub runners, or a provider's), set repository
variables. No workflow edit is needed, and deleting a variable goes back to the default.

| variable | used by | default |
|---|---|---|
| `SF_RUNNER_LINUX` | CI Linux x86_64, VFX and Blender jobs, Flatpak x86_64, Linux smoke tests | `ubuntu-24.04` |
| `SF_RUNNER_LINUX_ARM` | CI Linux ARM64, arm64 VFX and Flatpak, arm64 smoke test | `ubuntu-24.04-arm` |
| `SF_RUNNER_LINUX_OLD` / `SF_RUNNER_LINUX_OLD_ARM` | the .deb / AppImage builds (older glibc on purpose) | `ubuntu-22.04` / `ubuntu-22.04-arm` |
| `SF_RUNNER_MAC` | CI macOS, the .dmg build and smoke test | `macos-15` |
| `SF_RUNNER_WIN` | CI Windows shards, the Windows build and smoke test | `windows-2022` |

Set one with `gh variable set SF_RUNNER_WIN -b <label> -R islandmed/SculptForge`, or under Settings > Secrets and
variables > Actions > Variables.

Some jobs always stay on GitHub's own runners: the ones that read the repository's change list (CI `changes`,
Release `locate`), the required-check result jobs, and `publish`, the only job with a token that can write. Build
and test jobs get no secrets, only a read-only token, so a third-party runner holds nothing that can change the
repository.
