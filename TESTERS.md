# Testers: Android Mali, the sphere vanishing (0.13.14 follow-up)
For the member who reported it (Mali-G72, buffer-texture limit 65536) and any Mali phone or tablet; a Xiaomi that painted fine is not the case to repeat.
- **Sphere**, 6 subdivisions (about 70k triangles, past 65k); the sphere is visible at once. **Texture Image > Auto UV > New Texture** (2K); **Paint into: Texture**; paint a stroke. Good: the sphere stays visible and the stroke shows.
- Switch **Wire** and the PBR view on and off, add a Paint layer: the model never disappears.
- Micro levels (a later build): **Sphere** at 6 subdivisions, **Tool > Geometry > Micro** (Add Micro, level 2), a few sculpt strokes, turn the model: it stays visible and the strokes show.
- Then **Help > Copy log to clipboard** and paste it to us; "GL error" or "GL driver message" lines mean it still fails (tell us the device and Android version).

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
