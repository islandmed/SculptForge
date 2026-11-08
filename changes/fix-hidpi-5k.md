---
whats_new: **Sharper text on Retina and 5K screens**: text and icons are drawn at the display's full resolution, and
  **Preferences > Interface > UI scale** has finer steps (**110%**, **115%**, **135%**) and goes to **250%** and
  **300%** for large high-resolution monitors.
fixed: Text and icons were soft on Retina and other 2x displays (5K monitors, scaled Wayland outputs): the
  font atlas was rasterised at 1x and stretched. It now follows the window's pixel density, and moving the window
  between a 1x and a 2x monitor rebuilds it.
added: **Finer UI scale steps** (Preferences > Interface): 110%, 115% and 135% between 100% and 150%, and 250% and
  300% for 5K and 5K2K monitors run at their native resolution. Thanks @fenixpoints for the suggestion.
testers:
  - macOS on a Retina or 5K display: text, menu icons and shelf labels look as sharp as other apps' text.
  - macOS with a Retina laptop and a 1x external monitor: drag the window across; the text stays sharp on both.
  - Any OS: Preferences > Interface > UI scale > 110%, 115%, 135%: each a small step, text sharp.
  - Any OS on a 4K/5K monitor: Preferences > Interface > UI scale > 250% and 300%; panels, brush tiles and the
    Library grow evenly, nothing is cut off.
---
UI test: tests/ui/sizes/hidpi_5k.sfui (ctest ui_hidpi_5k, 5120x2880). Unit: tests/test_ui_scale.cpp.
