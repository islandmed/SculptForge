---
whats_new: **Alt+click a Part** finds thin and far Parts: a click a few points off the Part counts, and a trackpad click that wobbles is still a click.
fixed: **Alt+click to select a Part** (also Triple-click and Alt+Q) often missed thin, small or distant Parts, and a trackpad click that moved 4 points began a stroke instead. The pick now takes a Part within 8 points of the pointer when it is in front of the Part being sculpted, and a click may move 6 points (reported by phoenix on macOS).
testers:
  - macOS trackpad, Classic layout: a scene with a thin Part (a wire or a ring) in front of the body; Alt(Option)+click it from the default view, no zooming in. It becomes the active Part, and the status bar says so.
  - Same, zoomed far out: the Part still selects. Alt+drag on the body still makes an inverted stroke.
---
Cause: pickPart cast one exact ray (a thin Part is a pixel or two wide, so it was missed unless zoomed close), and the
4 point click slop was easy to exceed on a trackpad (the press became an Alt stroke on the active Part). The Option
key path (SDL_KMOD_ALT) was checked: nothing macOS-specific, no change. New: pickPartAt (core/Parts.cpp), kPartClickSlop.
