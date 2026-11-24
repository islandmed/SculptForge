---
whats_new: **Split view** (**View > Split view**, or <kbd>Shift</kbd>+<kbd>V</kbd>): a second live view of your scene beside or below the first, each with its own camera. Sculpt in one, watch the other follow every stroke; a view can hold the front, right or top side.
highlight: Split view: sculpt from the side, watch the front
added: **Split view** (View > Split view, Shift+V; #171, requested by members): a second live view of the same scene beside or
  below the first. Each view has its own camera: Free turns like any view, Front / Right / Top hold that side (orthographic;
  pan and zoom still work). The view under the pointer is the one you sculpt, orbit and frame in; the other updates with
  every stroke. Drag the divider to share the canvas; the chip at a view's top right switches its direction, swaps the
  views or goes back to one view. While a stroke runs or the view moves, the other view draws at a lower resolution
  (half on the Essential tier) and comes back sharp when you stop. Off by default; saved with the preferences.
testers:
  - Any OS: Shift+V; sculpt in the left view, the right (front) view follows each stroke. Move the pointer to the right view, sculpt there: the stroke lands under the pointer.
  - Any OS: View > Split view > Below; drag the divider; the chip at a view's top right: pick Top, Swap views, Single view.
  - Windows at 150% scaling, and a window resize: both views and the divider stay in place, no stripe between them.
  - A big mesh (5M+ triangles): sculpt with the split on; the other view goes softer while the stroke runs, then sharp.
---
Second-opinion critique (agy) reviewed; Grok was unavailable (402). Cameras are not saved, the layout is.
Not linked: a fixed view keeps its own pan and zoom (asked in the critique: linking jumps when perspective and orthographic mix).
