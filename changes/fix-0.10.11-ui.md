---
whats_new: **High-DPI laptops**: on a Windows laptop at 150-200 % scaling the window opens inside the screen at a
  size the interface fits, and nothing runs past its edges.
whats_new: **Right tray in every look**: Forge Terminal, Phosphor and Blueprint open the right tray on 1280 px screens,
  as Classic does.
whats_new: **QuickMenu**: square thumbnails and the look's own colours.
fixed: Windows at 150-200 % display scaling (#105, a 2560x1600 laptop): the window opened at its 100 % size, 800-1067
  logical px wide, and the shelves and status bar ran past its right edge. The window now opens inside the screen's
  usable area at the display's scale (maximised when that doesn't fit), its title bar on screen, and keeps its
  logical size when the scale changes; the status bar drops its readouts before they reach the menus.
fixed: The right tray stayed closed and wouldn't open on screens under 1920 px with Forge Terminal, Phosphor or
  Blueprint (#107, thanks ebarranco): the tiled looks' gaps made a 1280 px window count as too narrow for a tray.
fixed: The QuickMenu (right click) squashed its brush, alpha and material thumbnails and had a blue tint in the
  Neutral look (thanks ThinkingPolygons): the thumbnails are square and uncropped, the background the look's grey.
testers:
  - Windows laptop at 150 % or 200 % scaling (e.g. 2560x1600): start SculptForge; the window is inside the screen,
    the left shelf, the right shelf and tray, and the status bar's search and close buttons all show.
  - Any OS, a 1280x720 or 1366x768 screen: Preferences > Interface > Look > Forge Terminal, Phosphor, Blueprint; the
    right tray is open and its divider closes and opens it.
  - Right click on the canvas in the default look (Neutral): the brush, stroke, alpha and material thumbnails are
    round spheres, not ovals; the menu is grey, no blue.
---
UI tests: windows_dpi (2560x1600 and 2160x1350, at 1280 and 1440 logical px; offscreen_items == 0), right_tray_looks
(1280x720), right_click_menu (quickmenu-neutral shot). Unit tests: shell_tiled_look_not_narrow_at_1280,
window_fits_display.
