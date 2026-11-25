# packaging

Everything that turns a build into something people install, and the manifests for each package manager. `cpack`
in a build folder makes the platform's own packages (`.deb`, `.rpm`, `.tar.gz`, `.dmg`, or the Windows installer
and `.zip`); the files here are what it and the release workflow add around them.

| Folder / file | What | Updated |
|---|---|---|
| `linux/` | The desktop entry and AppStream metadata, `build-appimage.sh` (AppImage and its `.zsync`), `check-page-size.sh` (an AppDir must start on 4, 16 and 64 KiB memory pages), `install-build-deps.sh` (build dependencies for Debian/Ubuntu and Fedora/RHEL) | by hand |
| `windows/` | Version resources and the manifest of `sculptforge.exe`, `sculptforge-cli.exe` and the Python host, and the installer's update-check script | by hand |
| `flatpak/` | The Flatpak manifest and `build.sh`; `flatpark/` is the small recipe for FlatPark, a community Flatpak remote that pins our GitHub download by URL, size and SHA-256 | the manifest by hand |
| `arch/` | The `PKGBUILD` for the AUR, built from a release tag against Arch's own SDL3 and curl; `check.sh` builds it in a clean container and runs namcap ([README](arch/README.md)) | each release |
| `rez/` | A [Rez](https://github.com/AcademySoftwareFoundation/rez) package for studio deployment | each release |
| `scoop/`, `winget/` | The Scoop manifest (also published in the `scoop-sculptforge` bucket) and the winget manifests | each release |
| `upstream.json` | The upstream versions SculptForge targets (OpenUSD and others), checked weekly by `.github/workflows/upstream-watch.yml` | when a tracked item is bumped |
| `release-notes-footer.md` | The text added under every GitHub release's notes | by hand |

The version numbers in the per-release files (`rez/package.py`, `scoop`, `winget`, `arch`) are rewritten by
`scripts/release.sh`: do not bump them in a feature pull request. How a release is built, signed and published is in
[docs/releasing.md](../docs/releasing.md); the one-line installers are in [install](../install/README.md).

## Building the packages yourself

After a normal [build](../docs/building.md):

```sh
sudo packaging/linux/install-build-deps.sh     # Linux only, before building
cd build && cpack                              # the platform's packages
packaging/flatpak/build.sh out/                # a Flatpak bundle (needs flatpak-builder)
```

## Adding a package manager

Add its manifest here, make `scripts/release.sh` update it as a step (so it can never fall behind), and add a CI job
that installs it from scratch (see `.github/workflows/arch.yml` and `flatpak.yml`). Builds must not need network access
beyond the pinned, checksummed downloads in `cmake/Fetch.cmake`.
