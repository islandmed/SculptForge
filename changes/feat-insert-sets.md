---
whats_new: **Insert sets**: **Parts > Append > Insert on the model > Insert set from file...** or **from folder...** loads a
  kitbash set (a file with several objects, or a folder of meshes); pick one in its thumbnail window and drag it onto the
  model, `[` and `]` step through the set.
highlight: Kitbash sets for Insert
added: **Insert sets** (Parts > Append > Insert on the model): a model file with several objects (`.obj`, `.glb`) or a
  folder of `.obj` / `.ply` / `.glb` files becomes a set to insert from, one item at a time, with a thumbnail picker;
  `[` and `]` step to the previous / next item while inserting. Symmetry, undo and the up axis work as with one mesh.
  Commands `insert_mesh set=<path> index=<n>` and `insert_set_items set=<path>`. Asked by a viewer on askNK's video.
testers:
  - Any OS: Parts > Append > Insert on the model > Insert set from folder..., pick a folder with a few .obj / .glb files;
    the Insert set window shows a thumbnail per file. Click one, drag on the model: that mesh is inserted. Press ] and
    drag again: the next one. Ctrl+Z takes one insert back at a time.
  - Windows: the same with a .glb holding several objects (Insert set from file...); names under the thumbnails are the objects'.
---
Saving sets in projects / .sfpack (brush packs shipping kitbash sets) and showing them in the Library are follow-ups.
