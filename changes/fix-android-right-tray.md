---
whats_new: **Trays on tablets**: the Right Tray and the Left Tray open with one tap on their edge handle on tablets, held either way up.
fixed: **Android tablets** (reported on 0.12.14-beta.2): the Right Tray never opened on a tablet window under 1280 logical px (a 2000x1200 screen at UI scale 2.62 is 763), and neither tray opened upright. A narrow window now holds one tray at a time (at most 40 % of its width); a tap on a tray's handle opens it (closing the other) and a second tap closes it. A timeline track narrower than its margins no longer loops forever. Wider windows and the mouse behave as before.
member_request: true
testers:
  - Android tablet (landscape and portrait): tap the right edge handle - the Right Tray opens; tap again - it closes. Tap the left edge handle - the Left Tray opens in its place. A stroke on the model still sculpts with a tray open.
---
Cause: computeShell forced the right tray to zero width under kNarrowWidth (1280 logical px) and the divider only showed a status line; under 640 px neither tray opened. Tests: tests/ui/hidpi/android_trays_{wide,narrow,portrait}.sfui, test_shell_layout.
