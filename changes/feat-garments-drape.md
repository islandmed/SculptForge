---
whats_new: **Drape a garment on a body**: in **Tool > Garments**, click the body once for each panel, press
  **Drape** and watch the pattern sew itself shut and settle, then **Convert to Part** to sculpt it (UVs from the flat
  pattern).
added: **Garments: drape on the body** (Tool > Garments > Drape on the body, `garment_place`, `garment_drape`): each
  panel placed by a click on the body (in front of it, facing the view; Turn, Wrap, Gap), saved with the pattern;
  the sewn pattern simulated live on the visible Parts (rest shape = the flat pattern, seams pulled shut).
added: **Garments: Convert to Part**: the draped garment as the Part "Garment": seams welded, UVs from the flat
  pattern, the pattern's thickness, one undo step.
testers:
  - Any OS: File > New from starter > Full Body, Tool > Garments > Templates > T-shirt, Place panels: click the
    chest (front view), the back (behind), each upper arm (from its side). Drape: the shirt closes and settles on
    the body; Convert to Part: a Part "Garment"; Ctrl+Z removes it.
  - Wrap a sleeve to 0 and drape: it still closes (more slowly); 360 makes a full tube.
  - Windows: drape the T-shirt on the Full Body; at rest within about 10 s.
---
Part 3 of docs/design/garments.md. Placement is by hand only (see "Do not build").
