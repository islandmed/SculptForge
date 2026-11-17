---
whats_new: **Insert on a mesh with subdivision levels**: Append > Insert on the model no longer asks you to Freeze Levels first. The piece becomes a new Part beside the mesh, and every level keeps its detail. Requested by a member.
changed: **Append > Insert on the model** works on a mesh with subdivision levels (it used to stop with "Dynamic topology needs a mesh without subdivision levels"). The inserted piece (with its mirrored copies) is added as a new Part next to the active one, so no level is frozen or changed; the status line says so, and one Undo takes the Part away. Merge Parts joins it to the mesh after Freeze Levels, if you want it in one mesh (requested by a member).
testers:
  - Any OS: Subdivide a sphere twice (Tool > Subdivision), then Parts > Append > Insert on the model > Cylinder and drag on it. A new Part appears, the status line mentions the levels, and Level 1 / 2 / 3 still show their detail. Undo removes the Part.
  - Any OS: the same with Symmetry on X; one mirrored copy sits in the same new Part.
---
Anonymous request (credit: "requested by a member" only). Part-not-freeze chosen because Freeze Levels drops every level but the shown one.
