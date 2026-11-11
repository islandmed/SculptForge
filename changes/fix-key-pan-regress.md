---
whats_new: **Pan and zoom keys work again**: Pan left / right / up / down and Zoom in / out (on their own keys or rebound) move the view again; a tap nudges it, holding glides.
fixed: **Pan and zoom keys did nothing** (0.12.5: the keys' glide ran in the test runner's frame loop only, never in the app's own; reported by Artizmo on Discord). A tap now moves the view about a tenth of a second's worth, a hold glides, on the default and on rebound keys. Tests: `ui_key_nav_loop` drives the app's real loop (`--measure-key-nav`), `ui_key_nav` the rest.
testers:
  - Windows, Linux, macOS: tap Ctrl+Numpad 4 / 6 / 8 / 2 and Numpad + / -: the view moves a little each time; hold one: it glides, let go: it eases to a stop.
  - Preferences > Hotkeys: rebind Pan left to J, press J: it pans. Works in the Classic and SculptForge key styles.
---
Cause: 0.12.5 moved the pan / zoom step into updateKeyNav (once a frame), but only runUiTest's loop called it.
