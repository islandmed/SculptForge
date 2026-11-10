# apt and dnf repositories (#149)

Status: built and tested, **off until the maintainer creates the signing key** (`~/Work/.sf/repo-key.sh`, below). Until then nothing changes
for users: releases carry no repository metadata and the site has no /apt or /rpm.

## What users get

```sh
# Debian, Ubuntu, Mint, Pop!_OS (amd64, arm64)
sudo curl -fsSL -o /usr/share/keyrings/sculptforge.asc https://sculptforge.app/sculptforge.asc
echo "deb [signed-by=/usr/share/keyrings/sculptforge.asc] https://sculptforge.app/apt stable main" |
  sudo tee /etc/apt/sources.list.d/sculptforge.list
sudo apt update && sudo apt install sculptforge

# Fedora, RHEL, Rocky, Alma 9+ (x86_64, aarch64)
sudo curl -fsSL -o /etc/yum.repos.d/sculptforge.repo https://sculptforge.app/rpm/sculptforge.repo
sudo dnf install sculptforge      # asks once to trust the key: compare its fingerprint with the guide's
```

The apt key goes in `/usr/share/keyrings/` (not `/etc/apt/keyrings/`) so a later `.deb` can ship that same file
and renew it on every upgrade (see "Expiry" below).

Then `apt upgrade` / `dnf upgrade` bring each release. The packages are the release's own `.deb` and `.rpm`
files, byte for byte (the ones the release workflow verified and smoke-tested).

## How it works

1. **Release workflow** (`release.yml`, job publish, environment `release`): `tools/publish/package_repos.sh` builds
   the metadata for the release's versioned `.deb` and `.rpm` and signs it: apt `InRelease` (clearsigned) and
   `Release.gpg`, dnf `repodata/repomd.xml.asc`. It uploads only the metadata, `sculptforge-package-repos.tar.gz`, as a
   release asset, covered by `SHA256SUMS.txt` and its signature like every other asset. Without the
   `REPO_SIGNING_KEY` secret the step says so and does nothing.
2. **Website** (`pages.yml`): `tools/publish/package_repos_fetch.sh` takes the latest published release's metadata
   and downloads the packages it names into `website/apt/pool/...` and `website/rpm/packages/`, each checked against
   that release's `SHA256SUMS.txt`, whose release signature (`SHA256SUMS.txt.sig`, the app's one-click update key) is
   verified first, then deploys with the rest of the site. Nothing is committed to git (about
   120 MB per release); only the newest version is in the repositories (older ones stay on the releases page).
   The downloads retry for minutes; if GitHub's release downloads stay down (504s for hours on 11-09), `pages.yml`
   puts the site together from the assets the last good fetch kept in the Actions cache (`PACKAGE_REPOS_KEEP`, one
   cache per release tag, saved from main only) through the same script, so the signature and every checksum are
   checked again, and a site-only change still deploys. With no kept copy the deploy fails rather than drop /apt and /rpm.
   Tradeoff: while the downloads fail, a deploy publishes the previous release's apt and dnf repositories, so apt and dnf
   users stay one release behind until the next deploy that fetches fresh (the run says so in a warning).
3. **Trust chain**: the key signs the metadata; the metadata lists each package's SHA-256; apt and dnf refuse a
   package whose bytes differ (`.github/workflows/repos.yml` tests exactly that, plus installs on Debian, Ubuntu,
   Fedora and Rocky 9 with a throwaway key). The `.rpm` files themselves are not signed, because signing would
   change the tested bytes: the repo file sets `repo_gpgcheck=1` (signed metadata, which carries every package's
   checksum) and `gpgcheck=0`. A later step can sign the `.rpm` in `packages.yml` if a distribution policy needs it.

The key lives only in the `release` environment (tags `v*` only), reaches one step, and is never written to disk
outside that step's temporary keyring (the passphrase goes to gpg through a 0600 file, never a command line). The tools are installed in an earlier step that never sees it. pages.yml
and everything else never see it.

## The signing key (maintainer: once, about 2 minutes)

Not the app's update key (`UPDATE_SIGNING_KEY`): a separate OpenPGP key used only for these repositories.

- **Shape**: an **offline primary** key (RSA 4096, certify only, no expiry) and a **signing subkey** (RSA 4096,
  2 years). RSA works with every apt, including `sqv` on Debian 13+, and with dnf/rpm on RHEL 9.
- **Identity**: `SculptForge packages <232726012+islandmed@users.noreply.github.com>` (no personal name or address).
- **Made by** `~/Work/.sf/repo-key.sh`, run by the maintainer in their own terminal (it asks for a passphrase), in a
  temporary keyring in RAM (`/dev/shm`), never `~/.gnupg`. It keeps in `~/Work/.sf/secrets/repo-key/` (chmod 700):
  the full secret key **encrypted with that passphrase** (which lives only in the password manager, so nothing on
  the laptop, agents included, can use the primary), gpg's revocation certificate, the public key and its
  fingerprint. It sets one secret, `REPO_SIGNING_KEY`, in the **`release` environment only**: the signing subkey
  with the primary as a stub, unprotected (the environment secret is its protection; no passphrase secret).
  `--print` prints it to paste instead of setting it with gh.
- **The public key** is committed as `website/sculptforge.asc` (sculptforge.app/sculptforge.asc); pages.yml refuses
  repository metadata signed by any other primary key and copies the committed key to `/apt` and `/rpm`.

**The coordinator then**: commits `~/Work/.sf/secrets/repo-key/sculptforge.asc` as `website/sculptforge.asc` (site
lane); from the next release on the repositories go live by themselves. Check that
https://sculptforge.app/apt/dists/stable/InRelease verifies against it, add the snippets above and the fingerprint
to docs/guide/installing.md ("Package managers": apt and dnf **Available**), install/README.md and the home page
note, and close #149 (credit @NicTanghe).

## Expiry

`scripts/release.sh` (step check) stops a release once the subkey has less than 120 days left. Then the maintainer
runs `~/Work/.sf/repo-key.sh --extend` (2 more years, same keys, the secret replaced) and the coordinator commits
the new `website/sculptforge.asc`. Users' installed copies keep the old date, so **before the first extension
(by 2028-07)** the `.deb` must ship `/usr/share/keyrings/sculptforge.asc` and the `.rpm` the key for dnf, so every
upgrade renews them (app lane, its own branch; the key file exists only once the key is made).

## Rotation and compromise

- **Suspected leak** (a secret exposed, a stolen laptop with the password-manager copy unlocked): move `~/Work/.sf/secrets/repo-key` aside, make a new key
  exactly as above (it replaces the secret), release at once, and announce in the release notes, #announcements and
  #149 that apt users re-run the `curl ... sculptforge.asc` line (until the packages ship the key) (dnf asks to import the new key by itself on the
  next upgrade). Publish the old key's revocation certificate on docs/guide/security.md.
- **Planned rotation** (optional, e.g. every 5 years or a stronger algorithm): the same steps without the revocation;
  announce a release ahead.
- **Who can read the secret**: maintainers with admin on the repository, through a workflow run in the `release`
  environment from a `v*` tag. Keep that environment's deployment rule at tags `v*` only.

## Not in scope here

- The app's update banner for a `.deb`/`.rpm` install still says to download the new file; once the repositories
  are live it can say `sudo apt upgrade` / `sudo dnf upgrade` (small app change, its own branch).
- Flathub, winget and the AUR are separate (docs/guide/installing.md, "Package managers").
