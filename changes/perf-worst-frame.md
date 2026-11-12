---
whats_new: **Fewer hitches on dense meshes**: after a stroke or a move on a mesh of tens of millions of triangles the view no longer stalls for a tenth of a second or more; the simplified view catches up a few milliseconds per frame.
highlight: No long stall after a stroke on dense meshes
fixed: **Dense meshes** (tens of millions of triangles): the simplified view that follows a stroke was brought up to date in one frame, a 75-100 ms stall on a 33M-triangle mesh at the first Move or orbit after the stroke (330 ms on a 104M one). It now takes a few milliseconds of work per frame and sends its points in pieces, so the slowest frame of a 33M-triangle run on the simulated entry-level tier drops from 107 ms to about 50 ms (orbit from 105 to 21 ms, Move from 82 to 35 ms). The model's bounding box, asked for by the first panel redraw after every stroke, is measured on all cores.
testers:
  - A mesh of 20M triangles or more (Tool > Geometry, Divide to a high level): sculpt a stroke, lift the pen and start to orbit or Move; the view should not hitch.
---
Slicing is Renderer::updateCoarse (compute 12-20 us a cluster measured at 33M; upload 8 ms for the 1.9M points). Numbers and method in REPORT.md.
