---
whats_new: **Shift with the Paint brush** never changes the shape any more, on any Part. On a Part that paints into a texture it smooths the colour in the texture (it used to smooth the shape and dent it).
fixed: Holding Shift with the Paint brush (Sculpt off) on a Part that paints into a texture smoothed the shape and dented it. Shift now only ever smooths colour with the Paint brush, on every Part; on a texture Part it blends the texels under the brush toward the texels around them (about a mesh triangle's width, so a stroke smears as far as it does on vertex colours; never across into another UV island), with Smooth's Intensity, mask, symmetry, falloff and the active layer, one undo step per stroke. With a sculpt brush (Paint off) Shift still smooths the shape. Reported by a member.
testers:
  - Windows, macOS or Linux: Paint on, Sculpt off, Paint into Texture on a spiky Part; hold Shift and drag over the spikes and a colour edge; the spikes keep their shape and the edge blends. Ctrl+Z takes the stroke back.
  - Same Part with Sculpt on and a sculpt brush: Shift still smooths the shape.
---
Reviewers: TexturePainter::smoothDab (src/core/Texture.cpp) runs the tile dab in two passes (compute from the texels before the dab,
then write) so a drag keeps smoothing and the result does not depend on the thread count. Colour only (no material channels). Floor (4 threads) 4.8 ms per dab at a 0.08 radius, 19 ms at 0.25, 4K texture on 1.3M triangles.
The blur's radius is the mesh's mean triangle edge in texels (box blur from per-island summed-area tables, shrinking toward an island's border); seams are not stitched, so a sharp colour edge crossing a seam keeps a thin sharp sliver there. UiTest values texture_rough and moved_exact added.
