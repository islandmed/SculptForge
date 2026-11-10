---
whats_new: **Customize Shelves**: every button and value field can be moved with Ctrl+Alt+drag, including options that are greyed out and the Size, Intensity and Detail fields.
fixed: **Customize Shelves** (Ctrl+Alt+drag): buttons that don't apply right now (the Front option, Vertex / Texture paint without a texture) and the shelf's value fields (Size, Intensity, Detail) can be moved again. Reported by fenix on Discord and MasterFULL on Discord.
fixed: **Right shelf** with the console open: the view toggles fold into the View button only when the shelf really is too short, and unfold only for a taller shelf; the buttons grow back only for a taller shelf, so the shelf should hold still (reported by Hameron (FXiT); please say if it still moves).
testers:
  - macOS (Retina, console open at about 982 px tall): the right shelf stays still. Turn on View > Console, leave the mouse alone for 10 seconds.
  - Any OS: Customize Shelves, then Ctrl+Alt+drag Front (top shelf), the Size slider and a greyed button off the shelf and back on.
---
UiTests: tests/ui/shelf_drag_all.sfui drags every shelf action and widget; tests/ui/sizes/shelf_console_*.sfui sweep the UI scale with
the console open and require no shelf layout change without input (this did not fail on 0.12.0 offscreen on Linux: the jitter is not
reproduced there; the hysteresis is defensive, Fenix to confirm on macOS).
