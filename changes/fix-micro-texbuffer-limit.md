---
fixed: **Android**: on graphics chips with a small buffer limit (some Mali phones and tablets), a Micro level on a mesh of more than about 33,000 triangles now draws and takes strokes.
testers:
  - Android with a Mali GPU: Sphere at 6 subdivisions, Tool > Geometry > Micro (Add Micro, level 2), sculpt a few strokes and turn the model; it stays visible and the strokes show.
---
A Micro level keeps two texels per base triangle and per base vertex in buffer textures, past a Mali-G72's 65536-texel limit at 33k triangles. Those tables (and the Focus and tile page tables) are now 2D textures there (render/TexelStore.h, the micro programs' WIDE variant); a mesh that fits neither says so in the log. Tests: tests/e2e/micro_small_gpu.sfui and the render matrix's "tables over the buffer limit" cells.
