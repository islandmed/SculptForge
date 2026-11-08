---
whats_new: **Garment templates and home printing**: trousers, straight and A-line skirts, a hoodie with hood and kangaroo pocket, a dress, a hooded cloak and gloves, drawn at real size from a size chart you type in once (`garment_size`, `garment_new template=...`, `add=true` for an outfit). `garment_export path=x.pdf` prints any pattern on A4 or Letter pages with trim lines, matching triangles, page names and a 50 mm check square; mirrored pieces print once, marked "cut 2". Guide: [Garments](garments.md).
highlight: Sewing patterns from your measurements, printed at home
added: **Garment templates** (#garments): trousers, straight and A-line skirts (with waistband), hoodie (long sleeves,
  two-piece hood, kangaroo pocket), dress (bodice and A-line skirt), cloak (half-circle cape with hood) and gloves (a
  pair), next to the T-shirt. Flat panels at real size from a size chart saved in the project (chest, waist, hip,
  neck, shoulder, arm, inseam, rise, head, hand and more: `garment_size`), sewn the way each garment is made with
  the seam lengths matched; `garment_new template=... add=true` builds an outfit in one pattern. Our own proportions,
  documented in docs/design/garment-templates.md.
  **Printing patterns at home**: `garment_export` writes tiled PDF (or one SVG per page) for A4 or Letter, portrait
  or landscape: trim lines, half triangles that complete across each join, page names (A1, A2, ... B1), the 50 mm
  check square on A1; blank pages are left out and a mirrored or repeated piece prints once, marked "cut 2, 1
  reversed". The whole-sheet SVG and the DXF for cutters stay. Guide chapter [Garments](docs/guide/garments.md).
testers:
  - Any OS, Scripting: `garment_new template=trousers`, then `garment_export path=<Desktop>/trousers.pdf`; open it in
    any PDF viewer: 24 A4 pages, page A1 has the 50 mm square. Print A1 at 100% and measure the square: 50 mm.
  - Windows (Edge or Acrobat) and macOS (Preview): the same PDF opens without warnings; text is readable.
  - `garment_size chest_mm=1100` then `garment_new template=hoodie`; `garment_info` lists 7 panels, no "Problem" lines.
  - `garment_export path=<Desktop>/x.svg format=svg_pages paper=letter`: files x-A1.svg, ... open in a browser.
---
Slices 1 (templates) and 4 (print) of BRIEF garments-next. No UI on main yet (the Pattern window rides
feat/cloth-land); fabric presets, layering and the Costume scene build on that branch.
