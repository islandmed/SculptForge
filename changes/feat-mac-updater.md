---
whats_new: **One-click update on macOS**: the update banner's **Update** button now works for the Mac app too. It downloads the new release, checks it, and **Restart now** swaps it into place; the old version is kept until the new one has started.
highlight: One-click update on macOS
added: **One-click update on macOS** (update banner > **Update**): the Mac app downloads the release's disk image from
  SculptForge's GitHub releases to `~/Library/Caches/SculptForge/updates`, checks it against the release's signed checksum
  list, copies the app out of it beside the current one, runs macOS's code signature check (`codesign --verify --deep
  --strict`, and the same developer as the running app), and swaps the bundles in one step on **Restart now** (or at quit
  with **Later**). The old bundle is kept as `SculptForge.app.old` until the new version has started once, and put back
  when it doesn't start. No administrator password is ever asked for. Homebrew installs keep their `brew upgrade`
  line. `SF_UPDATE_DRY_RUN=1` runs every check and replaces nothing.
testers:
  - macOS (an older release in /Applications, Help > Check for updates): Update downloads with a progress bar; the next
    screen names the version, its size and github.com/islandmed/SculptForge; Restart now starts the new version; About
    shows it; no SculptForge.app.old is left after a few seconds.
  - macOS, Later: quit; the next start is the new version.
  - macOS, SF_UPDATE_DRY_RUN=1 in the environment (`SF_UPDATE_DRY_RUN=1 open -a SculptForge` won't do; start the binary
    from Terminal): the status line says it was unpacked and removed; /Applications unchanged.
  - macOS, a bundle in a folder you can't write to (an admin's /Applications as a standard user): the banner says it can't
    write there; nothing changed, Download still opens the page.
  - Windows and Linux: Update unchanged (progress, Restart now, Later).
---
Attack-surface notes and the signing decision for the owner: REPORT.md in the worktree (not shipped).
