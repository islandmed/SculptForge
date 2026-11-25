# tools

Scripts and small programs around the code: checks that run in CI, generators for assets and the website, test and
release helpers. Most are Python 3 with no packages, and each one starts with a comment or docstring that says what
it does and how to call it; read that first. A tool that checks something has a `--self-test` or a ctest entry.

## Checks you will meet in a pull request

| Tool | What it checks |
|---|---|
| `check_marks.py` | Fails when a third-party sculpting-app trademark appears anywhere in the repository, in any spelling. The policy is [docs/legal/trademarks.md](../docs/legal/trademarks.md): SculptForge uses its own names only. Runs in CI and in every pre-merge check |
| `check_marks_media.py` | The same for images and videos: reads text in them. Run it after adding one, and commit `marks_media_cache.json` with the new image |
| `check_public_email.py` | A private inbox address must not appear in a public file |
| `media_takedown.py` | Before the website deploys, removes any image or video that `check_marks_media.py` flagged, and its references in the pages |
| `dev/check_literals.py`, `dev/check_macos_api.py`, `dev/icons_used.py` | Raw string literals MSVC accepts, calls the macOS 11 package cannot use, and the list of icons the code uses (`IconsUsed.h`) |
| `i18n/coverage.py` | Which interface strings lack a translation, and raw labels that bypass the translator (`i18n/subset_cjk_font.py` cuts the CJK fonts down to the characters used) |
| `ci/` | Helpers CI calls: `t0.sh` (build and run only the tests a change can break), `no_revert.py`, `iwyu_lite.py` (headers compile without the precompiled header), `prune_caches.py`, signing checks |

## Testing

| Tool | What |
|---|---|
| `test/` | The test flow: `run.py` (ctest plus a result cache, a flake rerun and a time budget), `impacted.py` and `affected.py` (which tests a change can break; `tests/areas.map`), `warm_ui.py` (a pool of warm UI-test servers), `shard_times.py`, `shrink_sfui.py` (shrinks a failing UI script to the fewest lines), `tsan.sh` |
| `qa/` | Artist-style sessions: plays `tests/artist/*.sfui` and close-ups in the real app and measures them (mirror check, close-up sheets). [README](qa/README.md) |
| `export_check/` | Checks textured exports with other tools than our own loaders ([README](export_check/README.md)) |
| `wintest.sh` | Cross-compiles for Windows with llvm-mingw and runs the whole suite under Wine |
| `bench_strokes.sh`, `precision_check.sh`, `benchmark/`, `perf_scoreboard.py`, `dev/edit_timing.sh` | Performance: stroke benchmark, precision at extreme zoom, the bench scene pack and result validator, and how long the edit-build loop takes. See [bench](../bench/README.md) |

## Generating things (change the generator, not the output)

| Tool | Makes |
|---|---|
| `brand/` | The app icon in every size, the marks and the social icons ([docs/brand.md](../docs/brand.md)) |
| `starter/`, `creature-bones/`, `face/` | The starter meshes, the Creature bones insert set and the facial-shapes template in [assets](../assets/README.md) |
| `guide/` | The website's user guide from `docs/guide`, its screenshots, and the keyboard table ([README](guide/README.md)) |
| `brush-packs/`, `brush-gallery/` | The built-in brush packs and the community brush gallery on the website (`brush-gallery/repo` is the gallery's own repository, with its README) |
| `plugin/sfplugin.py` | A `.sfplugin` from a WebAssembly module and a manifest ([sdk/plugin](../sdk/plugin/README.md)) |
| `readme_art.sh` | The artwork on the GitHub front page |
| `splat/` | A tiny synthetic phone capture for splat-training tests |

## Release, packaging and community (maintainers)

| Tool | What |
|---|---|
| `release/` | The pieces `scripts/release.sh` and `release.yml` call: release notes from `changes/*.md` fragments, the build-input key, checksum signing, the SBOM, the MCP bundle, winget manifests, install-script checks ([docs/releasing.md](../docs/releasing.md)) |
| `publish/` | Signed apt and dnf repositories, and mirrors for networks that block new domains |
| `studio/` | Checks a studio profile before it is baked into a studio build ([docs/studios.md](../docs/studios.md)) |
| `upstream-watch/` | Opens an issue when a project we target has a new release (`packaging/upstream.json`) |
| `download-stats.py`, `jobs/`, `drops/`, `member-requests/` | Data and community features of the website and Discord: download statistics, the jobs page, daily drops and monthly member requests |
| `comfyui-smoke.sh`, `comfyui-validate-workflow.py` | Run and validate a ComfyUI workflow against a ComfyUI server you started |

## Adding a tool

Keep it dependency-free where you can (the standard library, POSIX shell), put the usage in its first comment or
docstring, give it `--self-test` if it decides pass or fail, and register it in `CMakeLists.txt` as a ctest entry
if it should gate changes. Never write secrets, tokens or personal data into its output or its files.
