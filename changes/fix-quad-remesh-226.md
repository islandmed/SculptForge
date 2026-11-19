---
fixed: **Quad Remesh** no longer stops at 52% on a dense sculpt with surface grain (pores, scan noise, a rough
  brush texture), and **Cancel** now stops it at once at any point, leaving the Part as it was. The grain's sharp
  little edges were taken for hard edges and its bumps for sharp tips, so the layout grew hundreds of thousands of poles
  and never finished; Cancel was only seen between steps. Grain is now told apart from real hard edges and tips, the bar
  keeps moving through the long steps: on our test PC, 2.4 million triangle grainy sculpts at 50000 quads now finish in
  30 seconds to about 2 minutes (#226, reported by @maratcgartist). Fixes #226.
testers:
  - Windows, a Part of 2 million triangles or more with fine surface grain or pores: Geometry > Quad Remesh at
    Target Quads 50000. The bar keeps moving past 52% and the remesh finishes; Ctrl+Z brings the sculpt back.
  - Any OS, the same Part, Symmetry on and off: start Quad Remesh and press Cancel at any point; the bar goes at once,
    the status says "Quad Remesh cancelled" and the Part is unchanged (no undo step added).
---
Benchmarks (PC, sf_remesh_bench, 2.4M triangles, 50000 quads; ctest -L nightly -R remesh_bench_226):
smooth bumpy sphere 18.5 s before, 14.3-17.7 s after; with grain past the 600 s limit before, 29 s after; the bust with
grain past 600 s before, 137 s after (3841 singularities: bounded, still slow).
Cancel: the panel lets the job go on the click; the worker's longest gap between checks is 0.65 s on 2.4M grainy
triangles (PC, SF_QREMESH_TIMING), 1-43 ms at seven points in qremesh2_cancel_answers_within_a_second.
