# apt and dnf repositories (#149)

Status: built and tested, **off until the maintainer creates the signing key** (below). Until then nothing changes
for users: releases carry no repository metadata and the site has no /apt or /rpm.

## What users get

```sh
# Debian, Ubuntu, Mint, Pop!_OS (amd64, arm64)
sudo install -d -m 0755 /etc/apt/keyrings
curl -fsSL https://sculptforge.app/apt/sculptforge.asc | sudo tee /etc/apt/keyrings/sculptforge.asc >/dev/null
echo "deb [signed-by=/etc/apt/keyrings/sculptforge.asc] https://sculptforge.app/apt stable main" |
  sudo tee /etc/apt/sources.list.d/sculptforge.list
sudo apt update && sudo apt install sculptforge

# Fedora, RHEL, Rocky, Alma 9+ (x86_64, aarch64)
sudo curl -fsSL -o /etc/yum.repos.d/sculptforge.repo https://sculptforge.app/rpm/sculptforge.repo
sudo dnf install sculptforge      # asks once to trust the key: compare its fingerprint with the guide's
```

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
3. **Trust chain**: the key signs the metadata; the metadata lists each package's SHA-256; apt and dnf refuse a
   package whose bytes differ (`.github/workflows/repos.yml` tests exactly that, plus installs on Debian, Ubuntu,
   Fedora and Rocky 9 with a throwaway key). The `.rpm` files themselves are not signed, because signing would
   change the tested bytes: the repo file sets `repo_gpgcheck=1` (signed metadata, which carries every package's
   checksum) and `gpgcheck=0`. A later step can sign the `.rpm` in `packages.yml` if a distribution policy needs it.

The key lives only in the `release` environment (tags `v*` only), reaches one step, and is never written to disk
outside that step's temporary keyring (the passphrase goes to gpg through a 0600 file, never a command line). The tools are installed in an earlier step that never sees it. pages.yml
and everything else never see it.

## The signing key (maintainer: once, about 5 minutes)

Not the app's update key (`UPDATE_SIGNING_KEY`): a separate OpenPGP key used only for these repositories.

- **Type**: OpenPGP, RSA 4096, sign-only primary key, no subkeys (works with every apt, including `sqv` on Debian
  13+, and with dnf/rpm on RHEL 9).
- **Identity**: `SculptForge packages <232726012+islandmed@users.noreply.github.com>` (no personal name or address).
- **Expiry**: none. apt and dnf keep the copy users installed and never refresh it, so an expiring key silently
  stops every user's updates on the expiry date. Rotation (below) handles compromise instead.
- **Where it is made**: on the laptop, in a temporary keyring in RAM (`/dev/shm`), never in `~/.gnupg`.
- **Where the private part lives**: GitHub Actions secrets `REPO_SIGNING_KEY` (armored private key) and
  `REPO_SIGNING_PASSPHRASE`, in the **`release` environment only**: not repository secrets, not `member-release`.
  One offline copy in the password manager: the armored private key, the passphrase and the revocation
  certificate gpg writes (`openpgp-revocs.d/<fingerprint>.rev`).

```sh
export GNUPGHOME=$(mktemp -d -p /dev/shm)
gpg --quick-gen-key "SculptForge packages <232726012+islandmed@users.noreply.github.com>" rsa4096 sign never
#   (pick a long passphrase when asked; it goes in the password manager and the second secret)
gpg --armor --export-secret-keys | gh secret set REPO_SIGNING_KEY --env release -R islandmed/SculptForge
gh secret set REPO_SIGNING_PASSPHRASE --env release -R islandmed/SculptForge    # paste the passphrase
gpg --armor --export-secret-keys; cat "$GNUPGHOME"/openpgp-revocs.d/*.rev       # both into the password manager
gpg --fingerprint                                                                # send the fingerprint to the coordinator
rm -rf "$GNUPGHOME"; unset GNUPGHOME
```

From the next release on, the repositories go live by themselves.

**The coordinator then**: checks that https://sculptforge.app/apt/dists/stable/InRelease is signed by that
fingerprint (`gpg --show-keys` on `/apt/sculptforge.asc`), adds the snippets above and the fingerprint to
docs/guide/installing.md ("Package managers": apt and dnf **Available**), install/README.md and the home page note,
and replies on #149 (credit @NicTanghe).

## Rotation and compromise

- **Suspected leak** (a secret exposed, a stolen laptop with the password-manager copy unlocked): make a new key
  exactly as above, replace both secrets, release at once, and announce in the release notes, #announcements and
  #149 that apt users re-run the `curl ... sculptforge.asc` line (dnf asks to import the new key by itself on the
  next upgrade). Publish the old key's revocation certificate on docs/guide/security.md.
- **Planned rotation** (optional, e.g. every 5 years or a stronger algorithm): the same steps without the revocation;
  announce a release ahead.
- **Who can read the secret**: maintainers with admin on the repository, through a workflow run in the `release`
  environment from a `v*` tag. Keep that environment's deployment rule at tags `v*` only.

## Not in scope here

- The app's update banner for a `.deb`/`.rpm` install still says to download the new file; once the repositories
  are live it can say `sudo apt upgrade` / `sudo dnf upgrade` (small app change, its own branch).
- Flathub, winget and the AUR are separate (docs/guide/installing.md, "Package managers").
