---
whats_new: **Clean creases on micro levels**: **Sharp Crease**, **Crease** and **Pinch** at full strength no longer
  leave random noise on a micro level. The title bar counts the micro triangles beside the triangles, and **Wire**
  shows the level below the micro level on the micro surface.
fixed: **Micro levels: Sharp Crease, Crease and Pinch** at high intensity left prominent random noise. They
  now pull the height profile together the way the brushes pull a mesh, resampling the heights from where the pull
  brings the surface, so nothing grows from dab to dab; big brushes stay clean too (#125, reported by RodTDai).
fixed: **Wire** on a micro level showed nothing; it now draws the edges of the level below the micro level (quad
  diagonals left out, as on a mesh) on the micro surface (#128, reported by RodTDai).
added: The title bar's triangle count adds the micro triangles of the visible Parts while there are any
  ("20.5k + 5.24M micro"), and so does the classic look's ActiveTris readout (#125).
testers:
  - Any OS: Tool > Geometry > Divide to Micro on a sphere, Sharp Crease at Intensity 100 with a pen pressed hard:
    a clean deep groove, no speckled noise around it. Same with Crease and Pinch, small and large sizes.
  - Turn Wire on with the micro level: the base edges show on the sculpted surface and follow it; off again: gone.
  - The title bar reads "<triangles> + <count> micro"; Delete Micro: the micro part goes.
---
Reviewer note: the old micro sharpen was an anti-diffusion (each point pushed away from its neighbours' average),
which amplifies grid-scale detail every dab. tests/test_micro.cpp micro_crease_strokes_stay_smooth measures it
(roughness 0.276 -> 0.014 for Sharp Crease).
