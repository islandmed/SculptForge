# Sculpt layers and blend shapes

Sculpt layers hold changes on top of the base mesh, each with its own weight: a smile, a blink, a corrective shape
or a variation you want to dial in and out. Because a layer is a set of per-vertex offsets, it exports straight
to game engines and DCC tools as a blend shape (morph target). This chapter covers creating, sculpting into,
weighting, playing and exporting layers.

## How layers work

- Each layer stores how far every vertex moves from the base. The shape you see is the base plus every layer
  times its weight.
- New strokes go into the **active** layer, or into the base mesh when **Base mesh** is selected.
- Layers belong to a [Part](parts.md): each Part has its own list of layers.
- A weight of 1 shows a layer as sculpted (new layers start there), 0 turns it off and -1 turns it the opposite
  way. **All layers** scales every layer's weight at once, and the eye hides a layer without losing its weight.
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

- **All layers**: one strength for every layer of the Part, from -1 to 1 (1 = as sculpted, 0 = all off).
- The list: **Base mesh** (select it to sculpt the base shape), then one row per layer with its name, its weight,
  **Play** and the eye (show / hide). Click a layer to sculpt into it; <kbd>Ctrl</kbd>+click adds layers to the
  selection and <kbd>Shift</kbd>+click selects a run of them; double-click renames; right-click for the layer's menu.
- **Weight**: the active layer's weight, from -1 to 1.
- The arrows move the active layer up or down; **Merge down**, **Duplicate** and **Invert** work on the active layer.
- **Bake** and **Delete** work on the selected layers (or the active one); **...** has Move to top, Move to bottom,
  Rename, **Bake all layers** and **Delete all layers**.
- A name field and **+ Add** at the bottom.

The Layers list highlights the active layer (and, more lightly, the other selected ones); in the Classic layout the
title strip also shows it (`Layer Smile`, or `Layer Base`). Every one of these actions is one undo step, and the
project keeps the order, names, weights, hidden layers and the strength for all layers.

## Add a layer and sculpt into it

1. Type a name in the field at the bottom of **Tool > Layers**, for example `Smile` or `BlinkL`.
2. Click **+ Add** (or press <kbd>Enter</kbd> in the field). The new layer becomes the active one.
3. Sculpt. Every stroke goes into the layer until you select another layer or **Base mesh**. Moving, scaling and
   rotating with the [Transform gizmo](gizmo.md) (or the `transform` command) goes into the layer too, masked or not.

A layer whose weight is 0 (or that is hidden, or with **All layers** at 0) can't be sculpted into: SculptForge
tells you to raise the weight first.

> **Tip:** Name layers the way your engine or rig expects (`jawOpen`, `BlinkL`): the names become the blend shape
> and morph target names in exported files. For a face, **Tool > Facial Shapes** makes all 52 ARKit shapes for you:
> see [Facial shapes and live face](facial-shapes.md).

## Adjust and preview weights

- Drag the **Weight** slider to blend the active layer in and out; below 0 it pushes the other way.
- Click the eye to hide a layer and again to show it; its weight is kept.
- Drag **All layers** to dial every layer down (or past 0, the other way) at once.
- **Play** animates the layer from 0 to 1 and back, over and over, so you can judge the motion. Click **Stop** to
  end it; the weight returns to 1. Starting a stroke also stops playback.

Weights are saved with the project and used by exports that keep weights (USD writes the current weights). Exports
write each blend shape's weight as it shows: a hidden layer at 0, every weight times **All layers**.

## Organize layers

- **Move**: the arrows move the active layer up or down the list; **...** > **Move to top** / **Move to bottom**.
  The order is how the list reads and the order blend shapes export in; the shape doesn't change.
- **Rename**: double-click the name (or **...** > **Rename**), type, press <kbd>Enter</kbd>. Names stay unique in a Part
  (a second `Smile` becomes `Smile 2`).
- **Duplicate** copies the active layer, offsets and settings, to a new layer below it.
- **Merge down** folds the active layer into the one below it, keeping how both look: the merged layer gets weight 1
  and the name of the lower layer.
- **Invert** turns the active layer the opposite way: bumps become dents.

## Bake or delete layers

- **Bake** puts the selected layers into the base mesh exactly as they show now (weight, hidden, **All layers**),
  then removes them: the shape doesn't change. **...** > **Bake all layers** does it for every layer.
- **Delete** removes the selected layers and their offsets; **...** > **Delete all layers** removes every layer.
- Select several with <kbd>Ctrl</kbd>+click or <kbd>Shift</kbd>+click first; the buttons then read **Bake 2**,
  **Delete 2**. With nothing else selected they act on the active layer.

Every bake and delete is one undo step, and the base mesh becomes the active target.

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
- **Add Level** carries the layers to the new level. **Rebuild Lower Levels** keeps them, exact at the top. **Remove Below**
  and **Remove Above** keep the current level's layers as they are.
- **Deleting, baking, merging, duplicating, inverting, renaming or moving a layer** works on every level, at
  whatever level you are: each level keeps its own detail of every layer. Undo steps back through strokes, level changes and divisions
  with their layers.
- **Projects** keep every level's layers. **Exports** write the current level with its layers as blend shapes, so
  export from the top for the game mesh; `subdiv_export path=face_low.glb level=1` writes the base cage with its
  blend shapes instead.

The tools that rebuild the mesh (Voxel Remesh, Quad Remesh, Decimate, Subdivide) work on the current level and drop
the levels, as described in [Subdivision levels](subdivision-levels.md); Voxel Remesh and Quad Remesh bake the layers
into the surface.

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
| `layer_set_weight index=0 weight=0.5` | Set a layer's weight live (not an undo step; negative or above 1 exaggerates) |
| `layer_set index=0 weight=-0.5 visible=false name=Frown` | Change a layer's weight (-1 to 1), visibility or name (undoable) |
| `layer_strength strength=0.5` | The strength for all layers, -1 to 1 |
| `layer_move index=0 direction=down` | Move a layer `up`, `down`, `top` or `bottom` |
| `layer_duplicate index=0` | Copy a layer to a new layer below it |
| `layer_merge_down index=0` | Merge a layer into the one below it |
| `layer_invert index=0` | Turn a layer the opposite way |
| `layer_bake indices=0,2` | Bake layers into the base mesh (`indices=all` for every layer) |
| `layer_remove_many indices=all` | Delete several layers, or all of them |
| `layer_remove index=0` | Delete a layer |

Layers are addressed by index, starting at 0 for the top layer.

```text
layer_add name=jawOpen; stroke points="0,-0.3,1 0,-0.5,1" brush=Inflate; export_glb path=head.glb
```

## See also

- [Parts](parts.md)
- [Subdivide, remesh, decimate and LODs](geometry.md)
- [Exporting](exporting.md): glTF morph targets, FBX blend shapes, Send to Unreal
