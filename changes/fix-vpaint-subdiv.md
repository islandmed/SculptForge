---
whats_new: **Vertex paint keeps its detail across subdivision levels**: paint a stroke at a low level and it blends over the finer colour above instead of replacing it.
fixed: **Vertex paint lost on a subdivision round trip**: fine colour painted at the highest level was overwritten with the interpolated low-level colour after painting at a lower level and stepping back up. A low-level stroke now goes up as a blended delta added to the colour the higher levels already hold (vertex paint, Base and paint layers). Undo across levels and Voxel Remesh colour transfer are covered by tests. Reported by @masterfull95 (Discord).
testers:
  - Any OS: Divide twice, paint fine dots at the highest level, step to level 1, paint a low-opacity stroke over them, step back up; the dots are still there under the stroke.
  - Undo the stroke and the level steps one by one; the colours return each time.
---
Fix in `spreadColorDelta` / `spreadLayerDelta` (src/core/Multires.cpp). Tests: multires_low_level_paint_*, multires_paint_strokes_undo_across_levels_and_remesh_keeps_the_colour.
