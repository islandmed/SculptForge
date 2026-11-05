---
whats_new: **Studio mode** for studio builds: no outside connections, updates only from the studio's own channel.
added: **Studio mode**: `--studio studio.json` (or a studio build) starts SculptForge with no outside connections:
  no public update check, AI services, community gallery or Discord presence, and network sockets only on this
  computer. Only the studio's own update channel or mirror is reached. `--offline` (or `SF_OFFLINE=1`) does the same
  with nothing allowed. See the user guide's "Offline deployment".
changed: **Studio membership**: $300 a month for up to 10 artists (+$25 per extra artist; Founding Studio $150 for
  the first 10 studios), now with the studio's own isolated build, a pinned version with security fixes, an offline
  signed installer and up to 3 custom items a month.
testers: Start with `--offline`: Help > About says studio mode, Help > Check for updates says there is no update
  channel, AI features are off and locked, and Preferences has no Discord option. Windows first.
---
Studio builds: tools/studio/ (profile check, private-repo workflow template), docs/releasing.md "Studio builds".
