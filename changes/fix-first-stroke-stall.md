---
fixed: The first stroke after opening a large model no longer freezes the view for 1-2 seconds on NVIDIA graphics on
  Windows. The brush's shaders now compile while the model opens, also when a large model draws in preview detail.
testers:
  - Windows with NVIDIA: open a model of 15 million triangles or more (or Help > Benchmark's head), sculpt one Clay
    stroke right away; it starts without a pause.
---
Follow-up to the 0.11.10 warm-up (8db5392e), which skipped meshes whose visible clusters all draw at a coarse level.
