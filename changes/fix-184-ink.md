---
whats_new: **Size and Intensity follow the pen**: press S (or U) with a pen in range and move it above the tablet; the size follows live, a tap keeps it. Pressing straight down on the slider's knob still drags it.
fixed: With a graphics tablet, the S / U / O pop-up stood still while the pen moved above the tablet (it looked frozen) and only changed once the tip came down; a hovering pen now sets the value live like the mouse, and a tap keeps it without sculpting (#184, reported by @HG4Science).
testers:
  - Any OS with a pen tablet: hover over the model, press S, move the pen sideways above the tablet; the ring and the number follow at once. Tap: the size stays and nothing is sculpted.
  - The same, but put the tip straight down on the knob and drag: the knob drags as before, lifting closes it.
---
Also on #184: the Sharp Crease "other strokes" were the X-mirror copy (symmetry on, the mesh had no micro level, so
Keep Strokes Editable was not involved). New invariant tests pin that a stroke after undo changes nothing outside its
dabs and their mirror, on a plain dense sphere and on a micro level with the option on and off (byte-identical).
