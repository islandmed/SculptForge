# Release train v2: design

Status: design only (nothing here is built yet). v1 is in place: CI under 10 minutes for branches, note fragments
(`changes/<branch>.md`), the merge queue (`scripts/merge-queue.sh`, [docs/merge-queue.md](../merge-queue.md)),
one-command releases (`scripts/release.sh`). v2 is about shipping more often with less risk: users choose how new
they want it, risky features ship dark, and hardware we can't run in CI is checked by people before a release.

Every item below adds no port, no listener and no code from the network. Where an item touches the network or
accounts it says so, and needs Tony's OK before it is built.

## 1. Channels: Stable, Preview, Nightly

| channel | what | built by | who |
|---|---|---|---|
| Stable | tagged releases (today's) | release.yml | everyone, the default |
| Preview | the next release while testers check it (`X.Y.Z-rc.N`) | release.yml from a `vX.Y.Z-rc.N` tag, published as a GitHub pre-release | testers, anyone who opts in |
| Nightly | main, once a day if it changed and CI and Packages were green | a scheduled job promoting that day's Packages artifacts to a rolling `nightly` pre-release | developers, bug reporters asked to confirm a fix |

**Feed.** `website/latest.json` keeps its top-level fields (old apps read them and stay on Stable) and gains:

```json
"channels": {
  "preview": { "version": "0.11.0-rc.2", "download": "https://github.com/islandmed/SculptForge/releases/tag/v0.11.0-rc.2",
               "notes": "https://sculptforge.app/guide/whats-new.html#version-0110", "highlights": ["..."] },
  "nightly": { "version": "0.11.0-dev.20261110", "commit": "1065d2c", "download": "https://github.com/islandmed/SculptForge/releases/tag/nightly" }
}
```

`parseUpdateFeed` (src/core/UpdateCheck.h) reads the channel's entry and falls back to the top level. A channel
never offers something older than Stable: when Stable passes the Preview, Preview users are offered Stable.
`compareVersions` must order `0.11.0-rc.2 < 0.11.0` (today it ignores the suffix): rc and dev suffixes sort below the
plain version.

**App.** Help > Update channel: Stable / Preview / Nightly (radio, saved in the preferences). The title bar and
About show the channel for anything but Stable ("0.11.0-rc.2 Preview"). The update banner is the same; it still only
opens the download page (no download or install from the app: that would be new attack surface).

**Release script.** `scripts/release.sh X.Y.Z-rc.N` runs the same steps minus site/AUR/brew/Omarchy, and step
`status` writes `channels.preview`. The nightly job writes `channels.nightly` through the Pages workflow.

**Insiders.** Insider builds (private sculptforge-insiders repo) stay separate from Preview: the public wording is
"Insiders will get each release about two weeks early, starting with SculptForge 1.0." Before 1.0, Preview is the
public release candidate. Open question for Tony: from 1.0 on, is Preview for Insiders only (then the channel shows
only to signed-in members, which needs an account check: not before Tony decides), or public with Insiders getting
features two weeks earlier through feature flags (section 2)?

## 2. Feature flags

Risky or unfinished features merge early and ship dark instead of waiting on long-lived branches.

- `src/core/FeatureFlags.h`: a compiled-in table `{id, description, default per channel}`; `flag("quadremesh3")`.
  Defaults: off on Stable, on on Nightly, chosen per flag on Preview.
- Preferences > Experimental lists the flags with a switch each (on any channel), saved in the preferences.
  `SF_FLAGS=+id,-id` and `--flag id=on` for tests and studios (studios can pin them, like `SF_AI=off`).
- UI tests and ctest run with every flag on (and the Stable defaults in one extra UI pass), so dark code is tested.
- A flag is deleted within two releases of going on by default; `ctest flags_have_owners` fails on a flag older
  than that (a date in the table).
- No remote flags: switching features from the feed would be remote configuration of every install (attack surface,
  and surprising). If ever wanted, Tony decides.

## 3. Hardware matrix gate

CI has no tablets or GPUs; the Windows Ink and Wacom bugs (#72) came from hardware we didn't have.

- A matrix file `docs/hardware-matrix.md`: classes, not models: Windows + Wacom driver (Ink on / off), Windows +
  Windows Ink only (Surface, XP-Pen, Huion), macOS + Wacom, Linux Wayland + tablet, Linux X11, low-end iGPU,
  high-DPI 150-200%.
- Fragments mark what a branch touches: `hardware: tablet, dpi` in the front matter (a new key for
  assemble_notes.py). The #testers checklist groups its bullets by class.
- Testers report on the Preview build with the Test-this panel (section 6); results land as one GitHub issue per
  release (`Release X.Y.Z hardware check`) with a checkbox per class.
- `release.sh X.Y.Z` (non-rc), step check: if any fragment of the release names a class, that class needs one
  pass on the issue, or the release stops with the list of missing classes. `--waive tablet` with a reason in the
  release notes exists for emergencies.

## 4. Pen flight recorder

For "the pen does something weird" reports that we can't reproduce.

- A ring buffer of the last 30 seconds of raw input in memory: SDL pen/mouse/touch events with timestamps, pressure,
  tilt, proximity, the device and driver path chosen (Wintab / Windows Ink / Wacom Bridge / libinput), focus changes,
  plus what the app did with each (stroke start, camera move, Part switch). Fixed size (about 2 MB), no file until
  asked.
- Help > Report a pen problem: writes `pen-recording-<time>.sfpen` (JSON lines, gzip) to the logs folder and opens
  the folder; the bug template asks for it. Nothing is sent anywhere by the app.
- `sculptforge --replay-pen file.sfpen` feeds it back through the same input code in a UI test, so a report becomes
  a regression test (`tests/ui/pen_replays/`). The file holds no screen or mesh content; only input.
- Privacy: on by default, because it holds only input timing and positions, in memory, written only when the user
  asks. The guide's privacy page says so.

## 5. Screenshot-diff approval

Tony's polish bar: every visible change is looked at before merge.

- The UI tests already save screenshots (`shot` in tests/ui/*.sfui, uploaded as `ui-shots-*` artifacts). Main's
  Linux x86_64 shots (Mesa llvmpipe, deterministic enough) become the baseline artifact for each main commit.
- A PR's Linux run compares its shots with its base commit's: `tools/ci/shot_diff.py` (pixel difference above a
  small threshold after a 1 px blur, so font hinting noise doesn't count) writes a page of before / after / diff for
  every changed shot, uploaded as an artifact and linked in the job summary.
- Any changed shot adds a required check `Screenshots` that stays pending until a maintainer labels the PR
  `shots-approved` (the merge queue skips PRs without it). No changed shots: the check passes by itself.
- macOS and Windows shots are not compared (fonts and drivers differ); they stay as artifacts.

## 6. Test-this panel

Testers in #testers get a checklist; today they read it in Discord and answer in free text.

- Preview and Nightly builds embed the release's #testers checklist (the `testers` bullets of the fragments,
  `assemble_notes.py X.Y.Z --testers`, generated at build time into the binary like the guide).
- Help > Test This Build: the checklist with Pass / Fail / Skip and a note per item, the hardware class detected
  (OS, tablet driver path, GPU, scale) filled in, and "Copy report" (Markdown for #testers or the release issue).
  "Open issue" opens a prefilled GitHub new-issue URL in the browser (the user sends it, signed in as themselves).
  The app itself sends nothing (no new network path, no token).
- A Fail item offers "Save pen recording" when the item is in the tablet class (section 4).

## Order

1. Feature flags (unblocks merging early; no infrastructure).
2. Channels: Nightly first (artifacts already exist per main commit), then Preview with rc tags.
3. Screenshot diff (CI only).
4. Test-this panel + hardware matrix gate (needs 1 and 2: testers on Preview).
5. Pen flight recorder.
