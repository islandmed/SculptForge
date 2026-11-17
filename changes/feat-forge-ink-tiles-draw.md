---
testers:
  - Any OS, started with SF_MICRO_TILES=1: New Sphere, Add Micro level 4, zoom close and draw a short line and a few dots with
    Standard at a small size. Good = the line and dots show and catch the light at every zoom, no cracks or seams along the
    base triangles' edges, undo returns the smooth surface and redo brings the same marks back; bad = a crash, a lost GPU,
    marks that pop as you zoom, or memory that keeps climbing while you zoom in and out.
  - Without SF_MICRO_TILES (the default): the same steps look exactly as in the previous version.
---
Adaptive Detail tiles drawn (testers only, no user-visible change): with SF_MICRO_TILES=1, strokes finer than the stored
micro grid are kept in per-triangle tiles and drawn as delta pages added to the micro surface at every drawn level, lit by
per-page slopes. The flag stays OFF by default. On Mesa's OpenGL over D3D12 (WSL, Windows on ARM) the first tiled frame
removed the GPU device; tiles now fall back to the plain micro levels when the GL renderer is D3D12, and that device loss
needs a real fix before tiles can ever default on. Stacked on feat/forge-ink-tiles (lands after it).
