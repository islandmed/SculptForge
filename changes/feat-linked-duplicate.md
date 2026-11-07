---
whats_new: **Linked Duplicates**: **Tool > Parts > Linked Duplicate** makes a copy that shares the Part's mesh but
  sits in its own place. Sculpt, paint or mask any of them and all change; move, turn or scale each with the Gizmo.
  **Make Unique** gives one its own mesh again. Hundreds of copies cost about one mesh.
highlight: Linked Duplicates: sculpt one, every copy follows
added: **Linked Duplicates** (#94, asked for by ebarranco; the idea of placing copies on their own by NicTanghe):
  **Tool > Parts > Linked Duplicate** adds a Part sharing the active one's mesh, undo history and GPU buffers, with
  its own transform. Strokes, masking and painting on any copy edit the shared mesh (symmetry about the copy's own
  axes); the Gizmo and `transform` move a linked copy as a whole (a mask poses the shared mesh instead); **Make
  Unique** breaks the link; deleting one keeps the others. The Parts list shows a link and the count. Projects keep
  the links (project format 8); glTF reuses one mesh with a node per copy, USD writes instanceable prims referencing
  the first copy (and reads them back), OBJ / STL / FBX / PLY write each copy where it is. Commands
  `part_duplicate_linked` and `part_make_unique`. 500 linked copies use about the memory of one mesh.
testers:
  - Any OS: new sphere, Tool > Parts > Linked Duplicate three times. The rows show a link and 4; sculpt on the last
    copy: all four change, Ctrl+Z on the first undoes it.
  - Any OS: Gizmo (W / E / R) on a copy: only that copy moves, turns, scales; then sculpt on the turned copy, the
    stroke lands under the cursor; with X symmetry the mirrored dab lands on the copy's own other side.
  - Any OS: Make Unique on one copy, sculpt it: the others don't change. Delete the original: the copies stay.
  - Any OS: save, reopen: the links and places are back. Export GLB and USD and open them in another app: every
    copy is where it was.
  - Windows (low-end GPU): 100 linked copies of a 50k-triangle Part: orbiting stays smooth, and Edit >
    Preferences > Performance shows the GPU memory of about one Part.
---
Phase 2 (arrays of linked copies: linear, radial, along a curve) is planned in REPORT.md.
