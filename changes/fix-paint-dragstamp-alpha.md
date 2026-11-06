---
whats_new: **Paint with alphas**: vertex paint now follows the brush alpha with every stroke (Drag Stamp, Dots, Spray), so a Cracks stamp paints cracks instead of a soft round spot. Thanks @fxrtst.
fixed: Vertex paint ignored the brush alpha: Drag Stamp, Slide Stamp, Dots and Spray painted a soft round blob with any
  alpha (texture painting already followed it). The paint dab now samples the alpha on the view plane, turned as sculpt
  dabs turn it, and reaches into the stamp's corners (#152, thanks @fxrtst).
testers:
  - Windows: Paint, Rgb, stroke Drag Stamp, alpha Cracks, X symmetry, on the default sphere: drag a stamp; both sides show the crack pattern, not a round red spot.
  - Any OS: same with Dots and Spray; Alt still paints back to white in the alpha's shape.
---
Root cause: paintDab (core/VertexPaint.cpp) only used brushFalloff and never read DabParams::alpha; the 0.11.8 Drag
Stamp pressure change (#146) was unrelated. Vertex density is not the limit: on the 163.8K sphere a 60 px+ stamp spans
dozens of vertices per side, enough to show Cracks (finer detail than the vertex spacing still blurs; texture paint or
Divide for that).
