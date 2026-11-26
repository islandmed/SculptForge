---
changed: **Big strokes end sooner** on dense meshes: the stroke ends right after its last dab (with its undo step), and the
  shading catches up within a few frames after release, on the stroke thread, instead of holding up the release. A new stroke
  can start at once, even while the shading catches up (it works out the shading it reads first, so the result is the same);
  shortcuts made meanwhile run as soon as it is in.
testers:
  - Windows, a dense Part (10M+ triangles): a 240 px Clay stroke, release, watch the end close up with Cavity on. Good: no flicker or pop as the shading settles.
  - Release and stroke again at once over the first one's end. Good: no hitch, no seam.
---
Floor tier, Lucy 28M, 240 px: release_end_ms (bench.sh quick, same binary A/B with SF_DEFER_NORMALS=0) and restroke_ms (+50 / +300 ms)
in the READY. SF_DEFER_NORMALS=0 turns it off. Bit for bit: tests/test_deferred_normals.cpp; tests/ui/async_stroke_settle.sfui.
