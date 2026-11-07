---
whats_new: **Pen view speed**: **Preferences > Tablet > View speed** sets how fast the pen turns, pans and zooms the
  view, apart from the mouse. Turn it down if the view spins too fast with a tablet. Thanks to GonzaloHernandez3D on Reddit.
added: **View speed** (Preferences > Tablet): how far the pen turns, pans and zooms the view per pixel, 0.10 to 2.00
  (1.00, the default, moves it as the mouse does). A tablet spreads its whole area over the screen, so a short pen
  movement crosses many pixels and orbiting seemed far faster than with the mouse (Huion on Windows; thanks to
  GonzaloHernandez3D on Reddit). Saved in the preferences; Tablet defaults sets it back to 1.00.
fixed: Pen taps on menus and buttons work the first time, also a tap within one frame or right after the pen comes into
  range, with Windows Ink and with Huion's extra mouse events (reported by GonzaloHernandez3D on Reddit; fixed by the
  pen click rework in 0.10.9, now covered by a test).
testers:
  - Windows with a Huion or XP-Pen tablet: tap File, View, Help and a brush with the pen, each once: each opens or
    picks the first time, also right after bringing the pen in from out of range.
  - Same tablet: drag on the background with the pen tip, then Preferences > Tablet > View speed 0.40 and drag again:
    the view turns less than half as far; the mouse turns it as before.
  - Windows with a real Huion or Wacom tablet: orbiting with the pen feels right at View speed 1.00, and lower values
    slow it (0.50 about half as far per stroke).
---
Root causes: the missed taps were SDL 3.2's stuck pen-mouse state and the driver's unreleased presses (#95, #13),
fixed on main after the report (10-31 to 11-02); tests/ui/pen_quick_taps.sfui replays the report with one-frame taps
(new `pen_tap` verb). Orbit: SDL 3.2.30 gives pen and mouse in the same window pixels (per-monitor DPI aware, no
relative mode), and the test proves the same drag turns the view by the same angle with either; the difference is the
tablet's absolute mapping, hence the setting.
Also: Preferences > Tablet's Troubleshooting button moves below "Pressure: ..." when it doesn't fit beside it (it was
cut off at 200% UI scale); tests/ui/tablet_view_speed.sfui checks with `past_right:Tablet` (new query: items of a
pushScope panel past their window's inner right edge) at 100% and 200%, 1440x900 and 1024x768.
