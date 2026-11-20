---
whats_new: **Paint after Smooth**: the Paint brush colours again when Smooth was the brush before it; strokes no longer blend instead of painting.
fixed: **Paint brush smoothing instead of colouring**: with Smooth picked (S, or its shelf tile) and then Paint (top shelf Paint on, Sculpt off, or P), strokes blended the colour (or smoothed the shape) and never laid any, until another brush was picked. Paint now colours whatever brush came before it; holding Shift still blends the colour.
testers:
  - Any OS, mouse or pen: pick Smooth, then turn Paint on (Sculpt off); drag over the model, it colours and the shape stays put. Hold Shift to blend the colour.
---
Regression tests tests/ui/paint_after_smooth.sfui and paint_pen.sfui.
