---
whats_new: **Fast Navigation** (Preferences > Performance) says when **Turbo** is what keeps the view light while it moves, with a button to turn Turbo off.
fixed: **Fast Navigation seemed to stay on after turning it off** (GitHub #184, thanks @HG4Science): View > Turbo, remembered on the computer, draws the lighter view
  (no antialiasing, 75% resolution) while the view moves, whatever Fast Navigation says. The Fast Navigation row now says so
  while Turbo is on, with a **Turn Turbo off** button; its help and the status line name Turbo.
testers:
  - Turn Turbo on (Ctrl+Alt+T), then Preferences > Performance: the Fast Navigation row says Turbo is on; Turn Turbo off puts the view back to full quality while orbiting.
  - Fast Navigation off, restart: it stays off.
---
Cause: Turbo (hardware profile override, saved in hardware_profile.json) and Fast Navigation (ui.json) are two switches; the view's
moving-frame check was `fastNavigation || turbo`, and the panel never showed Turbo. Setting, save/load and profile/tier changes were fine
(applyHardwareProfile never writes fastNavigation). Evidence: the reporter's log shows 1 sample at scale 0.75 on a 69K-triangle scene
while moving, which only Turbo does.
