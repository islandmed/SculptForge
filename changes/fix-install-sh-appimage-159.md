---
fixed: The one-line Linux installer (`curl -fsSL .../install/install.sh | sh`) downloads the AppImage again: it asked
  for `SculptForge-X.Y.Z-linux-x86_64.AppImage` (and `linux-arm64`) while releases name it
  `SculptForge-X.Y.Z-x86_64.AppImage` and `-aarch64.AppImage`, so it stopped with a 404. Checked against
  SHA256SUMS.txt as before. A test and a release check now fail when an install script asks for an asset the
  release doesn't have (#159, thanks @justblack1556).
testers:
  - Linux x86_64 (Arch or any distro): `curl -fsSL https://raw.githubusercontent.com/islandmed/SculptForge/main/install/install.sh | sh`;
    it downloads SculptForge-X.Y.Z-x86_64.AppImage, verifies it and `sculptforge` starts.
  - Linux arm64 (Asahi, Pi 5): the same command fetches the -aarch64.AppImage.
---
install/ is served from raw main: lane candidate, merge at once.
