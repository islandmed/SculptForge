# Sculpt layers and blend shapes

Sculpt layers hold changes on top of the base mesh, each with its own weight: a smile, a blink, a corrective shape
or a variation you want to dial in and out. Because a layer is a set of per-vertex offsets, it exports straight
to game engines and DCC tools as a blend shape (morph target). This chapter covers creating, sculpting into,
weighting, organizing, baking, playing and exporting layers.

## How layers work

- Each layer stores how far every vertex moves from the base. The shape you see is the base plus every shown
  layer times its weight, all times the **Strength**.
- New strokes go into the **active** layer, or into the base mesh when **Base mesh** is selected.
- Layers belong to a [Part](parts.md): each Part has its own list of layers.
- A weight of 0 turns a layer off, 1 shows it as sculpted and -1 shows the opposite shape. Scripts can also set
  weights beyond -1..1 to exaggerate a shape.
- Layers work on every [subdivision level](subdivision-levels.md): see
  [Layers and subdivision levels](#layers-and-subdivision-levels).
- Layers survive [subdividing](geometry.md#subdivide), [decimating](geometry.md#decimate) and
  [LOD chains](geometry.md#lod-chains). A [voxel remesh](geometry.md#voxel-remesh) bakes them into the surface
  (the layers are gone afterwards).

> **Note:** Sculpt layers are not the **Layer** brush. The Layer brush raises the surface to a fixed height; sculpt
> layers are the blend-shape stack described here.

## The Layers sub-palette

Open **Tool > Layers** in the right tray.

![The Layers sub-palette with a base mesh and two blend shapes](images/layers.jpg)

From top to bottom:

- **Strength** (`all layers 1.00`): one slider for every layer of the Part, from 0 (the base mesh alone) to 2.
- **Base mesh**: select it to sculpt the base shape.
- One block per layer: an **eye** (show or hide), the layer's name (click to sculpt into that layer), **Play**,
  **X** (delete) and a **weight** slider from -1 to 1, with a mark at 0 in the middle.
- **Top**, **Up**, **Down** and **Bottom** for the selected layers; **Merge Down**, **Invert**, **Duplicate** and
  **Bake** for the active layer; then **Bake All** and **Clear All**.
- A name field and **+ Add** at the bottom.

Right-click a layer's name for **Rename**, **Move Up**, **Move Down**, **Move to Top**, **Move to Bottom**,
**Duplicate**, **Merge Down**, **Invert**, **Bake into Base** and **Delete** on that layer.

The Layers list highlights the active layer; in the Classic layout the title strip also shows it (`Layer Smile`,
or `Layer Base`).

## Add a layer and sculpt into it

1. Type a name in the field at the bottom of **Tool > Layers**, for example `Smile` or `BlinkL`.
2. Click **+ Add** (or press <kbd>Enter</kbd> in the field). The new layer becomes the active one.
3. Sculpt. Every stroke goes into the layer until you select another layer or **Base mesh**. Moving, scaling and
   rotating with the [Transform gizmo](gizmo.md) (or the `transform` command) goes into the layer too, masked or not.

A layer whose weight is 0 can't be sculpted into: SculptForge tells you to raise the weight first.

> **Tip:** Name layers the way your engine or rig expects (`jawOpen`, `BlinkL`): the names become the blend shape
> and morph target names in exported files. For a face, **Tool > Facial Shapes** makes all 52 ARKit shapes for you:
> see [Facial shapes and live face](facial-shapes.md).

## Adjust and preview weights

- Drag a layer's **weight** slider to blend it in and out. Left of the middle mark the weight is negative: the
  layer pushes where it pulled, so one "Smile" layer also gives a frown. Near the mark it snaps to 0. Ctrl+click the
  slider to type an exact value.
- Drag **Strength** to scale every layer at once, for example to tone down a whole set of correctives.
- **Play** animates the layer from 0 to 1 and back, over and over, so you can judge the motion. Click **Stop** to
  end it; the weight returns to 1. Starting a stroke also stops playback.

Each weight or Strength drag is one undo step. Weights, eyes and Strength are saved with the project, and
exports write each layer's weight as it shows (weight times Strength, 0 for a hidden layer).

## Show and hide layers

Click a layer's **eye** to hide it: the model shows without it, its name turns grey, and its weight is kept for
when you show it again. A hidden layer can't be sculpted into (SculptForge tells you to show it first) and exports
at weight 0. Showing and hiding is undoable.

## Organize layers

- **Reorder**: drag a layer by its name onto another layer to move it there, or select it and click **Up** or
  **Down**, or **Top** / **Bottom** to send it to the very top or bottom of the list. The order doesn't change the
  shape; it's the order in the list and of the blend shapes in exports.
- **Move several together**: <kbd>Shift</kbd>+click more layers' names to select them with the active one (an
  accent bar marks the active layer, the one strokes go into). **Top**, **Up**, **Down**, **Bottom** and dragging
  one of them move them all, in their order. A plain click selects one layer again.
- **Duplicate** copies the active layer just above it (`Smile copy`); the copy becomes the active layer, so you can
  try a variation and keep the original. Both show, so the shape doubles until you hide or lower one.
- **Rename**: double-click the name, type, press <kbd>Enter</kbd> (<kbd>Esc</kbd> cancels). The name is the
  blend shape's name in exported files.
- **Merge Down** puts the active layer into the one below it, each at its weight: the shape stays the same, and the
  merged layer keeps the lower layer's name with weight 1. Both layers must be shown (or both hidden).
- **Invert** negates the layer's offsets, as if you had sculpted it the other way. Inverting twice gives the layer
  back exactly.

## Bake and clear

- **Bake** puts the active layer into the base mesh as it shows now (its weight and the Strength) and removes the
  layer: the model looks the same, but the change is now part of the base. A hidden layer can't be baked; show it
  first.
- **Bake All** bakes every shown layer at once. Hidden layers stay as layers.
- **Clear All** deletes every layer of the Part, leaving the base mesh.

Each of these is one undo step, and so is every operation above.

## Delete a layer

Click **X** next to the layer. Its offsets are removed from the mesh and the base mesh becomes the active target.
Deleting a layer is undoable.

## Layers and subdivision levels

With [subdivision levels](subdivision-levels.md) a layer works the way 3D Layers do: record it at the
top level for the fine detail of an expression, and it stays with the Part whatever level you work on.

- **Each layer has its own detail on every level.** Step down with **Lower Level** (<kbd>Shift</kbd>+<kbd>D</kbd>)
  and a lower level shows the broad part of the layer, exactly as it would show the same strokes made in the base.
  Step back up and the layer is there as you recorded it, to the last vertex.
- **Weights are shared by all levels.** Turn a layer off (weight 0) or blend it at a low level and the top shows
  the same when you go back up; the slider and **Play** work at every level.
- **Sculpt into a layer at any level.** At a low level a stroke into a layer makes a broad change that the
  higher levels follow smoothly; the detail recorded above it stays. A layer added at a low level shows on every
  level above it too.
- **Reshape the base at a low level** (or turn it with the [Gizmo](gizmo.md)) and each layer's detail rides on
  the new shape: its offsets turn with the surface instead of pointing the old way.
- **Divide** carries the layers to the new level. **Reconstruct Subdiv** keeps them, exact at the top. **Delete Lower**
  and **Delete Higher** keep the current level's layers as they are.
- **Deleting, reordering, renaming, merging, inverting, baking and clearing** work on every level at once and
  exactly: bake a layer at SDiv 1 and the top level looks just as it did before, with the layer now in its base.
  The eye and Strength are shared by all levels, like the weights. Undo steps back through strokes, level changes and divisions
  with their layers.
- **Projects** keep every level's layers. **Exports** write the current level with its layers as blend shapes, so
  export from the top for the game mesh; `subdiv_export path=face_low.glb level=1` writes the base cage with its
  blend shapes instead.

The tools that rebuild the mesh (Voxel Remesh, Quad Remesh, Decimate, Subdivide) work on the current level and drop
the levels, as described in [Subdivision levels](subdivision-levels.md); Voxel Remesh and Quad Remesh bake the layers
into the surface.

A **micro level** doesn't sit on sculpt layers yet: **Bake All** (or delete the layers) before you divide to a micro
level, and delete the micro level before you add a layer again.

## Export blend shapes

| Format | What layers become |
|---|---|
| glTF / GLB (**File > Export GLB...**, <kbd>Ctrl</kbd>+<kbd>E</kbd>) | Morph targets |
| FBX | Blend shapes |
| USD / USDZ | UsdSkel blend shapes, with their current weights |
| Send to Unreal / Export FBX for Unreal | A skeletal mesh with morph targets |
| LOD chain | Blend shapes are kept on every LOD |
| Project (`.sfproj`) | Everything: layers, weights and the active layer |
| OBJ, PLY, STL | The current blended shape only |

Importing works the other way: glTF morph targets and FBX or USD blend shapes open as sculpt layers. See
[Exporting](exporting.md).

## Scripting

| Command | What it does |
|---|---|
| `layer_add name=Smile` | Add a layer and make it active |
| `layer_set_active index=0` | Choose the layer strokes go into (`-1` = base mesh) |
| `layer_set_weight index=0 weight=0.5` | Set a layer's weight (-1 = the opposite shape; beyond -1..1 exaggerates) |
| `layer_set_visible index=0 visible=false` | Hide (or show) a layer |
| `layer_strength strength=0.5` | Scale every layer of the Part (0 to 2) |
| `layer_move index=2 to=0` | Move a layer (`direction=-1` / `1`: one place up / down; `place=top` / `bottom`; `indices=0,2`: several together) |
| `layer_duplicate index=0` | Copy a layer just above it |
| `layer_rename index=0 name=jawOpen` | Rename a layer |
| `layer_merge_down index=0` | Merge a layer into the one below it |
| `layer_invert index=0` | Negate a layer's offsets |
| `layer_bake index=0` | Bake a layer into the base mesh (`all=true`: every shown layer) |
| `layer_clear` | Delete every layer |
| `layer_remove index=0` | Delete a layer |
| `layer_info` | List the layers: name, weight, shown or hidden, Strength |

Layers are addressed by index, starting at 0 for the top layer.

```text
layer_add name=jawOpen; stroke points="0,-0.3,1 0,-0.5,1" brush=Inflate; export_glb path=head.glb
```

## See also

- [Parts](parts.md)
- [Subdivide, remesh, decimate and LODs](geometry.md)
- [Exporting](exporting.md): glTF morph targets, FBX blend shapes, Send to Unreal
