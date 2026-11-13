---
whats_new: **Welcome on a phone**: held sideways, the welcome window fits the screen, keeps **Close** at its bottom and scrolls with a swipe. Android's Back button closes it (and the other cards that open by themselves).
fixed: **Android**: the welcome window on a phone held sideways ran off the bottom of the screen with **Close** below the fold, and a finger could only drag the window, not scroll it, so only the small X in its title bar closed it (Hameron on Discord). It is now a sheet inside the screen's safe area (camera cut-out, bars, gesture area) with a finger-sized **Close** pinned at the bottom, content that scrolls with a swipe (a swipe that starts on a button scrolls, it doesn't press it), and Back closes it, the membership card, What's New and the quick tour.
testers:
  - Android phone, portrait and landscape (UI scale 250-350 %): Help > Welcome; Close is whole at the bottom and one tap closes it; swipe the body up and down (start on a button: it must not press); Back closes it.
  - Android: with nothing open, Back still leaves the app as before.
---
Tests: tests/ui/hidpi/welcome_touch*.sfui (touch taps only, safe-area insets, Back; 1080x2400 / 2400x1080 at 300 %, 1440x3120 / 3120x1440 at 350 %),
welcome_touch_scroll.sfui (swipe scroll in landscape). New UI-test verbs: touch_tap, touch_tap_at, touch_drag_at, touch_tap_welcome_x, android_back, safe_area.
Before: the same scripts fail on 0.12.13 (landscape: Close 570-690 px below the visible window). Not verified on a device: SDL's
trap-the-Back-button hint is switched on only while something is open (PanelSupport.cpp), the key reaches the app as SDLK_AC_BACK; testers please confirm.
