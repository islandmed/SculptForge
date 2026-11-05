---
whats_new: **One-click update**: on the Linux AppImage and on Windows (installer or portable zip) the update banner
  has an **Update** button: it downloads the new version in the background, checks its signature and checksum, and
  installs it on **Restart now** or when you quit (**Later**). The old version is kept until the new one has
  started, and put back if it doesn't. Package-manager installs show what to run instead.
highlight: One-click update, signed and checked
added: **One-click update** (the update banner's **Update** button) for the Linux AppImage and the Windows installer
  and portable zip: background download with progress and Cancel, then Restart now or Later (installs at quit).
  Files come only from SculptForge's GitHub releases (every redirect checked, TLS verified); the release's
  SHA256SUMS.txt must carry an Ed25519 signature under the key built into the app, and the package must match its
  checksum, before anything is written in place or run. The AppImage is replaced in one atomic rename with the old
  file kept as `.old` until the new version has started; a new version that doesn't start is rolled back. Flatpak,
  .deb/.rpm, AUR and Homebrew installs are never replaced (the banner names the command or says to download).
  macOS keeps the Download button for now. See the guide's Installing > Updating.
security: Release checksums (SHA256SUMS.txt) are now signed with Ed25519 (SHA256SUMS.txt.sig, a 64-byte raw
  signature); the release workflow checks the signing secret against the key compiled into the app.
testers:
  - Linux AppImage: run an older AppImage, Help > Check for updates, click Update; the bar fills, Restart now starts
    the new version (Help > About) and the `.old` file next to the AppImage disappears after a few seconds.
  - Linux AppImage: Update, then Later, then quit: the next start is the new version.
  - Windows portable zip: Update, Restart now: the folder is updated in place and the app starts again; the
    `<folder>.old` copy is gone after the new version has run for a few seconds.
  - Windows installer: Update, Restart now: Windows asks for permission, the installer runs silently and the new
    version starts.
  - Flatpak or .deb install: the banner says to update with the package manager instead of showing Update.
---
The Update button stays hidden until the release signing key exists (kUpdatePublicKeyHex in
src/core/SelfUpdate.h and the UPDATE_SIGNING_KEY secret; steps in REPORT.md). Tester checks need a signed release
newer than the build under test.
