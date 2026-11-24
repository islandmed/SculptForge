---
whats_new: **Stencil**: a stroke that starts where the Stencil's image is dark or off the model now says so in the status bar, and how to remove the Stencil.
changed: **Stencil** (Sculpt > Alpha): a stroke that begins where the image is dark or off the model shows "A Stencil is on: strokes reach only its light parts", with where to remove it (Alpha > Stencil, X). Strokes stayed held back with no sign why, which read as sculpting that had stopped.
testers:
  - Any OS: Sculpt > Alpha > Stencil > Load image, turn Adjust on, drag the image off the model, turn Adjust off, stroke on the model. The status bar says a Stencil is on; untick Show (or X) and the stroke sculpts.
---
tests/ui/stencil_adjust_off.sfui pins Adjust ending by its button, Enter, Esc and Esc mid-drag (mouse and pen): input returns to sculpting each time.
