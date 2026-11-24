---
fixed: **The surface lagging a second or two behind the brush on a dense Micro level** (#184, thanks @HG4Science): each dab
  re-shaded every patch within reach of the brush, not just the ones it changed. With an alpha (whose square reaches well
  past what it moves) that was more than twice the work, and the shading fell behind the stroke. A dab now re-shades only
  what it changed and the patches next to it.
testers:
  - Linux, a 2M+ base at Micro 4: Standard brush, Bolt alpha, size about 90 px, one long stroke. The relief shows right behind
    the brush, not a second or two later.
  - Any OS, Micro 3: the same stroke, then orbit. No dark or flat patches at the stroke's edges.
---
Reviewers: MicroStroke::changed_ (0b2d89b81: stitch marks only the candidates the kernel or the stitch changed) was never
filled on main, so stitch marked every candidate. The kernel now sets it per candidate it writes. The neighbours' shading is
still redone: the renderer's shadeSeeds puts each shaded patch's vertex ring in stale_ (MicroShader::shadeSet ring2_).
Floor replay of the reporter's Draw + Bolt flow (2.58M base, Micro 4, 27 px, 8 threads): 41% of candidates change; the
stroke's mean coarse-shading queue goes from 93.6K to 11.3K tiles, fps 22.9 -> 26.3, dab time unchanged.
Tests: micro_alpha_dab_marks_only_what_changed (fails without the fill), ui micro_alpha_stroke_shading (view_stale 0 after
the stroke drains, at Micro 3 and 4, with and without Focus Detail). SF_STROKE_LOG adds one "micro dab:" line per dab:
triangles changed of those in reach, the stride, and find / plane / kernel / stitch ms.
