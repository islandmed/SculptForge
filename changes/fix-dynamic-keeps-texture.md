---
whats_new: **Dynamic** keeps a painted Part's texture: sculpting with Dynamic on no longer erases the paint.
fixed: Dynamic topology no longer erases a Part's painted texture: the UVs are kept through the stroke (new triangles get UVs from the ones they split; at a UV seam the remesh merges and relaxes only along the seam), so the paint stays in place and Undo restores the UVs exactly. A collapse that would leave a triangle off the painted area of its chart (no white specks where big brushes simplify a painted head), and a collapse, flip or relax that would turn a triangle's texture over, are refused.
testers:
  - Windows or macOS: paint a Part into its texture (Paint into Texture), turn Dynamic on and sculpt over the paint, big and small strokes, across a seam if the Part has one; the paint stays. Undo, then redo; save and reopen.
  - Windows or macOS: Auto UV the Human head, paint a few colours, turn Dynamic on and drag big Smooth, Standard and Clay brushes across the eyes and forehead; no white specks or streaks appear on the paint.
---
Reviewers: src/core/Dyntopo.cpp (UVs per triangle slot in TopologyTriangleValues; split/collapse/flip/relax), src/render/Renderer.cpp (patches carry their triangles' UVs), tests/test_dyntopo.cpp, tests/e2e/dynamic_keeps_texture.sfui.
On the Human head with Auto UV (seams all round the eyes) the strokes leave more folded edges than without UVs (30 against 8 on artist-qa's session; a first version that froze seam vertices left 139 and tore the eyelids), at the eye openings and ears, and the head keeps 13.5k triangles against 10.8k (the seams keep some). No white patches on the paint at normal viewing distance: 7 of 539 forehead-band triangles sample white, against 49 of 1802 before any stroke.
Cost, ms per dab (`--bench-dyntopo[-uvs]`: Standard, Detail 50, 40 dabs, X symmetry, GPU sync per dab, RTX 3060 on the PC; runs of 3-5):
- Part without UVs, batch base vs this branch: 82k 3.87 vs 3.79; 1M 14.98 vs 15.53 (the best runs 14.44 vs 14.47; run spread is about 5%): unchanged.
- Painted Part, full upload per dab (UVs kept, no renderer patching): 82k 10.6-12.5, 1M 43.
- Painted Part, patched UVs (this branch, with the seam checks), 1M, three brushes (Standard Detail 50, Standard r 0.4, Smooth): +23 to +28% per dab (remesh +10-14%, GPU upload about +30%); artist-qa's slowest frame per stroke on a 1.3M sphere: +22 to +50% (Standard 60 80-85 vs 63, Standard 200 168 vs 131-144, Smooth 150 145-157 vs 104-107 ms). Before the fix the paint was simply gone.
Follow-up after the merge: the GPU upload of a painted Part's patches (the transfer of their UVs itself costs about nothing) and the cost of the seam checks.
