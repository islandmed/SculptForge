---
fixed: On a mesh of millions of triangles, the creases and fine detail around the brush could fade out and pop back as a
  stroke began (they looked like strokes appearing beside the brush): the view dropped to its lighter preview of the mesh
  from the moment the pen went down until the stroke's first dabs showed. With the view still, a stroke now keeps the full
  detail from its first frame; the lighter preview is only for turning the view (#184, reported by @HG4Science).
testers:
  - Any OS, a head subdivided to 10 million triangles or more, the view still: draw short Sharp
    Crease strokes with X symmetry on, also right after an undo; the creases already there stay put and sharp as each
    stroke starts and ends (no fading, no strokes appearing beside the brush).
  - The same mesh: turn the view; it may show the lighter preview while turning, and the full detail once it stops.
---
Cause (gh#184, clip 1 framed): while the pen was down, fine creases 100-250 px from the brush vanished and came back in
whole-region steps (the region's mean luma moved with them), with no dab there. Renderer::draw dropped every over-budget
mesh to its coarse levels whenever `settings.stroking` was set, until the first stroke upload marked clusters pending
(then all in full again). On a 10M mesh the dabs go to the stroke thread, so the first upload takes frames (longer after
an undo), and the coarse frames in between showed the stroke start as creases fading/appearing. A still-view stroke now
takes the at-rest decision (full detail when one full frame fits the still budget, as before at rest).
Test: test_render_clusters coarse_levels_not_drawn_for_a_stroke_with_the_view_still (the decision is now
render/Clusters coarseDraw) fails with main's rule (3 checks), passes here. A UI test can't catch the window: the
harness lands the first upload before it draws a stroke frame. Draft strokes (flat shading of moved clusters) were the
other candidate: off by default (no tier sets them, they need Predictive Strokes).
