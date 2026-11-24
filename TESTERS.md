# Testers: SpaceMouse that keeps moving after you let go (0.13.15)
For owners of a 3Dconnexion SpaceMouse, older models (SpaceExplorer, SpaceNavigator, SpacePilot) most of all, on Windows. No other OS needed unless you have one.
- **Edit > Preferences > Input > SpaceMouse**: push the cap, tilt it, twist it, then let go. Does the view stop at once? (Before: it kept moving at the last speed.)
- Hold a steady push for 5 to 10 seconds, as still as you can: the view must keep moving the whole time. If it stops mid-push, switch off **Stop when the cap stops reporting** and tell us your model.
- Push one way, then the other: the view reverses straight away, it does not just slow down.
- With 3DxWare running, and again with it quit: the same.
- If anything is odd: switch on **Log device reports** (it stays on this computer and is off unless you switch it on), repeat it for ten seconds, switch it off, then copy the lines from `spacemouse-log.txt` (the first 50 are enough; **Copy log** copies a short excerpt) and paste them into the issue as text. They hold only the device name, USB ids, axis values and raw reports: no paths, no user names, no file names.

# Testers: Android, New Texture on a Mali tablet or phone
The model vanished (or showed only its wireframe) after Texture Image > New Texture on a Mali-G72 tablet. Any Android device helps; Mali first.
- **Sphere**, 5-6 subdivisions (60k+ triangles); **Texture Image > Auto UV**; **New Texture** (2K); paint one stroke. Good: the sphere stays visible and the stroke shows.
- If the sphere shows without the texture, the status bar says the graphics chip rejected it. Either way, right after the stroke: **Help > Copy log to clipboard**, paste it to us. The lines with "GL error", "GL driver message" or "Texture not shown" say which call failed.
- Turn **Wire** on and off, and switch Vertex paint / Texture in **Paint into**: the model must never disappear.
- Tell us the device model, Android version and GPU (the log's "GL vendor" line has the GPU).

# Testers: My Panel, Alt+drag into every way of opening it (0.13.7)
Needs a real desktop with the panel in a window of its own (the automated tests only have a stand-in for the second window). Windows, macOS and Linux each, please.
- **Edit > My Panel...** opens it as a drop-down. Hold **Alt** and drag a shelf button (Wire, say) onto it: does the drop-down stay open, and does Wire appear in it? The status bar says "Added to My Panel".
- Press **F4** (My Panel at the cursor), Alt+drag a button from a shelf and a section's title from the Tool palette onto it: both land where the line shows.
- Click the small arrow button in My Panel's header (**Open in its own window**), and move that window beside the main one (a second monitor if you have one). Hold **Alt**, drag a shelf button from the main window, and let go over the My Panel window: is it added at the **end** of the panel, with "Added to My Panel" in the main window's status bar?
- Same drop, but let go over the main window instead: nothing is added. Drop it on the My Panel window after leaving it and coming back: still added.
- In the own window, Alt+drag an entry above another to reorder it; right-click one: **Remove from My Panel** works.
- **Pen tablets and pen displays**: repeat the own-window drop with the pen (hold Alt on the keyboard, or the pen button if you map it). Tell us the tablet model, and whether the pen tip lands the drop.
- Wayland (Linux): does the own window open and take the drop? If a drop over it does nothing, tell us your desktop and compositor.
- At 150% / 200% scaling, does the drop land on the window you released over (not one over, with two monitors at different scalings)?
