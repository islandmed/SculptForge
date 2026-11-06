---
whats_new: **Paint menu**: no longer jitters on short screens (1280x720 fullscreen); a menu taller than the screen scrolls smoothly.
fixed: The **Paint** menu jittered on a short screen (reported at 1280x720 fullscreen, thanks ebarranco): taller than the
  space under the menu bar, it scrolled, its colour picker shrank under the scrollbar, the menu stopped scrolling and
  grew again, every frame. A menu that scrolls now widens by its scrollbar instead.
testers:
  - Any OS, a 1280x720 (or smaller) screen, fullscreen (F11): open Paint; the menu holds still and scrolls with the wheel.
---
Test: tests/ui/menu_stable.sfui (also at --size 1280x720, ui_menu_stable_720): menu_rect_changes == 0 for Paint, Sculpt, Render.
