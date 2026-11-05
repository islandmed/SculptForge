# Micro levels below the base: coarse cluster levels (design note, 2026-11-04)

Status: design, not built. Ships behind an **off-by-default** setting until the maintainer has a freedom-to-operate
opinion from a patent attorney. Owner: perf/micro-dense-lod. Plan item 8 (perf-plan 11-05); item 9 (first frame)
and the level-0 vertex path (perf/micro-base) are not gated by this.

## Clean-room rules for this work (mandatory)

- Designed only from the public literature listed below. Nobody working on it reads or searches patents, and nobody
  studies, decompiles or copies any commercial engine's implementation, or documentation of its internals.
  Anything that starts to look like a specific product's technique stops and goes to the perf lead first.
- What we deliberately **do not** do: no cluster DAG; no grouping of clusters whose shared boundaries are locked while
  the group is simplified and re-split; no runtime "cut" selected through such a graph by parent/child error bounds.
  Each cluster has its own short chain of coarser versions (a per-cluster tree of depth a few), nothing is shared
  between clusters except the global grid below.

## Sources (public, 1976-2000)

- J. Clark, "Hierarchical geometric models for visible surface algorithms", CACM 1976: pick a model's detail from
  its size on screen.
- J. Rossignac and P. Borrel, "Multi-resolution 3D approximations for rendering complex scenes", 1993: vertex
  clustering on a uniform grid; each cell's vertices merge to one representative, degenerate triangles drop.
- H. Hoppe, "Progressive meshes", SIGGRAPH 1996: a mesh as a coarse base plus refinements; error measured as distance
  to the full surface.
- S. Rusinkiewicz and M. Levoy, "QSplat", SIGGRAPH 2000: a bounding-sphere hierarchy used both to cull and to stop
  refining once a node projects below a pixel threshold.

## The problem

On a dense base (a 28M triangle scan: about 14 base triangles per pixel at 1080p) the micro renderer's coarsest level
is one triangle per base triangle, so it never draws fewer than the base, and the floor tier's 2M triangle budget
can't hold. Orbit cost should follow the pixels, not the base.

## What we build

1. **Global nested grids** (Rossignac-Borrel). Levels -1, -2, ...: cell size = 2^k x the base's mean edge, cells
   aligned to the model's box so each level's cells nest in the next. Each cell's representative point is the mean
   of the **displaced base corners** inside it, computed over the whole mesh (not per cluster). Two clusters that
   touch the same cell therefore place it at the same point with no boundary rule at all.
2. **Per-cluster coarse versions** (a per-cluster tree). For each cluster (n2stroke's layout) and each level, map its
   base triangles' corners to cells, drop degenerate and duplicate triangles, store the index list (cell ids into
   that level's representative buffer). Stop a cluster's chain when a level would keep fewer than ~16 triangles.
   Built off the main thread, per frame within a measured time budget; rebuilt per cluster when a stroke dirties it.
3. **Selection** (Clark, QSplat). Per cluster per frame: error of level -k = its cell diagonal projected at the
   cluster's nearest point (bounding sphere); draw the coarsest level with error < ~1 px, else the existing micro
   levels. The triangle budget then lowers detail by raising that threshold, as today.
4. **Seams between clusters on different grid levels.** Same level: none (shared cells, item 1). Different levels: a
   gap of at most the coarser cell, which item 3 keeps under about a pixel. Adjacent clusters are kept within one
   level of each other (a balance pass over the cluster neighbours, as in restricted quadtrees), and the coarse
   triangles are drawn with the existing micro shading (normals from the shading grid's corners), so a sub-pixel gap
   shows the surface behind it, not the background, in the closed models this targets. If pinholes still show,
   the next step is to fall back to the finer level on the cluster's border cells, not to lock boundaries.
5. **Flag.** `RenderSettings` / Preferences > Performance "Coarse micro levels (Experimental)", off by default;
   off = today's renderer exactly.

## Measure

Lucy 112M / 448M and core-200m: orbit fps and GPU ms on the E-core floor, the laptop, the Windows 3060; triangles
drawn vs the floor's 2M budget; png_diff against the flag off where the base edge is > 1 px (must match).
