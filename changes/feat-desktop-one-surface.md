---
whats_new: **One surface**: a calm top bar that is also the window's title bar (rooms, the brush, undo and redo, a stats chip), flat panels in Forge Night's colours, and more tools on a right click. **Edit > Preferences > Interface > Look > Classic look** switches back in one click. Idea by polyflow on Discord.
highlight: One calm surface: the top bar is the title bar
added: **The one-surface look** (the default with the SculptForge layout; idea by polyflow on Discord, "Alternate UI"):
  the top bar is also the window's caption (drag to move, double-click to maximise, edges resize; minimise, maximise
  and close in the bar), with the menus button, the project and its saved state, the rooms Sculpt, Paint, UV and
  Model, the brush with its size and strength, undo, redo, search and a stats chip whose card holds triangles,
  memory, frame time and the slowest frame, Parts, undo steps, the version and the performance tier (pin it to keep
  it open). Bars, shelves and trays are one flat colour with a rounded viewport, in Forge Night's colours (shared
  with the touch layout). A right click on the model adds Select Part, Hide and Isolate face group, Frame, Mask,
  Face Groups, Remesh and Search all tools to the Quick Menu. What just happened shows as a short line over the bottom
  of the canvas instead of in the bar, and Tool > Parts keeps the core tools up front, the rest (Linked Duplicate, Merge,
  Pose, Split, Extract, Project) behind **More Part tools**; Search (F3) finds them all. **Classic look** (Edit > Preferences > Interface > Look)
  brings back the status bar, tiles and system title bar; shelves, trays, menus and keys are unchanged in both.
  **System title bar** keeps the system's own above it; tiling desktops and macOS always do.
testers:
  - Windows 10 and 11: drag the empty top bar, double-click it (maximise once, again restores), drag a maximised window down off the bar (it restores under the pointer), Win+Arrow keys, drag to a screen edge (snap), resize from every edge and corner, minimise / maximise / close buttons. Close asks to save.
  - Windows multi-monitor with 100 % and 150 % or 200 % scaling: move the window between them by the bar; the bar's buttons still click where they are drawn, resizing still works.
  - Windows DPI 100 / 150 / 200: nothing in the bar overlaps; a narrow window drops room names before anything collides.
  - GNOME on Wayland and on X11: one title bar only (ours), drag, double-click maximise, edge resize, close.
  - Hyprland / Sway / i3: no window buttons in the bar, no extra decorations, everything else the same.
  - macOS: the system title bar with the traffic lights stays above the bar; nothing else changes.
  - Any OS, an existing install: the first start shows "New look: switch back any time" once; Classic look brings back the old bars with the same shelves and keys; restart keeps the choice.
  - Pen users: tap the room tabs, brush chip and stats chip with the pen; they click, the window does not move.
---
Reviewers: src/app/OneSurface.cpp holds the bar, caption hit test, stats card, corners, toast and Quick Menu rows;
Theme.cpp applyOneSurface the flat tokens; ForgeTokens.h is the token header shared with feat/touch-shell (byte for byte).
UI tests start in the Classic look (deterministic config); tests/ui/one_surface.sfui and sizes/one_surface.sfui turn it on.
