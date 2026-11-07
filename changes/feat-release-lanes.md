---
added: **Credits update between releases**: Help > About's supporters and Founding Members (and the artist quotes)
  now come from the SculptForge website with the daily update check, so new names show up without a new version.
  A small signed text file from the same site as the update check; nothing about you is sent. Off in
  **Preferences > Updates > Fetch credits and community content**, in studio/offline mode and with `SF_CONTENT=off`;
  the list the version shipped with stays the fallback.
security: The credits file is checked with an Ed25519 signature under a key of its own (never the release key),
  capped at 64 KiB, never older than the copy already kept, and read as plain text by the same strict parser as the
  built-in list.
testers:
  - Any OS: Help > About lists the supporters and Founding Members; Preferences > Updates has the new switch.
---
