---
changed: Nothing a user sees: the UI test for the view after Undo while a stroke is still finishing no longer fails now and then on slow
  machines. It asked whether the view was within half of the undone stroke's difference from the saved view, but a stroke running shades the
  whole model a little differently; it now asks whether the view is nearer the saved view than the undone stroke's (`mark_view other`,
  `view_nearer_mark` in the UI test harness).
---
Cause, from the three failing GitHub runs (same 0.0595 of 0.1145 each time): after the undo the view is the saved one exactly; the next
stroke's first batches then add a whole-model shading change (0.0446 here, independent of where that stroke lands) that the old bound
sometimes sampled. No race in async stroke + undo + view: the undone stroke is never shown after its undo.
