---
changed: Micro levels on dense models open several times faster. The view shows the model as soon as the level is on
  the graphics card and its fine lighting fills in over the next frames, instead of waiting for all of it (a 28M
  triangle scan at micro level 1 / 2, 112M / 448M micro triangles, laptop: first frame 10.8 -> 2.8 s and 11.1 -> 3.3 s).
  Projects of meshes from 8 million triangles also save what opening would otherwise rebuild (the mesh's
  neighbour tables, its picking structure, a micro level's edge links), so they open faster again (the 28M triangle
  scan at micro level 1, laptop: first frame 3.0 -> 2.0 s), at the cost of a bigger file (588 MB -> 1.86 GB). Older
  versions open these projects as before.
testers:
  - Open a large project with a micro level (Tool > Geometry > Divide to Micro, saved and reopened): the model shows
    quickly, at first with slightly coarser lighting; within a few seconds the fine detail appears. Report anything that stays
    coarse, flickers or looks patchy after that.
---
Clusters not shaded yet are lit by the drawn micro triangles' own facet normals (screen-space slopes); the shading runs in slot order while the GPU draws
each frame (MicroRenderer::initialShadeMs, interactive loop only). Also: the Morton sort is a parallel radix sort, the
micro level attaches while the BVH builds, --benchmark-file no longer builds the BVH twice, and the app keeps drawing
while micro shading is pending. SF_OPEN_PROFILE=1 prints the open steps' times.
