---
whats_new: **Smoother strokes on dense meshes**: the frame right after a stroke on a big or highly subdivided model no
  longer stalls while the Sculpt Timeline copies the whole model (it did every 16th stroke, and when a project
  opened), and each dab of a big brush copies less. Thanks @samuelsharit (YouTube) for the lead.
changed: **No whole-model copies on the stroke path.** The Sculpt Timeline's checkpoint (a full copy of the positions every 16
  strokes, 168 MB at once on a 28M-triangle scan) is now made a slice at a time over the frames after the stroke, and is
  dropped if sculpting resumes first; a dab no longer copies its region's vertex list for normals that come from the
  BVH leaves (10 MB a dab at 240 px on Lucy 28M). Thanks @samuelsharit (YouTube) for the lead.
testers:
  - A dense model (a scan or a Part subdivided to 6 levels): sculpt 20 quick strokes in a row with a big brush; no hitch
    in the frame after any of them, and Sculpt Timeline scrubbing is as quick as before.
  - Undo and redo a few of those strokes, and step a level down and back up: everything comes back exactly.
---
Measured with an LD_PRELOAD memcpy/memset tracer (~/Work/.sf/wholecopy/copytrace.c) on v0.11.10, floor tier.
