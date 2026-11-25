---
whats_new: **UI scale** (**Edit > Preferences > Interface**) now goes down to **70%** for 4K and 5K screens: a slider from 70% to 300%, text built sharp at every size, and the size remembered per monitor.
highlight: UI scale down to 70% for high-resolution screens
added: **UI scale below 100%** (**Edit > Preferences > Interface > UI scale**): the setting is a slider from **70%** to
  **300%** in 5% steps (double-click it to type a value), applied the moment you let it go, with an **Automatic** tick that
  follows the monitor. Text and icons are built again at the chosen size, so they stay sharp (no shrunk bitmaps), and
  below 100% buttons and sliders keep a little more room around their text than the text loses, so they stay easy to hit
  with a mouse or a pen. The size is remembered per monitor: a laptop screen and a 4K monitor each keep their own.
  Requested for a Retina display whose system scaling made the interface too large.
testers:
  - A 4K or Retina screen: Edit > Preferences > Interface > UI scale, drag to 70% and let go. Text, icons, shelves and trays shrink together, the text stays sharp, and every button, tick and slider still takes a click.
  - Double-click the slider, type 85, Enter: 85% applies at once. Type 40: it stays at 70%.
  - Two monitors: set 80% on one, drag the window to the other and tick Automatic, drag it back: the 80% returns.
  - Restart: the size is kept, on each monitor.
  - Windows at 150% system scale: tick Automatic, the interface is 150% as before; untick and choose 100% to get the smaller interface.
member_request: true
member_credit: Fenix
---
Fenix (member, consented to credit) asked in #ideas on 11-22 for a UI scale below 100% on a Retina Mac. The lower bound is
70%: the 10.5 px captions fall under 8 px below that. Padding below 100% shrinks by sqrt(scale) (UiScale.h compactPadScale);
the slider applies on release because rebuilding the fonts mid-drag rescales the tray, and the slider with it, under the
pointer. Per-monitor memory keys on the display's name + pixel size (SDL3 exposes no serial), 16 entries at most.
