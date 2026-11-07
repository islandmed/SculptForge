---
highlight: Timelapse: rendered timelapses no longer flicker between detail levels (thanks @ebarranco)
whats_new: **Timelapse** videos of dense sculpts no longer flicker (**Edit > Export timelapse...**).
fixed: Timelapse: a PBR timelapse of a high-detail sculpt flickered a little in the video (not in the view): after each
  step of the history the frames switched between the full mesh and its lighter preview levels. Timelapse frames and
  `--render` turntables now always draw the full mesh. Thanks @ebarranco.
testers:
  - Any OS: a dense sculpt (millions of triangles) with a few strokes, Material > PBR, Edit > Export timelapse (4K); the
    video's shading stays steady from frame to frame.
---
Cause: RenderSettings had no offline mode; renderToImage used the view's coarse-LOD choice (still budget, edits pending).
