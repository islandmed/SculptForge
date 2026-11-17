---
whats_new: **Scoop**: on Windows, `scoop install sculptforge` installs the portable build and `scoop update sculptforge` keeps it current (bucket: islandmed/scoop-sculptforge, see the install page).
added: Scoop manifest for the portable Windows .zip (`packaging/scoop/sculptforge.json`, checkver and autoupdate from the GitHub releases), kept at the newest release by the release script like winget, Homebrew and the AUR; `sculptforge-cli` is on the PATH and SculptForge gets a Start menu shortcut. Fixes #170 (thanks @NemoStein).
testers:
  - Windows with Scoop: `scoop bucket add sculptforge https://github.com/islandmed/scoop-sculptforge`, `scoop install sculptforge`; SculptForge is in the Start menu and `sculptforge-cli --version` runs in a new terminal.
---
Packaging only; no app code changed.
