---
whats_new: After **Restart to update** on Linux, the previous version quits instead of staying hidden in the background (it kept showing SculptForge on Discord). Discord shows the outline cube icon.
fixed: Linux AppImage: after Restart to update, the previous version hid its window and never quit, so it kept running
  in the background (and, with Show SculptForge on Discord on, kept "Playing SculptForge" on your profile after you
  closed SculptForge). It now quits as soon as the new version is up. Updating from 0.11.10 or earlier, the new
  version ends the previous one itself (thanks @ebarranco).
fixed: Show SculptForge on Discord: quitting clears it first and it never comes back during the teardown; a quit that
  stalls ends the process after 15 s; the status shows the outline cube icon instead of the old SF letters.
testers:
  - Linux AppImage from 0.11.10 with Show SculptForge on Discord on: Help > Check for updates, Restart to update.
    After the new version opens, System Monitor shows one SculptForge; close it: nothing left, Discord status gone.
  - The same with Discord off: still only one SculptForge after the update, none after closing it.
  - Any OS: Preferences > Integrations > Show SculptForge on Discord on; profile shows the orange outline cube. Close the
    window: the status goes within a few seconds.
---
Cause: tickSelfUpdate (the old version watching for the new one, with a 90 s deadline) ran only in drawUpdateBanner,
i.e. on drawn frames; the old version hides its window for the restart, gets no events and draws nothing, so it never
checked and never quit. Now also on the passes that draw nothing (App::run). For 0.11.0-0.11.10 (old code), the new version
SIGTERMs its parent once it has started, only when that is sculptforge from the same AppImage file, same user
(update::isOldAppImage). The Rich Presence image is a new, frozen URL (press/sculptforge-app-icon-cube-outline-1024.png):
the PNG must be live (site lane) before the app ships.
