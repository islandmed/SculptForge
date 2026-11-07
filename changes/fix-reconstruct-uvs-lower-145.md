---
whats_new: **Reconstruct Subdiv** keeps UVs on the lower levels of a high-poly that was unwrapped on its own: no more faces stretched across UDIM tiles, even with overlapping islands.
fixed: **Reconstruct Subdiv** (Tool > Geometry): the levels it builds keep each face inside one UV island when the
  imported high-poly's seams run through the lower level's faces (UVs made on the high-poly, automatic unwraps with
  overlapping islands). Such faces used to take each corner's UV from a different island and stretch across UDIM
  tiles. Reconstructing again on such a mesh also finds the right lower level now. Thanks @RodTDai for the report,
  the screenshots and the diagnosis that pointed to the fix (#145).
testers:
  - Any OS: import a high-poly OBJ with UDIM UVs made on the high-poly (or an auto-unwrap with overlapping islands),
    Tool > Geometry > Reconstruct Subdiv once or twice, open the UV view on each lower level: no faces stretch
    between islands or tiles; step back to the top level and its UVs are exactly as imported.
---
#145 reopened. The UV transfer follows @RodTDai's diagnosis and fix description on the issue (re-implemented here):
restrictUvs finds fine-face UV islands (union-find over edges whose ends match in both faces), and a coarse polygon
whose fine faces span islands keeps the one with most of them (ties: larger island, lower id); a corner of another
island takes the island's UV at that vertex if a fine face of it touches the vertex, else a least-squares affine
fit over the polygon's layout, averaged per (vertex, island). Polygons with no seam inside copy corners as before.
Second fix: the Catmull-Clark reading rejects edge points of valence != 4 (3 on borders) and face points not of
valence 3/4, and prefers fewer valence-3 face points before fewer face points on seams; with seams through faces
the swapped reading used to win (a second Reconstruct failed; on the creature-head starter even one level had 4
triangles wrong). Before/after UV sheets: ~/Work/.sf/uv145-shots/sheet-145.png.
