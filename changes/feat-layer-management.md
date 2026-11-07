---
whats_new: **Layer management** (**Tool > Layers**): drag layers to reorder them (or **Up** / **Down** / **Top** /
  **Bottom**; Shift+click to move several together), **Duplicate**, double-click to rename, **Merge Down**, **Invert**, an eye to show or hide each layer, weights from -1 to 1 (0 marked in the
  middle), one **Strength** slider for all layers, **Bake** one layer or **Bake All** into the base mesh, and
  **Clear All**. Every one is undoable, saved in projects and exact on every subdivision level. Thanks @foirgairwolf.
highlight: Layer management: reorder, rename, merge, invert, bake, show/hide
added: **Sculpt layer management** (Tool > Layers; requested by @foirgairwolf): reorder by dragging a layer's name
  (and **Up** / **Down**, **Top** / **Bottom**; Shift+click names to move several together), **Duplicate** (a copy
  just above), rename by double-click, **Merge Down** (each layer at its weight, the shape stays),
  **Invert** (negates the offsets), weights from -1 to 1 (negative = the opposite shape; 0 marked on the slider),
  an eye per layer (hidden keeps its weight), **Strength** for all layers of a Part (0-2), **Bake** (the active
  layer) and **Bake All** (every shown layer) into the base mesh, and **Clear All** in one undo step; right-click a
  layer for the same. All undoable (a weight or Strength drag is one small step), saved in projects (older
  versions open them, showing hidden layers), exact on every subdivision level, kept beside a micro level, and
  exported as shown (glTF / FBX / USD weights: negative as set, hidden at 0). Commands: `layer_move` (`place=top|bottom`, `indices=` for several),
  `layer_duplicate`,
  `layer_rename`, `layer_merge_down`, `layer_invert`, `layer_set_visible`, `layer_strength`, `layer_bake`,
  `layer_clear`, `layer_info`; `layer_set_weight` is undoable now.
testers:
  - Any OS: Tool > Layers, add three layers and sculpt into each; drag the bottom one's name onto the top one: the
    order changes, the model doesn't. Ctrl+Z puts it back.
  - Shift+click two layers, click Bottom: both go to the bottom in their order; Duplicate makes "<name> copy" above.
  - Drag a weight slider left past the middle mark: the layer turns into its opposite; release, Ctrl+Z: back to the
    old weight in one step.
  - Click a layer's eye: it hides (name greyed); strokes into it are refused with a message; save, reopen: still hidden.
  - With 3 subdivision levels, Bake a layer at SDiv 1, then step up to the top: the shape is the same as before baking.
  - Export GLB with a layer at -0.5 and one inverted; open it in Blender: shape keys at -0.5 and the inverted shape.
---
Branch feat/layer-management. Discord #ideas msg 1568523845441163265.
