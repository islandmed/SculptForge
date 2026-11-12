---
whats_new: **Inflate over a Crease groove**: stacked Crease and Inflate strokes no longer tear needles and streaks into a divided mesh.
fixed: **Spikes and streaks from stacked Crease and Inflate strokes** (#194): on a divided mesh (about 5 million triangles) Crease packs the triangles across its line, and Inflate, moving every point along its own normal, then flipped them (twenty overlapping strokes left about 2,000 folded triangles and needles). A dab that reaches such a packed grid now swells along the brush's normal instead; Inflate on an ordinary surface is unchanged.
testers:
  - Sphere, Divide three times: Clay (12 strokes), Crease (4), Inflate (4) over the same spot, crossing each other; zoom in on the tip. No needles, no dark specks, no bright stretched streaks.
  - Inflate a fresh bump or a finger: it still swells outward on all sides.
---
Cause measured on the filed session (artist-qa closeup_stack.sfui) and a headless 5.4M replay (tests/test_stacked_tear.cpp): not stale normals (live vs recomputed 0.04 degrees); Crease packs edges to a tenth of the grid spacing and Inflate's per-vertex normals there are noisy. Tried and dropped: a per-vertex sideways gate (worse on the filed session), cone-limited or blended directions (still folds), edge-length caps.
