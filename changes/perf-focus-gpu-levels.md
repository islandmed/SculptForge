---
changed: Focus Detail (experimental, `--focus-detail`): the GPU now keeps a small record per tile (coarse grid and
  edges) and full-resolution grids only for the tiles drawn close up, within a fixed pool; coarse tiles are shaded at
  their own level, so a streamed level opens shaded at once instead of shading every full grid first.
---
Phase 1 of the billion-triangle brief (render/MicroFocus.{h,cpp}). Numbers go in after bench.sh focus; see REPORT.md.
