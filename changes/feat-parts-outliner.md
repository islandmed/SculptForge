---
whats_new: **Select several Parts and folders** (**Tool > Parts**): Ctrl+click and Shift+click rows and folder headers,
  then hide, show, **Solo** or drag them into a folder all at once. Asked for by @Ygrekie.
whats_new: **Move faces here**: right-click a Part > **Move faces here** to give it the active Part's masked, hidden,
  visible or unmasked triangles, in one undo step.
whats_new: **Part origin** (**Tool > Parts > Origin**): set a Part's origin to its bounds centre, centre of mass, the
  world centre or the gizmo's pivot; the gizmo turns about it and shows it. **Move to...** puts the Part at the world
  centre, or only onto a symmetry plane before Mirror And Weld. Asked for by Devon on Discord.
whats_new: **Snap menu**: a list at the cursor for the origin, the Part and the gizmo's pivot (bounds centre, world
  centre, gizmo pivot, symmetry plane). Shift+S with the Blender-style keys, any key you like in the others.
whats_new: **Mirror copy**: right-click a Part > **Mirror copy** for a copy mirrored across the symmetry plane, in one
  undo step: sculpt one ear, mirror-copy it for the other. Asked for by Oslen (osuren_) on Discord.
whats_new: **Copies are numbered**: duplicating Head makes Head.01, then Head.02, instead of "Head copy copy" (Parts,
  paint layers and texture layers). Asked for by Oslen (osuren_) on Discord.
highlight: Select several Parts and move faces between Parts
added: Parts list selection (Tool > Parts; `part_select add=true` / `range=true`, `part_select_folder`,
  `part_select_none`): Ctrl+click adds or takes out a Part, Shift+click selects from the active Part, Ctrl or
  Shift+click on a folder's header selects its Parts. **Hide** / **Show** / **Clear** under the list
  (`part_selection_visibility`), **Solo** shows the selection, dragging a selected row onto a folder moves all of them
  (`part_set_folder selection=true`) and the folder button puts them in a new folder. Thanks @Ygrekie.
added: **Move faces here** (right-click a Part in the list; `part_move_faces what=masked|hidden|visible|unmasked
  to=<Part>`): the active Part's triangles join another Part, keeping UVs, face groups and vertex paint. One undo
  step on the active Part also gives the other Part back its mesh and its own undo history. Thanks @Ygrekie.
added: **Part origin** (Tool > Parts > Origin; `part_origin set=bounds|mass|world|point|none`, `part_to_world
  axis=all|x|y|z`): a per-Part origin, saved in projects, that the gizmo pivots on (marked with a ring and cross) and
  that moves with whole-Part transforms. Move to the world centre or onto the X, Y or Z symmetry plane moves all of
  the Part, hidden faces and sculpt layers included. One undo step each. Thanks Devon on Discord.
added: **Snap menu** (`transform.snap_menu`; Shift+S with the Blender-style keys, where Lazy keeps L): origin to bounds
  / world / gizmo pivot, Part to world centre / onto the X plane, gizmo pivot to what moves / origin / world centre
  (new app command `gizmo_pivot to=selection|origin|world|point`). Thanks Devon on Discord.
added: **Mirror copy** (right-click a Part in the list, or **Mirror Copy** in the command palette;
  `part_mirror_copy axis=x|y|z pivot=world|origin`): Duplicate and Mirror in one undo step, the copy named
  as Duplicate names it, right below the Part. The copy is the current subdivision level (its other levels aren't copied;
  the source keeps them). Thanks Oslen (osuren_) on Discord.
changed: Duplicate names copies `<name>.01`, `.02`, ... (a copy of Head.01 is Head.02) instead of adding " copy", for
  Parts, paint layers and texture layers (core/CopyName.h). Thanks Oslen (osuren_) on Discord.
testers:
  - Any OS: three Parts; click one, Ctrl+click another, Shift+click a third: the rows are highlighted and the line under
    the list says "3 selected". Hide, Show, Solo (and Solo again), Clear.
  - Make a folder, Ctrl+click its header: its Parts are selected too. Drag a selected row onto another folder: all of
    them move.
  - macOS: Cmd+click adds to the selection (as Ctrl does on Windows and Linux).
  - Mask half a sphere, right-click a cube's row > Move faces here > Masked: the half joins the cube. Ctrl+Z once: the
    half is back and the cube is as it was, with its own undo steps.
  - Origin: move a sphere off to the side (gizmo, W), Tool > Parts > Origin > Set... > Centre of mass: the ring and
    cross sit in its middle and the gizmo turns about it. Move to... > Onto the X symmetry plane: it sits across X = 0.
    Ctrl+Z once for each. Save, reopen: the origin is still there.
  - Move a sphere to X = 2, right-click its row > Mirror copy: "Sphere.01" sits at X = -2, shaded right (not
    inside out). Ctrl+Z once: it's gone. With subdivision levels: the copy is the level you were on.
  - Duplicate a Part three times (Ctrl+Shift+D): Head.01, Head.02, Head.03; duplicate Head.02: Head.04. Same in Paint
    Layers and Texture Map layers.
  - Linked copies: Linked Duplicate a sphere, set the copy's origin (Set... > Bounds centre): the ring sits on the copy,
    not on the first sphere. Mirror copy the linked copy: the mirror lands opposite it.
  - Blender-style keys: Shift+S over the viewport opens the Snap menu at the cursor; each item does what it says.
---
GitHub discussion #158 by @Ygrekie: an outliner-style Parts panel (multi-select, hide / show / solo the selection,
move geometry between Parts, drag the selection into folders). Discord #ideas 1568632010023378944 by Devon:
Part origin controls; 1568633874546823201: the Snap menu. Oslen (osuren_) on Discord via sfmonitor, 11-07: Mirror
copy; #ideas 1568689069037199501: numbered copy names.
