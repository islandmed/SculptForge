---
changed: Micro levels on dense models open several times faster. The view shows the model as soon as the level is on
  the graphics card and its fine lighting fills in over the next frames, instead of waiting for all of it (a 28M
  triangle scan at micro level 1 / 2, 112M / 448M micro triangles, laptop: first frame 9.1 -> 3.0 s and 10.7 -> 3.1 s).
  Projects of meshes from 8 million triangles also save what opening would otherwise rebuild (the mesh's neighbour
  tables, its picking structure, a micro level's edge links and drawing order) and a small preview of the model: the
  preview shows the moment you open the file while the rest is read, and the rest opens faster too (the same scan
  from a fresh copy of the file, laptop: model on screen 7.5 -> 2.0 s; slow two-core floor: preview in 0.1 s, model
  17.0 -> 8.7 s). Such files are bigger (588 MB -> 1.09 GB); saving again copies that part over instead of working it
  out, and autosave never works it out. Older versions open these projects as before.
testers:
  - Open a large project with a micro level (Tool > Geometry > Divide to Micro, saved and reopened): the model shows
    quickly, at first with slightly coarser lighting; within a few seconds the fine detail appears. Report anything
    that stays coarse, flickers or looks patchy after that.
  - Save a model of 8M+ triangles, close, open it again: a preview with "Opening ..." shows at once; sculpting starts
    when it's in (strokes before that only turn the view). Report the time from Open to sculpting, and your disk.
---
Clusters not shaded yet are lit by the drawn micro triangles' own facet normals (from depth); the first shading runs
in slot order while the GPU draws (MicroRenderer::initialShadeMs, interactive loop only). Open cache: core/OpenCache.h
(SF_OPEN_CACHE=0 turns it off); preview: makeProjectPreview / loadProjectPreview, App::startOpening.
SF_OPEN_PROFILE=1 prints the open steps' times.
