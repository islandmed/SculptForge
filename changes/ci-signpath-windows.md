---
changed: **Windows files** (the command-line tool and the installer, as the app already did): Properties > Details shows the product
  name SculptForge and the release's version, which Windows code signing through SignPath Foundation requires.
testers:
  - Windows: right-click sculptforge-cli.exe and the SculptForge-*-windows installer > Properties > Details; Product name is SculptForge and Product version is this release.
---
Release builds check this in CI (tools/ci/check_pe_metadata.ps1); signing itself stays off until the owner turns on SIGNING.
