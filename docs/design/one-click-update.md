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

- **Linux AppImage**: download to `.download-<version>-<asset>` next to the AppImage (same filesystem), `chmod 755`,
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

## Member builds (src/core/MemberBuilds.*, src/core/Keychain.*)

Membership = read access to the private repository sculptforge/sculptforge-insiders (where betas are published). No
server of ours.

- **Sign-in**: GitHub's device flow for the GitHub App "SculptForge Member Builds" (client ID `Iv23liK7goArrLDUGKEk`,
  compiled in; public). The app is installed only on sculptforge-insiders with Contents: read (+ Metadata), so its
  user-to-server token reaches only repositories both the app and the user can access: that one, read-only. No client
  secret (device flow and its refresh need none); the app's private key exists only because GitHub requires one and
  is never used. POSTs go to exactly `github.com/login/device/code` and `github.com/login/oauth/access_token`, no
  redirects followed. The page the user opens is fixed (`github.com/login/device`); an answer naming another is refused.
- **Membership check**: after sign-in, `GET /repos/sculptforge/sculptforge-insiders/releases`: 200 = member; 403/404 =
  not a member, and the token is dropped, never stored.
- **Token storage**: OS keychain only (Windows Credential Manager `SculptForge/member-builds`; Secret Service via
  libsecret, loaded at run time; macOS Keychain to follow). No keychain: in memory until quit. Never in the
  preferences (they hold only "signed in" and the login name) or logs. Access tokens expire (8 h); the refresh token is
  single-use, so each refresh stores the new pair at once, under a mutex. A refused refresh or a 401 signs out.
- **Where the token goes**: only to the member API URLs (`api.github.com/repos/sculptforge/sculptforge-insiders/releases`,
  `.../releases/assets/<number>`, `api.github.com/user`), checked per hop; never to the asset host a redirect points at.
- **Same verification**: a member build's SHA256SUMS.txt, .sig and package are fetched as assets of its release; the
  same Ed25519 key, the same versioned file name, hash, size cap and install path as public releases. release.yml signs
  the betas' sums with the same step (tools/release/sign_sums.sh).
- **Sign out**: removes the keychain entry and forgets the tokens. (Revoking on GitHub needs the client secret, so the
  user can also revoke at github.com/settings/apps/authorizations.)
- **Network**: the member check runs only inside the update check (daily or Help > Check for updates) and only when
  signed in; sign-in only when clicked; UI tests use a stand-in code and send nothing.
