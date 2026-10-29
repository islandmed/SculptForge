# Install SculptForge

Pick your system and click the link. It always downloads the newest version.

| System | Download |
|---|---|
| **Windows 10 / 11** | [Installer (.exe)](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-windows-x86_64-setup.exe) · [portable .zip](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-windows-x86_64-portable.zip) |
| **macOS 11+** (Apple Silicon and Intel) | [SculptForge.dmg](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-macos.dmg) |
| **Ubuntu / Debian / Mint / Pop!_OS** | [.deb for PCs (x86_64)](https://github.com/islandmed/SculptForge/releases/latest/download/sculptforge_amd64.deb) · [.deb for ARM64](https://github.com/islandmed/SculptForge/releases/latest/download/sculptforge_arm64.deb) |
| **Fedora / RHEL / Rocky / Alma** | [.rpm for PCs (x86_64)](https://github.com/islandmed/SculptForge/releases/latest/download/sculptforge.x86_64.rpm) · [.rpm for ARM64](https://github.com/islandmed/SculptForge/releases/latest/download/sculptforge.aarch64.rpm) |
| **macOS (Homebrew)** | `brew install --cask islandmed/tap/sculptforge` |
| **Any Linux** | [AppImage for PCs (x86_64)](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-linux-x86_64.AppImage) · [AppImage for ARM64](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-linux-arm64.AppImage) |
| **Arch Linux** | `yay -S sculptforge` |

Not sure which Linux file? Most PCs are x86_64; ARM64 is for Raspberry Pi, Apple Silicon Macs running Linux
and ARM laptops.

## Then

- **Windows:** run the installer. If SmartScreen says "Windows protected your PC", click **More info** →
  **Run anyway** (the app isn't code-signed yet).
- **macOS:** open the .dmg and drag SculptForge into Applications. The first time, right-click the app and
  choose **Open**.
- **Ubuntu / Debian:** `sudo apt install ./sculptforge_amd64.deb` (or double-click the file).
- **Fedora / RHEL:** `sudo dnf install ./sculptforge.x86_64.rpm`
- **AppImage:** make it executable (`chmod +x SculptForge-*.AppImage`) and run it.

## Or install with one command

**macOS / Linux** (installs for your user, no admin password):

```sh
curl -fsSL https://raw.githubusercontent.com/islandmed/SculptForge/main/install/install.sh | sh
```

**Windows** (PowerShell):

```powershell
irm https://raw.githubusercontent.com/islandmed/SculptForge/main/install/install.ps1 | iex
```

Both download the latest release from GitHub and check it against the release's SHA-256 checksums before
installing. Run the same command again to update. To uninstall on macOS / Linux, add `-s -- --uninstall`
after `sh`; on Windows, use **Settings → Apps**.

Older versions, studio (VFX Reference Platform) builds and checksums are on the
[releases page](https://github.com/islandmed/SculptForge/releases).
