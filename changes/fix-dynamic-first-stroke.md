---
whats_new: **Dynamic** on a dense Part with a big brush no longer freezes the first stroke: the surface under the brush is remeshed a bounded amount per dab and the view keeps drawing.
fixed: **Dynamic first stroke** on a dense Part (hundreds of thousands of triangles, a big brush, Detail toward large polygons) stalled the app for seconds,
  up to 20 s on a 9M-triangle scene (macOS wait cursor). Each dab looked at every triangle under the brush (100-380 ms a dab at 800k) and the first
  stroke sent the whole Part to the GPU every frame, rebuilding the coarse levels each time. A dab now looks at no more than the 100,000 triangles
  nearest its centre (a brush covering more is remeshed over the nearest, the rest follows as it moves), finds mirror partners on all threads,
  a remeshing stroke sends its changed clusters as patches, and the coarse levels are made once, after the stroke. Where face groups, borders or masks make most collapse candidates fail (a tester's 803k Part froze the app in the collapse pass),
  the candidates a pass tries per dab are bounded too. A Dynamic dab slower than 150 ms is logged (Help > Copy log). Reported by a tester on macOS.
testers:
  - Dense Part (700k+ triangles, ideally in a multi-million-triangle scene), Dynamic on, Detail about 23, Clay Block with a big brush: the first stroke draws as you go, no wait cursor.
  - Large flat triangles (remeshed head) with a small brush: the area under the brush refines over the stroke, Undo restores it.
---
Dab work is bounded (tests/test_dyntopo.cpp: dyntopo_dab_work_is_bounded_*); the renderer sends patches during a stroke
(tests/ui/dyntopo_first_stroke.sfui). sf_dyntopo_bench prints per-dab and per-phase ms.
