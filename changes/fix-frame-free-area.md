---
whats_new: **Frame** (F) fits the model into the part of the canvas the open Library and the Sculpt Timeline leave free, and the left shelf's brush tiles and colour picker shrink to fit short screens.
fixed: **Frame** (F, the Frame button), a new model and **Reset View** fit the model into the canvas the open Library
  and the Sculpt Timeline strip leave free: at 200 % on a 1080p screen the sphere's top sat under them. Opening or
  closing the Library doesn't move the view; the next Frame uses the new free area.
fixed: The left shelf's brush tiles and colour picker shrink to fit its column (and the wheel scrolls it past that):
  at 150 % on a 1080p screen the colour swatches ran under the bottom shelf.
testers:
  - Windows 1080p at 200 %: open the Library (,), press F: the whole model shows below the Library and the Timeline; close the Library: the view stays put.
  - Windows 1080p at 150 %: every left shelf item (brush, stroke, alpha, material tiles, the colour picker and its swatches) is fully visible above the bottom shelf.
---
Lens shift on the camera (Camera::shift), set by frame(); benches, headless renders and --run at launch keep the plain centred frame.
