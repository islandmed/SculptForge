# Releasing SculptForge

One page for whoever coordinates a release. One command does the work:

```sh
HIGHLIGHTS=highlights.txt scripts/release.sh X.Y.Z --yes
```

It either stops within seconds, before anything is tagged, with the fix, or runs through to the end. This page explains
the gates, what each step does, and how to recover.

## Gates (in this order; a release never skips one)

1. **Merged tree, tested locally**: on main with the branches for this release merged, `ctest --test-dir build -j3`
   all green.
2. **Version and words**:
   - written by people, on main before the release: one note fragment per branch, `changes/<branch>.md`
     (changes/README.md: the What's New bullet, CHANGELOG entry and section, highlight, #testers bullets); in
     docs/guide/whats-new.md `## Version X.Y.Z`, with tests/ui/whats_new.sfui expecting it, its ```features block
     (date: unreleased, a tagline, highlights and cards: the app's What's New window, the guide, and the home page's
     card and lede come from it). The 3-5 feed highlights are the version's `highlight:` lines (What's New and
     fragments), or `HIGHLIGHTS=file` to override;
   - set by step `bump`: the fragments go into CHANGELOG `[Unreleased]` and What's New and are deleted
     (tools/release/assemble_notes.py); the version in CMakeLists.txt, CITATION.cff (with date-released), packaging/rez/package.py
     and the metainfo `<release>`; CHANGELOG `[Unreleased]` becomes `## [X.Y.Z] - date` under a new empty
     `[Unreleased]`; website/guide is rebuilt. Don't bump version files by hand.

   Step `check` verifies all of this, plus what the steps after the tag need (PKGBUILD format, the Homebrew tap, the
   Omarchy PR is open, the home page's version text), so nothing stops after the tag for a reason known before it.
3. **CI and Packages green on main** for exactly that commit:
   - **CI** (ci.yml) runs every test on Linux x86_64 and ARM64, macOS, Windows, the VFX platforms and Blender.
   - **Packages** (packages.yml) builds every release package the way it ships, with its tests.
4. **Packaged-artifact smoke tests** (release.yml `smoke`): every package is installed or unpacked on a fresh runner.
   The app opens a model and sculpts (tests/ui/release_smoke.sfui), and `sculptforge-cli` opens, sculpts, saves and
   reopens (tests/release_smoke.py). This covers the Windows installer and .zip, the .dmg, the .deb and AppImage
   (x86_64 and arm64) and the .rpm. Publishing waits for every one.
5. **Lockstep**: the site, update feed, live guide, AUR, Homebrew and Omarchy all say X.Y.Z, and issue replies are
   drafted. Read the drafts, then post them.
   - **Discord**: the #announcements post is automatic once the `DISCORD_ANNOUNCE_WEBHOOK` secret is set
     (.github/workflows/discord-announce.yml). It waits for step `site` to put this version's highlights in
     website/latest.json (up to 45 minutes). Other posts: .github/announcements/*.md, Actions > Discord announcement.

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
HIGHLIGHTS=highlights.txt scripts/release.sh 0.10.4 --yes   # all steps
HIGHLIGHTS=highlights.txt scripts/release.sh 0.10.4 --from site                    # resume
scripts/release.sh 0.10.4 --only replies
```

The home page's What's New area (between the `whats-new` markers in website/index.html) is written by
`tools/guide/build_site.py` from whats-new.md: the newest three versions as cards, older ones as groups.

| step | does | skips when |
|---|---|---|
| bump | refuses without What's New, its UI test, CHANGELOG entries, the version's features block and HIGHLIGHTS; then sets the version files, CHANGELOG section and the features block's date, rebuilds the guide and the home page's What's New area, commits as islandmed, pushes main | each file already at X.Y.Z |
| check | gate 2 and every precondition of the later steps, on a clean, up-to-date main, with gh logged in as islandmed | never (read-only) |
| ci | waits for CI and Packages on main's commit; stops if either failed | the tag exists |
| tag | asks, then tags main and pushes the tag (this publishes) | the tag is on GitHub |
| release | waits for the Release run, then checks the assets and SHA256SUMS.txt | the release has SHA256SUMS.txt |
| site | update feed (version + your highlights), home page version text, pushes; then waits for the live feed (10 min) and guide (5 min) | latest.json says X.Y.Z |
| aur | packaging/arch/PKGBUILD: pkgver and the source checksum, computed by downloading the tag's tarball | already at X.Y.Z with that checksum |
| brew | Homebrew cask through tools/publish/setup-channels.sh, after checking the .dmg's checksum here | the cask is at X.Y.Z |
| omarchy | bumps the PKGBUILD on our branch of omacom/omarchy-pkgs#1800 (author islandmed noreply) | the PR is at X.Y.Z |
| replies | writes `release-replies-X.Y.Z.md`: a draft per issue named in the CHANGELOG section; posts nothing | never |
| status | winget PR state, links, reminders | never |

The highlights and What's New's words are writing: the script never invents them. It checks they exist before
tagging.

Scripting rule: under `set -o pipefail`, never end a pipe with `grep -q` or `head` (they close the pipe early and the
writer fails); capture to a variable or file first, or let grep read everything (`grep ... >/dev/null`).

## When a step fails

The script stops with a line starting `fix:` and the command to resume (`--from <step>`). Fix that one thing, then
resume. Every step checks what is already done first, so re-running is always safe. Fix problems as they come;
don't pile them up for later.

| failure | do |
|---|---|
| bump/check: What's New, its features block, its test or HIGHLIGHTS missing | write it, commit, push main (or pass HIGHLIGHTS), run again |
| check: a version file missing | run step bump (`--only bump`), then resume from check |
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
