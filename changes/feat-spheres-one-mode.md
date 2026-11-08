---
whats_new: **Forge Spheres in one mode**: in **Tool > Forge Spheres > Draw**, click a sphere to grow a new one, drag it to move it, drag near its edge to resize it. The cursor and a small tag show which a drag will do.
highlight: Forge Spheres: draw, move and resize without switching modes
changed: **Forge Spheres** Draw builds without switching modes: a click on a sphere grows a new one, a drag on its body moves it and a drag on its rim resizes it (the cursor and a tag beside it say which); each drag is one undo step. Move and Scale work as before, and Drag places child (or Ctrl+drag) still grows and places a new sphere in one drag. Thanks @tristratos (#167)
testers:
  - Windows (mouse): Tool > Forge Spheres > New, Draw. Hover a sphere's middle: move cursor + "drag: move" tag; near its edge: resize cursor + "drag: resize". Click adds an arm; drag the arm to move it; drag its edge out/in to resize; Ctrl+Z undoes one drag at a time.
  - Any OS with a pen: a tap on a sphere adds one (no accidental moves from a small wobble).
---
Thanks @tristratos (#167). The old "drag sizes the new sphere" Draw drag is replaced by click-to-add + rim-drag resize.
