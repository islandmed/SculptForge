---
added: **Garments, part 1** (scripting and the AI assistant for now; the Pattern window follows): pattern panels at
  real size (millimetres, curves, notches, grain line, seam allowance) saved in the project, a T-shirt template
  (front, back, two sleeves, sewn), seams with length and direction checks, even triangulation at a chosen particle
  distance with seams matched point for point, placement around the body (front, back, sides, arms found from its
  shape), export as a real-size SVG for printing and sewing (with a 50 mm scale square) or DXF for cutters, and
  SVG panels read back in. Commands: garment_new, garment_info, garment_add_panel, garment_sew, garment_place,
  garment_export, garment_import and more.
testers:
  - Any OS: `sculptforge-cli -c "garment_new; garment_export path=shirt.svg"`; open the SVG in a
    browser or print it at 100%: the square is 50 mm, the front is 52 cm wide.
---
Part 1 of 3 (garments agent). Core only: no UI, no simulation yet (Drape comes from the cloth agent's solver,
docs/design/cloth.md section 7). Tests: tests/test_garment.cpp (a sewn T-shirt welds into one consistently
oriented surface with exactly four openings; SVG reader fuzzed; imports and project loads share a 50,000
flattened-outline-point budget, and the self-crossing check runs on a grid, so a hostile file stays fast).
