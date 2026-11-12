---
whats_new: **AI agents reach fine detail**: an AI tool connected over MCP now gets a step-by-step route to finished
  detail (how dense the mesh must be at each stage, brush sizes), **detail_pass** for pores, skin, wrinkles, scales and
  scars over a region in one undo step, detail presets for strokes (Detail Crease, Detail Wrinkle, Detail Pores...) and
  **view_closeup** to look at one spot up close.
highlight: AI agents sculpt down to pores and wrinkles
added: **Fine detail for AI agents (MCP)**: the server's instructions and the creature-head recipe walk an agent
  through block-out, secondary forms at about 1M triangles and detail at 3-6M, with brush sizes for each stage.
  **detail_pass** (pores, skin, wrinkles, scales, scars over a region or the whole Part; seeded, one undo step) refuses
  a mesh too coarse for the detail and says how far to subdivide. Detail presets (Detail Crease, Detail Wrinkle, Detail
  Pores, Detail Skin, Detail Scales, Detail Scar) work as brush names in stroke and stroke_screen without changing the
  user's alpha. **view_closeup** renders one spot up close along the surface, lit low, and reports the triangles there
  and the finest detail the mesh holds (read-only; in live mode the user's view doesn't move). Both servers, local only.
testers:
  - Any OS, live mode: ask your AI tool for "an orc head with pores, wrinkles around the eyes and a scar"; it should
    subdivide to a few million triangles before the detail, call detail_pass and view_closeup, and each step shows in
    Undo labelled AI.
  - Ask it for detail_pass on a fresh sphere: it should refuse and name a subdivide level; after that subdivide it works.
---
Benchmark (bench/mcp-detail/): one orc-head prompt over `sculptforge-cli --mcp --attach`, before and after, screenshots
plus a geometric detail metric (metric.py: high-frequency energy and curvature variance on the whole head).
