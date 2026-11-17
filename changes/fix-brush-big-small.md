---
whats_new: **Big brushes behave**: a brush bigger than the form it works on no longer shreds it. Clay, Inflate, Sharp Crease, Trim and Move keep a clean surface at any Size, with Symmetry on or off.
fixed: **Brushes bigger than the model** (thanks phoenix on Discord): with the brush 2-4 times the size of the Part, Inflate pulled shards, Move pulled a spiky cone, Sharp Crease long wings, Clay a rocky crumple and Trim a crater. The brush's plane is now read from the side facing the camera (it averaged the two sides of the form into noise), plane brushes leave surface turned away from them alone, a brush's depth no longer grows past the Part's own size, and Move with Symmetry no longer moves a vertex twice where the two sides' brushes overlap. Brushes up to half the Part's size are unchanged.
testers:
  - Any OS: New sphere, Symmetry on, Size so the brush ring is 3x the sphere; a few strokes each of Inflate, Clay, Sharp Crease, Trim and Move. The sphere stays one smooth form (Move shears it as a whole), no shards or spikes.
  - Any OS: the same with Front faces only on; a smaller brush (a quarter of the sphere) still leaves a visible mark in one stroke.
---
Cause per symptom: the dab's area normal and centre were summed over the whole region, both sides of a form smaller than the brush (it cancelled
to noise after a few strokes: normal turned away from the surface, plane through the middle of the form); every depth scaled with the brush's radius,
so a brush 4x the Part moved it 4x as far per dab; Move added each mirror copy's move on vertices both held. Not changed: Front faces only still tears a
little along the silhouette of a brush 2x or more the form, and Clay Block 1-2x the form is rough as before.
