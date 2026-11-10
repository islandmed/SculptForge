---
fixed: **Stencil** in the SculptForge layout: Sculpt > Alpha > Stencil shows Adjust and Tile again, and the Remove (X) button
  (they sat past the panel's right edge, so only the Classic layout had them). Thanks MasterFULL on Discord.
testers:
  - In the SculptForge layout: load an image as the Stencil (Sculpt > Alpha > Stencil > Load image...);
    Adjust sits right of Show and Tile right of Invert; both work as in the Classic layout.
---
src/app/StencilOverlay.cpp: the second column and the Remove button were placed with SameLine(x) from GetCursorPosX(), which
counts the Sculpt menu's group (beside its section list) twice; ui::SameLineAt (Ui.cpp) places them. Same line in PanelFill.cpp.
UI test tests/ui/alpha_layouts.sfui: the Alpha section lists the same
controls in both layouts (remember_items / expect_same_items), at 1440 and 2880.
