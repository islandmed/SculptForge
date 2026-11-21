---
whats_new: **Shift with the Paint brush** never changes the shape any more, on any Part. On a Part that paints into a texture it smooths the colour in the texture (it used to smooth the shape and dent it).
fixed: Holding Shift with the Paint brush (Sculpt off) on a Part that paints into a texture smoothed the shape and dented it. Shift now only ever smooths colour with the Paint brush, on every Part; on a texture Part it blends the texels under the brush toward their neighbours, with Smooth's Intensity, mask, symmetry, falloff and the active layer, one undo step per stroke. With a sculpt brush (Paint off) Shift still smooths the shape. Reported by a member.
testers:
  - Windows, macOS or Linux: Paint on, Sculpt off, Paint into Texture on a spiky Part; hold Shift and drag over the spikes and a colour edge; the spikes keep their shape and the edge blends. Ctrl+Z takes the stroke back.
  - Same Part with Sculpt on and a sculpt brush: Shift still smooths the shape.
---
Reviewers: TexturePainter::smoothDab (src/core/Texture.cpp) runs the tile dab in two passes (compute from the texels before the dab,
then write) so a drag keeps smoothing and the result does not depend on the thread count. Colour only (no material channels).
Neighbours are the 3x3 texels inside UV islands; seams are not stitched. UiTest values texture_rough and moved_exact added.
