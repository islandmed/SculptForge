---
fixed: **Micro levels on OpenGL over D3D12** (Windows on ARM laptops, WSL): a few strokes on a micro level no longer make
  the graphics driver drop the GPU ("Removing Device"). On that layer the strokes' updates now go to the GPU as copies.
testers:
  - Windows on ARM (or WSL with its GPU): Tool > Geometry > Add Micro on a dense sphere, then 20+ Standard strokes and
    undo; the view keeps drawing, no freeze, no black viewport.
---
Cause (isolated on the PC, Mesa 25.2.8 on D3D12, RTX 3060): glBufferSubData into micro texture buffers the GPU was still
drawing from; GALLIUM_THREAD=0 or a glFinish before the updates also avoided it. Copies only there (gl::copyUploads):
on Mesa/Apple they cost large micro strokes a third of their frame rate.
