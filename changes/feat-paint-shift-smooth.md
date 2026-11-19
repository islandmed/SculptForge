---
whats_new: **Smooth colour**: with the Paint brush, hold **Shift** to blend the vertex colours under the brush into their neighbours. The shape stays put. Requested by a member.
added: **Smooth colour** (Paint brush, hold Shift): blends the vertex colours under the brush toward the average of
  their neighbours, with Smooth's own Intensity. Mask, symmetry, falloff, the active paint layer and one undo step per stroke work as for painting. The shape is
  never touched. With a sculpt brush (Rgb on), Shift still smooths the shape; a Part that paints into a texture keeps
  the shape smooth for now.
testers:
  - Paint a noisy patch of colour (Paint brush, P), then hold Shift and stroke over it: the colours melt together and the shape does not change.
  - Mask half the patch and Shift-stroke across it: the masked half keeps its colour. Ctrl+Z undoes the whole stroke.
---
Requested on the queue ("Smooth / blur vertex colours") and in #show-your-sculpts. Texture colour blur is not in this
step: the texel tile path has no neighbour read yet.
