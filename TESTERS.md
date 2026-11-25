# Testers: the end of a big stroke on a dense Part (Windows first)
A big stroke now ends right after its last dab; that dab's shading catches up over the next few frames. Needs a real window.
- Open a dense Part (a few million triangles or more: Lucy, a scan, or a sphere divided until the status bar shows 10M+).
- **Clay** and **Standard** at about 240 px: a stroke with varied pressure, release, and watch where it ended (close up, light from the
  side, Cavity on). Good: the surface lands and stays put; no flicker, pop or blotch as the shading settles.
- Release and start the next stroke at once, over the end of the first. Good: it starts without a hitch; no seam where they meet.
- Right after a release, move the pointer: the brush ring stays on the surface and moves with your hand (it must not flip flat or lag).
- Press **Ctrl+Z** right after a release: the stroke is undone, no frozen frame.
- Tell us the GPU and how many triangles the Part has.

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
