---
whats_new: Fewer shards when sculpting rough micro-level surfaces (Clay strokes ending where hair or strands meet skin). Clay over rough strands can still leave small steps; a fuller fix is coming.
fixed: Micro levels: fewer spiky shards from Clay (and the other plane brushes: Clay Block, Flatten, Trim) where a
  stroke's dabs pile up over rough, folded surface such as strands meeting skin, also on the symmetric side. A big
  brush's dab that would fold the base mesh over now sculpts the detail instead, and the detail's own plane brushes no
  longer push points they can't bring to the plane. Clay over rough, leaning strands can still leave small steps; a
  fuller fix is coming.
testers:
  - A dense scan or sculpt with a micro level (2+), symmetry X on: Clay strokes across the brow where hair or mane meets it, ending on the mane; orbit: no shards at the stroke ends on either side (small steps over leaning strands are known).
---
Seen on a 3M-base lion with a 48M micro level (hero footage). Test: micro_stroke_end_no_spikes_on_a_rough_surface.
Steps over leaning strands: the micro level stores heights only, so its Clay moves points along their own normals;
the fuller fix (move the base, then resample the detail against it) is the next ticket.
