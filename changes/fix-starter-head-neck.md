---
whats_new: **Starter meshes**: the Human Head's neck ends in a clean, even rim (no more two spikes under the jaw), and
  the Stylised Head, Bust, Torso, Hand and Ear have clean open edges too.
fixed: **Starter meshes**: the Human Head and Stylised Head had two thin spikes hanging below the neck at every
  subdivision level; the cut now follows one of the base mesh's edge loops, so the open rim is an even ring. The
  Bust's chest and arm cuts, the Hand's wrist and the Ear's skin edge are clean too, and the Torso now ends in a
  ring round each upper thigh and at the top of each upper arm. The faces themselves are unchanged.
testers:
  - Any OS: File > New from Starter Mesh > Human Head; front and three-quarter view from slightly below: the neck
    ends in a smooth rim with no spikes, at every level (D / Shift+D).
  - Same for Stylised Head, Bust, Torso, Hand and Ear: every open edge is a smooth curve, no notches or points.
---
Generator: tools/starter/build_starter_meshes.py (snap_to_edge_loops, relax_rim); assets rebuilt for the six
meshes whose geometry changed. starter_meshes_clean_rims fails on the old assets.
