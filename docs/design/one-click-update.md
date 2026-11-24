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

## macOS

The same chain (feed, fixed GitHub URLs, signed `SHA256SUMS.txt`, versioned name `SculptForge-<v>-darwin-universal.dmg`,
hash), then a fourth check before the swap:

- **Where**: the bundle the running executable is in (`<...>/SculptForge.app/Contents/MacOS/sculptforge`), wherever the
  user keeps it. Not self-updated: a Homebrew cask (`brew upgrade --cask sculptforge`), a bundle Gatekeeper runs from a
  read-only translocation copy (`/AppTranslocation/`), a build folder. The `.dmg` is downloaded to
  `~/Library/Caches/SculptForge/updates/`, never into `/Applications`.
- **Open, copy** (`installMacApp`): the staged image's hash is checked again, then `/usr/bin/hdiutil attach -nobrowse
  -readonly -noautoopen -mountpoint <our folder>` (read-only, nothing opened or run from it; our images have no licence
  prompt and the child's stdin is /dev/null), the image must hold exactly one `.app`, named `SculptForge.app`;
  `/usr/bin/ditto` copies it to `SculptForge.app.new` beside the old bundle (same volume: the swap is a rename); detach.
- **Code signature**: `/usr/bin/codesign --verify --deep --strict SculptForge.app.new` must exit 0, and the Security
  framework (`SecStaticCodeCheckValidity` strict + nested code + all architectures) must agree; its team identifier must
  equal the running app's own (`SecCodeCopySelf`, read at run time). Neither identifier is stored, hard-coded or shown.
  Today's builds are ad-hoc signed (`cmake/MacSign.cmake`, no `SCULPTFORGE_CODESIGN_IDENTITY` in CI): both sides have no
  team identifier, so this step proves the bundle's integrity (every file matches its signature) but not who made it;
  the signed checksum list carries that. A future Developer ID build makes the team check bite: an app with a team
  identifier refuses a download with another one or none.
- **Swap**: `update_pending.json`, then `renamex_np(new, target, RENAME_SWAP)` (APFS: one atomic step; the target path
  is always a whole bundle), the old bundle becomes `SculptForge.app.old`. Elsewhere two renames, the second undone on
  failure. No privileges are asked for: a folder this user can't write to is an error and nothing changes.
- **Relaunch, rollback**: as Linux. Restart now starts `Contents/MacOS/sculptforge` of the new bundle and watches it;
  the new version confirms after 5 s (the `.old` bundle goes), else the old one is put back and the `.failed` bundle is
  removed so Finder never shows it. Later installs at quit.
- **Dry run**: `SF_UPDATE_DRY_RUN=1` does everything up to and including the signature checks, then removes the copy
  and the download; the status line says what would have happened. Linux/Windows: the same variable skips the install.
- **Quarantine**: the app's own HTTP client sets no `com.apple.quarantine` attribute (as `curl -O` doesn't), so the
  copied bundle opens without Gatekeeper's first-launch prompt; what it opens is exactly the bytes the signed list names.

Tests: `tests/test_self_update.cpp` (`self_update_mac_*`): the tools stood in for on every platform (order of the calls,
every refusal leaves Applications as it was, the download gone, the image detached), and on a macOS runner a real
ad-hoc signed probe bundle through `macBundleSignature` (passes; fails once a file is changed).

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
