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
| Any Linux, sandboxed | [Flatpak for x86_64](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-linux-x86_64.flatpak) or [ARM64](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-linux-arm64.flatpak) | `flatpak install --user ./SculptForge-linux-x86_64.flatpak` (see [Flatpak](#flatpak)) |
| Arch Linux | `packaging/arch/PKGBUILD` in the source | `makepkg -si` in that folder (an AUR package is planned) |
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

Every release also has a `SHA256SUMS.txt` file with the checksum of each download, so you can confirm a file
arrived intact.

Each package contains two programs: **SculptForge**, the desktop application, and **sculptforge-cli**, the
headless command-line tool for scripts, build farms and AI agents (see [Scripting and
automation](scripting.md)). On macOS the command-line tool sits inside the app bundle, at
`SculptForge.app/Contents/MacOS/sculptforge-cli`.

### Flatpak

The Flatpak runs SculptForge in Flatpak's sandbox and works on any distribution that has Flatpak (most do; see
[flatpak.org/setup](https://flatpak.org/setup/) if yours doesn't). It is a single file: download it, then

```sh
flatpak install --user ./SculptForge-linux-x86_64.flatpak     # or ...-linux-arm64.flatpak
flatpak run org.sculptforge.SculptForge
```

SculptForge then appears in your application menu. The first install also downloads the freedesktop runtime it
runs on (a few hundred MB, shared with other Flatpak apps) from Flathub; the file adds Flathub for that if it isn't
set up. Only the runtime comes from Flathub: SculptForge itself is not published there.

- **Updating**: SculptForge tells you when there is a new version, as the other packages do. Download the new
  `.flatpak` and install it the same way; your settings are kept. `flatpak update` updates the runtime but not
  a SculptForge installed from a file.
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
your library folder (**Documents/SculptForge**, with Projects, Tools, Brushes, Alphas, MatCaps, Images, Splats
and QuickSave (snapshots) sub-folders), which the Library shows as **User**.

The first start also asks what you are **Coming from**: **ZBrush**, **Blender**, **Maya** or **New to sculpting**.
Your answer picks the key style: **ZBrush** turns on the ZBrush-style keys, **Blender** and **Maya** their own
styles, and **New to sculpting** keeps the SculptForge keys (the default). Change it any time in
**Edit > Preferences > Input > Style** (see [Keyboard and mouse](shortcuts.md)); if you are updating from an earlier
version, your saved style is kept and the question isn't asked.

You can open a file directly from the command line, too: `sculptforge model.obj` (or a `.glb`, `.sfproj`,
splat capture or image). `sculptforge --fullscreen` starts filling the screen; `sculptforge --help` lists
every option.

The next chapter, [The interface](interface.md), explains what everything on screen does.

## Updating and uninstalling

SculptForge doesn't install updates by itself, but it tells you when there is one. Once a day at start-up it
downloads a small file from its website that names the newest version; if yours is older, a banner at the top
of the 3D view says what's new, with **What's new**, **Download**, **Later** and **Skip this version**. Nothing
about you or your computer is sent. **Help > Check for updates** asks right away, and **Edit > Preferences > Updates**
turns the daily check off. Builds from a package manager (such as the AUR package) leave updates to the package
manager and don't check.

To update, download the new release and install it over the old one. Your settings, brushes and library are
kept: they live in your user folders, not next to the program (see [Troubleshooting and FAQ](troubleshooting.md)
for where).

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
