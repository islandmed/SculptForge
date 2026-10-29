
---

### Downloads

New here? **[install/](https://github.com/islandmed/SculptForge/tree/main/install#install-sculptforge)** explains
which file to pick, or install with one command:
`curl -fsSL https://raw.githubusercontent.com/islandmed/SculptForge/main/install/install.sh | sh` (macOS / Linux),
`irm https://raw.githubusercontent.com/islandmed/SculptForge/main/install/install.ps1 | iex` (Windows PowerShell).
Each file is also attached without its version number (e.g. `SculptForge-macos.dmg`) so links to
`releases/latest/download/...` always get the newest build.

| Platform | File |
|---|---|
| Windows 10/11 (x64) | `SculptForge-…-windows-x86_64.exe` (installer) or `.zip` (portable) |
| macOS 11+ (Apple Silicon and Intel) | `SculptForge-…-darwin-universal.dmg` |
| Ubuntu / Debian / Mint / Pop!_OS | `sculptforge_…_amd64.deb` or `…_arm64.deb` |
| Fedora / RHEL / Rocky / Alma 8+ | `sculptforge-…x86_64.rpm` or `…aarch64.rpm` |
| Studio pipelines ([VFX Reference Platform](https://vfxplatform.com)) | `SculptForge-…-linux-…-vfx2026.tar.gz` (GCC 14.2, glibc 2.28) or `…-vfx2027.tar.gz` (glibc 2.34), with the Python 3.13 module |
| Any Linux | `SculptForge-…-x86_64.AppImage` or `…-aarch64.AppImage` (they update themselves with AppImageUpdate) |
| Arch Linux | `yay -S sculptforge` (AUR) |

The builds are not code-signed yet. **macOS:** right-click the app → **Open** the first time.
**Windows:** if SmartScreen appears, choose **More info → Run anyway**. Checksums are in `SHA256SUMS.txt`.
