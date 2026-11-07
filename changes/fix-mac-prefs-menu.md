---
whats_new: **macOS**: **SculptForge > Preferences...** in the app menu opens Preferences (it was greyed out).
fixed: macOS: **SculptForge > Preferences...** in the app menu was greyed out; it now opens Preferences, as Cmd+, and
  Edit > Preferences do (#111, thanks @fenixpoints).
testers:
  - macOS: open the SculptForge menu (next to the Apple menu). Preferences... is enabled; click it: Preferences opens once.
  - macOS: press Cmd+,: Preferences opens once, not twice (and Edit > Preferences, in the Classic layout, still opens it).
---
SDL builds the app menu with the item's action nil; src/app/MacAppMenu.mm gives it one after SDL_Init. The click
only sets a flag and wakes the loop; the next frame runs ui.palette.preferences. Cmd+, reaches the app through SDL
first and the menu after, so the item ignores its own key equivalent (tests/test_mac_menu.cpp).
