# Release lanes: design

Status (11-07): §1 built (feat/release-lanes), §2 built (feat/integration-lanes), §3 built (feat/release-overlap),
§4 live in ~/Work/.sf. Before this, two lanes existed: **site** (website/, README, brand: `tools/ci/site_lane.txt`, live from main in
minutes) and **app** (everything else: the release train). A credit, a quote or a brush still needs an app release.
This splits the rest so each kind of change ships at its own pace, and makes the app train itself smaller.

| lane | what | ships | gate |
|---|---|---|---|
| site | website/, README, brand | main, minutes | sitegate.sh |
| content (new) | credits, Founding count/names, testimonials; later packs | main, minutes; apps pick it up within a day | sitegate.sh + content check + signature |
| integration (new) | Blender add-on, Maya plugin | own tag `blender-vX.Y.Z` / `maya-vX.Y.Z` when they change | their own tests |
| app | the app | release train | T0, stage, PC suite |

## 1. Content lane

**What.** One signed file, `website/content/v1.json`, published by pages.yml with `v1.json.sig` next to it:

```json
{"schema": 1, "serial": 42, "supporters": ["..."], "founding": {"limit": 100, "taken": 2, "names": ["..."]},
 "testimonials": [{"quote": "...", "name": "...", "role": "..."}]}
```

**App.** With the daily update check (same timing, same thread, same two hosts), fetch `v1.json` + `.sig`, verify,
parse, cache in the preferences folder; About, the membership card and the members page read the cache, else the
compiled-in copy (docs/supporters.txt, website/founding.json, website/testimonials.json: kept as the offline
fallback and refreshed by each release's bump).

### Security note (read before any code merges)

New attack surface: one HTTPS GET of a small text file, plus its signature, from hosts the app **already** contacts
for the update check. No new host, port, listener, IPC or code path that runs anything.

- **Fixed URLs only**, compiled in: `https://sculptforge.app/content/v1.json` and the GitHub Pages mirror
  `https://islandmed.github.io/content/v1.json` (same pair as `kUpdateFeedUrl`/`kUpdateFeedMirrorUrl`). Through
  `httpGetRestricted`: https, certificates checked, no redirects off the allowed prefix. Studio/offline mode
  (`net::urlAllowed`) refuses it like every other request.
- **Signature, not a pinned hash** (a pinned hash would need a release per change, which is the thing we remove).
  Ed25519 over the exact bytes (`core/Ed25519.h`, already used by one-click update). **A separate content key**, not
  the release key: the content key signs on every main push, so it must never be able to vouch for a binary.
  Public half compiled in (`kContentPublicKeyHex`); private half only in the GitHub environment `content`
  (secret `CONTENT_SIGNING_KEY`), used by one pages.yml step on main. If it leaks, the worst case is wrong text in
  About until we rotate the key in the next release; nothing executes.
- **Rollback/replay:** `serial` must be >= the cached one (and >= the compiled-in serial), else ignored.
- **Budget:** 64 KiB max (refused while reading, not after), 10 s timeout, one attempt per day, background thread;
  failure is silent and keeps the cache/fallback. Startup never waits on it.
- **Strict parsers, reused:** names through `sanitizeSupporterName` (UTF-8 checked, no control/bidi/zero-width,
  40 chars, 2000 max), founding through `parseFoundingMembers` (clamped), testimonials through a new parser with the
  same rules (quote 400 chars, 12 max). Unknown keys ignored; a higher `schema` is ignored (old apps keep the cache).
  Nothing parsed is ever a path, URL, markup or format string; text is drawn as plain text.
- **No code, ever.** Packs (brushes, alphas, materials, Library) are a later step and need their own note: their
  parsers (images, meshes) are bigger surface. They would ride the same signed manifest with a SHA-256 per file.
- **Opt-out:** Preferences > Updates "Fetch credits and community content" (on), `SF_CONTENT=off`, studio mode off.
  Nothing about the user is sent (same GET as the feed; no cookies, no ID).
- **OKs:** coordinator (11-07) and Tony (11-07: the fetch, the key and the `content` environment, main only). The
  key was made by `~/Work/.sf/lanes/make-content-key.sh`; its private half went straight into the secret.

Once shipped, the `!` lines in `tools/ci/site_lane.txt` go: app tests read fixtures, not the live credits, and the
compiled-in copies are refreshed by `release.sh` bump.

## 2. Integration lanes

The Blender add-on and the Maya plugin are Python files the user installs; they change rarely and should not wait
for (or force) an app release.

- Own version: `addons/blender/.../blender_manifest.toml` / `bl_info`, `integrations/maya/sculptforge_maya.py`
  `__version__`. Notes in `changes/<branch>.md` with `lane: blender` front matter.
- Tags `blender-vX.Y.Z` / `maya-vX.Y.Z` run `integrations.yml`: test, zip, a GitHub release with that name, the
  download link on the site. Never touches the app version, latest.json or packages.
- Compatibility by **protocol version**, not app version: the bridge manifest already carries `version` (1). The app
  states the range it reads (`kBridgeProtocolMin..Max`); the add-on shows "update SculptForge" or "update the add-on"
  instead of failing. MCP is compiled into the app (src/cli) and stays on the app train; the Unreal link already
  has its protocol field.
- The app still bundles the current add-on zip so offline installs work.

## 3. Smaller app trains

- Already in place: T0 runs only impacted tests (`tools/test/impacted.py`), the full suite at stage, and the test
  runner (tools/test) fails a test over its time budget (`--budget`, 20 s) after a rerun alone.
- Stage N+1 while N promotes: a promote (`release.sh X.Y.Z --yes`) holds a lock (`sf-promoting` in the git common
  dir, with its pid). N+1's `--stage` runs staging, CI, packages and T1 meanwhile; only main's fast-forward
  (`merge-queue.sh staging`) waits for the lock, then lands on top of the promote's paperwork (paperwork_only.py) or
  stops as before. Promote already finds staging's packages by commit, not by `staging`'s tip.

## 4. PC disk hygiene

`pc-reap.sh`, every 6 h from reaper.sh: removes the WSL build trees of worktrees that are gone (any `git worktree list`
checkout counts as alive) or idle for 2 days (`build/.ninja_log` age), skips a tree whose pc.sh lock is held, fstrims
WSL, and writes C:'s free GB to `~/Work/.sf/pc-cfree.txt`. Before 11-07 it skipped whenever any pc.sh job ran, which
was nearly always, so it rarely did anything. ccache is already capped at 5 GB. Freed blocks go back to Windows only
when the WSL disk is sparse, which Tony sets once: `wsl --manage Ubuntu-24.04 --set-sparse true` (after
`wsl --shutdown`). Next, if C: keeps running low: pc.sh reads pc-cfree.txt and sends jobs to the laptop under 15 GB.

## Order

1 (biggest win; waits on the security OK) -> 4 (small, ops) -> 3 -> 2. Every slice: T0 or sitegate, REPORT.md, the
coordinator.
