---
whats_new: **Smoother view on big models**: the processor prepares the next frame while the graphics card draws this one,
  so turning the view and sculpting on large meshes run at up to twice the frame rate on some computers.
changed: Frames overlap the CPU and the GPU: the processor builds the next frame while the graphics card draws the last,
  at most one frame ahead, so the input lag doesn't grow. Reading each frame's GPU time used to wait for the whole
  frame on some drivers (Mesa on Apple silicon), which made CPU and GPU take turns. Floor tier, a 28M-triangle scan:
  orbit 56 to 90 fps; a 200M-triangle scene: orbit 84 to 186 fps, big-brush strokes 51 to 191 fps.
testers:
  - Any OS, a big model (10M+ triangles): orbit and sculpt with Help > Benchmark or the performance overlay;
    frames are as fast or faster than 0.11.5 and the brush doesn't trail the pointer more.
  - Windows (NVIDIA, AMD, Intel): orbit a big model for a minute; no stutter, no flicker, no frames shown out of order.
---
Fences (GL 3.2 core, no extension): GpuFrameTimer polls a fence after each timer query (zero timeout) instead of
GL_QUERY_RESULT_AVAILABLE; App::run waits for the previous frame's fence before the next swap and flushes after it.
