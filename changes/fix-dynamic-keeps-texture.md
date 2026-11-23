---
whats_new: **Dynamic** keeps a painted Part's texture: sculpting with Dynamic on no longer erases the paint.
fixed: Dynamic topology no longer erases a Part's painted texture: the UVs are kept through the stroke (new triangles get UVs from the ones they split; at a UV seam the remesh merges and relaxes only along the seam), so the paint stays in place and Undo restores the UVs exactly.
testers:
  - Windows or macOS: paint a Part into its texture (Paint into Texture), turn Dynamic on and sculpt over the paint, big and small strokes, across a seam if the Part has one; the paint stays. Undo, then redo; save and reopen.
---
Reviewers: src/core/Dyntopo.cpp (UVs per triangle slot in TopologyTriangleValues; split/collapse/flip/relax), src/render/Renderer.cpp (patches carry their triangles' UVs), tests/test_dyntopo.cpp, tests/e2e/dynamic_keeps_texture.sfui.
On the Human head with Auto UV (seams all round the eyes) six Dynamic strokes across the eyes leave 7-19 folded edges against 6 without UVs (a first version that froze seam vertices left 140 and tore the eyelids).
Cost, ms per dab (`--bench-dyntopo[-uvs]`: Standard, Detail 50, 40 dabs, X symmetry, GPU sync per dab, RTX 3060 on the PC; runs of 3-5):
- Part without UVs, batch base vs this branch: 82k 3.87 vs 3.79; 1M 14.98 vs 15.53 (the best runs 14.44 vs 14.47; run spread is about 5%): unchanged.
- Painted Part, full upload per dab (UVs kept, no renderer patching): 82k 10.6-12.5, 1M 43.
- Painted Part, patched UVs (this branch): 82k 4.1-4.3 (+3 to +10% over unpainted), 1M 17.4-18.3 (+12 to +17%). Before the fix the paint was simply gone.
Follow-up after the merge: the remaining ~2 ms per dab at 1M (the patch UV transfer itself costs about nothing).
