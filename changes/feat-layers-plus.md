---
whats_new: **Sculpt layer tools**: **Tool > Layers** now moves layers up and down (or to the top and bottom), renames with a double-click, duplicates, merges down, inverts, shows and hides with the eye, and bakes or deletes the selected layers (Ctrl+click, Shift+click) or all of them. Weights go from -1 to 1, and **All layers** sets one strength for every layer.
highlight: Sculpt layer tools: reorder, merge, invert, hide, bake and a strength for all layers (thanks foirgairwolf on Discord)
added: **Sculpt layer tools** (Tool > Layers; the `layer_move`, `layer_set`, `layer_strength`, `layer_duplicate`,
  `layer_merge_down`, `layer_invert`, `layer_bake` and `layer_remove_many` commands): move a layer up, down, to the
  top or the bottom; rename it (double-click); duplicate it; merge it down keeping how both look; invert it; show or
  hide it with the eye (its weight is kept); select several with Ctrl+click or Shift+click and bake them into the base
  mesh as they show, or delete them; Bake all layers and Delete all layers. Every action is one undo step and works on
  every subdivision level, keeping each level's detail of every layer. Idea by foirgairwolf on Discord.
added: **All layers** strength (Tool > Layers): one strength from -1 to 1 for every sculpt layer of a Part, on top of
  each layer's own weight. Saved in the project with hidden layers; older projects open with every layer shown and the
  strength at 1.
changed: A sculpt layer's weight slider goes from -1 to 1 (it was 0 to 1): below 0 the layer pushes the other way. 1 is
  still "as sculpted" and new layers start there. Dragging it, and the new All layers slider, is one undo step.
changed: The Layers panel is a tidy list like Paint Layers: name, weight, Play and the eye on one row per layer, with
  the actions below it. Exports write each blend shape's weight as it shows (a hidden layer at 0).
testers:
  - Any OS: Tool > Layers, add three layers and sculpt a stroke into each; hide one with the eye, drag All layers to 0 and back; each change is one Ctrl+Z.
  - Any OS: Ctrl+click two layers, Bake 2: the model doesn't change and one layer is left; Ctrl+Z brings both back.
  - Any OS with subdivision levels (Ctrl+D three times): record a layer at the top, go to level 1, Invert or Merge down there, go back to the top: the fine detail is still there.
  - Windows at 150% scaling: the Layers panel reads cleanly with 8+ layers (the list scrolls); double-click a name to rename it.
  - Any OS: save, reopen: order, names, hidden layers and All layers come back; an older project opens unchanged.
---
Default weight kept at 1 (= as sculpted; 0 = off): the brief's "default 0" would have switched every new layer off, so
the range grew to -1..1 without changing what the default means.
