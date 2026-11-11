# SculptForge studio kit

Your own SculptForge build, made on your machines: your brushes, keymaps, macros, plugins, preferences and logo,
on top of an official signed release. We never see your files, settings or pipeline code, and nothing here calls
out to us: the release files come from a folder you fill, and the build runs on your CI or an air-gapped box.

Copy this folder into a repository of your own. It needs Python 3.9 or newer and `openssl` 3 (Git for Windows
has one). No packages to install.

```
studio-kit/
  build.py                   the whole kit: check, pin, build, feed, source
  sculptforge-release.pem    the public key every SculptForge release is signed with
  example/                   an overlay to start from (copy it, rename it, fill it)
```

## 1. Your overlay

```
overlay/
  studio.json            your studio: id, name, update channel, what the build may reach
  brushes/*.sfbrush      brushes (Brush Lab saves them; right-click a brush > Save .sfbrush...)
  alphas/*.png|jpg       brush alphas
  litspheres/*.png|jpg   Lit Spheres
  materials/*.sfmaterial Material Recipes
  vdms/*.sfvdm           vector displacement brushes
  keymaps/*.json         keymaps (Preferences > Hotkeys lists the action ids)
  piemenus/*.json        pie menus (Preferences > Input > Radial Menus > Export)
  macros/*.sfmacro       your scripts, in the command language of sculptforge-cli
  plugins/*.sfplugin     sandboxed WebAssembly plugins
  preferences/ui.json    first-start layout, shortcuts and UI scale (Edit > Preferences > Interface > Save layout)
  branding/logo.png      your logo, shown in About and on the start screen
  LICENSE.txt            optional: the terms for your files
```

`studio.json` is the studio profile without the parts the kit fills in (`packs`, `preferences`, `branding.logo`):

```json
{
  "format": "sculptforge-studio", "version": 1,
  "id": "acme", "name": "Acme Animation",
  "updates": {"feed": "https://sw.acme.internal/sculptforge/latest.json", "mirror": "/mnt/sw/sculptforge/latest.json"},
  "allow": [],
  "localNetwork": false,
  "ai": false,
  "brushPaths": ["/studio/brushes"],
  "branding": {"title": "Acme"}
}
```

Every key is in the user guide's "Your studio build". A studio build is always offline: it reaches only your update
channel and the URLs in `allow`.

Brushes, keymaps, macros and plugins go into one pack (`packs/<id>-studio.sfpack`), checked at start-up exactly as
any pack is. Keymaps are offered under Preferences > Hotkeys > Apply keymap; macros show in the Macros list and run
when an artist runs them, never on their own; plugins run only on the mesh they are given, with no file or network
access.

```sh
python3 build.py check overlay      # the overlay's mistakes, before anything is built
```

## 2. Pin a release

Download a release's files (on any connected machine, or from your mirror) into one folder. Take
`SHA256SUMS.txt`, `SHA256SUMS.txt.sig`, `SculptForge-<version>-sbom.cdx.json` and the archives you deploy:

| Platform | File |
|---|---|
| Linux x86_64 | `SculptForge-<version>-linux-x86_64.tar.gz` (or `-vfx2026` / `-vfx2027` for the VFX Reference Platform) |
| Linux ARM64 | `SculptForge-<version>-linux-arm64.tar.gz` |
| Windows | `SculptForge-<version>-windows-x86_64.zip` |
| macOS | the `.dmg` as it is, plus the studio folder (below) |

```sh
python3 build.py pin --release releases/0.12.6          # checks the signature and every file, writes pin.json
git add pin.json && git commit -m "SculptForge 0.12.6"
```

`pin` refuses files whose signature or checksum doesn't match. To check the key yourself: its Ed25519 public key is
`f6f8f8e9e02123a759a974bffaa07db8ebaa0c70ba746a0a86c644eba6a68642`, the same one the app checks its own updates with.

## 3. Build

```sh
python3 build.py build --overlay overlay --release releases/0.12.6 --pin pin.json --out dist
```

For each pinned archive, `dist/` gets your build (`SculptForge-<version>-<id>-<platform>.tar.gz` or `.zip`), with
your folder at `share/sculptforge/studio/` inside it, and a CycloneDX SBOM beside it: our release's components, the
release archive's hash and every one of your files with its hash. `SculptForge-<version>-<id>-studio-folder.zip` is
the folder on its own. `SHA256SUMS.txt` lists everything.

The program itself is unchanged and still carries our signatures, so nothing is recompiled. It finds the folder
beside itself when it starts and runs in studio mode.

**Reproducible:** the same overlay, release and pin give byte-identical files on any machine with the same Python
version (archive entries in a fixed order, fixed times, no owners). Set `SOURCE_DATE_EPOCH` to choose the time
stamped on your files; otherwise it is the release's own.

**macOS:** install SculptForge.app and put the `studio` folder from the studio-folder zip beside it (for example
`/Applications/SculptForge/SculptForge.app` and `/Applications/SculptForge/studio/`), so the app's signature
stays intact. Any platform can do the same with an install you already deploy.

## 4. Updates

The build checks only your channel. To move to a new release:

1. Put the new release's files in a new folder and run `build.py pin` (review the diff of `pin.json`).
2. Run `build.py build` and deploy `dist/` the way you deploy anything else.
3. Tell your artists' copies about it: `python3 build.py feed --version 0.12.7 --download https://sw.acme.internal/sculptforge/ --out latest.json`
   and put `latest.json` where `updates.feed` (or `updates.mirror`) points.

A patch release (`0.12.7` after `0.12.6`) is the same three steps. Your overlay doesn't change between them.

## Building from source instead

```sh
git clone --branch v0.12.6 https://github.com/islandmed/SculptForge.git sf
python3 build.py source --overlay overlay --src sf --out dist
```

This compiles SculptForge with your profile built in (CMake, a C++20 compiler; see the repository's README for the
dependencies) and packages it for the platform you build on. The binaries are then yours to sign.

## Bug reports

**Help > Create bug report pack...** writes a `.zip` on the artist's computer for a bug report, without the model:
the version, GPU and driver, changed settings, the end of the log, the last commands, mesh counts and a generated
stand-in mesh with the same counts. No model, texture or image goes in; paths, file names, links, e-mail addresses and the names of your Parts, layers, project, packs and account are replaced.
Every file is shown before saving and nothing is sent. Read the log and actions before posting publicly: other names
typed into free text can remain.
