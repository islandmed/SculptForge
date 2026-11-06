# Publishing sculptforge to the AUR (maintainer, once)

What's ready: `PKGBUILD` (builds the release tag against Arch's SDL3 and curl, offline, `check()` runs the tests;
namcap-clean and built in a clean Arch container by `.github/workflows/arch.yml`) and its `.SRCINFO`. The name
`sculptforge` is free on the AUR (https://aur.archlinux.org/packages/sculptforge was 404 on 2026-11-06).

## Before: an AUR account (browser, a minute)

Register at https://aur.archlinux.org/register (any email you read; the AUR shows it only if you tick that), then **My Account > SSH
Public Key**: paste the public key of this laptop (`cat ~/.ssh/id_ed25519.pub`; a dedicated key is fine:
`ssh-keygen -t ed25519 -f ~/.ssh/aur` plus a `Host aur.archlinux.org` / `IdentityFile ~/.ssh/aur` /
`User aur` block in `~/.ssh/config`).

## The 4 commands (from ~/Work/Zbrush on main)

```sh
git clone ssh://aur@aur.archlinux.org/sculptforge.git ~/Work/.sf/aur-sculptforge
cp packaging/arch/PKGBUILD packaging/arch/.SRCINFO ~/Work/.sf/aur-sculptforge/
git -C ~/Work/.sf/aur-sculptforge add PKGBUILD .SRCINFO && git -C ~/Work/.sf/aur-sculptforge commit -m "sculptforge $(sed -n 's/^pkgver=//p' packaging/arch/PKGBUILD)"
git -C ~/Work/.sf/aur-sculptforge push origin master && touch ~/Work/.sf/aur-publish
```

The clone warns that the repository is empty: that's expected for a new package. The push creates the package
page; `yay -S sculptforge` works a minute later.

## After

`~/Work/.sf/aur-publish` turns on the AUR push in `scripts/release.sh` (step `aur`): each release it bumps the
PKGBUILD on main as before, regenerates `.SRCINFO`, and pushes both to the AUR as islandmed (noreply). Without the
file the step only bumps the PKGBUILD, as today. Step `check` then also confirms the AUR accepts this machine's key
before anything is tagged. To stop publishing: remove the file.

Then the coordinator updates the install docs (docs/guide/installing.md "Package managers", install/README.md,
the home page download note) from "ready, not published yet" to `yay -S sculptforge`, and replies on #149.

## Checking it locally

`packaging/arch/check.sh` repeats the CI check on this machine (aarch64): `.SRCINFO` current, namcap, a clean
chroot build. It needs `sudo pacman -S --needed devtools namcap` once and asks for sudo to create the chroot.
