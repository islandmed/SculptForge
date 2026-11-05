# One-click update

The update banner's **Update** button: `src/core/SelfUpdate.{h,cpp}` (what is trusted, the download, the swap,
rollback), `src/core/Ed25519.cpp` (signature check), `ai::httpGetRestricted` (the network), `src/app/PanelUpdate.cpp`
(the banner, Restart now / Later, watching the restart). Tests: `tests/test_self_update.cpp`.

## Trust chain

1. **Feed** (`latest.json`, unchanged): names the newest version. It can only offer a *newer* version; it chooses no
   URLs for the updater.
2. **Where**: `https://github.com/islandmed/SculptForge/releases/download/v<version>/<file>` only (no port, user,
   query, `..`, escapes), and redirects only to `objects.githubusercontent.com` / `release-assets.githubusercontent.com`.
   Every hop is checked before it is requested; https only, certificates checked (curl defaults made explicit,
   WinHTTP defaults).
3. **Signature**: `SHA256SUMS.txt.sig` is a raw 64-byte Ed25519 signature over `SHA256SUMS.txt`, checked with the
   public key in `kUpdatePublicKeyHex`. Mbed TLS 3.6 (what libdatachannel links) has no Ed25519, so the check is a
   verify-only port of TweetNaCl's arithmetic (public domain) with RFC 8032's S < L rule, tested against RFC 8032's
   vectors and OpenSSL-made signatures. No new library, nothing fetched at build time.
4. **Version binding**: the app downloads the *versioned* file (`SculptForge-0.12.0-x86_64.AppImage`), whose name is
   inside the signed list, so an older release's signed files can't be replayed under a newer tag (no downgrade).
5. **Hash**: the package is hashed as it arrives (SHA-256), capped at 1 GiB, and hashed again right before it is
   installed (a Later install waits on disk until quit).

Fail at any step: the temporary file is deleted, nothing in place changes, nothing downloaded runs.

## Installing

- **Linux AppImage**: download to `.<asset>.download-<version>` next to the AppImage (same filesystem), `chmod 755`,
  hard-link the current file to `<name>.old`, write `update_pending.json` in the preferences folder, then one
  `rename()` over the AppImage. The running app keeps its open file.
- **Windows**: a PowerShell helper (written to %TEMP%, deletes itself) waits for the app to exit, re-checks the hash,
  then: installer: `setup.exe /S /D=<folder>` (UAC as usual; the package was downloaded to
  `%LOCALAPPDATA%\SculptForge\updates`); portable: unzip beside the folder, rename the folder to `.old`, move the new one in.
- **Package managers** (Flatpak, pacman/AUR, .deb, .rpm, Homebrew, winget's portable layout) are never replaced:
  the banner names the command or says to download the new file.

## Rollback

- `update_pending.json` counts starts of the new version; a start that runs 5 seconds confirms it (the `.old` copy
  and the note go). After 3 unconfirmed starts the next start puts `.old` back and starts it.
- **Restart now** on Linux: the old app hides its window and waits up to 90 s; if the new process exits before it
  confirms, the old file is put back and the banner says the update didn't start.
- Windows portable: the helper restores the folder when the new version exits with an error within 20 s.
- Windows installer: the installer replaces files itself; if it fails (non-zero exit), the old version is left as
  the installer left it. No rollback beyond that (documented limitation; the installer is the same one users run).

## Limits (known, accepted)

- Same-user malware can already replace the app; the updater doesn't defend against it (the hash re-check before
  install narrows the window but cannot close it). The Windows installer runs elevated from a user-writable folder,
  as any downloaded installer does.
- The feed could hold back updates (offer nothing). It cannot make the app install anything unsigned or older.
- Key loss or leak: ship a release with a new key (old apps then fall back to the Download button for releases
  signed with a key they don't know).

## macOS (plan only)

The app is ad-hoc signed, so Gatekeeper treats each new bundle as new: replacing `/Applications/SculptForge.app`
in place would trip quarantine/translocation and could leave a bundle that won't open. Safe path, once there is a
Developer ID and notarization: download the `.dmg` (same chain), `hdiutil attach -nobrowse`, `ditto` the bundle to
`SculptForge.app.new` beside the old one, verify `codesign --verify --deep --strict` and the Team ID, swap with
`rename`, relaunch with `open -n`. Until then: Download (and `brew upgrade --cask sculptforge` for Homebrew).

## Insider early builds (design only; Tony decides)

Insiders get builds about two weeks early. Proposal: a third channel, `insider`, read from the feed's
`channels.insider`, whose packages sit in a private place (not public GitHub releases). The app would need a
membership check (a signed, expiring token from the membership portal, stored in the preferences) and a second
allowed host. That adds an account surface and a new host to the allowlist, so it needs Tony's OK before any code;
the signature chain above stays the same (same key, or a separate insider key).
