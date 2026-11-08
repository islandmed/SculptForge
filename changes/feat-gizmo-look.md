---
whats_new: **A cleaner gizmo**: in **All** (Shift+W) each axis is a thin line with its scale ball and its move arrow,
  well inside the rings, and rings seen edge-on fade away from the centre. Move and Scale use the same arrows and
  balls. Design by fenix on Discord (@fenixpoints).
whats_new: **Esc** (or picking a brush) now leaves Move, Scale, Rotate and All, so the next stroke sculpts again.
changed: **Gizmo look** (fenix on Discord's design, @fenixpoints): All (Shift+W) is cleaner and easier to read. Each
  axis is a thin line with its scale ball part way out and its move arrow at the end, with clear space before the
  rings (the scale boxes past the rings are gone); rings seen edge-on and their back halves fade so they don't pile
  up over the centre; the plane squares are lighter; the centre, view ring and even-scale tabs are light grey
  (charcoal on a light flat background) so the yellow highlight stands out on every handle. Move and Scale use the
  same arrows and balls, and every handle has a dark edge so it reads on light backgrounds too. Keys, handles and
  behaviour are unchanged.
fixed: **Leaving the gizmo**: Esc, or picking a brush, now leaves Move, Scale, Rotate and All; before, strokes kept
  driving the gizmo until Draw (Q) was picked.
testers:
  - Any OS, 100% and 200% UI scale: Shift+W on a sphere; turn the view; every handle (arrow, ball, square, ring,
    white view ring, outer tabs, centre) lights up yellow under the cursor and drags as before.
  - Edit > Preferences > Display, Flat Colour, a light colour: the gizmo's white parts turn charcoal and stay readable.
  - W, then Esc: back to Draw; Shift+W, then pick Clay on the shelf: back to sculpting with Clay.
---
Look pass on the 0.11.16 All gizmo. ga::pick priorities unchanged in kind (balls win on themselves as the boxes did).
