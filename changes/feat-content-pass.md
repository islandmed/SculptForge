---
whats_new: **Brush packs**: sixty new built-in brushes in six packs (Clay & Form, Hard Surface, Organic & Skin,
  Cloth & Folds, Damage & Wear, Ornament), each with its own alpha and a one-line description; see
  [Brush packs](brush-packs.md).
whats_new: **A-Z everywhere**: brushes, alphas, materials and the Library list A-Z; **Sort** switches to the most
  recently used first or (brushes, alphas) by pack. Alphas get a filter field.
whats_new: **Masking panel**: **Tool > Masking** is one panel of buttons with pictures: the Shapes, **Mask by**
  (cavity, peaks, face group, colour, facing, border) and **Adjust** (grow, shrink, sharpen, blur, invert, clear,
  all).
highlight: Sixty new brushes in six built-in packs
highlight: Mask by peaks, colour and facing in a new Masking panel
added: **Brush packs**: 60 built-in brushes in six packs (Clay & Form, Hard Surface, Organic & Skin, Cloth & Folds,
  Damage & Wear, Ornament), made from SculptForge's own recipe engine and procedural alphas. A pack brush picked the
  first time brings its alpha (and turns it along the stroke where that helps). Guide chapter
  [Brush packs](docs/guide/brush-packs.md).
added: Six new procedural alphas: **Cobblestone**, **Leather Grain**, **Hex Plates**, **Knit**, **Bolt** and
  **Chips** (21 built-in alphas in all).
added: **Sort** menu on the brush grid and list, the Alpha palette, the Material picker and the Library: A-Z (the
  default), Recent, and Pack (brushes, alphas). Saved in the preferences. The Alpha palette gets a filter field.
added: **Mask by peaks**, **Mask by colour** and **Mask by facing** (`mask_by_peaks`, `mask_by_color`,
  `mask_by_facing`), and `brush_use name=...` to pick a library or pack brush from scripts and macros.
added: Brush recipes (.sfbrush, brush codes) may name a built-in `alpha` and `alphaFollow`; older files are
  unchanged.
changed: **Tool > Masking** is one panel of buttons with pictures and a hint each: Shapes, Mask by and Adjust,
  with their options under **Mask by options**.
changed: Brushes, alphas, materials and Library files list A-Z by default (alphas and materials were in a fixed
  order; Library projects were newest first, which is still one click away under **Sort: Recent**).
testers:
  - Brush palette: the grid shows Built-in, Custom, then Brush packs, A-Z. **Sort: A-Z > Pack** shows one section
    per pack. Pick **Rivet Row** and stroke across the model: a row of rivets along the stroke.
  - Pick **Seam Stitch** on a sphere and drag a curve: the stitches follow the curve.
  - Alpha palette: A-Z with Round first; type "st" in the filter: only Cobblestone, Rock Strata and Stitches stay.
  - Tool > Masking: hold Ctrl over each button for its hint; **By facing** masks the front of a sphere from the front
    view; paint a red patch, set the colour to red and **By colour** masks just the patch.
  - Windows at 150% scaling: the Masking buttons' pictures and labels fit, nothing overlaps.
---
Content pass from the 11-03 reviewer video ("missing a ton of stuff", "build these brushes myself", "not much
masking", "alphas not alphabetical").
