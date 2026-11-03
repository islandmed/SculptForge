---
whats_new: **Close Holes**: **Tool > Geometry > Close Holes** caps every open boundary of a Part with a patch that
  follows the curve of the surface around it.
whats_new: **Clip**: **Tool > Cut > Clip** presses everything on one side of a line, or outside a lasso or rectangle,
  flat onto the shape. Nothing is removed. Use it for hard-surface planes and clean silhouettes.
whats_new: **X-Ray**: the **X-Ray** button next to **Solo** shows the active Part through the others. It is also in
  **View**.
whats_new: **Light**: **Render > Materials > Light** turns the light around the model with a small pad, and sets its
  intensity and ambient light.
whats_new: **View resolution**: **View** or **Preferences > Performance** draws the 3D view at 75% or 50% for slow
  GPUs.
whats_new: **Save Render** sizes (up to 4096 px), shapes (1:1, 16:9, 9:16, 4:3) and JPG.
highlight: Close Holes, Clip and X-Ray
added: **Close Holes** (Tool > Geometry, `close_holes`, #103): caps every open boundary of the active Part (a hole, an
  open bottom, the rim of an extracted shell) with new triangles sized like the ones around it. The patch is faired
  into the curvature around the hole, so a sphere closes round, not flat. The rest of the Part is not changed, and the
  patches get a face group of their own. `max_edges` closes only the small holes. Thanks to the reporter of #103.
added: **Clip** mode for Cut (Tool > Cut, `cut mode=clip`, #102): a line or open polyline presses its shaded side
  flat onto itself, and a lasso, rectangle or closed polyline presses everything outside it onto its outline. Points
  keep their depth and nothing is removed. The cut goes through the whole Part, follows symmetry, and the mask holds
  points back. Thanks to the reporter of #102.
added: **X-Ray** (`xray`, #80, #91): a button next to Solo in Tool > Parts, and a View checkbox. The other Parts turn
  see-through and only their nearest surface shows, so the active Part shows through them. When only the active Part is
  shown, it is see-through itself.
added: **Light controls** (Render > Materials > Light, `light`, #80): drag on the pad to turn the light of the Lit
  Sphere and the studio shading. Intensity and Ambient sliders sit beside it, and Reset goes back to the material's own
  light. The light is saved in the project.
added: **View resolution** (View and Preferences > Performance, `view_resolution`, #80): 100%, 75% or 50%. The 3D
  view is drawn smaller and scaled up, which helps slow or integrated GPUs. Save Render stays at full resolution.
added: **Save Render** sizes, shapes and JPG (Render > Materials, `save_render`, #80): twice the view (as before) or
  1080, 1920, 2048 or 4096 pixels on the longer side. Shapes: the view's, 1:1, 16:9, 9:16 or 4:3 (a narrower shape
  widens the field of view, so the picture still shows what the view shows). Output is PNG or JPG.
fixed: The Library's close button works on small screens at a large UI scale (#80). When the tabs don't fit beside it,
  they fold into a drop-down, and the Sculpt Timeline no longer covers the Library's header (720p at 150%, ZBrush
  layout).
testers:
  - Windows at 150% scaling on a 1366x768 or 1280x720 screen: press comma to open the Library, then click its X. It
    closes in both layouts (Preferences > Interface > Layout).
  - Tool > Parts with two Parts: click X-Ray. The other Part turns see-through and the active one shows through it.
    Solo plus X-Ray shows the lone Part see-through. Click X-Ray again to make it solid.
  - Render > Materials > Light: drag on the ball. The model's light follows the dot. Try Intensity, Ambient and Reset,
    with a Lit Sphere and with Studio.
  - View > View resolution 50% on a laptop GPU: orbiting a big mesh should feel faster and look a little softer.
    100% puts it back.
  - Render > Materials: pick 9:16 and JPG, then click Save Render. Library > Image has a tall JPG showing what the view
    shows.
  - Tool > Cut > Clip: Ctrl+Shift+drag a Cut Line across a sphere. One side goes flat and nothing is removed. Try it
    with a Cut Rectangle and with symmetry X on.
  - Hide part of a sphere (Ctrl+Shift+drag), then Visibility > Delete hidden, then Tool > Geometry > Close Holes. The
    holes close smoothly, and Ctrl+Z brings them back.
---
#102 Clip is a Cut mode (Ctrl+Shift picks Cut Line / Lasso / Rectangle / Polyline as before, Tool > Cut > Clip). The
tests are test_close_holes.cpp, the clip test in test_cut.cpp, tests/ui/view_controls.sfui, and the 150% part of
library_small_screen.sfui.
