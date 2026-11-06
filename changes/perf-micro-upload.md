---
changed: Micro levels: big brushes spend about a third less time preparing each frame's shading (a 240 px stroke on
  a 200 million triangle head: 11 to under 8 ms a dab on the floor tier). Each changed patch is shaded on its own;
  its neighbours, whose shared border rarely changes, are brought up to date when the stroke ends.
testers:
  - Any OS, a micro level (Subdivide > Micro): long strokes with a large Clay brush; the shading under and around
    the brush follows as before, and no lines show along patch borders during or after the stroke.
---
Measured with the benchmark's stroke_work_ms (uploads = MicroRenderer::sync). The GL uploads were ~3% of it;
the rest was CPU shading (MicroShader).
