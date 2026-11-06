# Arch Linux (AUR)

`PKGBUILD` builds SculptForge from a release tag against Arch's own SDL3 and curl, fully offline
(Dear ImGui and GLM are pinned sources with checksums).

## Status

Not on the AUR yet: `SUBMIT.md` is the one-time submission. After it, `scripts/release.sh` (step `aur`) bumps this
PKGBUILD and `.SRCINFO` every release and pushes them to the AUR. `check.sh` (and CI, `.github/workflows/arch.yml`)
builds it in a clean Arch environment and runs namcap.

Until then: `makepkg -si` in this folder. Once published: `yay -S sculptforge` (or any AUR helper).
