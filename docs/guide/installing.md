# Install SculptForge on Windows, macOS and Linux

This chapter covers what SculptForge needs, which package to download for your system, what to expect the
first time you open it, and how to build it from source.

## System requirements

- **Operating system**: Windows 10 or 11 (x64), macOS 11 or later (Apple Silicon or Intel), or a 64-bit Linux
  distribution (x86_64 or ARM64) with X11 or Wayland.
- **Graphics**: a GPU and driver that support **OpenGL 3.3** (core profile). Any graphics card from the last
  ten years qualifies, including integrated Intel, AMD and Apple GPUs. SculptForge asks for 4x multisampling
  and falls back to none if the driver refuses it.
- **Input**: a three-button mouse works for everything. A pen tablet adds pressure (see [Sculpting and
  brushes](sculpting.md)).
- **Network**: not needed for sculpting. The free AI features (Brush Lab and image or text to 3D) and the
  community brush gallery need an internet connection unless you run models on your own machine.

> **Note:** SculptForge is a 64-bit desktop application. There is no web, iPad or Android version.

## Download

Each link below downloads the newest version. Pick the one for your system:

| System | Download | How to install |
|---|---|---|
| Windows 10/11 (x64) | [Installer (.exe)](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-windows-x86_64-setup.exe) | Run the installer (adds a Start menu shortcut) |
| Windows, portable | [Portable .zip](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-windows-x86_64-portable.zip) | Unzip anywhere and run `bin\sculptforge.exe` |
| macOS 11+ (Apple Silicon and Intel) | [SculptForge.dmg](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-macos.dmg) | Open the disk image and drag SculptForge to Applications |
| macOS with [Homebrew](https://brew.sh) | `brew install --cask islandmed/tap/sculptforge` | Updates with `brew upgrade` |
| Ubuntu, Debian, Mint, Pop!_OS | [.deb for x86_64](https://github.com/islandmed/SculptForge/releases/latest/download/sculptforge_amd64.deb) or [ARM64](https://github.com/islandmed/SculptForge/releases/latest/download/sculptforge_arm64.deb) | `sudo apt install ./sculptforge_amd64.deb` |
| Fedora, RHEL, Rocky, Alma 8+ | [.rpm for x86_64](https://github.com/islandmed/SculptForge/releases/latest/download/sculptforge.x86_64.rpm) or [ARM64](https://github.com/islandmed/SculptForge/releases/latest/download/sculptforge.aarch64.rpm) | `sudo dnf install ./sculptforge.x86_64.rpm` |
| Any Linux | [AppImage for x86_64](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-linux-x86_64.AppImage) or [ARM64](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-linux-arm64.AppImage) | `chmod +x` the file, then run it |
| Any Linux, sandboxed | [Flatpak for x86_64](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-linux-x86_64.flatpak) or [ARM64](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-linux-arm64.flatpak) | `flatpak install --user ./SculptForge-linux-x86_64.flatpak`, or install from FlatPark to get updates (see [Flatpak](#flatpak)) |
| Arch Linux | `packaging/arch/PKGBUILD` in the source | `makepkg -si` in that folder (the AUR package isn't published yet; see [Package managers](#package-managers)) |
| Studio pipelines | `SculptForge-<version>-...-vfx2026.tar.gz` or `...-vfx2027.tar.gz` | See [SculptForge for studios](studios.md) |

Most PCs are x86_64; ARM64 is for ARM laptops, Raspberry Pi and Apple Silicon Macs running Linux. Older
versions are on the [releases page](https://github.com/islandmed/SculptForge/releases).

### Install with one command

Instead of downloading by hand you can install (or update) with one command. On macOS and Linux it installs
for your user only, with no administrator password:

```sh
curl -fsSL https://raw.githubusercontent.com/islandmed/SculptForge/main/install/install.sh | sh
```

On Windows, in PowerShell (it downloads and starts the installer):

```powershell
irm https://raw.githubusercontent.com/islandmed/SculptForge/main/install/install.ps1 | iex
```

Both check the download against the release's checksums before installing. To uninstall on macOS or Linux,
run the first command with `sh -s -- --uninstall` at the end instead of `sh`.

### Package managers

Where SculptForge is in a package manager today, so it updates with the rest of your system:

| Package manager | Status | Command |
|---|---|---|
| Homebrew (macOS) | **Available** | `brew install --cask islandmed/tap/sculptforge`, then `brew upgrade` |
| winget (Windows) | **Submitted, in review** at winget-pkgs; not installable yet | Use the installer or the one-command install meanwhile |
| AUR (Arch Linux) | **Ready, not published yet** | Until then: `makepkg -si` in `packaging/arch` |
| Omarchy | **Submitted, in review** at omarchy-pkgs | As for Arch meanwhile |
| apt (Debian, Ubuntu) and dnf (Fedora, RHEL) repositories | **Not yet; being built** ([#149](https://github.com/islandmed/SculptForge/issues/149)) | Install the `.deb` or `.rpm` file; SculptForge tells you when a new version is out |
| FlatPark (Linux) | **Available** (x86_64, ARM64) | `flatpak --user remote-add --if-not-exists flatpark https://dl.flatpark.org/flatpark.flatpakrepo` then `flatpak --user install flatpark org.sculptforge.SculptForge` (see [Flatpak](#flatpak)) |

This table changes as each one goes live.

Every release also has a `SHA256SUMS.txt` file with the checksum of each download, so you can confirm a file
arrived intact.

Each package contains two programs: **SculptForge**, the desktop application, and **sculptforge-cli**, the
headless command-line tool for scripts, build farms and AI agents (see [Scripting and
automation](scripting.md)). On macOS the command-line tool sits inside the app bundle, at
`SculptForge.app/Contents/MacOS/sculptforge-cli`.

### Flatpak

The Flatpak runs SculptForge in Flatpak's sandbox and works on any distribution that has Flatpak (most do; see
[flatpak.org/setup](https://flatpak.org/setup/) if yours doesn't). The easiest way is from
[FlatPark](https://flatpark.org/), a community Flatpak repository that carries SculptForge's official Linux build
unmodified (x86_64 and ARM64):

```sh
flatpak --user remote-add --if-not-exists flatpark https://dl.flatpark.org/flatpark.flatpakrepo
flatpak --user install flatpark org.sculptforge.SculptForge
flatpak run org.sculptforge.SculptForge
```

Or install the single file from the release: download it, then
`flatpak install --user ./SculptForge-linux-x86_64.flatpak` (or `...-linux-arm64.flatpak`).

SculptForge then appears in your application menu. The first install also downloads the freedesktop runtime it
runs on (a few hundred MB, shared with other Flatpak apps) from Flathub; both ways add Flathub for that if it
isn't set up. SculptForge itself is not on Flathub.

- **Updating**: from FlatPark, new versions arrive with `flatpak update` (or your software center's updates),
  usually within a day of each release. SculptForge's own new-version notice still says to download the file;
  with FlatPark you can ignore that. Installed from the file, download the new `.flatpak` and install it the
  same way. Your settings are kept either way.
- **Uninstalling**: `flatpak uninstall org.sculptforge.SculptForge` (add `--delete-data` to remove its
  settings too).
- **Files**: the sandbox can read and write your home folder, so projects, models and the textures beside them
  open as they would without it. Files elsewhere (another drive under `/mnt` or `/media`) open through the file
  dialog; to let SculptForge see a whole drive, run
  `flatpak override --user --filesystem=/mnt/data org.sculptforge.SculptForge`.
- **Settings** live in `~/.var/app/org.sculptforge.SculptForge/`, not `~/.local/share/SculptForge`, so a
  Flatpak and another install of SculptForge on the same computer keep separate settings and brushes. Your
  library in Documents/SculptForge is shared.
- **Pen tablets and the SpaceMouse** work in the sandbox.
- **Blender bridge**: the add-on finds the Flatpak by itself and exchanges models through the usual folder.
- **Not in the sandbox**: programs SculptForge starts on your computer, such as `ffmpeg` for recording demo
  videos and the splat trainer (**Splat > Train**), are not visible to it. Use another package for those.
  Ollama, ComfyUI and other local AI servers work, because SculptForge talks to them over the network.

To build the Flatpak yourself from the source, install `flatpak` and `flatpak-builder` and run
`packaging/flatpak/build.sh`; it writes `SculptForge-<version>-<arch>.flatpak`.

## Windows

Windows is where most SculptForge artists work. The notes below cover the first start, pen tablets and screen
scaling.

### SmartScreen

The builds are not code-signed yet, so the first start may show "Windows protected your PC".

1. Click **More info**.
2. Click **Run anyway**.

To confirm the file is the one we published, compare it with `SHA256SUMS.txt` on the release page.

### Pen tablets: Windows Ink and Wintab

SculptForge reads the pen through **Windows Ink**, the pen input built into Windows 10 and 11. When Windows Ink is
off in the tablet driver, it takes the pressure from the driver's **Wintab** instead. Either way works; Windows Ink
also gives the eraser end and tilt.

- **Wacom**: Wacom Tablet Properties > **Mapping** > **Use Windows Ink**. You can set it for SculptForge only by
  adding it as an application in Wacom Tablet Properties.
- **Huion, XP-Pen, Gaomon, Ugee**: **Windows Ink** (sometimes "Windows Ink mode") in the driver's settings.
- **Pen displays and Surface-type pens** use Windows Ink already.

Check it in **Edit > Preferences > Tablet**: it shows "Pressure: on" once the pen has pressed, or "Pressure: not
detected". A tablet plugged in while SculptForge runs works straight away. If pressure is missing, switch the
driver's Windows Ink option and bring the pen back to the tablet.

**Mouse mode.** Most drivers map the pen to the screen (pen mode): where the pen touches is where the cursor goes.
If you set the driver to **Mouse** (relative) mode, the pen moves the cursor by how far it moves, like a mouse.
Turn on **Edit > Preferences > Tablet > Mouse mode (relative)** to match it, or the cursor jumps when you put the
pen down.

Pen side buttons work as the driver sets them; a button set to **Right click** opens the
[Quick Menu](sculpting.md#the-quick-menu). More in [Troubleshooting: pen tablets](troubleshooting.md#pen-tablets).

### Screen scaling (DPI)

SculptForge follows the **Scale** in Windows' **Settings > System > Display**, so text and buttons have the
same size as in other apps at 125%, 150% or 200%. To make the interface larger or smaller than that, choose a
size in **Edit > Preferences > Interface > UI scale** (**Automatic** follows Windows).

### Where files go

- Settings, brushes and the log: `%APPDATA%\SculptForge\SculptForge` (**Help > Open log folder** opens it).
- Your library (projects, snapshots, alphas, Lit Spheres): `Documents\SculptForge`.

See [Where settings live](troubleshooting.md#where-settings-live) for every file.

## Opening SculptForge for the first time

The builds are not code-signed yet, so your operating system may warn you the first time.

### macOS: Gatekeeper

macOS may say that SculptForge "cannot be opened because the developer cannot be verified".

1. In Finder, open the Applications folder.
2. Right-click (or <kbd>Ctrl</kbd>+click) **SculptForge** and choose **Open**.
3. Click **Open** in the dialog that appears.

You only need to do this once. On recent macOS versions you may instead have to go to **System Settings >
Privacy & Security** and click **Open Anyway** next to the message about SculptForge.

### Windows: SmartScreen

Windows may show "Windows protected your PC". See [Windows](#windows) below.

### Linux

The `.deb` and `.rpm` packages add SculptForge to your desktop's application menu. An AppImage needs to be
made executable first (`chmod +x SculptForge-*.AppImage`, or tick "Allow executing file as program" in your
file manager's properties).

### Wayland and X11

Every Linux package (.deb, .rpm, AppImage, Flatpak, tar.gz) opens as a native Wayland window on a Wayland
desktop, without XWayland, and as a normal X11 window on an X11 session. On Wayland the window stays sharp at
fractional and HiDPI scales (125%, 150%, 200%), and title bars come from the desktop (or from libdecor on GNOME).

To force X11 (for example, a screen recorder or remote-desktop tool that only captures X11 windows), start
SculptForge with:

```sh
SDL_VIDEO_DRIVER=x11 sculptforge
```

Pen tablets work on both. On Wayland, pressure, tilt, the eraser end and side buttons come through the
desktop's tablet protocol (tablet-v2), so the tablet must be set up in your desktop's settings rather than
through X11 tools such as `xsetwacom`. If pressure doesn't respond, check **Edit > Preferences > Tablet** for the
live pressure reading.

## What you see at start-up

SculptForge opens a 1600 x 960 window with a sphere ready to sculpt and the **Library** open along the top of
the canvas on its **Project** tab, so you can reopen recent work with a double-click. Press <kbd>,</kbd>
(comma) to close it, or turn off **Edit > Preferences > Library > Show at startup**. The first start also creates
your library folder (**Documents/SculptForge**, with Projects, Tools, Brushes, Alphas, Lit Spheres, Images, Splats
and QuickSave (snapshots) sub-folders), which the Library shows as **User**.

The first start also asks what you are **Coming from**: **Classic sculpting keys**, **Blender**, **Maya** or **New to sculpting**.
Your answer picks the key style: **Classic sculpting keys** turns on the Classic sculpting keys, **Blender** and **Maya** their own
styles, and **New to sculpting** keeps the SculptForge keys (the default). Change it any time in
**Edit > Preferences > Input > Style** (see [Keyboard and mouse](shortcuts.md)); if you are updating from an earlier
version, your saved style is kept and the question isn't asked.

You can open a file directly from the command line, too: `sculptforge model.obj` (or a `.glb`, `.sfproj`,
splat capture or image). `sculptforge --fullscreen` starts filling the screen; `sculptforge --help` lists
every option.

The next chapter, [The interface](interface.md), explains what everything on screen does.

## Updating and uninstalling

SculptForge tells you when there is a new version. Once a day at start-up it downloads a small file from its
website that names the newest version; if yours is older, a banner at the top of the 3D view says what's new, with
**What's new**, **Download**, **Later** and **Skip this version**. Nothing about you or your computer is sent.
**Help > Check for updates** asks right away, and **Edit > Preferences > Updates** turns the daily check off (and
picks the update channel: **Stable**, or **Preview** for the betas).

**One-click update.** The Linux AppImage and the Windows installer and portable zip also get an **Update** button
on the banner. It downloads the new version in the background (with a progress bar and **Cancel**), checks it, and
then asks: **Restart now** installs it and starts the new version; **Later** installs it when you quit, so the
next start is the new version. If you have unsaved changes, Restart now asks first, as quitting does.

What the check means: the files come only from SculptForge's own GitHub releases, over a checked secure
connection; the release's list of checksums must carry the SculptForge release signature (a key built into the
app); and the download must match its checksum. If any of that fails, nothing is changed and nothing downloaded
is run: the banner says what went wrong and **Download** still opens the release page. The current version is
kept beside the new one (the AppImage as `<name>.AppImage.old`, the portable folder as `<folder>.old`) until the
new version has started once, then removed; if the new version doesn't start, the old one is put back. The
Windows installer asks for permission to change Program Files, as installing does.

**Member builds.** Members get each release early. In **Edit > Preferences > Updates > Member builds**, click
**Sign in with GitHub**: SculptForge shows a short code; open **github.com/login/device**, sign in with the GitHub
account that accepted the invitation to the member repository, and enter the code. From then on the update check
also offers the member builds, and **Update** installs them exactly like releases, with the same signature check.
The sign-in can only read the member repository's releases (nothing else on your GitHub account), it is kept in your
system's keychain (Windows Credential Manager, or the Secret Service on Linux such as GNOME Keyring or KWallet; never
in SculptForge's settings), and **Sign out** removes it. Without a keychain the sign-in lasts until you quit.

Installs that belong to a package manager are never replaced by the app: the Arch PKGBUILD (and the AUR and
Omarchy packages once they are published) builds without the update check, a Homebrew install shows
`brew upgrade --cask sculptforge`, and the Flatpak, `.deb` and `.rpm` files say to download the new file and open
it (a Flatpak from FlatPark updates with `flatpak update`). The macOS app shows **Download** for now.

To update by hand, download the new release and install it over the old one. Either way your settings, brushes
and library are kept: they live in your user folders, not next to the program (see
[Troubleshooting and FAQ](troubleshooting.md) for where).

To uninstall, use your system's usual method (Settings > Apps on Windows, dragging the app to the Bin on
macOS, `sudo apt remove sculptforge` or `sudo dnf remove sculptforge` on Linux, `flatpak uninstall
org.sculptforge.SculptForge` for the Flatpak, or delete the AppImage).
Delete the settings and library folders as well if you want to remove everything.

## Building from source

You need CMake 3.24 or later and a C++20 compiler: GCC 11+, Clang 14+, Xcode 15+ or Visual Studio 2022. Dear
ImGui, GLM and (if it isn't installed already) SDL3 are downloaded automatically. AI features use libcurl on
Linux and macOS and the built-in WinHTTP on Windows.

```sh
git clone https://github.com/islandmed/SculptForge.git
cd SculptForge

# Linux only: compilers and X11/Wayland/GL/curl headers (Debian/Ubuntu or Fedora/RHEL)
sudo ./packaging/linux/install-build-deps.sh

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/sculptforge
```

On Windows the program is `build\Release\sculptforge.exe`; on macOS run `open build/SculptForge.app`.

Useful CMake options:

| Option | Default | What it does |
|---|---|---|
| `SCULPTFORGE_BUILD_APP` | `ON` | Build the desktop application (turn off for a headless build of `sculptforge-cli` only) |
| `SCULPTFORGE_BUILD_PYTHON` | `OFF` | Build the Python module (`import sculptforge`); needs Python development headers |
| `SCULPTFORGE_BUILD_TESTS` | `ON` | Build the unit and UI tests |
| `SCULPTFORGE_USE_SYSTEM_SDL` | `ON` | Use an installed SDL3 if one is found |

Running `cpack` in the build folder makes the platform's packages (`.deb`, `.rpm` and `.tar.gz` on Linux, a
`.dmg` on macOS, an installer and a `.zip` on Windows).

## See also

- [The interface](interface.md)
- [Troubleshooting and FAQ](troubleshooting.md)
- [SculptForge for studios](studios.md)
