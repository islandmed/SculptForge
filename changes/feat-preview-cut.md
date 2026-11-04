---
whats_new: **Update channel**: **Preferences > Updates > Update channel** picks **Stable** (finished releases, the default) or **Preview (betas)**, which also offers the betas of the next version, a couple of weeks before it ships. A beta says so in the title bar and About ("0.11.0 Beta 1"). Problems in a beta go to the Discord's #bug-reports.
added: **Update channel** (Preferences > Updates): Stable or Preview. Preview is also offered the betas, which the update feed lists under `channels.preview`. It is never offered anything older than the stable release. Betas are GitHub pre-releases (`vX.Y.Z-beta.N`). They show "X.Y.Z Beta N" in the title bar and About.
changed: Version comparison in the update check: a prerelease sorts before its version (0.11.0-beta.1 < 0.11.0), and betas compare by number.
testers:
  - Any OS: Preferences > Updates shows Update channel: Stable selected; switch to Preview, restart, it is still Preview.
  - Any OS: on Preview, Help > Check for updates still says this version is the newest (no beta is out yet).
---
Release tooling for betas: scripts/release.sh X.Y.Z-beta.N, release.yml pre-releases, discord-announce.yml beta posts,
docs/releasing.md "Betas". The first beta is 0.11.0-beta.1, cut after stable 0.10.11.
