---
whats_new: **Faster Pack**: **UV Editor > Pack** across a UDIM grid takes well under a second on most models (it
  could freeze the app for half a minute), runs in the background with a progress bar and **Cancel**, and lays islands
  out tighter. Unwrap runs in the background too.
fixed: UV Editor > Pack froze the app on UDIM grids (about 18 s for a 10 x 2 grid on the lion head on a laptop, 11 s
  for 4 x 3): the shape packer's search now runs several densities at once, coarse first, stops as soon as an island
  can't be kept whole, and keeps its quarter-resolution layout instead of packing everything again at full size.
  10 x 2 on the lion: 18 s -> 0.04 s. Pack, Pack at Density and every Unwrap now run on a worker thread with a
  progress bar and Cancel under the view.
fixed: Packing could shrink some islands and lay them over their neighbours: the shape packer's quarter turns mirror
  an island, which the fit back onto the island couldn't follow. Islands are now only turned to their smallest box
  first; layouts are up to 6% denser and no longer overlap.
fixed: Pack's status line could report more than 100% of the space used (an island bigger than a tile is shrunk to
  fit one); it is now measured.
testers:
  - Any OS: open a detailed model (40k+ triangles, e.g. a head), UV Editor (Alt+U), Unwrap; set Columns 10, Rows 2, click Pack: a progress
    bar shows briefly, the window keeps drawing, the islands land in the grid, Ctrl+Z puts them back.
  - Any OS: Auto Seams, Unwrap, then Pack with a 10 x 2 grid and click Cancel under the view while it runs: "Pack
    cancelled", the UVs stay as they were.
---
Found by slowtests (ui_uv_editor). Numbers and shots in REPORT.md.
