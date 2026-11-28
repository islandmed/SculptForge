---
whats_new: **Project Detail** no longer leaves shards and fins along crevices and plate borders, and a concave pocket (a mouth roof) no longer pinches into a star. The default settings are clean now; Blur and a small Dist are not needed.
fixed: **Project Detail** (Part > Project Detail): a vertex next to a crevice could jump across to the opposite wall (the nearest hit along its normal faced the other way), and the vertices of a plate border that reached a neighbouring plate frayed into hair-like fins; Mean averaged the two walls and was worse. A hit now counts only when its surface faces the way the vertex does, a vertex whose offset differs from its neighbours' median by more than two edge lengths takes that median instead, and a vertex whose nearest source point lies more than 45 degrees off its normal follows its neighbours instead of collapsing onto that point (many vertices of a concave pocket shared one, a star-shaped pinch). On a 7.6M-triangle test sculpt the spiked vertices at the default Dist go from 2477 to 0.
testers:
  - Any OS: sculpt a model with sharp plates or ridges, duplicate it, Remesh the copy low, Divide twice, Project Detail at the default Dist (also with Mean on): the borders stay clean, no hair-like fins along the crevices.
  - Any OS: a concave pocket (inside of a mouth) projected the same way: smooth, no star-shaped pinch.
---
Reviewers: measurements and the spike counts per setting are in REPORT.md.
