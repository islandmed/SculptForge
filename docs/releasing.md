# Releasing SculptForge

One page for whoever coordinates a release. One command does the work:

```sh
HIGHLIGHTS=highlights.txt scripts/release.sh X.Y.Z --yes
```

It either stops within seconds, before anything is tagged, with the fix, or runs through to the end. This page explains
the gates, what each step does, and how to recover.

## Gates (in this order; a release never skips one)

1. **Merged tree, tested once on each machine** (docs/testing-flow.md): the merger's batch goes to the branch
   `staging` (`scripts/merge-queue.sh staging --with ~/Work/.sf/t1.sh HEAD`). The whole suite runs on the PC (T1) while
   the full CI runs on staging (T2), and main fast-forwards to the batch only when both are green. Nobody runs a
   second local full ctest. Packages (packages.yml) is built on staging too, beside CI. **The release batch carries
   the bump**: `RELEASE=X.Y.Z [HIGHLIGHTS=f] ~/Work/.sf/merge-batch.sh <branches>` runs
   `scripts/release.sh X.Y.Z --only bump --staged` on top of the merged batch (it commits, pushes nothing), so staging
   builds and tests the packages of the exact commit that will be tagged. The release command afterwards finds the
   versions already set, reuses CI and promotes those packages: tag to published is the smoke tests, about 5 minutes.
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
3. **CI and Packages green on main** for exactly that commit. CI's run on main reuses the staging run of the same
   commit. For the bump commit, step `ci` and release.yml reuse the CI result of the commit before it when the bump
   only added release paperwork (`tools/release/paperwork_only.py`: exact files and lines, fails safe). Step `check` has
   already run the bump's own tests (What's New, the guide, notes) on the bumped tree. Packages still has to pass for the
   bump commit itself:
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

A push to main runs CI and Packages; a batch that went through `staging` already ran both there, and main's runs
reuse them. When the tag points at a commit whose CI passed on main and whose Packages passed on staging or on main,
the Release workflow takes **that run's packages unchanged** (it checks the commit, the workflow file, the branch and
the event). It
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

## Betas (members only)

A beta is the next release, published early for members (Member and Studio; docs/design/release-train.md §1). Since
0.11.1 betas are not public: they go to the private repository islandmed/sculptforge-insiders, and members are invited
to it by hand with the GitHub username they gave at checkout.

```sh
scripts/release.sh 0.12.0-beta.1 --yes                      # a beta (also 0.12.0-rc.1)
HIGHLIGHTS=highlights.txt scripts/release.sh 0.12.0 --yes   # later: the stable release, on main as it is then
gh api -X PUT repos/islandmed/sculptforge-insiders/collaborators/USERNAME -f permission=pull   # a new member
```

Members are **always** invited with `permission=pull` (read: download releases, nothing else); never `push`,
`maintain` or `admin`. Check with `gh api repos/islandmed/sculptforge-insiders/collaborators --jq '.[]|.login+" "+.role_name'`.

Steps for a beta: `bump check ci tag release status`.

- **bump** writes the note fragments into CHANGELOG `[Unreleased]` and What's New `0.12.0` (date stays `unreleased`), and
  saves their testers bullets to `release-testers-0.12.0-beta.1.md` first. It sets CMakeLists.txt to
  `VERSION 0.12.0` and `set(SCULPTFORGE_PRERELEASE "beta.1")`. The app reads "0.12.0 Beta 1" in the title bar and
  About; packages are named `SculptForge-0.12.0-*`. CITATION, rez, metainfo, the CHANGELOG section and the home page
  stay at the stable release (`build_site.py` leaves the home page alone while a prerelease is set).
- **check** checks CMakeLists.txt (version and prerelease), the guide, the DPI tests, the workflows (below, "Secrets"),
  that the `member-release` environment has the `INSIDERS_REPO_TOKEN` secret and that gh can see the private repository. A beta skips the site/AUR/Homebrew/Omarchy
  checks.
- **tag / release**: `v0.12.0-beta.1` (the tag is public, like the source). release.yml checks that the tag equals the
  version plus the prerelease, then its job `publish-members` waits for the maintainer's approval (one click in the
  run, or `gh run view`), publishes a **pre-release in islandmed/sculptforge-insiders** with
  `INSIDERS_REPO_TOKEN` (a fine-grained token for that repository only, Contents: read and write), and posts it in
  #members (`DISCORD_MEMBERS_WEBHOOK`). Nothing is published in the public repository, and the update feed is
  untouched: the app's Preview channel offers only what the feed names.
- **status** prints the private release link, the invite command and the testers checklist (post it in #members).
- No site, feed, AUR, Homebrew, Omarchy, winget or issue replies for a beta.

**Promoting** is the normal stable release of `0.11.0`. Its bump clears `SCULPTFORGE_PRERELEASE`, so it is a new commit
after the beta's. Step `site` removes any older `channels.preview` entry (from the public betas before 0.11.1). In the
app, Preview never offers anything older than stable. The beta's tag doesn't get in the way: stable releases take
the guide from the newest tag without a `-` (pages.yml). A second beta is `0.12.0-beta.2`.

### Secrets (the members' publishing path)

What keeps the insiders token from leaking; `tools/release/check_workflows.py` (run by release.sh check and ctest
`release_workflows`) fails when the workflow side of it breaks.

- `INSIDERS_REPO_TOKEN` exists only as a secret of the environment **`member-release`**, never at repository level:
  deployment only from tags `v*`, required reviewer islandmed (self-review allowed: solo maintainer). Only release.yml's
  `publish-members` uses it; a workflow_dispatch from a branch can't. Set or rotate (never echo it):
  `wl-paste -n | gh secret set INSIDERS_REPO_TOKEN --env member-release -R islandmed/SculptForge`.
  (The daily-drop workflow and the preview-build workflow were retired with the Insider tier; betas are the members' builds.)
- member-requests.yml's `publish` job is the only other user, also in `member-release` (one approval a month); it
  runs from `main`, so the environment's deployment rule lists `main` besides tags `v*`. Its `public` job, which
  writes the `packs` branch, has no environment and never sees the token.
- release.yml: top-level `permissions: contents: read`; `publish-members` reads only, runs only for betas, and passes
  the token as an env var to its one publishing step; every `uses:` pinned to a full commit SHA with the version in a
  comment (update by resolving the new tag: `gh api repos/OWNER/REPO/git/ref/tags/vX.Y.Z --jq .object.sha`); checkouts
  don't persist credentials; no `set -x`.
- No workflow uses `pull_request_target`; fork pull requests get no secrets (GitHub's default for `pull_request`), and
  every outside contributor's run waits for approval.
- Repository: secret scanning and push protection on, Dependabot alerts and security updates on, the default workflow
  token read-only, Actions may not create or approve pull requests.
- islandmed/sculptforge-insiders: private, members invited with `permission=pull` only, ruleset on `main`: no force
  push, no deletion.

## Membership upkeep (by hand)

- **Founding Members:** `website/founding.json` (`limit` 100, `taken`, `names`). On each new member among the first
  100: give them the Discord role, `taken` + 1, and their name in `names` only if they want it listed. The website's
  counter and Members page read it live; the app compiles it in (Help > About, and the membership card's founding
  note, which stops once `taken` reaches `limit`).
- **Monthly challenge results:** when the winners are announced, add `"challenge": {"id": "YYYY-MM", "title":
  "<theme>"}` to `website/latest.json` (push main; the site deploys). The app's daily update check reads it once per
  id: the small membership card's challenge moment ("The <theme> challenge results are in, on Discord").
- **Member Requests:** each approved request goes into islandmed/sculptforge-insiders as
  `requests/YYYY-MM/<slug>/request.json` + its file (+ `preview.png`), `"approved": true` once the requester has
  OK'd the preview (format: `tools/member-requests/requests.py`). On the 1st, member-requests.yml packs last month's,
  waits for the `member-release` approval, releases `requests-YYYY-MM` there, posts in #members, and schedules the
  pack in the public listing (`packs` branch) for a month later.

## Studio builds

A Studio member's build is public SculptForge at a pinned tag with the studio's profile baked in (docs/guide/studios.md
"Your studio build"; src/core/StudioProfile.h). The code is the public, GPL code; only the studio's own files are
private, in one private repository per studio.

```
<private org>/studio-<id>          private; the studio's IT invited with permission=pull
  studio/studio.json               the profile (tools/studio/check_profile.py checks it)
  studio/packs/<id>.sfpack         brushes, alphas, Lit Spheres, keymaps, macros (made with File > New Pack)
  studio/ui.json, studio/logo.png  first-start interface settings, branding
  .github/workflows/release.yml    tools/studio/studio-release.yml, copied
```

- **Build:** Actions > Studio release > Run workflow with the tag (the current release, or the studio's LTS line). It
  checks out islandmed/SculptForge at that tag and the studio repo, runs `check_profile.py`, builds with
  `-DSCULPTFORGE_STUDIO_PROFILE=studio/studio.json` (the profile is compiled in, the folder is installed as
  `share/sculptforge/studio`), packages Linux tar.gz and Windows zip, and publishes a private release in the studio
  repository with `SHA256SUMS.txt` and `SHA256SUMS.txt.sig`. Add macOS when a studio asks for it.
- **Signing:** the same key as public releases. `UPDATE_SIGNING_KEY` is a secret of the environment `studio-release` in
  each studio repository (required reviewer islandmed; never at repository level); `tools/release/sign_sums.sh`
  refuses a key that isn't the app's `kUpdatePublicKeyHex`. Give the studio's IT the public key as a PEM to check
  installers offline.
- **Updates:** the build reads only `updates.feed` / `updates.mirror` from its profile. For each new studio release,
  write a `latest.json` (the public feed's format, core/UpdateCheck.h) to the studio's channel; the studio copies it to
  its mirror when it wants to move. One-click update stays off in studio builds (it downloads only public releases).
- **Security fixes:** backported only to the current release and ONE LTS line (`lts/X.Y` branches, tagged
  `vX.Y.Z`); every studio is on one of the two. Rebuild each studio on the new tag the day the fix ships.
- **Privacy:** a studio's files never go into public builds, packs or the public repository, and are never run (data
  only). Studio mode is tested: `ctest -R studio` (no request or socket leaves the machine).

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
