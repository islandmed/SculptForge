# What is in .github

(This file is `WORKFLOWS.md`, not `README.md`, on purpose: GitHub shows a `.github/README.md` as the repository's
front page in place of the one at the top.)

The automation that builds, tests, packages and publishes SculptForge, and the templates for issues and pull
requests. New here? You only need the first two rows.

| Path | What it is |
|---|---|
| `workflows/ci.yml` | **CI**, on every push and pull request: the trademark check (`tools/check_marks.py`), then build and test on Linux x86_64 and ARM64, macOS and Windows (tests split into shards), the Blender add-on, the VFX Reference Platform builds and the oldest supported Linux. The `result` jobs (one per platform) turn all of those into one pass or fail |
| `ISSUE_TEMPLATE/`, `PULL_REQUEST_TEMPLATE.md` | The forms for bugs, feature requests and benchmark results, and the pull request checklist. [CONTRIBUTING.md](../CONTRIBUTING.md) explains what to put in them |
| `dependabot.yml` | Monthly updates of the GitHub Actions versions we use |

Everything else is maintainers' machinery. Pull requests from forks do not get any secrets, and workflow changes
need a maintainer's review.

## Test tiers

| Workflow | When | What |
|---|---|---|
| `t0.yml` | a branch pushed for a quick check | Builds the branch and runs only the tests the change can break (`tools/test/impacted.py`), plus the trademark check |
| `ci.yml` | push, pull request | The full matrix above |
| `nightly.yml` | daily | The tests labelled `nightly`: stress and benchmark checks left out of CI |
| `android.yml` | push, daily | The Android APK build (see [android](../android/README.md)) |
| `flatpak.yml`, `arch.yml`, `repos.yml` | packaging changes | Build and install the Flatpak, the Arch package, and the signed apt and dnf repositories from scratch |
| `signing-selftest.yml` | by hand | The Windows code-signing checks, against a throwaway certificate |

How the tiers fit together: [docs/testing-flow.md](../docs/testing-flow.md).

## Release and publishing

| Workflow | What |
|---|---|
| `packages.yml` | Builds every release package (.deb, .rpm, AppImage, tar.gz, Flatpak, .dmg, Windows installer and .zip) and tests it as shipped. Runs on `staging` and `main`, so a release promotes those packages instead of building again |
| `release.yml` | A `v*` tag publishes the GitHub release from the tested packages: checksums, signature for the one-click updater, SBOM, MCP bundle. Process: [docs/releasing.md](../docs/releasing.md) |
| `integrations.yml` | Releases of the Blender add-on and the Maya script, which ship on their own tags (`blender-vX.Y.Z`, `maya-vX.Y.Z`) |
| `pages.yml` | Builds and deploys the website from [website](../website/README.md) and the guide from [docs/guide](../docs/guide) |
| `discord-announce.yml`, `member-requests.yml`, `monthly-challenge.yml` | Community posts and the monthly sculpt challenge (`challenges.txt` is its theme list; `announcements/` the text of some posts) |

## Housekeeping

`ci-status.yml` and `prune-caches.yml` keep CI quick and cheap; `download-stats.yml` and `jobs.yml` write the data
behind the public stats and jobs pages; `upstream-watch.yml` opens an issue when a project we track (listed in
`packaging/upstream.json`) has a new release; `attachment-guard.yml` hides executables and archives attached to issues
and comments, because only builds from our own pipeline are offered for download.

## Changing a workflow

Keep `permissions:` as small as the job allows, pin new actions by version, never run code from a pull request with
secrets, and take no input from issue or pull request text into a shell. Test it on your fork first. Workflows that
publish need a maintainer to try them.
