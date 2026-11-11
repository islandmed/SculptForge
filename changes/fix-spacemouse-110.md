---
whats_new: **SpaceMouse** (macOS, Windows): a SpaceMouse that 3Dconnexion's driver was holding when SculptForge started should be picked up on its own once the driver lets go, no unplugging needed.
fixed: SpaceMouse (#110, thanks @fenixpoints): a SpaceMouse that SDL listed but could not open (3DxWare held it) was never tried again until it was replugged; SculptForge now retries every 3 seconds. Running alongside 3DxWare and the Wacom conflict are still open.
testers:
  - macOS with a SpaceMouse and 3DxWare installed (#testers): quit 3DxWare, start SculptForge, open Edit > Preferences > Input > SpaceMouse. Note the device line, and whether the live bars move when you push the cap.
  - Same Mac: start SculptForge with 3DxWare running, then quit 3DxWare (menu bar icon). Within a few seconds the SpaceMouse should appear in Preferences without replugging; push the cap and the view should move.
  - 3DxWare running next to SculptForge: if the cap shows but the live bars stay still, click Copy device report and paste it in issue #110 (include the macOS version and the 3DxWare version).
  - Wacom tablet plugged in with the SpaceMouse: does the pen still work, does the SpaceMouse still move the view, and does any axis drop out? Paste the device report in #110.
  - Change Speed or Dead zone, quit and reopen: the values are kept (Preferences save themselves).
---
Retry on a failed SDL_OpenJoystick (spaceMouseOpenFailed_, App::updateSpaceMouse). Settings persistence was already
automatic since 0.11.1 (#117); nothing to change there.
