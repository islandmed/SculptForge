---
whats_new: **Crisp Flatten** no longer leaves spiky, thin triangle shards along the edges of a deep cut across a ridge or a crest.
fixed: Crisp Flatten no longer turns triangles inside out when a deep stroke crosses a ridge, crest or steep flank, the spiky thin shards a member saw along flattened edges on a dense mesh without micro detail. Each point of the cut now stops short of folding a triangle over, counted over the whole stroke, and carries on on the next dab. A very steep flank can be left partly cut.
testers:
  - Any OS (a Mac too): a dense creature or head with ridges, Brush > Hard Surface > Crisp Flatten, a big radius, drag deep across a ridge or the brow, then look along the edge from a grazing angle; the flat facet has a clean edge and no thin spikes or inside-out shards. Dynamic topology off.
  - Same stroke with pen pressure and with Symmetry on; Flatten still fills dips as before.
---
Crisp Flatten (Sculpt.cpp, crisp dab): limitFolds now runs on a crisp dab's pull (kCrispFoldLimits: turn up to 90 degrees from the stroke-start facing, a sliver 60, each keeps a tenth of its area), against the positions the stroke found each point at (crispOrigin_), since the turns of many dabs add up (the first try, per dab, fixed 3 of 255 folds). A held point's crispDone_ is scaled to what it reached, the next dab carries on. Regions over 150k vertices and Dynamic topology skip it. No per-stroke depth cap was needed: the deepest cut in the fixture is 0.92 radii.
