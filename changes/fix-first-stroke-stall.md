---
fixed: The first stroke of a session no longer waits for the stroke's own shaders: the predictive preview and draft
  stroke programs are compiled and drawn once a frame after the model shows (on Windows with NVIDIA the first stroke
  froze one frame for 1-1.8 s on 15-28M meshes). SF_NO_STROKE_WARM=1 turns it off to compare.
testers:
  - Windows, NVIDIA: open a 15M+ model, wait a second, make one stroke; the view keeps moving with the brush (no
    one-second freeze at the start).
---
Cause found in the bench JSONs: the stall is all in the "draw" split (drawView), not sculpt/upload/GPU time; the only
GL work new on that frame is the GHOST / FLAT program variants and the first BufferSubData into the vertex buffer.
The Windows gate (win.sh, PC frozen at commit time) is in REPORT.md: drop this fragment if it doesn't confirm.
