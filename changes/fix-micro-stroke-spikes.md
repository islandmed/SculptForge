---
fixed: Micro levels: Clay (and the other plane brushes: Clay Block, Flatten, Trim) no longer leave spiky shards where a
  stroke's dabs pile up over rough, folded surface such as strands meeting skin, also on the symmetric side. A big
  brush's dab that would fold the base mesh over now sculpts the detail instead, and the detail's own plane brushes no
  longer push points they can't bring to the plane.
testers:
  - A dense scan or sculpt with a micro level (2+), symmetry X on: Clay strokes across the brow where hair or mane meets it, ending on the mane; orbit: no spikes or shards at the stroke ends, on either side.
---
Seen on a 3M-base lion with a 48M micro level (hero footage). Test: micro_stroke_end_no_spikes_on_a_rough_surface.
