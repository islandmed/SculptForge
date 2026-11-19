---
whats_new: **Crisp Flatten** cuts ridges and steep walls clean, without teeth, shards or folded triangles along the cut, with a big brush on a dense mesh and with Symmetry on.
fixed: Crisp Flatten no longer leaves teeth, spiky shards and folded triangles where a cut crosses a ridge or a steep
  wall, or where a stroke runs along the mirror plane with Symmetry on (stacked strokes and big brushes on a dense mesh
  were the worst). Where the two mirrored copies of a stroke overlap, each point is now cut once, by its own side's
  plane, and nothing slides across the mirror; a steep wall or an undercut comes down flat onto the cut instead of
  being held partway up.
testers:
  - Any OS (a Mac too): a dense sphere or head (1M triangles or more), build lumps with Clay and pinch them into sharp
    ridges, then Brush > Hard Surface > Crisp Flatten across the ridges with Symmetry X on: three strokes over the same
    path across the middle, then one pass with a very big brush. Look along the cut from a grazing angle: flat facets,
    clean edges, no teeth, spikes or dark folded specks, and both sides of the mirror match.
  - Same on the template head: flat planes on a cheek and along the side of the nose; the facet has no lump left under
    the eye. Dynamic topology off.
---
Crisp Flatten kernel (Sculpt.cpp, dabRecipeSingle, crisp locked plane). artist-qa's crisp_ridges close-up (1.31M sphere,
Symmetry X on, the app's default): main 351 folds / 30 spikes stacked x3, 1893 / 39 -> 220 big brush; template head face
planes 572 / 6. Causes and fixes:
- Symmetry: the copies of a dab that overlap shared crispStamp_/crispDone_ per vertex while each pulled along its own
  plane's normal (two planes up to 50 degrees apart on the big pass), so a vertex was pulled both ways and the sides slid
  across the mirror. Now each vertex is cut by the copy whose dab centre is nearest, the copies pull along the
  overlap-weighted mean of their normals (in the mirror plane when they overlap fully; each its own when two radii
  apart), an overlapping copy's lock plane is the mirror image of the first copy's (measured, its region was already
  part cut), and the in-plane relax only counts the copy's own points.
- Steep walls: the facing fade (60-85 degrees off the plane) held wall vertices partway, each at its own height: the
  teeth. On a locked plane the cut side no longer fades; a pull along one fixed way keeps every triangle's facing across
  the plane, so a wall or undercut lies flat in the plane instead (the remaining "folds" of the face planes step are
  those flat slivers at the nose wing and eye rim, zero height). The fill side and unlocked crisp planes keep the fade.
- No fold limiter and no region cap: the cost per vertex is a distance per symmetry copy.
- Cost: cutting every wall point lets a deep cut's dabs reach as much of the part under the facet as above it (a 240 px
  stroke across a scan: 2.5M points a dab instead of 1.6M, half as many dabs, the stroke trailing by a third more). A
  locked Flatten cut never moves a point below its plane, so those leave the region before their weights, and the dab
  refits the BVH leaves around the points it moved (about 6% of a deep cut's sphere), not every leaf the query found.
