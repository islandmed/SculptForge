---
whats_new: **Keep Face Groups** (Tool > Remesh, under Voxel Remesh): the new surface keeps your face groups with hard
  borders. **Refresh the topology under the brush**: hold <kbd>N</kbd> and drag over stretched triangles to even them out
  at the Part's detail, no dialog, one undo step (on the background <kbd>N</kbd> still remeshes the whole Part). Requested by members.
highlight: Face groups survive Voxel Remesh; N-drag refreshes stretched topology
added: **Keep Face Groups** (Tool > Remesh, beside Keep Masked; off by default): Voxel Remesh carries the Part's face groups
  onto the new surface. Each new polygon takes the group of the original surface nearest its centre, the border is relaxed at
  the scale of the original triangles and stray polygons along it take their neighbours' group, so borders stay hard, clean lines. Quick Remesh and Remesh after
  each stroke follow the checkbox; command `remesh keep_groups=true`. Requested by a Founding Member.
added: **Refresh the topology under the brush**: with Quick Remesh on, hold <kbd>N</kbd> (the Quick Remesh drag key) and
  drag on the model to remesh only the stretched triangles under the brush at the Part's detail (dynamic topology's edge
  length when it is on, else the Part's own typical edge), as you drag, with no dialog. Edges are split and the new points
  relaxed along the surface; nothing is moved and nothing finer is coarsened. Masks, hidden faces, face group borders and
  symmetry are respected as in dynamic topology. One undo step per drag. On the background <kbd>N</kbd> remeshes the
  whole Part as before. Requested by members (a Founding Member and Rand_gogh).
testers:
  - Any OS, Tool > Remesh: on a Part with two or more face groups (Mask half, Face Groups > From Mask), tick Keep Face
    Groups, press Voxel Remesh. The groups are still there with a clean border; untick and remesh again: one group.
  - Any OS, SculptForge layout (Classic: tick Tool > Remesh > Quick Remesh first): pull a long tendril with Move, then hold N
    and drag over it. The long triangles split into even ones as you drag; the rest of the model is untouched; Ctrl+Z
    undoes the whole drag in one step. Hold N and click on the background: the whole Part remeshes as before.
  - Any OS: the same N drag on a Part with subdivision levels does nothing and the status bar says why.
  - Any OS, Symmetry on: the N drag on one side refreshes the mirror side the same way.
member_request: true
member_credit: Rand_gogh
---
Requesters: a Founding Member (anonymous by request, so "requested by a Founding Member") for Keep Face Groups and the
one-gesture refresh; Rand_gogh (credited by handle, as in the remesh-speed note) for the one-gesture refresh.
